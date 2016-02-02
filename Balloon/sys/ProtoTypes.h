/**********************************************************************
 * Copyright (c) 2009-2016 Red Hat, Inc.
 *
 * File: precomp.h
 *
 * Author(s):
 *
 * Main include file
 * This file contains various routines and globals
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
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


typedef VirtIODevice VIODEVICE, *PVIODEVICE;
typedef struct virtqueue VIOQUEUE, *PVIOQUEUE;
typedef struct VirtIOBufferDescriptor VIO_SG, *PVIO_SG;

#define __DRIVER_NAME "BALLOON: "

typedef struct {
    SINGLE_LIST_ENTRY       SingleListEntry;
    PMDL                    PageMdl;
} PAGE_LIST_ENTRY, *PPAGE_LIST_ENTRY;

typedef struct _DEVICE_CONTEXT {
    WDFINTERRUPT            WdfInterrupt;
    WDFWORKITEM             StatWorkItem;
    LONG                    WorkCount;
    PUCHAR                  PortBase;
    ULONG                   PortCount;
    BOOLEAN                 PortMapped;
    PKEVENT                 evLowMem;
    HANDLE                  hLowMem;
    VIODEVICE               VDevice;
    PVIOQUEUE               InfVirtQueue;
    PVIOQUEUE               DefVirtQueue;
    PVIOQUEUE               StatVirtQueue;

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
EVT_WDF_WORKITEM                               StatWorkItemWorker;

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

NTSTATUS
StatInitializeWorkItem(
    IN WDFDEVICE Device
    );

#endif  // _PROTOTYPES_H_
