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

#pragma once
#include "viogpu_queue.h"

// The virtio-gpu commands, a call for each, named after it (VIRTIO_GPU_CMD_GET_EDID is GetEdid). A call fills a
// command buffer from the adapter's pool and sends it on the control ring, or, for the cursor's two commands, on the
// cursor ring. It keeps no state of its own but a deadline (Until): the rings are the adapter's, which opens, closes
// and completes them.
class VioGpuCommands
{
  public:
    VioGpuCommands(void) : m_pCtrl(NULL), m_pCursor(NULL), m_Deadline(0)
    {
    }
    void Init(_In_ VioGpuQueue *pCtrlQueue, _In_ VioGpuQueue *pCursorQueue)
    {
        m_pCtrl = pCtrlQueue;
        m_pCursor = pCursorQueue;
    }
    // A copy of the commands bound to Deadline, interrupt time in 100 ns units: each of its calls waits for room in a
    // full ring, and for an answer it waits for, until then and no longer, rather than for VIOGPU_ROOM_TIMEOUT_MS and
    // VIOGPU_ANSWER_TIMEOUT_MS each. For a caller that holds others up while it waits, and that reports its failures
    // itself (VioGpuQueue::SendAndWait). Use a copy for the commands of one critical section: once its deadline has
    // passed, a call that waits for its answer fails at once without sending, and one that does not gives up on a full
    // ring at once.
    VioGpuCommands Until(ULONGLONG Deadline) const
    {
        VioGpuCommands Bound = *this;
        Bound.m_Deadline = Deadline;
        return Bound;
    }

    // The control ring.
    NTSTATUS GetDisplayInfo(PGPU_RESP_DISP_INFO info);
    NTSTATUS GetEdid(UINT id, PGPU_RESP_EDID edid);
    NTSTATUS ResourceCreate2D(UINT res_id, UINT format, UINT width, UINT height);
    NTSTATUS ResourceUnref(UINT id);
    NTSTATUS SetScanout(UINT scan_id, UINT res_id, UINT width, UINT height, UINT x, UINT y, BOOLEAN wait = FALSE);
    NTSTATUS ResourceFlush(UINT res_id, UINT width, UINT height, UINT x, UINT y);
    NTSTATUS TransferToHost2D(UINT res_id, ULONG offset, UINT width, UINT height, UINT x, UINT y);
    NTSTATUS TransferToHost2DAndWait(UINT res_id, UINT width, UINT height, PVIOGPU_COMPLETE late, PVOID ctx);
    NTSTATUS ResourceAttachBacking(UINT res_id, PGPU_MEM_ENTRY ents, UINT nents);
    NTSTATUS ResourceDetachBacking(UINT id);

    // The cursor ring.
    NTSTATUS UpdateCursor(UINT scanout, UINT res_id, INT x, INT y, UINT hot_x, UINT hot_y);
    NTSTATUS MoveCursor(UINT scanout, UINT res_id, INT x, INT y);

    // Reading the device's answers.
    static BOOLEAN ScanoutSize(PGPU_RESP_DISP_INFO resp, UINT id, PULONG xres, PULONG yres);
    static BOOLEAN CopyEdid(PGPU_RESP_EDID resp, UINT id, PBYTE edid);

  private:
    PVOID AllocCmd(PGPU_VBUFFER *buf, int sz);
    PVOID AllocCmdResp(PGPU_VBUFFER *buf, int cmd_sz, int resp_sz, u32 resp_type);
    PVOID AllocCursor(PGPU_VBUFFER *buf);
    // Every command goes to its ring through one of these, with the deadline the commands are bound to.
    NTSTATUS Send(_In_ VioGpuQueue *pQueue, PGPU_VBUFFER buf)
    {
        return pQueue->Send(buf, 0, NULL, NULL, m_Deadline);
    }
    NTSTATUS SendAndWait(_In_ VioGpuQueue *pQueue,
                         PGPU_VBUFFER buf,
                         ULONG flags,
                         PVOID answer,
                         ULONG size,
                         PVIOGPU_COMPLETE late = NULL,
                         PVOID ctx = NULL)
    {
        return pQueue->SendAndWait(buf, flags, answer, size, m_Deadline, late, ctx);
    }

  private:
    VioGpuQueue *m_pCtrl;
    VioGpuQueue *m_pCursor;
    // Interrupt time; 0 for the rings' default waits.
    ULONGLONG m_Deadline;
};
