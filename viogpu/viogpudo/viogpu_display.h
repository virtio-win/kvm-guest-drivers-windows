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
#include "viogpu_component.h"
#include "viogpu_queue.h"
#include "viogpum.h"

class VioGpuAdapter;

// The most modes the driver offers for a display, besides the custom mode.
#define VIOGPU_MAX_MODES 64

// What the host reports about the display on a scanout, and the modes the driver offers for it. VioGpuHostDisplays
// reads it from the host, at start and then in its thread, into a copy of its own, and the controller publishes it
// (VioGpuDisplayController::Publish); the DDIs read it there. Only the escape writes it besides them, and only Custom
// and Generation, which Publish merges under the lock. State that the mode-set path writes does not belong here: the
// next publish would overwrite it.
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
    // Changes with each change of Custom and each plug-in, to a value no scanout has had since the device started
    // (VIOGPU_SOURCE_MODE.Generation): a helper applies Custom when the generation differs from the last one it
    // applied to the display, and leaves a size the user chose alone otherwise.
    ULONG Generation;
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

// The format of every frame buffer: what the firmware's frame buffer has, and what the source modes on offer present.
#define VIOGPU_FRAMEBUFFER_FORMAT D3DDDIFMT_X8R8G8B8

// A VidPN source: what Windows committed for it, and the frame buffer that shows it.
struct VioGpuSource
{
    // The size of the source mode, which the frame buffer has: that of the last commit that made one, 1024x768 before.
    ULONG Width;
    ULONG Height;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    // The scanout the source's path drives, D3DDDI_ID_UNINITIALIZED before the first commit gives it one.
    UINT ScanoutId;
    // Windows shows the source (SetVidPnSourceVisibility): presents are dropped while it does not.
    BOOLEAN Visible;
    // A commit is changing the frame buffer: presents skip the source until the commit publishes it.
    BOOLEAN Changing;
    // The frame buffer shown on ScanoutId, or none: before the first commit, after one that failed, and once closed.
    VioGpuObj *pFrameBuffer;
};

// A part that follows what the controller commits: told after each commit, once the display lock is released, so that
// it may take its own locks and call into dxgkrnl.
class VioGpuDisplayListener
{
  public:
    // A commit of SourceId: the source's path drove OldScanoutId and drives NewScanoutId (D3DDDI_ID_UNINITIALIZED for
    // none), whether or not its frame buffer could be made; the two are equal when the path stays where it was. Told
    // at PASSIVE_LEVEL in DxgkDdiCommitVidPn, which dxgkrnl keeps apart from the other level-2 DDIs but not from the
    // pointer DDIs, once per committed source, in source order: in a commit that moves sources, a scanout may be taken
    // before the source that leaves it is told. The listener may read the source (GetSource). A rotation that
    // DxgkDdiUpdateActiveVidPnPresentPath changes (SetRotation) is not told, nor the close of the device, which drops
    // the frame buffers and keeps the paths.
    virtual void SourceCommitted(UINT SourceId, UINT OldScanoutId, UINT NewScanoutId) = 0;
    // The host's displays changed: one came, went, or changed its size or EDID, as published (Publish). ConnectedMask
    // has a bit per connected scanout. Told at PASSIVE_LEVEL from the thread that reads the host, alongside the DDIs,
    // outside the display lock. A listener that does not care leaves it out.
    virtual void DisplaysChanged(ULONG ConnectedMask)
    {
        UNREFERENCED_PARAMETER(ConnectedMask);
    }

  protected:
    ~VioGpuDisplayListener(void)
    {
    }
};

// The most listeners a controller tells.
#define VIOGPU_MAX_LISTENERS 4

// A copy of a source's state, for the code outside the controller.
typedef struct _VIOGPU_SOURCE_VIEW
{
    ULONG Width;
    ULONG Height;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
} VIOGPU_SOURCE_VIEW, *PVIOGPU_SOURCE_VIEW;

// The display state: the scanouts and the sources, under the display lock. The lock is a reader-writer lock, taken at
// PASSIVE_LEVEL only: presents, the DDIs and the escapes read under it shared; Publish, a commit and the visibility
// write under it exclusive. Nobody holds it across a wait for the host, for room in a ring or for an answer, or across
// a call into dxgkrnl, and a holder never takes it again. A commit holds it exclusive twice, briefly: to mark the
// source changing, and to publish what the host commands it sent in between, without the lock, made of it.
class VioGpuDisplayController : public VioGpuComponent
{
  public:
    VioGpuDisplayController(void);
    ~VioGpuDisplayController(void);
    void Init(_In_ VioGpuAdapter *pAdapter)
    {
        m_pAdapter = pAdapter;
    }
    // At construction, before anything commits.
    void AddListener(_In_ VioGpuDisplayListener *pListener);
    void DisplaysChanged(ULONG ConnectedMask);
    // The scanouts the driver presents, from 0.
    UINT GetScanoutCount(void) const
    {
        return MAX_CHILDREN;
    }
    // The VidPN sources, from 0.
    UINT GetSourceCount(void) const
    {
        return MAX_VIEWS;
    }
    NTSTATUS Start(void) override;
    void Close(void) override;
    BOOLEAN CustomMode(_In_ ULONG xres, _In_ ULONG yres, _Inout_ PVIOGPU_DISP_MODE pMode);
    void Assume(UINT ScanoutId, _In_ const VIOGPU_DISPLAY *pDisplay);
    BOOLEAN Publish(UINT ScanoutId, _Inout_ PVIOGPU_DISPLAY pDisplay, BOOLEAN Event, _Out_ PBOOLEAN pWasConnected);
    BOOLEAN IsConnected(UINT ScanoutId);
    ULONG GetConnectedMask(void);
    NTSTATUS CopyEdid(UINT ScanoutId, ULONG Offset, PVOID Buffer, PULONG Length);
    ULONG GetModes(UINT ScanoutId, VIOGPU_DISP_MODE (&Modes)[VIOGPU_MAX_MODES + 1]);
    VIOGPU_DISP_MODE GetCustomMode(UINT ScanoutId);
    NTSTATUS GetEscapeMode(_Out_ PVIOGPU_DISP_MODE pMode);
    NTSTATUS SetEscapeMode(USHORT Width,
                           USHORT Height,
                           _Out_ PUINT pScanoutId,
                           _Out_ PBOOLEAN pChanged,
                           _Out_ PVIOGPU_DISP_MODE pSave);
    NTSTATUS GetSourceMode(_Inout_ PVIOGPU_SOURCE_MODE pMode);
    NTSTATUS Commit(UINT SourceId,
                    UINT ScanoutId,
                    ULONG Width,
                    ULONG Height,
                    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation);
    void SetRotation(UINT SourceId, D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation);
    void SetVisible(UINT SourceId, BOOLEAN Visible);
    NTSTATUS Present(UINT SourceId, _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *pPresent);
    void BlackOut(UINT SourceId);
    void GetSource(UINT SourceId, _Out_ PVIOGPU_SOURCE_VIEW pView);
    void DropFrameBuffers(void);

  private:
    void LockShared(void);
    void LockExclusive(void);
    void Unlock(void);
    ULONG NextGeneration(void);
    BOOLEAN EscapeScanout(_Out_ PUINT pScanoutId);
    NTSTATUS CreateFrameBuffer(UINT ScanoutId, ULONG Width, ULONG Height, _Out_ VioGpuObj **ppObj);
    NTSTATUS ShowFrameBuffer(VioGpuObj *pObj, UINT ScanoutId, ULONG Width, ULONG Height);
    void ReleaseFrameBuffer(VioGpuObj *pObj, UINT ScanoutId);

  private:
    VioGpuAdapter *m_pAdapter;
    ERESOURCE m_Lock;
    VioGpuScanout m_Scanouts[MAX_CHILDREN];
    VioGpuSource m_Sources[MAX_VIEWS];
    // The last generation given to a display (NextGeneration), under the lock.
    ULONG m_GenerationSerial;
    // Scanout 0's persistent mode, 0x0 for none, as the escape asked for it: it replaces the host's size at the host's
    // first answer (Assume, Publish). From the registry when the controller starts (VioGpuDod), and from the escape
    // (SetEscapeMode), whose caller saves it. Under the lock.
    VIOGPU_DISP_MODE m_Persistent;
    VioGpuDisplayListener *m_Listeners[VIOGPU_MAX_LISTENERS];
    UINT m_ListenerCount;
};
