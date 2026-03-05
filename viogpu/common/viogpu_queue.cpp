/*
 * Copyright (C) 2019-2020 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
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

#include "viogpu_queue.h"
#include "baseobj.h"
#if !DBG
#include "viogpu_queue.tmh"
#endif

static BOOLEAN BuildSGElement(VirtIOBufferDescriptor *sg, PVOID buf, ULONG size)
{
    if (size != 0 && MmIsAddressValid(buf))
    {
        sg->length = min(size, PAGE_SIZE);
        sg->physAddr = MmGetPhysicalAddress(buf);
        return TRUE;
    }
    return FALSE;
}

// Contiguous memory block sizes for fallback allocation strategy
static SIZE_T g_ContiguousBlockSizes[] = {1024 * 1024, 64 * 1024, 32 * 1024, 16 * 1024, PAGE_SIZE};

// Helper function to try allocating contiguous memory with fallback sizes
// Uses g_ContiguousBlockSizes array (1MB -> 64KB -> 32KB -> 16KB -> 4KB)
static PVOID AllocateContiguousWithFallback(SIZE_T requestedSize,
                                            SIZE_T *actualSize,
                                            PHYSICAL_ADDRESS highestAcceptable)
{
    UINT blockSizeCount = ARRAYSIZE(g_ContiguousBlockSizes);
    for (UINT i = 0; i < blockSizeCount; i++)
    {
        SIZE_T trySize = min(requestedSize, g_ContiguousBlockSizes[i]);
        PVOID ptr = MmAllocateContiguousMemory(trySize, highestAcceptable);
        if (ptr)
        {
            *actualSize = trySize;
            return ptr;
        }
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("AllocateContiguousWithFallback: failed size=%llu, trying smaller\n", (ULONGLONG)trySize));
    }

    *actualSize = 0;
    return NULL;
}

static void NotifyEventCompleteCB(void *ctx)
{
    KeSetEvent((PKEVENT)ctx, IO_NO_INCREMENT, FALSE);
}

VioGpuQueue::VioGpuQueue()
{
    m_pBuf = NULL;
    m_Index = (UINT)-1;
    m_pVIODevice = NULL;
    m_pVirtQueue = NULL;
    KeInitializeSpinLock(&m_SpinLock);
}

VioGpuQueue::~VioGpuQueue()
{
    Close();
}

void VioGpuQueue::Close(void)
{
    KIRQL SavedIrql;
    Lock(&SavedIrql);
    m_pVirtQueue = NULL;
    Unlock(SavedIrql);
}

BOOLEAN VioGpuQueue::Init(_In_ VirtIODevice *pVIODevice, _In_ struct virtqueue *pVirtQueue, _In_ UINT index)
{
    if ((pVIODevice == NULL) || (pVirtQueue == NULL))
    {
        return FALSE;
    }
    m_pVIODevice = pVIODevice;
    m_pVirtQueue = pVirtQueue;
    m_Index = index;
    EnableInterrupt();
    return TRUE;
}

_IRQL_requires_max_(DISPATCH_LEVEL) _IRQL_saves_global_(OldIrql, Irql) _IRQL_raises_(DISPATCH_LEVEL) void VioGpuQueue::
                                                                                                    Lock(KIRQL *Irql)
{
    KIRQL SavedIrql = KeGetCurrentIrql();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s at IRQL %d\n", __FUNCTION__, SavedIrql));

    if (SavedIrql < DISPATCH_LEVEL)
    {
        KeAcquireSpinLock(&m_SpinLock, &SavedIrql);
    }
    else if (SavedIrql == DISPATCH_LEVEL)
    {
        KeAcquireSpinLockAtDpcLevel(&m_SpinLock);
    }
    else
    {
        // This is possible situation in case of bugcheck.
        // DxgkDdiSystemDisplayEnable can be called at any IRQL.
        // We need to allocate buffer for several command during this proccess.
        // VioGpuDbgBreak();
    }
    *Irql = SavedIrql;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

_IRQL_requires_(DISPATCH_LEVEL) _IRQL_restores_global_(OldIrql, Irql) void VioGpuQueue::Unlock(KIRQL Irql)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s at IRQL %d\n", __FUNCTION__, Irql));

    if (Irql < DISPATCH_LEVEL)
    {
        KeReleaseSpinLock(&m_SpinLock, Irql);
    }
    else if (Irql == DISPATCH_LEVEL)
    {
        KeReleaseSpinLockFromDpcLevel(&m_SpinLock);
    }
    else
    {
        // This is possible situation in case of bugcheck.
        // DxgkDdiSystemDisplayEnable can be called at any IRQL.
        // We need to allocate buffer for several command during this proccess.
        // VioGpuDbgBreak();
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PAGED_CODE_SEG_BEGIN

UINT VioGpuQueue::QueryAllocation()
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    USHORT NumEntries;
    ULONG RingSize, HeapSize;

    NTSTATUS status = virtio_query_queue_allocation(m_pVIODevice, m_Index, &NumEntries, &RingSize, &HeapSize);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("[%s] virtio_query_queue_allocation(%d) failed with error %x\n", __FUNCTION__, m_Index, status));
        return 0;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return NumEntries;
}
PAGED_CODE_SEG_END

PAGED_CODE_SEG_BEGIN

BOOLEAN CtrlQueue::GetDisplayInfo(PGPU_VBUFFER buf, UINT id, PULONG xres, PULONG yres)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_RESP_DISP_INFO resp = (PGPU_RESP_DISP_INFO)buf->resp_buf;
    if (resp->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
    {
        DbgPrint(TRACE_LEVEL_VERBOSE, (" %s type = %x: disabled\n", __FUNCTION__, resp->hdr.type));
        return FALSE;
    }
    if (resp->pmodes[id].enabled)
    {
        DbgPrint(TRACE_LEVEL_VERBOSE,
                 ("output %d: %dx%d+%d+%d\n",
                  id,
                  resp->pmodes[id].r.width,
                  resp->pmodes[id].r.height,
                  resp->pmodes[id].r.x,
                  resp->pmodes[id].r.y));
        *xres = resp->pmodes[id].r.width;
        *yres = resp->pmodes[id].r.height;
    }
    else
    {
        DbgPrint(TRACE_LEVEL_VERBOSE, ("output %d: disabled\n", id));
        return FALSE;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return TRUE;
}

BOOLEAN CtrlQueue::AskDisplayInfo(PGPU_VBUFFER *buf)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_CTRL_HDR cmd;
    PGPU_VBUFFER vbuf;
    PGPU_RESP_DISP_INFO resp_buf;
    KEVENT event;
    NTSTATUS status;

    resp_buf = reinterpret_cast<PGPU_RESP_DISP_INFO>(new (NonPagedPoolNx) BYTE[sizeof(GPU_RESP_DISP_INFO)]);

    if (!resp_buf)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("---> %s Failed allocate %d bytes\n", __FUNCTION__, sizeof(GPU_RESP_DISP_INFO)));
        return FALSE;
    }

    cmd = (PGPU_CTRL_HDR)AllocCmdResp(&vbuf, sizeof(GPU_CTRL_HDR), resp_buf, sizeof(GPU_RESP_DISP_INFO));
    RtlZeroMemory(cmd, sizeof(GPU_CTRL_HDR));

    cmd->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    vbuf->complete_cb = NotifyEventCompleteCB;
    vbuf->complete_ctx = &event;
    vbuf->auto_release = false;

    LARGE_INTEGER timeout = {0};
    timeout.QuadPart = Int32x32To64(1000, -10000);

    QueueBuffer(vbuf);
    status = KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, &timeout);

    if (status == STATUS_TIMEOUT)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("---> Failed to ask display info\n"));
        VioGpuDbgBreak();
    }
    *buf = vbuf;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return TRUE;
}

BOOLEAN CtrlQueue::AskEdidInfo(PGPU_VBUFFER *buf, UINT id)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_CMD_GET_EDID cmd;
    PGPU_VBUFFER vbuf;
    PGPU_RESP_EDID resp_buf;
    KEVENT event;
    NTSTATUS status;

    resp_buf = reinterpret_cast<PGPU_RESP_EDID>(new (NonPagedPoolNx) BYTE[sizeof(GPU_RESP_EDID)]);

    if (!resp_buf)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("---> %s Failed allocate %d bytes\n", __FUNCTION__, sizeof(GPU_RESP_EDID)));
        return FALSE;
    }
    cmd = (PGPU_CMD_GET_EDID)AllocCmdResp(&vbuf, sizeof(GPU_CMD_GET_EDID), resp_buf, sizeof(GPU_RESP_EDID));
    RtlZeroMemory(cmd, sizeof(GPU_CMD_GET_EDID));

    cmd->hdr.type = VIRTIO_GPU_CMD_GET_EDID;
    cmd->scanout = id;

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    vbuf->complete_cb = NotifyEventCompleteCB;
    vbuf->complete_ctx = &event;
    vbuf->auto_release = false;

    LARGE_INTEGER timeout = {0};
    timeout.QuadPart = Int32x32To64(1000, -10000);

    QueueBuffer(vbuf);

    status = KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, &timeout);

    if (status == STATUS_TIMEOUT)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("---> Failed to get edid info\n"));
        VioGpuDbgBreak();
    }

    *buf = vbuf;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return TRUE;
}

BOOLEAN CtrlQueue::GetEdidInfo(PGPU_VBUFFER buf, UINT id, PBYTE edid)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    PGPU_CMD_GET_EDID cmd = (PGPU_CMD_GET_EDID)buf->buf;
    PGPU_RESP_EDID resp = (PGPU_RESP_EDID)buf->resp_buf;
    PUCHAR resp_edit = (PUCHAR)(resp->edid + (ULONGLONG)id * EDID_V1_BLOCK_SIZE);
    if (resp->hdr.type != VIRTIO_GPU_RESP_OK_EDID)
    {
        DbgPrint(TRACE_LEVEL_VERBOSE, (" %s type = %x: disabled\n", __FUNCTION__, resp->hdr.type));
        return FALSE;
    }
    if (cmd->scanout != id)
    {
        DbgPrint(TRACE_LEVEL_VERBOSE, (" %s invalid scaout = %x\n", __FUNCTION__, cmd->scanout));
        return FALSE;
    }

    RtlCopyMemory(edid, resp_edit, EDID_RAW_BLOCK_SIZE);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    return TRUE;
}

void CtrlQueue::CreateResource(UINT res_id, UINT format, UINT width, UINT height)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_RES_CREATE_2D cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_CREATE_2D)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd->resource_id = res_id;
    cmd->format = format;
    cmd->width = width;
    cmd->height = height;

    // FIXME!!! if
    QueueBuffer(vbuf);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void CtrlQueue::ResFlush(UINT res_id, UINT width, UINT height, UINT x, UINT y)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    PGPU_RES_FLUSH cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_FLUSH)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd->resource_id = res_id;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->r.x = x;
    cmd->r.y = y;

    QueueBuffer(vbuf);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void CtrlQueue::TransferToHost2D(UINT res_id, ULONG offset, UINT width, UINT height, UINT x, UINT y)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    PGPU_RES_TRANSF_TO_HOST_2D cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_TRANSF_TO_HOST_2D)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd->resource_id = res_id;
    cmd->offset = offset;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->r.x = x;
    cmd->r.y = y;

    QueueBuffer(vbuf);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void CtrlQueue::AttachBacking(UINT res_id, PGPU_MEM_ENTRY ents, UINT nents)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    // QEMU virtio_gpu_create_mapping_iov() rejects nr_entries > 16384
    ASSERT(nents <= VIRTIO_GPU_MAX_BACKING_ENTRIES);

    PGPU_RES_ATTACH_BACKING cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_ATTACH_BACKING)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->resource_id = res_id;
    cmd->nr_entries = nents;

    vbuf->data_buf = ents;
    vbuf->data_size = sizeof(*ents) * nents;
    vbuf->use_indirect = true;

    QueueBuffer(vbuf);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PAGED_CODE_SEG_END

void CtrlQueue::DestroyResource(UINT res_id)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_RES_UNREF cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_UNREF)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    cmd->resource_id = res_id;

    QueueBuffer(vbuf);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void CtrlQueue::DetachBacking(UINT res_id)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_RES_DETACH_BACKING cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_DETACH_BACKING)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
    cmd->resource_id = res_id;

    QueueBuffer(vbuf);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PVOID CtrlQueue::AllocCmdResp(PGPU_VBUFFER *buf, int cmd_sz, PVOID resp_buf, int resp_sz)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER vbuf;
    vbuf = m_pBuf->GetBuf(cmd_sz, resp_sz, resp_buf);
    ASSERT(vbuf);
    *buf = vbuf;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return vbuf ? vbuf->buf : NULL;
}

PVOID CtrlQueue::AllocCmd(PGPU_VBUFFER *buf, int sz)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (buf == NULL || sz == 0)
    {
        return NULL;
    }

    PGPU_VBUFFER vbuf = m_pBuf->GetBuf(sz, sizeof(GPU_CTRL_HDR), NULL);
    ASSERT(vbuf);
    *buf = vbuf;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s  vbuf = %p\n", __FUNCTION__, vbuf));

    return vbuf ? vbuf->buf : NULL;
}

void CtrlQueue::SetScanout(UINT scan_id, UINT res_id, UINT width, UINT height, UINT x, UINT y)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_SET_SCANOUT cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_SET_SCANOUT)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd->resource_id = res_id;
    cmd->scanout_id = scan_id;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->r.x = x;
    cmd->r.y = y;

    // FIXME if
    QueueBuffer(vbuf);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

#define SGLIST_SIZE 256
UINT CtrlQueue::QueueBuffer(PGPU_VBUFFER buf)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    // Allocate indirect descriptors if requested
    if (buf->use_indirect)
    {
        if (!m_pBuf->AllocateIndirectDescriptors(buf, buf->data_size))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("<--> %s failed to allocate indirect descriptors\n", __FUNCTION__));
            return 0;
        }
    }

    VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT sgleft = SGLIST_SIZE;
    UINT outcnt = 0, incnt = 0;
    UINT ret = 0;
    KIRQL SavedIrql;

    if (buf->size > PAGE_SIZE)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--> %s size is too big %d\n", __FUNCTION__, buf->size));
        return 0;
    }

    if (BuildSGElement(&sg[outcnt + incnt], (PVOID)buf->buf, buf->size))
    {
        outcnt++;
        sgleft--;
    }

    if (buf->data_size)
    {
        ULONG data_size = buf->data_size;
        PVOID data_buf = (PVOID)buf->data_buf;
        while (data_size)
        {
            if (BuildSGElement(&sg[outcnt + incnt], data_buf, data_size))
            {
                data_buf = (PVOID)((LONG_PTR)(data_buf) + PAGE_SIZE);
                data_size -= min(data_size, PAGE_SIZE);
                outcnt++;
                sgleft--;
                if (sgleft == 0)
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("<--> %s no more sgelenamt spots left %d\n", __FUNCTION__, outcnt));
                    return 0;
                }
            }
        }
    }

    if (buf->resp_size > PAGE_SIZE)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--> %s resp_size is too big %d\n", __FUNCTION__, buf->resp_size));
        return 0;
    }

    if (buf->resp_size && (sgleft > 0))
    {
        if (BuildSGElement(&sg[outcnt + incnt], (PVOID)buf->resp_buf, buf->resp_size))
        {
            incnt++;
            sgleft--;
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s sgleft %d\n", __FUNCTION__, sgleft));

    Lock(&SavedIrql);
    ret = AddBuf(&sg[0], outcnt, incnt, buf, buf->desc, buf->desc_pa.QuadPart);
    Kick();
    Unlock(SavedIrql);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s ret = %d\n", __FUNCTION__, ret));

    return ret;
}

PGPU_VBUFFER CtrlQueue::DequeueBuffer(_Out_ UINT *len)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER buf = NULL;
    KIRQL SavedIrql;
    Lock(&SavedIrql);
    buf = (PGPU_VBUFFER)GetBuf(len);
    Unlock(SavedIrql);
    if (buf == NULL)
    {
        *len = 0;
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return buf;
}

void VioGpuQueue::ReleaseBuffer(PGPU_VBUFFER buf)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    m_pBuf->FreeBuf(buf);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuBuf::AllocateIndirectDescriptors(_In_ PGPU_VBUFFER pbuf, _In_ SIZE_T dataSize)
{
    // Calculate descriptor table size: data pages + 2 (cmd + resp)
    SIZE_T numPages = (dataSize + PAGE_SIZE - 1) / PAGE_SIZE;
    SIZE_T numDescriptors = numPages + 2;
    SIZE_T descTableSize = numDescriptors * SIZE_OF_SINGLE_INDIRECT_DESC;
    descTableSize = (descTableSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    PHYSICAL_ADDRESS highestAcceptable;
    highestAcceptable.QuadPart = (ULONGLONG)-1;
    pbuf->desc = MmAllocateContiguousMemory(descTableSize, highestAcceptable);
    if (!pbuf->desc)
    {
        pbuf->desc_pa.QuadPart = 0;
        return FALSE;
    }
    RtlZeroMemory(pbuf->desc, descTableSize);
    pbuf->desc_pa = MmGetPhysicalAddress(pbuf->desc);
    return TRUE;
}

void VioGpuBuf::DeleteBuffer(_In_ PGPU_VBUFFER pbuf)
{
    if (pbuf->desc)
    {
        MmFreeContiguousMemory(pbuf->desc);
        pbuf->desc = NULL;
    }
    delete[] reinterpret_cast<PBYTE>(pbuf);
}

BOOLEAN VioGpuBuf::Init(_In_ UINT cnt)
{
    KIRQL OldIrql;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    m_uCountMin = cnt;

    for (UINT i = 0; i < cnt; ++i)
    {
        PGPU_VBUFFER pvbuf = reinterpret_cast<PGPU_VBUFFER>(new (NonPagedPoolNx) BYTE[VBUFFER_SIZE]);
        // FIXME
        RtlZeroMemory(pvbuf, VBUFFER_SIZE);
        if (pvbuf)
        {
            KeAcquireSpinLock(&m_SpinLock, &OldIrql);
            InsertTailList(&m_FreeBufs, &pvbuf->list_entry);
            ++m_uCount;
            KeReleaseSpinLock(&m_SpinLock, OldIrql);
        }
    }
    ASSERT(m_uCount == cnt);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return (m_uCount > 0);
}

void VioGpuBuf::Close(void)
{
    KIRQL OldIrql;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    KeAcquireSpinLock(&m_SpinLock, &OldIrql);
    while (!IsListEmpty(&m_InUseBufs))
    {
        LIST_ENTRY *pListItem = RemoveHeadList(&m_InUseBufs);
        if (pListItem)
        {
            PGPU_VBUFFER pvbuf = CONTAINING_RECORD(pListItem, GPU_VBUFFER, list_entry);
            ASSERT(pvbuf);
            ASSERT(pvbuf->resp_size <= MAX_INLINE_RESP_SIZE);

            DeleteBuffer(pvbuf);
            --m_uCount;
        }
    }

    while (!IsListEmpty(&m_FreeBufs))
    {
        LIST_ENTRY *pListItem = RemoveHeadList(&m_FreeBufs);
        if (pListItem)
        {
            PGPU_VBUFFER pbuf = CONTAINING_RECORD(pListItem, GPU_VBUFFER, list_entry);
            ASSERT(pbuf);

            if (pbuf->resp_buf && pbuf->resp_size > MAX_INLINE_RESP_SIZE)
            {
                delete[] reinterpret_cast<PBYTE>(pbuf->resp_buf);
                pbuf->resp_buf = NULL;
                pbuf->resp_size = 0;
            }

            if (pbuf->data_buf && pbuf->data_size)
            {
                delete[] reinterpret_cast<PBYTE>(pbuf->data_buf);
                pbuf->data_buf = NULL;
                pbuf->data_size = 0;
            }

            DeleteBuffer(pbuf);
            --m_uCount;
        }
    }
    KeReleaseSpinLock(&m_SpinLock, OldIrql);

    ASSERT(m_uCount == 0);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PGPU_VBUFFER VioGpuBuf::GetBuf(_In_ int size, _In_ int resp_size, _In_opt_ void *resp_buf)
{

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER pbuf = NULL;
    PLIST_ENTRY pListItem = NULL;
    KIRQL SavedIrql = KeGetCurrentIrql();

    if (SavedIrql < DISPATCH_LEVEL)
    {
        KeAcquireSpinLock(&m_SpinLock, &SavedIrql);
    }
    else if (SavedIrql == DISPATCH_LEVEL)
    {
        KeAcquireSpinLockAtDpcLevel(&m_SpinLock);
    }
    else
    {
        // This is possible situation in case of bugcheck.
        // DxgkDdiSystemDisplayEnable can be called at any IRQL.
        // We need to allocate buffer for several command during this proccess.
        // VioGpuDbgBreak();
    }

    if (IsListEmpty(&m_FreeBufs))
    {
        pbuf = reinterpret_cast<PGPU_VBUFFER>(new (NonPagedPoolNx) BYTE[VBUFFER_SIZE]);
        ++m_uCount;
    }
    else
    {
        pListItem = RemoveHeadList(&m_FreeBufs);
        pbuf = CONTAINING_RECORD(pListItem, GPU_VBUFFER, list_entry);
    }

    ASSERT(pbuf);
    memset(pbuf, 0, VBUFFER_SIZE);
    ASSERT(size <= MAX_INLINE_CMD_SIZE);

    pbuf->buf = (char *)((ULONG_PTR)pbuf + sizeof(*pbuf));
    pbuf->size = size;
    pbuf->auto_release = true;

    pbuf->resp_size = resp_size;
    if (resp_size <= MAX_INLINE_RESP_SIZE)
    {
        pbuf->resp_buf = (char *)((ULONG_PTR)pbuf->buf + size);
    }
    else
    {
        pbuf->resp_buf = (char *)resp_buf;
    }
    ASSERT(pbuf->resp_buf);
    InsertTailList(&m_InUseBufs, &pbuf->list_entry);

    if (SavedIrql < DISPATCH_LEVEL)
    {
        KeReleaseSpinLock(&m_SpinLock, SavedIrql);
    }
    else if (SavedIrql == DISPATCH_LEVEL)
    {
        KeReleaseSpinLockFromDpcLevel(&m_SpinLock);
    }
    else
    {
        // This is possible situation in case of bugcheck.
        // DxgkDdiSystemDisplayEnable can be called at any IRQL.
        // We need to allocate buffer for several command during this proccess.
        // VioGpuDbgBreak();
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s buf = %p\n", __FUNCTION__, pbuf));

    return pbuf;
}

void VioGpuBuf::FreeBuf(_In_ PGPU_VBUFFER pbuf)
{
    KIRQL OldIrql;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s buf = %p\n", __FUNCTION__, pbuf));
    KeAcquireSpinLock(&m_SpinLock, &OldIrql);

    if (!IsListEmpty(&m_InUseBufs))
    {
        PLIST_ENTRY leCurrent = m_InUseBufs.Flink;
        PGPU_VBUFFER pvbuf = CONTAINING_RECORD(leCurrent, GPU_VBUFFER, list_entry);
        while (leCurrent && pvbuf)
        {
            if (pvbuf == pbuf)
            {
                RemoveEntryList(leCurrent);
                pvbuf = NULL;
                break;
            }

            leCurrent = leCurrent->Flink;
            if (leCurrent)
            {
                pvbuf = CONTAINING_RECORD(leCurrent, GPU_VBUFFER, list_entry);
            }
        }
    }
    if (pbuf->resp_buf && pbuf->resp_size > MAX_INLINE_RESP_SIZE)
    {
        delete[] reinterpret_cast<PBYTE>(pbuf->resp_buf);
        pbuf->resp_buf = NULL;
        pbuf->resp_size = 0;
    }

    if (pbuf->data_buf && pbuf->data_size)
    {
        delete[] reinterpret_cast<PBYTE>(pbuf->data_buf);
        pbuf->data_buf = NULL;
        pbuf->data_size = 0;
    }

    // Free indirect descriptor table if allocated
    if (pbuf->desc)
    {
        MmFreeContiguousMemory(pbuf->desc);
        pbuf->desc = NULL;
        pbuf->desc_pa.QuadPart = 0;
    }

    if (m_uCount > m_uCountMin)
    {
        DeleteBuffer(pbuf);
        --m_uCount;
    }
    else
    {
        InsertTailList(&m_FreeBufs, &pbuf->list_entry);
    }

    KeReleaseSpinLock(&m_SpinLock, OldIrql);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PAGED_CODE_SEG_BEGIN
VioGpuBuf::VioGpuBuf()
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    InitializeListHead(&m_FreeBufs);
    InitializeListHead(&m_InUseBufs);
    KeInitializeSpinLock(&m_SpinLock);
    m_uCount = 0;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VioGpuBuf::~VioGpuBuf()
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s 0x%p\n", __FUNCTION__, this));

    Close();

    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
}

VioGpuMemSegment::VioGpuMemSegment(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    m_pSGList = NULL;
    m_pVAddr = NULL;
    m_pMdl = NULL;
    m_bSystemMemory = FALSE;
    m_bMapped = FALSE;
    m_Size = 0;
    m_pBlocks = NULL;
    m_pBlockSizes = NULL;
    m_nBlocks = 0;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VioGpuMemSegment::~VioGpuMemSegment(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    Close();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuMemSegment::Init(_In_ UINT size, _In_opt_ PPHYSICAL_ADDRESS pPAddr)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s size=%u\n", __FUNCTION__, size));

    ASSERT(size);
    UINT pages = BYTES_TO_PAGES(size);
    size = pages * PAGE_SIZE;

    if ((pPAddr == NULL) || pPAddr->QuadPart == 0LL)
    {
        // System memory path - use multi-block contiguous allocation with fallback
        m_bSystemMemory = TRUE;

        // Calculate max blocks needed (worst case: all PAGE_SIZE blocks)
        UINT maxBlocks = pages;
        m_pBlocks = reinterpret_cast<PVOID *>(new (NonPagedPoolNx) BYTE[maxBlocks * sizeof(PVOID)]);
        m_pBlockSizes = reinterpret_cast<SIZE_T *>(new (NonPagedPoolNx) BYTE[maxBlocks * sizeof(SIZE_T)]);
        if (!m_pBlocks || !m_pBlockSizes)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s insufficient resources to allocate block arrays\n", __FUNCTION__));
            delete[] reinterpret_cast<PBYTE>(m_pBlocks);
            delete[] reinterpret_cast<PBYTE>(m_pBlockSizes);
            m_pBlocks = NULL;
            m_pBlockSizes = NULL;
            return FALSE;
        }
        RtlZeroMemory(m_pBlocks, maxBlocks * sizeof(PVOID));
        RtlZeroMemory(m_pBlockSizes, maxBlocks * sizeof(SIZE_T));

        // Allocate contiguous blocks with fallback strategy
        PHYSICAL_ADDRESS highestAcceptable;
        highestAcceptable.QuadPart = 0xFFFFFFFFFF; // 40-bit address limit
        SIZE_T remaining = size;
        m_nBlocks = 0;

        while (remaining > 0 && m_nBlocks < maxBlocks)
        {
            SIZE_T actualSize = 0;
            m_pBlocks[m_nBlocks] = AllocateContiguousWithFallback(remaining, &actualSize, highestAcceptable);

            if (!m_pBlocks[m_nBlocks])
            {
                DbgPrint(TRACE_LEVEL_FATAL,
                         ("%s failed to allocate contiguous memory, remaining=%Iu\n", __FUNCTION__, remaining));
                // Cleanup already allocated blocks
                for (UINT i = 0; i < m_nBlocks; i++)
                {
                    MmFreeContiguousMemory(m_pBlocks[i]);
                }
                delete[] reinterpret_cast<PBYTE>(m_pBlocks);
                delete[] reinterpret_cast<PBYTE>(m_pBlockSizes);
                m_pBlocks = NULL;
                m_pBlockSizes = NULL;
                m_nBlocks = 0;
                return FALSE;
            }

            RtlZeroMemory(m_pBlocks[m_nBlocks], actualSize);
            m_pBlockSizes[m_nBlocks] = actualSize;
            remaining -= actualSize;
            m_nBlocks++;
        }

        DbgPrint(TRACE_LEVEL_INFORMATION,
                 ("%s allocated %u contiguous blocks for %u bytes\n", __FUNCTION__, m_nBlocks, size));

        // Build MDL from the allocated blocks
        // Create an empty MDL and manually fill in the PFN array
        m_pMdl = IoAllocateMdl(NULL, size, FALSE, FALSE, NULL);
        if (!m_pMdl)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s insufficient resources to allocate MDL\n", __FUNCTION__));
            for (UINT i = 0; i < m_nBlocks; i++)
            {
                MmFreeContiguousMemory(m_pBlocks[i]);
            }
            delete[] reinterpret_cast<PBYTE>(m_pBlocks);
            delete[] reinterpret_cast<PBYTE>(m_pBlockSizes);
            m_pBlocks = NULL;
            m_pBlockSizes = NULL;
            m_nBlocks = 0;
            return FALSE;
        }

        // Fill PFN array - MmAllocateContiguousMemory guarantees physical contiguity within each block
        PPFN_NUMBER pfnArray = MmGetMdlPfnArray(m_pMdl);
        UINT pfnIndex = 0;

        for (UINT i = 0; i < m_nBlocks; i++)
        {
            UINT blockPages = (UINT)(m_pBlockSizes[i] / PAGE_SIZE);
            PHYSICAL_ADDRESS blockPA = MmGetPhysicalAddress(m_pBlocks[i]);
            PFN_NUMBER basePfn = (PFN_NUMBER)(blockPA.QuadPart / PAGE_SIZE);

            for (UINT j = 0; j < blockPages; j++)
            {
                pfnArray[pfnIndex++] = basePfn + j;
            }
        }

        // Mark MDL as locked and map to contiguous virtual address
        m_pMdl->MdlFlags |= MDL_PAGES_LOCKED;
        m_pVAddr = MmMapLockedPagesSpecifyCache(m_pMdl, KernelMode, MmNonCached, NULL, FALSE, NormalPagePriority);
        if (!m_pVAddr)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s MmMapLockedPagesSpecifyCache failed\n", __FUNCTION__));
            IoFreeMdl(m_pMdl);
            m_pMdl = NULL;
            for (UINT i = 0; i < m_nBlocks; i++)
            {
                MmFreeContiguousMemory(m_pBlocks[i]);
            }
            delete[] reinterpret_cast<PBYTE>(m_pBlocks);
            delete[] reinterpret_cast<PBYTE>(m_pBlockSizes);
            m_pBlocks = NULL;
            m_pBlockSizes = NULL;
            m_nBlocks = 0;
            return FALSE;
        }

        // Build SGList - one element per block (not per page!)
        // This greatly reduces NumberOfElements and avoids QEMU's 16384 limit
        UINT sglsize = sizeof(SCATTER_GATHER_LIST) + sizeof(SCATTER_GATHER_ELEMENT) * m_nBlocks;
        m_pSGList = reinterpret_cast<PSCATTER_GATHER_LIST>(new (NonPagedPoolNx) BYTE[sglsize]);
        if (!m_pSGList)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s insufficient resources to allocate SGL\n", __FUNCTION__));
            MmUnmapLockedPages(m_pVAddr, m_pMdl);
            m_pVAddr = NULL;
            IoFreeMdl(m_pMdl);
            m_pMdl = NULL;
            for (UINT i = 0; i < m_nBlocks; i++)
            {
                MmFreeContiguousMemory(m_pBlocks[i]);
            }
            delete[] reinterpret_cast<PBYTE>(m_pBlocks);
            delete[] reinterpret_cast<PBYTE>(m_pBlockSizes);
            m_pBlocks = NULL;
            m_pBlockSizes = NULL;
            m_nBlocks = 0;
            return FALSE;
        }

        RtlZeroMemory(m_pSGList, sglsize);
        m_pSGList->NumberOfElements = m_nBlocks;

        for (UINT i = 0; i < m_nBlocks; i++)
        {
            m_pSGList->Elements[i].Address = MmGetPhysicalAddress(m_pBlocks[i]);
            m_pSGList->Elements[i].Length = (ULONG)m_pBlockSizes[i];
        }

        DbgPrint(TRACE_LEVEL_VERBOSE,
                 ("<--- %s system memory success, %u blocks, %u SGL elements\n",
                  __FUNCTION__,
                  m_nBlocks,
                  m_pSGList->NumberOfElements));
    }
    else
    {
        // BAR memory path
        NTSTATUS Status = MapFrameBuffer(*pPAddr, size, &m_pVAddr);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s MapFrameBuffer failed with Status: 0x%X\n", __FUNCTION__, Status));
            return FALSE;
        }
        m_bMapped = TRUE;

        // Create MDL for BAR memory
        m_pMdl = IoAllocateMdl(m_pVAddr, size, FALSE, FALSE, NULL);
        if (!m_pMdl)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s insufficient resources to allocate MDL\n", __FUNCTION__));
            UnmapFrameBuffer(m_pVAddr, size);
            m_pVAddr = NULL;
            m_bMapped = FALSE;
            return FALSE;
        }

        // Create SGList with single element for entire BAR
        UINT sglsize = sizeof(SCATTER_GATHER_LIST) + sizeof(SCATTER_GATHER_ELEMENT);
        m_pSGList = reinterpret_cast<PSCATTER_GATHER_LIST>(new (NonPagedPoolNx) BYTE[sglsize]);
        if (!m_pSGList)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s insufficient resources to allocate SGL\n", __FUNCTION__));
            IoFreeMdl(m_pMdl);
            m_pMdl = NULL;
            UnmapFrameBuffer(m_pVAddr, size);
            m_pVAddr = NULL;
            m_bMapped = FALSE;
            return FALSE;
        }
        RtlZeroMemory(m_pSGList, sglsize);
        m_pSGList->NumberOfElements = 1;
        m_pSGList->Elements[0].Address = *pPAddr;
        m_pSGList->Elements[0].Length = size;

        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s BAR memory success\n", __FUNCTION__));
    }

    m_Size = size;
    return TRUE;
}

PHYSICAL_ADDRESS VioGpuMemSegment::GetPhysicalAddress(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PHYSICAL_ADDRESS pa = {0};
    if (m_pVAddr && MmIsAddressValid(m_pVAddr))
    {
        pa = MmGetPhysicalAddress(m_pVAddr);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return pa;
}

void VioGpuMemSegment::Close(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (m_bSystemMemory)
    {
        // System memory path: unmap virtual address, free MDL, free contiguous blocks
        if (m_pVAddr && m_pMdl)
        {
            MmUnmapLockedPages(m_pVAddr, m_pMdl);
        }
        m_pVAddr = NULL;

        if (m_pMdl)
        {
            IoFreeMdl(m_pMdl);
            m_pMdl = NULL;
        }

        // Free all contiguous memory blocks
        for (UINT i = 0; i < m_nBlocks; i++)
        {
            MmFreeContiguousMemory(m_pBlocks[i]);
        }
        delete[] reinterpret_cast<PBYTE>(m_pBlocks);
        m_pBlocks = NULL;
        delete[] reinterpret_cast<PBYTE>(m_pBlockSizes);
        m_pBlockSizes = NULL;
        m_nBlocks = 0;
    }
    else
    {
        // BAR memory path: free MDL, unmap frame buffer
        if (m_pMdl)
        {
            IoFreeMdl(m_pMdl);
            m_pMdl = NULL;
        }

        if (m_bMapped && m_pVAddr)
        {
            UnmapFrameBuffer(m_pVAddr, (ULONG)m_Size);
            m_bMapped = FALSE;
        }
        m_pVAddr = NULL;
    }

    // Clean up SGList
    delete[] reinterpret_cast<PBYTE>(m_pSGList);
    m_pSGList = NULL;

    m_Size = 0;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VioGpuObj::VioGpuObj(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    m_uiHwRes = 0;
    m_pSegment = NULL;
    m_Size = 0;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VioGpuObj::~VioGpuObj(void)
{
    // Driver can destroy object in case of a bugcheck.
    // DxgkDdiSystemDisplayEnable can be called at any IRQL, so it must
    // be in nonpageable memory. DxgkDdiSystemDisplayEnable must not
    // call any code that is in pageable memory and must not manipulate
    // any data that is in pageable memory.
    // PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuObj::Init(_In_ UINT size, VioGpuMemSegment *pSegment)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s requested size = %d\n", __FUNCTION__, size));

    ASSERT(size);
    ASSERT(pSegment);
    UINT pages = BYTES_TO_PAGES(size);
    size = pages * PAGE_SIZE;
    if (size > pSegment->GetSize())
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("<--- %s segment size too small = %Iu (%u)\n", __FUNCTION__, pSegment->GetSize(), size));
        return FALSE;
    }
    m_pSegment = pSegment;
    m_Size = size;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s size = %Iu\n", __FUNCTION__, m_Size));
    return TRUE;
}

PVOID CrsrQueue::AllocCursor(PGPU_VBUFFER *buf)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER vbuf;
    vbuf = m_pBuf->GetBuf(sizeof(GPU_UPDATE_CURSOR), 0, NULL);
    ASSERT(vbuf);
    *buf = vbuf;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s  vbuf = %p\n", __FUNCTION__, vbuf));

    return vbuf ? vbuf->buf : NULL;
}

PAGED_CODE_SEG_END

UINT CrsrQueue::QueueCursor(PGPU_VBUFFER buf)
{
    //    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    UINT res = 0;
    KIRQL SavedIrql;

    VirtIOBufferDescriptor sg[1];
    int outcnt = 0;
    UINT ret = 0;

    ASSERT(buf->size <= PAGE_SIZE);
    if (BuildSGElement(&sg[outcnt], (PVOID)buf->buf, buf->size))
    {
        outcnt++;
    }

    ASSERT(outcnt);
    Lock(&SavedIrql);
    ret = AddBuf(&sg[0], outcnt, 0, buf, NULL, 0);
    Kick();
    Unlock(SavedIrql);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s vbuf = %p outcnt = %d, ret = %d\n", __FUNCTION__, buf, outcnt, ret));
    return res;
}

PGPU_VBUFFER CrsrQueue::DequeueCursor(_Out_ UINT *len)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER buf = NULL;
    KIRQL SavedIrql;
    Lock(&SavedIrql);
    buf = (PGPU_VBUFFER)GetBuf(len);
    Unlock(SavedIrql);
    if (buf == NULL)
    {
        *len = 0;
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s buf %p len = %u\n", __FUNCTION__, buf, *len));
    return buf;
}
