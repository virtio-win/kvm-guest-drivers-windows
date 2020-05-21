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

EVT_WDF_REQUEST_CANCEL VirtFsEvtRequestCancel;

static int GetVirtQueueIndex(IN PDEVICE_CONTEXT Context)
{
    int index = 0;

    UNREFERENCED_PARAMETER(Context);
    
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "VirtQueueIndex: %d", index);
    
    return index;
}

static SIZE_T GetRequiredScatterGatherSize(IN PVIRTIO_FS_REQUEST Request)
{
    SIZE_T n;
    
    n = ((Request->InputBufferLength / PAGE_SIZE) + 1) + 
        ((Request->OutputBufferLength / PAGE_SIZE) + 1);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "Required SG Size: %Iu", n);

    return n;
}

static PMDL VirtFsAllocatePages(IN SIZE_T TotalBytes)
{
    PHYSICAL_ADDRESS low_addr;
    PHYSICAL_ADDRESS high_addr;
    PHYSICAL_ADDRESS skip_bytes;

    low_addr.QuadPart = 0;
    high_addr.QuadPart = -1;
    skip_bytes.QuadPart = 0;

    return MmAllocatePagesForMdlEx(low_addr, high_addr, skip_bytes,
        TotalBytes, MmNonCached,
        MM_DONT_ZERO_ALLOCATION | MM_ALLOCATE_FULLY_REQUIRED);
}

static int FillScatterGatherFromMdl(OUT struct scatterlist sg[],
                                    IN PMDL Mdl,
                                    IN size_t Length)
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
            sg[i].physAddr.QuadPart = (ULONGLONG)(*(pfn + j)) << PAGE_SHIFT;
            sg[i].length = len;
            i += 1;
        }
        Mdl = Mdl->Next;
    }

    return i;
}

static NTSTATUS VirtFsEnqueueRequest(IN PDEVICE_CONTEXT Context,
                                     IN PVIRTIO_FS_REQUEST Request)
{
    WDFSPINLOCK vq_lock;
    struct virtqueue *vq;
    struct scatterlist *sg;
    size_t sg_size;
    int vq_index;
    int ret;
    int out_num, in_num;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "--> %!FUNC!");

    vq_index = GetVirtQueueIndex(Context);
    vq = Context->VirtQueues[vq_index];
    vq_lock = Context->VirtQueueLocks[vq_index];

    sg_size = GetRequiredScatterGatherSize(Request);
    sg = ExAllocatePoolWithTag(NonPagedPool,
        sg_size * sizeof(struct scatterlist), VIRT_FS_MEMORY_TAG);

    if (sg == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
            "Failed to allocate a %Iu items scatter-gatter list", sg_size);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    out_num = FillScatterGatherFromMdl(sg, Request->InputBuffer,
        Request->InputBufferLength);
    in_num = FillScatterGatherFromMdl(sg + out_num, Request->OutputBuffer,
        Request->OutputBufferLength);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "Push %p Request: %p",
        Request, Request->Request);

    WdfSpinLockAcquire(Context->RequestsLock);
    PushEntryList(&Context->RequestsList, &Request->ListEntry);
    WdfSpinLockRelease(Context->RequestsLock);

    WdfSpinLockAcquire(vq_lock);
    ret = virtqueue_add_buf(vq, sg, out_num, in_num, Request, NULL, 0);
    if (ret < 0)
    {
        PSINGLE_LIST_ENTRY iter;

        WdfSpinLockRelease(vq_lock);

        WdfSpinLockAcquire(Context->RequestsLock);
        iter = &Context->RequestsList;
        while (iter->Next != NULL)
        {
            PVIRTIO_FS_REQUEST removed = CONTAINING_RECORD(iter->Next,
                VIRTIO_FS_REQUEST, ListEntry);

            if (Request == removed)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC,
                    "Delete %p Request: %p", removed, removed->Request);
                iter->Next = removed->ListEntry.Next;
                break;
            }

            iter = iter->Next;
        };
        WdfSpinLockRelease(Context->RequestsLock);
        
        ExFreePoolWithTag(sg, VIRT_FS_MEMORY_TAG);

        return STATUS_UNSUCCESSFUL;
    }

    WdfSpinLockRelease(vq_lock);
    ExFreePoolWithTag(sg, VIRT_FS_MEMORY_TAG);

    virtqueue_kick(vq);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

static VOID HandleGetVolumeName(IN PDEVICE_CONTEXT Context,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength)
{
    NTSTATUS status;
    WCHAR WideTag[MAX_FILE_SYSTEM_NAME + 1];
    BYTE *out_buf;
    char tag[MAX_FILE_SYSTEM_NAME];
    size_t size;

    RtlZeroMemory(WideTag, sizeof(WideTag));

    VirtIOWdfDeviceGet(&Context->VDevice,
        FIELD_OFFSET(VIRTIO_FS_CONFIG, Tag),
        &tag, sizeof(tag));

    status = RtlUTF8ToUnicodeN(WideTag, sizeof(WideTag), NULL,
        tag, sizeof(tag));

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER,
            "Failed to convert config tag: %!STATUS!", status);
        status = STATUS_SUCCESS;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_POWER,
            "Config tag: %s Tag: %S", tag, WideTag);
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
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
            "WdfRequestRetrieveOutputBuffer failed");
        WdfRequestComplete(Request, status);
        return;
    }

    RtlCopyMemory(out_buf, WideTag, size);
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, size);
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
    PUCHAR in_buf, out_buf;

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

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength,
        &in_buf, NULL);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
            "WdfRequestRetrieveInputBuffer failed");
        goto complete_wdf_req_no_fs_req;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength,
        &out_buf, NULL);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
            "WdfRequestRetrieveOutputBuffer failed");
        goto complete_wdf_req_no_fs_req;
    }

    status = WdfMemoryCreateFromLookaside(Context->RequestsLookaside,
        &handle);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
            "WdfMemoryCreateFromLookaside failed");
        goto complete_wdf_req_no_fs_req;
    }

    fs_req = WdfMemoryGetBuffer(handle, NULL);
    fs_req->Handle = handle;
    fs_req->Request = Request;
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

    in_buf_va = MmMapLockedPagesSpecifyCache(fs_req->InputBuffer, KernelMode,
        MmNonCached, NULL, FALSE, NormalPagePriority);

    if (in_buf_va == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "MmMapLockedPages failed");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto complete_wdf_req;
    }

    RtlCopyMemory(in_buf_va, in_buf, InputBufferLength);
    MmUnmapLockedPages(in_buf_va, fs_req->InputBuffer);

    status = WdfRequestMarkCancelableEx(Request, VirtFsEvtRequestCancel);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
            "WdfRequestMarkCancelableEx failed: %!STATUS!", status);
        goto complete_wdf_req;
    }

    status = VirtFsEnqueueRequest(Context, fs_req);
    if (!NT_SUCCESS(status))
    {
        if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            goto complete_wdf_req;
        }
    }

    return;

complete_wdf_req:
    FreeVirtFsRequest(fs_req);

complete_wdf_req_no_fs_req:
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL,
        "Complete Request: %p Status: %!STATUS!", Request, status);
    WdfRequestComplete(Request, status);
}

VOID VirtFsEvtIoDeviceControl(IN WDFQUEUE Queue,
                              IN WDFREQUEST Request,
                              IN size_t OutputBufferLength,
                              IN size_t InputBufferLength,
                              IN ULONG IoControlCode)
{
    PDEVICE_CONTEXT context = GetDeviceContext(WdfIoQueueGetDevice(Queue));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL,
        "--> %!FUNC! Queue: %p Request: %p IoCtrl: %x InLen: %Iu OutLen: %Iu",
        Queue, Request, IoControlCode, InputBufferLength, OutputBufferLength);

    switch (IoControlCode)
    {
        case IOCTL_VIRTFS_GET_VOLUME_NAME:
            HandleGetVolumeName(context, Request, OutputBufferLength);
            break;

        case IOCTL_VIRTFS_FUSE_REQUEST:
            HandleSubmitFuseRequest(context, Request, OutputBufferLength,
                InputBufferLength);
            break;

        default:
            WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
            break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "<-- %!FUNC!");
}

VOID VirtFsEvtIoStop(IN WDFQUEUE Queue,
                     IN WDFREQUEST Request,
                     IN ULONG ActionFlags)
{
    PDEVICE_CONTEXT context = GetDeviceContext(WdfIoQueueGetDevice(Queue));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL,
        "--> %!FUNC! Request: %p ActionFlags: 0x%08x", Request, ActionFlags);

    if (ActionFlags & WdfRequestStopRequestCancelable)
    {
        if (WdfRequestUnmarkCancelable(Request) == STATUS_CANCELLED)
        {
            goto request_cancelled;
        }
    }

    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        WdfRequestStopAcknowledge(Request, FALSE);
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {
        PSINGLE_LIST_ENTRY iter;

        WdfSpinLockAcquire(context->RequestsLock);
        iter = &context->RequestsList;
        while (iter->Next != NULL)
        {
            PVIRTIO_FS_REQUEST removed = CONTAINING_RECORD(iter->Next,
                VIRTIO_FS_REQUEST, ListEntry);

            if (Request == removed->Request)
            {
                removed->Request = NULL;
                break;
            }

            iter = iter->Next;
        };
        WdfSpinLockRelease(context->RequestsLock);

        WdfRequestComplete(Request, STATUS_CANCELLED);
    }

request_cancelled:
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "<-- %!FUNC!");
}

VOID VirtFsEvtRequestCancel(IN WDFREQUEST Request)
{
    PDEVICE_CONTEXT context = GetDeviceContext(WdfIoQueueGetDevice(
        WdfRequestGetIoQueue(Request)));
    PSINGLE_LIST_ENTRY iter;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL,
        "--> %!FUNC! Cancelled Request: %p", Request);
    
    WdfRequestComplete(Request, STATUS_CANCELLED);

    WdfSpinLockAcquire(context->RequestsLock);
    iter = &context->RequestsList;
    while (iter->Next != NULL)
    {
        PVIRTIO_FS_REQUEST entry = CONTAINING_RECORD(iter->Next,
            VIRTIO_FS_REQUEST, ListEntry);

        if (Request == entry->Request)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL,
                "Clear virtio fs request %p", entry);
            entry->Request = NULL;
            break;
        }

        iter = iter->Next;
    };
    WdfSpinLockRelease(context->RequestsLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "<-- %!FUNC!");
}
