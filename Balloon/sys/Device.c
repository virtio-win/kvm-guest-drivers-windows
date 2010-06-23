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

#pragma alloc_text(PAGE, BalloonPrepareHardware)
#pragma alloc_text(PAGE, BalloonReleaseHardware)
#pragma alloc_text(PAGE, BalloonDeviceAdd)
#pragma alloc_text(PAGE, BalloonEvtDeviceFileCreate)
#pragma alloc_text(PAGE, BalloonEvtFileClose)

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
    pnpPowerCallbacks.EvtDevicePrepareHardware = BalloonPrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = BalloonReleaseHardware;
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
BalloonPrepareHardware(
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

    status = BalloonInit(Device);
    if (status != STATUS_SUCCESS)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, 
           "BalloonInit failed with status 0x%08x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
BalloonReleaseHardware (
    WDFDEVICE      Device,
    WDFCMRESLIST   ResourcesTranslated
    )
{                  
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    BalloonTerm(Device);
    devCtx = GetDeviceContext(Device);

    if (devCtx->PortBase) {
        if (devCtx->PortMapped) {
            MmUnmapIoSpace( devCtx->PortBase,  devCtx->PortCount );
        }
        devCtx->PortBase = NULL;
    }

    TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "<-- %s\n", __FUNCTION__);
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
    int hsr;
    int hcr;

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
        BalloonFill(pItemContext->Device, pItemContext->Diff);
    } else if (pItemContext->Diff < 0) {  
        BalloonLeak(pItemContext->Device, -pItemContext->Diff);
    }
    SetBalloonSize(pItemContext->Device, drvCtx->num_pages); 
    if (pItemContext->bStatUpdate) {
        BalloonMemStats(pItemContext->Device);
    }
    WdfInterruptSynchronize(
        devCtx->WdfInterrupt,
        RestartInterrupt,
        devCtx);

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
    size_t                num_pages;
    BOOLEAN               bStatUpdate = FALSE;

    UNREFERENCED_PARAMETER( WdfInterrupt );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "--> %s\n", __FUNCTION__);
  
    devCtx->InfVirtQueue->vq_ops->get_buf(devCtx->InfVirtQueue, &len);
    devCtx->DefVirtQueue->vq_ops->get_buf(devCtx->DefVirtQueue, &len);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "--> BalloonInterruptDpc 1\n");
    if(devCtx->StatVirtQueue &&
       devCtx->StatVirtQueue->vq_ops->get_buf(devCtx->StatVirtQueue, &len)) {
       bStatUpdate = TRUE;
    }
    num_pages = GetBalloonSize(WdfDevice);

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
    context->Diff = num_pages - drvCtx->num_pages;
    context->bStatUpdate = bStatUpdate;

    WdfWorkItemEnqueue(hWorkItem);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "<-- %s\n", __FUNCTION__);
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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s Device = %p\n", __FUNCTION__, WdfDevice);

    PAGED_CODE ();

    devCtx = GetDeviceContext(WdfDevice);

    if(VirtIODeviceGetHostFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ))
    {
        VirtIODeviceEnableGuestFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ);
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

    }
    return;
}
