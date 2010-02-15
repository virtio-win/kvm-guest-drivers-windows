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

NTSTATUS BalloonDeviceAdd(
                      IN WDFDRIVER  Driver,
                      IN PWDFDEVICE_INIT  DeviceInit
                      )
{
    NTSTATUS                    status = STATUS_SUCCESS;
    WDFDEVICE                   device;
    PDEVICE_CONTEXT             devCtx = NULL;
    WDF_OBJECT_ATTRIBUTES       attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_INTERRUPT_CONFIG        interruptConfig;
    WDF_OBJECT_ATTRIBUTES       interruptAttributes;

    UNREFERENCED_PARAMETER(Driver);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> BalloonDeviceAdd\n");

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = BalloonPrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = BalloonReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

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

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&interruptAttributes, INTERRUPT_DATA);
    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
                            BalloonInterruptIsr,
                            BalloonInterruptDpc);
 
    interruptConfig.EvtInterruptEnable = BalloonInterruptEnable;
    interruptConfig.EvtInterruptDisable = BalloonInterruptDisable;

    status = WdfInterruptCreate(device,
                              &interruptConfig,
                              &interruptAttributes,
                              &devCtx->WdfInterrupt);
    if (!NT_SUCCESS (status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfInterruptCreate failed: %!STATUS!\n", status);
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEV_IF_BALLOON, NULL);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfDeviceCreateDeviceInterface failed with status %!STATUS!\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- BalloonDeviceAdd\n");
    return status;
}

NTSTATUS
BalloonPrepareHardware(
    IN WDFDEVICE    Device,
    IN WDFCMRESLIST ResourceList,
    IN WDFCMRESLIST ResourceListTranslated
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG               i;
    BOOLEAN             foundPort      = FALSE;
    PHYSICAL_ADDRESS    PortBasePA     = {0};
    ULONG               PortLength     = 0;
  
    PDEVICE_CONTEXT     devCtx = NULL;

    PCM_PARTIAL_RESOURCE_DESCRIPTOR  desc;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> EvtDevicePrepareHardware\n");

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
                //
                // Ignore all other descriptors
                //
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
           "BalloonInit failed with status %!STATUS!\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- EvtDevicePrepareHardware\n");
    return status;
}

NTSTATUS
BalloonReleaseHardware (
    WDFDEVICE      Device,
    WDFCMRESLIST   ResourcesTranslated
    )
{                  
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> EvtDeviceReleaseHardware\n");

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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- EvtDeviceReleaseHardware\n");

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
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-> Interrupt\n");
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> FillLeakWorkItem\n");

    pItemContext = GetWorkItemContext(WorkItem);

    devCtx = GetDeviceContext(pItemContext->Device);

    if (pItemContext->Diff > 0)
        BalloonFill(pItemContext->Device, pItemContext->Diff);
    else
        BalloonLeak(pItemContext->Device, -pItemContext->Diff);

    SetBalloonSize(pItemContext->Device, drvCtx->num_pages); 

    WdfInterruptSynchronize(
        devCtx->WdfInterrupt,
        RestartInterrupt,
        devCtx);

    WdfObjectDelete(WorkItem);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- FillLeakWorkItem\n");
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

    UNREFERENCED_PARAMETER( WdfInterrupt );

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> BalloonInterruptDpc\n");
  
    devCtx->InfVirtQueue->vq_ops->get_buf(devCtx->InfVirtQueue, &len);
    devCtx->DefVirtQueue->vq_ops->get_buf(devCtx->DefVirtQueue, &len);

    num_pages = GetBalloonSize(WdfDevice);

    if((num_pages - drvCtx->num_pages) == 0) return;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, WORKITEM_CONTEXT);
    attributes.ParentObject = WdfDevice;

    WDF_WORKITEM_CONFIG_INIT(&workitemConfig, FillLeakWorkItem);

    status = WdfWorkItemCreate( &workitemConfig,
                                &attributes,
                                &hWorkItem);

    if (!NT_SUCCESS(status)) {
        return;
    }

    context = GetWorkItemContext(hWorkItem);

    context->Device = WdfDevice;
    context->Diff = num_pages - drvCtx->num_pages;
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "<-- num_pages = 0x%x, pages = 0x%x\n", num_pages, drvCtx->num_pages);

    WdfWorkItemEnqueue(hWorkItem);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- BalloonInterruptDpc\n");
    return;
}

NTSTATUS
BalloonInterruptEnable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> BalloonInterruptEnable\n");

    devCtx = GetDeviceContext(WdfDevice);
    EnableInterrupt(WdfInterrupt, devCtx);
    
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- BalloonInterruptEnable\n");
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> BalloonInterruptDisable\n");

    devCtx = GetDeviceContext(WdfDevice);
    DisableInterrupt(devCtx);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- BalloonInterruptDisable\n");
    return STATUS_SUCCESS;
}
