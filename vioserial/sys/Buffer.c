#include "precomp.h"
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "Buffer.tmh"
#endif

// Number of descriptors that queue contains.
#define QUEUE_DESCRIPTORS 128

PPORT_BUFFER
VIOSerialAllocateBuffer(
    IN size_t buf_size
)
{
    PPORT_BUFFER buf;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    buf = ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 sizeof(PORT_BUFFER),
                                 VIOSERIAL_DRIVER_MEMORY_TAG
                                 );
    if (buf == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING, "ExAllocatePoolWithTag failed, %s::%d\n", __FUNCTION__, __LINE__);
        return NULL;
    }
    buf->va_buf = ExAllocatePoolWithTag(
                                 NonPagedPool,
                                 buf_size,
                                 VIOSERIAL_DRIVER_MEMORY_TAG
                                 );
    if(buf->va_buf == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING, "ExAllocatePoolWithTag failed, %s::%d\n", __FUNCTION__, __LINE__);
        ExFreePoolWithTag(buf, VIOSERIAL_DRIVER_MEMORY_TAG);
        return NULL;
    }
    buf->pa_buf = MmGetPhysicalAddress(buf->va_buf);
    buf->len = 0;
    buf->offset = 0;
    buf->size = buf_size;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
    return buf;
}

size_t VIOSerialSendBuffers(IN PVIOSERIAL_PORT Port,
                            IN PVOID Buffer,
                            IN size_t Length,
                            IN WDFREQUEST Request)
{
    struct virtqueue *vq = GetOutQueue(Port);
    struct VirtIOBufferDescriptor sg[QUEUE_DESCRIPTORS];
    PVOID buffer = Buffer;
    size_t length = Length;
    int out = 0;
    int ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE,
        "--> %s Buffer: %p Length: %d\n", __FUNCTION__, Buffer, Length);

    if (BYTES_TO_PAGES(Length) > QUEUE_DESCRIPTORS)
    {
        return 0;
    }

    while (length > 0)
    {
        sg[out].physAddr = MmGetPhysicalAddress(buffer);
        sg[out].ulSize = min(length, PAGE_SIZE);

        buffer = (PVOID)((LONG_PTR)buffer + sg[out].ulSize);
        length -= sg[out].ulSize;
        out += 1;
    }

    WdfSpinLockAcquire(Port->OutVqLock);

    ret = vq->vq_ops->add_buf(vq, sg, out, 0, Request, NULL, 0);
    vq->vq_ops->kick(vq);

    if (ret >= 0)
    {
        // Add a reference because the request is send to the virt queue.
        WdfObjectReference(Request);

        Port->OutVqFull = (ret == 0);
    }
    else
    {
        Length = 0;
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "Error adding buffer to queue (ret = %d)\n", ret);
    }

    WdfSpinLockRelease(Port->OutVqLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return Length;
}

VOID
VIOSerialFreeBuffer(
    IN PPORT_BUFFER buf
)
{
    ASSERT(buf);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s  buf = %p, buf->va_buf = %p\n", __FUNCTION__, buf, buf->va_buf);
    if (buf->va_buf)
    {
        ExFreePoolWithTag(buf->va_buf, VIOSERIAL_DRIVER_MEMORY_TAG);
        buf->va_buf = NULL;
    }
    ExFreePoolWithTag(buf, VIOSERIAL_DRIVER_MEMORY_TAG);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
}

VOID VIOSerialReclaimConsumedBuffers(IN PVIOSERIAL_PORT Port)
{
    WDFREQUEST request;
    UINT len;
    struct virtqueue *vq = GetOutQueue(Port);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    if (vq)
    {
        while ((request = (WDFREQUEST)vq->vq_ops->get_buf(vq, &len)) != NULL)
        {
            // Removed the reference after the request was pulled out from the
            // virt queue.
            WdfObjectDereference(request);

            WdfRequestCompleteWithInformation(request, STATUS_SUCCESS,
                GetWriteRequestContext(request)->Length);

            Port->OutVqFull = FALSE;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s Full: %d\n",
        __FUNCTION__, Port->OutVqFull);
}

// this procedure must be called with port InBuf spinlock held
SSIZE_T
VIOSerialFillReadBufLocked(
    IN PVIOSERIAL_PORT port,
    IN PVOID outbuf,
    IN SIZE_T count
)
{
    PPORT_BUFFER buf;
    NTSTATUS  status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    if (!count || !VIOSerialPortHasDataLocked(port))
        return 0;

    buf = port->InBuf;
    count = min(count, buf->len - buf->offset);

    RtlCopyMemory(outbuf, (PVOID)((LONG_PTR)buf->va_buf + buf->offset), count);

    buf->offset += count;

    if (buf->offset == buf->len)
    {
        port->InBuf = NULL;

        status = VIOSerialAddInBuf(GetInQueue(port), buf);
        if (!NT_SUCCESS(status))
        {
           TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING, "%s::%d  VIOSerialAddInBuf failed\n", __FUNCTION__, __LINE__);
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
    return count;
}


NTSTATUS
VIOSerialAddInBuf(
    IN struct virtqueue *vq,
    IN PPORT_BUFFER buf)
{
    NTSTATUS  status = STATUS_SUCCESS;
    struct VirtIOBufferDescriptor sg;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s  buf = %p\n", __FUNCTION__, buf);
    if (buf == NULL)
    {
        ASSERT(0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (vq == NULL)
    {
        ASSERT(0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    sg.physAddr = buf->pa_buf;
    sg.ulSize = buf->size;

    if(0 > vq->vq_ops->add_buf(vq, &sg, 0, 1, buf, NULL, 0))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING, "<-- %s cannot add_buf\n", __FUNCTION__);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    vq->vq_ops->kick(vq);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
    return status;
}


PVOID
VIOSerialGetInBuf(
    IN PVIOSERIAL_PORT port
)
{
    PPORT_BUFFER buf = NULL;
    struct virtqueue *vq = GetInQueue(port);
    UINT len;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    if (vq)
    {
        buf = vq->vq_ops->get_buf(vq, &len);
        if (buf)
        {
           buf->len = len;
           buf->offset = 0;
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
    return buf;
}
