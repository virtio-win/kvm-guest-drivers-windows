#include "precomp.h"
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "Port.tmh"
#endif

PPORT_BUFFER 
VIOSerialAllocateBuffer(
    IN size_t buf_size
)
{
    PPORT_BUFFER buf;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    buf = ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 sizeof(PORT_BUFFER),
                                 VIOSERIAL_DRIVER_MEMORY_TAG
                                 );
    if (buf == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"ExAllocatePoolWithTag failed, %s::%d\n", __FUNCTION__, __LINE__);
        return NULL;
    }
    buf->va_buf = ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 buf_size,
                                 VIOSERIAL_DRIVER_MEMORY_TAG
                                 );
    if(buf->va_buf == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"ExAllocatePoolWithTag failed, %s::%d\n", __FUNCTION__, __LINE__);
        ExFreePoolWithTag(buf, VIOSERIAL_DRIVER_MEMORY_TAG);
        return NULL;
    }
    buf->pa_buf = GetPhysicalAddress(buf->va_buf);
    buf->len = 0;
    buf->offset = 0;
    buf->size = buf_size;
    return buf; 
}

VOID 
VIOSerialFreeBuffer(
    IN PPORT_BUFFER buf
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    ExFreePoolWithTag(buf->va_buf, VIOSERIAL_DRIVER_MEMORY_TAG);
    ExFreePoolWithTag(buf, VIOSERIAL_DRIVER_MEMORY_TAG);
}

VOID 
VIOSerialReclaimConsumedBuffers(
    IN PVIOSERIAL_PORT port
)
{
    PVOID buf;
    UINT len;
    struct virtqueue *vq = port->out_vq;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    while (buf = vq->vq_ops->get_buf(vq, &len))
    {
        ExFreePoolWithTag(buf, VIOSERIAL_DRIVER_MEMORY_TAG);  
        port->OutVqFull = FALSE;
    }
}

SSIZE_T 
VIOSerialSendBuffers(
    IN PVIOSERIAL_PORT port,
    IN PVOID buf,
    IN SIZE_T count,
    IN BOOLEAN nonblock
)
{
    UINT len;
    SSIZE_T ret;
    struct VirtIOBufferDescriptor sg;
    struct virtqueue *vq = port->in_vq;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    VIOSerialReclaimConsumedBuffers(port);

    sg.physAddr = GetPhysicalAddress(buf);
    sg.ulSize = (unsigned long)count;

    ret = vq->vq_ops->add_buf(vq, &sg, 1, 0, buf);
    vq->vq_ops->kick(vq);
    if (ret < 0)
    {
        return count;
    }
    
    if (ret == 0)
    {
        port->OutVqFull = TRUE;
    }

    if (!nonblock)
    {
        while(!vq->vq_ops->get_buf(vq, &len))
        {
           KeStallExecutionProcessor(100);
        }   
    }
    return count;
}


SSIZE_T 
VIOSerialFillReadBuf(
    IN PVIOSERIAL_PORT port,
    IN PVOID outbuf,
    IN SIZE_T count
)
{
    PPORT_BUFFER buf;
    struct virtqueue *vq = port->out_vq;
    NTSTATUS  status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    if (!count || !VIOSerialPortHasData(port))
        return 0;

    buf = port->InBuf;
    count = min(count, buf->len - buf->offset);

    memcpy(outbuf, (PVOID)((LONG_PTR)buf->va_buf + buf->offset), count);

    buf->offset += count;

    if (buf->offset == buf->len) 
    {
        port->InBuf = NULL;

        status = VIOSerialAddInBuf(port->in_vq, buf);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"%s::%d  VIOSerialAddInBuf failed\n", __FUNCTION__, __LINE__);
        }
    }
    return count;
}


NTSTATUS 
VIOSerialAddInBuf(
    IN struct virtqueue *vq,
    IN PPORT_BUFFER buf)
{
    NTSTATUS  status = STATUS_SUCCESS;
    struct VirtIOBufferDescriptor sg;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    sg.physAddr = buf->pa_buf;
    sg.ulSize = buf->size;

    if(vq->vq_ops->add_buf(vq, &sg, 0, 1, buf) != 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> Cannot add buffer\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    vq->vq_ops->kick(vq);
    return status;
}


PVOID
VIOSerialGetInBuf(
    IN PVIOSERIAL_PORT port
)
{
    PPORT_BUFFER buf;
    struct virtqueue *vq = port->in_vq;
    UINT len;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    vq = port->in_vq;
    buf = vq->vq_ops->get_buf(vq, &len);
    if (buf) 
    {
        buf->len = len;
        buf->offset = 0;
    }
    return buf;
}
