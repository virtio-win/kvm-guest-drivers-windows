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
EVT_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND          VIOSockEvtDeviceSelfManagedIoSuspend;
EVT_WDF_DEVICE_SELF_MANAGED_IO_RESTART          VIOSockEvtDeviceSelfManagedIoRestart;

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL  VIOSockEvtIoDeviceControl;
EVT_WDF_REQUEST_CANCEL              VIOSockSelectCancel;
EVT_WDF_WORKITEM                    VIOSockSelectWorkitem;
EVT_WDF_TIMER                       VIOSockSelectTimerFunc;

VOID
VIOSockQueuesCleanup(
    IN WDFDEVICE hDevice
);

NTSTATUS
VIOSockQueuesInit(
    IN WDFDEVICE hDevice
);

NTSTATUS
VIOSockDeviceGetConfig(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

NTSTATUS
VIOSockDeviceGetAf(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

typedef struct _VIOSOCK_SELECT_HANDLE
{
    ULONGLONG       hSocket;
    WDFFILEOBJECT   Socket;
}VIOSOCK_SELECT_HANDLE, *PVIOSOCK_SELECT_HANDLE;

typedef struct _VIOSOCK_SELECT_PKT
{
    LIST_ENTRY              ListEntry;
    LONGLONG                Timeout;
    PVIRTIO_VSOCK_SELECT    pSelect;
    ULONG                   FdCount[FDSET_MAX];
    NTSTATUS                Status;
    VIOSOCK_SELECT_HANDLE   Fds[FD_SETSIZE];
}VIOSOCK_SELECT_PKT, *PVIOSOCK_SELECT_PKT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSOCK_SELECT_PKT, GetSelectContext);

NTSTATUS
VIOSockSelectInit(
    IN PDEVICE_CONTEXT pContext
);

VOID
VIOSockSelectCleanupFds(
    IN PVIOSOCK_SELECT_PKT      pPkt,
    IN VIRTIO_VSOCK_FDSET_TYPE  iFdSet,
    IN ULONG                    uStartIndex
);

BOOLEAN
VIOSockSelectCheckPkt(
    IN PVIOSOCK_SELECT_PKT  pPkt
);

BOOLEAN
VIOSockSelectCopyFds(
    IN PDEVICE_CONTEXT          pContext,
    IN BOOLEAN                  bIs32BitProcess,
    IN PVIRTIO_VSOCK_SELECT     pSelect,
    IN PVIOSOCK_SELECT_PKT      pPkt,
    IN VIRTIO_VSOCK_FDSET_TYPE  iFdSet,
    IN ULONG                    uStartIndex
);

NTSTATUS
VIOSockSelect(
    IN WDFREQUEST Request,
    IN OUT size_t *pLength
);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockEvtDeviceAdd)
#pragma alloc_text (PAGE, VIOSockQueuesCleanup)

#pragma alloc_text (PAGE, VIOSockEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, VIOSockEvtDeviceReleaseHardware)
#pragma alloc_text (PAGE, VIOSockEvtDeviceD0Exit)
#pragma alloc_text (PAGE, VIOSockEvtDeviceSelfManagedIoSuspend)

#pragma alloc_text (PAGE, VIOSockDeviceGetConfig)
#pragma alloc_text (PAGE, VIOSockDeviceGetAf)
#pragma alloc_text (PAGE, VIOSockEvtIoDeviceControl)
#pragma alloc_text (PAGE, VIOSockSelectInit)
#pragma alloc_text (PAGE, VIOSockSelectCleanupFds)
#pragma alloc_text (PAGE, VIOSockSelectCheckPkt)
#pragma alloc_text (PAGE, VIOSockSelectWorkitem)
#pragma alloc_text (PAGE, VIOSockSelectCopyFds)
#pragma alloc_text (PAGE, VIOSockSelect)
#endif

static
VOID
VIOSockQueuesCleanup(
    IN WDFDEVICE hDevice
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    NTSTATUS status = STATUS_SUCCESS;

    ULONG uBufferSize;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    if (pContext->RxVq)
        VIOSockRxVqCleanup(pContext);

    if (pContext->TxVq)
        VIOSockTxVqCleanup(pContext);

    if (pContext->EvtVq)
        VIOSockEvtVqCleanup(pContext);

    VirtIOWdfDestroyQueues(&pContext->VDevice);
}

static
NTSTATUS
VIOSockQueuesInit(
    IN WDFDEVICE hDevice
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    NTSTATUS status = STATUS_SUCCESS;
    VIRTIO_WDF_QUEUE_PARAM params[VIOSOCK_VQ_MAX];
    PVIOSOCK_VQ vqs[VIOSOCK_VQ_MAX];

    ULONG uBufferSize;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    // rx
    params[VIOSOCK_VQ_RX].Interrupt = pContext->WdfInterrupt;
    // tx
    params[VIOSOCK_VQ_TX].Interrupt = pContext->WdfInterrupt;
    // event
    params[VIOSOCK_VQ_EVT].Interrupt = pContext->WdfInterrupt;

    status = VirtIOWdfInitQueues(&pContext->VDevice, VIOSOCK_VQ_MAX, vqs, params);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfInitQueues failed: 0x%x\n", status);
        return status;
    }

    pContext->RxVq = vqs[VIOSOCK_VQ_RX];
    status = VIOSockRxVqInit(pContext);
    if (NT_SUCCESS(status))
    {
        pContext->TxVq = vqs[VIOSOCK_VQ_TX];
        status = VIOSockTxVqInit(pContext);
        if (NT_SUCCESS(status))
        {
            pContext->EvtVq = vqs[VIOSOCK_VQ_EVT];
            status = VIOSockEvtVqInit(pContext);
            if (!NT_SUCCESS(status))
                pContext->EvtVq = NULL;
        }
        else
            pContext->TxVq = NULL;
    }
    else
        pContext->RxVq = NULL;

    if (!NT_SUCCESS(status))
        VIOSockQueuesCleanup(hDevice);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);

    return status;
}

//////////////////////////////////////////////////////////////////////////
NTSTATUS
VIOSockEvtDeviceAdd(
    IN WDFDRIVER Driver,
    IN PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES        Attributes;
    WDFDEVICE                    hDevice;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
    PDEVICE_CONTEXT              pContext = NULL;
    WDF_FILEOBJECT_CONFIG        fileConfig;
    WDF_IO_QUEUE_CONFIG          queueConfig;
    WDF_WORKITEM_CONFIG          wrkConfig;

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
    PnpPowerCallbacks.EvtDeviceSelfManagedIoSuspend = VIOSockEvtDeviceSelfManagedIoSuspend;
    PnpPowerCallbacks.EvtDeviceSelfManagedIoRestart = VIOSockEvtDeviceSelfManagedIoRestart;
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

    // Set device access (for user mode clients)
    status = WdfDeviceInitAssignSDDLString(DeviceInit, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceInitAssignSDDLString failed - 0x%x\n", status);
        return status;
    }

    // Configure file object callbacks
    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        VIOSockCreateStub,
        VIOSockClose,
        WDF_NO_EVENT_CALLBACK // Cleanup
    );
    fileConfig.FileObjectClass = WdfFileObjectWdfCanUseFsContext;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SOCKET_CONTEXT);

    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &Attributes);

    // Create device
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &Attributes, &hDevice);
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

    status = VIOSockBoundListInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSockBoundListInit failed - 0x%x\n", status);
        return status;
    }

    status = VIOSockConnectedListInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSockConnectedListInit failed - 0x%x\n", status);
        return status;
    }

    pContext = GetDeviceContext(hDevice);

    pContext->ThisDevice = hDevice;

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = hDevice;

    status = WdfLookasideListCreate(&Attributes,
        sizeof(VIOSOCK_ACCEPT_ENTRY), NonPagedPoolNx,
        &Attributes, VIOSOCK_DRIVER_MEMORY_TAG,
        &pContext->AcceptMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfLookasideListCreate failed: 0x%x\n", status);
        return status;
    }

    // Create sequential queue for IoCtl requests
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchParallel
    );
    queueConfig.EvtIoDeviceControl = VIOSockEvtIoDeviceControl;
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

    // Create parallel queue for Write requests
    status = VIOSockWriteQueueInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "VIOSockWriteQueueInit failed (Write Queue): 0x%x\n", status);
        return status;
    }

    // Create parallel queue for Read requests
    status = VIOSockReadQueueInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "VIOSockReadQueueInit failed (Read Queue): 0x%x\n", status);
        return status;
    }

    status = VIOSockSelectInit(pContext);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "VIOSockSelectInit failed: 0x%x\n", status);
        return status;
    }

    status = VIOSockInterruptInit(hDevice);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSockInterruptInit failed - 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

static
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    status = VirtIOWdfInitialize(
        &pContext->VDevice,
        Device,
        ResourcesTranslated,
        NULL,
        VIOSOCK_DRIVER_MEMORY_TAG);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfInitialize failed with 0x%x\n", status);
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
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfSetDriverFeatures failed: 0x%x\n", status);
        VirtIOWdfSetDriverFailed(&pContext->VDevice);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

static
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

    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOSockEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PreviousState
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    status = VIOSockQueuesInit(Device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "VIOSockQueuesInit failed: 0x%x\n", status);
        return status;
    }

    VirtIOWdfDeviceGet(&pContext->VDevice,
        0,
        &pContext->Config,
        sizeof(pContext->Config));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
        "guest_cid %lld\n", pContext->Config.guest_cid);

    return status;
}

static
NTSTATUS
VIOSockEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"--> %s TargetState: %d\n",
        __FUNCTION__, TargetState);

    VIOSockQueuesCleanup(Device);

    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOSockEvtDeviceD0EntryPostInterruptsEnabled(
    IN  WDFDEVICE WdfDevice,
    IN  WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PDEVICE_CONTEXT    pContext = GetDeviceContext(WdfDevice);
    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Setting VIRTIO_CONFIG_S_DRIVER_OK flag\n");
    VirtIOWdfSetDriverOK(&pContext->VDevice);

    ASSERT(pContext->RxVq && pContext->EvtVq);

    return STATUS_SUCCESS;
}


NTSTATUS
VIOSockEvtDeviceSelfManagedIoSuspend(
    IN WDFDEVICE Device
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    PAGED_CODE();

    VIOSockWriteIoSuspend(pContext);

    return STATUS_SUCCESS;
}

NTSTATUS
VIOSockEvtDeviceSelfManagedIoRestart(
    IN WDFDEVICE Device
)
{

    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    VIOSockWriteIoRestart(pContext);

    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOSockDeviceGetConfig(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PVIRTIO_VSOCK_CONFIG    pConfig = NULL;
    NTSTATUS                status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VIRTIO_VSOCK_CONFIG), (PVOID*)&pConfig, pLength);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
            "WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveOutputBuffer above
    _Analysis_assume_(*pLength >= sizeof(VIRTIO_VSOCK_CONFIG));

    *pConfig = GetDeviceContextFromRequest(Request)->Config;
    *pLength = sizeof(*pConfig);

    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOSockDeviceGetAf(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PULONG                  pulAF = NULL;
    NTSTATUS                status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pulAF), (PVOID*)&pulAF, pLength);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
            "WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveOutputBuffer above
    _Analysis_assume_(*pLength >= sizeof(*pulAF));

    *pulAF = AF_VSOCK;
    *pLength = sizeof(*pulAF);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s, AF: %d\n", __FUNCTION__, *pulAF);

    return STATUS_SUCCESS;
}

static
VOID
VIOSockEvtIoDeviceControl(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     OutputBufferLength,
    IN size_t     InputBufferLength,
    IN ULONG      IoControlCode
)
{
    size_t          Length = 0;
    NTSTATUS        status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    switch (IoControlCode)
    {
    case IOCTL_GET_CONFIG:
        status = VIOSockDeviceGetConfig(Request, &Length);
        break;

    case IOCTL_SELECT:
        status = VIOSockSelect(Request, &Length);
        break;

    case IOCTL_GET_AF:
        status = VIOSockDeviceGetAf(Request, &Length);
        break;

    default:
        if (IsControlRequest(Request))
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid socket type\n");
            status = STATUS_NOT_SOCKET;
        }
        else
        {
            status = VIOSockDeviceControl(
                Request,
                IoControlCode,
                &Length);
        }
    }

    if (status != STATUS_PENDING)
        WdfRequestCompleteWithInformation(Request, status, Length);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
}

//////////////////////////////////////////////////////////////////////////
static
NTSTATUS
VIOSockSelectInit(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS                status;
    WDF_OBJECT_ATTRIBUTES   Attributes;
    WDF_WORKITEM_CONFIG     wrkConfig;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    InitializeListHead(&pContext->SelectList);
    pContext->SelectInProgress = 0;

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = pContext->ThisDevice;

    status = WdfWaitLockCreate(&Attributes, &pContext->SelectLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT,
            "WdfWaitLockCreate failed (Select): 0x%x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = pContext->ThisDevice;

    WDF_WORKITEM_CONFIG_INIT(&wrkConfig, VIOSockSelectWorkitem);
    status = WdfWorkItemCreate(&wrkConfig, &Attributes, &pContext->SelectWorkitem);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT,
            "WdfWorkItemCreate failed (Select): 0x%x\n", status);
        return status;
    }

    VIOSockTimerCreate(&pContext->SelectTimer, pContext->ThisDevice, VIOSockSelectTimerFunc);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT,
            "VIOSockTimerCreate failed (Select): 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "<-- %s\n", __FUNCTION__);

    return status;
}

static
VOID
VIOSockSelectCleanupFds(
    IN PVIOSOCK_SELECT_PKT      pPkt,
    IN VIRTIO_VSOCK_FDSET_TYPE  iFdSet,
    IN ULONG                    uStartIndex

)
{
    ULONG i;
    PVIOSOCK_SELECT_HANDLE  pHandleSet = &pPkt->Fds[uStartIndex];

    PAGED_CODE();

    for (i = 0; i < pPkt->FdCount[iFdSet]; ++i)
    {
        ASSERT(pHandleSet[i].Socket);

        InterlockedDecrement(&GetSocketContext(pHandleSet[i].Socket)->SelectRefs[iFdSet]); //dereference socket
        WdfObjectDereference(pHandleSet[i].Socket);
    }

    pPkt->FdCount[iFdSet] = 0;
}

__inline
VOID
VIOSockSelectCleanupPkt(
    IN PVIOSOCK_SELECT_PKT pPkt
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s, status: 0x%08x\n", __FUNCTION__, pPkt->Status);

    VIOSockSelectCleanupFds(pPkt, FDSET_READ, 0);
    VIOSockSelectCleanupFds(pPkt, FDSET_WRITE, pPkt->FdCount[FDSET_READ]);
    VIOSockSelectCleanupFds(pPkt, FDSET_EXCPT, pPkt->FdCount[FDSET_READ] + pPkt->FdCount[FDSET_WRITE]);
}

static
VOID
VIOSockSelectTimerFunc(
    IN WDFTIMER Timer
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfTimerGetParentObject(Timer));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    if (InterlockedIncrement(&pContext->SelectInProgress) == 1)
    {
        WdfWorkItemEnqueue(pContext->SelectWorkitem);
    }
}

static
BOOLEAN
VIOSockSelectCheckPkt(
    IN PVIOSOCK_SELECT_PKT  pPkt
)
{
    ULONG i;
    PVIOSOCK_SELECT_HANDLE  pHandleSet;
    PVIRTIO_VSOCK_FD_SET    pFds;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    pFds = &pPkt->pSelect->Fdss[FDSET_READ];
    pHandleSet = pPkt->Fds;

    pFds->fd_count = 0;
    for (i = 0; i < pPkt->FdCount[FDSET_READ]; ++i)
    {
        PSOCKET_CONTEXT pSocket = GetSocketContext(pHandleSet[i].Socket);
        if (pSocket->Events & (FD_ACCEPT | FD_READ | FD_CLOSE))
        {
            pFds->fd_array[pFds->fd_count++] = pHandleSet[i].hSocket;
        }
    }

    pFds = &pPkt->pSelect->Fdss[FDSET_WRITE];
    pHandleSet = &pPkt->Fds[pPkt->FdCount[FDSET_READ]];

    pFds->fd_count = 0;
    for (i = 0; i < pPkt->FdCount[FDSET_WRITE]; ++i)
    {
        PSOCKET_CONTEXT pSocket = GetSocketContext(pHandleSet[i].Socket);
        if (pSocket->Events & FD_WRITE ||
            (pSocket->Events & FD_CONNECT) && NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT_BIT]))
        {
            pFds->fd_array[pFds->fd_count++] = pHandleSet[i].hSocket;
        }
    }

    pFds = &pPkt->pSelect->Fdss[FDSET_EXCPT];
    pHandleSet = &pPkt->Fds[pPkt->FdCount[FDSET_READ] + pPkt->FdCount[FDSET_WRITE]];

    pFds->fd_count = 0;
    for (i = 0; i < pPkt->FdCount[FDSET_EXCPT]; ++i)
    {
        PSOCKET_CONTEXT pSocket = GetSocketContext(pHandleSet[i].Socket);
        if ((pSocket->Events & FD_CONNECT) &&
            !NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT_BIT]))
        {
            pFds->fd_array[pFds->fd_count++] = pHandleSet[i].hSocket;
        }
    }

    return pPkt->pSelect->Fdss[FDSET_READ].fd_count ||
        pPkt->pSelect->Fdss[FDSET_WRITE].fd_count ||
        pPkt->pSelect->Fdss[FDSET_EXCPT].fd_count;
}

static
VOID
VIOSockSelectCancel(
    IN WDFREQUEST Request
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    if (InterlockedIncrement(&pContext->SelectInProgress) == 1)
    {
        WdfWorkItemEnqueue(pContext->SelectWorkitem);
    }
}

static
VOID
VIOSockSelectWorkitem(
    IN WDFWORKITEM Workitem
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfWorkItemGetParentObject(Workitem));

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    do
    {
        LIST_ENTRY  CompletionList;
        WDFREQUEST  Request;
        LONGLONG    TimePassed, Timeout = LONGLONG_MAX;
        PLIST_ENTRY CurrentItem;
        BOOLEAN     bRemove;

        InterlockedExchange(&pContext->SelectInProgress, 1);

        InitializeListHead(&CompletionList);

        WdfWaitLockAcquire(pContext->SelectLock, NULL);

        TimePassed = VIOSockTimerPassed(&pContext->SelectTimer);

        for (CurrentItem = pContext->SelectList.Flink;
            CurrentItem != &pContext->SelectList;
            CurrentItem = CurrentItem->Flink)
        {
            PVIOSOCK_SELECT_PKT pPkt = CONTAINING_RECORD(CurrentItem, VIOSOCK_SELECT_PKT, ListEntry);
            WDFREQUEST Request = WdfObjectContextGetObject(pPkt);
            NTSTATUS status = WdfRequestUnmarkCancelable(Request);

            ASSERT(NT_SUCCESS(status) || status == STATUS_CANCELLED);

            bRemove = FALSE;

            if (status == STATUS_CANCELLED)
            {
                bRemove = TRUE;
                pPkt->Status = STATUS_CANCELLED;
            }
            else if (VIOSockSelectCheckPkt(pPkt))
            {
                bRemove = TRUE;
                pPkt->Status = STATUS_SUCCESS;
            }
            else if (pPkt->Timeout)
            {
                if (pPkt->Timeout <= TimePassed + VIOSOCK_TIMER_TOLERANCE)
                {
                    bRemove = TRUE;
                    pPkt->Status = STATUS_TIMEOUT;
                }
                else
                {
                    pPkt->Timeout -= TimePassed;

                    if (pPkt->Timeout < Timeout)
                        Timeout = pPkt->Timeout;
                }
            }

            if (!bRemove)
            {
                status = WdfRequestMarkCancelableEx(Request, VIOSockSelectCancel);

                ASSERT(NT_SUCCESS(status) || status == STATUS_CANCELLED);

                if (status == STATUS_CANCELLED)
                {
                    bRemove = TRUE;
                    pPkt->Status = STATUS_CANCELLED;
                }
            }

            if (bRemove)
            {
                CurrentItem = pPkt->ListEntry.Blink;
                RemoveEntryList(&pPkt->ListEntry);
                InsertTailList(&CompletionList, &pPkt->ListEntry);
                if (pPkt->Timeout)
                    VIOSockTimerDeref(&pContext->SelectTimer, TRUE);
            }
        }

        if(Timeout!=LONGLONG_MAX)
            VIOSockTimerSet(&pContext->SelectTimer, Timeout);

        WdfWaitLockRelease(pContext->SelectLock);

        while (!IsListEmpty(&CompletionList))
        {
            PVIOSOCK_SELECT_PKT pPkt = CONTAINING_RECORD(RemoveHeadList(&CompletionList), VIOSOCK_SELECT_PKT, ListEntry);

            VIOSockSelectCleanupPkt(pPkt);
            WdfRequestComplete(WdfObjectContextGetObject(pPkt), pPkt->Status);
        }

    } while (InterlockedCompareExchange(&pContext->SelectInProgress, 0, 1) != 1);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "<-- %s\n", __FUNCTION__);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
VIOSockSelectRun(
    IN PSOCKET_CONTEXT pSocket
)
{
    BOOLEAN bRun = FALSE;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    if (pSocket->SelectRefs[FDSET_READ])
    {
        bRun |= pSocket->Events & (FD_ACCEPT | FD_READ | FD_CLOSE);
    }

    if (pSocket->SelectRefs[FDSET_WRITE])
    {
        bRun |= pSocket->Events & FD_WRITE ||
            (pSocket->Events & FD_CONNECT) && NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT_BIT]);
    }

    if (pSocket->SelectRefs[FDSET_EXCPT])
    {
        bRun |= (pSocket->Events & FD_CONNECT) &&
            !NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT_BIT]);
    }

    if (bRun && InterlockedIncrement(&pContext->SelectInProgress) == 1)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "Enqueue workitem\n");
        WdfWorkItemEnqueue(pContext->SelectWorkitem);
    }

}

static
BOOLEAN
VIOSockSelectCopyFds(
    IN PDEVICE_CONTEXT          pContext,
    IN BOOLEAN                  bIs32BitProcess,
    IN PVIRTIO_VSOCK_SELECT     pSelect,
    IN PVIOSOCK_SELECT_PKT      pPkt,
    IN VIRTIO_VSOCK_FDSET_TYPE  iFdSet,
    IN ULONG                    uStartIndex
)
{
    ULONG                   i;
    PVIOSOCK_SELECT_HANDLE  pHandleSet = &pPkt->Fds[uStartIndex];

    PAGED_CODE();

    pPkt->FdCount[iFdSet] = 0;
    for (i = 0; i < pSelect->Fdss[iFdSet].fd_count; ++i)
    {
        ULONGLONG hSocket = pSelect->Fdss[iFdSet].fd_array[i];
        if (hSocket)
        {
            WDFFILEOBJECT Socket = VIOSockGetSocketFromHandle(pContext, hSocket, bIs32BitProcess);
            if (Socket != WDF_NO_HANDLE)
            {
                PSOCKET_CONTEXT pSocket = GetSocketContext(Socket);

                pHandleSet[i].hSocket = hSocket;
                pHandleSet[i].Socket = Socket;
                InterlockedIncrement(&pSocket->SelectRefs[iFdSet]); //reference socket
            }
            else
                break;
        }
        else
            break;
    }

    pPkt->FdCount[iFdSet] = i;

    return i == pSelect->Fdss[iFdSet].fd_count;
}

static
NTSTATUS
VIOSockSelect(
    IN WDFREQUEST Request,
    IN OUT size_t *pLength
)
{
    PDEVICE_CONTEXT         pContext = GetDeviceContextFromRequest(Request);
    PVIRTIO_VSOCK_SELECT    pSelect;
    SIZE_T                  stSelectLen;
    NTSTATUS                status;
    BOOLEAN                 bIs32BitProcess = FALSE;
    WDF_OBJECT_ATTRIBUTES   Attributes;
    PVIOSOCK_SELECT_PKT     pPkt = NULL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    *pLength = 0;

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pSelect), &pSelect, &stSelectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stSelectLen >= sizeof(*pSelect));

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pSelect), &pSelect, &stSelectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stSelectLen >= sizeof(*pSelect));

    if (FD_SETSIZE < pSelect->Fdss[FDSET_READ].fd_count +
        pSelect->Fdss[FDSET_WRITE].fd_count +
        pSelect->Fdss[FDSET_EXCPT].fd_count)
    {
        return STATUS_INVALID_PARAMETER;
    }

#ifdef _WIN64
    bIs32BitProcess = WdfRequestIsFrom32BitProcess(Request);
#endif //_WIN64

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &Attributes,
        VIOSOCK_SELECT_PKT
    );

    status = WdfObjectAllocateContext(Request, &Attributes, &pPkt);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return status;
    }

    if (!VIOSockSelectCopyFds(pContext, bIs32BitProcess, pSelect, pPkt, FDSET_READ, 0) ||
        !VIOSockSelectCopyFds(pContext, bIs32BitProcess, pSelect, pPkt, FDSET_WRITE, pPkt->FdCount[FDSET_READ]) ||
        !VIOSockSelectCopyFds(pContext, bIs32BitProcess, pSelect, pPkt, FDSET_EXCPT,
            pPkt->FdCount[FDSET_READ] + pPkt->FdCount[FDSET_WRITE]))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SELECT, "VIOSockSelectCopyFds failed\n");
        status = STATUS_INVALID_HANDLE;
    }

    if (NT_SUCCESS(status))
    {
        WdfWaitLockAcquire(pContext->SelectLock, NULL);

        pPkt->Status = status = VIOSockSelectCheckPkt(pPkt) ? STATUS_SUCCESS : STATUS_PENDING;

        if (status == STATUS_PENDING)
        {
            status = WdfRequestMarkCancelableEx(Request, VIOSockSelectCancel);

            ASSERT(NT_SUCCESS(status) || status == STATUS_CANCELLED);

            if (NT_SUCCESS(status))
            {
                status = STATUS_PENDING;

                InsertTailList(&pContext->SelectList, &pPkt->ListEntry);
                pPkt->Timeout = pSelect->Timeout;

                if (pPkt->Timeout)
                    VIOSockTimerStart(&pContext->SelectTimer, pPkt->Timeout);
            }
        }
        else
            *pLength = sizeof(*pSelect);

        WdfWaitLockRelease(pContext->SelectLock);
    }

    if (status != STATUS_PENDING)
        VIOSockSelectCleanupPkt(pPkt);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "<-- %s\n", __FUNCTION__);

    return status;
}
