/*
 * Main include file
 * This file contains various routines and globals
 *
 * Copyright (c) 2009-2017 Red Hat, Inc.
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
#include "public.h"
#include "trace.h"

/* The ID for virtio_balloon */
#define VIRTIO_ID_BALLOON    5

/* The feature bitmap for virtio balloon */
#define VIRTIO_BALLOON_F_MUST_TELL_HOST    0 /* Tell before reclaiming pages */
#define VIRTIO_BALLOON_F_STATS_VQ    1 /* Memory status virtqueue */

typedef struct _VIRTIO_BALLOON_CONFIG
{
    u32 num_pages;
    u32 actual;
}VIRTIO_BALLOON_CONFIG, *PVIRTIO_BALLOON_CONFIG;


typedef struct virtqueue VIOQUEUE, *PVIOQUEUE;
typedef struct VirtIOBufferDescriptor VIO_SG, *PVIO_SG;

#define __DRIVER_NAME "BALLOON: "

typedef struct {
    SINGLE_LIST_ENTRY       SingleListEntry;
    PMDL                    PageMdl;
} PAGE_LIST_ENTRY, *PPAGE_LIST_ENTRY;

typedef struct _DEVICE_CONTEXT {
    WDFINTERRUPT            WdfInterrupt;
    PUCHAR                  PortBase;
    ULONG                   PortCount;
    BOOLEAN                 PortMapped;
    PKEVENT                 evLowMem;
    HANDLE                  hLowMem;
    VIRTIO_WDF_DRIVER       VDevice;
    PVIOQUEUE               InfVirtQueue;
    PVIOQUEUE               DefVirtQueue;
    PVIOQUEUE               StatVirtQueue;

    WDFSPINLOCK             StatQueueLock;
    WDFSPINLOCK             InfDefQueueLock;

    KEVENT                  HostAckEvent;

    volatile ULONG          num_pages;
    ULONG                   num_pfns;
    PPFN_NUMBER             pfns_table;
    NPAGED_LOOKASIDE_LIST   LookAsideList;
    BOOLEAN                 bListInitialized;
    SINGLE_LIST_ENTRY       PageListHead;
    PBALLOON_STAT           MemStats;

    KEVENT                  WakeUpThread;
    PKTHREAD                Thread;
    BOOLEAN                 bShutDown;

#ifdef USE_BALLOON_SERVICE
    WDFREQUEST              PendingWriteRequest;
    BOOLEAN                 HandleWriteRequest;
#else // USE_BALLOON_SERVICE
    WDFWORKITEM             StatWorkItem;
    LONG                    WorkCount;
#endif //USE_BALLOON_SERVICE

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

#define BALLOON_MGMT_POOL_TAG 'mtlB'

EVT_WDF_DRIVER_DEVICE_ADD BalloonDeviceAdd;
KSTART_ROUTINE            BalloonRoutine;
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_OBJECT_CONTEXT_CLEANUP EvtDriverContextCleanup;
EVT_WDF_DEVICE_CONTEXT_CLEANUP                 BalloonEvtDeviceContextCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE                BalloonEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE                BalloonEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY                        BalloonEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT                         BalloonEvtDeviceD0Exit;
EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED BalloonEvtDeviceD0ExitPreInterruptsDisabled;
EVT_WDF_INTERRUPT_ISR                          BalloonInterruptIsr;
EVT_WDF_INTERRUPT_DPC                          BalloonInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE                       BalloonInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE                      BalloonInterruptDisable;
#ifdef USE_BALLOON_SERVICE
EVT_WDF_FILE_CLOSE                             BalloonEvtFileClose;
#else // USE_BALLOON_SERVICE
EVT_WDF_WORKITEM                               StatWorkItemWorker;
#endif // USE_BALLOON_SERVICE

VOID
BalloonInterruptDpc(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
    );

BOOLEAN
BalloonInterruptIsr(
    IN WDFINTERRUPT Interrupt,
    IN ULONG        MessageID
    );

NTSTATUS
BalloonInterruptEnable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    );

NTSTATUS
BalloonInterruptDisable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    );

NTSTATUS
BalloonInit(
    IN WDFOBJECT    WdfDevice
    );

VOID
BalloonTerm(
    IN WDFOBJECT    WdfDevice
    );

VOID
BalloonFill(
    IN WDFOBJECT WdfDevice,
    IN size_t num
    );

VOID
BalloonLeak(
    IN WDFOBJECT WdfDevice,
    IN size_t num
    );

VOID
BalloonMemStats(
    IN WDFOBJECT WdfDevice
    );

VOID
BalloonTellHost(
    IN WDFOBJECT WdfDevice,
    IN PVIOQUEUE vq
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

    virtqueue_enable_cb(devCtx->InfVirtQueue);
    virtqueue_kick(devCtx->InfVirtQueue);
    virtqueue_enable_cb(devCtx->DefVirtQueue);
    virtqueue_kick(devCtx->DefVirtQueue);

    if (devCtx->StatVirtQueue)
    {
       virtqueue_enable_cb(devCtx->StatVirtQueue);
       virtqueue_kick(devCtx->StatVirtQueue);
    }
}

__inline
VOID
DisableInterrupt(
    IN PDEVICE_CONTEXT devCtx
    )
{
    virtqueue_disable_cb(devCtx->InfVirtQueue);
    virtqueue_disable_cb(devCtx->DefVirtQueue);
    if (devCtx->StatVirtQueue)
    {
        virtqueue_disable_cb(devCtx->StatVirtQueue);
    }
}

VOID
BalloonSetSize(
    IN WDFOBJECT WdfDevice,
    IN size_t    num
    );

LONGLONG
BalloonGetSize(
    IN WDFOBJECT WdfDevice
    );

NTSTATUS
BalloonCloseWorkerThread(
    IN WDFDEVICE  Device
    );

VOID
BalloonRoutine(
    IN PVOID pContext
    );

__inline BOOLEAN
IsLowMemory(
    IN WDFOBJECT    WdfDevice
    )
{
    LARGE_INTEGER       TimeOut = {0};
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);

    if(devCtx->evLowMem)
    {
        return (STATUS_WAIT_0 == KeWaitForSingleObject(
                                 devCtx->evLowMem,
                                 Executive,
                                 KernelMode,
                                 FALSE,
                                 &TimeOut));
    }
    return FALSE;
}

#ifdef USE_BALLOON_SERVICE
NTSTATUS
BalloonQueueInitialize(
    IN WDFDEVICE hDevice
    );
#else // USE_BALLOON_SERVICE
NTSTATUS
StatInitializeWorkItem(
    IN WDFDEVICE Device
    );
#endif // USE_BALLOON_SERVICE

#endif  // _PROTOTYPES_H_
