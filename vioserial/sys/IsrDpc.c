#include "precomp.h"
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "IsrDpc.tmh"
#endif

BOOLEAN
VIOSerialInterruptIsr(IN WDFINTERRUPT Interrupt, IN ULONG MessageID)
{
    PPORTS_DEVICE pContext = GetPortsDevice(WdfInterruptGetDevice(Interrupt));
    WDF_INTERRUPT_INFO info;
    BOOLEAN serviced;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(Interrupt, &info);

    // Schedule a DPC if the device is using message-signaled interrupts, or
    // if the device ISR status is enabled.
    if (info.MessageSignaled || VirtIOWdfGetISRStatus(&pContext->VDevice))
    {
        WdfInterruptQueueDpcForIsr(Interrupt);
        serviced = TRUE;
    }
    else
    {
        serviced = FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-- %s Serviced: %d\n", __FUNCTION__, serviced);

    return serviced;
}

// Process the input and output queues of every port.
static VOID VIOSerialServicePorts(IN WDFDEVICE BusDevice)
{
    NTSTATUS status;
    WDFCHILDLIST PortList;
    WDFDEVICE Device;
    WDF_CHILD_LIST_ITERATOR iterator;
    WDF_CHILD_RETRIEVE_INFO PortInfo;
    VIOSERIAL_PORT VirtPort;
    VIOSERIAL_PORT *Port;

    PortList = WdfFdoGetDefaultChildList(BusDevice);
    WDF_CHILD_LIST_ITERATOR_INIT(&iterator, WdfRetrievePresentChildren);

    WdfChildListBeginIteration(PortList, &iterator);
    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO_INIT(&PortInfo, &VirtPort.Header);
        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&VirtPort.Header, sizeof(VirtPort));

        status = WdfChildListRetrieveNextDevice(PortList, &iterator, &Device, &PortInfo);

        if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }

        Port = RawPdoSerialPortGetData(Device)->port;

        // handle the read queue
        VIOSerialProcessInputBuffers(Port);

        // handle the write queue
        VIOSerialReclaimConsumedBuffers(Port);
    }
    WdfChildListEndIteration(PortList, &iterator);
}

// A completion can arrive on either of our messages, not only on the one we
// assigned to its queue. So both DPCs look at the port queues, and both make
// sure that the control DPC, which handles control messages in order, runs
// when the control receive queue has work.
VOID VIOSerialInterruptDpc(IN WDFINTERRUPT Interrupt, IN WDFOBJECT AssociatedObject)
{
    WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);

    UNREFERENCED_PARAMETER(AssociatedObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    VIOSerialCtrlWorkHandler(Device);
    VIOSerialServicePorts(Device);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

VOID VIOSerialQueuesInterruptDpc(IN WDFINTERRUPT Interrupt, IN WDFOBJECT AssociatedObject)
{
    WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
    PPORTS_DEVICE Context = GetPortsDevice(Device);
    BOOLEAN control_work = FALSE;

    UNREFERENCED_PARAMETER(AssociatedObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    VIOSerialServicePorts(Device);

    if (Context->c_ivq)
    {
        WdfSpinLockAcquire(Context->CInVqLock);
        control_work = virtqueue_has_buf(Context->c_ivq);
        WdfSpinLockRelease(Context->CInVqLock);
    }

    if (control_work)
    {
        WdfInterruptQueueDpcForIsr(Context->WdfInterrupt);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

static VOID VIOSerialEnableInterrupt(PPORTS_DEVICE pContext)
{

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s enable\n", __FUNCTION__);

    if (!pContext)
    {
        return;
    }

    if (pContext->c_ivq)
    {
        virtqueue_enable_cb(pContext->c_ivq);
        virtqueue_kick(pContext->c_ivq);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s enable\n", __FUNCTION__);
}

static VOID VIOSerialDisableInterrupt(PPORTS_DEVICE pContext)
{

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s disable\n", __FUNCTION__);

    if (!pContext)
    {
        return;
    }

    if (pContext->c_ivq)
    {
        virtqueue_disable_cb(pContext->c_ivq);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s disable\n", __FUNCTION__);
}

NTSTATUS
VIOSerialInterruptEnable(IN WDFINTERRUPT Interrupt, IN WDFDEVICE AssociatedDevice)
{
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    VIOSerialEnableInterrupt(GetPortsDevice(WdfInterruptGetDevice(Interrupt)));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
VIOSerialInterruptDisable(IN WDFINTERRUPT Interrupt, IN WDFDEVICE AssociatedDevice)
{
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    VIOSerialDisableInterrupt(GetPortsDevice(WdfInterruptGetDevice(Interrupt)));
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID VIOSerialEnableInterruptQueue(IN struct virtqueue *vq)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s enable\n", __FUNCTION__);

    if (!vq)
    {
        return;
    }

    virtqueue_enable_cb(vq);
    virtqueue_kick(vq);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
}

VOID VIOSerialDisableInterruptQueue(IN struct virtqueue *vq)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s disable\n", __FUNCTION__);

    if (!vq)
    {
        return;
    }

    virtqueue_disable_cb(vq);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
}
