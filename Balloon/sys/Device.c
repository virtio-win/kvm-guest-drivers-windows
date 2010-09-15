/**********************************************************************
 * Copyright (c) 2009  Red Hat, Inc.
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

EVT_WDF_DEVICE_PREPARE_HARDWARE     BalloonEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     BalloonEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY             BalloonEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT              BalloonEvtDeviceD0Exit;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, BalloonEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, BalloonEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, BalloonEvtDeviceD0Exit)
#pragma alloc_text(PAGE, BalloonDeviceAdd)
#pragma alloc_text(PAGE, BalloonEvtDeviceFileCreate)
#pragma alloc_text(PAGE, BalloonEvtFileClose)
#endif

#if (WINVER >= 0x0501)
#define LOMEMEVENTNAME L"\\KernelObjects\\LowMemoryCondition"
DECLARE_CONST_UNICODE_STRING(evLowMemString, LOMEMEVENTNAME);
#endif // (WINVER >= 0x0501)


NTSTATUS 
BalloonDeviceAdd(
    IN WDFDRIVER  Driver,
    IN PWDFDEVICE_INIT  DeviceInit)
{
    NTSTATUS                    status = STATUS_SUCCESS;
    WDFDEVICE                   device;
    PDEVICE_CONTEXT             devCtx = NULL;
    WDF_OBJECT_ATTRIBUTES       attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_INTERRUPT_CONFIG        interruptConfig;
    WDF_FILEOBJECT_CONFIG       fileConfig;

    UNREFERENCED_PARAMETER(Driver);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = BalloonEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = BalloonEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry         = BalloonEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit          = BalloonEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);


    WDF_FILEOBJECT_CONFIG_INIT(
                            &fileConfig,
                            BalloonEvtDeviceFileCreate,
                            BalloonEvtFileClose,
                            WDF_NO_EVENT_CALLBACK
                            );

    WdfDeviceInitSetFileObjectConfig(DeviceInit,
                            &fileConfig,
                            WDF_NO_OBJECT_ATTRIBUTES);

    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);  
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfDeviceCreate failed with status 0x%08x\n", status);
        return status;
    }
  
    devCtx = GetDeviceContext(device);
    devCtx->Device = device;
    devCtx->DriverObject = WdfDriverWdmGetDriverObject(Driver);

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

    status = BalloonQueueInitialize(device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "BalloonQueueInitialize failed with status 0x%08x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
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
#if (WINVER >= 0x0501)
    devCtx->evLowMem = IoCreateNotificationEvent(
                               (PUNICODE_STRING )&evLowMemString,
                               &devCtx->hLowMem);
#endif // (WINVER >= 0x0501)

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
BalloonEvtDeviceReleaseHardware (
    WDFDEVICE      Device,
    WDFCMRESLIST   ResourcesTranslated
    )
{                  
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

#if (WINVER >= 0x0501)
    ZwClose(&devCtx->hLowMem);
#endif // (WINVER >= 0x0501)

    devCtx = GetDeviceContext(Device);

    if (devCtx->PortBase) {
        if (devCtx->PortMapped) {
            MmUnmapIoSpace( devCtx->PortBase,  devCtx->PortCount );
        }
        devCtx->PortBase = NULL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
BalloonEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;
    NTSTATUS            status = STATUS_SUCCESS;
    PDRIVER_CONTEXT     drvCtx = GetDriverContext(WdfGetDriver());

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);
    devCtx = GetDeviceContext(Device);
    status = BalloonInit(Device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "BalloonInit failed with status 0x%08x\n", status);
        return status;
    }

    if (PreviousState != WdfPowerDeviceD3Final)
    {
        u32 num_pages = drvCtx->num_pages;
        VirtIODeviceSet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, num_pages), &num_pages, sizeof(num_pages));
        SetBalloonSize(Device, num_pages);
    }

    if (devCtx->bServiceConnected &&
       VirtIODeviceGetHostFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ))
    {
        VirtIODeviceEnableGuestFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
BalloonEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(Device);
    PDRIVER_CONTEXT       drvCtx = GetDriverContext(WdfGetDriver());

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--> %s\n", __FUNCTION__);

    PAGED_CODE();

    if (TargetState == WdfPowerDeviceD3Final)
    {
        while(drvCtx->num_pages)
        {
           BalloonLeak(Device, drvCtx->num_pages);
        }
        SetBalloonSize(Device, drvCtx->num_pages);
    }
    BalloonTerm(Device);
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
FillLeakWorkItem(
    IN WDFWORKITEM  WorkItem
    )
{
    PWORKITEM_CONTEXT     pItemContext;
    PDEVICE_CONTEXT       devCtx = NULL;
    PDRIVER_CONTEXT       drvCtx = GetDriverContext(WdfGetDriver());

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    pItemContext = GetWorkItemContext(WorkItem);

    devCtx = GetDeviceContext(pItemContext->Device);

    if (pItemContext->Diff > 0) {
        BalloonFill(pItemContext->Device, (size_t)(pItemContext->Diff));
    } else if (pItemContext->Diff < 0) {  
        BalloonLeak(pItemContext->Device, (size_t)(-pItemContext->Diff));
    }
    SetBalloonSize(pItemContext->Device, drvCtx->num_pages); 
    if (pItemContext->bStatUpdate) {
        BalloonMemStats(pItemContext->Device);
    }

    WdfObjectDelete(WorkItem);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return;
}

VOID
BalloonInterruptDpc(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
    )
{
    unsigned int          len;
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);
    PDRIVER_CONTEXT       drvCtx = GetDriverContext(WdfGetDriver());

    PWORKITEM_CONTEXT     context;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG   workitemConfig;
    WDFWORKITEM           hWorkItem;
    NTSTATUS              status = STATUS_SUCCESS;
    BOOLEAN               bStatUpdate = FALSE;

    UNREFERENCED_PARAMETER( WdfInterrupt );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "--> %s\n", __FUNCTION__);
  
    devCtx->InfVirtQueue->vq_ops->get_buf(devCtx->InfVirtQueue, &len);
    devCtx->DefVirtQueue->vq_ops->get_buf(devCtx->DefVirtQueue, &len);
    if(devCtx->StatVirtQueue &&
       devCtx->StatVirtQueue->vq_ops->get_buf(devCtx->StatVirtQueue, &len)) {
       bStatUpdate = TRUE;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, WORKITEM_CONTEXT);
    attributes.ParentObject = WdfDevice;

    WDF_WORKITEM_CONFIG_INIT(&workitemConfig, FillLeakWorkItem);

    status = WdfWorkItemCreate( &workitemConfig,
                                &attributes,
                                &hWorkItem);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "WdfWorkItemCreate failed with status = 0x%08x\n", status);
        return;
    }

    context = GetWorkItemContext(hWorkItem);

    context->Device = WdfDevice;
    context->Diff = GetBalloonSize(WdfDevice);

    context->bStatUpdate = bStatUpdate;

    WdfWorkItemEnqueue(hWorkItem);

    return;
}

NTSTATUS
BalloonInterruptEnable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
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
    IN WDFOBJECT    WdfDevice
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
BalloonEvtDeviceFileCreate (
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;

    UNREFERENCED_PARAMETER(FileObject);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s\n", __FUNCTION__);

    PAGED_CODE ();

    devCtx = GetDeviceContext(WdfDevice);

    if(VirtIODeviceGetHostFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ))
    {
        VirtIODeviceEnableGuestFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ);
        devCtx->bServiceConnected = TRUE;
    }
    WdfRequestComplete(Request, STATUS_SUCCESS);

    return;
}


VOID
BalloonEvtFileClose (
    IN WDFFILEOBJECT    FileObject
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;

    PAGED_CODE ();

    devCtx = GetDeviceContext(WdfFileObjectGetDevice(FileObject));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s\n", __FUNCTION__);

    if(VirtIODeviceGetHostFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ))
    {
	ULONG ulValue = 0;

	ulValue = ReadVirtIODeviceRegister(devCtx->VDevice.addr + VIRTIO_PCI_GUEST_FEATURES);
	ulValue	&= ~(1 << VIRTIO_BALLOON_F_STATS_VQ);
	WriteVirtIODeviceRegister(devCtx->VDevice.addr + VIRTIO_PCI_GUEST_FEATURES, ulValue);
        devCtx->bServiceConnected = FALSE;
    }
    return;
}

VOID
SetBalloonSize(
    IN WDFOBJECT WdfDevice,
    IN size_t    num
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);
    u32 actual = (u32)num;
    VirtIODeviceSet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, actual), &actual, sizeof(actual));
}

LONGLONG
GetBalloonSize(
    IN WDFOBJECT WdfDevice
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);
    PDRIVER_CONTEXT       drvCtx = GetDriverContext(WdfGetDriver());

    u32 v;
    VirtIODeviceGet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, num_pages), &v, sizeof(v));
    return (LONGLONG)v - drvCtx->num_pages;
}

