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
#include "bitops.h"

#if !DBG
#include "viogpu_display.tmh"
#endif

PAGED_CODE_SEG_BEGIN

VioGpuDisplayController::VioGpuDisplayController(void)
{
    PAGED_CODE();
    m_pAdapter = NULL;
    RtlZeroMemory(m_Scanouts, sizeof(m_Scanouts));
    RtlZeroMemory(m_Sources, sizeof(m_Sources));
    for (UINT SourceId = 0; SourceId < GetSourceCount(); ++SourceId)
    {
        m_Sources[SourceId].Width = NOM_WIDTH_SIZE;
        m_Sources[SourceId].Height = NOM_HEIGHT_SIZE;
        m_Sources[SourceId].Rotation = D3DKMDT_VPPR_IDENTITY;
        m_Sources[SourceId].Visible = TRUE;
    }
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
        DbgPrint(TRACE_LEVEL_FATAL, ("%s: (%dx%d) is wider or taller than 8192\n", __FUNCTION__, width, height));
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

// Clips a rectangle of the presented surface to Width x Height. FALSE when nothing of it is left.
static BOOLEAN ClipRect(_Inout_ RECT *pRect, LONG Width, LONG Height)
{
    PAGED_CODE();

    pRect->left = max(pRect->left, 0L);
    pRect->top = max(pRect->top, 0L);
    pRect->right = min(pRect->right, Width);
    pRect->bottom = min(pRect->bottom, Height);
    return pRect->left < pRect->right && pRect->top < pRect->bottom;
}

// The rectangle of a Width x Height frame buffer that a rectangle of the presented surface lands in: BltBits rotates it
// by Rotation.
static RECT FrameBufferRect(_In_ CONST RECT *pRect,
                            D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
                            LONG Width,
                            LONG Height)
{
    PAGED_CODE();

    RECT Rect = *pRect;
    switch (Rotation)
    {
        case D3DKMDT_VPPR_ROTATE90:
            Rect.left = pRect->top;
            Rect.right = pRect->bottom;
            Rect.top = Height - pRect->right;
            Rect.bottom = Height - pRect->left;
            break;
        case D3DKMDT_VPPR_ROTATE180:
            Rect.left = Width - pRect->right;
            Rect.right = Width - pRect->left;
            Rect.top = Height - pRect->bottom;
            Rect.bottom = Height - pRect->top;
            break;
        case D3DKMDT_VPPR_ROTATE270:
            Rect.left = Width - pRect->bottom;
            Rect.right = Width - pRect->top;
            Rect.top = pRect->left;
            Rect.bottom = pRect->right;
            break;
        default:
            break;
    }
    return Rect;
}

// Shows a frame buffer on a scanout, and has the host take its whole content.
NTSTATUS VioGpuDisplayController::ShowFrameBuffer(VioGpuObj *pObj, UINT ScanoutId, ULONG Width, ULONG Height)
{
    PAGED_CODE();

    // Waited for: QEMU's SET_SCANOUT fails for a resource the device did not create, and up to QEMU 11.1, on its 2D
    // device, for one it did not back.
    NTSTATUS status = m_pAdapter->GetCommands()->SetScanout(ScanoutId, pObj->GetId(), Width, Height, 0, 0, TRUE);
    if (NT_SUCCESS(status))
    {
        // The scanout shows what the frame buffer holds once these are through; the next present sends it again.
        NTSTATUS sent = m_pAdapter->GetCommands()->TransferToHost2D(pObj->GetId(), 0, Width, Height, 0, 0);
        if (NT_SUCCESS(sent))
        {
            sent = m_pAdapter->GetCommands()->ResourceFlush(pObj->GetId(), Width, Height, 0, 0);
        }
        if (!NT_SUCCESS(sent))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("%s: scanout %d shows resource %d before it is sent (0x%x): until the next present\n",
                      __FUNCTION__,
                      ScanoutId,
                      pObj->GetId(),
                      sent));
        }
    }
    return status;
}

// Makes a Width x Height frame buffer and shows it on a scanout. *ppObj receives it; on failure what was made of it
// is released. BAR 0 backs it when the driver is told to use it and it holds the mode; system memory otherwise, and
// when the BAR cannot be mapped.
NTSTATUS VioGpuDisplayController::CreateFrameBuffer(UINT ScanoutId, ULONG Width, ULONG Height, _Out_ VioGpuObj **ppObj)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s - scanout %d: (%d x %d)\n", __FUNCTION__, ScanoutId, Width, Height));

    *ppObj = NULL;
    if (!m_pAdapter->ModeFits(Width, Height))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s %dx%d is wider or taller than 8192\n", __FUNCTION__, Width, Height));
        return STATUS_INVALID_PARAMETER;
    }
    UINT size = Width * 4 * Height;
    UINT format = ColorFormat(VIOGPU_FRAMEBUFFER_FORMAT);
    VioGpuObj *pObj = NULL;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    PHYSICAL_ADDRESS bar = m_pAdapter->GetPciResources()->GetPciBar(0)->GetPA();
    if (m_pAdapter->GetVioGpu()->IsUsePhysicalMemory() && bar.QuadPart != 0 &&
        size <= m_pAdapter->GetPciResources()->GetPciBar(0)->GetSize())
    {
        status = m_pAdapter->GetResources()->Create(size, &bar, format, Width, Height, &pObj);
        if (status == STATUS_INSUFFICIENT_RESOURCES)
        {
            DbgPrint(TRACE_LEVEL_WARNING,
                     ("%s scanout %d: BAR 0 not usable for %dx%d: system memory instead\n",
                      __FUNCTION__,
                      ScanoutId,
                      Width,
                      Height));
        }
    }
    if (status == STATUS_INSUFFICIENT_RESOURCES)
    {
        status = m_pAdapter->GetResources()->Create(size, NULL, format, Width, Height, &pObj);
    }
    if (!NT_SUCCESS(status))
    {
        return status;
    }
    status = ShowFrameBuffer(pObj, ScanoutId, Width, Height);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s resource %d not shown (0x%x)\n", __FUNCTION__, pObj->GetId(), status));
        m_pAdapter->GetResources()->Release(pObj);
        return status;
    }
    *ppObj = pObj;
    return STATUS_SUCCESS;
}

// Turns a scanout off and releases the frame buffer it showed. The resource goes whether or not the scanout went off.
void VioGpuDisplayController::ReleaseFrameBuffer(VioGpuObj *pObj, UINT ScanoutId)
{
    PAGED_CODE();

    NTSTATUS status = m_pAdapter->GetCommands()->SetScanout(ScanoutId, 0, 0, 0, 0, 0, TRUE);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s: scanout %d not turned off (0x%x)\n", __FUNCTION__, ScanoutId, status));
    }
    m_pAdapter->GetResources()->Release(pObj);
}

// Shows source SourceId on scanout ScanoutId at the size Windows pinned, whether or not that size is still among the
// modes on offer, which may have changed since Windows picked it. A frame buffer of that size is shown again. Otherwise
// the old one is released first, so that only one is ever allocated: a commit that fails leaves the source without a
// frame buffer until the next one.
NTSTATUS VioGpuDisplayController::Commit(UINT SourceId,
                                         UINT ScanoutId,
                                         ULONG Width,
                                         ULONG Height,
                                         D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
{
    PAGED_CODE();
    ASSERT(SourceId < GetSourceCount() && ScanoutId < GetScanoutCount());
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("---> %s source %d on scanout %d: %dx%d\n", __FUNCTION__, SourceId, ScanoutId, Width, Height));

    // When its frame buffer goes, the source is marked changing under the lock held exclusive, which waits for the
    // presents copying into the frame buffer and keeps the next ones out of it: from here on the frame buffer is this
    // commit's alone, until it is published. A frame buffer the commit keeps goes on taking presents: their pixels
    // would be lost otherwise, as nothing repaints what they changed.
    VioGpuSource *pSource = &m_Sources[SourceId];
    LockExclusive();
    VioGpuObj *pObj = pSource->pFrameBuffer;
    UINT OldScanoutId = pSource->ScanoutId;
    BOOLEAN same = pObj != NULL && pSource->Width == Width && pSource->Height == Height;
    if (!same)
    {
        pSource->Changing = TRUE;
    }
    Unlock();

    NTSTATUS status;
    if (same)
    {
        status = ShowFrameBuffer(pObj, ScanoutId, Width, Height);
        // A local: DbgPrint does not parenthesize its level.
        UCHAR TraceLevel = NT_SUCCESS(status) ? TRACE_LEVEL_INFORMATION : TRACE_LEVEL_FATAL;
        DbgPrint(TraceLevel,
                 ("%s: scanout %d shows resource %d (%dx%d) again, 0x%x\n",
                  __FUNCTION__,
                  ScanoutId,
                  pObj->GetId(),
                  Width,
                  Height,
                  status));
    }
    else
    {
        if (pObj != NULL)
        {
            ReleaseFrameBuffer(pObj, OldScanoutId);
            pObj = NULL;
        }
        status = CreateFrameBuffer(ScanoutId, Width, Height, &pObj);
        if (NT_SUCCESS(status))
        {
            DbgPrint(TRACE_LEVEL_FATAL,
                     ("%s: scanout %d shows resource %d, %dx%d, %Iu bytes in %d run(s)\n",
                      __FUNCTION__,
                      ScanoutId,
                      pObj->GetId(),
                      Width,
                      Height,
                      pObj->GetSize(),
                      pObj->GetSGList()->NumberOfElements));
        }
        else
        {
            DbgPrint(TRACE_LEVEL_FATAL,
                     ("<--- %s %dx%d failed (0x%x): no frame buffer until the next commit\n",
                      __FUNCTION__,
                      Width,
                      Height,
                      status));
        }
    }

    LockExclusive();
    pSource->pFrameBuffer = pObj;
    pSource->ScanoutId = ScanoutId;
    pSource->Rotation = Rotation;
    if (!same && NT_SUCCESS(status))
    {
        pSource->Width = Width;
        pSource->Height = Height;
    }
    pSource->Changing = FALSE;
    Unlock();
    return status;
}

// The rotation of the source's path (DxgkDdiUpdateActiveVidPnPresentPath).
void VioGpuDisplayController::SetRotation(UINT SourceId, D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
{
    PAGED_CODE();

    if (SourceId >= GetSourceCount())
    {
        ASSERT(FALSE);
        return;
    }
    LockExclusive();
    m_Sources[SourceId].Rotation = Rotation;
    Unlock();
}

// Whether Windows shows the source. A source it hides is blacked out, after presents to it stop.
void VioGpuDisplayController::SetVisible(UINT SourceId, BOOLEAN Visible)
{
    PAGED_CODE();

    if (SourceId >= GetSourceCount())
    {
        ASSERT(FALSE);
        return;
    }
    LockExclusive();
    m_Sources[SourceId].Visible = Visible;
    Unlock();
    if (!Visible)
    {
        BlackOut(SourceId);
    }
}

// A copy of the source's state.
void VioGpuDisplayController::GetSource(UINT SourceId, _Out_ PVIOGPU_SOURCE_VIEW pView)
{
    PAGED_CODE();

    if (SourceId >= GetSourceCount())
    {
        ASSERT(FALSE);
        pView->Width = 0;
        pView->Height = 0;
        pView->Rotation = D3DKMDT_VPPR_IDENTITY;
        return;
    }
    VioGpuSource *pSource = &m_Sources[SourceId];
    LockShared();
    pView->Width = pSource->Width;
    pView->Height = pSource->Height;
    pView->Rotation = pSource->Rotation;
    Unlock();
}

// The device is about to be reset, which drops every resource the host has: each source's frame buffer is released,
// its scanout turned off first. The sources keep their size and show nothing until Windows commits again.
void VioGpuDisplayController::Close(void)
{
    PAGED_CODE();

    for (UINT SourceId = 0; SourceId < GetSourceCount(); ++SourceId)
    {
        VioGpuSource *pSource = &m_Sources[SourceId];
        LockExclusive();
        VioGpuObj *pObj = pSource->pFrameBuffer;
        pSource->pFrameBuffer = NULL;
        UINT ScanoutId = pSource->ScanoutId;
        Unlock();
        if (pObj != NULL)
        {
            ReleaseFrameBuffer(pObj, ScanoutId);
        }
    }
}

// Copies the moved and dirty rectangles of a present into the source's frame buffer, and has the host take the part of
// it they cover. The copy is under the display lock, shared: a commit waits for it, and a source it is changing is
// skipped, its frame dropped. The two commands go after the lock, as they may wait for room in the ring: a commit that
// releases the frame buffer meanwhile leaves them naming a resource the host no longer has, as IDs are not used again,
// and the host takes the transfer before a release that came later on the ring.
NTSTATUS VioGpuDisplayController::Present(UINT SourceId, _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *pPresent)
{
    PAGED_CODE();
    if (SourceId >= GetSourceCount())
    {
        ASSERT(FALSE);
        return STATUS_INVALID_PARAMETER;
    }

    VioGpuSource *pSource = &m_Sources[SourceId];
    LockShared();
    if (!pSource->Visible || pSource->Changing)
    {
        Unlock();
        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s source %d is not visible or changing\n", __FUNCTION__, SourceId));
        return STATUS_SUCCESS;
    }
    if (pSource->pFrameBuffer == NULL)
    {
        // Per frame: the commit that left it without one logged why.
        Unlock();
        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s source %d has no frame buffer\n", __FUNCTION__, SourceId));
        return STATUS_UNSUCCESSFUL;
    }

    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation = pPresent->Flags.Rotate ? pSource->Rotation : D3DKMDT_VPPR_IDENTITY;
    UINT DstBitPerPixel = BPPFromPixelFormat(VIOGPU_FRAMEBUFFER_FORMAT);
    ULONG Pitch = pSource->Width * DstBitPerPixel / BITS_PER_BYTE;
    BLT_INFO SrcBltInfo = {0};
    BLT_INFO DstBltInfo = {0};
    RECT updrect = {0};
    BOOLEAN updated = FALSE;

    DbgPrint(TRACE_LEVEL_VERBOSE,
             ("SrcBytesPerPixel = %d DstBitPerPixel = %d (%dx%d)\n",
              pPresent->BytesPerPixel,
              DstBitPerPixel,
              pSource->Width,
              pSource->Height));

    DstBltInfo.pBits = pSource->pFrameBuffer->GetVirtualAddress();
    DstBltInfo.Pitch = Pitch;
    DstBltInfo.BitsPerPel = DstBitPerPixel;
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = Rotation;
    DstBltInfo.Width = pSource->Width;
    DstBltInfo.Height = pSource->Height;

    SrcBltInfo.pBits = pPresent->pSource;
    SrcBltInfo.Pitch = pPresent->Pitch;
    SrcBltInfo.BitsPerPel = pPresent->BytesPerPixel * BITS_PER_BYTE;
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    if (Rotation == D3DKMDT_VPPR_ROTATE90 || Rotation == D3DKMDT_VPPR_ROTATE270)
    {
        SrcBltInfo.Width = DstBltInfo.Height;
        SrcBltInfo.Height = DstBltInfo.Width;
    }
    else
    {
        SrcBltInfo.Width = DstBltInfo.Width;
        SrcBltInfo.Height = DstBltInfo.Height;
    }

    // Each rectangle is clipped to the frame buffer: Windows may still present for the mode before the last commit.
    for (ULONG i = 0; i < pPresent->NumMoves + pPresent->NumDirtyRects; i++)
    {
        RECT Rect = i < pPresent->NumMoves ? pPresent->pMoves[i].DestRect
                                           : pPresent->pDirtyRect[i - pPresent->NumMoves];
        if (!ClipRect(&Rect, (LONG)SrcBltInfo.Width, (LONG)SrcBltInfo.Height))
        {
            continue;
        }
        BltBits(&DstBltInfo, &SrcBltInfo, &Rect);
        if (!updated)
        {
            updrect = Rect;
            updated = TRUE;
        }
        else
        {
            updrect.left = min(Rect.left, updrect.left);
            updrect.top = min(Rect.top, updrect.top);
            updrect.right = max(Rect.right, updrect.right);
            updrect.bottom = max(Rect.bottom, updrect.bottom);
        }
    }
    // The host takes the part of the frame buffer the copy changed, where BltBits rotated it to; the whole frame buffer
    // when Windows names no rectangle.
    if (updated)
    {
        updrect = FrameBufferRect(&updrect, Rotation, (LONG)DstBltInfo.Width, (LONG)DstBltInfo.Height);
    }
    else
    {
        updrect.right = (LONG)DstBltInfo.Width;
        updrect.bottom = (LONG)DstBltInfo.Height;
    }
    ULONG offset = (updrect.top * Pitch) + (updrect.left * ((DstBitPerPixel + BITS_PER_BYTE - 1) / BITS_PER_BYTE));

    UINT resid = pSource->pFrameBuffer->GetId();
    DbgPrint(TRACE_LEVEL_VERBOSE,
             ("offset = %lu (XxYxWxH) (%dx%dx%dx%d) vs (%dx%dx%dx%d)\n",
              offset,
              updrect.left,
              updrect.top,
              updrect.right - updrect.left,
              updrect.bottom - updrect.top,
              0,
              0,
              DstBltInfo.Width,
              DstBltInfo.Height));
    Unlock();

    m_pAdapter->GetCommands()->TransferToHost2D(resid,
                                                offset,
                                                updrect.right - updrect.left,
                                                updrect.bottom - updrect.top,
                                                updrect.left,
                                                updrect.top);
    m_pAdapter->GetCommands()->ResourceFlush(resid,
                                             updrect.right - updrect.left,
                                             updrect.bottom - updrect.top,
                                             updrect.left,
                                             updrect.top);
    return STATUS_SUCCESS;
}

// Blacks the source's whole frame buffer out, on the host too, whatever the rotation.
void VioGpuDisplayController::BlackOut(UINT SourceId)
{
    PAGED_CODE();
    if (SourceId >= GetSourceCount())
    {
        ASSERT(FALSE);
        return;
    }

    VioGpuSource *pSource = &m_Sources[SourceId];
    LockShared();
    if (pSource->pFrameBuffer == NULL || pSource->Changing)
    {
        Unlock();
        return;
    }
    UINT resid = pSource->pFrameBuffer->GetId();
    ULONG Width = pSource->Width;
    ULONG Height = pSource->Height;
    ULONG Pitch = Width * BPPFromPixelFormat(VIOGPU_FRAMEBUFFER_FORMAT) / BITS_PER_BYTE;
    RtlZeroMemory(pSource->pFrameBuffer->GetVirtualAddress(), (ULONGLONG)Height * Pitch);
    Unlock();
    // After the lock, as a present's (Present).
    m_pAdapter->GetCommands()->TransferToHost2D(resid, 0UL, Width, Height, 0, 0);
    m_pAdapter->GetCommands()->ResourceFlush(resid, Width, Height, 0, 0);
}

PAGED_CODE_SEG_END

// The bugcheck flow, which may run at any IRQL with nothing alongside it, and takes no lock: every frame buffer is
// parked, nothing is sent, and the device reset that follows destroys the resources. The bugcheck screen goes to BAR 0
// (VioGpuDod::SystemDisplayEnable).
void VioGpuDisplayController::DropFrameBuffers(void)
{
    for (UINT SourceId = 0; SourceId < GetSourceCount(); ++SourceId)
    {
        VioGpuObj *pObj = m_Sources[SourceId].pFrameBuffer;
        m_Sources[SourceId].pFrameBuffer = NULL;
        if (pObj != NULL)
        {
            m_pAdapter->GetResources()->Park(pObj);
        }
    }
}
