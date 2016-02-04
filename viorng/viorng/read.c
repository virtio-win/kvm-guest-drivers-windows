/*
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Refer to the LICENSE file for full details of the license.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
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
    entry->Buffer = ExAllocatePoolWithTag(NonPagedPool, length,
        VIRT_RNG_MEMORY_TAG);

    if (entry->Buffer == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "Failed to allocate a read buffer.");
        ExFreePoolWithTag(entry, VIRT_RNG_MEMORY_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    entry->Request = Request;

    sg.physAddr = MmGetPhysicalAddress(entry->Buffer);
    sg.length = (unsigned)length;

    WdfSpinLockAcquire(Context->VirtQueueLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
        "Push %p Request: %p Buffer: %p",
        entry, entry->Request, entry->Buffer);

    PushEntryList(&Context->ReadBuffersList, &entry->ListEntry);

    ret = virtqueue_add_buf(vq, &sg, 0, 1, entry, NULL, 0);
    if (ret < 0)
    {
        PSINGLE_LIST_ENTRY removed;

        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "Failed to add buffer to virt queue.");

        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
            "Pop %p Request: %p Buffer: %p",
            entry, entry->Request, entry->Buffer);

        removed = PopEntryList(&Context->ReadBuffersList);
        NT_ASSERT(entry == CONTAINING_RECORD(
            removed, READ_BUFFER_ENTRY, ListEntry));

        ExFreePoolWithTag(entry->Buffer, VIRT_RNG_MEMORY_TAG);
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
