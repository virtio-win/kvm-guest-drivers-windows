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

// The driver's channel to its user-mode helpers (viogpuap, viogpures; viogpum.h): the escapes they send, and the named
// resolution events that tell them a display changed. The events are made by the first VIOGPU_GET_DEVICE_ID, in the
// helper's context, and go when the component stops, once the config-change thread that sets them has stopped.
class VioGpuHelperChannel : public VioGpuComponent, public VioGpuDisplayListener
{
  public:
    VioGpuHelperChannel(void);
    void Init(_In_ VioGpuAdapter *pAdapter)
    {
        m_pAdapter = pAdapter;
    }
    void Stop(void) override;
    void SourceCommitted(UINT SourceId, UINT OldScanoutId, UINT NewScanoutId) override;
    void DisplaysChanged(ULONG ConnectedMask) override;
    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE *pEscape);

  private:
    VOID Notify(VOID);
    VOID CreateEvents(VOID);
    VOID NotifySource(UINT SourceId);
    VOID CloseEvents(VOID);
    ULONG GetApplied(UINT SourceId, ULONGLONG Layout);
    VOID SetApplied(UINT SourceId, ULONGLONG Layout, ULONG Generation);

  private:
    VioGpuAdapter *m_pAdapter;
    // The device's event, which a change pulses for helpers that know no other, and an auto-reset event per source,
    // which stays set until the source's helper thread waits on it: a pulse is lost on a thread that is not waiting at
    // that moment. Each pointer goes from NULL to its event once (CreateEvents), and back only when the component
    // stops, when nothing sets the events any more: they are read without a lock.
    PKEVENT m_pEvent;
    HANDLE m_EventHandle;
    PKEVENT m_SourceEvents[MAX_VIEWS];
    HANDLE m_SourceEventHandles[MAX_VIEWS];
    // Per source, the generations the helper last applied, one per layout (VIOGPU_SET_SOURCE_APPLIED), the least
    // recently set forgotten first; a Layout 0 and Generation 0 entry is free. Under m_AppliedMutex: escapes may run
    // at once, at PASSIVE_LEVEL.
    struct
    {
        ULONGLONG Layout;
        ULONG Generation;
        ULONG Stamp;
    } m_Applied[MAX_VIEWS][VIOGPU_APPLIED_LAYOUTS];
    ULONG m_AppliedStamp;
    KGUARDED_MUTEX m_AppliedMutex;
};
