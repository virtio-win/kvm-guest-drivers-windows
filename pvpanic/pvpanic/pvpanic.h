/*
 * Copyright (C) 2015-2017 Red Hat, Inc.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
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

#include <ntddk.h>
#include <wdf.h>

#include "trace.h"

// The bit of supported PV event.
#define PVPANIC_F_PANICKED      0
#define PVPANIC_F_CRASHLOADED   1

// The PV event value.
#define PVPANIC_PANICKED        (1 << PVPANIC_F_PANICKED)
#define PVPANIC_CRASHLOADED     (1 << PVPANIC_F_CRASHLOADED)

PUCHAR PvPanicPortAddress;
BOOLEAN bEmitCrashLoadedEvent;
UCHAR   SupportedFeature;

typedef struct _DEVICE_CONTEXT {

    // HW Resources.
    PVOID               IoBaseAddress;
    ULONG               IoRange;
    BOOLEAN             MappedPort;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

#ifndef _IRQL_requires_
#define _IRQL_requires_(level)
#endif

//
// Bug check callback registration functions.
//

VOID PVPanicRegisterBugCheckCallback(IN PVOID PortAddress);
VOID PVPanicDeregisterBugCheckCallback();

//
// WDFDRIVER events.
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD PVPanicEvtDeviceAdd;

// Context cleanup callbacks generally run at IRQL <= DISPATCH_LEVEL but
// WDFDRIVER context cleanup is guaranteed to run at PASSIVE_LEVEL.
// Annotate the prototype to make static analysis happy.
EVT_WDF_OBJECT_CONTEXT_CLEANUP _IRQL_requires_(PASSIVE_LEVEL) PVPanicEvtDriverContextCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE PVPanicEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE PVPanicEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY PVPanicEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT PVPanicEvtDeviceD0Exit;

EVT_WDF_DEVICE_FILE_CREATE PVPanicEvtDeviceFileCreate;
