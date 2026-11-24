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

#include "helper.h"
#include "driver.h"
#include "viogpudo.h"
#include "viogpu_vsync.h"

#if !DBG
#include "viogpu_vsync.tmh"
#endif

// A refresh period, in 100 ns units of the interrupt time.
#define VIOGPU_VSYNC_PERIOD  (10000000LL / VIOGPU_REFRESH_RATE)
// The vertical blank: the first tenth of a period, from the VSync.
#define VIOGPU_VSYNC_BLANK   (VIOGPU_VSYNC_PERIOD / 10)
// The lines GetScanLine counts on a target without a path.
#define VIOGPU_NOMINAL_LINES 1080
// A VSync this late is logged: the guest stalled, and Windows may reset the adapter (TdrDodVSyncDelay, 2 s).
#define VIOGPU_VSYNC_STALL   (10000000LL)

// Non-paged: Tick calls it at DISPATCH_LEVEL.
static LONG64 InterruptTime(void)
{
    ULONG64 Qpc;
    return (LONG64)KeQueryInterruptTimePrecise(&Qpc);
}

// The first VSync after Time. The VSyncs fall on the whole multiples of the period, counted in interrupt time from
// boot: the phase is the same whenever the timer runs, as a display keeps scanning while its interrupt is off.
static LONG64 NextVSync(LONG64 Time)
{
    return (Time / VIOGPU_VSYNC_PERIOD + 1) * VIOGPU_VSYNC_PERIOD;
}

PAGED_CODE_SEG_BEGIN
VioGpuVSync::VioGpuVSync(void)
{
    PAGED_CODE();

    m_pAdapter = NULL;
    m_pTimer = NULL;
    m_Enabled = FALSE;
    m_Open = FALSE;
    m_Targets = FALSE;
    m_Running = FALSE;
    m_Due = 0;
    m_Unreported = 0;
    for (UINT TargetId = 0; TargetId < MAX_CHILDREN; ++TargetId)
    {
        m_Source[TargetId] = D3DDDI_ID_UNINITIALIZED;
        m_Lines[TargetId] = 0;
    }
}

// Makes the timer, before the device first opens: a driver that reports VSync has to report it, and a start fails
// without it.
NTSTATUS VioGpuVSync::Start(void)
{
    PAGED_CODE();

    m_pTimer = ExAllocateTimer(Tick, this, EX_TIMER_HIGH_RESOLUTION);
    if (m_pTimer == NULL)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s: no timer\n", __FUNCTION__));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return STATUS_SUCCESS;
}

// Deletes the timer once a callback that is running has returned: nothing reaches the adapter through it afterwards. A
// callback that sets the timer again meanwhile does nothing (ExSetTimer on a timer being deleted).
void VioGpuVSync::Stop(void)
{
    PAGED_CODE();

    m_Running = FALSE;
    ExDeleteTimer(m_pTimer, TRUE, TRUE, NULL);
    m_pTimer = NULL;
}

// The device opened: VSync runs from here on while Windows has it enabled.
void VioGpuVSync::Open(void)
{
    PAGED_CODE();

    m_Open = TRUE;
    Update();
}

// The device is about to close.
void VioGpuVSync::Close(void)
{
    PAGED_CODE();

    m_Open = FALSE;
    Update();
}

// DxgkDdiControlInterrupt for VSync. Windows turns it off after a few idle periods and on again with the next present.
// Enabled on a closed device, after a D0 that did not open it, VSync is never reported, and Windows resets the adapter
// after TdrDodVSyncDelay: that is logged.
void VioGpuVSync::Control(BOOLEAN Enable)
{
    PAGED_CODE();

    m_Enabled = Enable;
    if (Enable && !m_Open)
    {
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("<--> %s VSync enabled while the device is closed: none is reported\n", __FUNCTION__));
    }
    Update();
}

// A commit moved a source's path from OldScanoutId to NewScanoutId, D3DDDI_ID_UNINITIALIZED for none: the VSync goes to
// the targets with a path, with the lines of their source's mode, and the timer runs only while one has a path. The
// target a source left keeps another source that took it in the same commit. The lines are the height of the source's
// frame buffer, which has the target mode's size, as the driver does not scale and a rotated path keeps the unrotated
// size; after a commit that could not make the frame buffer, the height of the last one made, or 768 before any.
void VioGpuVSync::SourceCommitted(UINT SourceId, UINT OldScanoutId, UINT NewScanoutId)
{
    PAGED_CODE();

    if (OldScanoutId < MAX_CHILDREN && OldScanoutId != NewScanoutId && m_Source[OldScanoutId] == SourceId)
    {
        m_Source[OldScanoutId] = D3DDDI_ID_UNINITIALIZED;
        InterlockedExchange(&m_Lines[OldScanoutId], 0);
    }
    if (NewScanoutId < MAX_CHILDREN)
    {
        VIOGPU_SOURCE_VIEW View;
        m_pAdapter->GetController()->GetSource(SourceId, &View);
        m_Source[NewScanoutId] = SourceId;
        InterlockedExchange(&m_Lines[NewScanoutId], (LONG)View.Height);
    }
    BOOLEAN Targets = FALSE;
    for (UINT TargetId = 0; TargetId < MAX_CHILDREN; ++TargetId)
    {
        Targets = Targets || m_Source[TargetId] != D3DDDI_ID_UNINITIALIZED;
    }
    m_Targets = Targets;
    Update();
}

// Runs the timer while Windows has VSync enabled, the device is open and a target has a path; it is set for the next
// VSync. A callback of a stopped timer returns at once, unless it was past its check of m_Running: then it sets the
// timer once more. If Update started the timer again meanwhile, that set may replace Update's, and the callback it
// brings runs on as the timer's: ExSetTimer replaces a pending expiration, so one is pending at most, and both pick a
// VSync of the same grid. Otherwise the callback it brings returns at once. None of them reports a VSync once the
// adapter's interrupts are off (Notify); Stop cancels the pending one and waits for a running one.
void VioGpuVSync::Update(void)
{
    PAGED_CODE();

    BOOLEAN Run = m_Enabled && m_Open && m_Targets && m_pTimer != NULL;
    if (Run == m_Running)
    {
        return;
    }
    m_Running = Run;
    if (Run)
    {
        LONG64 Now = InterruptTime();
        m_Due = NextVSync(Now);
        ExSetTimer(m_pTimer, Now - m_Due, 0, NULL);
    }
    else
    {
        ExCancelTimer(m_pTimer, NULL);
        ULONG Unreported = (ULONG)InterlockedExchange(&m_Unreported, 0);
        if (Unreported != 0)
        {
            DbgPrint(TRACE_LEVEL_INFORMATION,
                     ("<--> %s VSync stopped after %u unreported\n", __FUNCTION__, Unreported));
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s VSync %s\n", __FUNCTION__, Run ? "on" : "off"));
}

// The scan line of a target, from the time since the last VSync of the grid, which goes on while the timer is stopped.
// A VSync starts the vertical blank, as a retrace raises it on hardware; the lines are scanned out over the rest of the
// period. A target without a path counts VIOGPU_NOMINAL_LINES, so that its scan line still runs through a frame.
// VioGpuDod::GetScanLine checks the target against the scanouts the driver presents.
NTSTATUS VioGpuVSync::GetScanLine(_Inout_ DXGKARG_GETSCANLINE *pGetScanLine)
{
    PAGED_CODE();

    if (pGetScanLine->VidPnTargetId >= MAX_CHILDREN)
    {
        return STATUS_INVALID_PARAMETER;
    }
    LONG64 Lines = InterlockedCompareExchange(&m_Lines[pGetScanLine->VidPnTargetId], 0, 0);
    if (Lines <= 0)
    {
        Lines = VIOGPU_NOMINAL_LINES;
    }
    LONG64 Phase = InterruptTime() % VIOGPU_VSYNC_PERIOD;
    pGetScanLine->InVerticalBlank = Phase < VIOGPU_VSYNC_BLANK;
    pGetScanLine->ScanLine = Phase < VIOGPU_VSYNC_BLANK ? 0
                                                        : (UINT)(Lines * (Phase - VIOGPU_VSYNC_BLANK) /
                                                                 (VIOGPU_VSYNC_PERIOD - VIOGPU_VSYNC_BLANK));
    return STATUS_SUCCESS;
}
PAGED_CODE_SEG_END

// Non-paged from here: Tick runs at DISPATCH_LEVEL, Notify at the device's interrupt level.
//
// The timer's callback. dxgkrnl takes DxgkCbNotifyInterrupt at the device's interrupt level only, from the message it
// was told about: the VSync is reported under that message's interrupt lock, as the ISR would. VSyncs that cannot be
// reported are counted: the first is logged, and the count when one is reported again or the timer stops.
//
// The timer is set for one VSync at a time, on the grid (NextVSync), and fires up to about a millisecond late; the
// next is the first of the grid at least half a period away, so two never come closer than that. A periodic timer,
// measured, ran at 59 Hz of 60, as if each period counted from when it fired. VSyncs the callback is too late for are
// skipped, as on a display; a run of them a second long is logged.
VOID VioGpuVSync::Tick(_In_ PEX_TIMER Timer, _In_opt_ PVOID Context)
{
    VioGpuVSync *pThis = reinterpret_cast<VioGpuVSync *>(Context);
    PDXGKRNL_INTERFACE pDxgkInterface = pThis->m_pAdapter->GetDxgkInterface();
    BOOLEAN Ret = FALSE;

    if (!pThis->m_Running)
    {
        return;
    }
    LONG64 Now = InterruptTime();
    LONG64 Late = Now - pThis->m_Due;
    if (Late >= VIOGPU_VSYNC_STALL)
    {
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("<--> %s VSync %I64d ms late, %I64d skipped\n",
                  __FUNCTION__,
                  Late / 10000,
                  Late / VIOGPU_VSYNC_PERIOD));
    }
    pThis->m_Due = NextVSync(Now + VIOGPU_VSYNC_PERIOD / 2);
    ExSetTimer(Timer, Now - pThis->m_Due, 0, NULL);

    NTSTATUS Status = pDxgkInterface->DxgkCbSynchronizeExecution(pDxgkInterface->DeviceHandle,
                                                                 Notify,
                                                                 pThis,
                                                                 pThis->m_pAdapter->GetNotifyMessage(),
                                                                 &Ret);
    // Notify reports nothing while the adapter's interrupts are off: a device reset that left the timer running. A
    // callback the stop came too late for may find no target left, which is no failure.
    if (!NT_SUCCESS(Status) || (!Ret && pThis->m_Running && !pThis->m_pAdapter->AreInterruptsOn()))
    {
        if (InterlockedIncrement(&pThis->m_Unreported) == 1)
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("<--> %s VSync not reported (0x%x, %d)\n", __FUNCTION__, Status, Ret));
        }
    }
    else if (pThis->m_Unreported != 0)
    {
        ULONG Unreported = (ULONG)InterlockedExchange(&pThis->m_Unreported, 0);
        DbgPrint(TRACE_LEVEL_INFORMATION,
                 ("<--> %s VSync reported again after %u unreported\n", __FUNCTION__, Unreported));
    }
}

// Under the interrupt lock. While the adapter's interrupts are off its device is closed and its ISR silent, and no
// VSync is reported either.
BOOLEAN VioGpuVSync::Notify(_In_opt_ PVOID Context)
{
    VioGpuVSync *pThis = reinterpret_cast<VioGpuVSync *>(Context);
    VioGpuAdapter *pAdapter = pThis->m_pAdapter;
    if (!pAdapter->AreInterruptsOn())
    {
        return FALSE;
    }

    PDXGKRNL_INTERFACE pDxgkInterface = pAdapter->GetDxgkInterface();
    BOOLEAN Notified = FALSE;
    for (UINT TargetId = 0; TargetId < MAX_CHILDREN; ++TargetId)
    {
        if (pThis->m_Lines[TargetId] != 0)
        {
            DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyInterrupt = {};
            NotifyInterrupt.InterruptType = DXGK_INTERRUPT_DISPLAYONLY_VSYNC;
            NotifyInterrupt.DisplayOnlyVsync.VidPnTargetId = TargetId;
            pDxgkInterface->DxgkCbNotifyInterrupt(pDxgkInterface->DeviceHandle, &NotifyInterrupt);
            Notified = TRUE;
        }
    }
    // DxgkCbNotifyDpc from the DPC completes each notification (VioGpuDod::DpcRoutine).
    if (Notified)
    {
        pDxgkInterface->DxgkCbQueueDpc(pDxgkInterface->DeviceHandle);
    }
    return Notified;
}
