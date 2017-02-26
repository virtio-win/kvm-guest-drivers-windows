/*
 * Copyright (C) 2015-2017 Red Hat, Inc.
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

// Name of the symbolic link object exposed in the guest.
// The file name visible to user space is "\\.\PVPanicDevice".
#define PVPANIC_DOS_DEVICE_NAME L"\\DosDevices\\Global\\PVPanicDevice"

// IOCTLs supported by the symbolic link object.
#define IOCTL_GET_CRASH_DUMP_HEADER CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)

typedef struct _DEVICE_CONTEXT {

    // HW Resources.
    PVOID               IoBaseAddress;
    ULONG               IoRange;
    BOOLEAN             MappedPort;

    // IOCTL request queue.
    WDFQUEUE            IoctlQueue;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

#define PVPANIC_DRIVER_MEMORY_TAG (ULONG)'npVP'

// Referenced in MSDN but not declared in SDK/WDK headers.
#define DUMP_TYPE_FULL 1

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
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL PVPanicEvtQueueDeviceControl;
