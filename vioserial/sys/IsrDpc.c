#include "precomp.h"
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "IsrDpc.tmh"
#endif

BOOLEAN
VIOSerialInterruptIsr(
    IN WDFINTERRUPT Interrupt,
    IN ULONG MessageID)
{
    PPORTS_DEVICE pContext = GetPortsDevice(WdfInterruptGetDevice(Interrupt));
    BOOLEAN serviced;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);

    // Schedule a DPC if the device is using message-signaled interrupts, or
    // if the device ISR status is enabled.
    if (MessageID || VirtIODeviceISR(pContext->pIODevice))
    {
        WdfInterruptQueueDpcForIsr(Interrupt);
        serviced = TRUE;
    }
    else
    {
        serviced = FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT,
        "<-- %s Serviced: %d\n", __FUNCTION__, serviced);

    return serviced;
}

VOID
VIOSerialInterruptDpc(
    IN WDFINTERRUPT Interrupt,
    IN WDFOBJECT AssociatedObject)
{
    WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
    PPORTS_DEVICE Context = GetPortsDevice(Device);
    WDF_INTERRUPT_INFO info;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    VIOSerialCtrlWorkHandler(Device);

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(Context->QueuesInterrupt, &info);

    // Using the queues' DPC if only one interrupt is availble.
    if (info.Vector == 0)
    {
        VIOSerialQueuesInterruptDpc(Interrupt, AssociatedObject);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

VOID VIOSerialQueuesInterruptDpc(IN WDFINTERRUPT Interrupt,
                                 IN WDFOBJECT AssociatedObject)
{
    NTSTATUS status;
    WDFCHILDLIST PortList;
    WDFDEVICE Device;
    WDF_CHILD_LIST_ITERATOR iterator;
    WDF_CHILD_RETRIEVE_INFO PortInfo;
    VIOSERIAL_PORT VirtPort;
    VIOSERIAL_PORT *Port;

    UNREFERENCED_PARAMETER(AssociatedObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    PortList = WdfFdoGetDefaultChildList(WdfInterruptGetDevice(Interrupt));
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator, WdfRetrievePresentChildren);

    WdfChildListBeginIteration(PortList, &iterator);
    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO_INIT(&PortInfo, &VirtPort.Header);
        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&VirtPort.Header,
            sizeof(VirtPort));

        status = WdfChildListRetrieveNextDevice(PortList, &iterator, &Device,
            &PortInfo);

        if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }

        Port = RawPdoSerialPortGetData(Device)->port;

        WdfSpinLockAcquire(Port->InBufLock);
        if (!Port->InBuf)
        {
            Port->InBuf = (PPORT_BUFFER)VIOSerialGetInBuf(Port);
        }

        if (!Port->GuestConnected)
        {
            VIOSerialDiscardPortDataLocked(Port);
        }

        if (Port->InBuf && Port->PendingReadRequest)
        {
            WDFREQUEST Request = Port->PendingReadRequest;
            status = WdfRequestUnmarkCancelable(Request);
            if (status != STATUS_CANCELLED)
            {
                PVOID Buffer;
                size_t Length;

                status = WdfRequestRetrieveOutputBuffer(Request, 0, &Buffer, &Length);
                if (NT_SUCCESS(status))
                {
                    ULONG Read;

                    Port->PendingReadRequest = NULL;
                    Read = (ULONG)VIOSerialFillReadBufLocked(Port, Buffer, Length);
                    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                        Read);
                }
                else
                {
                    TraceEvents(TRACE_LEVEL_ERROR, DBG_DPC,
                        "Failed to retrieve output buffer (Status: %x Request: %p).\n",
                        status, Request);
                }
            }
            else
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,
                    "Request %p was cancelled.\n", Request);
            }
        }
        WdfSpinLockRelease(Port->InBufLock);

        WdfSpinLockAcquire(Port->OutVqLock);
        VIOSerialReclaimConsumedBuffers(Port);
        WdfSpinLockRelease(Port->OutVqLock);
    }
    WdfChildListEndIteration(PortList, &iterator);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

static
VOID
VIOSerialEnableInterrupt(PPORTS_DEVICE pContext)
{

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s enable\n", __FUNCTION__);

    if(!pContext)
        return;

    if(pContext->c_ivq)
    {
        virtqueue_enable_cb(pContext->c_ivq);
        virtqueue_kick(pContext->c_ivq);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s enable\n", __FUNCTION__);
}

static
VOID
VIOSerialDisableInterrupt(PPORTS_DEVICE pContext)
{

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s disable\n", __FUNCTION__);

    if(!pContext)
        return;

    if(pContext->c_ivq)
    {
        virtqueue_disable_cb(pContext->c_ivq);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s disable\n", __FUNCTION__);
}


NTSTATUS
VIOSerialInterruptEnable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    VIOSerialEnableInterrupt(GetPortsDevice(WdfInterruptGetDevice(Interrupt)));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
VIOSerialInterruptDisable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    VIOSerialDisableInterrupt(GetPortsDevice(WdfInterruptGetDevice(Interrupt)));
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID
VIOSerialEnableInterruptQueue(IN struct virtqueue *vq)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s enable\n", __FUNCTION__);

    if(!vq)
        return;

    virtqueue_enable_cb(vq);
    virtqueue_kick(vq);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialDisableInterruptQueue(IN struct virtqueue *vq)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s disable\n", __FUNCTION__);

    if(!vq)
        return;

    virtqueue_disable_cb(vq);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
}

