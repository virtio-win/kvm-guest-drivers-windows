/*
 * Main include file
 * This file contains various routines and globals
 *
 * Copyright (c) 2020-2021 Red Hat, Inc.
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
#if !defined(_PROTOTYPES_H_)
#define _PROTOTYPES_H_

#include "virtio.h"
#include "trace.h"
#include "viomem.h"
#include <initguid.h>

// 
// virtio-me GUID definition for WDF
//

DEFINE_GUID(GUID_DEVINTERFACE_VIOMEM,
	0x363e0228, 0x4f1, 0x49fb, 0x9b, 0xbe, 0xe3, 0xda, 0x99, 0xfd, 0x50, 0xe5);


typedef struct virtqueue VIOQUEUE, *PVIOQUEUE;
typedef struct VirtIOBufferDescriptor VIO_SG, *PVIO_SG;

//
// virtio-mem driver processing states
//
// VIOMEM_PROCESS_STATE_INIT - state referes to the checking if 
// a device still has memory plugged (plugged_size > 0) during
// driver initilization.
// 
// VIOMEM_PROCESS_STATE_RUNNING - state refers to a normal processing
// of plug/unplug requests by the driver.
//

#define VIOMEM_PROCESS_STATE_INIT    	0
#define VIOMEM_PROCESS_STATE_RUNNING    2

typedef struct _DEVICE_CONTEXT {
    WDFINTERRUPT            WdfInterrupt;
    VIRTIO_WDF_DRIVER       VDevice;
    PVIOQUEUE               infVirtQueue;
    WDFSPINLOCK             infVirtQueueLock;

    KEVENT                  hostAcknowledge;

	virtio_mem_req			*plugRequest;
	virtio_mem_config		MemoryConfiguration;
	virtio_mem_resp			*MemoryResponse;

	BOOLEAN					ACPIProximityIdActive;

    KEVENT                  WakeUpThread;
    PKTHREAD                Thread;
    BOOLEAN                 finishProcessing;

	UINT					state;
	//
	// Memory ranges used by converting from MDL to Ranges
	// todo: add dynamic allocation
	//

	PHYSICAL_MEMORY_RANGE	MemoryRange[255];

	RTL_BITMAP				memoryBitmapHandle;
	ULONG					*bitmapBuffer;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

#define VIRTIO_MEM_POOL_TAG 'mtmV'


#ifndef _IRQL_requires_
#define _IRQL_requires_(level)
#endif

EVT_WDF_DRIVER_DEVICE_ADD ViomemDeviceAdd;
KSTART_ROUTINE            ViomemWorkerThread;
DRIVER_INITIALIZE DriverEntry;

// Context cleanup callbacks generally run at IRQL <= DISPATCH_LEVEL but
// WDFDRIVER and WDFDEVICE cleanup is guaranteed to run at PASSIVE_LEVEL.
// Annotate the prototypes to make static analysis happy.
EVT_WDF_OBJECT_CONTEXT_CLEANUP                 _IRQL_requires_(PASSIVE_LEVEL) EvtDriverContextCleanup;
EVT_WDF_DEVICE_CONTEXT_CLEANUP                 _IRQL_requires_(PASSIVE_LEVEL) ViomemEvtDeviceContextCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE                ViomemEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE                ViomemEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY                        ViomemEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT                         ViomemEvtDeviceD0Exit;
EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED ViomemEvtDeviceD0ExitPreInterruptsDisabled;
EVT_WDF_INTERRUPT_ISR                          ViomemISR;
EVT_WDF_INTERRUPT_DPC                          ViomemDPC;
EVT_WDF_INTERRUPT_ENABLE                       ViomemEnableInterrupts;
EVT_WDF_INTERRUPT_DISABLE                      ViomemDisableInterrupts;

VOID
ViomemDPC(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
    );

BOOLEAN
ViomemISR(
    IN WDFINTERRUPT Interrupt,
    IN ULONG        MessageID
    );

NTSTATUS
ViomemEnableInterrupts(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    );

NTSTATUS
ViomemDisableInterrupts(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    );

NTSTATUS
ViomemInit(
    IN WDFOBJECT    WdfDevice
    );

VOID
ViomemTerminate(
    IN WDFOBJECT    WdfDevice
    );

__inline
VOID
EnableInterrupt(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFCONTEXT Context
    )
{
    PDEVICE_CONTEXT devCtx = (PDEVICE_CONTEXT)Context;
    UNREFERENCED_PARAMETER(WdfInterrupt);

    virtqueue_enable_cb(devCtx->infVirtQueue);
    virtqueue_kick(devCtx->infVirtQueue);

}

__inline
VOID
DisableInterrupt(
    IN PDEVICE_CONTEXT devCtx
    )
{
    virtqueue_disable_cb(devCtx->infVirtQueue);
}


NTSTATUS
ViomemCloseWorkerThread(
    IN WDFDEVICE  Device
    );

VOID
ViomemWorkerThread(
    IN PVOID pContext
    );

#endif  // _PROTOTYPES_H_
