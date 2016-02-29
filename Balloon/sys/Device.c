/**********************************************************************
 * Copyright (c) 2009-2016  Red Hat, Inc.
 *
 * File: device.c
 *
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This file contains balloon driver routines
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "precomp.h"

#if defined(EVENT_TRACING)
#include "device.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, BalloonEvtDeviceContextCleanup)
#pragma alloc_text(PAGE, BalloonEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, BalloonEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, BalloonEvtDeviceD0Exit)
#pragma alloc_text(PAGE, BalloonEvtDeviceD0ExitPreInterruptsDisabled)
#pragma alloc_text(PAGE, BalloonDeviceAdd)
#pragma alloc_text(PAGE, BalloonEvtFileClose)
#pragma alloc_text(PAGE, BalloonCloseWorkerThread)
#endif

#define LOMEMEVENTNAME L"\\KernelObjects\\LowMemoryCondition"
DECLARE_CONST_UNICODE_STRING(evLowMemString, LOMEMEVENTNAME);


NTSTATUS
BalloonDeviceAdd(
    IN WDFDRIVER  Driver,
    IN PWDFDEVICE_INIT  DeviceInit)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDFDEVICE                    device;
    PDEVICE_CONTEXT              devCtx = NULL;
    WDF_INTERRUPT_CONFIG         interruptConfig;
    WDF_FILEOBJECT_CONFIG        fileConfig;
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

    pnpPowerCallbacks.EvtDevicePrepareHardware      = BalloonEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware      = BalloonEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry              = BalloonEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit               = BalloonEvtDeviceD0Exit;
    pnpPowerCallbacks.EvtDeviceD0ExitPreInterruptsDisabled = BalloonEvtDeviceD0ExitPreInterruptsDisabled;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_FILEOBJECT_CONFIG_INIT(
                            &fileConfig,
                            WDF_NO_EVENT_CALLBACK,
                            BalloonEvtFileClose,
                            WDF_NO_EVENT_CALLBACK
                            );

    WdfDeviceInitSetFileObjectConfig(DeviceInit,
                            &fileConfig,
                            WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = BalloonEvtDeviceContextCleanup;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfDeviceCreate failed with status 0x%08x\n", status);
        return status;
    }

    devCtx = GetDeviceContext(device);

    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
                            BalloonInterruptIsr,
                            BalloonInterruptDpc);

    interruptConfig.EvtInterruptEnable  = BalloonInterruptEnable;
    interruptConfig.EvtInterruptDisable = BalloonInterruptDisable;

    status = WdfInterruptCreate(device,
                            &interruptConfig,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            &devCtx->WdfInterrupt);
    if (!NT_SUCCESS (status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfInterruptCreate failed: 0x%08x\n", status);
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_BALLOON, NULL);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfDeviceCreateDeviceInterface failed with status 0x%08x\n", status);
        return status;
    }
    devCtx->bShutDown = FALSE;
    devCtx->num_pages = 0;
    devCtx->PageListHead.Next = NULL;
    ExInitializeNPagedLookasideList(
                      &devCtx->LookAsideList,
                      NULL,
                      NULL,
                      0,
                      sizeof(PAGE_LIST_ENTRY),
                      BALLOON_MGMT_POOL_TAG,
                      0
                      );
    devCtx->bListInitialized = TRUE;
    devCtx->pfns_table = (PPFN_NUMBER)
              ExAllocatePoolWithTag(
                      NonPagedPool,
                      PAGE_SIZE,
                      BALLOON_MGMT_POOL_TAG
                      );

    if(devCtx->pfns_table == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"ExAllocatePoolWithTag failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        return status;
    }

    devCtx->MemStats = (PBALLOON_STAT)
              ExAllocatePoolWithTag(
                      NonPagedPool,
                      sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR,
                      BALLOON_MGMT_POOL_TAG
                      );

    if(devCtx->MemStats == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"ExAllocatePoolWithTag failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        return status;
    }

    RtlFillMemory (devCtx->MemStats, sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR, -1);

    KeInitializeEvent(&devCtx->HostAckEvent,
                      SynchronizationEvent,
                      FALSE
                      );

    status = BalloonQueueInitialize(device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "BalloonQueueInitialize failed with status 0x%08x\n", status);
        return status;
    }

    KeInitializeEvent(&devCtx->WakeUpThread,
                      SynchronizationEvent,
                      FALSE
                      );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID
BalloonEvtDeviceContextCleanup(
    IN WDFOBJECT  Device
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext((WDFDEVICE)Device);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if(devCtx->bListInitialized)
    {
        ExDeleteNPagedLookasideList(&devCtx->LookAsideList);
        devCtx->bListInitialized = FALSE;
    }
    if(devCtx->pfns_table)
    {
        ExFreePoolWithTag(
                   devCtx->pfns_table,
                   BALLOON_MGMT_POOL_TAG
                   );
        devCtx->pfns_table = NULL;
    }
    if(devCtx->MemStats)
    {
        ExFreePoolWithTag(
                   devCtx->MemStats,
                   BALLOON_MGMT_POOL_TAG
                   );
        devCtx->MemStats = NULL;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
BalloonEvtDevicePrepareHardware(
    IN WDFDEVICE    Device,
    IN WDFCMRESLIST ResourceList,
    IN WDFCMRESLIST ResourceListTranslated
    )
{
    NTSTATUS            status         = STATUS_SUCCESS;
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourceList);

    PAGED_CODE();

    devCtx = GetDeviceContext(Device);

    status = VirtIOWdfInitialize(
        &devCtx->VDevice,
        Device,
        ResourceListTranslated,
        NULL,
        BALLOON_MGMT_POOL_TAG,
        3 /* nMaxQueues */);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "VirtIOWdfInitialize failed with %x\n", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
BalloonEvtDeviceReleaseHardware (
    IN WDFDEVICE      Device,
    IN WDFCMRESLIST   ResourcesTranslated
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    devCtx = GetDeviceContext(Device);

    VirtIOWdfShutdown(&devCtx->VDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
BalloonCreateWorkerThread(
    IN WDFDEVICE  Device
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(Device);
    NTSTATUS            status = STATUS_SUCCESS;
    HANDLE              hThread = 0;
    OBJECT_ATTRIBUTES   oa;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);
    devCtx->bShutDown = FALSE;

    if(NULL == devCtx->Thread)
    {
        InitializeObjectAttributes(&oa, NULL, 
            OBJ_KERNEL_HANDLE, NULL, NULL);

        status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &oa, NULL, NULL,
                                          BalloonRoutine, Device);

        if(!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
              "failed to create worker thread status 0x%08x\n", status);
            return status;
        }

        ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL,
                                  KernelMode, (PVOID*)&devCtx->Thread, NULL);
        ZwClose(hThread);
    }

    KeSetEvent(&devCtx->WakeUpThread, 0, FALSE);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
BalloonCloseWorkerThread(
    IN WDFDEVICE  Device
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(Device);
    NTSTATUS            status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    PAGED_CODE();

    if(NULL != devCtx->Thread)
    {
        devCtx->bShutDown = TRUE;
        KeSetEvent(&devCtx->WakeUpThread, 0, FALSE);
        status = KeWaitForSingleObject(devCtx->Thread, Executive, KernelMode, FALSE, NULL);
        if(!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
              "KeWaitForSingleObject didn't succeed status 0x%08x\n", status);
        }
        ObDereferenceObject(devCtx->Thread);
        devCtx->Thread = NULL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}


NTSTATUS
BalloonEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PreviousState
    )
{
    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    status = BalloonInit(Device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "BalloonInit failed with status 0x%08x\n", status);
        BalloonTerm(Device);
        return status;
    }

    status = BalloonCreateWorkerThread(Device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "BalloonCreateWorkerThread failed with status 0x%08x\n", status);
    } 

    devCtx->evLowMem = IoCreateNotificationEvent(
        (PUNICODE_STRING)&evLowMemString, &devCtx->hLowMem);

    return status;
}

NTSTATUS
BalloonEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(TargetState);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--> %s\n", __FUNCTION__);

    PAGED_CODE();

    if (devCtx->evLowMem)
    {
        ZwClose(devCtx->hLowMem);
        devCtx->evLowMem = NULL;
    }

    BalloonTerm(Device);

    return STATUS_SUCCESS;
}

NTSTATUS
BalloonEvtDeviceD0ExitPreInterruptsDisabled(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--> %s\n", __FUNCTION__);

    PAGED_CODE();

    BalloonCloseWorkerThread(Device);
    if (TargetState == WdfPowerDeviceD3Final)
    {
       while(devCtx->num_pages)
       {
          BalloonLeak(Device, devCtx->num_pages);
       }

       BalloonSetSize(Device, devCtx->num_pages);
    }
    return STATUS_SUCCESS;
}

BOOLEAN
BalloonInterruptIsr(
    IN WDFINTERRUPT WdfInterrupt,
    IN ULONG        MessageID
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;
    WDFDEVICE           Device;

    UNREFERENCED_PARAMETER( MessageID );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    Device = WdfInterruptGetDevice(WdfInterrupt);
    devCtx = GetDeviceContext(Device);

    if (VirtIOWdfGetISRStatus(&devCtx->VDevice) > 0)
    {
        WdfInterruptQueueDpcForIsr( WdfInterrupt );
        return TRUE;
    }
    return FALSE;
}

VOID
BalloonInterruptDpc(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
    )
{
    unsigned int          len;
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);

    BOOLEAN               bHostAck = FALSE;
    UNREFERENCED_PARAMETER( WdfInterrupt );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "--> %s\n", __FUNCTION__);

    if (virtqueue_get_buf(devCtx->InfVirtQueue, &len))
    {
        bHostAck = TRUE;
    }
    if (virtqueue_get_buf(devCtx->DefVirtQueue, &len))
    {
        bHostAck = TRUE;
    }

    if(bHostAck)
    {
        KeSetEvent (&devCtx->HostAckEvent, IO_NO_INCREMENT, FALSE);
    }

    if (devCtx->StatVirtQueue &&
        virtqueue_get_buf(devCtx->StatVirtQueue, &len))
    {
        WDFREQUEST request = devCtx->PendingWriteRequest;

        devCtx->HandleWriteRequest = TRUE;

        if ((request != NULL) &&
            (WdfRequestUnmarkCancelable(request) != STATUS_CANCELLED))
        {
            NTSTATUS status;
            PVOID buffer;
            size_t length = 0;

            devCtx->PendingWriteRequest = NULL;

            status = WdfRequestRetrieveInputBuffer(request, 0, &buffer, &length);
            if (!NT_SUCCESS(status))
            {
                length = 0;
            }
            WdfRequestCompleteWithInformation(request, status, length);
        }
    }

    if(devCtx->Thread)
    {
       KeSetEvent(&devCtx->WakeUpThread, 0, FALSE);
    }
}

NTSTATUS
BalloonInterruptEnable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    devCtx = GetDeviceContext(WdfDevice);
    EnableInterrupt(WdfInterrupt, devCtx);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
BalloonInterruptDisable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;
    UNREFERENCED_PARAMETER( WdfInterrupt );

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    devCtx = GetDeviceContext(WdfDevice);
    DisableInterrupt(devCtx);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID
BalloonEvtFileClose (
    IN WDFFILEOBJECT    FileObject
    )
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(
        WdfFileObjectGetDevice(FileObject));

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-> %s\n", __FUNCTION__);

    RtlFillMemory(devCtx->MemStats,
        sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR, -1);

    if (devCtx->StatVirtQueue)
    {
        BalloonMemStats(WdfFileObjectGetDevice(FileObject));
    }
}

VOID
BalloonSetSize(
    IN WDFOBJECT WdfDevice,
    IN size_t    num
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);
    u32 actual = (u32)num;
    VirtIOWdfDeviceSet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, actual), &actual, sizeof(actual));
}

LONGLONG
BalloonGetSize(
    IN WDFOBJECT WdfDevice
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);

    u32 v;
    VirtIOWdfDeviceGet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, num_pages), &v, sizeof(v));
    return (LONGLONG)v - devCtx->num_pages;
}

VOID
BalloonRoutine(
    IN PVOID pContext
    )
{
    WDFOBJECT Device  =  (WDFOBJECT)pContext;
    PDEVICE_CONTEXT                 devCtx = GetDeviceContext(Device);

    NTSTATUS            status = STATUS_SUCCESS;
    LONGLONG            diff;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Balloon thread started....\n");

    for (;;)
    {
        status = KeWaitForSingleObject(&devCtx->WakeUpThread, Executive,
                                       KernelMode, FALSE, NULL);
        if(STATUS_WAIT_0 == status)
        {
            if(devCtx->bShutDown)
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Exiting Thread!\n");
                break;
            }
            else
            {
                diff = BalloonGetSize(Device);
                if (diff > 0)
                {
                    BalloonFill(Device, (size_t)(diff));
                }
                else if (diff < 0)
                {
                    BalloonLeak(Device, (size_t)(-diff));
                }
                BalloonSetSize(Device, devCtx->num_pages);
            }
        }
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Thread about to exit...\n");

    PsTerminateSystemThread(STATUS_SUCCESS);
}
