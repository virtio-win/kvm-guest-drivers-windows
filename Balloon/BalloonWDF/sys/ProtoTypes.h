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


/* The ID for virtio_balloon */
#define VIRTIO_ID_BALLOON	5

/* The feature bitmap for virtio balloon */
#define VIRTIO_BALLOON_F_MUST_TELL_HOST	0 /* Tell before reclaiming pages */

typedef struct _VIRTIO_BALLOON_CONFIG
{
	/* Number of pages host wants Guest to give up. */
	u32 num_pages;
	/* Number of pages we've actually got in balloon. */
	u32 actual;
}VIRTIO_BALLOON_CONFIG, *PVIRTIO_BALLOON_CONFIG;


typedef VirtIODevice VIODEVICE, *PVIODEVICE;
typedef struct virtqueue VIOQUEUE, *PVIOQUEUE;
typedef struct VirtIOBufferDescriptor VIO_SG, *PVIO_SG;

#define __DRIVER_NAME "BALLOON: "

typedef struct {
    SINGLE_LIST_ENTRY SingleListEntry;
    PMDL                PageMdl;
    PFN_NUMBER          PagePfn;
} PAGE_LIST_ENTRY, *PPAGE_LIST_ENTRY;


typedef struct _DEVICE_CONTEXT {
    WDFDEVICE           Device;                 // A WDFDEVICE handle
    WDFINTERRUPT        WdfInterrupt;
    
    PDRIVER_OBJECT      DriverObject;

    PUCHAR              PortBase;               // I/O port base address
    ULONG               PortCount;              // Number of assigned ports
    BOOLEAN             PortMapped;             // TRUE if mapped port addr
  
    PKEVENT             evLowMem;
    HANDLE              hLowMem;

    VIODEVICE           VDevice;

    PVIOQUEUE           InfVirtQueue, DefVirtQueue;

    BOOLEAN             bTellHostFirst;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

typedef struct _INTERRUPT_DATA {
    PVOID Context;
} INTERRUPT_DATA, *PINTERRUPT_DATA;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(INTERRUPT_DATA, GetInterruptData)

typedef struct _DRIVER_CONTEXT {
    volatile ULONG          num_pages;   

    ULONG                   num_pfns;
    PPFN_NUMBER             pfns_table;

    NPAGED_LOOKASIDE_LIST   LookAsideList;
    SINGLE_LIST_ENTRY       PageListHead;
    WDFSPINLOCK             SpinLock;
} DRIVER_CONTEXT, * PDRIVER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DRIVER_CONTEXT, GetDriverContext)

typedef struct _WORKITEM_CONTEXT {
    WDFDEVICE           Device;
    LONG                Diff;
} WORKITEM_CONTEXT, *PWORKITEM_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WORKITEM_CONTEXT,
                                                GetWorkItemContext)


#define arraysize(p) (sizeof(p)/sizeof((p)[0]))

#define BALLOON_MGMT_POOL_TAG 'mtlB'


DRIVER_INITIALIZE DriverEntry; 
EVT_WDF_OBJECT_CONTEXT_CLEANUP EvtDriverContextCleanup;
EVT_WDF_DRIVER_DEVICE_ADD BalloonDeviceAdd;

NTSTATUS
BalloonPrepareHardware(
    IN WDFDEVICE    Device,
    IN WDFCMRESLIST ResourceList,
    IN WDFCMRESLIST ResourceListTranslated
    );

NTSTATUS
BalloonReleaseHardware(
    IN WDFDEVICE    Device,
    IN WDFCMRESLIST ResourceList
    );

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

__inline VOID
DisableInterrupt(
    IN PDEVICE_CONTEXT devCtx
    )
{
    UNREFERENCED_PARAMETER(devCtx);
}

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
BalloonTellHost(
    IN WDFOBJECT WdfDevice, 
    IN PVIOQUEUE vq
    );

__inline BOOLEAN EnableInterrupt(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFCONTEXT Context
    )
{
    PDEVICE_CONTEXT devCtx = (PDEVICE_CONTEXT)Context;
    UNREFERENCED_PARAMETER(WdfInterrupt);

    devCtx->InfVirtQueue->vq_ops->kick(devCtx->InfVirtQueue);
    return TRUE;
}

__inline VOID 
SetBalloonSize(
    IN WDFOBJECT WdfDevice, 
    IN size_t    num
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);
    VIRTIO_BALLOON_CONFIG v;
    v.actual = num;
    VirtIODeviceSet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, actual), &v.actual, sizeof(v.actual));
}

__inline size_t 
GetBalloonSize(
    IN WDFOBJECT WdfDevice
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);
    VIRTIO_BALLOON_CONFIG v;
    VirtIODeviceGet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, num_pages), &v.num_pages, sizeof(v.num_pages));
    return v.num_pages;
}


__inline BOOLEAN
RestartInterrupt(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFCONTEXT Context
    )
{
    PDEVICE_CONTEXT devCtx = (PDEVICE_CONTEXT)Context;
    UNREFERENCED_PARAMETER(WdfInterrupt);
    devCtx->InfVirtQueue->vq_ops->restart(devCtx->InfVirtQueue);
    return TRUE;
}

BOOLEAN 
LogError(
    __in PDRIVER_OBJECT  drvObj,
    __in NTSTATUS        ErrorCode
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
