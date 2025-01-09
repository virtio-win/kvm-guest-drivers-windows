/*
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "viofs.h"
#include "ioctl.tmh"

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))

EVT_WDF_REQUEST_CANCEL VirtFsEvtRequestCancel;

static inline int GetVirtQueueIndex(IN PDEVICE_CONTEXT Context, IN BOOLEAN HighPrio)
{
    int index = HighPrio ? VQ_TYPE_HIPRIO : VQ_TYPE_REQUEST;

    UNREFERENCED_PARAMETER(Context);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "VirtQueueIndex: %d", index);

    return index;
}

#if !VIRT_FS_DMAR
static SIZE_T GetRequiredScatterGatherSize(IN PVIRTIO_FS_REQUEST Request)
{
    SIZE_T n;

    n = DIV_ROUND_UP(Request->InputBufferLength, PAGE_SIZE) + DIV_ROUND_UP(Request->OutputBufferLength, PAGE_SIZE);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "Required SG Size: %Iu", n);

    return n;
}
#endif

static PMDL VirtFsAllocatePages(IN SIZE_T TotalBytes)
{
    PHYSICAL_ADDRESS low_addr;
    PHYSICAL_ADDRESS high_addr;
    PHYSICAL_ADDRESS skip_bytes;

    low_addr.QuadPart = 0;
    high_addr.QuadPart = -1;
    skip_bytes.QuadPart = 0;

    return MmAllocatePagesForMdlEx(low_addr,
                                   high_addr,
                                   skip_bytes,
                                   TotalBytes,
                                   MmNonCached,
                                   MM_DONT_ZERO_ALLOCATION | MM_ALLOCATE_FULLY_REQUIRED);
}

static int FillScatterGatherFromMdl(OUT struct scatterlist sg[], IN PMDL Mdl, IN size_t Length)
{
    PPFN_NUMBER pfn;
    ULONG total_pages;
    ULONG len;
    ULONG j;
    int i = 0;

    while (Mdl != NULL)
    {
        total_pages = MmGetMdlByteCount(Mdl) / PAGE_SIZE;
        pfn = MmGetMdlPfnArray(Mdl);
        for (j = 0; j < total_pages; j++)
        {
            len = (ULONG)(min(Length, PAGE_SIZE));
            Length -= len;
            sg[i].physAddr.QuadPart = (ULONGLONG)pfn[j] << PAGE_SHIFT;
            sg[i].length = len;
            i += 1;
        }
        Mdl = Mdl->Next;
    }

    return i;
}

#if !VIRT_FS_DMAR
static NTSTATUS VirtFsEnqueueRequest(IN PDEVICE_CONTEXT Context, IN PVIRTIO_FS_REQUEST Request, IN BOOLEAN HighPrio)
{
    WDFSPINLOCK vq_lock;
    struct virtqueue *vq;
    struct scatterlist *sg;
    size_t sg_size;
    int vq_index;
    int ret;
    int out_num, in_num;
    void *indirect_va = NULL;
    ULONGLONG indirect_pa = 0;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "--> %!FUNC!");

    vq_index = GetVirtQueueIndex(Context, HighPrio);
    vq = Context->VirtQueues[vq_index];
    vq_lock = Context->VirtQueueLocks[vq_index];

    sg_size = GetRequiredScatterGatherSize(Request);
    sg = ExAllocatePoolUninitialized(NonPagedPool, sg_size * sizeof(struct scatterlist), VIRT_FS_MEMORY_TAG);

    if (sg == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "Failed to allocate a %Iu items scatter-gatter list", sg_size);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    out_num = FillScatterGatherFromMdl(sg, Request->InputBuffer, Request->InputBufferLength);
    in_num = FillScatterGatherFromMdl(sg + out_num, Request->OutputBuffer, Request->OutputBufferLength);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "Push %p Request: %p", Request, Request->Request);

    WdfSpinLockAcquire(Context->RequestsLock);
    PushEntryList(&Context->RequestsList, &Request->ListEntry);
    WdfSpinLockRelease(Context->RequestsLock);

    if (2 < sg_size && sg_size <= VIRT_FS_INDIRECT_AREA_CAPACITY && Context->UseIndirect && !HighPrio)
    {
        indirect_va = Context->IndirectVA;
        indirect_pa = (ULONGLONG)Context->IndirectPA.QuadPart;
    }

    WdfSpinLockAcquire(vq_lock);
    ret = virtqueue_add_buf(vq, sg, out_num, in_num, Request, indirect_va, indirect_pa);
    if (ret < 0)
    {
        WdfSpinLockRelease(vq_lock);

        VirtFsDequeueRequest(Context, Request);

        ExFreePoolWithTag(sg, VIRT_FS_MEMORY_TAG);

        return STATUS_UNSUCCESSFUL;
    }

    WdfSpinLockRelease(vq_lock);
    ExFreePoolWithTag(sg, VIRT_FS_MEMORY_TAG);

    virtqueue_kick(vq);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}
#else

void FailFsRequest(IN PDEVICE_CONTEXT Context, IN PVIRTIO_FS_REQUEST Request)
{
    // failing FS request that already was queued but not sent
    WDFREQUEST wdfReq;
    TraceEvents(TRACE_LEVEL_ERROR,
                DBG_IOCTL,
                "%s: in %d, out %d",
                __FUNCTION__,
                Request->H2D_Params.size,
                Request->D2H_Params.size);
    if (Request->H2D_Params.transaction)
    {
        VirtIOWdfDeviceDmaTxComplete(&Context->VDevice.VIODevice, Request->H2D_Params.transaction);
        Request->H2D_Params.transaction = NULL;
    }
    if (Request->D2H_Params.transaction)
    {
        VirtIOWdfDeviceDmaRxComplete(&Context->VDevice.VIODevice, Request->D2H_Params.transaction, 0);
        Request->D2H_Params.transaction = NULL;
    }

    VirtFsDequeueRequest(Context, Request);
    wdfReq = Request->Request;

    if (wdfReq)
    {
        // TODO: why are we sure the wdfReq is valid and not destroyed yet?
        NTSTATUS status = WdfRequestUnmarkCancelable(wdfReq);
        if (status != STATUS_CANCELLED)
        {
            status = STATUS_UNSUCCESSFUL;
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "%s: completing %p with %X", __FUNCTION__, wdfReq, status);
            WdfRequestComplete(wdfReq, status);
        }
        else
        {
            // cancellation flow already started and will complete the WDF request
        }
    }
    FreeVirtFsRequest(Request);
}

static FORCEINLINE ULONG FragmentSize(PHYSICAL_ADDRESS Addr, ULONG Length)
{
    ULONG offset = Addr.LowPart & (PAGE_SIZE - 1);
    return min(PAGE_SIZE - offset, Length);
}

// split SG elements to fragments <= PAGE_SIZE and inside the same page
// with large SG elements the virtiofsd may fail to map the fragment (happens with 1M elements)
// DestSG = NULL for dry run (just calculate)
static ULONG PopulateSG(struct VirtIOBufferDescriptor *DestSG, PSCATTER_GATHER_LIST SrcSG)
{
    ULONG i, n = 0;
    for (i = 0; i < SrcSG->NumberOfElements; ++i)
    {
        PHYSICAL_ADDRESS pa = SrcSG->Elements[i].Address;
        ULONG len = SrcSG->Elements[i].Length;
        while (len)
        {
            ULONG current;
            current = FragmentSize(pa, len);
            if (DestSG)
            {
                DestSG[n].physAddr = pa;
                DestSG[n].length = current;
            }
            n++;
            if (current < len)
            {
                len -= current;
                pa.QuadPart += current;
            }
            else
            {
                len = 0;
            }
        }
    }
    return n;
}

static ULONG CalculateFragments(PSCATTER_GATHER_LIST SGList)
{
    return PopulateSG(NULL, SGList);
}

static BOOLEAN VirtioFsRxTransactionCallback(PVIRTIO_DMA_TRANSACTION_PARAMS Params)
{
    PDEVICE_CONTEXT context = Params->param1;
    PVIRTIO_FS_REQUEST fs_req = Params->param2;
    void *indirect_va = NULL;
    ULONGLONG indirect_pa = 0;
    ULONG sgNum, sgNumIn, sgNumOut;

    // save actual RX DMA data
    fs_req->D2H_Params.sgList = Params->sgList;
    fs_req->D2H_Params.transaction = Params->transaction;

    sgNumIn = CalculateFragments(fs_req->H2D_Params.sgList);
    sgNumOut = CalculateFragments(fs_req->D2H_Params.sgList);
    sgNum = sgNumIn + sgNumOut;
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "--> %s: %d + %d fragments", __FUNCTION__, sgNumIn, sgNumOut);
    if (sgNum > VIRT_FS_MAX_QUEUE_SIZE)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "--> %s: SG SIZE %d is too large", __FUNCTION__, sgNum);
        FailFsRequest(context, fs_req);
        return TRUE;
    }
#if 0
    if (sgNum > context->QueueSize) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "--> %s: SG SIZE %d is too large", __FUNCTION__, sgNum);
        FailFsRequest(context, fs_req);
        return TRUE;
    }
#endif
    fs_req->Use_Indirect = fs_req->Use_Indirect && 2 < sgNum && sgNum <= VIRT_FS_INDIRECT_AREA_CAPACITY;
    if (fs_req->Use_Indirect)
    {
        indirect_va = context->IndirectVA;
        indirect_pa = (ULONGLONG)context->IndirectPA.QuadPart;
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "%s: using indirect transfer", __FUNCTION__);
    }
    // populate fs_req->SGTable with SG elements
    sgNumIn = PopulateSG(fs_req->SGTable, fs_req->H2D_Params.sgList);
    sgNumOut = PopulateSG(fs_req->SGTable + sgNumIn, fs_req->D2H_Params.sgList);
    // push buffers to virtqueue
    WdfSpinLockAcquire(fs_req->VQ_Lock);
    int ret = virtqueue_add_buf(fs_req->VQ, fs_req->SGTable, sgNumIn, sgNumOut, fs_req, indirect_va, indirect_pa);
    WdfSpinLockRelease(fs_req->VQ_Lock);
    if (ret < 0)
    {
        FailFsRequest(context, fs_req);
    }
    else
    {
        virtqueue_kick(fs_req->VQ);
    }
    return TRUE;
}

static BOOLEAN VirtioFsTxTransactionCallback(PVIRTIO_DMA_TRANSACTION_PARAMS Params)
{
    PDEVICE_CONTEXT context = Params->param1;
    PVIRTIO_FS_REQUEST fs_req = Params->param2;

    // save actual TX DMA data
    fs_req->H2D_Params.sgList = Params->sgList;
    fs_req->H2D_Params.transaction = Params->transaction;

    if (!VirtIOWdfDeviceDmaRxAsync(&context->VDevice.VIODevice, &fs_req->D2H_Params, VirtioFsRxTransactionCallback))
    {
        FailFsRequest(context, fs_req);
    }
    return TRUE;
}

static NTSTATUS VirtFsEnqueueRequest(IN PDEVICE_CONTEXT Context, IN PVIRTIO_FS_REQUEST Request, IN BOOLEAN HighPrio)
{
    int vq_index;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "--> %!FUNC!");

    vq_index = GetVirtQueueIndex(Context, HighPrio);
    Request->VQ = Context->VirtQueues[vq_index];
    Request->VQ_Lock = Context->VirtQueueLocks[vq_index];
    Request->Use_Indirect = Context->UseIndirect && !HighPrio;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "Push %p Request: %p", Request, Request->Request);

    // save the request structure in request queue
    WdfSpinLockAcquire(Context->RequestsLock);
    PushEntryList(&Context->RequestsList, &Request->ListEntry);
    WdfSpinLockRelease(Context->RequestsLock);

    // initiate TX part of the DMA mapping
    if (VirtIOWdfDeviceDmaTxAsync(&Context->VDevice.VIODevice, &Request->H2D_Params, VirtioFsTxTransactionCallback))
    {
        status = STATUS_PENDING;
    }
    return status;
}
#endif

static VOID HandleGetVolumeName(IN PDEVICE_CONTEXT Context, IN WDFREQUEST Request, IN size_t OutputBufferLength)
{
    NTSTATUS status;
    WCHAR WideTag[MAX_FILE_SYSTEM_NAME + 1];
    ULONG WideTagActualSize;
    BYTE *out_buf;
    char tag[MAX_FILE_SYSTEM_NAME];
    size_t size;

    RtlZeroMemory(WideTag, sizeof(WideTag));

    VirtIOWdfDeviceGet(&Context->VDevice, FIELD_OFFSET(VIRTIO_FS_CONFIG, Tag), &tag, sizeof(tag));

    status = RtlUTF8ToUnicodeN(WideTag, sizeof(WideTag), &WideTagActualSize, tag, sizeof(tag));

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "Failed to convert config tag: %!STATUS!", status);
        status = STATUS_SUCCESS;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_POWER, "Config tag: %s Tag: %S", tag, WideTag);
    }

    size = (wcslen(WideTag) + 1) * sizeof(WCHAR);

    if (OutputBufferLength < size)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "Insufficient out buffer");
        WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        return;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, size, &out_buf, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "WdfRequestRetrieveOutputBuffer failed");
        WdfRequestComplete(Request, status);
        return;
    }

    RtlCopyMemory(out_buf, WideTag, size);
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, size);
}

void CopyBuffer(void *_Dst, void const *_Src, size_t _Size)
{
    RtlCopyMemory(_Dst, _Src, _Size);
}

static inline BOOLEAN VirtFsOpcodeIsHighPrio(IN UINT32 Opcode)
{
    return (Opcode == FUSE_FORGET) || (Opcode == FUSE_INTERRUPT) || (Opcode == FUSE_BATCH_FORGET);
}

static VOID HandleSubmitFuseRequest(IN PDEVICE_CONTEXT Context,
                                    IN WDFREQUEST Request,
                                    IN size_t OutputBufferLength,
                                    IN size_t InputBufferLength)
{
    WDFMEMORY handle;
    NTSTATUS status;
    PVIRTIO_FS_REQUEST fs_req;
    PVOID in_buf_va;
    PVOID in_buf, out_buf;
    BOOLEAN hiprio;

    UNREFERENCED_PARAMETER(in_buf_va);

    if (InputBufferLength < sizeof(struct fuse_in_header))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "Insufficient in buffer");
        status = STATUS_BUFFER_TOO_SMALL;
        goto complete_wdf_req_no_fs_req;
    }

    if (OutputBufferLength < sizeof(struct fuse_out_header))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "Insufficient out buffer");
        status = STATUS_BUFFER_TOO_SMALL;
        goto complete_wdf_req_no_fs_req;
    }

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &in_buf, NULL);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "WdfRequestRetrieveInputBuffer failed");
        goto complete_wdf_req_no_fs_req;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &out_buf, NULL);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "WdfRequestRetrieveOutputBuffer failed");
        goto complete_wdf_req_no_fs_req;
    }

    status = WdfMemoryCreateFromLookaside(Context->RequestsLookaside, &handle);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "WdfMemoryCreateFromLookaside failed");
        goto complete_wdf_req_no_fs_req;
    }

    fs_req = WdfMemoryGetBuffer(handle, NULL);
    fs_req->Handle = handle;
    fs_req->Request = Request;
#if !VIRT_FS_DMAR
    fs_req->InputBuffer = VirtFsAllocatePages(InputBufferLength);
    fs_req->InputBufferLength = InputBufferLength;
    fs_req->OutputBuffer = VirtFsAllocatePages(OutputBufferLength);
    fs_req->OutputBufferLength = OutputBufferLength;

    if ((fs_req->InputBuffer == NULL) || (fs_req->OutputBuffer == NULL))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "Data allocation failed");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto complete_wdf_req;
    }

    in_buf_va = MmMapLockedPagesSpecifyCache(fs_req->InputBuffer,
                                             KernelMode,
                                             MmNonCached,
                                             NULL,
                                             FALSE,
                                             NormalPagePriority);

    if (in_buf_va == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "MmMapLockedPages failed");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto complete_wdf_req;
    }

    CopyBuffer(in_buf_va, in_buf, InputBufferLength);
    MmUnmapLockedPages(in_buf_va, fs_req->InputBuffer);
#else
    RtlZeroMemory(&fs_req->H2D_Params, sizeof(fs_req->H2D_Params));
    RtlZeroMemory(&fs_req->D2H_Params, sizeof(fs_req->D2H_Params));

    fs_req->H2D_Params.allocationTag = VIRT_FS_MEMORY_TAG;
    fs_req->H2D_Params.buffer = in_buf;
    fs_req->H2D_Params.size = (ULONG)InputBufferLength;
    fs_req->H2D_Params.param1 = Context;
    fs_req->H2D_Params.param2 = fs_req;

    fs_req->D2H_Params.allocationTag = VIRT_FS_MEMORY_TAG;
    fs_req->D2H_Params.buffer = out_buf;
    fs_req->D2H_Params.size = (ULONG)OutputBufferLength;
    fs_req->D2H_Params.param1 = Context;
    fs_req->D2H_Params.param2 = fs_req;
#endif
    status = WdfRequestMarkCancelableEx(Request, VirtFsEvtRequestCancel);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "WdfRequestMarkCancelableEx failed: %!STATUS!", status);
        goto complete_wdf_req;
    }

    hiprio = VirtFsOpcodeIsHighPrio(((struct fuse_in_header *)in_buf)->opcode);

    status = VirtFsEnqueueRequest(Context, fs_req, hiprio);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "VirtFsEnqueueRequest failed: %!STATUS!", status);
        status = WdfRequestUnmarkCancelable(Request);
        __analysis_assume(status != STATUS_NOT_SUPPORTED);
        if (status != STATUS_CANCELLED)
        {
            goto complete_wdf_req;
        }
    }

    return;

complete_wdf_req:
    FreeVirtFsRequest(fs_req);

complete_wdf_req_no_fs_req:
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "Complete Request: %p Status: %!STATUS!", Request, status);
    WdfRequestComplete(Request, status);
}

VOID VirtFsEvtIoDeviceControl(IN WDFQUEUE Queue,
                              IN WDFREQUEST Request,
                              IN size_t OutputBufferLength,
                              IN size_t InputBufferLength,
                              IN ULONG IoControlCode)
{
    PDEVICE_CONTEXT context = GetDeviceContext(WdfIoQueueGetDevice(Queue));

    TraceEvents(TRACE_LEVEL_VERBOSE,
                DBG_IOCTL,
                "--> %!FUNC! Queue: %p Request: %p IoCtrl: %x InLen: %Iu OutLen: %Iu",
                Queue,
                Request,
                IoControlCode,
                InputBufferLength,
                OutputBufferLength);

    switch (IoControlCode)
    {
        case IOCTL_VIRTFS_GET_VOLUME_NAME:
            HandleGetVolumeName(context, Request, OutputBufferLength);
            break;

        case IOCTL_VIRTFS_FUSE_REQUEST:
            HandleSubmitFuseRequest(context, Request, OutputBufferLength, InputBufferLength);
            break;

        default:
            WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
            break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "<-- %!FUNC!");
}

VOID VirtFsEvtIoStop(IN WDFQUEUE Queue, IN WDFREQUEST Request, IN ULONG ActionFlags)
{
    PDEVICE_CONTEXT context = GetDeviceContext(WdfIoQueueGetDevice(Queue));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "--> %!FUNC! Request: %p ActionFlags: 0x%08x", Request, ActionFlags);

    if (ActionFlags & WdfRequestStopRequestCancelable)
    {
        NTSTATUS status = WdfRequestUnmarkCancelable(Request);
        __analysis_assume(status != STATUS_NOT_SUPPORTED);
        if (status == STATUS_CANCELLED)
        {
            WdfRequestStopAcknowledge(Request, FALSE);
            goto end_io_stop;
        }
    }

    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        WdfRequestStopAcknowledge(Request, TRUE);
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {
        VirtFsDequeueWdfRequest(context, Request);

        WdfRequestComplete(Request, STATUS_CANCELLED);
    }
    else
    {
        WdfRequestComplete(Request, STATUS_UNSUCCESSFUL);
    }

end_io_stop:
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "<-- %!FUNC!");
}

VOID VirtFsEvtRequestCancel(IN WDFREQUEST Request)
{
    PDEVICE_CONTEXT context = GetDeviceContext(WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request)));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "--> %!FUNC! Cancelled Request: %p", Request);

    if (!VirtFsDequeueWdfRequest(context, Request))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTL, "--> %!FUNC! the request %p is not found", Request);
    }
    WdfRequestComplete(Request, STATUS_CANCELLED);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "<-- %!FUNC!");
}

// find respective queue entry and clear WDF request in it
// the PVIRTIO_FS_REQUEST structure stays in the list
BOOLEAN VirtFsDequeueWdfRequest(PDEVICE_CONTEXT Context, WDFREQUEST WdfRequest)
{
    PSINGLE_LIST_ENTRY iter;
    BOOLEAN found = FALSE;
    WdfSpinLockAcquire(Context->RequestsLock);
    iter = &Context->RequestsList;
    while (iter->Next != NULL)
    {
        PVIRTIO_FS_REQUEST entry = CONTAINING_RECORD(iter->Next, VIRTIO_FS_REQUEST, ListEntry);

        if (WdfRequest == entry->Request)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "Clear virtio fs request %p", entry);
            entry->Request = NULL;
            found = TRUE;
            break;
        }

        iter = iter->Next;
    };
    WdfSpinLockRelease(Context->RequestsLock);
    return found;
}
