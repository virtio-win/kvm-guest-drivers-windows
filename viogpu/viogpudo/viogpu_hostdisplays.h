/*
 * Copyright (C) 2019-2022 Red Hat, Inc.
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

#include "viogpu_component.h"
#include "viogpu_display.h"

class VioGpuAdapter;

// The config-change thread's work, posted with VioGpuHostDisplays::Post.
#define VIOGPU_WORK_EVENT  0x1 // the device raised a configuration interrupt
#define VIOGPU_WORK_REREAD 0x2 // read the displays whether the device raised a display event or not
#define VIOGPU_WORK_STOP   0x4

// The host's displays. A thread of their own reads them when the device reports a display change, which the DPC
// posts, and at every open, since a change made while the device was reset raised no interrupt. dxgkrnl does not
// serialize the thread with any DDI, so it uses the device only while its component is open: Close waits for a read in
// progress to finish and keeps the next one waiting until Open.
class VioGpuHostDisplays : public VioGpuComponent
{
  public:
    VioGpuHostDisplays(void);
    void Init(_In_ VioGpuAdapter *pAdapter)
    {
        m_pAdapter = pAdapter;
    }
    NTSTATUS Start(void) override;
    void Open(void) override;
    void Close(void) override;
    void Stop(void) override;
    void InitDisplays(void);
    // Posts work for the thread, VIOGPU_WORK_*, and wakes it. Posts merge until the thread takes them. Callable up to
    // DISPATCH_LEVEL.
    void Post(LONG Work);

  private:
    static void ThreadWork(_In_ PVOID Context);
    static void ReadLate(_In_opt_ PVOID ctx, _In_ NTSTATUS status);
    void ThreadWorkRoutine(void);
    BOOLEAN ConfigChanged(BOOLEAN Reread, PBOOLEAN pEvent);
    BOOLEAN ReadDisplays(void);
    BOOLEAN ReadEdid(UINT ScanoutId, PVIOGPU_DISPLAY pDisplay);
    void AddMode(PVIOGPU_DISPLAY pDisplay, USHORT xres, USHORT yres);
    void BuildModes(PVIOGPU_DISPLAY pDisplay);
    NTSTATUS UpdateChildStatus(UINT ScanoutId, BOOLEAN Connected);

  private:
    VioGpuAdapter *m_pAdapter;
    // The work posted and not taken yet, and the event that wakes the thread to take it.
    volatile LONG m_WorkPosted;
    KEVENT m_WorkEvent;
    PETHREAD m_pWorkThread;
    // Acquired by the thread for each read: it can be only while the component is open.
    EX_RUNDOWN_REF m_Rundown;
    // Where the displays are read into, at start and then by the thread, before they are published; and the display
    // each scanout counts as at start until the host answers.
    VIOGPU_DISPLAY m_Next[MAX_CHILDREN];
    VIOGPU_DISPLAY m_Assumed;
    // The children whose connection Windows was last told about, a bit each: the thread tells it again until it matches
    // the displays. And the children whose last report failed, told again with the next pass.
    ULONG m_ChildAnnounced;
    ULONG m_AnnounceFailed;
    // A read of the host (GET_DISPLAY_INFO or GET_EDID) whose answer did not come in time is still on the ring: no
    // other read is sent until it is answered, or the queue closes, which ReadLate tells.
    volatile LONG m_ReadOutstanding;
};
