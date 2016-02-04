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
#include "isrdpc.tmh"

NTSTATUS VirtRngEvtInterruptEnable(IN WDFINTERRUPT Interrupt,
                                   IN WDFDEVICE AssociatedDevice)
{
    PDEVICE_CONTEXT context = GetDeviceContext(
        WdfInterruptGetDevice(Interrupt));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT,
        "--> %!FUNC! Interrupt: %p Device: %p",
        Interrupt, AssociatedDevice);

    virtqueue_enable_cb(context->VirtQueue);
    virtqueue_kick(context->VirtQueue);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

NTSTATUS VirtRngEvtInterruptDisable(IN WDFINTERRUPT Interrupt,
                                    IN WDFDEVICE AssociatedDevice)
{
    PDEVICE_CONTEXT context = GetDeviceContext(
        WdfInterruptGetDevice(Interrupt));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT,
        "--> %!FUNC! Interrupt: %p Device: %p",
        Interrupt, AssociatedDevice);

    virtqueue_disable_cb(context->VirtQueue);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

BOOLEAN VirtRngEvtInterruptIsr(IN WDFINTERRUPT Interrupt, IN ULONG MessageId)
{
    PDEVICE_CONTEXT context = GetDeviceContext(
        WdfInterruptGetDevice(Interrupt));
    WDF_INTERRUPT_INFO info;
    BOOLEAN serviced;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT,
        "--> %!FUNC! Interrupt: %p MessageId: %u", Interrupt, MessageId);

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(context->WdfInterrupt, &info);

    if ((info.MessageSignaled && (MessageId == 0)) ||
        VirtIODeviceISR(&context->VirtDevice))
    {
        WdfInterruptQueueDpcForIsr(Interrupt);
        serviced = TRUE;
    }
    else
    {
        serviced = FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-- %!FUNC!");

    return serviced;
}

VOID VirtRngEvtInterruptDpc(IN WDFINTERRUPT Interrupt,
                            IN WDFOBJECT AssociatedObject)
{
    PDEVICE_CONTEXT context = GetDeviceContext(
        WdfInterruptGetDevice(Interrupt));
    struct virtqueue *vq = context->VirtQueue;
    PREAD_BUFFER_ENTRY entry;
    PSINGLE_LIST_ENTRY iter;
    NTSTATUS status;
    PVOID buffer;
    size_t bufferLen;
    unsigned int length;

    UNREFERENCED_PARAMETER(AssociatedObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC,
        "--> %!FUNC! Interrupt: %p", Interrupt);

    for (;;)
    {
        WdfSpinLockAcquire(context->VirtQueueLock);

        entry = (PREAD_BUFFER_ENTRY)virtqueue_get_buf(vq, &length);
        if (entry == NULL)
        {
            WdfSpinLockRelease(context->VirtQueueLock);
            break;
        }

        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
            "Got %p Request: %p Buffer: %p",
            entry, entry->Request, entry->Buffer);

        iter = &context->ReadBuffersList;
        while (iter->Next != NULL)
        {
            PREAD_BUFFER_ENTRY current = CONTAINING_RECORD(iter->Next,
                READ_BUFFER_ENTRY, ListEntry);

            if (entry == current)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
                    "Delete %p Request: %p Buffer: %p",
                    entry, entry->Request, entry->Buffer);

                iter->Next = current->ListEntry.Next;
                break;
            }
            else
            {
                iter = iter->Next;
            }
        };

        if ((entry->Request == NULL) ||
            (WdfRequestUnmarkCancelable(entry->Request) == STATUS_CANCELLED))
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,
                "Ignoring a canceled read request: %p", entry->Request);

            entry->Request = NULL;
        }

        WdfSpinLockRelease(context->VirtQueueLock);

        if (entry->Request != NULL)
        {
            status = WdfRequestRetrieveOutputBuffer(entry->Request, length,
                &buffer, &bufferLen);

            if (NT_SUCCESS(status))
            {
                length = min(length, (unsigned)bufferLen);
                RtlCopyMemory(buffer, entry->Buffer, length);

                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC,
                    "Complete Request: %p Length: %d", entry->Request, length);

                WdfRequestCompleteWithInformation(entry->Request,
                    STATUS_SUCCESS, (ULONG_PTR)length);
            }
            else
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
                    "WdfRequestRetrieveOutputBuffer failed: %!STATUS!", status);
                WdfRequestComplete(entry->Request, status);
            }
        }

        ExFreePoolWithTag(entry->Buffer, VIRT_RNG_MEMORY_TAG);
        ExFreePoolWithTag(entry, VIRT_RNG_MEMORY_TAG);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %!FUNC!");
}
