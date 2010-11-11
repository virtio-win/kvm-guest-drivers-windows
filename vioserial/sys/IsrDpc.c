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
    ULONG          ret;
    PPORTS_DEVICE  pContext = GetPortsDevice(WdfInterruptGetDevice(Interrupt));

    if((ret = VirtIODeviceISR(&pContext->IODevice)) > 0)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "Got ISR - it is ours %d!\n", ret);
        WdfInterruptQueueDpcForIsr(Interrupt);
        return TRUE;
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "WRONG ISR!\n");
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
    WDFDMATRANSACTION dmaTransaction;


    ULONG            information;
    NTSTATUS         status;
    PUCHAR           systemBuffer;
    size_t           Length;
    WDFREQUEST       request;
    BOOLEAN          nonBlock;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    if(!Interrupt)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_DPC, "Got NULL interrupt object DPC!\n");
        return;
    }
    Device = WdfInterruptGetDevice(Interrupt);
    pContext = GetPortsDevice(Device);

    VIOSerialCtrlWorkHandler(Device);
    for (i = 0; i < pContext->consoleConfig.max_nr_ports; ++i)
    {
        port = VIOSerialFindPortById(Device, i);
        if (port)
        {
           struct virtqueue    *out_vq = GetOutQueue(port);
           WdfSpinLockAcquire(port->InBufLock);
           if (!port->InBuf)
           {
              port->InBuf = VIOSerialGetInBuf(port);
              TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "%s::%d  port->InBuf = %p\n", __FUNCTION__, __LINE__, port->InBuf);
           }
           if (!port->GuestConnected)
           {
              VIOSerialDiscardPortData(port);
           }
           WdfSpinLockRelease(port->InBufLock);

           if (port->InBuf)
           {
              status = WdfIoQueueRetrieveNextRequest(port->PendingReadQueue, &request);
              if (NT_SUCCESS(status))
              {
                 TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,"Got available read request\n");
                 status = WdfRequestRetrieveOutputBuffer(request, 0, &systemBuffer, &Length);
                 if (NT_SUCCESS(status))
                 {
                    information = (ULONG)VIOSerialFillReadBuf(port, systemBuffer, Length);
                    WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, information);
                 }
              }
           }
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

static 
VOID 
VIOSerialEnableDisableInterrupt(
    PPORTS_DEVICE pContext,
    IN BOOLEAN bEnable)
{
    unsigned int i;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s enable = %d\n", __FUNCTION__, bEnable);

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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s enable = %d\n", __FUNCTION__, bEnable);
}


NTSTATUS 
VIOSerialInterruptEnable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
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
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
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
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s enable = %d\n", __FUNCTION__, bEnable);

    if(!vq)
        return;

    vq->vq_ops->enable_interrupt(vq, bEnable);
    if(bEnable)
    {
        vq->vq_ops->kick(vq);
    }
}
