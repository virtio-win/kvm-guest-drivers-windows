/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: Device.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Device related functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioinput.h"

#if defined(EVENT_TRACING)
#include "Device.tmh"
#endif

EVT_WDF_DEVICE_PREPARE_HARDWARE     VIOInputEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     VIOInputEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY             VIOInputEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT              VIOInputEvtDeviceD0Exit;

static NTSTATUS VIOInputInitInterruptHandling(IN WDFDEVICE hDevice);
static NTSTATUS VIOInputInitAllQueues(IN WDFOBJECT hDevice);
static VOID VIOInputShutDownAllQueues(IN WDFOBJECT WdfDevice);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOInputEvtDeviceAdd)
#pragma alloc_text (PAGE, VIOInputEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, VIOInputEvtDeviceReleaseHardware)
#pragma alloc_text (PAGE, VIOInputEvtDeviceD0Exit)
#endif

static
NTSTATUS
VIOInputInitInterruptHandling(
    IN WDFDEVICE hDevice)
{
    WDF_INTERRUPT_CONFIG interruptConfig;
    NTSTATUS             status = STATUS_SUCCESS;
    PINPUT_DEVICE        pContext = GetDeviceContext(hDevice);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Failed to create control queue interrupt: %x\n", status);
        return status;
    }

    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
        VIOInputInterruptIsr, VIOInputQueuesInterruptDpc);

    interruptConfig.EvtInterruptEnable = VIOInputInterruptEnable;
    interruptConfig.EvtInterruptDisable = VIOInputInterruptDisable;

    status = WdfInterruptCreate(hDevice, &interruptConfig, WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->QueuesInterrupt);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Failed to create general queue interrupt: %x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOInputEvtDeviceAdd(
    IN WDFDRIVER Driver,
    IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES        Attributes;
    WDFDEVICE                    hDevice;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
    PINPUT_DEVICE                pContext = NULL;
    WDF_IO_QUEUE_CONFIG          queueConfig;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    // This driver acts like a lower filter under MsHidKmdf.sys
    WdfFdoInitSetFilter(DeviceInit);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDevicePrepareHardware = VIOInputEvtDevicePrepareHardware;
    PnpPowerCallbacks.EvtDeviceReleaseHardware = VIOInputEvtDeviceReleaseHardware;
    PnpPowerCallbacks.EvtDeviceD0Entry = VIOInputEvtDeviceD0Entry;
    PnpPowerCallbacks.EvtDeviceD0Exit = VIOInputEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, INPUT_DEVICE);
    Attributes.SynchronizationScope = WdfSynchronizationScopeDevice;
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&DeviceInit, &Attributes, &hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceCreate failed - 0x%x\n", status);
        return status;
    }

    status = VIOInputInitInterruptHandling(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "VIOInputInitInterruptHandling failed - 0x%x\n", status);
    }

    status = WdfDeviceCreateDeviceInterface(
        hDevice,
        &GUID_VIOINPUT_CONTROLLER,
        NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceCreateDeviceInterface failed - 0x%x\n", status);
        return status;
    }

    pContext = GetDeviceContext(hDevice);

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchParallel);

    queueConfig.EvtIoInternalDeviceControl = EvtIoDeviceControl;

    status = WdfIoQueueCreate(
        hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->IoctlQueue);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed - 0x%x\n", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual);

    status = WdfIoQueueCreate(
        hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->HidQueue);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed - 0x%x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = hDevice;
    status = WdfSpinLockCreate(
        &Attributes,
        &pContext->EventQLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfSpinLockCreate failed - 0x%x\n", status);
        return status;
    }
    status = WdfSpinLockCreate(
        &Attributes,
        &pContext->StatusQLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfSpinLockCreate failed - 0x%x\n", status);
        return status;
    }

    RtlZeroMemory(&pContext->HidDeviceAttributes, sizeof(HID_DEVICE_ATTRIBUTES));
    pContext->HidDeviceAttributes.Size = sizeof(HID_DEVICE_ATTRIBUTES);
    pContext->HidDeviceAttributes.VendorID = HIDMINI_VID;
    pContext->HidDeviceAttributes.ProductID = HIDMINI_PID;
    pContext->HidDeviceAttributes.VersionNumber = HIDMINI_VERSION;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOInputEvtDevicePrepareHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesRaw,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PINPUT_DEVICE pContext = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;
    u64 hostFeatures, guestFeatures = 0;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    status = VirtIOWdfInitialize(
        &pContext->VDevice,
        Device,
        ResourcesTranslated,
        NULL,
        VIOINPUT_DRIVER_MEMORY_TAG,
        2 /* max queues */);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfInitialize failed with %x\n", status);
        return status;
    }

    hostFeatures = VirtIOWdfGetDeviceFeatures(&pContext->VDevice);
    if (virtio_is_feature_enabled(hostFeatures, VIRTIO_F_VERSION_1))
    {
        virtio_feature_enable(guestFeatures, VIRTIO_F_VERSION_1);
    }
    if (virtio_is_feature_enabled(hostFeatures, VIRTIO_F_ANY_LAYOUT))
    {
        virtio_feature_enable(guestFeatures, VIRTIO_F_ANY_LAYOUT);
    }
    VirtIOWdfSetDriverFeatures(&pContext->VDevice, guestFeatures);

    // Figure out what kind of input device this is and build a
    // corresponding HID report descriptor.
    status = VIOInputBuildReportDescriptor(pContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOInputEvtDeviceReleaseHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PINPUT_DEVICE pContext = GetDeviceContext(Device);
    PSINGLE_LIST_ENTRY entry;
    ULONG i;

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    VirtIOWdfShutdown(&pContext->VDevice);

    for (i = 0; i < pContext->uNumOfClasses; i++)
    {
        PINPUT_CLASS_COMMON pClass = pContext->InputClasses[i];
        if (pClass->CleanupFunc)
        {
            pClass->CleanupFunc(pClass);
        }
        VIOInputFree(&pClass->pHidReport);
        VIOInputFree(&pClass);
    }

    VIOInputFree(&pContext->HidReportDescriptor);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOInputInitAllQueues(
    IN WDFOBJECT Device)
{
    NTSTATUS status = STATUS_SUCCESS;
    PINPUT_DEVICE pContext = GetDeviceContext(Device);

    struct virtqueue *vqs[2];
    VIRTIO_WDF_QUEUE_PARAM params[2];

    // event
    params[0].bEnableInterruptSuppression = false;
    params[0].Interrupt = pContext->QueuesInterrupt;

    // status
    params[1].bEnableInterruptSuppression = false;
    params[1].Interrupt = pContext->QueuesInterrupt;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    status = VirtIOWdfInitQueues(&pContext->VDevice, 2, vqs, params);
    if (NT_SUCCESS(status))
    {
        pContext->EventQ = vqs[0];
        pContext->StatusQ = vqs[1];
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VirtIOWdfInitQueues returned %x\n", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID
VIOInputShutDownAllQueues(IN WDFOBJECT WdfDevice)
{
    PINPUT_DEVICE pContext = GetDeviceContext(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    VirtIOWdfDestroyQueues(&pContext->VDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VIOInputFillQueue(
    IN struct virtqueue *vq,
    IN WDFSPINLOCK Lock)
{
    NTSTATUS status = STATUS_SUCCESS;
    PVIRTIO_INPUT_EVENT buf = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s\n", __FUNCTION__);

    for (;;)
    {
        buf = (PVIRTIO_INPUT_EVENT)ExAllocatePoolWithTag(
            NonPagedPool,
            sizeof(VIRTIO_INPUT_EVENT),
            VIOINPUT_DRIVER_MEMORY_TAG
            );
        if (buf == NULL)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIRTIO_INPUT_EVENT alloc failed\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        WdfSpinLockAcquire(Lock);
        status = VIOInputAddInBuf(vq, buf);
        WdfSpinLockRelease(Lock);
        if (!NT_SUCCESS(status))
        {
            ExFreePoolWithTag(buf, VIOINPUT_DRIVER_MEMORY_TAG);
            break;
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

static NTSTATUS
VIOInputAddBuf(
    IN struct virtqueue *vq,
    IN PVIRTIO_INPUT_EVENT buf,
    IN BOOLEAN out)
{
    NTSTATUS  status = STATUS_SUCCESS;
    struct VirtIOBufferDescriptor sg;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s  buf = %p\n", __FUNCTION__, buf);
    if (buf == NULL)
    {
        ASSERT(0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (vq == NULL)
    {
        ASSERT(0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    sg.physAddr = MmGetPhysicalAddress(buf);
    sg.length = sizeof(VIRTIO_INPUT_EVENT);

    if (0 > virtqueue_add_buf(vq, &sg, (out ? 1 : 0), (out ? 0 : 1), buf, NULL, 0))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING, "<-- %s cannot add_buf\n", __FUNCTION__);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    virtqueue_kick(vq);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOInputAddInBuf(
    IN struct virtqueue *vq,
    IN PVIRTIO_INPUT_EVENT buf)
{
    return VIOInputAddBuf(vq, buf, FALSE);
}

NTSTATUS
VIOInputAddOutBuf(
    IN struct virtqueue *vq,
    IN PVIRTIO_INPUT_EVENT buf)
{
    return VIOInputAddBuf(vq, buf, TRUE);
}

NTSTATUS
VIOInputEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PreviousState)
{
    NTSTATUS status = STATUS_SUCCESS;
    PINPUT_DEVICE pContext = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    status = VIOInputInitAllQueues(Device);
    if (NT_SUCCESS(status))
    {
        VirtIOWdfSetDriverOK(&pContext->VDevice);
        VIOInputFillQueue(pContext->EventQ, pContext->EventQLock);
    }
    else
    {
        VirtIOWdfSetDriverFailed(&pContext->VDevice);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);

    return status;
}

NTSTATUS
VIOInputEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState)
{
    PINPUT_DEVICE pContext = GetDeviceContext(Device);
    PVIRTIO_INPUT_EVENT buf;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"--> %s TargetState: %d\n",
        __FUNCTION__, TargetState);

    PAGED_CODE();

    if (pContext->EventQ)
    {
        while (buf = (PVIRTIO_INPUT_EVENT)virtqueue_detach_unused_buf(pContext->EventQ))
        {
            ExFreePoolWithTag(buf, VIOINPUT_DRIVER_MEMORY_TAG);
        }
    }
    VIOInputShutDownAllQueues(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}
