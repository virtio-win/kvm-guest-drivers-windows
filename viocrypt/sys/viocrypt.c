/*
* Copyright (C) 2018 Red Hat, Inc.
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

#include "driver.h"
#include "viocrypt-public.h"

#ifndef NO_WPP
#include "viocrypt.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, VioCryptDeviceAdd)
#pragma alloc_text(PAGE, VioCryptDriverContextCleanup)
#pragma alloc_text(PAGE, VioCryptDevicePrepareHardware)
#endif

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    // Initialize the WPP tracing.
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    Trace(TRACE_LEVEL_VERBOSE, "[%s] -->", __FUNCTION__);

    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = VioCryptDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, VioCryptDeviceAdd);

    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes,
        &config, WDF_NO_HANDLE);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfDriverCreate failed: status %X", __FUNCTION__, status);
        WPP_CLEANUP(DriverObject);
    }

    Trace(TRACE_LEVEL_VERBOSE, "[%s] -->", __FUNCTION__);

    return status;
}

NTSTATUS VioCryptDeviceAdd(IN WDFDRIVER Driver, IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFQUEUE queue;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_INTERRUPT_CONFIG interruptConfig;
    PDEVICE_CONTEXT context;

    UNREFERENCED_PARAMETER(Driver);

    Trace(TRACE_LEVEL_VERBOSE, "[%s] -->", __FUNCTION__);

    PAGED_CODE();

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

    pnpPowerCallbacks.EvtDevicePrepareHardware = VioCryptDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = VioCryptDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = VioCryptDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = VioCryptDeviceD0Exit;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = VioCryptDeviceContextCleanup;

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfDeviceCreate failed: status %X", __FUNCTION__, status);
        return status;
    }

    context = GetDeviceContext(device);

    RtlZeroMemory(context, sizeof(*context));
    InitializeListHead(&context->PendingBuffers);

    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, VioCryptInterruptIsr, VioCryptInterruptDpc);

    interruptConfig.EvtInterruptEnable = VioCryptInterruptEnable;
    interruptConfig.EvtInterruptDisable = VioCryptInterruptDisable;

    status = WdfInterruptCreate(device, &interruptConfig,
        WDF_NO_OBJECT_ATTRIBUTES, &context->WdfInterrupt);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfInterruptCreate failed: status %X", __FUNCTION__, status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfSpinLockCreate(&attributes,
        &context->VirtQueueLock);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfSpinLockCreate failed: status %X", __FUNCTION__, status);
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(device,
        &GUID_DEVINTERFACE_VIOCRYPT, NULL);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfDeviceCreateDeviceInterface failed: status %X", __FUNCTION__, status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = VioCryptIoControl;
    queueConfig.EvtIoStop = VioCryptIoStop;
    queueConfig.AllowZeroLengthRequests = FALSE;

    status = WdfIoQueueCreate(device, &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES, &queue);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfIoQueueCreate failed: status %X", __FUNCTION__, status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(device, queue, WdfRequestTypeDeviceControl);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "[%s] WdfDeviceConfigureRequestDispatching failed: status %X", __FUNCTION__, status);
        return status;
    }

    Trace(TRACE_LEVEL_VERBOSE, "[%s] <--", __FUNCTION__);

    return status;
}

VOID VioCryptDriverContextCleanup(IN WDFOBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    Trace(TRACE_LEVEL_VERBOSE, "[%s]", __FUNCTION__);

    PAGED_CODE();

    // Stop the WPP tracing.
    WPP_CLEANUP(WdfDriverWdmGetDriverObject(DriverObject));
}

NTSTATUS VioCryptInterruptEnable(IN WDFINTERRUPT Interrupt, IN WDFDEVICE wdfDevice)
{
    PDEVICE_CONTEXT context = GetDeviceContext(wdfDevice);
    UNREFERENCED_PARAMETER(Interrupt);

    virtqueue_enable_cb(context->ControlQueue);
    virtqueue_kick(context->ControlQueue);

    Trace(TRACE_LEVEL_VERBOSE, "[%s]", __FUNCTION__);

    return STATUS_SUCCESS;
}

NTSTATUS VioCryptInterruptDisable(IN WDFINTERRUPT Interrupt, IN WDFDEVICE wdfDevice)
{
    PDEVICE_CONTEXT context = GetDeviceContext(wdfDevice);
    UNREFERENCED_PARAMETER(Interrupt);

    Trace(TRACE_LEVEL_VERBOSE, "[%s]", __FUNCTION__);

    virtqueue_disable_cb(context->ControlQueue);

    return STATUS_SUCCESS;
}

BOOLEAN VioCryptInterruptIsr(IN WDFINTERRUPT Interrupt, IN ULONG MessageId)
{
    PDEVICE_CONTEXT context = GetDeviceContext(
        WdfInterruptGetDevice(Interrupt));
    WDF_INTERRUPT_INFO info;
    BOOLEAN processed;

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(context->WdfInterrupt, &info);

    processed = ((info.MessageSignaled && (MessageId == 0)) ||
        VirtIOWdfGetISRStatus(&context->VDevice));
    
    if (processed)
    {
        WdfInterruptQueueDpcForIsr(Interrupt);
    }

    Trace(TRACE_LEVEL_VERBOSE, "[%s] %sprocessed", __FUNCTION__, processed ? "" : "not ");

    return processed;
}

VOID VioCryptInterruptDpc(IN WDFINTERRUPT Interrupt,
    IN WDFOBJECT AssociatedObject)
{
    PDEVICE_CONTEXT context = GetDeviceContext(
        WdfInterruptGetDevice(Interrupt));
    UNREFERENCED_PARAMETER(AssociatedObject);
    UNREFERENCED_PARAMETER(context);
}

NTSTATUS VioCryptDevicePrepareHardware(IN WDFDEVICE Device,
    IN WDFCMRESLIST Resources,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Resources);

    Trace(TRACE_LEVEL_VERBOSE, "[%s]", __FUNCTION__);

    PAGED_CODE();

    status = VirtIOWdfInitialize(
        &context->VDevice,
        Device,
        ResourcesTranslated,
        NULL,
        VIO_CRYPT_MEMORY_TAG);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, "VirtIOWdfInitialize failed with %x\n", status);
    }

    return status;
}

NTSTATUS VioCryptDeviceReleaseHardware(IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    VirtIOWdfShutdown(&context->VDevice);

    Trace(TRACE_LEVEL_VERBOSE, "[%s]", __FUNCTION__);

    return STATUS_SUCCESS;
}

NTSTATUS VioCryptDeviceD0Entry(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE PreviousState)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    VIRTIO_WDF_QUEUE_PARAM param;
    struct virtqueue *queues[1];

    Trace(TRACE_LEVEL_VERBOSE, "[%s] from D%d", __FUNCTION__, PreviousState - WdfPowerDeviceD0);

    PAGED_CODE();

    if (NT_SUCCESS(status))
    {
        param.Interrupt = context->WdfInterrupt;
        status = VirtIOWdfInitQueues(&context->VDevice, 1, queues, &param);
    }

    if (NT_SUCCESS(status))
    {
        context->ControlQueue = queues[0];
        VirtIOWdfSetDriverOK(&context->VDevice);
    }
    else
    {
        VirtIOWdfSetDriverFailed(&context->VDevice);
        Trace(TRACE_LEVEL_ERROR, "[%s] VirtIOWdfInitQueues failed with %x\n", __FUNCTION__, status);
    }

    return status;
}

NTSTATUS VioCryptDeviceD0Exit(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE TargetState)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);

    Trace(TRACE_LEVEL_VERBOSE, "[%s] to D%d", __FUNCTION__, TargetState - WdfPowerDeviceD0);

    PAGED_CODE();

    VirtIOWdfDestroyQueues(&context->VDevice);

    return STATUS_SUCCESS;
}

VOID VioCryptDeviceContextCleanup(IN WDFOBJECT DeviceObject)
{
    PDEVICE_CONTEXT context = GetDeviceContext(DeviceObject);
    BOOLEAN bHasBuffers = !IsListEmpty(&context->PendingBuffers);
    Trace(TRACE_LEVEL_VERBOSE, "[%s] %s", __FUNCTION__, bHasBuffers ? "Has unfreed buffers" : "");
    // TODO: free buffers allocated by the driver
}

VOID VioCryptIoControl
(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength,
    IN size_t InputBufferLength,
    IN ULONG IoControlCode)
{
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    UNREFERENCED_PARAMETER(Queue);
    Trace(TRACE_LEVEL_INFORMATION, "[%s] code %X, in %lld, out %lld",
        __FUNCTION__, IoControlCode, InputBufferLength, OutputBufferLength);
    if (status != STATUS_PENDING)
    {
        WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
    }
}

VOID VioCryptIoStop(IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN ULONG ActionFlags)
{
    BOOLEAN bCancellable = (ActionFlags & WdfRequestStopRequestCancelable) != 0;
    UNREFERENCED_PARAMETER(Queue);

    Trace(TRACE_LEVEL_INFORMATION, "[%s] Req %p, action %X, the request is %scancellable",
        __FUNCTION__, Request, ActionFlags, bCancellable ? "" : "not ");

    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        // the driver owns the request and it will not be able to process it
        Trace(TRACE_LEVEL_INFORMATION, "[%s] Req %p can't be suspended", __FUNCTION__, Request);
        if (!bCancellable || WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            WdfRequestComplete(Request, STATUS_CANCELLED);
        }
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {
        Trace(TRACE_LEVEL_INFORMATION, "[%s] Req %p purged", __FUNCTION__, Request);
        if (!bCancellable || WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            WdfRequestComplete(Request, STATUS_CANCELLED);
        }
    }
}
