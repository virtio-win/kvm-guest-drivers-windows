/*
 * This file contains definitions of named types for failover user-mode
   protocol service
 *
 * Copyright (c) 2020 Red Hat, Inc.
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

template <typename t> LPCSTR Name(ULONG val)
{
    LPCSTR GetName(const t&);
    t a;
    a = (t)val;
    return GetName(a);
}

typedef enum { eDummy1 = SERVICE_CONTROL_STOP } eServiceControl;

typedef enum _tAdapterState
{
    // if VF present, bind it to all default protocols, netkvmp is optional
    asAbsent,
    // VF is not expected here, otherwise the virtio may not work
    asStandalone,
    // VF is typically removed or not present, virtio will work when its link becomes on
    asAloneInactive,
    // VF is typically removed, virtio works
    asAloneActive,
    // VF should be bound to netkvmp only, virtio works over it
    asBoundInactive,
    // VF should be bound to netkvmp only, virtio works over it
    asBoundInitial,
    // VF should be bound to netkvmp only, virtio works over it
    asBoundActive,
    // covers invalid combinations of statuses
    asUnknown /* THE LAST!*/
} tAdapterState;

typedef enum _tBindingState
{
    bsBindAll,
    bsBindVioProt,
    bsBindOther,
    bsUnbindTcpip,
    bsBindTcpip,
    bsBindNone,
    bsBindNoChange
} tBindingState;
