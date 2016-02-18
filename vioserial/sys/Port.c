#include "precomp.h"
#include "vioser.h"
#include "public.h"
#include <wdmsec.h>

#if defined(EVENT_TRACING)
#include "Port.tmh"
#endif

EVT_WDF_WORKITEM VIOSerialPortSymbolicNameWork;
EVT_WDF_WORKITEM VIOSerialPortPnpNotifyWork;
EVT_WDF_REQUEST_CANCEL VIOSerialPortReadRequestCancel;
EVT_WDF_REQUEST_CANCEL VIOSerialPortWriteRequestCancel;
EVT_WDF_DEVICE_D0_ENTRY VIOSerialPortEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT VIOSerialPortEvtDeviceD0Exit;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VIOSerialDeviceListCreatePdo)
#pragma alloc_text(PAGE, VIOSerialPortWrite)
#pragma alloc_text(PAGE, VIOSerialPortDeviceControl)
#pragma alloc_text(PAGE, VIOSerialPortEvtDeviceD0Entry)
#endif

PVIOSERIAL_PORT
VIOSerialFindPortById(
    IN WDFDEVICE Device,
    IN ULONG id
)
{
    NTSTATUS        status = STATUS_SUCCESS;
    WDFCHILDLIST    list;
    WDF_CHILD_LIST_ITERATOR     iterator;
    PRAWPDO_VIOSERIAL_PORT          rawPdo = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP,"%s  port = %d\n", __FUNCTION__, id);

    list = WdfFdoGetDefaultChildList(Device);
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator,
                                 WdfRetrievePresentChildren );

    WdfChildListBeginIteration(list, &iterator);

    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO  childInfo;
        VIOSERIAL_PORT           port;
        WDFDEVICE                hChild;

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &port.Header,
                                 sizeof(port)
                                 );
        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &port.Header);

        status = WdfChildListRetrieveNextDevice(
                                 list,
                                 &iterator,
                                 &hChild,
                                 &childInfo
                                 );
        if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        ASSERT(childInfo.Status == WdfChildListRetrieveDeviceSuccess);
        rawPdo = RawPdoSerialPortGetData(hChild);

        if(rawPdo && rawPdo->port->PortId == id)
        {
            WdfChildListEndIteration(list, &iterator);
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s  id = %d port = 0x%p\n", __FUNCTION__, id, rawPdo->port);
            return rawPdo->port;
        }
    }
    WdfChildListEndIteration(list, &iterator);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return NULL;
}

VOID
VIOSerialAddPort(
    IN WDFDEVICE Device,
    IN ULONG id
)
{
    VIOSERIAL_PORT  port;
    PPORTS_DEVICE   pContext = GetPortsDevice(Device);
    NTSTATUS        status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s  DeviceId = %d :: PortId = %d\n", __FUNCTION__, pContext->DeviceId, id);

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &port.Header,
                                 sizeof(port)
                                 );

    port.PortId = id;
    port.DeviceId = pContext->DeviceId;
    port.NameString.Buffer = NULL;
    port.NameString.Length = 0;
    port.NameString.MaximumLength = 0;

    port.InBuf = NULL;
    port.HostConnected = port.GuestConnected = FALSE;
    port.OutVqFull = FALSE;
    port.Removed = FALSE;

    port.BusDevice = Device;

    status = WdfChildListAddOrUpdateChildDescriptionAsPresent(
                                 WdfFdoGetDefaultChildList(Device),
                                 &port.Header,
                                 NULL
                                 );

    if (status == STATUS_OBJECT_NAME_EXISTS)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
           "The description is already present in the list, the serial number is not unique.\n");
        return;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
           "WdfChildListAddOrUpdateChildDescriptionAsPresent = 0x%x.\n", status);

}

VOID
VIOSerialRemovePort(
    IN WDFDEVICE Device,
    IN PVIOSERIAL_PORT Port
)
{
    WDFCHILDLIST PortList;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
        "--> %s DeviceId: %d PortId: %d\n",
        __FUNCTION__, Port->DeviceId, Port->PortId);

    Port->Removed = TRUE;

    PortList = WdfFdoGetDefaultChildList(Device);

    status = WdfChildListUpdateChildDescriptionAsMissing(PortList,
        &Port->Header);

    if (status == STATUS_NO_SUCH_DEVICE)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
            "A device which does not exist was specified.\n", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialInitPortConsole(
    IN PVIOSERIAL_PORT port
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    port->GuestConnected = TRUE;
    VIOSerialSendCtrlMsg(port->BusDevice, port->PortId, VIRTIO_CONSOLE_PORT_OPEN, 1);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"<-- %s\n", __FUNCTION__);
}

// this procedure must be called with port InBuf spinlock held
VOID
VIOSerialDiscardPortDataLocked(
    IN PVIOSERIAL_PORT port
)
{
    struct virtqueue *vq;
    PPORT_BUFFER buf = NULL;
    UINT len;
    NTSTATUS  status = STATUS_SUCCESS;
    UINT ret = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    vq = GetInQueue(port);

    if (port->InBuf)
    {
        buf = port->InBuf;
    }
    else if (vq)
    {
        buf = (PPORT_BUFFER)virtqueue_get_buf(vq, &len);
    }

    while (buf)
    {
        status = VIOSerialAddInBuf(vq, buf);
        if(!NT_SUCCESS(status))
        {
           ++ret;
           VIOSerialFreeBuffer(buf);
        }
        buf = (PPORT_BUFFER)virtqueue_get_buf(vq, &len);
    }
    port->InBuf = NULL;
    if (ret > 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "%s::%d Error adding %u buffers back to queue\n",
                      __FUNCTION__, __LINE__, ret);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"<-- %s\n", __FUNCTION__);
}

// this procedure must be called with port InBuf spinlock held
BOOLEAN
VIOSerialPortHasDataLocked(
    IN PVIOSERIAL_PORT port
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    if (port->InBuf)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return TRUE;
    }
    port->InBuf = (PPORT_BUFFER)VIOSerialGetInBuf(port);
    if (port->InBuf)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return TRUE;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
    return FALSE;
}


BOOLEAN
VIOSerialWillWriteBlock(
    IN PVIOSERIAL_PORT port
)
{
    BOOLEAN ret = FALSE;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"--> %s\n", __FUNCTION__);
    if (!port->HostConnected)
    {
        return TRUE;
    }

    ret = VIOSerialReclaimConsumedBuffers(port);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"<-- %s\n", __FUNCTION__);
    return ret;
}

NTSTATUS
VIOSerialDeviceListCreatePdo(
    IN WDFCHILDLIST DeviceList,
    IN PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    IN PWDFDEVICE_INIT ChildInit
    )
{
    PVIOSERIAL_PORT                 pport = NULL;
    NTSTATUS                        status = STATUS_SUCCESS;

    WDFDEVICE                       hChild = NULL;

    WDF_OBJECT_ATTRIBUTES           attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS    PnpPowerCallbacks;
    WDF_DEVICE_PNP_CAPABILITIES     pnpCaps;
    WDF_DEVICE_STATE                deviceState;
    WDF_IO_QUEUE_CONFIG             queueConfig;
    PRAWPDO_VIOSERIAL_PORT          rawPdo = NULL;
    WDF_FILEOBJECT_CONFIG           fileConfig;

    DECLARE_CONST_UNICODE_STRING(deviceId, PORT_DEVICE_ID );
    DECLARE_CONST_UNICODE_STRING(deviceLocation, L"RedHat VIOSerial Port" );

    DECLARE_UNICODE_STRING_SIZE(buffer, DEVICE_DESC_LENGTH);

    UNREFERENCED_PARAMETER(DeviceList);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    pport = CONTAINING_RECORD(IdentificationDescription,
                                 VIOSERIAL_PORT,
                                 Header
                                 );

    WdfDeviceInitSetDeviceType(ChildInit, FILE_DEVICE_SERIAL_PORT);
    WdfDeviceInitSetIoType(ChildInit, WdfDeviceIoDirect);

    do
    {
        WdfDeviceInitSetExclusive(ChildInit, TRUE);
        status = WdfPdoInitAssignRawDevice(ChildInit, &GUID_DEVCLASS_PORT_DEVICE);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAssignRawDevice failed - 0x%x\n", status);
           break;
        }

        status = WdfDeviceInitAssignSDDLString(ChildInit, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceInitAssignSDDLString failed - 0x%x\n", status);
           break;
        }

        status = WdfPdoInitAssignDeviceID(ChildInit, &deviceId);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAssignDeviceID failed - 0x%x\n", status);
           break;
        }

        status = WdfPdoInitAddHardwareID(ChildInit, &deviceId);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAddHardwareID failed - 0x%x\n", status);
           break;
        }

        status = RtlUnicodeStringPrintf(
                                 &buffer,
                                 L"%02u",
                                 pport->PortId
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "RtlUnicodeStringPrintf failed - 0x%x\n", status);
           break;
        }

        status = WdfPdoInitAssignInstanceID(ChildInit, &buffer);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAssignInstanceID failed - 0x%x\n", status);
           break;
        }

        status = RtlUnicodeStringPrintf(
                                 &buffer,
                                 L"vport%up%u",
                                 pport->DeviceId,
                                 pport->PortId
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "RtlUnicodeStringPrintf failed 0x%x\n", status);
           break;
        }

        status = WdfPdoInitAddDeviceText(
                                 ChildInit,
                                 &buffer,
                                 &deviceLocation,
                                 0x409
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfPdoInitAddDeviceText failed 0x%x\n", status);
           break;
        }

        WdfPdoInitSetDefaultLocale(ChildInit, 0x409);

        WDF_FILEOBJECT_CONFIG_INIT(
                                 &fileConfig,
                                 VIOSerialPortCreate,
                                 VIOSerialPortClose,
                                 WDF_NO_EVENT_CALLBACK
                                 );

        WdfDeviceInitSetFileObjectConfig(
                                 ChildInit,
                                 &fileConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES
                                 );

        WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
        PnpPowerCallbacks.EvtDeviceD0Entry = VIOSerialPortEvtDeviceD0Entry;
        PnpPowerCallbacks.EvtDeviceD0Exit = VIOSerialPortEvtDeviceD0Exit;
        WdfDeviceInitSetPnpPowerEventCallbacks(ChildInit, &PnpPowerCallbacks);

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, RAWPDO_VIOSERIAL_PORT);
        attributes.SynchronizationScope = WdfSynchronizationScopeDevice;
        attributes.ExecutionLevel = WdfExecutionLevelPassive;

        status = WdfDeviceCreate(
                                 &ChildInit,
                                 &attributes,
                                 &hChild
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfDeviceCreate failed 0x%x\n", status);
           break;
        }

        rawPdo = RawPdoSerialPortGetData(hChild);
        rawPdo->port = pport;
        pport->Device = hChild;

        WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
                                 WdfIoQueueDispatchSequential
                                 );

        queueConfig.EvtIoDeviceControl = VIOSerialPortDeviceControl;
        status = WdfIoQueueCreate(hChild,
                                 &queueConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &pport->IoctlQueue
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfIoQueueCreate failed (IoCtrl Queue): 0x%x\n", status);
           break;
        }
        status = WdfDeviceConfigureRequestDispatching(
                                 hChild,
                                 pport->IoctlQueue,
                                 WdfRequestTypeDeviceControl
                                 );

        if(!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "DeviceConfigureRequestDispatching failed (IoCtrl Queue): 0x%x\n", status);
           break;
        }

        WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
                                 WdfIoQueueDispatchSequential);

        queueConfig.EvtIoRead   =  VIOSerialPortRead;
        queueConfig.EvtIoStop   =  VIOSerialPortReadIoStop;
        status = WdfIoQueueCreate(hChild,
                                 &queueConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &pport->ReadQueue
                                 );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfIoQueueCreate (Read Queue) failed 0x%x\n", status);
           break;
        }

        status = WdfDeviceConfigureRequestDispatching(
                                 hChild,
                                 pport->ReadQueue,
                                 WdfRequestTypeRead
                                 );

        if(!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "DeviceConfigureRequestDispatching failed (Read Queue): 0x%x\n", status);
           break;
        }

        WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchParallel);
        queueConfig.AllowZeroLengthRequests = WdfFalse;
        queueConfig.EvtIoWrite = VIOSerialPortWrite;
        queueConfig.EvtIoStop = VIOSerialPortWriteIoStop;

        status = WdfIoQueueCreate(hChild, &queueConfig,
            WDF_NO_OBJECT_ATTRIBUTES, &pport->WriteQueue);

        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfIoQueueCreate failed (Write Queue): 0x%x\n", status);
           break;
        }
        status = WdfDeviceConfigureRequestDispatching(
                                 hChild,
                                 pport->WriteQueue,
                                 WdfRequestTypeWrite
                                 );

        if(!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "DeviceConfigureRequestDispatching failed (Write Queue): 0x%x\n", status);
           break;
        }

        WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);

        pnpCaps.NoDisplayInUI    =  WdfTrue;
        pnpCaps.Removable        =  WdfTrue;
        pnpCaps.EjectSupported   =  WdfTrue;
        pnpCaps.SurpriseRemovalOK=  WdfTrue;
        pnpCaps.Address          =  pport->DeviceId;
        pnpCaps.UINumber         =  pport->PortId;

        WdfDeviceSetPnpCapabilities(hChild, &pnpCaps);

        WDF_DEVICE_STATE_INIT(&deviceState);
        deviceState.DontDisplayInUI = WdfTrue;
        WdfDeviceSetDeviceState(hChild, &deviceState);

        status = WdfDeviceCreateDeviceInterface(
                                 hChild,
                                 &GUID_VIOSERIAL_PORT,
                                 NULL
                                 );

        if (!NT_SUCCESS (status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfDeviceCreateDeviceInterface failed 0x%x\n", status);
           break;
        }

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = hChild;
        status = WdfSpinLockCreate(
                                &attributes,
                                &pport->InBufLock
                                );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfSpinLockCreate failed 0x%x\n", status);
           break;
        }

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = hChild;
        status = WdfSpinLockCreate(
                                &attributes,
                                &pport->OutVqLock
                                );
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfSpinLockCreate failed 0x%x\n", status);
           break;
        }

    } while (0);

    if (!NT_SUCCESS(status))
    {
        // We can send this before PDO is PRESENT since the device won't send any response.
        VIOSerialSendCtrlMsg(pport->BusDevice, pport->PortId, VIRTIO_CONSOLE_PORT_READY, 0);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s status 0x%x\n", __FUNCTION__, status);
    return status;
}

VOID
VIOSerialPortRead(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     Length
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfIoQueueGetDevice(Queue));
    PVIOSERIAL_PORT         pport = pdoData->port;
    size_t             length;
    NTSTATUS           status;
    PVOID              systemBuffer;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "-->%s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, Length, &systemBuffer, &length);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

	WdfSpinLockAcquire(pport->InBufLock);

	if (!VIOSerialPortHasDataLocked(pport))
	{
		if (!pport->HostConnected)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			length = 0;
		}
		else
		{
			ASSERT (pport->PendingReadRequest == NULL);
			status = WdfRequestMarkCancelableEx(Request,
				VIOSerialPortReadRequestCancel);
			if (!NT_SUCCESS(status))
			{
				length = 0;
			}
			else
			{
				pport->PendingReadRequest = Request;
				Request = NULL;
			}
		}
    }
    else
    {
        length = (ULONG)VIOSerialFillReadBufLocked(pport, systemBuffer, length);
        if (!length)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    WdfSpinLockRelease(pport->InBufLock);

    if (Request != NULL)
    {
        // we are completing the request right here, either because of
        // an error or because data was available in the input buffer
        WdfRequestCompleteWithInformation(Request, status, (ULONG_PTR)length);
    }

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ,"<-- %s\n", __FUNCTION__);
}

VOID VIOSerialPortWrite(IN WDFQUEUE Queue,
                        IN WDFREQUEST Request,
                        IN size_t Length)
{
    NTSTATUS status;
    PVOID InBuf;
    PVOID buffer;
    PVIOSERIAL_PORT Port;
    PWRITE_BUFFER_ENTRY entry;
    WDFDEVICE Device;
    PDRIVER_CONTEXT Context;
    WDFMEMORY EntryHandle;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE,
        "--> %s Request: %p Length: %d\n", __FUNCTION__, Request, Length);

    PAGED_CODE();

    Device = WdfIoQueueGetDevice(Queue);
    Port = RawPdoSerialPortGetData(Device)->port;
    if (Port->Removed)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE,
            "Write request on a removed port %d\n", Port->PortId);
        WdfRequestComplete(Request, STATUS_OBJECT_NO_LONGER_EXISTS);
        return;
    }

    status = WdfRequestRetrieveInputBuffer(Request, Length, &InBuf, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "Failed to retrieve input buffer: %x\n", status);
        WdfRequestComplete(Request, status);
        return;
    }

    if (VIOSerialWillWriteBlock(Port))
    {
        WdfRequestComplete(Request, STATUS_CANT_WAIT);
        return;
    }

    buffer = ExAllocatePoolWithTag(NonPagedPool, Length,
        VIOSERIAL_DRIVER_MEMORY_TAG);

    if (buffer == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "Failed to allocate.\n");
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    Context = GetDriverContext(WdfDeviceGetDriver(Device));
    status = WdfMemoryCreateFromLookaside(Context->WriteBufferLookaside, &EntryHandle);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "Failed to allocate write buffer entry: %x.\n", status);
        ExFreePoolWithTag(buffer, VIOSERIAL_DRIVER_MEMORY_TAG);
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    status = WdfRequestMarkCancelableEx(Request,
        VIOSerialPortWriteRequestCancel);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "Failed to mark request as cancelable: %x\n", status);
        ExFreePoolWithTag(buffer, VIOSERIAL_DRIVER_MEMORY_TAG);
        WdfObjectDelete(EntryHandle);
        WdfRequestComplete(Request, status);
        return;
    }

    RtlCopyMemory(buffer, InBuf, Length);
    WdfRequestSetInformation(Request, (ULONG_PTR)Length);

    entry = (PWRITE_BUFFER_ENTRY)WdfMemoryGetBuffer(EntryHandle, NULL);
    entry->EntryHandle = EntryHandle;
    entry->Buffer = buffer;
    entry->Request = Request;

    if (VIOSerialSendBuffers(Port, entry, Length) <= 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "Failed to send user's buffer.\n");

        ExFreePoolWithTag(buffer, VIOSERIAL_DRIVER_MEMORY_TAG);
        WdfObjectDelete(EntryHandle);

        if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            WdfRequestComplete(Request, Port->Removed ?
                STATUS_INVALID_DEVICE_STATE : STATUS_INSUFFICIENT_RESOURCES);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE,"<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialPortReadRequestCancel(
    IN WDFREQUEST Request
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request)));

    TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "-->%s called on request 0x%p\n", __FUNCTION__, Request);

    // synchronize with VIOSerialQueuesInterruptDpc because the pending
    // request is not guaranteed to be alive after we return from this callback
    WdfSpinLockAcquire(pdoData->port->InBufLock);
    pdoData->port->PendingReadRequest = NULL;
    WdfSpinLockRelease(pdoData->port->InBufLock);

    WdfRequestCompleteWithInformation(Request, STATUS_CANCELLED, 0L);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

VOID VIOSerialPortWriteRequestCancel(IN WDFREQUEST Request)
{
    PVIOSERIAL_PORT Port = RawPdoSerialPortGetData(
        WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request)))->port;
    PSINGLE_LIST_ENTRY iter;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s Request: 0x%p\n",
        __FUNCTION__, Request);

    // synchronize with VIOSerialReclaimConsumedBuffers because the pending
    // request is not guaranteed to be alive after we return from this callback
    WdfSpinLockAcquire(Port->OutVqLock);
    iter = &Port->WriteBuffersList;
    while ((iter = iter->Next) != NULL)
    {
        PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter, WRITE_BUFFER_ENTRY, ListEntry);
        if (entry->Request == Request)
        {
            entry->Request = NULL;
            break;
        }
    }
    WdfSpinLockRelease(Port->OutVqLock);

    WdfRequestCompleteWithInformation(Request, STATUS_CANCELLED, 0L);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialPortDeviceControl(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     OutputBufferLength,
    IN size_t     InputBufferLength,
    IN ULONG      IoControlCode
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfIoQueueGetDevice(Queue));
    size_t                  length = 0;
    NTSTATUS                status = STATUS_SUCCESS;
    PVIRTIO_PORT_INFO       pport_info = NULL;
    size_t                  name_size = 0;

    PAGED_CODE();

    UNREFERENCED_PARAMETER( InputBufferLength  );
    UNREFERENCED_PARAMETER( OutputBufferLength  );

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    switch (IoControlCode)
    {

        case IOCTL_GET_INFORMATION:
        {
           status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VIRTIO_PORT_INFO), (PVOID*)&pport_info, &length);
           if (!NT_SUCCESS(status))
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                            "WdfRequestRetrieveInputBuffer failed 0x%x\n", status);
              break;
           }

           RtlZeroMemory(pport_info, sizeof(VIRTIO_PORT_INFO));
           pport_info->Id = pdoData->port->PortId;
           pport_info->OutVqFull = pdoData->port->OutVqFull;
           pport_info->HostConnected = pdoData->port->HostConnected;
           pport_info->GuestConnected = pdoData->port->GuestConnected;

           status = STATUS_SUCCESS;
           if (pdoData->port->NameString.Buffer)
           {
              name_size = pdoData->port->NameString.MaximumLength;
              if (length < sizeof(VIRTIO_PORT_INFO) + name_size)
              {
                 status = STATUS_BUFFER_OVERFLOW;
                 TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS,
                               "Buffer too small. got = %d, expected = %d\n", length, sizeof (VIRTIO_PORT_INFO) + name_size);
              }
              else
              {
                 RtlZeroMemory(pport_info->Name, name_size);
                 status = RtlStringCbCopyA(pport_info->Name, name_size - 1, pdoData->port->NameString.Buffer);
                 if (!NT_SUCCESS(status))
                 {
                    TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                      "RtlStringCbCopyA failed 0x%x\n", status);
                    name_size = 0;
                 }
              }
           }
           length =  sizeof (VIRTIO_PORT_INFO) + name_size;
           break;
        }

        default:
           status = STATUS_INVALID_DEVICE_REQUEST;
           break;
    }

    WdfRequestCompleteWithInformation(Request, status, length);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialPortCreate(
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfDevice);
    NTSTATUS                status  = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(FileObject);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "%s Port id = %d\n", __FUNCTION__, pdoData->port->PortId);

    if (pdoData->port->Removed)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
            "Connect request on removed port id %d\n", pdoData->port->PortId);
        status = STATUS_OBJECT_NO_LONGER_EXISTS;
    }
    else if (pdoData->port->GuestConnected == TRUE)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
            "Guest already connected to port id %d\n", pdoData->port->PortId);
        status = STATUS_OBJECT_NAME_EXISTS;
    }
    else
    {
        pdoData->port->GuestConnected = TRUE;

        VIOSerialReclaimConsumedBuffers(pdoData->port);

        VIOSerialSendCtrlMsg(pdoData->port->BusDevice, pdoData->port->PortId,
            VIRTIO_CONSOLE_PORT_OPEN, 1);
    }

    WdfRequestComplete(Request, status);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialPortClose(
    IN WDFFILEOBJECT FileObject
    )
{
    PRAWPDO_VIOSERIAL_PORT pdoData = RawPdoSerialPortGetData(
        WdfFileObjectGetDevice(FileObject));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "--> %s\n", __FUNCTION__);

    if (!pdoData->port->Removed)
    {
        if (pdoData->port->GuestConnected) {
            VIOSerialSendCtrlMsg(pdoData->port->BusDevice,
                pdoData->port->PortId, VIRTIO_CONSOLE_PORT_OPEN, 0);
        }

        WdfSpinLockAcquire(pdoData->port->InBufLock);
        VIOSerialDiscardPortDataLocked(pdoData->port);
        WdfSpinLockRelease(pdoData->port->InBufLock);

        VIOSerialReclaimConsumedBuffers(pdoData->port);
    }

    pdoData->port->GuestConnected = FALSE;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialPortCreateName(
    IN WDFDEVICE WdfDevice,
    IN PVIOSERIAL_PORT port,
    IN PPORT_BUFFER buf
    )
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG   workitemConfig;
    WDFWORKITEM           hWorkItem;
    PRAWPDO_VIOSERIAL_PORT  pdoData = NULL;
    NTSTATUS              status = STATUS_SUCCESS;
    size_t                length;
    PVIRTIO_CONSOLE_CONTROL cpkt;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "--> %s\n", __FUNCTION__);
    cpkt = (PVIRTIO_CONSOLE_CONTROL)((ULONG_PTR)buf->va_buf + buf->offset);
    if (port && !port->NameString.Buffer)
    {
        length = buf->len - buf->offset - sizeof(VIRTIO_CONSOLE_CONTROL);
        port->NameString.Length = (USHORT)( length );
        port->NameString.MaximumLength = port->NameString.Length + 1;
        port->NameString.Buffer = (PCHAR)ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 port->NameString.MaximumLength,
                                 VIOSERIAL_DRIVER_MEMORY_TAG
                                 );
        if (port->NameString.Buffer)
        {
           RtlCopyMemory(  port->NameString.Buffer,
                                 (PVOID)((LONG_PTR)buf->va_buf + buf->offset + sizeof(*cpkt)),
                                 length
                                 );
           port->NameString.Buffer[length] = '\0';
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "VIRTIO_CONSOLE_PORT_NAME name_size = %d %s\n", length, port->NameString.Buffer);
        }
        else
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                                 "VIRTIO_CONSOLE_PORT_NAME: Unable to alloc string buffer\n"
                                 );
        }

        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, RAWPDO_VIOSERIAL_PORT);
        attributes.ParentObject = WdfDevice;
        WDF_WORKITEM_CONFIG_INIT(&workitemConfig, VIOSerialPortSymbolicNameWork);

        status = WdfWorkItemCreate( &workitemConfig,
                                 &attributes,
                                 &hWorkItem);

        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "WdfWorkItemCreate failed with status = 0x%08x\n", status);
           return;
        }

        pdoData = RawPdoSerialPortGetData(hWorkItem);

        pdoData->port = port;

        WdfWorkItemEnqueue(hWorkItem);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "VIRTIO_CONSOLE_PORT_NAME invalid id = %d\n", cpkt->id);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialPortPnpNotify (
    IN WDFDEVICE WdfDevice,
    IN PVIOSERIAL_PORT port,
    IN BOOLEAN connected
)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG   workitemConfig;
    WDFWORKITEM           hWorkItem;
    PRAWPDO_VIOSERIAL_PORT  pdoData = NULL;
    NTSTATUS              status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);
    port->HostConnected = connected;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, RAWPDO_VIOSERIAL_PORT);
    attributes.ParentObject = WdfDevice;
    WDF_WORKITEM_CONFIG_INIT(&workitemConfig, VIOSerialPortPnpNotifyWork);

    status = WdfWorkItemCreate( &workitemConfig,
                                 &attributes,
                                 &hWorkItem);

    if (!NT_SUCCESS(status))
    {
       TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "WdfWorkItemCreate failed with status = 0x%08x\n", status);
       return;
    }

    pdoData = RawPdoSerialPortGetData(hWorkItem);

    pdoData->port = port;

    WdfWorkItemEnqueue(hWorkItem);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialPortSymbolicNameWork(
    IN WDFWORKITEM  WorkItem
    )
{

    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WorkItem);
    PVIOSERIAL_PORT         pport = pdoData->port;
    UNICODE_STRING          deviceUnicodeString = {0};
    NTSTATUS                status  = STATUS_SUCCESS;

    DECLARE_UNICODE_STRING_SIZE(symbolicLinkName, 256);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    do
    {
        if (pport->NameString.Buffer)
        {
           status = RtlAnsiStringToUnicodeString( &deviceUnicodeString,
                                 &pport->NameString,
                                 TRUE
                                 );
           if (!NT_SUCCESS(status))
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "RtlAnsiStringToUnicodeString failed 0x%x\n", status);
              break;
           }

           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"deviceUnicodeString = %ws\n", deviceUnicodeString.Buffer);

           status = RtlUnicodeStringPrintf(
                                 &symbolicLinkName,
                                 L"%ws%ws",
                                 L"\\DosDevices\\",
                                 deviceUnicodeString.Buffer
                                 );
           if (!NT_SUCCESS(status))
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "RtlUnicodeStringPrintf failed 0x%x\n", status);
              break;
           }

           status = WdfDeviceCreateSymbolicLink(
                                 pport->Device,
                                 &symbolicLinkName
                                 );
           if (!NT_SUCCESS(status))
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfDeviceCreateSymbolicLink %ws failed 0x%x\n", &symbolicLinkName, status);
              break;
           }
        }
    } while (0);

    if (deviceUnicodeString.Buffer != NULL)
    {
        RtlFreeUnicodeString( &deviceUnicodeString );
    }
    WdfObjectDelete(WorkItem);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialPortPnpNotifyWork(
    IN WDFWORKITEM  WorkItem
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WorkItem);
    PVIOSERIAL_PORT         pport = pdoData->port;
    PTARGET_DEVICE_CUSTOM_NOTIFICATION  notification;
    ULONG                               requiredSize;
    NTSTATUS                            status;
    VIRTIO_PORT_STATUS_CHANGE           portStatus = {0};

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);
    portStatus.Version = 1;
    portStatus.Reason = pport->HostConnected;

    status = RtlULongAdd((sizeof(TARGET_DEVICE_CUSTOM_NOTIFICATION) - sizeof(UCHAR)),
                                 sizeof(VIRTIO_PORT_STATUS_CHANGE),
                                 &requiredSize);

    if (NT_SUCCESS(status))
    {
        notification = (PTARGET_DEVICE_CUSTOM_NOTIFICATION)
                       ExAllocatePoolWithTag(NonPagedPool,
                                 requiredSize,
                                 VIOSERIAL_DRIVER_MEMORY_TAG);

        if (notification != NULL)
        {
            RtlZeroMemory(notification, requiredSize);
            notification->Version = 1;
            notification->Size = (USHORT)(requiredSize);
            notification->FileObject = NULL;
            notification->NameBufferOffset = -1;
            notification->Event = GUID_VIOSERIAL_PORT_CHANGE_STATUS;
            RtlCopyMemory(notification->CustomDataBuffer, &portStatus, sizeof(VIRTIO_PORT_STATUS_CHANGE));
            if(WdfDeviceGetDevicePnpState(pport->Device) == WdfDevStatePnpStarted)
            {
               status = IoReportTargetDeviceChangeAsynchronous(
                                 WdfDeviceWdmGetPhysicalDevice(pport->Device),
                                 notification,
                                 NULL,
                                 NULL);
               if (!NT_SUCCESS(status))
               {
                    TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                                 "IoReportTargetDeviceChangeAsynchronous Failed! status = 0x%x\n", status);
               }
            }
            ExFreePoolWithTag(notification, VIOSERIAL_DRIVER_MEMORY_TAG);
        }
    }
    WdfObjectDelete(WorkItem);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VIOSerialEvtChildListIdentificationDescriptionDuplicate(
    WDFCHILDLIST DeviceList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER SourceIdentificationDescription,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER DestinationIdentificationDescription
    )
{
    PVIOSERIAL_PORT src, dst;

    UNREFERENCED_PARAMETER(DeviceList);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "--> %s\n", __FUNCTION__);

    src = CONTAINING_RECORD(SourceIdentificationDescription,
                            VIOSERIAL_PORT,
                            Header);
    dst = CONTAINING_RECORD(DestinationIdentificationDescription,
                            VIOSERIAL_PORT,
                            Header);

    dst->BusDevice = src->BusDevice;
    dst->Device = src->Device;

    dst->InBuf = src->InBuf;
    dst->InBufLock = src->InBufLock;
    dst->OutVqLock = src->OutVqLock;

    dst->NameString.Length = src->NameString.Length;
    dst->NameString.MaximumLength = src->NameString.MaximumLength;
    if (dst->NameString.Length)
    {
        dst->NameString.Buffer = (PCHAR)ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 dst->NameString.MaximumLength,
                                 VIOSERIAL_DRIVER_MEMORY_TAG
                                 );
        if (!dst->NameString.Buffer)
        {
           ASSERT(0);
           return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(dst->NameString.Buffer,
                                 src->NameString.Buffer,
                                 dst->NameString.MaximumLength
                                 );
    }
    dst->DeviceId = src->DeviceId;
    dst->PortId = src->PortId;

    dst->OutVqFull = src->OutVqFull;
    dst->HostConnected = src->HostConnected;
    dst->GuestConnected = src->GuestConnected;
    dst->Removed = src->Removed;

    dst->ReadQueue = src->ReadQueue;
    dst->PendingReadRequest = src->PendingReadRequest;
    dst->WriteQueue = src->WriteQueue;
    dst->IoctlQueue = src->IoctlQueue;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

BOOLEAN
VIOSerialEvtChildListIdentificationDescriptionCompare(
    WDFCHILDLIST DeviceList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER FirstIdentificationDescription,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER SecondIdentificationDescription
    )
{
    PVIOSERIAL_PORT lhs, rhs;

    UNREFERENCED_PARAMETER(DeviceList);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "--> %s\n", __FUNCTION__);

    lhs = CONTAINING_RECORD(FirstIdentificationDescription,
                            VIOSERIAL_PORT,
                            Header);
    rhs = CONTAINING_RECORD(SecondIdentificationDescription,
                            VIOSERIAL_PORT,
                            Header);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "<-- %s\n", __FUNCTION__);
    return ((lhs->PortId == rhs->PortId) && (lhs->DeviceId == rhs->DeviceId));
}

VOID
VIOSerialEvtChildListIdentificationDescriptionCleanup(
    WDFCHILDLIST DeviceList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription
    )
{
    PVIOSERIAL_PORT pDesc;

    UNREFERENCED_PARAMETER(DeviceList);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "--> %s\n", __FUNCTION__);

    pDesc = CONTAINING_RECORD(IdentificationDescription,
                              VIOSERIAL_PORT,
                              Header);

	// only for code analyzer; IdentificationDescription erroneously defined as "out"
	IdentificationDescription->IdentificationDescriptionSize = sizeof(*pDesc);

	if (pDesc->NameString.Buffer)
    {
       ExFreePoolWithTag(pDesc->NameString.Buffer, VIOSERIAL_DRIVER_MEMORY_TAG);
       pDesc->NameString.Buffer = NULL;
       pDesc->NameString.Length = 0;
       pDesc->NameString.MaximumLength = 0;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "<-- %s\n", __FUNCTION__);
}

VOID VIOSerialPortReadIoStop(IN WDFQUEUE Queue,
                             IN WDFREQUEST Request,
                             IN ULONG ActionFlags)
{
    PRAWPDO_VIOSERIAL_PORT pdoData = RawPdoSerialPortGetData(
        WdfIoQueueGetDevice(Queue));
    PVIOSERIAL_PORT pport = pdoData->port;

    TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "--> %s\n", __FUNCTION__);
    ASSERT(pport->PendingReadRequest == Request);

    WdfSpinLockAcquire(pport->InBufLock);
    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        WdfRequestStopAcknowledge(Request, FALSE);
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {
        if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
           pport->PendingReadRequest = NULL;
           WdfRequestCompleteWithInformation(Request , STATUS_CANCELLED, 0L);
        }
    }
    WdfSpinLockRelease(pport->InBufLock);
}

VOID VIOSerialPortWriteIoStop(IN WDFQUEUE Queue,
                              IN WDFREQUEST Request,
                              IN ULONG ActionFlags)
{
   PRAWPDO_VIOSERIAL_PORT pdoData = RawPdoSerialPortGetData(
      WdfIoQueueGetDevice(Queue));
   PVIOSERIAL_PORT pport = pdoData->port;

   TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "--> %s\n", __FUNCTION__);

   WdfSpinLockAcquire(pport->OutVqLock);
   if (ActionFlags & WdfRequestStopActionSuspend)
   {
      WdfRequestStopAcknowledge(Request, FALSE);
   }
   else if (ActionFlags & WdfRequestStopActionPurge)
   {
      if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
      {
         PSINGLE_LIST_ENTRY iter = &pport->WriteBuffersList;
         while ((iter = iter->Next) != NULL)
         {
            PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter, WRITE_BUFFER_ENTRY, ListEntry);
            if (entry->Request == Request)
            {
               entry->Request = NULL;
               break;
            }
         }
         WdfRequestComplete(Request, STATUS_OBJECT_NO_LONGER_EXISTS);
      }
   }
   WdfSpinLockRelease(pport->OutVqLock);

   TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

NTSTATUS VIOSerialPortEvtDeviceD0Entry(
    IN WDFDEVICE Device,
    IN WDF_POWER_DEVICE_STATE PreviousState)
{
    PVIOSERIAL_PORT port = RawPdoSerialPortGetData(Device)->port;
    PPORTS_DEVICE pCtx = GetPortsDevice(port->BusDevice);
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    PAGED_CODE();

    if ((pCtx->in_vqs == NULL) || (pCtx->in_vqs[port->PortId] == NULL))
    {
        return STATUS_NOT_FOUND;
    }

    status = VIOSerialFillQueue(GetInQueue(port), port->InBufLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
            "Error allocating input queue's buffers.\n");
        return status;
    }

    VIOSerialSendCtrlMsg(port->BusDevice, port->PortId,
        VIRTIO_CONSOLE_PORT_READY, 1);

    if (port->GuestConnected)
    {
        VIOSerialSendCtrlMsg(port->BusDevice, port->PortId,
            VIRTIO_CONSOLE_PORT_OPEN, 1);
    }

    port->Removed = FALSE;

    VIOSerialEnableInterruptQueue(GetInQueue(port));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);

    return status;
}

NTSTATUS
VIOSerialPortEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PVIOSERIAL_PORT Port = RawPdoSerialPortGetData(Device)->port;
    PPORT_BUFFER buf;
    PSINGLE_LIST_ENTRY iter;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s TargetState: %d\n",
        __FUNCTION__, TargetState);

    Port->Removed = TRUE;

    VIOSerialDisableInterruptQueue(GetInQueue(Port));

    WdfSpinLockAcquire(Port->InBufLock);
    VIOSerialDiscardPortDataLocked(Port);
    Port->InBuf = NULL;
    WdfSpinLockRelease(Port->InBufLock);

    VIOSerialReclaimConsumedBuffers(Port);

    while (buf = (PPORT_BUFFER)virtqueue_detach_unused_buf(GetInQueue(Port)))
    {
        VIOSerialFreeBuffer(buf);
    }

    iter = PopEntryList(&Port->WriteBuffersList);
    while (iter != NULL)
    {
        PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter,
            WRITE_BUFFER_ENTRY, ListEntry);

        ExFreePoolWithTag(entry->Buffer, VIOSERIAL_DRIVER_MEMORY_TAG);
        WdfObjectDelete(entry->EntryHandle);

        iter = PopEntryList(&Port->WriteBuffersList);
    };

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}
