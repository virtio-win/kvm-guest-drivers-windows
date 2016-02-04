/*
 * Copyright (C) 2015-2016 Red Hat, Inc.
 *
 * This software is licensed under the GNU General Public License,
 * version 2 (GPLv2) (see COPYING for details), subject to the following
 * clarification.
 *
 * With respect to binaries built using the Microsoft(R) Windows Driver
 * Kit (WDK), GPLv2 does not extend to any code contained in or derived
 * from the WDK ("WDK Code"). As to WDK Code, by using or distributing
 * such binaries you agree to be bound by the Microsoft Software License
 * Terms for the WDK. All WDK Code is considered by the GPLv2 licensors
 * to qualify for the special exception stated in section 3 of GPLv2
 * (commonly known as the system library exception).
 *
 * There is NO WARRANTY for this software, express or implied,
 * including the implied warranties of NON-INFRINGEMENT, TITLE,
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Refer to the LICENSE file for full details of the license.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 */

#include <ntddk.h>
#include <wdf.h>

#include "trace.h"

// The bit of supported PV event.
#define PVPANIC_F_PANICKED      0

// The PV event value.
#define PVPANIC_PANICKED        (1 << PVPANIC_F_PANICKED)

typedef struct _DEVICE_CONTEXT {

    // HW Resources.
    PVOID               IoBaseAddress;
    ULONG               IoRange;
    BOOLEAN             MappedPort;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

//
// Bug check callback registration functions.
//

BOOLEAN PVPanicRegisterBugCheckCallback(IN PVOID PortAddress);
BOOLEAN PVPanicDeregisterBugCheckCallback();

//
// WDFDRIVER events.
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD PVPanicEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP PVPanicEvtDriverContextCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE PVPanicEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE PVPanicEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY PVPanicEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT PVPanicEvtDeviceD0Exit;

EVT_WDF_DEVICE_FILE_CREATE PVPanicEvtDeviceFileCreate;
