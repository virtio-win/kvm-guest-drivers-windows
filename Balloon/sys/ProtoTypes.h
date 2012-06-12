/**********************************************************************
 * Copyright (c) 2009  Red Hat, Inc.
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
#define VIRTIO_ID_BALLOON	5

/* The feature bitmap for virtio balloon */
#define VIRTIO_BALLOON_F_MUST_TELL_HOST	0 /* Tell before reclaiming pages */
#define VIRTIO_BALLOON_F_STATS_VQ	1 /* Memory status virtqueue */

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
    PFN_NUMBER              PagePfn;
} PAGE_LIST_ENTRY, *PPAGE_LIST_ENTRY;

typedef struct _DEVICE_CONTEXT {
    WDFINTERRUPT            WdfInterrupt;
    PUCHAR                  PortBase;
    ULONG                   PortCount;
    BOOLEAN                 PortMapped;
    PKEVENT                 evLowMem;
    HANDLE                  hLowMem;
    VIODEVICE               VDevice;
    PVIOQUEUE               InfVirtQueue;
    PVIOQUEUE               DefVirtQueue;
    PVIOQUEUE               StatVirtQueue;
    BOOLEAN                 bServiceConnected;

    KEVENT                  HostAckEvent;

    volatile ULONG          num_pages;
    ULONG                   num_pfns;
    PPFN_NUMBER             pfns_table;
    NPAGED_LOOKASIDE_LIST   LookAsideList;
    SINGLE_LIST_ENTRY       PageListHead;
    PBALLOON_STAT           MemStats;

    KEVENT                  WakeUpThread;
    PKTHREAD                Thread;
    BOOLEAN                 bShutDown;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

#define arraysize(p) (sizeof(p)/sizeof((p)[0]))

#define BALLOON_MGMT_POOL_TAG 'mtlB'

EVT_WDF_DRIVER_DEVICE_ADD BalloonDeviceAdd;

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
    IN WDFOBJECT    WdfDevice
    );

NTSTATUS
BalloonInterruptDisable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
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

    devCtx->InfVirtQueue->vq_ops->enable_interrupt(devCtx->InfVirtQueue, TRUE);
    devCtx->InfVirtQueue->vq_ops->kick(devCtx->InfVirtQueue);
    devCtx->DefVirtQueue->vq_ops->enable_interrupt(devCtx->DefVirtQueue, TRUE);
    devCtx->DefVirtQueue->vq_ops->kick(devCtx->DefVirtQueue);

    if (devCtx->StatVirtQueue)
    {
       devCtx->StatVirtQueue->vq_ops->enable_interrupt(devCtx->StatVirtQueue, TRUE);
       devCtx->StatVirtQueue->vq_ops->kick(devCtx->StatVirtQueue);
    }
}

__inline
VOID
DisableInterrupt(
    IN PDEVICE_CONTEXT devCtx
    )
{
    devCtx->InfVirtQueue->vq_ops->enable_interrupt(devCtx->InfVirtQueue, FALSE);
    devCtx->DefVirtQueue->vq_ops->enable_interrupt(devCtx->DefVirtQueue, FALSE);
    if (devCtx->StatVirtQueue)
    {
       devCtx->StatVirtQueue->vq_ops->enable_interrupt(devCtx->StatVirtQueue, FALSE);
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
BalloonQueueInitialize(
    IN WDFDEVICE hDevice
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
#if (WINVER >= 0x0501)
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
#endif // (WINVER >= 0x0501)
    return FALSE;
}

#endif  // _PROTOTYPES_H_
