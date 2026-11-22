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
#include "bitops.h"

#if !DBG
#include "viogpu_cursor.tmh"
#endif

PAGED_CODE_SEG_BEGIN

VioGpuCursor::VioGpuCursor(void)
{
    PAGED_CODE();
    m_pAdapter = NULL;
    m_pCursorObj = NULL;
    KeInitializeGuardedMutex(&m_Mutex);
    // No sprite is attached until the first shape is set.
    m_bHidden = TRUE;
    m_HotX = 0;
    m_HotY = 0;
}

// The device is about to be reset, which drops the cursor resource: it is released, and the next shape makes it
// again and attaches it.
void VioGpuCursor::Close(void)
{
    PAGED_CODE();
    DestroyCursor();
    m_bHidden = TRUE;
}

NTSTATUS VioGpuCursor::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape,
                                       _In_ CONST VIOGPU_SOURCE_VIEW *pSource)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_INFORMATION,
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

    if (pSetPointerShape->Flags.Monochrome)
    {
        VioGpuDbgBreak();
        return STATUS_UNSUCCESSFUL;
    }

    // The image upload and the UPDATE_CURSOR below must not interleave with a concurrent MOVE_CURSOR
    // issued by SetPointerPosition on another thread (see m_Mutex).
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    KeAcquireGuardedMutex(&m_Mutex);

    if (UpdateCursor(pSetPointerShape, pSource))
    {
        status = m_pAdapter->GetCommands()->UpdateCursor(0,
                                                         m_pCursorObj->GetId(),
                                                         0,
                                                         0,
                                                         pSetPointerShape->XHot,
                                                         pSetPointerShape->YHot);
        DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s status = 0x%x\n", __FUNCTION__, status));
        if (NT_SUCCESS(status))
        {
            // This UPDATE_CURSOR attaches the resource and shows the sprite, so any pending hide is over.
            // Remember the hot spot for the UPDATE_CURSOR that re-attaches the sprite after a later hide.
            m_HotX = pSetPointerShape->XHot;
            m_HotY = pSetPointerShape->YHot;
            m_bHidden = FALSE;
        }
    }
    else
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Failed to create or upload the cursor\n", __FUNCTION__));
    }

    KeReleaseGuardedMutex(&m_Mutex);
    return status;
}

NTSTATUS VioGpuCursor::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition,
                                          _In_ CONST VIOGPU_SOURCE_VIEW *pSource)
{
    PAGED_CODE();
    // Serialize against SetPointerShape (see m_Mutex).
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    KeAcquireGuardedMutex(&m_Mutex);
    if (m_pCursorObj != NULL)
    {
        // Out-of-range coordinates are treated as invisible, as before.
        BOOLEAN visible = pSetPointerPosition->Flags.Visible && pSetPointerPosition->X >= 0 &&
                          pSetPointerPosition->Y >= 0 && (UINT)pSetPointerPosition->X <= pSource->Width &&
                          (UINT)pSetPointerPosition->Y <= pSource->Height;

        DbgPrint(TRACE_LEVEL_VERBOSE,
                 ("---> %s (%d - %d) Visible = %d Value = %x VidPnSourceId = %d hidden = %d\n",
                  __FUNCTION__,
                  pSetPointerPosition->X,
                  pSetPointerPosition->Y,
                  pSetPointerPosition->Flags.Visible,
                  pSetPointerPosition->Flags.Value,
                  pSetPointerPosition->VidPnSourceId,
                  m_bHidden));

        if (!visible && m_bHidden)
        {
            // Already hidden, nothing to send.
            status = STATUS_SUCCESS;
        }
        else
        {
            BOOLEAN hidden = m_bHidden;
            if (!visible)
            {
                // Hide the sprite for real instead of parking it at (0, 0) with the resource still
                // attached: a parked sprite stays visible in the top left corner of the display.
                status = m_pAdapter->GetCommands()->UpdateCursor(0, 0, 0, 0, 0, 0);
                hidden = TRUE;
            }
            else if (m_bHidden)
            {
                // Coming back from a hide: MOVE_CURSOR does not re-associate a detached resource, so the
                // sprite must be re-attached with a full UPDATE_CURSOR. The image itself is still
                // uploaded, only the association has to be recreated.
                status = m_pAdapter->GetCommands()->UpdateCursor(0,
                                                                 m_pCursorObj->GetId(),
                                                                 pSetPointerPosition->X,
                                                                 pSetPointerPosition->Y,
                                                                 m_HotX,
                                                                 m_HotY);
                hidden = FALSE;
            }
            else
            {
                status = m_pAdapter->GetCommands()->MoveCursor(0,
                                                               m_pCursorObj->GetId(),
                                                               pSetPointerPosition->X,
                                                               pSetPointerPosition->Y);
            }

            DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s status = 0x%x\n", __FUNCTION__, status));
            if (NT_SUCCESS(status))
            {
                // Record the sprite as hidden or shown only once the command that does it was sent, so that a command
                // that was not is sent again with the next position.
                m_bHidden = hidden;
            }
        }
    }
    KeReleaseGuardedMutex(&m_Mutex);
    return status;
}

BOOLEAN VioGpuCursor::CreateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape)
{
    UINT format, size;
    VioGpuObj *obj = NULL;
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("---> %s - %d: (%d x %d - %d) (%d + %d)\n",
              __FUNCTION__,
              m_pAdapter->GetInstanceId(),
              pSetPointerShape->Width,
              pSetPointerShape->Height,
              pSetPointerShape->Pitch,
              pSetPointerShape->XHot,
              pSetPointerShape->YHot));

    size = POINTER_SIZE * POINTER_SIZE * 4;
    format = ColorFormat(D3DDDIFMT_A8R8G8B8);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s - (%x -> %x)\n", __FUNCTION__, D3DDDIFMT_A8R8G8B8, format));
    if (!NT_SUCCESS(m_pAdapter->GetResources()->Create(size, NULL, format, POINTER_SIZE, POINTER_SIZE, &obj)))
    {
        VioGpuDbgBreak();
        return FALSE;
    }
    m_pCursorObj = obj;
    return TRUE;
}

BOOLEAN VioGpuCursor::UpdateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape,
                                   _In_ CONST VIOGPU_SOURCE_VIEW *pSource)
{
    PAGED_CODE();
    RECT Rect;
    Rect.left = 0;
    Rect.top = 0;
    Rect.right = Rect.left + pSetPointerShape->Width;
    Rect.bottom = Rect.top + pSetPointerShape->Height;

    if ((m_pCursorObj == NULL) && !CreateCursor(pSetPointerShape))
    {
        VioGpuDbgBreak();
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Cannot create cursor\n", __FUNCTION__));
        return FALSE;
    }

    BLT_INFO DstBltInfo;
    DstBltInfo.pBits = m_pCursorObj->GetVirtualAddress();
    DstBltInfo.Pitch = POINTER_SIZE * 4;
    DstBltInfo.BitsPerPel = BPPFromPixelFormat(D3DDDIFMT_A8R8G8B8);
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    DstBltInfo.Width = POINTER_SIZE;
    DstBltInfo.Height = POINTER_SIZE;

    BLT_INFO SrcBltInfo;
    SrcBltInfo.pBits = (PVOID)pSetPointerShape->pPixels;
    SrcBltInfo.Pitch = pSetPointerShape->Pitch;
    if (pSetPointerShape->Flags.Color)
    {
        SrcBltInfo.BitsPerPel = BPPFromPixelFormat(D3DDDIFMT_A8R8G8B8);
    }
    else
    {
        VioGpuDbgBreak();
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Invalid cursor color %d\n", __FUNCTION__, pSetPointerShape->Flags.Value));
        return FALSE;
    }
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = pSource->Rotation;
    SrcBltInfo.Width = pSetPointerShape->Width;
    SrcBltInfo.Height = pSetPointerShape->Height;

    BltBits(&DstBltInfo, &SrcBltInfo, &Rect);

    if (!NT_SUCCESS(m_pAdapter->GetCommands()->TransferToHost2D(m_pCursorObj->GetId(),
                                                                0,
                                                                pSetPointerShape->Width,
                                                                pSetPointerShape->Height,
                                                                0,
                                                                0)))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Failed to upload the cursor image\n", __FUNCTION__));
        return FALSE;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

void VioGpuCursor::DestroyCursor(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    if (m_pCursorObj != NULL)
    {
        VioGpuObj *pObj = m_pCursorObj;
        m_pCursorObj = NULL;
        m_pAdapter->GetResources()->Release(pObj);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PAGED_CODE_SEG_END
