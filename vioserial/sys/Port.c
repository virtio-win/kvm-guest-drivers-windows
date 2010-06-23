#include "precomp.h"
#include "vioser.h"
#include "public.h"

#if defined(EVENT_TRACING)
#include "Port.tmh"
#endif


#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VIOSerialDeviceListCreatePdo)
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


    for (;;) {
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
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"Port was not found id = %d\n", id);
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

    status = VIOSerialFillQueue(port.in_vq);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"%s::%d  Error allocating inbufs\n", __FUNCTION__, __LINE__);
        return;
    }

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



    VIOSerialEnableDisableInterruptQueue(port.in_vq, TRUE);
    VIOSerialEnableDisableInterruptQueue(port.out_vq, TRUE);

    if (!pContext->isHostMultiport) 
    {
        ASSERT(0);
    }

    VIOSerialSendCtrlMsg(Device, port.Id, VIRTIO_CONSOLE_PORT_READY, 1);
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


    for (;;) {
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

    if (status != STATUS_NO_MORE_ENTRIES) {
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
                    ret,  __FUNCTION__, __LINE__);
    }
}

BOOLEAN
VIOSerialPortHasData(
    IN PVIOSERIAL_PORT port
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    if (port->InBuf) 
    {
        return TRUE;
    }
    port->InBuf = VIOSerialGetInBuf(port);
    if (port->InBuf) 
    {
        return TRUE;
    }
    return FALSE;
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

    WDF_OBJECT_ATTRIBUTES           pdoAttributes;
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

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&pdoAttributes, RAWPDO_VIOSERIAL_PORT);
    status = WdfDeviceCreate(
                                &ChildInit, 
                                &pdoAttributes, 
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

//    WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig,
//                             WdfIoQueueDispatchManual);
//
//    status = WdfIoQueueCreate(hChild,
//                              &ioQueueConfig,
//                              WDF_NO_OBJECT_ATTRIBUTES,
//                              &pdoData->ReadQueue
//                             );
//    if (!NT_SUCCESS(status)) 
//    {
//        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed 0x%x\n", status);
//        return status;
//    }

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


    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--VmchannelEvtDeviceListCreatePdo\n");
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
    ULONG              information;
    NTSTATUS           status;
    PUCHAR             systemBuffer;
    size_t             bufLen;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "-->%s\n", __FUNCTION__);
    status = WdfRequestRetrieveOutputBuffer(Request, Length, &systemBuffer, &bufLen);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    if (VIOSerialPortHasData(pdoData->port) && !pdoData->port->HostConnected)
    {
        information = 0;
    }
    else
    {
        information = (ULONG)VIOSerialFillReadBuf(pdoData->port, systemBuffer, Length);
    }
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, information);
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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "-->%s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, Length, &systemBuffer, &length);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    length = VIOSerialSendBuffers(pdoData->port, systemBuffer, Length, FALSE);

    WdfRequestCompleteWithInformation(Request, status, (ULONG_PTR)length);
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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"%s Device = %p\n", __FUNCTION__, WdfDevice);

    PAGED_CODE ();

    if (pdoData->port->GuestConnected == TRUE)
    {
        status = STATUS_OBJECT_NAME_EXISTS;
    }
    else
    {
        pdoData->port->GuestConnected = TRUE;
        VIOSerialReclaimConsumedBuffers(pdoData->port);
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

    PAGED_CODE ();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s\n", __FUNCTION__);

    VIOSerialSendCtrlMsg(pdoData->port->Device, pdoData->port->Id, VIRTIO_CONSOLE_PORT_OPEN, 0);
    pdoData->port->GuestConnected = FALSE;

    VIOSerialDiscardPortData(pdoData->port);
    VIOSerialReclaimConsumedBuffers(pdoData->port);

    return;

}
