/*
 * Copyright (C) 2014-2017 Red Hat, Inc.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "viorng.h"
#include "read.tmh"

EVT_WDF_REQUEST_CANCEL VirtRngEvtRequestCancel;

static NTSTATUS VirtQueueAddBuffer(IN PDEVICE_CONTEXT Context,
                                   IN WDFREQUEST Request,
                                   IN size_t Length)
{
    PREAD_BUFFER_ENTRY entry;
    size_t length;
    struct virtqueue *vq = Context->VirtQueue;
    struct VirtIOBufferDescriptor sg;
    int ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %!FUNC!");

    entry = (PREAD_BUFFER_ENTRY)ExAllocatePoolWithTag(NonPagedPool,
        sizeof(READ_BUFFER_ENTRY), VIRT_RNG_MEMORY_TAG);

    if (entry == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "Failed to allocate a read entry.");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    length = min(Length, PAGE_SIZE);

    entry->Request = Request;

    sg.physAddr = Context->SingleBufferPA;
    sg.length = (unsigned)length;

    WdfSpinLockAcquire(Context->VirtQueueLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
        "Push %p Request: %p Buffer: %p",
        entry, entry->Request, Context->SingleBufferVA);

    PushEntryList(&Context->ReadBuffersList, &entry->ListEntry);

    ret = virtqueue_add_buf(vq, &sg, 0, 1, entry, NULL, 0);
    if (ret < 0)
    {
        PSINGLE_LIST_ENTRY removed;

        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "Failed to add buffer to virt queue.");

        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
            "Pop %p Request: %p Buffer: %p",
            entry, entry->Request, Context->SingleBufferVA);

        removed = PopEntryList(&Context->ReadBuffersList);
        NT_ASSERT(entry == CONTAINING_RECORD(
            removed, READ_BUFFER_ENTRY, ListEntry));

        ExFreePoolWithTag(entry, VIRT_RNG_MEMORY_TAG);

        WdfSpinLockRelease(Context->VirtQueueLock);

        return STATUS_UNSUCCESSFUL;
    }

    WdfSpinLockRelease(Context->VirtQueueLock);

    virtqueue_kick(vq);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

VOID VirtRngEvtIoRead(IN WDFQUEUE Queue,
                      IN WDFREQUEST Request,
                      IN size_t Length)
{
    PDEVICE_CONTEXT context = GetDeviceContext(WdfIoQueueGetDevice(Queue));
    NTSTATUS status;
    PVOID buffer;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
        "--> %!FUNC! Queue: %p Request: %p Length: %d",
        Queue, Request, (ULONG)Length);

    status = WdfRequestRetrieveOutputBuffer(Request, Length, &buffer, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfRequestRetrieveOutputBuffer failed: %!STATUS!", status);
        WdfRequestComplete(Request, status);
        return;
    }

    if (!context->SingleBufferPA.QuadPart || !context->SingleBufferVA)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "The RX buffer is not allocated!");
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    status = WdfRequestMarkCancelableEx(Request, VirtRngEvtRequestCancel);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfRequestMarkCancelableEx failed: %!STATUS!", status);
        WdfRequestComplete(Request, status);
        return;
    }

    status = VirtQueueAddBuffer(context, Request, Length);
    if (!NT_SUCCESS(status))
    {
        if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            WdfRequestComplete(Request, status);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %!FUNC!");
}

VOID VirtRngEvtIoStop(IN WDFQUEUE Queue,
                      IN WDFREQUEST Request,
                      IN ULONG ActionFlags)
{
    UNREFERENCED_PARAMETER(Queue);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
        "--> %!FUNC! Request: %p", Request);

    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        WdfRequestStopAcknowledge(Request, FALSE);
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {
        if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            WdfRequestComplete(Request , STATUS_CANCELLED);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %!FUNC!");
}

VOID VirtRngEvtRequestCancel(IN WDFREQUEST Request)
{
    PDEVICE_CONTEXT context = GetDeviceContext(WdfIoQueueGetDevice(
        WdfRequestGetIoQueue(Request)));
    PSINGLE_LIST_ENTRY iter;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
        "--> %!FUNC! Cancelled Request: %p", Request);

    WdfSpinLockAcquire(context->VirtQueueLock);

    WdfRequestComplete(Request, STATUS_CANCELLED);

    iter = &context->ReadBuffersList;
    while (iter->Next != NULL)
    {
        PREAD_BUFFER_ENTRY entry = CONTAINING_RECORD(iter->Next,
            READ_BUFFER_ENTRY, ListEntry);

        if (Request == entry->Request)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
                "Clear entry %p request.", entry);

            entry->Request = NULL;
            break;
        }
        else
        {
            iter = iter->Next;
        }
    };

    WdfSpinLockRelease(context->VirtQueueLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %!FUNC!");
}
