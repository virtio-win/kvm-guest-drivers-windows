#include "precomp.h"
#include "vioser.h"
#include "public.h"

#if defined(EVENT_TRACING)
#include "Control.tmh"
#endif

static
VOID
VIOSerialHandleCtrlMsg(
    IN WDFDEVICE Device,
    IN PPORT_BUFFER buf
);

VOID
VIOSerialSendCtrlMsg(
    IN WDFDEVICE Device,
    IN ULONG id,
    IN USHORT event,
    IN USHORT value
)
{
    struct VirtIOBufferDescriptor sg;
    struct virtqueue *vq;
    UINT len;
    PPORTS_DEVICE pContext = GetPortsDevice(Device);
    VIRTIO_CONSOLE_CONTROL *cpkt;

    if (!pContext->isHostMultiport || !pContext->ControlDmaBlock)
    {
        return;
    }

    vq = pContext->c_ovq;

    cpkt = pContext->ControlDmaBlock->get_slice(pContext->ControlDmaBlock, &sg.physAddr);
    if (!cpkt) {

        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "<-- %s can't get a slice\n", __FUNCTION__);
        return;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s vq = %p, &cmd %p\n", __FUNCTION__, vq, cpkt);

    cpkt->id = id;
    cpkt->event = event;
    cpkt->value = value;

    sg.length = sizeof(*cpkt);

    WdfWaitLockAcquire(pContext->COutVqLock, NULL);
    if(0 <= virtqueue_add_buf(vq, &sg, 1, 0, &cpkt, NULL, 0))
    {
        virtqueue_kick(vq);
        while(!virtqueue_get_buf(vq, &len))
        {
            LARGE_INTEGER interval;
            interval.QuadPart = -1;
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
        }
    }
    WdfWaitLockRelease(pContext->COutVqLock);

    pContext->ControlDmaBlock->return_slice(pContext->ControlDmaBlock, cpkt);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSerialCtrlWorkHandler(
    IN WDFDEVICE Device
)
{
    struct virtqueue *vq;
    PPORT_BUFFER buf;
    UINT len;
    NTSTATUS  status = STATUS_SUCCESS;
    PPORTS_DEVICE pContext = GetPortsDevice(Device);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    vq = pContext->c_ivq;
    ASSERT(vq);

    WdfSpinLockAcquire(pContext->CInVqLock);
    while ((buf = virtqueue_get_buf(vq, &len)))
    {
        WdfSpinLockRelease(pContext->CInVqLock);
        buf->len = len;
        buf->offset = 0;
        VIOSerialHandleCtrlMsg(Device, buf);

        WdfSpinLockAcquire(pContext->CInVqLock);
        status = VIOSerialAddInBuf(vq, buf);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "%s::%d Error adding buffer to queue\n", __FUNCTION__, __LINE__);
           VIOSerialFreeBuffer(buf);
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    WdfSpinLockRelease(pContext->CInVqLock);
}

VOID
VIOSerialHandleCtrlMsg(
    IN WDFDEVICE Device,
    IN PPORT_BUFFER buf
)
{
    PPORTS_DEVICE pContext = GetPortsDevice(Device);
    PVIRTIO_CONSOLE_CONTROL cpkt;
    PVIOSERIAL_PORT port;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    cpkt = (PVIRTIO_CONSOLE_CONTROL)((ULONG_PTR)buf->va_buf + buf->offset);

    port = VIOSerialFindPortById(Device, cpkt->id);

    if (!port && (cpkt->event != VIRTIO_CONSOLE_PORT_ADD))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Invalid index %u in control packet\n", cpkt->id);
    }

    switch (cpkt->event)
    {
        case VIRTIO_CONSOLE_PORT_ADD:
           if (port)
           {
               TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "VIRTIO_CONSOLE_PORT_ADD id = %d\n", cpkt->id);
               break;
           }
           if (cpkt->id >= pContext->consoleConfig.max_nr_ports)
           {
               TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "Out-of-bound id %u\n", cpkt->id);
               break;
           }
           VIOSerialAddPort(Device, cpkt->id);
        break;

        case VIRTIO_CONSOLE_PORT_REMOVE:
           if (!port)
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "VIRTIO_CONSOLE_PORT_REMOVE invalid id = %d\n", cpkt->id);
              break;
           }
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "VIRTIO_CONSOLE_PORT_REMOVE id = %d\n", cpkt->id);
           VIOSerialRemovePort(Device, port);
        break;

        case VIRTIO_CONSOLE_CONSOLE_PORT:
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                       "VIRTIO_CONSOLE_CONSOLE_PORT id = %d value = %u\n", cpkt->id, cpkt->value);
           if (cpkt->value)
           {
              VIOSerialInitPortConsole(Device,port);
           }
        break;

        case VIRTIO_CONSOLE_RESIZE:
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "VIRTIO_CONSOLE_RESIZE id = %d\n", cpkt->id);
        break;

        case VIRTIO_CONSOLE_PORT_OPEN:
           if (port)
           {
              BOOLEAN  Connected = (BOOLEAN)cpkt->value;
              TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "VIRTIO_CONSOLE_PORT_OPEN id = %d, HostConnected = %d\n", cpkt->id, Connected);
              if (port->HostConnected != Connected)
              {
                 VIOSerialPortPnpNotify(Device, port, Connected);
              }

              // Someone is listening. Trigger a check to see if we have
              // something waiting to be told.
              if (port->HostConnected)
              {
                  WDF_INTERRUPT_INFO info;
                  WDFINTERRUPT *interrupt;

                  WDF_INTERRUPT_INFO_INIT(&info);
                  WdfInterruptGetInfo(pContext->QueuesInterrupt, &info);

                  // Check if MSI is enabled and notify the right interrupt.
                  interrupt = (info.Vector == 0) ? &pContext->WdfInterrupt :
                      &pContext->QueuesInterrupt;

                  WdfInterruptQueueDpcForIsr(*interrupt);
              }
           }
           else
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "VIRTIO_CONSOLE_PORT_OPEN invalid id = %d\n", cpkt->id);
           }
        break;

        case VIRTIO_CONSOLE_PORT_NAME:
           if (port)
           {
              VIOSerialPortCreateName(Device, port, buf);
           }
        break;
        default:
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s UNKNOWN event = %d\n", __FUNCTION__, cpkt->event);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}
