/*
 * Copyright (C) 2019 Red Hat, Inc.
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

#pragma once

#define INITGUID

#include <ntddk.h>
#include <wdf.h>
#include <wdmguid.h> // required for GUID_BUS_INTERFACE_STANDARD

#include "trace.h"
#include "osdep.h"
#include "virtfs.h"
#include "virtio_pci.h"
#include "virtio.h"
#include "VirtIOWdf.h"
#include "fuse.h"

#define VIRT_FS_MEMORY_TAG ((ULONG)'sf_V')

#define MAX_FILE_SYSTEM_NAME 36

typedef struct _VIRTIO_FS_CONFIG
{
    CHAR Tag[MAX_FILE_SYSTEM_NAME];
    UINT32 RequestQueues;

} VIRTIO_FS_CONFIG, *PVIRTIO_FS_CONFIG;

typedef struct _VIRTIO_FS_REQUEST
{
    SINGLE_LIST_ENTRY ListEntry;

    // The memory object of the allocated virtio fs request. Required because
    // virtio fs requests are allocated from a look aside list.
    WDFMEMORY Handle;

    WDFREQUEST Request;

    // Device-readable part.
    PMDL InputBuffer;
    size_t InputBufferLength;

    // Device-writable part.
    PMDL OutputBuffer;
    size_t OutputBufferLength;

} VIRTIO_FS_REQUEST, *PVIRTIO_FS_REQUEST;

void FreeVirtFsRequest(IN PVIRTIO_FS_REQUEST Request);

typedef struct _DEVICE_CONTEXT {

    VIRTIO_WDF_DRIVER   VDevice;
    UINT32              RequestQueues;
    struct virtqueue    **VirtQueues;

    WDFINTERRUPT        WdfInterrupt;
    WDFSPINLOCK         *VirtQueueLocks;

    WDFLOOKASIDE        RequestsLookaside;
    SINGLE_LIST_ENTRY   RequestsList;
    WDFSPINLOCK         RequestsLock;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

#ifndef _IRQL_requires_
#define _IRQL_requires_(level)
#endif

//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VirtFsEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VirtFsEvtDeviceContextCleanup;

// Context cleanup callbacks generally run at IRQL <= DISPATCH_LEVEL but
// WDFDRIVER context cleanup is guaranteed to run at PASSIVE_LEVEL.
// Annotate the prototype to make static analysis happy.
EVT_WDF_OBJECT_CONTEXT_CLEANUP 
    _IRQL_requires_(PASSIVE_LEVEL) VirtFsEvtDriverContextCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE VirtFsEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VirtFsEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY VirtFsEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT VirtFsEvtDeviceD0Exit;

EVT_WDF_INTERRUPT_ISR VirtFsEvtInterruptIsr;
EVT_WDF_INTERRUPT_DPC VirtFsEvtInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE VirtFsEvtInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE VirtFsEvtInterruptDisable;

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VirtFsEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_STOP VirtFsEvtIoStop;
