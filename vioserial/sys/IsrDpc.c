#include "precomp.h"
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "IsrDpc.tmh"
#endif

EVT_WDF_WORKITEM WriteRequestReSubmitWorkItem;

typedef struct _RESUBMIT_REQUEST_CONTEXT {
    PVIOSERIAL_PORT Port;
    WDFREQUEST Request;
} RESUBMIT_REQUEST_CONTEXT, *PRESUBMIT_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RESUBMIT_REQUEST_CONTEXT,
                                   GetReSubmitRequestContext)

VOID ReSubmitWriteRequest(IN VIOSERIAL_PORT *Port, IN WDFREQUEST Request)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG config;
    WDFWORKITEM workitem;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,
        RESUBMIT_REQUEST_CONTEXT);
    attributes.ParentObject = Port->Device;

    WDF_WORKITEM_CONFIG_INIT(&config, WriteRequestReSubmitWorkItem);

    status = WdfWorkItemCreate(&config, &attributes, &workitem);
    if (NT_SUCCESS(status))
    {
        RESUBMIT_REQUEST_CONTEXT *context;
        context = GetReSubmitRequestContext(workitem);
        context->Port = Port;
        context->Request = Request;

        WdfWorkItemEnqueue(workitem);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_DPC,
            "Failed to create write request work item (0x%08x)\n", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

VOID WriteRequestReSubmitWorkItem(IN WDFWORKITEM WorkItem)
{
    RESUBMIT_REQUEST_CONTEXT *context;
    PVIOSERIAL_PORT port;
    WDFREQUEST request;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    context = GetReSubmitRequestContext(WorkItem);
    port = context->Port;
    request = context->Request;

    if (request)
    {
        status = WdfRequestUnmarkCancelable(request);
        if (status != STATUS_CANCELLED)
        {
            VIOSerialPortWrite(WdfRequestGetIoQueue(request), request,
                GetWriteRequestContext(request)->Length);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,
                "Request %p was cancelled.\n", request);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

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
    WDFREQUEST Request;
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
            Request = Port->PendingReadRequest;
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

        Request = NULL;
        WdfSpinLockAcquire(Port->OutVqLock);
        if (Port->PendingWriteRequest)
        {
            Request = Port->PendingWriteRequest;
            Port->PendingWriteRequest = NULL;
        }
        WdfSpinLockRelease(Port->OutVqLock);

        if (Request)
        {
            ReSubmitWriteRequest(Port, Request);
        }
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
        pContext->c_ivq->vq_ops->enable_interrupt(pContext->c_ivq);
        pContext->c_ivq->vq_ops->kick(pContext->c_ivq);
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
        pContext->c_ivq->vq_ops->disable_interrupt(pContext->c_ivq);
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

    vq->vq_ops->enable_interrupt(vq);
    vq->vq_ops->kick(vq);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialDisableInterruptQueue(IN struct virtqueue *vq)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s disable\n", __FUNCTION__);

    if(!vq)
        return;

    vq->vq_ops->disable_interrupt(vq);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
}

