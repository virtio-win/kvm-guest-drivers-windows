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

#include "viogpu.h"

class VioGpuAdapter;

// The most modes the driver offers for a display, besides the custom mode.
#define VIOGPU_MAX_MODES 64

// What the host reports about the display on a scanout, and the modes the driver offers for it. VioGpuHostDisplays
// reads it from the host, at start and then in its thread, into a copy of its own, and the controller publishes it
// (VioGpuDisplayController::Publish); the DDIs read it there. Only the escape writes it besides them, and only Custom,
// which Publish merges under the lock. State that the mode-set path writes does not belong here: the next publish would
// overwrite it.
typedef struct _VIOGPU_DISPLAY
{
    // The host enables the scanout (GET_DISPLAY_INFO's enabled flag) with a size; TRUE until the host first answers.
    BOOLEAN Connected;
    // A read of the display has been published since the device started; until then this is the display InitDisplay
    // assumes.
    BOOLEAN HostAnswered;
    // The host's size for the scanout, 0x0 while it is disabled and before the host first answers.
    ULONG HostWidth;
    ULONG HostHeight;
    // The size viogpuap applies: 1024x768 at start, then the host's, the persistent one, or the one the escape set.
    VIOGPU_DISP_MODE Custom;
    // The EDID Windows gets: the host's, or the built-in one when the host offers none or an invalid one. A disabled
    // scanout keeps the EDID it had, and its modes.
    ULONG EdidSize;
    BYTE Edid[EDID_MAX_SIZE];
    // The modes from that EDID, and 640x480 and 1024x768, each one the driver can back.
    ULONG ModeCount;
    VIOGPU_DISP_MODE Modes[VIOGPU_MAX_MODES];
} VIOGPU_DISPLAY, *PVIOGPU_DISPLAY;

// One of the device's outputs, as the driver knows it.
struct VioGpuScanout
{
    // The display on it, as published.
    VIOGPU_DISPLAY Display;
};

// The display state: the scanouts, under the display lock. The lock is a reader-writer lock, taken at PASSIVE_LEVEL
// only: the DDIs and the escapes read under it shared, Publish writes under it exclusive. Nobody holds it across a wait
// for the host or a call into dxgkrnl, and a holder never takes it again.
class VioGpuDisplayController
{
  public:
    VioGpuDisplayController(void);
    ~VioGpuDisplayController(void);
    void Init(_In_ VioGpuAdapter *pAdapter)
    {
        m_pAdapter = pAdapter;
    }
    // The scanouts the driver presents, from 0.
    UINT GetScanoutCount(void) const
    {
        return MAX_CHILDREN;
    }
    BOOLEAN CustomMode(_In_ ULONG xres, _In_ ULONG yres, _Inout_ PVIOGPU_DISP_MODE pMode);
    void Assume(UINT ScanoutId, _In_ const VIOGPU_DISPLAY *pDisplay);
    BOOLEAN Publish(UINT ScanoutId, _Inout_ PVIOGPU_DISPLAY pDisplay, BOOLEAN Event, _Out_ PBOOLEAN pWasConnected);
    BOOLEAN IsConnected(UINT ScanoutId);
    ULONG GetConnectedMask(void);
    NTSTATUS CopyEdid(UINT ScanoutId, ULONG Offset, PVOID Buffer, PULONG Length);
    ULONG GetModes(UINT ScanoutId, VIOGPU_DISP_MODE (&Modes)[VIOGPU_MAX_MODES + 1]);
    VIOGPU_DISP_MODE GetCustomMode(UINT ScanoutId);
    BOOLEAN SetPersistentMode(UINT ScanoutId, USHORT Width, USHORT Height);

  private:
    void LockShared(void);
    void LockExclusive(void);
    void Unlock(void);

  private:
    VioGpuAdapter *m_pAdapter;
    ERESOURCE m_Lock;
    VioGpuScanout m_Scanouts[MAX_CHILDREN];
};
