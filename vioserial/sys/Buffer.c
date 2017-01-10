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
                            IN PWRITE_BUFFER_ENTRY Entry,
                            IN size_t Length)
{
    struct virtqueue *vq = GetOutQueue(Port);
    struct VirtIOBufferDescriptor sg[QUEUE_DESCRIPTORS];
    PVOID buffer = Entry->Buffer;
    size_t length = Length;
    int out = 0, prepared = 0;
    int ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE,
        "--> %s Buffer: %p Length: %d\n", __FUNCTION__, Entry->Buffer, Length);

    if (BYTES_TO_PAGES(Length) > QUEUE_DESCRIPTORS)
    {
        return 0;
    }

    while (length > 0)
    {
        sg[out].physAddr = MmGetPhysicalAddress(buffer);
        sg[out].length = min(length, PAGE_SIZE);

        buffer = (PVOID)((LONG_PTR)buffer + sg[out].length);
        length -= sg[out].length;
        out += 1;
    }

    WdfSpinLockAcquire(Port->OutVqLock);

    ret = virtqueue_add_buf(vq, sg, out, 0, Entry->Buffer, NULL, 0);

    if (ret >= 0)
    {
        prepared = virtqueue_kick_prepare(vq);
        PushEntryList(&Port->WriteBuffersList, &Entry->ListEntry);
    }
    else
    {
        Port->OutVqFull = TRUE;
        Length = 0;
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "Error adding buffer to queue (ret = %d)\n", ret);
    }

    WdfSpinLockRelease(Port->OutVqLock);

    if (prepared)
    {
        // notify can run without the lock held
        virtqueue_notify(vq);
    }

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

VOID VIOSerialProcessInputBuffers(IN PVIOSERIAL_PORT Port)
{
    NTSTATUS status;
    ULONG Read;
    WDFREQUEST Request = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

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
        status = WdfRequestUnmarkCancelable(Port->PendingReadRequest);
        if (status != STATUS_CANCELLED)
        {
            PVOID Buffer;
            size_t Length;

            status = WdfRequestRetrieveOutputBuffer(Port->PendingReadRequest, 0, &Buffer, &Length);
            if (NT_SUCCESS(status))
            {
                Request = Port->PendingReadRequest;
                Port->PendingReadRequest = NULL;
                Read = (ULONG)VIOSerialFillReadBufLocked(Port, Buffer, Length);
            }
            else
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING,
                    "Failed to retrieve output buffer (Status: %x Request: %p).\n",
                    status, Request);
            }
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_QUEUEING,
                "Request %p was cancelled.\n", Request);
        }
    }
    WdfSpinLockRelease(Port->InBufLock);

    if (Request != NULL)
    {
        // no need to have the lock when completing the request
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, Read);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
}

BOOLEAN VIOSerialReclaimConsumedBuffers(IN PVIOSERIAL_PORT Port)
{
    WDFREQUEST request;
    SINGLE_LIST_ENTRY ReclaimedList = { NULL };
    PSINGLE_LIST_ENTRY iter, last = &ReclaimedList;
    PVOID buffer;
    UINT len;
    struct virtqueue *vq = GetOutQueue(Port);
    BOOLEAN ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(Port->OutVqLock);

    if (vq)
    {
        while ((buffer = virtqueue_get_buf(vq, &len)) != NULL)
        {
            iter = &Port->WriteBuffersList;
            while (iter->Next != NULL)
            {
                PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter->Next,
                    WRITE_BUFFER_ENTRY, ListEntry);

                if (buffer == entry->Buffer)
                {
                    if (entry->Request != NULL)
                    {
                        request = entry->Request;
                        if (WdfRequestUnmarkCancelable(request) == STATUS_CANCELLED)
                        {
                            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_QUEUEING,
                                "Request %p was cancelled.\n", request);
                            entry->Request = NULL;
                        }
                    }

                    // remove from WriteBuffersList
                    iter->Next = entry->ListEntry.Next;

                    // append to ReclaimedList
                    last->Next = &entry->ListEntry;
                    last = last->Next;
                    last->Next = NULL;
                }
                else
                {
                    iter = iter->Next;
                }
            };

            Port->OutVqFull = FALSE;
        }
    }
    ret = Port->OutVqFull;

    WdfSpinLockRelease(Port->OutVqLock);

    // no need to hold the lock to complete requests and free buffers
    while ((iter = PopEntryList(&ReclaimedList)) != NULL)
    {
        PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter,
            WRITE_BUFFER_ENTRY, ListEntry);

        request = entry->Request;
        if (request != NULL)
        {
            WdfRequestCompleteWithInformation(request, STATUS_SUCCESS,
                WdfRequestGetInformation(request));
        }

        ExFreePoolWithTag(entry->Buffer, VIOSERIAL_DRIVER_MEMORY_TAG);
        WdfObjectDelete(entry->EntryHandle);
    };

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s Full: %d\n",
        __FUNCTION__, ret);

    return ret;
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
    sg.length = buf->size;

    if(0 > virtqueue_add_buf(vq, &sg, 0, 1, buf, NULL, 0))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING, "<-- %s cannot add_buf\n", __FUNCTION__);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    virtqueue_kick(vq);
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
        buf = virtqueue_get_buf(vq, &len);
        if (buf)
        {
           buf->len = len;
           buf->offset = 0;
        }
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
    return buf;
}
