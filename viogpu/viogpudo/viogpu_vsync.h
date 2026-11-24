/*
 * Copyright (C) 2026 Zededa, Inc.
 *
 * Written By: Mikhail Malyshev <mike.malyshev@gmail.com>
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

// The refresh rate of every mode, and of the VSync the driver reports.
#define VIOGPU_REFRESH_RATE 60

// The VSync. virtio-gpu has no vertical blank, and a display-only driver that does not report VSync must leave its
// modes' refresh rate unspecified: dxgkrnl then makes up VSync itself, at 64 Hz in a QEMU guest (measured). So the
// driver makes one up at VIOGPU_REFRESH_RATE: while Windows has VSync enabled, the device is open and a target has a
// committed path, a timer reports DXGK_INTERRUPT_DISPLAYONLY_VSYNC on every target with a committed path, as the ISR
// would; GetScanLine derives the scan line from the time since the last VSync of the 60 Hz grid the timer follows,
// whether or not it was reported. The modes' signal has no blanking
// (BuildVideoSignalInfo), the scan line has: without one, a caller waiting for the blank would wait forever.
//
// A component: the timer lives from Start to Stop, and runs only between Open and Close. A listener of the display
// controller: each commit tells it which targets have a path, and their lines.
class VioGpuVSync : public VioGpuComponent, public VioGpuDisplayListener
{
  public:
    VioGpuVSync(void);
    void Init(_In_ VioGpuAdapter *pAdapter)
    {
        m_pAdapter = pAdapter;
    }
    NTSTATUS Start(void) override;
    void Open(void) override;
    void Close(void) override;
    void Stop(void) override;
    void SourceCommitted(UINT SourceId, UINT OldScanoutId, UINT NewScanoutId) override;
    void Control(BOOLEAN Enable);
    NTSTATUS GetScanLine(_Inout_ DXGKARG_GETSCANLINE *pGetScanLine);

  private:
    void Update(void);
    static EXT_CALLBACK Tick;
    static KSYNCHRONIZE_ROUTINE Notify;

    VioGpuAdapter *m_pAdapter;
    // Made when the adapter starts its components, and deleted when it stops them.
    PEX_TIMER m_pTimer;
    // Windows has VSync enabled (Control), the device is open (Open, Close), a target has a committed path
    // (SourceCommitted), the timer runs (Update, Stop). They change only in DDIs dxgkrnl serializes:
    // DxgkDdiControlInterrupt and DxgkDdiCommitVidPn are level 2 DDIs, and the device opens and closes in level 3 ones,
    // which run alone. Tick reads m_Running.
    BOOLEAN m_Enabled;
    BOOLEAN m_Open;
    BOOLEAN m_Targets;
    volatile BOOLEAN m_Running;
    // The VSync the timer is set for (Update, Tick); Tick logs a long delay from it, and nothing else reads it.
    LONG64 m_Due;
    // VSyncs Tick could not report since the last one it did: DxgkCbSynchronizeExecution failed, or the adapter's
    // interrupts were off (Notify).
    volatile LONG m_Unreported;
    // Per target, the source whose committed path drives it, D3DDDI_ID_UNINITIALIZED for none, and the height of that
    // source's frame buffer, 0 for none. Notify and GetScanLine, which may run during a commit, read the lines.
    UINT m_Source[MAX_CHILDREN];
    volatile LONG m_Lines[MAX_CHILDREN];
};
