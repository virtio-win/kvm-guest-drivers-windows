#include "precomp.h"
#include "vioser.h"
#include "public.h"

#if defined(EVENT_TRACING)
#include "Port.tmh"
#endif


#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VIOSerialDeviceListCreatePdo)
#pragma alloc_text(PAGE, VIOSerialPortRead)
#pragma alloc_text(PAGE, VIOSerialPortWrite)
#pragma alloc_text(PAGE, VIOSerialPortDeviceControl)
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

        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &port.Header);

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &port.Header,
                                 sizeof(port)
                                 );
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

        if(rawPdo && rawPdo->port->Id == id)
        {
            WdfChildListEndIteration(list, &iterator);
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s  id = %d port = 0x%p\n", __FUNCTION__, id, rawPdo->port);
            return rawPdo->port;
        }
    }
    WdfChildListEndIteration(list, &iterator);
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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s  port = %d\n", __FUNCTION__, id);

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &port.Header,
                                 sizeof(port)
                                 );

    port.Id = id;
    port.Name = NULL;
    port.InBuf = NULL;
    port.HostConnected = port.GuestConnected = FALSE;
    port.OutVqFull = FALSE;

    port.in_vq = pContext->in_vqs[port.Id];
    port.out_vq = pContext->out_vqs[port.Id];
    port.Device = Device;

    status = WdfChildListAddOrUpdateChildDescriptionAsPresent(
                                 WdfFdoGetDefaultChildList(Device), 
                                 &port.Header,
                                 NULL
                                 );

    if (status == STATUS_OBJECT_NAME_EXISTS) 
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
           "The description is already present in the list, the serial number is not unique.\n");
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
           "WdfChildListAddOrUpdateChildDescriptionAsPresent = 0x%x.\n", status);

}

VOID
VIOSerialRemovePort(
    IN WDFDEVICE Device,
    IN ULONG id
)
{
    PPORT_BUFFER    buf;
    PPORTS_DEVICE   pContext = GetPortsDevice(Device);
    NTSTATUS        status = STATUS_SUCCESS;
    WDFCHILDLIST    list;
    WDF_CHILD_LIST_ITERATOR     iterator;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s  port = %d\n", __FUNCTION__, id);

    list = WdfFdoGetDefaultChildList(Device);
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator,
                                 WdfRetrievePresentChildren );

    WdfChildListBeginIteration(list, &iterator);


    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO  childInfo;
        VIOSERIAL_PORT           port;
        WDFDEVICE                hChild;

        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &port.Header);

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
                                 &port.Header,
                                 sizeof(port)
                                 );
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

        if(port.Id == id)
        {
           status = WdfChildListUpdateChildDescriptionAsMissing(
                                 list,
                                 &port.Header
                                 );

           if (status == STATUS_NO_SUCH_DEVICE)
           {
              status = STATUS_INVALID_PARAMETER;
              break;
           }

           VIOSerialEnableDisableInterruptQueue(port.in_vq, FALSE);
           VIOSerialEnableDisableInterruptQueue(port.out_vq, FALSE);

           if(port.GuestConnected)
           {
              VIOSerialSendCtrlMsg(port.Device, port.Id, VIRTIO_CONSOLE_PORT_OPEN, 0);
           }

           VIOSerialDiscardPortData(&port);
           VIOSerialReclaimConsumedBuffers(&port);
           while (buf = VirtIODeviceDetachUnusedBuf(port.in_vq))
           {
              VIOSerialFreeBuffer(buf);
           }
           if (port.Name)
           {
              ExFreePoolWithTag(port.Name, VIOSERIAL_DRIVER_MEMORY_TAG);
              port.Name = NULL;
           }
        }
    }
    WdfChildListEndIteration(list, &iterator);

    if (status != STATUS_NO_MORE_ENTRIES)
    {
        ASSERT(0);
    }
}

VOID
VIOSerialInitPortConsole(
    IN PVIOSERIAL_PORT port
)
{
    PPORT_BUFFER    buf;
    PPORTS_DEVICE   pContext = GetPortsDevice(port->Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    port->GuestConnected = TRUE;
    VIOSerialSendCtrlMsg(port->Device, port->Id, VIRTIO_CONSOLE_PORT_OPEN, 1);
}

VOID
VIOSerialDiscardPortData(
    IN PVIOSERIAL_PORT port
)
{
    struct virtqueue *vq;
    PPORT_BUFFER buf;
    UINT len;
    PPORTS_DEVICE pContext = GetPortsDevice(port->Device);
    NTSTATUS  status = STATUS_SUCCESS;
    UINT ret = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    vq = port->in_vq;
    if (port->InBuf)
    {
        buf = port->InBuf;
    }
    else
    {
        buf = vq->vq_ops->get_buf(vq, &len);
    }

    while (buf)
    {
        status = VIOSerialAddInBuf(vq, buf); 
        if(!NT_SUCCESS(status))
        {
           ++ret;
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "%s::%d Error adding buffer to queue\n", __FUNCTION__, __LINE__);
           VIOSerialFreeBuffer(buf);  
        }
        buf = vq->vq_ops->get_buf(vq, &len);
    }
    port->InBuf = NULL;
    if (ret > 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "%s::%d Error adding %u buffers back to queue\n",
                      __FUNCTION__, __LINE__, ret);
    }
}

BOOLEAN
VIOSerialPortHasData(
    IN PVIOSERIAL_PORT port
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(port->InBufLock);
    if (port->InBuf) 
    {
        WdfSpinLockRelease(port->InBufLock);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return TRUE;
    }
    port->InBuf = VIOSerialGetInBuf(port);
    if (port->InBuf) 
    {
        WdfSpinLockRelease(port->InBufLock);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return TRUE;
    }
    WdfSpinLockRelease(port->InBufLock);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
    return FALSE;
}

BOOLEAN
VIOSerialWillReadBlock(
    IN PVIOSERIAL_PORT port
)
{
    return !VIOSerialPortHasData(port) && port->HostConnected;
}

BOOLEAN
VIOSerialWillWriteBlock(
    IN PVIOSERIAL_PORT port
)
{
    BOOLEAN ret = FALSE;

    if (!port->HostConnected)
    {
        return TRUE;
    }

    WdfSpinLockAcquire(port->OutVqLock);
    VIOSerialReclaimConsumedBuffers(port);
    ret = port->OutVqFull;
    WdfSpinLockRelease(port->OutVqLock);
    return ret;
}


NTSTATUS
VIOSerialDeviceListCreatePdo(
    IN WDFCHILDLIST DeviceList,
    IN PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    IN PWDFDEVICE_INIT ChildInit
    )
{
    PVIOSERIAL_PORT pport;
    NTSTATUS  status = STATUS_SUCCESS;

    WDFDEVICE                       hChild = NULL;

    WDF_OBJECT_ATTRIBUTES           attributes;
    WDF_DEVICE_PNP_CAPABILITIES     pnpCaps;
    WDF_DEVICE_STATE                deviceState;
    WDF_IO_QUEUE_CONFIG             ioQueueConfig;
    WDFQUEUE                        queue;
    PRAWPDO_VIOSERIAL_PORT          rawPdo = NULL;
    WDF_FILEOBJECT_CONFIG           fileConfig;

    DECLARE_CONST_UNICODE_STRING(deviceId, PORT_DEVICE_ID );
    DECLARE_CONST_UNICODE_STRING(deviceLocation, L"RedHat VIOSerial Port" );
    DECLARE_UNICODE_STRING_SIZE(HwId,   DEVICE_DESC_LENGTH);
    DECLARE_UNICODE_STRING_SIZE(buffer, DEVICE_DESC_LENGTH);


    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    pport = CONTAINING_RECORD(IdentificationDescription,
                                 VIOSERIAL_PORT,
                                 Header
                                 );

    status = WdfPdoInitAssignRawDevice(ChildInit, &GUID_DEVCLASS_PORT_DEVICE);
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAssignRawDevice failed - 0x%x\n", status);
        return status;
    }

    status = WdfDeviceInitAssignSDDLString(ChildInit, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceInitAssignSDDLString failed - 0x%x\n", status);
        return status;
    }

    status = WdfPdoInitAssignDeviceID(ChildInit, &deviceId);
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAssignDeviceID failed - 0x%x\n", status);
        return status;
    }

    status = RtlUnicodeStringPrintf(
                                 &HwId, 
                                 L"%04d", 
                                 pport->Id
                                 );
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "RtlUnicodeStringPrintf failed - 0x%x\n", status);
        return status;
    }

    status = WdfPdoInitAddHardwareID(ChildInit, &HwId);
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAddHardwareID failed - 0x%x\n", status);
        return status;
    }

    status = RtlUnicodeStringPrintf(
                                 &buffer, 
                                 L"%02d", 
                                 pport->Id
                                 );
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "RtlUnicodeStringPrintf failed - 0x%x\n", status);
        return status;
    }

    status = WdfPdoInitAssignInstanceID(ChildInit, &buffer);
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfPdoInitAssignInstanceID failed - 0x%x\n", status);
        return status;
    }

    status = RtlUnicodeStringPrintf(
                                 &buffer,
                                 L"VIOSerial Port %02d",
                                 pport->Id
                                 );
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "RtlUnicodeStringPrintf failed 0x%x\n", status);
        return status;
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
        return status;
    }

    WdfPdoInitSetDefaultLocale(ChildInit, 0x409);

    WdfDeviceInitSetIoType(ChildInit, WdfDeviceIoBuffered);

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

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, RAWPDO_VIOSERIAL_PORT);
    status = WdfDeviceCreate(
                                &ChildInit, 
                                &attributes,
                                &hChild
                                );
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "WdfDeviceCreate failed 0x%x\n", status);
        return status;
    }

    rawPdo = RawPdoSerialPortGetData(hChild);

    rawPdo->port = pport;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
                                 &ioQueueConfig,
                                 WdfIoQueueDispatchSequential);

    ioQueueConfig.EvtIoRead   =  VIOSerialPortRead;
    ioQueueConfig.EvtIoWrite  =  VIOSerialPortWrite;
    ioQueueConfig.EvtIoDeviceControl = VIOSerialPortDeviceControl;

    status = WdfIoQueueCreate(
                                 hChild,
                                 &ioQueueConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &queue
                                 );
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed 0x%x\n", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig,
                             WdfIoQueueDispatchManual);

    status = WdfIoQueueCreate(hChild,
                              &ioQueueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &pport->ReadQueue
                             );
    if (!NT_SUCCESS(status)) 
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed 0x%x\n", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig,
                             WdfIoQueueDispatchManual);

    status = WdfIoQueueCreate(hChild,
                              &ioQueueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &pport->WriteQueue
                             );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed 0x%x\n", status);
        return status;
    }

    WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);

    pnpCaps.NoDisplayInUI     =  WdfTrue;
    pnpCaps.Address           =  pport->Id;
    pnpCaps.UINumber          =  pport->Id;

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
        return status;
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
        return status;
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
        return status;
    }

    status = VIOSerialFillQueue(pport->in_vq, pport->InBufLock);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"%s::%d  Error allocating inbufs\n", __FUNCTION__, __LINE__);
        return status;
    }


    VIOSerialEnableDisableInterruptQueue(pport->in_vq, TRUE);
    VIOSerialEnableDisableInterruptQueue(pport->out_vq, TRUE);

    VIOSerialSendCtrlMsg(pport->Device, pport->Id, VIRTIO_CONSOLE_PORT_READY, 1);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s\n", __FUNCTION__);
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
    SIZE_T             length;
    NTSTATUS           status;
    PUCHAR             systemBuffer;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "-->%s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, Length, &systemBuffer, &length);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    if (!VIOSerialPortHasData(pdoData->port))
    {
        if (!pdoData->port->HostConnected)
        {
           WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
           return;
        }

        status = WdfRequestForwardToIoQueue(Request, pdoData->port->ReadQueue);
        if (NT_SUCCESS(status)) 
        {
            return;
        } 
        else 
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestForwardToIoQueue failed: %x\n", status);
           WdfRequestComplete(Request, status);
           return;
        }
    }

    length = (ULONG)VIOSerialFillReadBuf(pdoData->port, systemBuffer, length);
    if (length == Length)
    {
        WdfRequestCompleteWithInformation(Request, status, (ULONG_PTR)length);
        return;
    }
    WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
    return;
}

VOID
VIOSerialPortWrite(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     Length
    )
{

    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfIoQueueGetDevice(Queue));
    NTSTATUS           status = STATUS_SUCCESS;
    SIZE_T             length;
    WDFREQUEST         readRequest;
    PUCHAR             systemBuffer;
    BOOLEAN            nonBlock;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "-->%s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, Length, &systemBuffer, &length);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return;
    }

    nonBlock = ((WdfFileObjectGetFlags(WdfRequestGetFileObject(Request)) & FO_SYNCHRONOUS_IO) != FO_SYNCHRONOUS_IO);
    if (VIOSerialWillWriteBlock(pdoData->port))
    {
        if (nonBlock)
        {
           WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
           TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<--%s::%d\n", __FUNCTION__, __LINE__);
           return;
        }

        status = WdfRequestForwardToIoQueue(Request, pdoData->port->WriteQueue);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfRequestForwardToIoQueue failed: %x\n", status);
           WdfRequestComplete(Request, status);
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return;
    }

    Length = min((32 * 1024), Length);
    length = VIOSerialSendBuffers(pdoData->port, systemBuffer, length, FALSE);

    if (length == Length)
    {
        WdfRequestCompleteWithInformation(Request, status, (ULONG_PTR)length);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return;
    }
    ASSERT(0);
    WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "-->%s\n", __FUNCTION__);

    switch (IoControlCode)
    {

        case IOCTL_GET_INFORMATION:
        {
           status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VIRTIO_PORT_INFO), &pport_info, &length);
           if (!NT_SUCCESS(status))
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                            "WdfRequestRetrieveInputBuffer failed 0x%x\n", status);
              WdfRequestComplete(Request, status);
              return;
           }
           if (pdoData->port->Name)
           {
              status = RtlStringCbLengthA(pdoData->port->Name,NTSTRSAFE_MAX_CCH * sizeof(char),&name_size);
              if (!NT_SUCCESS(status))
              {
                 TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                            "RtlStringCbLengthA failed 0x%x\n", status);
                 name_size = 0;
              }
              else
              {
                 name_size += sizeof(char);
              }
           }
           if (length < sizeof (VIRTIO_PORT_INFO) + name_size)
           {
              status = STATUS_BUFFER_OVERFLOW;
              TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                            "Buffer too small. get = %d, expected = %d\n", length, sizeof (VIRTIO_PORT_INFO) + name_size);
              length = sizeof (VIRTIO_PORT_INFO) + name_size;
              break;
           }
           RtlZeroMemory(pport_info, sizeof(VIRTIO_PORT_INFO));
           pport_info->Id = pdoData->port->Id;
           pport_info->OutVqFull = pdoData->port->OutVqFull;
           pport_info->HostConnected = pdoData->port->HostConnected;
           pport_info->GuestConnected = pdoData->port->GuestConnected;

           if (pdoData->port->Name && name_size > 0 )
           {
              RtlZeroMemory(pport_info->Name, name_size);
              status = RtlStringCbCopyA(pport_info->Name, name_size, pdoData->port->Name);   
              if (!NT_SUCCESS(status))
              {
                 TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                            "RtlStringCbCopyA failed 0x%x\n", status);
                 name_size = 0;
              }
           }
           status = STATUS_SUCCESS;
           length =  sizeof (VIRTIO_PORT_INFO) + name_size;
           break;
        }

        default:
           status = STATUS_INVALID_DEVICE_REQUEST;
           break;
    }
    WdfRequestCompleteWithInformation(Request, status, length);
}

VOID
VIOSerialPortCreate (
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfDevice);
    NTSTATUS                status  = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(FileObject);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,"%s Port id = %d\n", __FUNCTION__, pdoData->port->Id);

    WdfSpinLockAcquire(pdoData->port->InBufLock);
    if (pdoData->port->GuestConnected == TRUE)
    {
        WdfSpinLockRelease(pdoData->port->InBufLock);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,"Guest already connected Port id = %d\n", pdoData->port->Id);
        status = STATUS_OBJECT_NAME_EXISTS;
    }
    else
    {
        pdoData->port->GuestConnected = TRUE;
        WdfSpinLockRelease(pdoData->port->InBufLock);

        WdfSpinLockAcquire(pdoData->port->OutVqLock);
        VIOSerialReclaimConsumedBuffers(pdoData->port);
        WdfSpinLockRelease(pdoData->port->OutVqLock);

        VIOSerialSendCtrlMsg(pdoData->port->Device, pdoData->port->Id, VIRTIO_CONSOLE_PORT_OPEN, 1);
    }
    WdfRequestComplete(Request, status);

    return;
}

VOID
VIOSerialPortClose (
    IN WDFFILEOBJECT    FileObject
    )
{
    PRAWPDO_VIOSERIAL_PORT  pdoData = RawPdoSerialPortGetData(WdfFileObjectGetDevice(FileObject));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "%s\n", __FUNCTION__);

    VIOSerialSendCtrlMsg(pdoData->port->Device, pdoData->port->Id, VIRTIO_CONSOLE_PORT_OPEN, 0);

    WdfSpinLockAcquire(pdoData->port->InBufLock);
    pdoData->port->GuestConnected = FALSE;
    VIOSerialDiscardPortData(pdoData->port);
    WdfSpinLockRelease(pdoData->port->InBufLock);

    WdfSpinLockAcquire(pdoData->port->OutVqLock);
    VIOSerialReclaimConsumedBuffers(pdoData->port);
    WdfSpinLockRelease(pdoData->port->OutVqLock);

    return;

}
