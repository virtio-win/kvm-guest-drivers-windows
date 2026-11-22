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

class VioGpuAdapter;

// The host's displays. A thread of their own reads them when the device reports a display change: the DPC posts it
// (Post), and a D0 does too, since a change made while the device was reset raised no interrupt. dxgkrnl does not
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
    // The device reported a display change. Callable up to DISPATCH_LEVEL.
    void Post(void);

  private:
    static void ThreadWork(_In_ PVOID Context);
    void ThreadWorkRoutine(void);

  private:
    VioGpuAdapter *m_pAdapter;
    KEVENT m_ConfigUpdateEvent;
    PETHREAD m_pWorkThread;
    volatile BOOLEAN m_bStopWorkThread;
    // Acquired by the thread for each read: it can be only while the component is open.
    EX_RUNDOWN_REF m_Rundown;
};
