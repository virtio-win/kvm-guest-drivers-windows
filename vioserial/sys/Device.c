/**********************************************************************
 * Copyright (c) 2010  Red Hat, Inc.
 *
 * File: VIOSerialDevice.c
 *
 * Placeholder for the device related functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "Device.tmh"
#endif

EVT_WDF_DEVICE_PREPARE_HARDWARE     VIOSerialEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     VIOSerialEvtDeviceReleaseHardware;

static NTSTATUS VIOSerialInitInterruptHandling(IN WDFDEVICE hDevice);
static NTSTATUS VIOSerialInit(IN WDFOBJECT hDevice);
static NTSTATUS VIOSerialDeinit(IN WDFOBJECT WdfDevice);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSerialEvtDeviceAdd)
#pragma alloc_text (PAGE, VIOSerialEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, VIOSerialEvtDeviceReleaseHardware)

#endif

static NTSTATUS VIOSerialInitInterruptHandling(WDFDEVICE hDevice)
{
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_INTERRUPT_CONFIG         interruptConfig;
    PPORTS_DEVICE	         pContext = GetPortsDevice(hDevice);
    NTSTATUS                     status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, PORTS_DEVICE);
    WDF_INTERRUPT_CONFIG_INIT(
                                 &interruptConfig,
                                 VIOSerialInterruptIsr,
                                 VIOSerialInterruptDpc
                                 );

    interruptConfig.EvtInterruptEnable = VIOSerialInterruptEnable;
    interruptConfig.EvtInterruptDisable = VIOSerialInterruptDisable;

    status = WdfInterruptCreate(
                                 hDevice,
                                 &interruptConfig,
                                 &attributes,
                                 &pContext->WdfInterrupt
                                 );

    if (!NT_SUCCESS (status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "WdfInterruptCreate failed: %x\n", status);
        return status;
    }

    return status;
}

NTSTATUS 
VIOSerialEvtDeviceAdd(
    IN WDFDRIVER Driver,
    IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES        Attributes;
    WDFDEVICE                    hDevice;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
    WDF_CHILD_LIST_CONFIG        ChildListConfig;
    PNP_BUS_INFORMATION          busInfo;
	
    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--> %s\n", __FUNCTION__);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDevicePrepareHardware = VIOSerialEvtDevicePrepareHardware;
    PnpPowerCallbacks.EvtDeviceReleaseHardware = VIOSerialEvtDeviceReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

    WDF_CHILD_LIST_CONFIG_INIT(
                                 &ChildListConfig,
                                 sizeof(VIOSERIAL_PORT),
                                 VIOSerialDeviceListCreatePdo
                                 );

    WdfFdoInitSetDefaultChildListConfig(
                                 DeviceInit,
                                 &ChildListConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES
                                 );

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, PORTS_DEVICE);
    Attributes.SynchronizationScope = WdfSynchronizationScopeDevice;
    status = WdfDeviceCreate(&DeviceInit, &Attributes, &hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceCreate failed - 0x%x\n", status);
        return status;
    }

    status = VIOSerialInitInterruptHandling(hDevice);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "VIOSerialInitInterruptHandling failed - 0x%x\n", status);
    }


    status = WdfDeviceCreateDeviceInterface(
                                 hDevice,
                                 &GUID_DEVINTERFACE_PORTSENUM_VIOSERIAL,
                                 NULL
                                 );
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceCreateDeviceInterface failed - 0x%x\n", status);
        return status;
    }

    busInfo.BusTypeGuid = GUID_DEVCLASS_PORT_DEVICE;
    busInfo.LegacyBusType = PNPBus;
    busInfo.BusNumber = 0;

    WdfDeviceSetBusInformationForChildren(hDevice, &busInfo);

    return status;
}

NTSTATUS 
VIOSerialEvtDevicePrepareHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesRaw,
    IN WDFCMRESLIST ResourcesTranslated)
{
    int nListSize = 0;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDescriptor;
    int i = 0;
    PPORTS_DEVICE pContext = GetPortsDevice(Device);
    bool bPortFound = FALSE;
    NTSTATUS status = STATUS_SUCCESS;
    WDF_DMA_ENABLER_CONFIG dmaConfig;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<--> %s\n", __FUNCTION__);

    nListSize = WdfCmResourceListGetCount(ResourcesTranslated);

    for (i = 0; i < nListSize; i++)
    {
        if(pResDescriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i))
        {
            switch(pResDescriptor->Type)
            {
                case CmResourceTypePort :
                    pContext->bPortMapped = (pResDescriptor->Flags & CM_RESOURCE_PORT_IO) ? FALSE : TRUE;
                    pContext->PortBasePA = pResDescriptor->u.Port.Start;
                    pContext->uPortLength = pResDescriptor->u.Port.Length;
                    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "IO Port Info  [%08I64X-%08I64X]\n",
                                 pResDescriptor->u.Port.Start.QuadPart,
                                 pResDescriptor->u.Port.Start.QuadPart +
                                 pResDescriptor->u.Port.Length);

                    if (pContext->bPortMapped )
                    {
                        pContext->pPortBase = MmMapIoSpace(pContext->PortBasePA,
                                                           pContext->uPortLength,
                                                           MmNonCached);

                        if (!pContext->pPortBase) {
                            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "%s>>> Failed to map IO port!\n", __FUNCTION__);
                            VIOSerialSendCtrlMsg(Device, VIRTIO_CONSOLE_BAD_ID, VIRTIO_CONSOLE_DEVICE_READY, 0);
                            return STATUS_INSUFFICIENT_RESOURCES;
                        }
                    }
                    else
                    {
                        pContext->pPortBase = (PVOID)(ULONG_PTR)pContext->PortBasePA.QuadPart;
                    }

                    bPortFound = TRUE;

                    break;
                case CmResourceTypeInterrupt:
                    break;
            }
        }
    }

    if(!bPortFound)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "%s>>> %s", __FUNCTION__, "IO port wasn't found!\n");
        VIOSerialSendCtrlMsg(Device, VIRTIO_CONSOLE_BAD_ID, VIRTIO_CONSOLE_DEVICE_READY, 0);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    status = VIOSerialInit(Device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIOSerialInit failed - 0x%x\n", status);
        VIOSerialSendCtrlMsg(Device, VIRTIO_CONSOLE_BAD_ID, VIRTIO_CONSOLE_DEVICE_READY, 0);
        return status;
    }

    pContext->MaximumTransferLength = PORT_MAXIMUM_TRANSFER_LENGTH;
    WDF_DMA_ENABLER_CONFIG_INIT( &dmaConfig,
                                 WdfDmaProfileScatterGather64Duplex,
                                 pContext->MaximumTransferLength );

    status = WdfDmaEnablerCreate(Device,
                                 &dmaConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &pContext->DmaEnabler
                                 );

    if (!NT_SUCCESS (status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                        "WdfDmaEnablerCreate failed: 0x%x\n", status);
        VIOSerialSendCtrlMsg(Device, VIRTIO_CONSOLE_BAD_ID, VIRTIO_CONSOLE_DEVICE_READY, 0);
        return status;
    }


    VIOSerialSendCtrlMsg(Device, VIRTIO_CONSOLE_BAD_ID, VIRTIO_CONSOLE_DEVICE_READY, 1);

    return STATUS_SUCCESS;
}

NTSTATUS 
VIOSerialEvtDeviceReleaseHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PPORTS_DEVICE pContext = GetPortsDevice(Device);
    UNREFERENCED_PARAMETER(ResourcesTranslated);
	
    PAGED_CODE();
	
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<--> %s\n", __FUNCTION__);
	
    VIOSerialSendCtrlMsg(Device, VIRTIO_CONSOLE_BAD_ID, VIRTIO_CONSOLE_DEVICE_READY, 0);
    VIOSerialDeinit(Device);

    if (pContext->pPortBase && pContext->bPortMapped) 
    {
        MmUnmapIoSpace(pContext->pPortBase, pContext->uPortLength);
    }

    pContext->pPortBase = (ULONG_PTR)NULL;

    return STATUS_SUCCESS;
}

static 
NTSTATUS 
VIOSerialInit(IN WDFOBJECT Device)
{
    NTSTATUS               status = STATUS_SUCCESS;
    PPORTS_DEVICE          pContext = GetPortsDevice(Device);
    UINT                   nr_ports, nr_queues, i, j;
    struct virtqueue       *in_vq, *out_vq;
    WDF_OBJECT_ATTRIBUTES  attributes;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<--> %s\n", __FUNCTION__);
    VirtIODeviceSetIOAddress(&pContext->IODevice, (ULONG_PTR)pContext->pPortBase);

    VirtIODeviceReset(&pContext->IODevice);

    VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER);

    pContext->consoleConfig.max_nr_ports = 1;
    if(pContext->isHostMultiport = VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_CONSOLE_F_MULTIPORT))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "We have multiport host\n");
        VirtIODeviceEnableGuestFeature(&pContext->IODevice, VIRTIO_CONSOLE_F_MULTIPORT);
        VirtIODeviceGet(&pContext->IODevice,
                                 FIELD_OFFSET(CONSOLE_CONFIG, max_nr_ports),
                                 &pContext->consoleConfig.max_nr_ports,
                                 sizeof(pContext->consoleConfig.max_nr_ports));
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, 
                                "VirtIOConsoleConfig->max_nr_ports %d\n", pContext->consoleConfig.max_nr_ports);
    }
    
    nr_ports = pContext->consoleConfig.max_nr_ports;
    nr_queues = pContext->isHostMultiport ? (nr_ports + 1) * 2 : 2;

    pContext->in_vqs = ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 nr_ports * sizeof(struct virtqueue*),
                                 VIOSERIAL_DRIVER_MEMORY_TAG);

    if(pContext->in_vqs == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,"ExAllocatePoolWithTag failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    pContext->out_vqs = ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 nr_ports * sizeof(struct virtqueue*),
                                 VIOSERIAL_DRIVER_MEMORY_TAG
                                 );

    if(pContext->out_vqs == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "ExAllocatePoolWithTag failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    for(i = 0, j = 0; i < nr_ports; i++)
    {
        in_vq  = VirtIODeviceFindVirtualQueue(&pContext->IODevice, i * 2, NULL);
        out_vq = VirtIODeviceFindVirtualQueue(&pContext->IODevice, (i * 2 ) + 1, NULL);

        if(i == 1) // Control Port
        {
           pContext->c_ivq = in_vq;
           pContext->c_ovq = out_vq;
        }
        else
        {
           pContext->in_vqs[j] = in_vq;
           pContext->out_vqs[j] = out_vq;
           ++j;
        }
    }

    if(pContext->isHostMultiport)
    {
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = Device;
        status = WdfSpinLockCreate(
                                &attributes,
                                &pContext->CVqLock
                                );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                "WdfSpinLockCreate failed 0x%x\n", status);
           return status;
        }
        ASSERT(pContext->c_ivq);
        status = VIOSerialFillQueue(pContext->c_ivq, pContext->CVqLock);
    }
    else
    {
        VIOSerialAddPort(Device, 0);
    }
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Setting VIRTIO_CONFIG_S_FAILED flag\n");
        VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_FAILED);
        return status; 
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Setting VIRTIO_CONFIG_S_DRIVER_OK flag\n");
        VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
    }

    return status;
}

NTSTATUS 
VIOSerialDeinit(
    IN WDFOBJECT WdfDevice)
{
    NTSTATUS		status = STATUS_SUCCESS;
    PPORTS_DEVICE	pContext = GetPortsDevice(WdfDevice);
    UINT                nr_ports, i;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s\n", __FUNCTION__);

    VirtIODeviceRemoveStatus(&pContext->IODevice , VIRTIO_CONFIG_S_DRIVER_OK);

    if(pContext->isHostMultiport)
    {
        if(pContext->c_ivq)
        {
            pContext->c_ivq->vq_ops->shutdown(pContext->c_ivq);
            VirtIODeviceDeleteVirtualQueue(pContext->c_ivq);
            pContext->c_ivq = NULL;
        }
        if(pContext->c_ovq)
        {
            pContext->c_ovq->vq_ops->shutdown(pContext->c_ovq);
            VirtIODeviceDeleteVirtualQueue(pContext->c_ovq);
            pContext->c_ovq = NULL;
        }
    } 

    nr_ports = pContext->consoleConfig.max_nr_ports - 1;
    for(i = 0; i < nr_ports; i++ )
    {
        if(pContext->in_vqs && pContext->in_vqs[i])
        {
            pContext->in_vqs[i]->vq_ops->shutdown(pContext->in_vqs[i]);
            VirtIODeviceDeleteVirtualQueue(pContext->in_vqs[i]);
            pContext->in_vqs[i] = NULL;
        }

        if(pContext->out_vqs && pContext->out_vqs[i])
        {
            pContext->out_vqs[i]->vq_ops->shutdown(pContext->out_vqs[i]);
            VirtIODeviceDeleteVirtualQueue(pContext->out_vqs[i]);
            pContext->out_vqs[i] = NULL;
        }
    }

    if(pContext->in_vqs)
    {
        ExFreePoolWithTag(pContext->in_vqs, VIOSERIAL_DRIVER_MEMORY_TAG);
        pContext->in_vqs = NULL;
    }

    if(pContext->out_vqs)
    {
        ExFreePoolWithTag(pContext->out_vqs, VIOSERIAL_DRIVER_MEMORY_TAG);
        pContext->out_vqs = NULL;
    }

    VirtIODeviceReset(&pContext->IODevice);

    return status;
}

NTSTATUS 
VIOSerialFillQueue(
    IN struct virtqueue *vq,
    IN WDFSPINLOCK Lock
)
{
    NTSTATUS     status = STATUS_SUCCESS;
    PPORT_BUFFER buf = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<--> %s\n", __FUNCTION__);

    for (;;)
    {
        buf = VIOSerialAllocateBuffer(PAGE_SIZE);
        if(buf == NULL)
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSerialAllocateBuffer failed\n");
           return STATUS_INSUFFICIENT_RESOURCES;
        }

        WdfSpinLockAcquire(Lock);
        status = VIOSerialAddInBuf(vq, buf);
        if(!NT_SUCCESS(status))
        {
           WdfSpinLockRelease(Lock);
           VIOSerialFreeBuffer(buf);
           break;
        }
        WdfSpinLockRelease(Lock);
    }
    return STATUS_SUCCESS;
}

