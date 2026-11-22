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
#include "viogpu_display.h"

#if !DBG
#include "viogpu_display.tmh"
#endif

PAGED_CODE_SEG_BEGIN

VioGpuDisplayController::VioGpuDisplayController(void)
{
    PAGED_CODE();
    m_pAdapter = NULL;
    RtlZeroMemory(m_Scanouts, sizeof(m_Scanouts));
    ExInitializeResourceLite(&m_Lock);
}

VioGpuDisplayController::~VioGpuDisplayController(void)
{
    PAGED_CODE();
    ExDeleteResourceLite(&m_Lock);
}

void VioGpuDisplayController::LockShared(void)
{
    PAGED_CODE();
    ExEnterCriticalRegionAndAcquireResourceShared(&m_Lock);
}

void VioGpuDisplayController::LockExclusive(void)
{
    PAGED_CODE();
    ExEnterCriticalRegionAndAcquireResourceExclusive(&m_Lock);
}

void VioGpuDisplayController::Unlock(void)
{
    PAGED_CODE();
    ExReleaseResourceAndLeaveCriticalRegion(&m_Lock);
}

// The custom mode for a size, the size viogpuap applies: raised to 640x480 without FlexResolution. FALSE, leaving
// *pMode as it was, for a size the driver cannot back.
BOOLEAN VioGpuDisplayController::CustomMode(_In_ ULONG xres, _In_ ULONG yres, _Inout_ PVIOGPU_DISP_MODE pMode)
{
    PAGED_CODE();

    if (xres < MIN_WIDTH_SIZE || yres < MIN_HEIGHT_SIZE)
    {
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("%s: (%dx%d) less than (%dx%d)\n", __FUNCTION__, xres, yres, MIN_WIDTH_SIZE, MIN_HEIGHT_SIZE));
    }
    ULONG width = m_pAdapter->GetVioGpu()->IsFlexResolution() ? xres : max((ULONG)MIN_WIDTH_SIZE, xres);
    ULONG height = m_pAdapter->GetVioGpu()->IsFlexResolution() ? yres : max((ULONG)MIN_HEIGHT_SIZE, yres);
    // Checked before the sizes are narrowed to the mode's 16 bits.
    if (!m_pAdapter->ModeFits(width, height))
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("%s: (%dx%d) is more than the driver can back (8192x8192, 64 MiB, segment %d bytes)\n",
                  __FUNCTION__,
                  width,
                  height,
                  (ULONG)m_pAdapter->GetFrameSegmentSize()));
        return FALSE;
    }
    pMode->XResolution = (USHORT)width;
    pMode->YResolution = (USHORT)height;
    return TRUE;
}

// The display a scanout counts as until the host first answers, set before the config-change thread runs.
void VioGpuDisplayController::Assume(UINT ScanoutId, _In_ const VIOGPU_DISPLAY *pDisplay)
{
    PAGED_CODE();

    // Every entry point checks its id: a bad one is a bug, and indexes nothing.
    if (ScanoutId >= GetScanoutCount())
    {
        ASSERT(FALSE);
        return;
    }
    LockExclusive();
    m_Scanouts[ScanoutId].Display = *pDisplay;
    Unlock();
}

// Publishes what VioGpuHostDisplays read about a scanout, for the DDIs. A disabled scanout keeps the EDID and the modes
// it had. The custom mode takes the host's size on a display event and whenever that size changes; at the host's first
// answer the persistent mode, when set, then replaces it, as Assume set it before the answer. Returns whether the
// display changed: its connection, size or EDID; pWasConnected receives whether it was connected before.
BOOLEAN VioGpuDisplayController::Publish(UINT ScanoutId,
                                         _Inout_ PVIOGPU_DISPLAY pDisplay,
                                         BOOLEAN Event,
                                         _Out_ PBOOLEAN pWasConnected)
{
    PAGED_CODE();

    *pWasConnected = FALSE;
    if (ScanoutId >= GetScanoutCount())
    {
        ASSERT(FALSE);
        return FALSE;
    }
    PVIOGPU_DISPLAY pPublished = &m_Scanouts[ScanoutId].Display;
    LockExclusive();
    if (!pDisplay->Connected)
    {
        RtlCopyMemory(pDisplay->Edid, pPublished->Edid, pPublished->EdidSize);
        pDisplay->EdidSize = pPublished->EdidSize;
        RtlCopyMemory(pDisplay->Modes, pPublished->Modes, sizeof(pPublished->Modes));
        pDisplay->ModeCount = pPublished->ModeCount;
    }
    BOOLEAN sizeChanged = pDisplay->HostWidth != pPublished->HostWidth ||
                          pDisplay->HostHeight != pPublished->HostHeight;
    BOOLEAN changed = sizeChanged || pDisplay->Connected != pPublished->Connected ||
                      pDisplay->EdidSize != pPublished->EdidSize ||
                      RtlCompareMemory(pDisplay->Edid, pPublished->Edid, pDisplay->EdidSize) != pDisplay->EdidSize;
    // Merged here, under the lock: the escape may have just set it.
    pDisplay->Custom = pPublished->Custom;
    if (pDisplay->HostWidth != 0 && (Event || sizeChanged))
    {
        CustomMode(pDisplay->HostWidth, pDisplay->HostHeight, &pDisplay->Custom);
    }
    if (ScanoutId == 0 && !pPublished->HostAnswered && m_pAdapter->GetVioGpu()->IsPersistentDispMode0Set())
    {
        CustomMode(m_pAdapter->GetVioGpu()->GetPersistentDispMode0Width(),
                   m_pAdapter->GetVioGpu()->GetPersistentDispMode0Height(),
                   &pDisplay->Custom);
    }
    pDisplay->HostAnswered = TRUE;
    *pWasConnected = pPublished->Connected;
    *pPublished = *pDisplay;
    Unlock();

    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("%s: scanout %d, event %d, connected %d -> %d, host %dx%d, EDID %d bytes, %d modes, custom %dx%d, %s\n",
              __FUNCTION__,
              ScanoutId,
              Event,
              *pWasConnected,
              pDisplay->Connected,
              pDisplay->HostWidth,
              pDisplay->HostHeight,
              pDisplay->EdidSize,
              pDisplay->ModeCount,
              pDisplay->Custom.XResolution,
              pDisplay->Custom.YResolution,
              changed ? "changed" : "unchanged"));
    return changed;
}

// The scanouts whose display is connected, a bit each.
ULONG VioGpuDisplayController::GetConnectedMask(void)
{
    PAGED_CODE();

    ULONG connected = 0;
    LockShared();
    for (UINT ScanoutId = 0; ScanoutId < GetScanoutCount(); ++ScanoutId)
    {
        if (m_Scanouts[ScanoutId].Display.Connected)
        {
            connected |= 1UL << ScanoutId;
        }
    }
    Unlock();
    return connected;
}

// A scanout's display is connected while the host enables the scanout with a size.
BOOLEAN VioGpuDisplayController::IsConnected(UINT ScanoutId)
{
    PAGED_CODE();

    if (ScanoutId >= GetScanoutCount())
    {
        return FALSE;
    }
    LockShared();
    BOOLEAN connected = m_Scanouts[ScanoutId].Display.Connected;
    Unlock();
    return connected;
}

// Copies part of the EDID of a scanout's display for Windows, which reads it a block at a time. Past its end the answer
// is STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA. A publish between two blocks can mix two EDIDs: Windows is told about the
// display after the publish, and reads the new one again.
NTSTATUS VioGpuDisplayController::CopyEdid(UINT ScanoutId, ULONG Offset, PVOID Buffer, PULONG Length)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    if (ScanoutId >= GetScanoutCount())
    {
        return status;
    }
    PVIOGPU_DISPLAY pDisplay = &m_Scanouts[ScanoutId].Display;
    LockShared();
    ULONG size = pDisplay->EdidSize;
    if (Offset < size)
    {
        *Length = min(*Length, size - Offset);
        RtlCopyMemory(Buffer, pDisplay->Edid + Offset, *Length);
        status = STATUS_SUCCESS;
    }
    Unlock();
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("%s: Windows reads %d bytes at %d of %d of scanout %d's EDID\n",
              __FUNCTION__,
              NT_SUCCESS(status) ? *Length : 0,
              Offset,
              size,
              ScanoutId));
    return status;
}

// Copies the modes on offer for a scanout's display into Modes: the display's modes, then the custom mode. Returns how
// many there are.
ULONG VioGpuDisplayController::GetModes(UINT ScanoutId, VIOGPU_DISP_MODE (&Modes)[VIOGPU_MAX_MODES + 1])
{
    PAGED_CODE();

    ASSERT(ScanoutId < GetScanoutCount());
    PVIOGPU_DISPLAY pDisplay = &m_Scanouts[ScanoutId].Display;
    LockShared();
    ULONG count = pDisplay->ModeCount;
    RtlCopyMemory(Modes, pDisplay->Modes, count * sizeof(VIOGPU_DISP_MODE));
    Modes[count++] = pDisplay->Custom;
    Unlock();
    return count;
}

// The custom mode of a scanout's display, the size viogpuap applies.
VIOGPU_DISP_MODE VioGpuDisplayController::GetCustomMode(UINT ScanoutId)
{
    PAGED_CODE();

    ASSERT(ScanoutId < GetScanoutCount());
    LockShared();
    VIOGPU_DISP_MODE mode = m_Scanouts[ScanoutId].Display.Custom;
    Unlock();
    return mode;
}

// The size the custom-resolution escape sets: it becomes the custom mode of a scanout's display and the persistent
// mode, which replaces the host's size at the host's first answer. FALSE, changing nothing, for a size the driver
// cannot back. The caller saves the persistent mode in the registry, outside the lock.
BOOLEAN VioGpuDisplayController::SetPersistentMode(UINT ScanoutId, USHORT Width, USHORT Height)
{
    PAGED_CODE();

    ASSERT(ScanoutId < GetScanoutCount());
    VIOGPU_DISP_MODE mode;
    if (!CustomMode(Width, Height, &mode))
    {
        return FALSE;
    }
    // Under the display lock, under which Publish reads the persistent mode.
    LockExclusive();
    m_pAdapter->GetVioGpu()->SetPersistentDispMode0Width(Width);
    m_pAdapter->GetVioGpu()->SetPersistentDispMode0Height(Height);
    m_Scanouts[ScanoutId].Display.Custom = mode;
    Unlock();
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("%s scanout %d: custom mode %dx%d\n", __FUNCTION__, ScanoutId, mode.XResolution, mode.YResolution));
    return TRUE;
}

PAGED_CODE_SEG_END
