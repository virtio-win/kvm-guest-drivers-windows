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

void CtrlQueue::CreateResourceSync(UINT res_id, UINT format, UINT width, UINT height)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s res_id=%u width=%u height=%u\n", __FUNCTION__, res_id, width, height));

    PGPU_RES_CREATE_2D cmd;
    PGPU_VBUFFER vbuf;
    KEVENT event;

    cmd = (PGPU_RES_CREATE_2D)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd->resource_id = res_id;
    cmd->format = format;
    cmd->width = width;
    cmd->height = height;

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    vbuf->complete_cb = NotifyEventCompleteCB;
    vbuf->complete_ctx = &event;
    vbuf->auto_release = false;

    QueueBuffer(vbuf);
    KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);

    ReleaseBuffer(vbuf);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s res_id=%u\n", __FUNCTION__, res_id));
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

BOOLEAN CtrlQueue::AttachBacking(UINT res_id, PGPU_MEM_ENTRY ents, UINT nents)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s res_id=%u nents=%u\n", __FUNCTION__, res_id, nents));

    // QEMU virtio_gpu_create_mapping_iov() rejects nr_entries > 16384
    if (nents > VIRTIO_GPU_MAX_BACKING_ENTRIES)
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("<--- %s QEMU entry limit exceeded: %u > %u\n", __FUNCTION__, nents, VIRTIO_GPU_MAX_BACKING_ENTRIES));
        return FALSE;
    }

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
    return TRUE;
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
void CtrlQueue::DestroyResourceSync(UINT res_id)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_RES_UNREF cmd;
    PGPU_VBUFFER vbuf;
    KEVENT event;

    cmd = (PGPU_RES_UNREF)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    cmd->resource_id = res_id;

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    vbuf->complete_cb = NotifyEventCompleteCB;
    vbuf->complete_ctx = &event;
    vbuf->auto_release = false;

    QueueBuffer(vbuf);
    KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);

    ReleaseBuffer(vbuf);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void CtrlQueue::DetachBackingSync(UINT res_id)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_RES_DETACH_BACKING cmd;
    PGPU_VBUFFER vbuf;
    KEVENT event;

    cmd = (PGPU_RES_DETACH_BACKING)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
    cmd->resource_id = res_id;

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    vbuf->complete_cb = NotifyEventCompleteCB;
    vbuf->complete_ctx = &event;
    vbuf->auto_release = false;

    QueueBuffer(vbuf);
    KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);

    ReleaseBuffer(vbuf);
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
    UINT numPages = (UINT)((dataSize + PAGE_SIZE - 1) / PAGE_SIZE);
    UINT numDescriptors = numPages + 2;
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

// Helper function to try allocating contiguous memory with fallback sizes
static PVOID AllocateContiguousWithFallback(SIZE_T requestedSize,
                                            SIZE_T *actualSize,
                                            PHYSICAL_ADDRESS highestAcceptable,
                                            const SIZE_T *blockSizes = g_ContiguousBlockSizes,
                                            UINT blockSizeCount = CONTIGUOUS_BLOCK_SIZE_COUNT)
{
    for (UINT i = 0; i < blockSizeCount; i++)
    {
        SIZE_T trySize = min(requestedSize, blockSizes[i]);
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

BOOLEAN VioGpuMemSegment::Init(_In_ UINT size, _In_opt_ CPciBar *pBar, _In_ BOOLEAN singleBlock)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s size=%u singleBlock=%d\n", __FUNCTION__, size, singleBlock));

    ASSERT(size);
    UINT pages = BYTES_TO_PAGES(size);
    size = pages * PAGE_SIZE;

    // Delegate to Merge which handles BAR vs system memory decision
    // When singleBlock is TRUE, use size as fixedBlockSize to allocate in one block
    return Merge(size, pBar, singleBlock ? size : 0);
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
        CloseSystemMemory();
    }
    else
    {
        CloseBar();
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void VioGpuMemSegment::CloseBar()
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (m_pMdl)
    {
        IoFreeMdl(m_pMdl);
        m_pMdl = NULL;
    }

    if (m_bMapped)
    {
        UnmapFrameBuffer(m_pVAddr, (ULONG)m_Size);
        m_bMapped = FALSE;
    }
    m_pVAddr = NULL;

    CleanSGList();

    m_Size = 0;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void VioGpuMemSegment::CloseSystemMemory()
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    CleanMapping();

    if (m_pBlocks)
    {
        for (UINT i = 0; i < m_nBlocks; i++)
        {
            if (m_pBlocks[i])
            {
                MmFreeContiguousMemory(m_pBlocks[i]);
                m_pBlocks[i] = NULL;
            }
        }
        delete[] reinterpret_cast<PBYTE>(m_pBlocks);
        m_pBlocks = NULL;
    }

    if (m_pBlockSizes)
    {
        delete[] reinterpret_cast<PBYTE>(m_pBlockSizes);
        m_pBlockSizes = NULL;
    }

    CleanSGList();

    m_nBlocks = 0;
    m_Size = 0;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuMemSegment::AllocateBar(PHYSICAL_ADDRESS pAddr,
                                      SIZE_T size,
                                      PVOID *pVAddr,
                                      PMDL *pMdl,
                                      PSCATTER_GATHER_LIST *pSGList)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s size=%Iu\n", __FUNCTION__, size));

    PVOID newVAddr = NULL;
    PMDL newMdl = NULL;
    PSCATTER_GATHER_LIST newSGList = NULL;

    NTSTATUS Status = MapFrameBuffer(pAddr, (ULONG)size, &newVAddr);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("%s MapFrameBuffer failed with Status: 0x%X\n", __FUNCTION__, Status));
        return FALSE;
    }

    newMdl = IoAllocateMdl(newVAddr, (ULONG)size, FALSE, FALSE, NULL);
    if (!newMdl)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("%s insufficient resources to allocate MDL\n", __FUNCTION__));
        UnmapFrameBuffer(newVAddr, (ULONG)size);
        return FALSE;
    }

    UINT sglsize = sizeof(SCATTER_GATHER_LIST) + sizeof(SCATTER_GATHER_ELEMENT);
    newSGList = reinterpret_cast<PSCATTER_GATHER_LIST>(new (NonPagedPoolNx) BYTE[sglsize]);
    if (!newSGList)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("%s insufficient resources to allocate SGL\n", __FUNCTION__));
        IoFreeMdl(newMdl);
        UnmapFrameBuffer(newVAddr, (ULONG)size);
        return FALSE;
    }
    RtlZeroMemory(newSGList, sglsize);

    newSGList->NumberOfElements = 1;
    newSGList->Elements[0].Address = pAddr;
    newSGList->Elements[0].Length = (ULONG)size;

    *pVAddr = newVAddr;
    *pMdl = newMdl;
    *pSGList = newSGList;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s success\n", __FUNCTION__));
    return TRUE;
}

BOOLEAN VioGpuMemSegment::ExpandSystemMemory(SIZE_T targetSize, SIZE_T fixedBlockSize)
{
    SIZE_T additionalSize = targetSize - m_Size;
    SIZE_T minBlockSize = fixedBlockSize > 0 ? fixedBlockSize : PAGE_SIZE;
    UINT maxNewBlocks = (UINT)((additionalSize + minBlockSize - 1) / minBlockSize);

    // Temporary storage for new blocks
    PVOID *newBlocks = reinterpret_cast<PVOID *>(new (NonPagedPoolNx) BYTE[maxNewBlocks * sizeof(PVOID)]);
    SIZE_T *newSizes = reinterpret_cast<SIZE_T *>(new (NonPagedPoolNx) BYTE[maxNewBlocks * sizeof(SIZE_T)]);
    if (!newBlocks || !newSizes)
    {
        delete[] reinterpret_cast<PBYTE>(newBlocks);
        delete[] reinterpret_cast<PBYTE>(newSizes);
        return FALSE;
    }
    RtlZeroMemory(newBlocks, maxNewBlocks * sizeof(PVOID));
    RtlZeroMemory(newSizes, maxNewBlocks * sizeof(SIZE_T));
    UINT newBlockCount = 0;

    PHYSICAL_ADDRESS highestAcceptable;
    highestAcceptable.QuadPart = 0xFFFFFFFFFF;
    SIZE_T remaining = additionalSize;

    // Use fixed block size as single-element fallback list, or default fallback list
    const SIZE_T *blockSizes = fixedBlockSize > 0 ? &fixedBlockSize : g_ContiguousBlockSizes;
    UINT blockSizeCount = fixedBlockSize > 0 ? 1 : CONTIGUOUS_BLOCK_SIZE_COUNT;

    // Allocate new blocks
    while (remaining > 0 && newBlockCount < maxNewBlocks)
    {
        SIZE_T actualSize = 0;
        newBlocks[newBlockCount] = AllocateContiguousWithFallback(remaining,
                                                                  &actualSize,
                                                                  highestAcceptable,
                                                                  blockSizes,
                                                                  blockSizeCount);

        if (!newBlocks[newBlockCount])
        {
            // Allocation failed, free already allocated new blocks
            for (UINT i = 0; i < newBlockCount; i++)
            {
                MmFreeContiguousMemory(newBlocks[i]);
            }
            delete[] reinterpret_cast<PBYTE>(newBlocks);
            delete[] reinterpret_cast<PBYTE>(newSizes);
            return FALSE;
        }

        RtlZeroMemory(newBlocks[newBlockCount], actualSize);
        newSizes[newBlockCount] = actualSize;
        remaining -= actualSize;
        newBlockCount++;
    }

    // Expand existing arrays
    UINT totalBlocks = m_nBlocks + newBlockCount;
    PVOID *expandedBlocks = reinterpret_cast<PVOID *>(new (NonPagedPoolNx) BYTE[totalBlocks * sizeof(PVOID)]);
    SIZE_T *expandedSizes = reinterpret_cast<SIZE_T *>(new (NonPagedPoolNx) BYTE[totalBlocks * sizeof(SIZE_T)]);
    if (!expandedBlocks || !expandedSizes)
    {
        // Free new blocks on allocation failure
        for (UINT i = 0; i < newBlockCount; i++)
        {
            MmFreeContiguousMemory(newBlocks[i]);
        }
        delete[] reinterpret_cast<PBYTE>(newBlocks);
        delete[] reinterpret_cast<PBYTE>(newSizes);
        delete[] reinterpret_cast<PBYTE>(expandedBlocks);
        delete[] reinterpret_cast<PBYTE>(expandedSizes);
        return FALSE;
    }

    // Copy existing blocks
    RtlCopyMemory(expandedBlocks, m_pBlocks, m_nBlocks * sizeof(PVOID));
    RtlCopyMemory(expandedSizes, m_pBlockSizes, m_nBlocks * sizeof(SIZE_T));

    // Append new blocks
    RtlCopyMemory(&expandedBlocks[m_nBlocks], newBlocks, newBlockCount * sizeof(PVOID));
    RtlCopyMemory(&expandedSizes[m_nBlocks], newSizes, newBlockCount * sizeof(SIZE_T));

    // Replace arrays
    delete[] reinterpret_cast<PBYTE>(m_pBlocks);
    delete[] reinterpret_cast<PBYTE>(m_pBlockSizes);
    delete[] reinterpret_cast<PBYTE>(newBlocks);
    delete[] reinterpret_cast<PBYTE>(newSizes);

    m_pBlocks = expandedBlocks;
    m_pBlockSizes = expandedSizes;
    m_nBlocks = totalBlocks;
    m_Size = targetSize;

    return TRUE;
}

BOOLEAN VioGpuMemSegment::ShrinkSystemMemory(SIZE_T targetSize)
{
    // Remove blocks from the end until size approaches targetSize
    // but keep >= targetSize (conservative strategy)
    SIZE_T currentSize = m_Size;
    UINT blocksToKeep = m_nBlocks;

    // Calculate how many blocks to keep
    while (blocksToKeep > 0)
    {
        SIZE_T sizeWithoutLast = currentSize - m_pBlockSizes[blocksToKeep - 1];
        if (sizeWithoutLast < targetSize)
        {
            // Removing this block would go below target, stop
            break;
        }
        currentSize = sizeWithoutLast;
        blocksToKeep--;
    }

    // Free excess blocks from the end
    for (UINT i = blocksToKeep; i < m_nBlocks; i++)
    {
        MmFreeContiguousMemory(m_pBlocks[i]);
        m_pBlocks[i] = NULL;
    }

    m_nBlocks = blocksToKeep;
    m_Size = currentSize;

    // Note: Keep array size unchanged to avoid frequent reallocations
    // Only m_nBlocks is updated

    return TRUE;
}

void VioGpuMemSegment::CleanMapping()
{
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
}

void VioGpuMemSegment::CleanSGList()
{
    delete[] reinterpret_cast<PBYTE>(m_pSGList);
    m_pSGList = NULL;
}

BOOLEAN VioGpuMemSegment::RebuildMapping()
{
    CleanMapping();

    if (m_nBlocks == 0 || m_Size == 0)
    {
        return TRUE; // Empty segment
    }

    m_pMdl = IoAllocateMdl(NULL, (ULONG)m_Size, FALSE, FALSE, NULL);
    if (!m_pMdl)
    {
        return FALSE;
    }

    // Build PFN array from all blocks
    PPFN_NUMBER pfnArray = MmGetMdlPfnArray(m_pMdl);
    UINT pfnIndex = 0;

    for (UINT i = 0; i < m_nBlocks; i++)
    {
        UINT blockPages = (UINT)(m_pBlockSizes[i] / PAGE_SIZE);
        PHYSICAL_ADDRESS blockPA = MmGetPhysicalAddress(m_pBlocks[i]);

        for (UINT j = 0; j < blockPages; j++)
        {
            pfnArray[pfnIndex++] = (PFN_NUMBER)((blockPA.QuadPart / PAGE_SIZE) + j);
        }
    }

    m_pMdl->MdlFlags |= MDL_PAGES_LOCKED;
    m_pVAddr = MmMapLockedPagesSpecifyCache(m_pMdl, KernelMode, MmNonCached, NULL, FALSE, NormalPagePriority);

    return (m_pVAddr != NULL);
}

BOOLEAN VioGpuMemSegment::RebuildSGList()
{
    CleanSGList();

    if (m_nBlocks == 0)
    {
        return TRUE;
    }

    UINT sglsize = sizeof(SCATTER_GATHER_LIST) + sizeof(SCATTER_GATHER_ELEMENT) * m_nBlocks;
    m_pSGList = reinterpret_cast<PSCATTER_GATHER_LIST>(new (NonPagedPoolNx) BYTE[sglsize]);

    if (!m_pSGList)
    {
        return FALSE;
    }

    RtlZeroMemory(m_pSGList, sglsize);
    m_pSGList->NumberOfElements = m_nBlocks;

    for (UINT i = 0; i < m_nBlocks; i++)
    {
        m_pSGList->Elements[i].Address = MmGetPhysicalAddress(m_pBlocks[i]);
        m_pSGList->Elements[i].Length = (ULONG)m_pBlockSizes[i];
    }

    return TRUE;
}

BOOLEAN VioGpuMemSegment::Merge(SIZE_T targetSize, CPciBar *pBar, SIZE_T fixedBlockSize)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE,
             ("---> %s current=%Iu target=%Iu fixedBlock=%Iu\n", __FUNCTION__, m_Size, targetSize, fixedBlockSize));

    // Align to page size
    SIZE_T pages = BYTES_TO_PAGES(targetSize);
    targetSize = pages * PAGE_SIZE;

    // Same size, nothing to do
    if (targetSize == m_Size)
    {
        return TRUE;
    }

    // Get BAR info from CPciBar pointer
    PHYSICAL_ADDRESS barAddr = {0};
    SIZE_T barSize = 0;
    if (pBar)
    {
        barAddr = pBar->GetPA();
        barSize = pBar->GetSize();
    }

    bool barAvailable = (barAddr.QuadPart != 0 && barSize > 0);
    bool shouldUseBar = barAvailable && (targetSize <= barSize);

    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("%s: barAvailable=%d shouldUseBar=%d targetSize=%Iu barSize=%Iu\n",
              __FUNCTION__,
              barAvailable,
              shouldUseBar,
              targetSize,
              barSize));

    BOOLEAN hadUsedBar = (m_bSystemMemory == FALSE);
    BOOLEAN success = FALSE;
    if (shouldUseBar)
    {
        // BAR path: targetSize <= barSize
        PVOID newVAddr = NULL;
        PMDL newMdl = NULL;
        PSCATTER_GATHER_LIST newSGList = NULL;

        if (AllocateBar(barAddr, targetSize, &newVAddr, &newMdl, &newSGList))
        {
            // Allocation succeeded - now clean up old resources
            if (m_bSystemMemory)
            {
                CloseSystemMemory();
            }
            else if (m_bMapped)
            {
                CloseBar();
            }

            // Commit new BAR resources
            m_pVAddr = newVAddr;
            m_pMdl = newMdl;
            m_pSGList = newSGList;
            m_bMapped = TRUE;
            m_Size = targetSize;

            DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s: ->BAR success\n", __FUNCTION__));
            success = TRUE;
        }
        else
        {
            DbgPrint(TRACE_LEVEL_WARNING, ("%s: BAR allocation failed\n", __FUNCTION__));
            success = FALSE;
        }

        m_bSystemMemory = FALSE;
        return success;
    }
    else if (hadUsedBar)
    {
        // Currently using BAR -> release BAR
        CloseBar();
        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s: BAR->system memory released BAR\n", __FUNCTION__));
    }

    // System memory path
    m_bSystemMemory = TRUE;

    if (targetSize > m_Size)
    {
        success = ExpandSystemMemory(targetSize, fixedBlockSize);
    }
    else
    {
        success = ShrinkSystemMemory(targetSize);
    }

    // if used BAR originally and failed to allocate system memory, try BAR again
    if (!success && hadUsedBar && barAvailable)
    {
        DbgPrint(TRACE_LEVEL_WARNING, ("%s: system memory allocation failed, trying BAR again\n", __FUNCTION__));
        PVOID newVAddr = NULL;
        PMDL newMdl = NULL;
        PSCATTER_GATHER_LIST newSGList = NULL;

        if (AllocateBar(barAddr, targetSize, &newVAddr, &newMdl, &newSGList))
        {
            // Clean up failed system memory state
            CloseSystemMemory();

            // Commit BAR resources
            m_pVAddr = newVAddr;
            m_pMdl = newMdl;
            m_pSGList = newSGList;
            m_bMapped = TRUE;
            m_Size = targetSize;
            m_bSystemMemory = FALSE;

            DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s: system memory->BAR success\n", __FUNCTION__));
            return TRUE;
        }
    }

    if (!success)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s: system memory merge failed\n", __FUNCTION__));
        return FALSE;
    }

    if (!RebuildMapping())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s: RebuildMapping failed\n", __FUNCTION__));
        return FALSE;
    }

    if (!RebuildSGList())
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s: RebuildSGList failed\n", __FUNCTION__));
        return FALSE;
    }

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s: merged to %Iu bytes (%u blocks)\n", __FUNCTION__, m_Size, m_nBlocks));
    return TRUE;
}

void VioGpuMemSegment::TakeFrom(VioGpuMemSegment &other)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    // Close our own resources first
    Close();

    // Take ownership of other's resources
    m_bSystemMemory = other.m_bSystemMemory;
    m_bMapped = other.m_bMapped;
    m_pSGList = other.m_pSGList;
    m_pVAddr = other.m_pVAddr;
    m_pMdl = other.m_pMdl;
    m_Size = other.m_Size;
    m_pBlocks = other.m_pBlocks;
    m_pBlockSizes = other.m_pBlockSizes;
    m_nBlocks = other.m_nBlocks;

    // Clear other to prevent double-free on destruction
    other.m_pSGList = NULL;
    other.m_pVAddr = NULL;
    other.m_pMdl = NULL;
    other.m_Size = 0;
    other.m_bSystemMemory = FALSE;
    other.m_bMapped = FALSE;
    other.m_pBlocks = NULL;
    other.m_pBlockSizes = NULL;
    other.m_nBlocks = 0;

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
