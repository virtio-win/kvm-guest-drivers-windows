/*
* Copyright (C) 2018 Red Hat, Inc.
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

// File: driver.h
// Contains necessary includes and definitions for WDF kernel driver

#include <wdm.h>
#include <wdf.h>
#include "WppTrace.h"
#include <evntrace.h>

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio.h"
#include "VirtIOWdf.h"

#define VIO_CRYPT_MEMORY_TAG ((ULONG)'prcV')

typedef struct _DEVICE_CONTEXT {

    VIRTIO_WDF_DRIVER   VDevice;
    struct virtqueue    *ControlQueue;
    struct virtqueue    *DataQueue;

    WDFINTERRUPT        WdfInterrupt;
    WDFSPINLOCK         VirtQueueLock;

    LIST_ENTRY          PendingBuffers;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

typedef struct _BUFFER_DESC
{
    LIST_ENTRY ListEntry;
    WDFREQUEST Request;
    PVOID      Buffer;
    size_t     Size;

} BUFFER_DESC, *PBUFFER_DESC;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

#ifndef _IRQL_requires_
#define _IRQL_requires_(level)
#endif

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VioCryptDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VioCryptDeviceContextCleanup;
EVT_WDF_OBJECT_CONTEXT_CLEANUP _IRQL_requires_(PASSIVE_LEVEL) VioCryptDriverContextCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE VioCryptDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VioCryptDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY VioCryptDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT VioCryptDeviceD0Exit;

EVT_WDF_INTERRUPT_ISR VioCryptInterruptIsr;
EVT_WDF_INTERRUPT_DPC VioCryptInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE VioCryptInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE VioCryptInterruptDisable;
    
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VioCryptIoControl;
EVT_WDF_IO_QUEUE_IO_STOP VioCryptIoStop;