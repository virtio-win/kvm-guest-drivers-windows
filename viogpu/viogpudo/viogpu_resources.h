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

#include "viogpu_commands.h"
#include "viogpu_idr.h"

// The host's resources and the guest pages behind them. Each resource gets an ID never used before (VioGpuIdr) and an
// object over the pages of a segment the adapter keeps (VioGpuObj), and is released in order: the host detaches the
// pages and drops the resource before the object goes. An object whose release got no answer waits for the device
// reset, after which the host no longer has the resource (FreeReleased).
class VioGpuResources
{
  public:
    VioGpuResources(void);
    void Init(_In_ VioGpuCommands *pCommands)
    {
        m_pCommands = pCommands;
        m_Idr.Init(1);
    }
    NTSTATUS Create(UINT size, VioGpuMemSegment *pSegment, UINT format, UINT Width, UINT Height, VioGpuObj **ppObj);
    void Release(VioGpuObj *pObj);
    void Park(VioGpuObj *pObj);
    void FreeReleased(void);

  private:
    VioGpuCommands *m_pCommands;
    VioGpuIdr m_Idr;
    // Objects whose release got no answer: they wait for the device reset (FreeReleased).
    VioGpuObj *volatile m_pReleased;
};
