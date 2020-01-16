/*
 * Placeholder for the device related functions
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
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
#include "viosock.h"

#if defined(EVENT_TRACING)
#include "Device.tmh"
#endif

EVT_WDF_DEVICE_PREPARE_HARDWARE     VIOSockEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     VIOSockEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY             VIOSockEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT              VIOSockEvtDeviceD0Exit;
EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED VIOSockEvtDeviceD0EntryPostInterruptsEnabled;

static NTSTATUS VIOSockInitInterrupt(IN WDFDEVICE hDevice);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockEvtDeviceAdd)
#pragma alloc_text (PAGE, VIOSockEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, VIOSockEvtDeviceReleaseHardware)
#pragma alloc_text (PAGE, VIOSockEvtDeviceD0Exit)
#pragma alloc_text (PAGE, VIOSockEvtDeviceD0EntryPostInterruptsEnabled)
#endif

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VIOSockDeviceControl;

static
NTSTATUS
VIOSockInitInterrupt(
    IN WDFDEVICE hDevice)
{
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_INTERRUPT_CONFIG         interruptConfig;
    PDEVICE_CONTEXT              pContext = GetDeviceContext(hDevice);
    NTSTATUS                     status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_INTERRUPT_CONFIG_INIT(
                              &interruptConfig,
                              VIOSockInterruptIsr,
                              VIOSockInterruptDpc
                              );

    interruptConfig.EvtInterruptEnable = VIOSockInterruptEnable;
    interruptConfig.EvtInterruptDisable = VIOSockInterruptDisable;

    status = WdfInterruptCreate(
                                 hDevice,
                                 &interruptConfig,
                                 &attributes,
                                 &pContext->WdfInterrupt
                                 );

    if (!NT_SUCCESS (status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Failed to create interrupt: %x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOSockEvtDeviceAdd(
    IN WDFDRIVER Driver,
    IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES        deviceAttributes, fileAttributes, collectionAttributes;
    WDFDEVICE                    hDevice;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
    PDEVICE_CONTEXT              pContext = NULL;
    WDF_FILEOBJECT_CONFIG        fileConfig;
    WDF_IO_QUEUE_CONFIG          queueConfig;

    DECLARE_CONST_UNICODE_STRING(usDeviceName, VIOSOCK_DEVICE_NAME);
    DECLARE_CONST_UNICODE_STRING(usDosDeviceName, VIOSOCK_SYMLINK_NAME);

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    // Configure Pnp/power callbacks
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDevicePrepareHardware = VIOSockEvtDevicePrepareHardware;
    PnpPowerCallbacks.EvtDeviceReleaseHardware = VIOSockEvtDeviceReleaseHardware;
    PnpPowerCallbacks.EvtDeviceD0Entry         = VIOSockEvtDeviceD0Entry;
    PnpPowerCallbacks.EvtDeviceD0Exit          = VIOSockEvtDeviceD0Exit;
    PnpPowerCallbacks.EvtDeviceD0EntryPostInterruptsEnabled = VIOSockEvtDeviceD0EntryPostInterruptsEnabled;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

    // Set DirectIO mode
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

    // Set device name (for kernel mode clients)
    status = WdfDeviceInitAssignName(DeviceInit, &usDeviceName);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceInitAssignName failed - 0x%x\n", status);
        return status;
    }

    // Configure file object callbacks
    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        VIOSockCreate,
        VIOSockClose,
        WDF_NO_EVENT_CALLBACK // Cleanup
    );
    fileConfig.FileObjectClass = WdfFileObjectWdfCanUseFsContext;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fileAttributes, SOCKET_CONTEXT);

    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &fileAttributes);

    // Create device
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceCreate failed - 0x%x\n", status);
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(
        hDevice,
        &GUID_DEVINTERFACE_VIOSOCK,
        NULL
    );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceCreateDeviceInterface failed - 0x%x\n", status);
        return status;
    }

    status = WdfDeviceCreateSymbolicLink(
        hDevice,
        &usDosDeviceName
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceCreateSymbolicLink failed - 0x%x\n", status);
        return status;
    }

    pContext = GetDeviceContext(hDevice);

    // Create collection for socket objects
    WDF_OBJECT_ATTRIBUTES_INIT(&collectionAttributes);
    collectionAttributes.ParentObject = hDevice;

    status = WdfCollectionCreate(&collectionAttributes, &pContext->SocketList);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfCollectionCreate failed - 0x%x\n", status);
        return status;
    }

    // Create sequential queue for IoCtl requests
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchSequential
    );
    queueConfig.EvtIoDeviceControl = VIOSockDeviceControl;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    status = WdfIoQueueCreate(hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->IoCtlQueue
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfIoQueueCreate failed (IoCtrl Queue): 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(hDevice,
        pContext->IoCtlQueue,
        WdfRequestTypeDeviceControl);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfDeviceConfigureRequestDispatching failed (IoCtrl Queue): 0x%x\n", status);
        return status;
    }

    status = VIOSockInitInterrupt(hDevice);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSockInitInterrupt failed - 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOSockEvtDevicePrepareHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesRaw,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);
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
        VIOSOCK_DRIVER_MEMORY_TAG);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfInitialize failed with %x\n", status);
        return status;
    }

    u64HostFeatures = VirtIOWdfGetDeviceFeatures(&pContext->VDevice);

    if (virtio_is_feature_enabled(u64HostFeatures, VIRTIO_RING_F_INDIRECT_DESC))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Enable indirect feature.\n");

        virtio_feature_enable(u64GuestFeatures, VIRTIO_RING_F_INDIRECT_DESC);
    }

    status = VirtIOWdfSetDriverFeatures(&pContext->VDevice, u64GuestFeatures, 0);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "VirtIOWdfSetDriverFeatures failed: %x\n", status);
        VirtIOWdfSetDriverFailed(&pContext->VDevice);
    }
    else
    {
        VIRTIO_WDF_QUEUE_PARAM params[3];
        PVIOSOCK_VQ vqs[3];

        // rx
        params[0].Interrupt = pContext->WdfInterrupt;
        // tx
        params[1].Interrupt = pContext->WdfInterrupt;
        // event
        params[2].Interrupt = pContext->WdfInterrupt;

        status = VirtIOWdfInitQueues(&pContext->VDevice, 3, vqs, params);
        if (NT_SUCCESS(status))
        {
            pContext->RxQueue = vqs[0];
            pContext->TxQueue = vqs[1];
            pContext->EvtQueue = vqs[2];
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Setting VIRTIO_CONFIG_S_FAILED flag\n");
            VirtIOWdfSetDriverFailed(&pContext->VDevice);
       }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOSockEvtDeviceReleaseHardware(
    IN WDFDEVICE Device,
    IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    VirtIOWdfShutdown(&pContext->VDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
VIOSockEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PreviousState
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    VirtIOWdfDeviceGet(&pContext->VDevice,
        0,
        &pContext->Config,
        sizeof(pContext->Config));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
        "guest_cid %lld\n", pContext->Config.guest_cid);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);

    return status;
}

NTSTATUS
VIOSockEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"--> %s TargetState: %d\n",
        __FUNCTION__, TargetState);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

NTSTATUS
VIOSockEvtDeviceD0EntryPostInterruptsEnabled(
    IN  WDFDEVICE WdfDevice,
    IN  WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PDEVICE_CONTEXT    pContext = GetDeviceContext(WdfDevice);
    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Setting VIRTIO_CONFIG_S_DRIVER_OK flag\n");
    VirtIOWdfSetDriverOK(&pContext->VDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID
VIOSockDeviceControl(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     OutputBufferLength,
    IN size_t     InputBufferLength,
    IN ULONG      IoControlCode
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfIoQueueGetDevice(Queue));

    size_t                  length = 0;
    NTSTATUS                status = STATUS_SUCCESS;
    PVIRTIO_VSOCK_HDR       pVsockHdr = NULL;
    PVIRTIO_VSOCK_CONFIG    pConfig = NULL;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    switch (IoControlCode)
    {

    case IOCTL_GET_CONFIG:
    {
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VIRTIO_VSOCK_CONFIG), (PVOID*)&pConfig, &length);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                "WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);
            break;
        }

        // minimum length guaranteed by WdfRequestRetrieveOutputBuffer above
        _Analysis_assume_(length >= sizeof(VIRTIO_VSOCK_CONFIG));

        *pConfig = pContext->Config;
        status = STATUS_SUCCESS;

        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, length);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);
}
