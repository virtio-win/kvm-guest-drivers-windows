#include "precomp.h"
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "Buffer.tmh"
#endif

// Number of descriptors that queue contains.
#define QUEUE_DESCRIPTORS 128

static BOOLEAN DmaWriteCallback(PVIRTIO_DMA_TRANSACTION_PARAMS params);

/* OutVqLock protects the write's position and its single in-flight batch. */
static BOOLEAN VIOSerialQueueNextWriteChunkLocked(PVIOSERIAL_PORT Port, PWRITE_BUFFER_ENTRY Entry, BOOLEAN *Notify)
{
    struct virtqueue *vq = GetOutQueue(Port);
    ULONG remaining = Entry->NumElements - Entry->NextElement;
    ULONG count = min(remaining, min(QUEUE_DESCRIPTORS, virtio_get_queue_size(vq)));
    SIZE_T bytes = 0;
    ULONG i;
    int ret;

    *Notify = FALSE;
    if (!count)
    {
        return FALSE;
    }
    ret = virtqueue_add_buf(vq, Entry->Sg + Entry->NextElement, count, 0, Entry, NULL, 0);
    if (ret < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "Error adding buffer to queue (ret = %d)\n", ret);
        return FALSE;
    }

    for (i = 0; i < count; ++i)
    {
        bytes += Entry->Sg[Entry->NextElement + i].length;
    }
    Entry->NextElement += count;
    Entry->InFlightBytes = bytes;
    *Notify = virtqueue_kick_prepare(vq);
    return TRUE;
}

PPORT_BUFFER
VIOSerialAllocateSinglePageBuffer(IN VirtIODevice *vdev, IN ULONG id)
{
    PPORT_BUFFER buf;
    ULONG buf_size = PAGE_SIZE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    buf = ExAllocatePoolUninitialized(NonPagedPool, sizeof(PORT_BUFFER), VIOSERIAL_DRIVER_MEMORY_TAG);
    if (buf == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_QUEUEING,
                    "ExAllocatePoolUninitialized failed, %s::%d\n",
                    __FUNCTION__,
                    __LINE__);
        return NULL;
    }
    buf->va_buf = VirtIOWdfDeviceAllocDmaMemory(vdev, buf_size, id);
    if (buf->va_buf == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_QUEUEING,
                    "VirtIOWdfDeviceAllocDmaMemory failed, %s::%d\n",
                    __FUNCTION__,
                    __LINE__);
        ExFreePoolWithTag(buf, VIOSERIAL_DRIVER_MEMORY_TAG);
        return NULL;
    }
    buf->pa_buf = VirtIOWdfDeviceGetPhysicalAddress(vdev, buf->va_buf);
    if (!buf->pa_buf.QuadPart)
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_QUEUEING,
                    "VirtIOWdfDeviceGetPhysicalAddress failed, %s::%d\n",
                    __FUNCTION__,
                    __LINE__);
        VirtIOWdfDeviceFreeDmaMemory(vdev, buf->va_buf);
        ExFreePoolWithTag(buf, VIOSERIAL_DRIVER_MEMORY_TAG);
        return NULL;
    }

    buf->len = 0;
    buf->offset = 0;
    buf->size = buf_size;
    buf->vdev = vdev;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
    return buf;
}

size_t VIOSerialSendBuffers(IN PVIOSERIAL_PORT Port, IN PWRITE_BUFFER_ENTRY Entry)
{
    struct virtqueue *vq = GetOutQueue(Port);
    VIRTIO_DMA_TRANSACTION_PARAMS params;
    RtlZeroMemory(&params, sizeof(params));
    params.allocationTag = VIOSERIAL_DRIVER_MEMORY_TAG;
    params.param1 = Port;
    params.param2 = Entry;
    params.buffer = Entry->OriginalWriteBuffer;
    params.size = (ULONG)Entry->OriginalWriteBufferSize;
    return !!VirtIOWdfDeviceDmaTxAsync(vq->vdev, &params, DmaWriteCallback);
}

BOOLEAN
DmaWriteCallback(PVIRTIO_DMA_TRANSACTION_PARAMS params)
{
    PVIOSERIAL_PORT Port = params->param1;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    PWRITE_BUFFER_ENTRY Entry = params->param2;
    struct virtqueue *vq = GetOutQueue(Port);
    ULONG i = 0;
    SIZE_T allocationSize;
    BOOLEAN notify = FALSE;
    WDFREQUEST request;

    if (!params->sgList || !params->sgList->NumberOfElements ||
        !NT_SUCCESS(RtlSizeTMult(params->sgList->NumberOfElements, sizeof(*Entry->Sg), &allocationSize)))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "%s sgList problem\n", __FUNCTION__);
        goto error;
    }

    Entry->Sg = ExAllocatePoolUninitialized(NonPagedPool, allocationSize, VIOSERIAL_DRIVER_MEMORY_TAG);
    if (!Entry->Sg)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "%s failed to allocate sg array\n", __FUNCTION__);
        goto error;
    }
    Entry->NumElements = params->sgList->NumberOfElements;
    for (i = 0; i < Entry->NumElements; ++i)
    {
        Entry->Sg[i].physAddr = params->sgList->Elements[i].Address;
        Entry->Sg[i].length = params->sgList->Elements[i].Length;
    }

    WdfSpinLockAcquire(Port->OutVqLock);
    if (Port->Removed)
    {
        WdfSpinLockRelease(Port->OutVqLock);
        goto error;
    }

    if (!VIOSerialQueueNextWriteChunkLocked(Port, Entry, &notify))
    {
        Port->OutVqFull = TRUE;
        WdfSpinLockRelease(Port->OutVqLock);
        goto error;
    }
    Port->OutVqFull = FALSE;
    Entry->dmaTransaction = params->transaction;
    PushEntryList(&Port->WriteBuffersList, &Entry->ListEntry);
    WdfSpinLockRelease(Port->OutVqLock);

    if (notify)
    {
        // notify can run without the lock held
        virtqueue_notify(vq);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s ok\n", __FUNCTION__);
    return TRUE;

error:
    WdfSpinLockAcquire(Port->OutVqLock);
    if (Port->CurrentWrite == Entry)
    {
        Port->CurrentWrite = NULL;
    }
    request = Entry->Request;
    Entry->Request = NULL;
    status = Port->Removed           ? STATUS_INVALID_DEVICE_STATE
             : Entry->StopAfterBatch ? STATUS_CANCELLED
                                     : STATUS_INSUFFICIENT_RESOURCES;
    WdfSpinLockRelease(Port->OutVqLock);
    if (Entry->Sg)
    {
        ExFreePoolWithTag(Entry->Sg, VIOSERIAL_DRIVER_MEMORY_TAG);
    }
    VirtIOWdfDeviceDmaTxComplete(vq->vdev, params->transaction);
    if (request)
    {
        WdfRequestCompleteWithInformation(request, status, 0);
    }

    WdfObjectDelete(Entry->EntryHandle);
    TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "<-- %s error\n", __FUNCTION__);
    return FALSE;
}

VOID VIOSerialFreeBuffer(IN PPORT_BUFFER buf)
{
    ASSERT(buf);
    TraceEvents(TRACE_LEVEL_VERBOSE,
                DBG_QUEUEING,
                "--> %s  buf = %p, buf->va_buf = %p\n",
                __FUNCTION__,
                buf,
                buf->va_buf);
    if (buf->va_buf)
    {
        VirtIOWdfDeviceFreeDmaMemory(buf->vdev, buf->va_buf);
        buf->va_buf = NULL;
    }
    ExFreePoolWithTag(buf, VIOSERIAL_DRIVER_MEMORY_TAG);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
}

VOID VIOSerialProcessInputBuffers(IN PVIOSERIAL_PORT Port)
{
    NTSTATUS status;
    ULONG Read = 0;
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
                TraceEvents(TRACE_LEVEL_ERROR,
                            DBG_QUEUEING,
                            "Failed to retrieve output buffer (Status: %x Request: %p).\n",
                            status,
                            Request);
            }
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_QUEUEING, "Request %p was cancelled.\n", Request);
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
    SINGLE_LIST_ENTRY ReclaimedList = {NULL};
    PSINGLE_LIST_ENTRY iter, last = &ReclaimedList;
    PVOID buffer;
    UINT len;
    struct virtqueue *vq = GetOutQueue(Port);
    BOOLEAN ret;
    BOOLEAN notify = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(Port->OutVqLock);

    if (vq)
    {
        while ((buffer = virtqueue_get_buf(vq, &len)) != NULL)
        {
            Port->OutVqFull = FALSE;
            iter = &Port->WriteBuffersList;
            while (iter->Next != NULL)
            {
                PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter->Next, WRITE_BUFFER_ENTRY, ListEntry);

                if (buffer == entry)
                {
                    BOOLEAN kick = FALSE;
                    entry->CompletedBytes += entry->InFlightBytes;
                    entry->InFlightBytes = 0;

                    if (!Port->Removed && !entry->StopRequested && !entry->StopAfterBatch &&
                        entry->NextElement < entry->NumElements &&
                        VIOSerialQueueNextWriteChunkLocked(Port, entry, &kick))
                    {
                        notify |= kick;
                    }
                    else
                    {
                        if (!Port->Removed && !entry->StopRequested && !entry->StopAfterBatch &&
                            entry->NextElement < entry->NumElements)
                        {
                            Port->OutVqFull = TRUE;
                        }
                        if (entry->NextElement == entry->NumElements)
                        {
                            entry->CompletionStatus = STATUS_SUCCESS;
                        }
                        else if (Port->Removed)
                        {
                            entry->CompletionStatus = STATUS_INVALID_DEVICE_STATE;
                        }
                        else if (entry->StopRequested || entry->StopAfterBatch)
                        {
                            entry->CompletionStatus = STATUS_CANCELLED;
                        }
                        else
                        {
                            entry->CompletionStatus = STATUS_INSUFFICIENT_RESOURCES;
                        }
                        if (Port->CurrentWrite == entry)
                        {
                            Port->CurrentWrite = NULL;
                        }
                        iter->Next = entry->ListEntry.Next;
                        last->Next = &entry->ListEntry;
                        last = last->Next;
                        last->Next = NULL;
                    }
                    break;
                }
                else
                {
                    iter = iter->Next;
                }
            };
        }
    }
    ret = Port->OutVqFull;

    WdfSpinLockRelease(Port->OutVqLock);

    if (notify)
    {
        virtqueue_notify(vq);
    }

    // no need to hold the lock to complete requests and free buffers
    while ((iter = PopEntryList(&ReclaimedList)) != NULL)
    {
        PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter, WRITE_BUFFER_ENTRY, ListEntry);
        WDFREQUEST request = entry->Request;

        if (vq != NULL && entry->dmaTransaction)
        {
            VirtIOWdfDeviceDmaTxComplete(vq->vdev, entry->dmaTransaction);
        }
        ExFreePoolWithTag(entry->Sg, VIOSERIAL_DRIVER_MEMORY_TAG);
        if (request != NULL)
        {
            WdfRequestCompleteWithInformation(request, entry->CompletionStatus, entry->CompletedBytes);
        }
        WdfObjectDelete(entry->EntryHandle);
    };

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s Full: %d\n", __FUNCTION__, ret);

    return ret;
}

// this procedure must be called with port InBuf spinlock held
SSIZE_T
VIOSerialFillReadBufLocked(IN PVIOSERIAL_PORT port, IN PVOID outbuf, IN SIZE_T count)
{
    PPORT_BUFFER buf;
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    if (!count || !VIOSerialPortHasDataLocked(port))
    {
        return 0;
    }

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
VIOSerialAddInBuf(IN struct virtqueue *vq, IN PPORT_BUFFER buf)
{
    NTSTATUS status = STATUS_SUCCESS;
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

    if (0 > virtqueue_add_buf(vq, &sg, 0, 1, buf, NULL, 0))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING, "<-- %s cannot add_buf\n", __FUNCTION__);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    virtqueue_kick(vq);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
    return status;
}

PVOID
VIOSerialGetInBuf(IN PVIOSERIAL_PORT port)
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
