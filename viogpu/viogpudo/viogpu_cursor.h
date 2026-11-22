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

// The hardware cursor: the image Windows sets (SetPointerShape), uploaded to a resource of its own, and the sprite the
// device shows where the pointer is (SetPointerPosition). The device reset drops the resource: closing releases it, and
// the next shape makes it again.
class VioGpuCursor : public VioGpuComponent
{
  public:
    VioGpuCursor(void);
    void Init(_In_ VioGpuAdapter *pAdapter)
    {
        m_pAdapter = pAdapter;
    }
    void Close(void) override;
    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape,
                             _In_ CONST VIOGPU_SOURCE_VIEW *pSource);
    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition,
                                _In_ CONST VIOGPU_SOURCE_VIEW *pSource);

  private:
    BOOLEAN CreateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape);
    BOOLEAN UpdateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape, _In_ CONST VIOGPU_SOURCE_VIEW *pSource);
    void DestroyCursor(void);

  private:
    VioGpuAdapter *m_pAdapter;
    VioGpuObj *m_pCursorObj;
    // Serializes SetPointerShape against SetPointerPosition: dxgkrnl calls the two pointer DDIs from
    // different threads without synchronizing them. A concurrent MOVE_CURSOR can then interleave with the
    // two-step shape update (control-queue image upload followed by the cursor-queue UPDATE_CURSOR) and the
    // commands reach the device out of order, leaving a stale or stuck cursor. Both DDIs run at
    // PASSIVE_LEVEL, so a guarded mutex is legal here and may be held across the upload.
    KGUARDED_MUTEX m_Mutex;
    // Pointer visibility, guarded by m_Mutex. Windows hides the hardware cursor (Flags.Visible = 0)
    // whenever it draws the pointer itself, for instance while DWM composes it into the primary during a
    // window move or resize loop, and shows it again afterwards.
    BOOLEAN m_bHidden;
    // Hot spot of the current shape, replayed by the UPDATE_CURSOR that re-attaches the sprite after a hide.
    UINT m_HotX;
    UINT m_HotY;
};
