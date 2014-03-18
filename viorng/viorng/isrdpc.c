/*
 * Copyright (C) 2014 Red Hat, Inc.
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

    context->VirtQueue->vq_ops->enable_interrupt(context->VirtQueue);
    context->VirtQueue->vq_ops->kick(context->VirtQueue);

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

    context->VirtQueue->vq_ops->disable_interrupt(context->VirtQueue);

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
    unsigned int length;

    UNREFERENCED_PARAMETER(AssociatedObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC,
        "--> %!FUNC! Interrupt: %p", Interrupt);

    for (;;)
    {
        PREAD_BUFFER_ENTRY entry;
        WDFREQUEST request;
        PSINGLE_LIST_ENTRY iter;

        WdfSpinLockAcquire(context->VirtQueueLock);
        entry = (PREAD_BUFFER_ENTRY)vq->vq_ops->get_buf(vq, &length);
        WdfSpinLockRelease(context->VirtQueueLock);

        if (entry == NULL)
        {
            break;
        }

        request = entry->Request;

        if (WdfRequestUnmarkCancelable(request) != STATUS_CANCELLED)
        {
            NTSTATUS status;
            PVOID buffer;
            size_t bufferLen = 0;

            status = WdfRequestRetrieveOutputBuffer(request, length,
                &buffer, &bufferLen);

            if (NT_SUCCESS(status))
            {
                length = min(length, (unsigned)bufferLen);
                RtlCopyMemory(buffer, entry->Buffer, length);

                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC,
                    "Complete Request: %p Length: %d", request, length);

                WdfRequestCompleteWithInformation(request, STATUS_SUCCESS,
                    (ULONG_PTR)length);
            }
            else
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
                    "WdfRequestRetrieveOutputBuffer failed: %!STATUS!", status);
                WdfRequestComplete(request, status);
            }
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,
                "Ignoring a canceled read request: %p", request);
        }

        WdfSpinLockAcquire(context->VirtQueueLock);
        iter = &context->ReadBuffersList;
        while (iter->Next != NULL)
        {
            PREAD_BUFFER_ENTRY entry = CONTAINING_RECORD(iter->Next,
                READ_BUFFER_ENTRY, ListEntry);

            if (iter == &entry->ListEntry)
            {
                iter->Next = entry->ListEntry.Next;

                WdfObjectDereference(entry->Request);
                ExFreePoolWithTag(entry->Buffer, VIRT_RNG_MEMORY_TAG);
                ExFreePoolWithTag(entry, VIRT_RNG_MEMORY_TAG);

                break;
            }
            else
            {
                iter = iter->Next;
            }
        };
        WdfSpinLockRelease(context->VirtQueueLock);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %!FUNC!");
}
