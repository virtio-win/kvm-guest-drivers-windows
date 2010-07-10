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

    ASSERT(pContext->isDeviceInitialized);

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

    for (i = 0; i < pContext->consoleConfig.max_nr_ports; ++i)
    {
        VIOSerialCtrlWorkHandler(WdfInterruptGetDevice(Interrupt)); 
        port = VIOSerialFindPortById(Device, i);
        if (port)
        {

           WdfSpinLockAcquire(port->InBufLock);
           if (!port->InBuf)
           {
              port->InBuf = VIOSerialGetInBuf(port);
           }
           if (!port->GuestConnected)
           {
              VIOSerialDiscardPortData(port);
           }
           WdfSpinLockRelease(port->InBufLock);

           if (!VIOSerialWillReadBlock(port))
           {
              status = WdfIoQueueRetrieveNextRequest(port->ReadQueue, &request);
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

           if (!VIOSerialWillWriteBlock(port))
           {
              status = WdfIoQueueRetrieveNextRequest(port->WriteQueue, &request);
              if (NT_SUCCESS(status))
              {
                 WDF_REQUEST_PARAMETERS  params;
                 ULONG                   length = 0;
                 WDF_REQUEST_PARAMETERS_INIT(&params);

                 TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,"Got available write request\n");

                 VIOSerialReclaimConsumedBuffers(port);
                 WdfRequestGetParameters(
                                         request,
                                         &params
                                        );
                 length = params.Parameters.Write.Length;

                 length = min((32 * 1024), length);
                 length = VIOSerialSendBuffers(port, systemBuffer, length, FALSE);

                 if (length == params.Parameters.Write.Length)
                 {
                    WdfRequestCompleteWithInformation(request, status, (ULONG_PTR)length);
                    return;
                 }

                 WdfRequestComplete(request, STATUS_INSUFFICIENT_RESOURCES);
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);

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
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
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
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
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
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);

    if(!vq)
        return;

    vq->vq_ops->enable_interrupt(vq, bEnable);
    if(bEnable)
    {
        vq->vq_ops->kick(vq);
    }
}
