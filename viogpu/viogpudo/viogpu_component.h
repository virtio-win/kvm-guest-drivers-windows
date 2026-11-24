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

#include "helper.h"

// A part of the adapter with something to do while the device runs. The adapter lists its components in the order
// they open, each after the parts it uses, and walks the list (VioGpuAdapter::StartComponents and the rest):
//
// - StartDevice starts every component, then opens every one, in list order. The device has opened by then: a Start may
//   read its configuration, and sends nothing. Between the two the adapter reads the host's displays
//   (VioGpuHostDisplays::InitDisplays), as dxgkrnl asks about them as soon as the start returns.
// - A power-down (D1 to D3) closes every one, in the opposite order, before the device is reset; D0 opens every one
//   again once the device is open.
// - StopDevice closes every one, then stops every one once the device is reset, in the opposite order.
//
// The adapter calls Open only on a closed component, Close only on an open one, and Stop only on a closed one; Start
// once. Every call runs at PASSIVE_LEVEL, in StartDevice, SetPowerState, StopDevice or
// StopDeviceAndReleasePostDisplayOwnership, which dxgkrnl does not run alongside another DDI of the driver's that uses
// the components. The ISR and the DPC still run alongside, and so does a thread of the component's own. In the bugcheck
// flow no component is closed: the device is reset under them, at any IRQL, and nothing runs after it.
class VioGpuComponent
{
  public:
    // What the component keeps while the adapter exists, made once, such as a thread. A failure fails the start, and
    // the components started before it are stopped.
    virtual NTSTATUS Start(void)
    {
        return STATUS_SUCCESS;
    }
    // The device is open: the component may send commands. An open cannot fail, since a D0 must not: a component that
    // cannot do its work logs why and does without until the next open.
    virtual void Open(void)
    {
    }
    // The device is about to be reset, and still answers commands. When Close returns, the component sends nothing
    // more until it opens again.
    virtual void Close(void)
    {
    }
    // Frees what the component holds. It runs only after a Start that succeeded.
    virtual void Stop(void)
    {
    }

  protected:
    // A component is a member of the adapter, never deleted through this class.
    ~VioGpuComponent(void)
    {
    }
};
