/*
 * Copyright (C) 2019-2020 Red Hat, Inc.
 * Copyright (C) 2026 Zededa, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
 *             Mikhail Malyshev <mike.malyshev@gmail.com>
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

#include "viogpu_commands.h"
#include "baseobj.h"
#if !DBG
#include "viogpu_commands.tmh"
#endif

PAGED_CODE_SEG_BEGIN

BOOLEAN VioGpuCommands::ScanoutSize(PGPU_RESP_DISP_INFO resp, UINT id, PULONG xres, PULONG yres)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

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

NTSTATUS VioGpuCommands::GetDisplayInfo(PGPU_RESP_DISP_INFO info)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_CTRL_HDR cmd;
    PGPU_VBUFFER vbuf;
    NTSTATUS status;

    cmd = (PGPU_CTRL_HDR)AllocCmdResp(&vbuf,
                                      sizeof(GPU_CTRL_HDR),
                                      sizeof(GPU_RESP_DISP_INFO),
                                      VIRTIO_GPU_RESP_OK_DISPLAY_INFO);
    if (cmd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(cmd, sizeof(GPU_CTRL_HDR));

    cmd->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    status = SendAndWait(m_pCtrl, vbuf, 0, info, sizeof(*info));
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("---> Failed to ask display info, status 0x%x\n", status));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return status;
}

NTSTATUS VioGpuCommands::GetEdid(UINT id, PGPU_RESP_EDID edid)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_CMD_GET_EDID cmd;
    PGPU_VBUFFER vbuf;
    NTSTATUS status;

    cmd = (PGPU_CMD_GET_EDID)AllocCmdResp(&vbuf,
                                          sizeof(GPU_CMD_GET_EDID),
                                          sizeof(GPU_RESP_EDID),
                                          VIRTIO_GPU_RESP_OK_EDID);
    if (cmd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(cmd, sizeof(GPU_CMD_GET_EDID));

    cmd->hdr.type = VIRTIO_GPU_CMD_GET_EDID;
    cmd->scanout = id;

    status = SendAndWait(m_pCtrl, vbuf, 0, edid, sizeof(*edid));
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("---> Failed to get edid info, status 0x%x\n", status));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return status;
}

// Copies the EDID an answer to GET_EDID carries, which starts at its first byte, as many bytes as the host says it
// returned and at most size. The rest of edid is cleared.
BOOLEAN VioGpuCommands::CopyEdid(PGPU_RESP_EDID resp, PBYTE edid, ULONG size, PULONG copied)
{
    PAGED_CODE();

    ULONG count = min(min(size, resp->size), (ULONG)sizeof(resp->edid));
    RtlCopyMemory(edid, resp->edid, count);
    RtlZeroMemory(edid + count, size - count);
    *copied = count;
    return TRUE;
}

NTSTATUS VioGpuCommands::ResourceCreate2D(UINT res_id, UINT format, UINT width, UINT height)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_RES_CREATE_2D cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_CREATE_2D)AllocCmd(&vbuf, sizeof(*cmd));
    if (cmd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd->resource_id = res_id;
    cmd->format = format;
    cmd->width = width;
    cmd->height = height;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return Send(m_pCtrl, vbuf);
}

NTSTATUS VioGpuCommands::ResourceFlush(UINT res_id, UINT width, UINT height, UINT x, UINT y)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    PGPU_RES_FLUSH cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_FLUSH)AllocCmd(&vbuf, sizeof(*cmd));
    if (cmd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd->resource_id = res_id;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->r.x = x;
    cmd->r.y = y;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return Send(m_pCtrl, vbuf);
}

// Uploads the width x height rectangle at the top left of a resource's backing, fenced, and waits for room and for the
// answer: a host that processes commands asynchronously answers a fenced one once it is complete. The device's error
// answer, as for a resource it does not have, is the outcome. STATUS_DEVICE_BUSY: nothing was sent. STATUS_IO_TIMEOUT:
// the transfer is on the ring, and late gets its outcome later (VioGpuQueue::SendAndWait).
NTSTATUS VioGpuCommands::TransferToHost2DAndWait(UINT res_id, UINT width, UINT height, PVIOGPU_COMPLETE late, PVOID ctx)
{
    PAGED_CODE();

    PGPU_VBUFFER vbuf;
    PGPU_RES_TRANSF_TO_HOST_2D cmd = (PGPU_RES_TRANSF_TO_HOST_2D)AllocCmd(&vbuf, sizeof(*cmd));
    if (cmd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd->resource_id = res_id;
    cmd->r.width = width;
    cmd->r.height = height;

    return SendAndWait(m_pCtrl, vbuf, VIOGPU_SEND_FENCE, NULL, 0, late, ctx);
}

NTSTATUS VioGpuCommands::TransferToHost2D(UINT res_id, ULONG offset, UINT width, UINT height, UINT x, UINT y)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    PGPU_RES_TRANSF_TO_HOST_2D cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_TRANSF_TO_HOST_2D)AllocCmd(&vbuf, sizeof(*cmd));
    if (cmd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd->resource_id = res_id;
    cmd->offset = offset;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->r.x = x;
    cmd->r.y = y;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return Send(m_pCtrl, vbuf);
}

// Backs a resource with the guest pages that sgl lists.
NTSTATUS VioGpuCommands::ResourceAttachBacking(UINT res_id, PSCATTER_GATHER_LIST sgl)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_RES_ATTACH_BACKING cmd;
    PGPU_VBUFFER vbuf;
    PGPU_MEM_ENTRY ents;
    UINT nents = sgl->NumberOfElements;
    cmd = (PGPU_RES_ATTACH_BACKING)AllocCmd(&vbuf, sizeof(*cmd));
    if (cmd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ents = (PGPU_MEM_ENTRY)m_pCtrl->AllocPayload(vbuf, sizeof(*ents) * nents);
    if (ents == NULL)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s no memory for %u entries\n", __FUNCTION__, nents));
        m_pCtrl->ReleaseBuffer(vbuf);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->resource_id = res_id;
    cmd->nr_entries = nents;

    for (UINT i = 0; i < nents; i++)
    {
        ents[i].addr = sgl->Elements[i].Address.QuadPart;
        ents[i].length = sgl->Elements[i].Length;
        ents[i].padding = 0;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    // A refused backing would leave the resource blank: only an answer tells.
    return SendAndWait(m_pCtrl, vbuf, 0, NULL, 0);
}

PAGED_CODE_SEG_END

NTSTATUS VioGpuCommands::ResourceUnref(UINT res_id)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_RES_UNREF cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_UNREF)AllocCmd(&vbuf, sizeof(*cmd));
    if (cmd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    cmd->resource_id = res_id;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return Send(m_pCtrl, vbuf);
}

NTSTATUS VioGpuCommands::ResourceDetachBacking(UINT res_id)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_RES_DETACH_BACKING cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_DETACH_BACKING)AllocCmd(&vbuf, sizeof(*cmd));
    if (cmd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
    cmd->resource_id = res_id;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return Send(m_pCtrl, vbuf);
}

PVOID VioGpuCommands::AllocCmdResp(PGPU_VBUFFER *buf, int cmd_sz, int resp_sz, u32 resp_type)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER vbuf;
    vbuf = m_pCtrl->AllocBuffer(cmd_sz, resp_sz, resp_type);
    *buf = vbuf;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return vbuf ? vbuf->buf : NULL;
}

PVOID VioGpuCommands::AllocCmd(PGPU_VBUFFER *buf, int sz)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (buf == NULL || sz == 0)
    {
        return NULL;
    }

    PGPU_VBUFFER vbuf = m_pCtrl->AllocBuffer(sz, sizeof(GPU_CTRL_HDR), VIRTIO_GPU_RESP_OK_NODATA);
    *buf = vbuf;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s  vbuf = %p\n", __FUNCTION__, vbuf));

    return vbuf ? vbuf->buf : NULL;
}

// Shows a resource on a scanout, or none for resource 0. With wait, returns once the device has answered: QEMU checks
// there that the resource exists, and up to QEMU 11.1, on its 2D device, that it has backing.
NTSTATUS VioGpuCommands::SetScanout(UINT scan_id, UINT res_id, UINT width, UINT height, UINT x, UINT y, BOOLEAN wait)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_SET_SCANOUT cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_SET_SCANOUT)AllocCmd(&vbuf, sizeof(*cmd));
    if (cmd == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd->resource_id = res_id;
    cmd->scanout_id = scan_id;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->r.x = x;
    cmd->r.y = y;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return wait ? SendAndWait(m_pCtrl, vbuf, 0, NULL, 0) : Send(m_pCtrl, vbuf);
}

PAGED_CODE_SEG_BEGIN

PVOID VioGpuCommands::AllocCursor(PGPU_VBUFFER *buf)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER vbuf;
    vbuf = m_pCursor->AllocBuffer(sizeof(GPU_UPDATE_CURSOR), 0, 0);
    *buf = vbuf;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s  vbuf = %p\n", __FUNCTION__, vbuf));

    return vbuf ? vbuf->buf : NULL;
}

// Shows the cursor image of resource res_id on a scanout with its top left at x, y and its hot spot at hot_x, hot_y in
// the image. Resource 0 hides the cursor.
NTSTATUS VioGpuCommands::UpdateCursor(UINT scanout, UINT res_id, INT x, INT y, UINT hot_x, UINT hot_y)
{
    PAGED_CODE();

    PGPU_VBUFFER vbuf;
    PGPU_UPDATE_CURSOR crsr = (PGPU_UPDATE_CURSOR)AllocCursor(&vbuf);
    if (crsr == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(crsr, sizeof(*crsr));

    crsr->hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
    crsr->pos.scanout_id = scanout;
    // Left of or above the scanout for a pointer near its edge: QEMU reads the position as signed, as it reads the
    // Linux driver's negative positions.
    crsr->pos.x = (UINT)x;
    crsr->pos.y = (UINT)y;
    crsr->resource_id = res_id;
    crsr->hot_x = hot_x;
    crsr->hot_y = hot_y;

    return Send(m_pCursor, vbuf);
}

// Moves the cursor of resource res_id on a scanout so that its top left is at x, y.
NTSTATUS VioGpuCommands::MoveCursor(UINT scanout, UINT res_id, INT x, INT y)
{
    PAGED_CODE();

    PGPU_VBUFFER vbuf;
    PGPU_UPDATE_CURSOR crsr = (PGPU_UPDATE_CURSOR)AllocCursor(&vbuf);
    if (crsr == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(crsr, sizeof(*crsr));

    crsr->hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
    crsr->pos.scanout_id = scanout;
    crsr->pos.x = (UINT)x;
    crsr->pos.y = (UINT)y;
    crsr->resource_id = res_id;

    return Send(m_pCursor, vbuf);
}

PAGED_CODE_SEG_END
