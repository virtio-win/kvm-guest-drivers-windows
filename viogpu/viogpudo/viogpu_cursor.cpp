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

#include "helper.h"
#include "driver.h"
#include "viogpudo.h"
#include "viogpu_cursor.h"
#include "baseobj.h"

#if !DBG
#include "viogpu_cursor.tmh"
#endif

// How long the cursor waits, for room in the rings and for answers together, each time it takes its mutex: the pointer
// DDIs and the commits wait for the mutex meanwhile.
#define VIOGPU_CURSOR_TIMEOUT_MS     100
// An image whose upload failed is sent again after this long (100 ns units).
#define VIOGPU_CURSOR_RETRY_INTERVAL (10000ULL * 1000)
// A max_cursor_size above this is not a cursor size: a device that uses the feature bit for something else.
#define VIOGPU_CURSOR_SIZE_SANE      1024
// The largest cursor the driver uses, whatever larger max_cursor_size the device offers: the cursor planes of current
// GPUs are 256 x 256, and every upload of a larger image would cost more.
#define POINTER_SIZE_MAX             256

PAGED_CODE_SEG_BEGIN
VioGpuCursor::VioGpuCursor(void)
{
    PAGED_CODE();

    m_pAdapter = NULL;
    m_Size = 0;
    m_DeviceSize = 0;
    KeInitializeGuardedMutex(&m_Mutex);
    m_Deadline = 0;
    m_pResource = NULL;
    m_W = 0;
    m_H = 0;
    m_HotX = 0;
    m_HotY = 0;
    m_Seq = 0;
    m_HostSeq = 0;
    m_pSaved = NULL;
    m_UploadPending = FALSE;
    m_UploadLate = STATUS_PENDING;
    m_UploadSeq = 0;
    m_FailedSeq = 0;
    m_FailedAt = 0;
    RtlZeroMemory(m_Sprite, sizeof(m_Sprite));
    for (UINT ScanoutId = 0; ScanoutId < MAX_CHILDREN; ++ScanoutId)
    {
        m_Sprite[ScanoutId].Source = D3DDDI_ID_UNINITIALIZED;
    }
    RtlZeroMemory(m_SourceSeq, sizeof(m_SourceSeq));
    m_Failures = 0;
}

VioGpuCursor::~VioGpuCursor(void)
{
    PAGED_CODE();

    delete[] m_pSaved;
}

// The cursor size: Windows reads MaxPointerWidth/Height once, after the start, so the cursor keeps the size the device
// offers now (GetMaxSize). And the copy that keeps the image across a close of the device.
NTSTATUS VioGpuCursor::Start(void)
{
    PAGED_CODE();

    m_Size = DeviceSize();
    m_DeviceSize = m_Size;
    if (m_pAdapter->GetVioGpu()->IsPointerEnabled())
    {
        // Without it the image does not outlive a close: the pointer shows again with the next shape.
        m_pSaved = new (PagedPool) BYTE[(SIZE_T)m_Size * m_Size * 4];
        if (m_pSaved == NULL)
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("%s no copy of the cursor image: after a D3 the pointer shows again with the next shape\n",
                      __FUNCTION__));
        }
        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s cursor size %u\n", __FUNCTION__, m_Size));
    }
    return STATUS_SUCCESS;
}

void VioGpuCursor::Stop(void)
{
    PAGED_CODE();

    delete[] m_pSaved;
    m_pSaved = NULL;
}

// The cursor size the device offers and the registry allows: the device's max_cursor_size, which the driver negotiates
// only when MaxCursorSize asks for more than POINTER_SIZE, at most MaxCursorSize and POINTER_SIZE_MAX; POINTER_SIZE
// otherwise.
UINT VioGpuCursor::DeviceSize(void)
{
    PAGED_CODE();

    UINT Size = POINTER_SIZE;
    UINT MaxCursorSize = m_pAdapter->GetMaxCursorSize();
    if (MaxCursorSize > VIOGPU_CURSOR_SIZE_SANE)
    {
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("%s ignores max_cursor_size %u: not a cursor size\n", __FUNCTION__, MaxCursorSize));
    }
    else if (MaxCursorSize > POINTER_SIZE)
    {
        Size = min(min(MaxCursorSize, m_pAdapter->GetVioGpu()->GetMaxCursorSizeSetting()), (UINT)POINTER_SIZE_MAX);
    }
    return Size;
}

// Makes the cursor resource when the device opens, with the hardware cursor on: in a pointer DDI its waits would hold
// up the pointer. Puts back the image the device had when it closed. Without a resource, as when the device offers
// smaller cursors than Windows was told, every shape fails at once, and Windows draws the pointer, until the device
// opens again. Hides every sprite, with the hardware cursor off too: QEMU keeps a console's cursor on across a device
// reset, and an earlier driver, or this one before HWCursor was turned off, may have left one.
void VioGpuCursor::Open(void)
{
    PAGED_CODE();

    if (m_pResource != NULL)
    {
        return;
    }
    BOOLEAN Enabled = m_pAdapter->GetVioGpu()->IsPointerEnabled();
    SIZE_T bytes = (SIZE_T)m_Size * m_Size * 4;
    VioGpuObj *obj = NULL;
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    if (Enabled)
    {
        m_DeviceSize = DeviceSize();
        if (m_DeviceSize != m_Size)
        {
            DbgPrint(TRACE_LEVEL_WARNING,
                     ("%s the device offers %u x %u cursors now, the driver keeps %u x %u\n",
                      __FUNCTION__,
                      m_DeviceSize,
                      m_DeviceSize,
                      m_Size,
                      m_Size));
        }
    }
    if (Enabled && m_DeviceSize >= m_Size)
    {
        status = m_pAdapter->GetResources()->Create((UINT)bytes,
                                                    NULL,
                                                    ColorFormat(D3DDDIFMT_A8R8G8B8),
                                                    m_Size,
                                                    m_Size,
                                                    &obj);
    }
    Lock();
    // No upload is on the ring: the reset cancelled any. The device that opened has none of the driver's images.
    m_UploadPending = FALSE;
    InterlockedExchange(&m_UploadLate, STATUS_PENDING);
    m_HostSeq = 0;
    m_FailedSeq = 0;
    if (NT_SUCCESS(status))
    {
        m_pResource = obj;
    }
    if (m_pResource != NULL && m_W != 0 && m_pSaved != NULL)
    {
        RtlCopyMemory(m_pResource->GetVirtualAddress(), m_pSaved, bytes);
        UploadLocked();
    }
    else
    {
        m_W = 0;
        m_H = 0;
    }
    for (UINT ScanoutId = 0; ScanoutId < m_pAdapter->GetController()->GetScanoutCount(); ++ScanoutId)
    {
        m_Sprite[ScanoutId].State = VIOGPU_SPRITE_HIDE_OWED;
    }
    RetryHidesLocked();
    Unlock();
    if (!Enabled)
    {
        return;
    }
    if (NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s %u x %u\n", __FUNCTION__, m_Size, m_Size));
    }
    else
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("%s no %u x %u cursor (0x%x, the device takes %u): software pointer until the device opens again\n",
                  __FUNCTION__,
                  m_Size,
                  m_Size,
                  status,
                  m_DeviceSize));
    }
}

// Sends a hide for every sprite while the device is still open, as QEMU keeps a console's cursor on across a reset (the
// reset may come before the device takes them: Open hides them again), keeps a copy of the image for Open, and frees
// the resource. The sources' images and which scanout shows each source stay.
void VioGpuCursor::Close(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    Lock();
    for (UINT ScanoutId = 0; ScanoutId < MAX_CHILDREN; ++ScanoutId)
    {
        HideLocked(ScanoutId);
        if (m_Sprite[ScanoutId].State != VIOGPU_SPRITE_HIDDEN)
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("%s the sprite on scanout %u may stay on the host\n", __FUNCTION__, ScanoutId));
        }
        m_Sprite[ScanoutId].State = VIOGPU_SPRITE_HIDDEN;
        m_Sprite[ScanoutId].Seq = 0;
    }
    VioGpuObj *pObj = m_pResource;
    m_pResource = NULL;
    if (pObj != NULL && m_W != 0 && m_pSaved != NULL)
    {
        RtlCopyMemory(m_pSaved, pObj->GetVirtualAddress(), (SIZE_T)m_Size * m_Size * 4);
    }
    m_HostSeq = 0;
    Unlock();
    // Outside the mutex: its waits are not the cursor's. A transfer still on the ring comes before the detach, on the
    // same queue in order, so the pages outlive it.
    if (pObj != NULL)
    {
        m_pAdapter->GetResources()->Release(pObj);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

// Takes a source's pointer shape. A shape the call accepts is on the host before it returns: a failure has a fallback
// here, as Windows then draws the pointer itself, and none in SetPointerPosition. Windows sets the same shape on every
// source, so the first call uploads a new image and the others find it there. A shape that fails hides the source's
// sprite.
NTSTATUS VioGpuCursor::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape,
                                       _In_ CONST VIOGPU_SOURCE_VIEW *pSource)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE,
             ("<--> %s flag = %d pitch = %d, pixels = %p, id = %d, w = %d, h = %d, x = %d, y = %d\n",
              __FUNCTION__,
              pSetPointerShape->Flags.Value,
              pSetPointerShape->Pitch,
              pSetPointerShape->pPixels,
              pSetPointerShape->VidPnSourceId,
              pSetPointerShape->Width,
              pSetPointerShape->Height,
              pSetPointerShape->XHot,
              pSetPointerShape->YHot));

    UINT SourceId = pSetPointerShape->VidPnSourceId;
    ASSERT(SourceId < MAX_VIEWS);
    NTSTATUS status;
    Lock();
    UINT ScanoutId = ScanoutOfLocked(SourceId);
    if (!pSetPointerShape->Flags.Color)
    {
        // Not in PointerCaps: Windows draws monochrome and masked-color pointers itself.
        status = STATUS_NOT_SUPPORTED;
    }
    else if (m_pResource == NULL)
    {
        status = STATUS_UNSUCCESSFUL;
    }
    else if (pSource->Rotation != D3DKMDT_VPPR_IDENTITY)
    {
        // The host draws the image unrotated, at scanout positions, which are Windows' positions only on an unrotated
        // source.
        status = STATUS_NOT_SUPPORTED;
    }
    else
    {
        status = LoadShapeLocked(pSetPointerShape);
        if (NT_SUCCESS(status))
        {
            status = UploadLocked();
        }
    }
    if (NT_SUCCESS(status) && ScanoutId < MAX_CHILDREN && ShownLocked(ScanoutId) && m_Sprite[ScanoutId].Seq != m_Seq)
    {
        // The sprite on screen takes the new image with its hot spot where the old one's was: the top left Windows
        // gives next for the new shape.
        VIOGPU_SPRITE *sprite = &m_Sprite[ScanoutId];
        status = ShowLocked(ScanoutId,
                            sprite->X + (INT)sprite->HotX - (INT)pSetPointerShape->XHot,
                            sprite->Y + (INT)sprite->HotY - (INT)pSetPointerShape->YHot);
    }
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_VERBOSE,
                 ("<--- %s source %u, shape 0x%x %u x %u: 0x%x\n",
                  __FUNCTION__,
                  SourceId,
                  pSetPointerShape->Flags.Value,
                  pSetPointerShape->Width,
                  pSetPointerShape->Height,
                  status));
        if (ScanoutId < MAX_CHILDREN)
        {
            HideLocked(ScanoutId);
        }
    }
    m_SourceSeq[SourceId] = NT_SUCCESS(status) ? m_Seq : 0;
    Unlock();
    return status;
}

// Moves or shows a source's sprite. The position is the image's top left, left of or above the source for a pointer
// near its left or top edge. The sprite shows only the source's own image (m_SourceSeq), and only when some of it is on
// the source; it is hidden otherwise, as on a rotated source, where Windows draws the pointer.
NTSTATUS VioGpuCursor::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition,
                                          _In_ CONST VIOGPU_SOURCE_VIEW *pSource)
{
    PAGED_CODE();
    UINT SourceId = pSetPointerPosition->VidPnSourceId;
    ASSERT(SourceId < MAX_VIEWS);
    INT x = pSetPointerPosition->X;
    INT y = pSetPointerPosition->Y;
    NTSTATUS status = STATUS_SUCCESS;
    Lock();
    UINT ScanoutId = ScanoutOfLocked(SourceId);
    if (ScanoutId < MAX_CHILDREN)
    {
        ULONG Own = m_SourceSeq[SourceId];
        BOOLEAN visible = pSetPointerPosition->Flags.Visible && Own != 0 &&
                          pSource->Rotation == D3DKMDT_VPPR_IDENTITY && x > -(INT)m_W && y > -(INT)m_H &&
                          x < (INT)pSource->Width && y < (INT)pSource->Height;

        DbgPrint(TRACE_LEVEL_VERBOSE,
                 ("---> %s (%d - %d) Visible = %d Value = %x VidPnSourceId = %d state = %d\n",
                  __FUNCTION__,
                  x,
                  y,
                  pSetPointerPosition->Flags.Visible,
                  pSetPointerPosition->Flags.Value,
                  pSetPointerPosition->VidPnSourceId,
                  m_Sprite[ScanoutId].State));

        if (!visible)
        {
            HideLocked(ScanoutId);
        }
        else if (ShownLocked(ScanoutId) && m_Sprite[ScanoutId].Seq == Own)
        {
            status = m_Commands.MoveCursor(ScanoutId, m_pResource->GetId(), x, y);
            NoteLocked(status, "move", (INT)ScanoutId);
            if (NT_SUCCESS(status))
            {
                m_Sprite[ScanoutId].X = x;
                m_Sprite[ScanoutId].Y = y;
            }
        }
        else if (Own == m_Seq && m_pResource != NULL && m_W != 0)
        {
            // A hidden sprite, or one with an older image: MOVE_CURSOR attaches no image, UPDATE_CURSOR does. The host
            // lacks the image the resource holds only when its upload at the device's open failed: it goes again first,
            // within the upload's limits, or the sprite stays hidden.
            status = UploadLocked();
            if (NT_SUCCESS(status))
            {
                status = ShowLocked(ScanoutId, x, y);
            }
            else
            {
                HideLocked(ScanoutId);
            }
        }
        else
        {
            // The source's image is no longer in the resource: its next shape brings the new one.
            HideLocked(ScanoutId);
        }
        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s status = 0x%x\n", __FUNCTION__, status));
    }
    Unlock();
    return status;
}

// A commit of a source: the source's pointer goes to the scanout that shows the source's frame buffer, so its sprite
// does not stay on a scanout that shows it no more, nor another source's on the one that shows it now. The scanout of
// the path is not enough: a path whose frame buffer could not be made shows nothing, and the scanout it names may still
// show another source.
void VioGpuCursor::SourceCommitted(UINT SourceId, UINT OldScanoutId, UINT NewScanoutId)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(OldScanoutId);
    UNREFERENCED_PARAMETER(NewScanoutId);
    VIOGPU_SOURCE_VIEW View;
    m_pAdapter->GetController()->GetSource(SourceId, &View);
    Lock();
    for (UINT ScanoutId = 0; ScanoutId < MAX_CHILDREN; ++ScanoutId)
    {
        if (m_Sprite[ScanoutId].Source == SourceId && ScanoutId != View.ShownOn)
        {
            HideLocked(ScanoutId);
            m_Sprite[ScanoutId].Source = D3DDDI_ID_UNINITIALIZED;
        }
    }
    if (View.ShownOn < MAX_CHILDREN && m_Sprite[View.ShownOn].Source != SourceId)
    {
        HideLocked(View.ShownOn);
        m_Sprite[View.ShownOn].Source = SourceId;
    }
    Unlock();
}

// Takes the mutex, and starts the time every cursor command sent under it shares (m_Deadline, m_Commands). Takes in the
// answer to an upload that came after its wait gave up, and retries the hides the device did not take.
void VioGpuCursor::Lock(void)
{
    PAGED_CODE();

    KeAcquireGuardedMutex(&m_Mutex);
    m_Deadline = KeQueryInterruptTime() + 10000ULL * VIOGPU_CURSOR_TIMEOUT_MS;
    m_Commands = m_pAdapter->GetCommands()->Until(m_Deadline);
    UploadBusyLocked();
    RetryHidesLocked();
}

void VioGpuCursor::Unlock(void)
{
    PAGED_CODE();

    KeReleaseGuardedMutex(&m_Mutex);
}

// The scanout of a source's path, as the controller last told, MAX_CHILDREN for none: the cursor's own record, in step
// with its sprites, where the controller may already name the scanout a commit is moving the source to.
UINT VioGpuCursor::ScanoutOfLocked(UINT SourceId) const
{
    PAGED_CODE();

    for (UINT ScanoutId = 0; ScanoutId < MAX_CHILDREN; ++ScanoutId)
    {
        if (m_Sprite[ScanoutId].Source == SourceId)
        {
            return ScanoutId;
        }
    }
    return MAX_CHILDREN;
}

// Takes the sprite off a scanout: resource 0 turns the host's cursor off (QEMU takes the resource id as its "on" flag),
// where a sprite only moved would stay on. Windows shows it again with the next visible position of the source shown
// there. A hide the device does not take is owed, and retried whenever the mutex is taken.
void VioGpuCursor::HideLocked(UINT ScanoutId)
{
    PAGED_CODE();

    ASSERT(ScanoutId < MAX_CHILDREN);
    VIOGPU_SPRITE *sprite = &m_Sprite[ScanoutId];
    if (sprite->State == VIOGPU_SPRITE_HIDDEN)
    {
        return;
    }
    NTSTATUS status = m_Commands.UpdateCursor(ScanoutId, 0, 0, 0, 0, 0);
    NoteLocked(status, "hide", (INT)ScanoutId);
    sprite->State = NT_SUCCESS(status) ? VIOGPU_SPRITE_HIDDEN : VIOGPU_SPRITE_HIDE_OWED;
}

// Hides the sprites whose hide the device did not take, up to the first that fails again: a full ring fails them all.
void VioGpuCursor::RetryHidesLocked(void)
{
    PAGED_CODE();

    for (UINT ScanoutId = 0; ScanoutId < MAX_CHILDREN; ++ScanoutId)
    {
        if (m_Sprite[ScanoutId].State == VIOGPU_SPRITE_HIDE_OWED)
        {
            HideLocked(ScanoutId);
            if (m_Sprite[ScanoutId].State == VIOGPU_SPRITE_HIDE_OWED)
            {
                break;
            }
        }
    }
}

// Puts a shape in the image, unless the image holds it already. Windows sets the same shape on every source, and sets
// it again when another source shows the pointer: a comparison costs a memcmp, an upload a round trip to the host.
// STATUS_INVALID_PARAMETER for an empty shape or one the resource does not hold; STATUS_DEVICE_BUSY for a new image
// while an upload is still on the ring, which reads the resource. Color shapes only, A8R8G8B8 like the resource.
NTSTATUS VioGpuCursor::LoadShapeLocked(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape)
{
    PAGED_CODE();

    UINT w = pSetPointerShape->Width;
    UINT h = pSetPointerShape->Height;
    if (w == 0 || h == 0 || w > m_Size || h > m_Size || pSetPointerShape->Pitch < w * 4)
    {
        // Windows sends none larger than MaxPointerWidth/Height.
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("<--- %s %u x %u (pitch %u) does not fit the %u x %u cursor\n",
                  __FUNCTION__,
                  w,
                  h,
                  pSetPointerShape->Pitch,
                  m_Size,
                  m_Size));
        return STATUS_INVALID_PARAMETER;
    }

    PBYTE image = (PBYTE)m_pResource->GetVirtualAddress();
    CONST BYTE *pixels = (CONST BYTE *)pSetPointerShape->pPixels;
    SIZE_T pitch = (SIZE_T)m_Size * 4;
    SIZE_T row = (SIZE_T)w * 4;
    if (w == m_W && h == m_H && pSetPointerShape->XHot == m_HotX && pSetPointerShape->YHot == m_HotY)
    {
        UINT y = 0;
        while (y < h && RtlCompareMemory(image + y * pitch, pixels + (SIZE_T)y * pSetPointerShape->Pitch, row) == row)
        {
            y++;
        }
        if (y == h)
        {
            return STATUS_SUCCESS;
        }
    }
    if (UploadBusyLocked())
    {
        NoteLocked(STATUS_DEVICE_BUSY, "shape (an upload is unanswered)", -1);
        return STATUS_DEVICE_BUSY;
    }

    // The host shows the whole resource: what a larger shape left around this one goes.
    RtlZeroMemory(image, pitch * m_Size);
    for (UINT y = 0; y < h; y++)
    {
        RtlCopyMemory(image + y * pitch, pixels + (SIZE_T)y * pSetPointerShape->Pitch, row);
    }
    m_W = w;
    m_H = h;
    m_HotX = pSetPointerShape->XHot;
    m_HotY = pSetPointerShape->YHot;
    if (++m_Seq == 0)
    {
        // 0 is no image in m_HostSeq, m_SourceSeq and a sprite's Seq.
        m_Seq = 1;
    }
    return STATUS_SUCCESS;
}

// Whether an upload whose wait gave up is still on the ring. Takes in its answer once UploadLate has put it in the
// mailbox: for image m_UploadSeq, which was not rewritten meanwhile (LoadShapeLocked). A reset's STATUS_CANCELLED is no
// failure: Open starts again.
BOOLEAN VioGpuCursor::UploadBusyLocked(void)
{
    PAGED_CODE();

    if (m_UploadPending)
    {
        NTSTATUS status = InterlockedExchange(&m_UploadLate, STATUS_PENDING);
        if (status != STATUS_PENDING)
        {
            m_UploadPending = FALSE;
            if (NT_SUCCESS(status))
            {
                m_HostSeq = m_UploadSeq;
            }
            else if (status != STATUS_CANCELLED)
            {
                m_FailedSeq = m_UploadSeq;
                m_FailedAt = KeQueryInterruptTime();
            }
            if (status != STATUS_CANCELLED)
            {
                NoteLocked(status, "late upload", -1);
            }
        }
    }
    return m_UploadPending;
}

// Gets the image to the host if it does not have it. The device copies the resource into the sprite at UPDATE_CURSOR,
// and the cursor queue is not ordered after the control queue: an UPDATE_CURSOR that overtook the transfer would keep
// the previous image until the next UPDATE_CURSOR. So the transfer is fenced, as the virtio specification asks for the
// cursor, and waited for until m_Deadline. One whose wait gave up stays on the ring: no other is sent before its
// answer, which counts when it comes. Nothing is sent when the time is up, or for an image that failed a moment ago.
NTSTATUS VioGpuCursor::UploadLocked(void)
{
    PAGED_CODE();

    BOOLEAN busy = UploadBusyLocked();
    if (HostHasImageLocked())
    {
        return STATUS_SUCCESS;
    }
    NTSTATUS status;
    PCSTR What = "upload";
    ULONGLONG now = KeQueryInterruptTime();
    if (busy)
    {
        // The device has not answered the last one yet.
        status = STATUS_DEVICE_BUSY;
        What = "upload (an earlier one is unanswered)";
    }
    else if (m_FailedSeq == m_Seq && now < m_FailedAt + VIOGPU_CURSOR_RETRY_INTERVAL)
    {
        status = STATUS_DEVICE_BUSY;
        What = "upload (it failed less than a second ago)";
    }
    else if (now >= m_Deadline)
    {
        status = STATUS_DEVICE_BUSY;
        What = "upload (no time left under the mutex)";
    }
    else
    {
        ASSERT(m_UploadLate == STATUS_PENDING);
        m_UploadSeq = m_Seq;
        m_UploadPending = TRUE;
        status = m_Commands.TransferToHost2DAndWait(m_pResource->GetId(), m_Size, m_Size, UploadLate, this);
        if (status != STATUS_IO_TIMEOUT)
        {
            // Answered, or not sent: UploadLate does not run.
            m_UploadPending = FALSE;
            if (NT_SUCCESS(status))
            {
                m_HostSeq = m_UploadSeq;
            }
            else
            {
                m_FailedSeq = m_Seq;
                m_FailedAt = now;
            }
        }
    }
    NoteLocked(status, What, -1);
    return status;
}

// Attaches the image the host has to a scanout's sprite with its top left at x, y: UPDATE_CURSOR, at which the device
// copies the resource.
NTSTATUS VioGpuCursor::ShowLocked(UINT ScanoutId, INT x, INT y)
{
    PAGED_CODE();

    ASSERT(ScanoutId < MAX_CHILDREN && HostHasImageLocked());
    NTSTATUS status = m_Commands.UpdateCursor(ScanoutId, m_pResource->GetId(), x, y, m_HotX, m_HotY);
    NoteLocked(status, "show", (INT)ScanoutId);
    if (NT_SUCCESS(status))
    {
        VIOGPU_SPRITE *sprite = &m_Sprite[ScanoutId];
        sprite->State = VIOGPU_SPRITE_SHOWN;
        sprite->X = x;
        sprite->Y = y;
        sprite->HotX = m_HotX;
        sprite->HotY = m_HotY;
        sprite->Seq = m_Seq;
    }
    return status;
}

// Logs the first of a run of failed cursor commands, on ScanoutId (-1 for none), and the command that ends the run with
// how many failed: a pointer DDI runs per move and per frame of an animated cursor, and the queue logs what the
// deadline causes (a full ring, no answer in time) only at VERBOSE.
void VioGpuCursor::NoteLocked(NTSTATUS status, PCSTR What, INT ScanoutId)
{
    PAGED_CODE();

    if (!NT_SUCCESS(status))
    {
        if (m_Failures++ == 0)
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("%s cursor %s on scanout %d failed (0x%x); the ones after are counted\n",
                      __FUNCTION__,
                      What,
                      ScanoutId,
                      status));
        }
    }
    else if (m_Failures != 0)
    {
        DbgPrint(TRACE_LEVEL_INFORMATION,
                 ("%s cursor %s done, after %u failed cursor commands\n", __FUNCTION__, What, m_Failures));
        m_Failures = 0;
    }
}
PAGED_CODE_SEG_END

// The answer to an upload whose wait gave up, from the DPC, or from the queue's Close with the answer the device gave
// before a reset or STATUS_CANCELLED. Only puts it in the mailbox: the next holder of the mutex takes it in.
void VioGpuCursor::UploadLate(_In_opt_ PVOID ctx, _In_ NTSTATUS status)
{
    InterlockedExchange(&((VioGpuCursor *)ctx)->m_UploadLate, status);
}
