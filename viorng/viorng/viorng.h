/*
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Refer to the LICENSE file for full details of the license.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 */

#define INITGUID

#include <ntddk.h>
#include <wdf.h>
#include <bcrypt.h>

#include "trace.h"

#include "osdep.h"

#include "virtio_pci.h"
#include "virtio_config.h"
#include "virtio.h"

#define VIRT_RNG_MEMORY_TAG ((ULONG)'gnrV')

// {2489fc19-d0fd-4950-8386-f3da3fa80508}
DEFINE_GUID(GUID_DEVINTERFACE_VIRT_RNG,
    0x2489fc19, 0xd0fd, 0x4950, 0x83, 0x86, 0xf3, 0xda, 0x3f, 0xa8, 0x5, 0x8);

typedef struct _ReadBufferEntry
{
    SINGLE_LIST_ENTRY ListEntry;
    WDFREQUEST Request;
    PVOID Buffer;

} READ_BUFFER_ENTRY, *PREAD_BUFFER_ENTRY;

typedef struct _DEVICE_CONTEXT {

    VirtIODevice        VirtDevice;
    struct virtqueue    *VirtQueue;

    // HW Resources
    PVOID               IoBaseAddress;
    ULONG               IoRange;
    BOOLEAN             MappedPort;

    WDFINTERRUPT        WdfInterrupt;
    WDFSPINLOCK         VirtQueueLock;

    // Hold a list of allocated buffers which were put into the virt queue
    // and was not returned yet.
    SINGLE_LIST_ENTRY   ReadBuffersList;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VirtRngEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VirtRngEvtDeviceContextCleanup;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VirtRngEvtDriverContextCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE VirtRngEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VirtRngEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY VirtRngEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT VirtRngEvtDeviceD0Exit;

EVT_WDF_INTERRUPT_ISR VirtRngEvtInterruptIsr;
EVT_WDF_INTERRUPT_DPC VirtRngEvtInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE VirtRngEvtInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE VirtRngEvtInterruptDisable;

EVT_WDF_IO_QUEUE_IO_READ VirtRngEvtIoRead;
EVT_WDF_IO_QUEUE_IO_STOP VirtRngEvtIoStop;
