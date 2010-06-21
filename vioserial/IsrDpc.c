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
    ULONG  ret;
    PPORTS_DEVICE	pContext = GetPortsDevice(WdfInterruptGetDevice(Interrupt));

    ASSERT(pContext->isDeviceInitialized);

    if((ret = VirtIODeviceISR(&pContext->IODevice)) > 0)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Got ISR - it is ours %d!\n", ret);
        WdfInterruptQueueDpcForIsr(Interrupt);
        return TRUE;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "WRONG ISR!\n");
    return FALSE;
}

VOID 
VIOSerialInterruptDpc(
    IN WDFINTERRUPT Interrupt,
    IN WDFOBJECT AssociatedObject)
{
    UINT             len, i;
    PVOID            buf;
    PPORTS_DEVICE    pContext;
    PVIOSERIAL_PORT  port;
    WDFDEVICE        Device;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    if(!Interrupt)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "Got NULL interrupt object DPC!\n");
        return;
    }
    Device = WdfInterruptGetDevice(Interrupt);
    pContext = GetPortsDevice(Device);

    for (i = 0; i < pContext->consoleConfig.max_nr_ports; ++i)
    {
        port = VIOSerialFindPortById(Device, i);
        if (i != 1 && port)
        {
           if (port->InBuf == NULL)
           {
              port->InBuf = VIOSerialGetInBuf(port);
           }
           if (!port->GuestConnected)
           {
              VIOSerialDiscardPortData(port);
           }
        }
        else if (i == 1)
        {
           VIOSerialCtrlWorkHandler(WdfInterruptGetDevice(Interrupt)); 
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}



static 
VOID 
VIOSerialEnableDisableInterrupt(
    PPORTS_DEVICE pContext,
    IN BOOLEAN bEnable)
{
    unsigned int i;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--> %s\n", __FUNCTION__);

    if(!pContext)
        return;

    if(pContext->c_ivq)
    {
        pContext->c_ivq->vq_ops->enable_interrupt(pContext->c_ivq, bEnable);
        if(bEnable)
        {
           pContext->c_ivq->vq_ops->kick(pContext->c_ivq);
        }
    }

    if(pContext->c_ovq)
    {
        pContext->c_ovq->vq_ops->enable_interrupt(pContext->c_ovq, bEnable);
        if(bEnable)
        {
           pContext->c_ovq->vq_ops->kick(pContext->c_ovq);
        }
    }
}


NTSTATUS 
VIOSerialInterruptEnable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--> %s\n", __FUNCTION__);
    VIOSerialEnableDisableInterrupt(
                                 GetPortsDevice(WdfInterruptGetDevice(Interrupt)), 
                                 TRUE);

    return STATUS_SUCCESS;
}

NTSTATUS 
VIOSerialInterruptDisable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--> %s\n", __FUNCTION__);
    VIOSerialEnableDisableInterrupt(
                                 GetPortsDevice(WdfInterruptGetDevice(Interrupt)),
                                 FALSE);
    return STATUS_SUCCESS;
}

VOID 
VIOSerialEnableDisableInterruptQueue(
    IN struct virtqueue *vq,
    IN BOOLEAN bEnable)
{
    unsigned int i;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--> %s\n", __FUNCTION__);

    if(!vq)
        return;

    vq->vq_ops->enable_interrupt(vq, bEnable);
    if(bEnable)
    {
        vq->vq_ops->kick(vq);
    }
}
