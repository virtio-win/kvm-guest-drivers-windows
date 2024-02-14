/*
 * Copyright (C) 2014-2017 Red Hat, Inc.
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

#define INITGUID

#include <ntddk.h>
#include <wdf.h>
#include <bcrypt.h>
#include <wdmguid.h> // required for GUID_BUS_INTERFACE_STANDARD

#include "trace.h"

#include "osdep.h"

#include "virtio_pci.h"
#include "virtio.h"
#include "VirtIOWdf.h"

#define VIRT_RNG_MEMORY_TAG ((ULONG)'gnrV')

// {2489fc19-d0fd-4950-8386-f3da3fa80508}
DEFINE_GUID(GUID_DEVINTERFACE_VIRT_RNG,
    0x2489fc19, 0xd0fd, 0x4950, 0x83, 0x86, 0xf3, 0xda, 0x3f, 0xa8, 0x5, 0x8);

typedef struct _ReadBufferEntry
{
    SINGLE_LIST_ENTRY ListEntry;
    WDFREQUEST Request;
} READ_BUFFER_ENTRY, *PREAD_BUFFER_ENTRY;

typedef struct _DEVICE_CONTEXT {

    VIRTIO_WDF_DRIVER   VDevice;
    struct virtqueue    *VirtQueue;

    WDFINTERRUPT        WdfInterrupt;
    WDFSPINLOCK         VirtQueueLock;

    // Hold a list of allocated buffers which were put into the virt queue
    // and was not returned yet.
    SINGLE_LIST_ENTRY   ReadBuffersList;
    void *              SingleBufferVA;
    PHYSICAL_ADDRESS    SingleBufferPA;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

#ifndef _IRQL_requires_
#define _IRQL_requires_(level)
#endif

//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VirtRngEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VirtRngEvtDeviceContextCleanup;

// Context cleanup callbacks generally run at IRQL <= DISPATCH_LEVEL but
// WDFDRIVER context cleanup is guaranteed to run at PASSIVE_LEVEL.
// Annotate the prototype to make static analysis happy.
EVT_WDF_OBJECT_CONTEXT_CLEANUP _IRQL_requires_(PASSIVE_LEVEL) VirtRngEvtDriverContextCleanup;

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
