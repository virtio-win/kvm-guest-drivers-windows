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

    status = StatInitializeWorkItem(device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "StatInitializeWorkItem failed with status 0x%08x\n", status);
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

    RtlFillMemory(devCtx->MemStats,
        sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR, -1);
    if (devCtx->StatVirtQueue)
    {
        BalloonMemStats(Device);
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
    BOOLEAN             foundPort      = FALSE;
    PHYSICAL_ADDRESS    PortBasePA     = {0};
    ULONG               PortLength     = 0;
    ULONG               i;
    WDF_INTERRUPT_INFO  interruptInfo;
    PDEVICE_CONTEXT     devCtx = NULL;

    PCM_PARTIAL_RESOURCE_DESCRIPTOR  desc;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourceList);

    PAGED_CODE();

    devCtx = GetDeviceContext(Device);

    for (i=0; i < WdfCmResourceListGetCount(ResourceListTranslated); i++) {

        desc = WdfCmResourceListGetDescriptor( ResourceListTranslated, i );

        if(!desc) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                        "WdfResourceCmGetDescriptor failed\n");
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        switch (desc->Type) {

            case CmResourceTypePort:
                if (!foundPort &&
                     desc->u.Port.Length >= 0x20) {

                    devCtx->PortMapped =
                         (desc->Flags & CM_RESOURCE_PORT_IO) ? FALSE : TRUE;

                    PortBasePA = desc->u.Port.Start;
                    PortLength = desc->u.Port.Length;
                    foundPort = TRUE;

                    if (devCtx->PortMapped) {
                         devCtx->PortBase =
                             (PUCHAR) MmMapIoSpace( PortBasePA, PortLength, MmNonCached );

                      if (!devCtx->PortBase) {
                         TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, " Unable to map port range %08I64X, length %d\n",
                                        PortBasePA.QuadPart, PortLength);

                         return STATUS_INSUFFICIENT_RESOURCES;
                      }
                      devCtx->PortCount = PortLength;

                    } else {
                         devCtx->PortBase  = (PUCHAR)(ULONG_PTR) PortBasePA.QuadPart;
                         devCtx->PortCount = PortLength;
                    }
                }

                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-> Port   Resource [%08I64X-%08I64X]\n",
                            desc->u.Port.Start.QuadPart,
                            desc->u.Port.Start.QuadPart +
                            desc->u.Port.Length);
                break;

            default:
                break;
        }
    }

    if (!foundPort) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, " Missing resources\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    WDF_INTERRUPT_INFO_INIT(&interruptInfo);
    WdfInterruptGetInfo(devCtx->WdfInterrupt, &interruptInfo);

    VirtIODeviceInitialize(&devCtx->VDevice, (ULONG_PTR)devCtx->PortBase, sizeof(devCtx->VDevice));
    VirtIODeviceSetMSIXUsed(&devCtx->VDevice, interruptInfo.MessageSignaled);

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

    if(devCtx->PortBase && devCtx->PortMapped)
    {
        MmUnmapIoSpace( devCtx->PortBase,  devCtx->PortCount );
    }

    devCtx->PortBase = NULL;

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

   /*
    * interrupts were already disabled (between BalloonEvtDeviceD0ExitPreInterruptsDisabled and this call)
    * we should flush StatWorkItem before calling BalloonTerm which will delete virtio queues
    */
    if (devCtx->StatWorkItem)
    {
        WdfWorkItemFlush(devCtx->StatWorkItem);
        devCtx->StatWorkItem = NULL;
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

    if(VirtIODeviceISR(&devCtx->VDevice) > 0)
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
        /*
         * According to MSDN 'Using Framework Work Items' article:
         * Create one or more work items that your driver requeues as necessary.
         * Subsequently, each time that the driver's EvtInterruptDpc callback
         * function is called it must determine if the EvtWorkItem callback
         * function has run. If the EvtWorkItem callback function has not run,
         * the EvtInterruptDpc callback function does not call WdfWorkItemEnqueue,
         * because the work item is still queued.
         * A few drivers might need to call WdfWorkItemFlush to flush their work
         * items from the work-item queue.
         *
         * For each dpc (i.e. interrupt) we'll push stats exactly that many times.
         */
        if (1==InterlockedIncrement(&devCtx->WorkCount))
        {
            WdfWorkItemEnqueue(devCtx->StatWorkItem);
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
BalloonSetSize(
    IN WDFOBJECT WdfDevice,
    IN size_t    num
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);
    u32 actual = (u32)num;
    VirtIODeviceSet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, actual), &actual, sizeof(actual));
}

LONGLONG
BalloonGetSize(
    IN WDFOBJECT WdfDevice
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);

    u32 v;
    VirtIODeviceGet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, num_pages), &v, sizeof(v));
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
