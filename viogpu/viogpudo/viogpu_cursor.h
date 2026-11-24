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

#include "viogpu_commands.h"
#include "viogpu_component.h"
#include "viogpu_display.h"

class VioGpuAdapter;

// What the driver last sent, or owes, on the cursor ring for a scanout. The host follows the ring in order.
typedef enum _VIOGPU_SPRITE_STATE
{
    // No sprite: resource 0 sent, or the sprite forgotten when the device closed (Open hides every one again).
    VIOGPU_SPRITE_HIDDEN = 0,
    // A sprite Windows shows the pointer with.
    VIOGPU_SPRITE_SHOWN,
    // A sprite Windows or the driver hid, whose hide the device did not take: it may still show, until a retry gets
    // there, and counts as hidden.
    VIOGPU_SPRITE_HIDE_OWED,
} VIOGPU_SPRITE_STATE;

// The sprite on a scanout.
typedef struct _VIOGPU_SPRITE
{
    // The source whose frame buffer the scanout shows, as the last commit left it (SourceCommitted),
    // D3DDDI_ID_UNINITIALIZED for none: only that source's pointer goes there.
    UINT Source;
    VIOGPU_SPRITE_STATE State;
    // Unless hidden: the top left last sent, as Windows gives positions, and the hot spot and the image (the cursor's
    // m_Seq) the host copied into the sprite.
    INT X;
    INT Y;
    UINT HotX;
    UINT HotY;
    ULONG Seq;
} VIOGPU_SPRITE;

// The hardware cursor: the cursor resource and the image it holds, the sprite on each scanout, and the upload of a new
// image. The pointer DDIs call SetPointerShape and SetPointerPosition; each commit tells it which scanout shows the
// source (SourceCommitted); the device's open and close make and free the resource.
class VioGpuCursor : public VioGpuComponent, public VioGpuDisplayListener
{
  public:
    VioGpuCursor(void);
    ~VioGpuCursor(void);
    void Init(_In_ VioGpuAdapter *pAdapter)
    {
        m_pAdapter = pAdapter;
    }
    // The side of the cursor resource, and MaxPointerWidth/Height: POINTER_SIZE, or the larger size MaxCursorSize asks
    // for and the device offers, as when the device started (Start). No lock: written once, before Windows asks.
    UINT GetMaxSize(void) const
    {
        return m_Size;
    }
    NTSTATUS Start(void) override;
    void Open(void) override;
    void Close(void) override;
    void Stop(void) override;
    void SourceCommitted(UINT SourceId, UINT OldScanoutId, UINT NewScanoutId) override;
    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape,
                             _In_ CONST VIOGPU_SOURCE_VIEW *pSource);
    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition,
                                _In_ CONST VIOGPU_SOURCE_VIEW *pSource);

  private:
    UINT DeviceSize(void);
    void Lock(void);
    void Unlock(void);
    UINT ScanoutOfLocked(UINT SourceId) const;
    // A sprite that shows the pointer: one hidden, or whose hide is owed, does not.
    BOOLEAN ShownLocked(UINT ScanoutId) const
    {
        return m_Sprite[ScanoutId].State == VIOGPU_SPRITE_SHOWN;
    }
    // The host has the image the resource holds.
    BOOLEAN HostHasImageLocked(void) const
    {
        return m_pResource != NULL && m_W != 0 && m_HostSeq == m_Seq;
    }
    void HideLocked(UINT ScanoutId);
    void RetryHidesLocked(void);
    NTSTATUS LoadShapeLocked(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape);
    BOOLEAN UploadBusyLocked(void);
    NTSTATUS UploadLocked(void);
    static void UploadLate(_In_opt_ PVOID ctx, _In_ NTSTATUS status);
    NTSTATUS ShowLocked(UINT ScanoutId, INT x, INT y);
    void NoteLocked(NTSTATUS status, PCSTR What, INT ScanoutId);

    VioGpuAdapter *m_pAdapter;
    UINT m_Size;
    // What the device offers since it last opened: Open makes no resource when it is less than m_Size.
    UINT m_DeviceSize;
    // Serializes the pointer DDIs, which dxgkrnl keeps apart from each other but calls independently of the other
    // DDIs, against the commits (SourceCommitted) and the device's open and close. Every cursor command sent while it
    // is held goes through m_Commands, bound to one deadline, m_Deadline, VIOGPU_CURSOR_TIMEOUT_MS from when it is
    // taken (Lock), for room in the rings and for answers; the resource is made and freed outside it. Legal, as all of
    // them run at PASSIVE_LEVEL. The state below is under it, except m_UploadLate, the DPC's mailbox; m_Size,
    // m_DeviceSize and m_pSaved, and m_pResource's check in Open, are used without it only in Start, Open, Close and
    // Stop, which run alone.
    KGUARDED_MUTEX m_Mutex;
    ULONGLONG m_Deadline;
    VioGpuCommands m_Commands;
    // The image: m_pResource, m_Size pixels square (none before the device opens, after it closes, and when it could
    // not be made), holds the shape last loaded, m_W x m_H (0 x 0: none) with its hot spot at m_HotX/Y. m_Seq numbers
    // the shapes it has held: 0 before the first, skipping 0 when it wraps. The host has image m_HostSeq: the device
    // copies the resource into a sprite at each UPDATE_CURSOR, so only that image is shown. A copy, m_pSaved, keeps the
    // image from the device's close to its open: Windows may move the pointer before it sets a shape again.
    VioGpuObj *m_pResource;
    UINT m_W;
    UINT m_H;
    UINT m_HotX;
    UINT m_HotY;
    ULONG m_Seq;
    ULONG m_HostSeq;
    PBYTE m_pSaved;
    // An upload whose wait gave up stays on the ring, reading the resource, until the device answers it: while
    // m_UploadPending, no other upload is sent and the image is not rewritten. UploadLate, from the DPC, only puts the
    // answer in m_UploadLate; the holder of the mutex takes it in for image m_UploadSeq (UploadBusyLocked). An image
    // that did not get there is not sent again for a second (m_FailedSeq, m_FailedAt): every source sets the same
    // shape, and each would wait for it.
    BOOLEAN m_UploadPending;
    volatile LONG m_UploadLate;
    ULONG m_UploadSeq;
    ULONG m_FailedSeq;
    ULONGLONG m_FailedAt;
    // Per scanout. Windows hides the hardware cursor (Flags.Visible = 0) whenever it draws the pointer itself, for
    // instance while DWM composes it into the primary during a window move or resize loop, and on a source the pointer
    // left; the cursor hides it where the pointer cannot show in hardware.
    VIOGPU_SPRITE m_Sprite[MAX_CHILDREN];
    // Per source, the image its last shape put in the resource, m_Seq then, 0 when it failed: a source's sprite shows
    // only its own image, and none when Windows draws its pointer.
    ULONG m_SourceSeq[MAX_VIEWS];
    // Cursor commands that failed in a row, logged when the run starts and when it ends.
    ULONG m_Failures;
};
