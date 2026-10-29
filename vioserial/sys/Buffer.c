#include "precomp.h"
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "Buffer.tmh"
#endif

static BOOLEAN DmaWriteCallback(PVIRTIO_DMA_TRANSACTION_PARAMS params);

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
    ULONG total, offset;
    int prepared = 0, ret;
    PWRITE_DMA_CHUNK firstChunk = NULL;

    if (!params->sgList || params->sgList->NumberOfElements == 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "%s sgList problem, NumberOfElements = 0\n", __FUNCTION__);
        goto error;
    }

    total = params->sgList->NumberOfElements;
    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_WRITE,
                "%s NumberOfElements = %lu (max %u per chunk)\n",
                __FUNCTION__,
                total,
                QUEUE_DESCRIPTORS);

    /* Initialise entry state — only valid on the first (and only) ProgramDma call. */
    Entry->dmaTransaction = params->transaction;
    Entry->PendingChunks = 0;
    Entry->Failed = FALSE;
    Entry->PendingBatches.Next = NULL;
    Entry->SubmittedBytes = 0;

    /*
     * Pre-allocate all WRITE_DMA_CHUNK objects and cache their SG data now,
     * while params->sgList is valid.  Only the first chunk goes into the VQ
     * immediately; the rest are queued on Entry->PendingBatches and submitted
     * one-at-a-time from VIOSerialReclaimConsumedBuffers as the VQ drains.
     * This avoids virtqueue overflow when total > QUEUE_DESCRIPTORS.
     */
    for (offset = 0; offset < total; offset += QUEUE_DESCRIPTORS)
    {
        ULONG n = min(QUEUE_DESCRIPTORS, total - offset);
        PWRITE_DMA_CHUNK chunk;
        ULONG i;

        chunk = ExAllocatePoolUninitialized(NonPagedPool, sizeof(*chunk), VIOSERIAL_DRIVER_MEMORY_TAG);
        if (chunk == NULL)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "Failed to allocate WRITE_DMA_CHUNK\n");
            Entry->Failed = TRUE;
            break;
        }
        chunk->Parent = Entry;
        chunk->NumElements = n;
        for (i = 0; i < n; ++i)
        {
            chunk->sg[i].physAddr = params->sgList->Elements[offset + i].Address;
            chunk->sg[i].length = params->sgList->Elements[offset + i].Length;
        }

        /* Track total work before any reclaim can observe it. */
        Entry->PendingChunks++;

        if (firstChunk == NULL)
        {
            firstChunk = chunk; /* submit this one to VQ below */
        }
        else
        {
            /* Append to pending list (LIFO — reversed at reclaim time is fine
             * because we submit them in order via PendingBatches). */
            chunk->PendingEntry.Next = Entry->PendingBatches.Next;
            Entry->PendingBatches.Next = &chunk->PendingEntry;
        }
    }

    if (firstChunk == NULL || Entry->Failed)
    {
        if (firstChunk != NULL)
        {
            ExFreePoolWithTag(firstChunk, VIOSERIAL_DRIVER_MEMORY_TAG);
            Entry->PendingChunks--;
            firstChunk = NULL;
        }
        goto error;
    }

    /* Reverse the PendingBatches list so batches are submitted in SG order. */
    {
        PSINGLE_LIST_ENTRY prev = NULL, cur = Entry->PendingBatches.Next, next;
        while (cur != NULL)
        {
            next = cur->Next;
            cur->Next = prev;
            prev = cur;
            cur = next;
        }
        Entry->PendingBatches.Next = prev;
    }

    WdfSpinLockAcquire(Port->OutVqLock);

    ret = virtqueue_add_buf(vq, firstChunk->sg, firstChunk->NumElements, 0, firstChunk, NULL, 0);
    if (ret < 0)
    {
        WdfSpinLockRelease(Port->OutVqLock);
        Port->OutVqFull = TRUE;
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "Error adding first buffer to queue (ret = %d)\n", ret);
        /* Free first chunk; pending list freed in error path below. */
        ExFreePoolWithTag(firstChunk, VIOSERIAL_DRIVER_MEMORY_TAG);
        Entry->PendingChunks--; /* undo the count for firstChunk */
        Entry->Failed = TRUE;
        firstChunk = NULL;
        goto error;
    }

    PushEntryList(&Port->WriteBuffersList, &firstChunk->ListEntry);
    {
        ULONG i;
        for (i = 0; i < firstChunk->NumElements; ++i)
        {
            Entry->SubmittedBytes += (ULONG)firstChunk->sg[i].length;
        }
    }
    prepared = virtqueue_kick_prepare(vq);

    WdfSpinLockRelease(Port->OutVqLock);

    if (prepared)
    {
        virtqueue_notify(vq);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE,
                DBG_WRITE,
                "<-- %s ok, first chunk submitted, %ld total pending%s\n",
                __FUNCTION__,
                Entry->PendingChunks,
                Entry->Failed ? " (partial alloc)" : "");
    return TRUE;

error:
    /* Free any pre-allocated pending batches that were not submitted. */
    {
        PSINGLE_LIST_ENTRY iter = Entry->PendingBatches.Next;
        while (iter != NULL)
        {
            PWRITE_DMA_CHUNK c = CONTAINING_RECORD(iter, WRITE_DMA_CHUNK, PendingEntry);
            iter = iter->Next;
            ExFreePoolWithTag(c, VIOSERIAL_DRIVER_MEMORY_TAG);
            Entry->PendingChunks--;
        }
        Entry->PendingBatches.Next = NULL;
    }
    Entry->dmaTransaction = NULL;
    VirtIOWdfDeviceDmaTxCompleteFinal(vq->vdev, params->transaction);
    WdfRequestComplete(Entry->Request, status);
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
    WDFREQUEST request;
    SINGLE_LIST_ENTRY ReclaimedList = {NULL};
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
                PWRITE_DMA_CHUNK chunk = CONTAINING_RECORD(iter->Next, WRITE_DMA_CHUNK, ListEntry);

                if (buffer == chunk)
                {
                    iter->Next = chunk->ListEntry.Next;
                    last->Next = &chunk->ListEntry;
                    last = last->Next;
                    last->Next = NULL;
                    break;
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

    while ((iter = PopEntryList(&ReclaimedList)) != NULL)
    {
        PWRITE_DMA_CHUNK chunk = CONTAINING_RECORD(iter, WRITE_DMA_CHUNK, ListEntry);
        PWRITE_BUFFER_ENTRY entry = chunk->Parent;
        BOOLEAN submitNext = FALSE;
        PWRITE_DMA_CHUNK nextChunk = NULL;

        ExFreePoolWithTag(chunk, VIOSERIAL_DRIVER_MEMORY_TAG);

        /*
         * If more pre-allocated batches are waiting, submit the next one to the
         * VQ now that the previous has been drained.  PendingChunks stays the
         * same (one batch left VQ, one enters), so the IRP is not completed yet.
         */
        if (entry->PendingBatches.Next != NULL && !entry->Failed && vq != NULL && !Port->Removed)
        {
            PSINGLE_LIST_ENTRY pe = PopEntryList(&entry->PendingBatches);
            nextChunk = CONTAINING_RECORD(pe, WRITE_DMA_CHUNK, PendingEntry);
        }

        if (nextChunk != NULL)
        {
            int nextRet;
            WdfSpinLockAcquire(Port->OutVqLock);
            nextRet = virtqueue_add_buf(vq, nextChunk->sg, nextChunk->NumElements, 0, nextChunk, NULL, 0);
            if (nextRet >= 0)
            {
                ULONG i;
                PushEntryList(&Port->WriteBuffersList, &nextChunk->ListEntry);
                for (i = 0; i < nextChunk->NumElements; ++i)
                {
                    entry->SubmittedBytes += (ULONG)nextChunk->sg[i].length;
                }
                submitNext = (virtqueue_kick_prepare(vq) != 0);
                /* PendingChunks unchanged: one out, one in. */
            }
            else
            {
                Port->OutVqFull = TRUE;
                entry->Failed = TRUE;
                TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "Error submitting next batch to queue (ret = %d)\n", nextRet);
                ExFreePoolWithTag(nextChunk, VIOSERIAL_DRIVER_MEMORY_TAG);
                /* Free remaining pending batches. */
                while (entry->PendingBatches.Next != NULL)
                {
                    PSINGLE_LIST_ENTRY pe2 = PopEntryList(&entry->PendingBatches);
                    PWRITE_DMA_CHUNK c2 = CONTAINING_RECORD(pe2, WRITE_DMA_CHUNK, PendingEntry);
                    ExFreePoolWithTag(c2, VIOSERIAL_DRIVER_MEMORY_TAG);
                    InterlockedDecrement(&entry->PendingChunks);
                }
                InterlockedDecrement(&entry->PendingChunks); /* for nextChunk itself */
                nextChunk = NULL;
            }
            WdfSpinLockRelease(Port->OutVqLock);

            if (submitNext)
            {
                virtqueue_notify(vq);
            }
        }

        /* Decrement for this reclaimed batch regardless of whether another was queued.
         * If a next batch was submitted it contributes its own PendingChunks count;
         * this decrement only accounts for the batch that just left the VQ. */
        if (InterlockedDecrement(&entry->PendingChunks) != 0)
        {
            continue;
        }

        if (vq != NULL && entry->dmaTransaction)
        {
            VirtIOWdfDeviceDmaTxCompleteFinal(vq->vdev, entry->dmaTransaction);
            entry->dmaTransaction = NULL;
        }

        WdfSpinLockAcquire(Port->OutVqLock);
        request = entry->Request;
        entry->Request = NULL;
        WdfSpinLockRelease(Port->OutVqLock);

        if (request != NULL)
        {
            if (entry->Failed)
            {
                WdfRequestCompleteWithInformation(request, STATUS_INSUFFICIENT_RESOURCES, entry->SubmittedBytes);
            }
            else
            {
                WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, WdfRequestGetInformation(request));
            }
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
