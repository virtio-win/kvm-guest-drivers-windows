/*
 * Placeholder for the device related functions
 *
 * Copyright (c) 2010-2017 Red Hat, Inc.
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

#include "precomp.h"
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "Device.tmh"
#endif

EVT_WDF_DEVICE_PREPARE_HARDWARE     VIOSerialEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     VIOSerialEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY             VIOSerialEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT              VIOSerialEvtDeviceD0Exit;
EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED VIOSerialEvtDeviceD0EntryPostInterruptsEnabled;

static NTSTATUS VIOSerialInitInterruptHandling(IN WDFDEVICE hDevice);
static NTSTATUS VIOSerialInitAllQueues(IN WDFOBJECT hDevice);
static VOID VIOSerialShutDownAllQueues(IN WDFOBJECT WdfDevice);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSerialEvtDeviceAdd)
#pragma alloc_text (PAGE, VIOSerialEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, VIOSerialEvtDeviceReleaseHardware)
#pragma alloc_text (PAGE, VIOSerialEvtDeviceD0Exit)
#pragma alloc_text (PAGE, VIOSerialEvtDeviceD0EntryPostInterruptsEnabled)

#endif

static UINT gDeviceCount = 0;


static
NTSTATUS
VIOSerialInitInterruptHandling(
    IN WDFDEVICE hDevice)
{
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_INTERRUPT_CONFIG         interruptConfig;
    PPORTS_DEVICE                pContext = GetPortsDevice(hDevice);
    NTSTATUS                     status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
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
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Failed to create control queue interrupt: %x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
        VIOSerialInterruptIsr, VIOSerialQueuesInterruptDpc);

    status = WdfInterruptCreate(hDevice, &interruptConfig, &attributes,
        &pContext->QueuesInterrupt);

    if (!NT_SUCCESS (status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Failed to create general queue interrupt: %x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
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
    PPORTS_DEVICE                pContext = NULL;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDevicePrepareHardware = VIOSerialEvtDevicePrepareHardware;
    PnpPowerCallbacks.EvtDeviceReleaseHardware = VIOSerialEvtDeviceReleaseHardware;
    PnpPowerCallbacks.EvtDeviceD0Entry         = VIOSerialEvtDeviceD0Entry;
    PnpPowerCallbacks.EvtDeviceD0Exit          = VIOSerialEvtDeviceD0Exit;
    PnpPowerCallbacks.EvtDeviceD0EntryPostInterruptsEnabled = VIOSerialEvtDeviceD0EntryPostInterruptsEnabled;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

    WDF_CHILD_LIST_CONFIG_INIT(
                                 &ChildListConfig,
                                 sizeof(VIOSERIAL_PORT),
                                 VIOSerialDeviceListCreatePdo
                                 );

    ChildListConfig.EvtChildListIdentificationDescriptionDuplicate =
                                 VIOSerialEvtChildListIdentificationDescriptionDuplicate;

    ChildListConfig.EvtChildListIdentificationDescriptionCompare =
                                 VIOSerialEvtChildListIdentificationDescriptionCompare;

    ChildListConfig.EvtChildListIdentificationDescriptionCleanup =
                                 VIOSerialEvtChildListIdentificationDescriptionCleanup;

    WdfFdoInitSetDefaultChildListConfig(
                                 DeviceInit,
                                 &ChildListConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES
                                 );

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, PORTS_DEVICE);
    Attributes.SynchronizationScope = WdfSynchronizationScopeDevice;
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
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
                                 &GUID_VIOSERIAL_CONTROLLER,
                                 NULL
                                 );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceCreateDeviceInterface failed - 0x%x\n", status);
        return status;
    }

    pContext = GetPortsDevice(hDevice);
    pContext->DeviceId = gDeviceCount++;
    pContext->ControlDmaBlock = NULL;

    busInfo.BusTypeGuid = GUID_DEVCLASS_PORT_DEVICE;
    busInfo.LegacyBusType = PNPBus;
    busInfo.BusNumber = pContext->DeviceId;

    WdfDeviceSetBusInformationForChildren(hDevice, &busInfo);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOSerialEvtDevicePrepareHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesRaw,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PPORTS_DEVICE pContext = GetPortsDevice(Device);
    NTSTATUS status = STATUS_SUCCESS;
    UINT nr_ports;
    u64 u64HostFeatures;
    u64 u64GuestFeatures = 0;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    status = VirtIOWdfInitialize(
        &pContext->VDevice,
        Device,
        ResourcesTranslated,
        NULL,
        VIOSERIAL_DRIVER_MEMORY_TAG);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfInitialize failed with %x\n", status);
        return status;
    }

    pContext->consoleConfig.max_nr_ports = 1;
    pContext->DmaGroupTag = 0xD0000000;

    u64HostFeatures = VirtIOWdfGetDeviceFeatures(&pContext->VDevice);

    if(pContext->isHostMultiport = virtio_is_feature_enabled(u64HostFeatures, VIRTIO_CONSOLE_F_MULTIPORT))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "We have multiport host\n");
        virtio_feature_enable(u64GuestFeatures, VIRTIO_CONSOLE_F_MULTIPORT);
        VirtIOWdfDeviceGet(&pContext->VDevice,
                           FIELD_OFFSET(CONSOLE_CONFIG, max_nr_ports),
                           &pContext->consoleConfig.max_nr_ports,
                           sizeof(pContext->consoleConfig.max_nr_ports));
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                    "VirtIOConsoleConfig->max_nr_ports %d\n", pContext->consoleConfig.max_nr_ports);
    }
    VirtIOWdfSetDriverFeatures(&pContext->VDevice, u64GuestFeatures, 0);

    if(pContext->isHostMultiport)
    {
        WDF_OBJECT_ATTRIBUTES  attributes;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = Device;
        status = WdfSpinLockCreate(
                                &attributes,
                                &pContext->CInVqLock
                                );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                "WdfSpinLockCreate failed 0x%x\n", status);
           return status;
        }

        status = WdfWaitLockCreate(
                                &attributes,
                                &pContext->COutVqLock
                                );
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                "WdfWaitLockCreate failed 0x%x\n", status);
            return status;
        }
    }
    else
    {
//FIXME
//        VIOSerialAddPort(Device, 0);
    }

    nr_ports = pContext->consoleConfig.max_nr_ports;
    pContext->in_vqs = (struct virtqueue**)ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 nr_ports * sizeof(struct virtqueue*),
                                 VIOSERIAL_DRIVER_MEMORY_TAG);

    if(pContext->in_vqs == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,"ExAllocatePoolWithTag failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memset(pContext->in_vqs, 0, nr_ports * sizeof(struct virtqueue*));
    pContext->out_vqs = (struct virtqueue**)ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 nr_ports * sizeof(struct virtqueue*),
                                 VIOSERIAL_DRIVER_MEMORY_TAG
                                 );

    if(pContext->out_vqs == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "ExAllocatePoolWithTag failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memset(pContext->out_vqs, 0, nr_ports * sizeof(struct virtqueue*));

    pContext->DeviceOK = TRUE;
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOSerialEvtDeviceReleaseHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PPORTS_DEVICE pContext = GetPortsDevice(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

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

    VirtIOWdfShutdown(&pContext->VDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

#if 0
void DumpQueues(WDFOBJECT Device)
{
    ULONG i, nr_ports;
    PPORTS_DEVICE          pContext = GetPortsDevice(Device);
    nr_ports = pContext->consoleConfig.max_nr_ports;
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "pContext->c_ivq %p\n",  pContext->c_ivq);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "pContext->c_ovq %p\n",  pContext->c_ovq);
    for (i = 0; i < nr_ports; ++i)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "pContext->in_vqs[%d] %p\n", i,  pContext->in_vqs[i]);
    }
    for (i = 0; i < nr_ports; ++i)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "pContext->out_vqs[%d] %p\n", i, pContext->out_vqs[i]);
    }
}
#endif

static void
VIOSerialGetQueueParamCallback(
    PVIRTIO_WDF_DRIVER pVDevice,
    ULONG uQueueIndex,
    PVIRTIO_WDF_QUEUE_PARAM pQueueParam)
{
    PPORTS_DEVICE pContext = CONTAINING_RECORD(pVDevice, PORTS_DEVICE, VDevice);

    if (uQueueIndex == 2 || uQueueIndex == 3) {
        // control queues
        pQueueParam->Interrupt = pContext->WdfInterrupt;
    } else {
        // port queues
        pQueueParam->Interrupt = pContext->QueuesInterrupt;
    }
}

static void
VIOSerialSetQueueCallback(
    PVIRTIO_WDF_DRIVER pVDevice,
    ULONG uQueueIndex,
    struct virtqueue *pQueue)
{
    PPORTS_DEVICE pContext = CONTAINING_RECORD(pVDevice, PORTS_DEVICE, VDevice);
    ULONG uPortIndex;

    // control queues
    if (uQueueIndex == 2) {
        pContext->c_ivq = pQueue;
    } else if (uQueueIndex == 3) {
        pContext->c_ovq = pQueue;
    } else {
        // port queues
        uPortIndex = uQueueIndex / 2;
        if (uPortIndex > 1) {
            uPortIndex--;
        }

        if (uQueueIndex & 1) {
            pContext->out_vqs[uPortIndex] = pQueue;
        } else {
            pContext->in_vqs[uPortIndex] = pQueue;
        }
    }
}

static
NTSTATUS
VIOSerialInitAllQueues(
    IN WDFOBJECT Device)
{
    NTSTATUS               status = STATUS_SUCCESS;
    PPORTS_DEVICE          pContext = GetPortsDevice(Device);
    UINT                   nr_ports;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    nr_ports = pContext->consoleConfig.max_nr_ports;
    if (pContext->isHostMultiport)
    {
        nr_ports++;
    }

    status = VirtIOWdfInitQueuesCB(
        &pContext->VDevice,
        nr_ports * 2,
        VIOSerialGetQueueParamCallback,
        VIOSerialSetQueueCallback);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
            "VirtIOWdfInitQueues failed with %x\n", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID VIOSerialShutDownAllQueues(IN WDFOBJECT WdfDevice)
{
    PPORTS_DEVICE pContext = GetPortsDevice(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    VirtIOWdfDestroyQueues(&pContext->VDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VIOSerialFillQueue(
    IN struct virtqueue *vq,
    IN WDFSPINLOCK Lock,
    IN ULONG id
)
{
    NTSTATUS     status = STATUS_SUCCESS;
    PPORT_BUFFER buf = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s\n", __FUNCTION__);

    for (;;)
    {
        buf = VIOSerialAllocateSinglePageBuffer(vq->vdev, id);
        if(buf == NULL)
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSerialAllocateBuffer failed\n");
           WdfSpinLockAcquire(Lock);
           VIOSerialDrainQueue(vq);
           WdfSpinLockRelease(Lock);
           return STATUS_INSUFFICIENT_RESOURCES;
        }

        WdfSpinLockAcquire(Lock);
        status = VIOSerialAddInBuf(vq, buf);
        if(!NT_SUCCESS(status))
        {
            /* nothing to protect in VIOSerialFreeBuffer
             * better to run it on PASSIVE to free the DMA block
             */
            WdfSpinLockRelease(Lock);
            VIOSerialFreeBuffer(buf);
            break;
        }
        WdfSpinLockRelease(Lock);
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID
VIOSerialDrainQueue(
    IN struct virtqueue *vq
    )
{
    PPORT_BUFFER buf;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s\n", __FUNCTION__);
    while (buf = (PPORT_BUFFER)virtqueue_detach_unused_buf(vq))
    {
        VIOSerialFreeBuffer(buf);
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VIOSerialEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PreviousState
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    PPORTS_DEVICE pContext = GetPortsDevice(Device);

    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if (!pContext->DeviceOK)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Setting VIRTIO_CONFIG_S_FAILED flag\n");
        VirtIOWdfSetDriverFailed(&pContext->VDevice);
    }
    else
    {
        status = VIOSerialInitAllQueues(Device);
        if (NT_SUCCESS(status) && pContext->isHostMultiport)
        {
            status = VIOSerialFillQueue(pContext->c_ivq, pContext->CInVqLock, pContext->DmaGroupTag);
        }

        if (NT_SUCCESS(status)) {
            pContext->ControlDmaBlock = VirtIOWdfDeviceAllocDmaMemorySliced(
                &pContext->VDevice.VIODevice,
                PAGE_SIZE, sizeof(VIRTIO_CONSOLE_CONTROL));
        }

        if (!NT_SUCCESS(status))
        {
            pContext->DeviceOK = FALSE;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);

    return status;
}

NTSTATUS
VIOSerialEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PPORTS_DEVICE pContext = GetPortsDevice(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"--> %s TargetState: %d\n",
        __FUNCTION__, TargetState);

    PAGED_CODE();

    VIOSerialDrainQueue(pContext->c_ivq);

    VIOSerialShutDownAllQueues(Device);

    VirtIOWdfDeviceFreeDmaMemoryByTag(&pContext->VDevice.VIODevice, pContext->DmaGroupTag);

    if (pContext->ControlDmaBlock) {
        pContext->ControlDmaBlock->destroy(pContext->ControlDmaBlock);
        pContext->ControlDmaBlock = NULL;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

NTSTATUS
VIOSerialEvtDeviceD0EntryPostInterruptsEnabled(
    IN  WDFDEVICE WdfDevice,
    IN  WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PPORTS_DEVICE    pContext = GetPortsDevice(WdfDevice);
    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    PAGED_CODE();

    if(!pContext->DeviceOK)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Sending VIRTIO_CONSOLE_DEVICE_READY 0\n");
        VIOSerialSendCtrlMsg(WdfDevice, VIRTIO_CONSOLE_BAD_ID, VIRTIO_CONSOLE_DEVICE_READY, 0);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Setting VIRTIO_CONFIG_S_DRIVER_OK flag\n");
        VirtIOWdfSetDriverOK(&pContext->VDevice);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Sending VIRTIO_CONSOLE_DEVICE_READY 1\n");
        VIOSerialSendCtrlMsg(WdfDevice, VIRTIO_CONSOLE_BAD_ID, VIRTIO_CONSOLE_DEVICE_READY, 1);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}
