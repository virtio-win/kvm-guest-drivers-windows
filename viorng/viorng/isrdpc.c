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
        VirtIOWdfGetISRStatus(&context->VDevice))
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
            entry, entry->Request, context->SingleBufferVA);

        iter = &context->ReadBuffersList;
        while (iter->Next != NULL)
        {
            PREAD_BUFFER_ENTRY current = CONTAINING_RECORD(iter->Next,
                READ_BUFFER_ENTRY, ListEntry);

            if (entry == current)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
                    "Delete %p Request: %p Buffer: %p",
                    entry, entry->Request, context->SingleBufferVA);

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
                RtlCopyMemory(buffer, context->SingleBufferVA, length);

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

        ExFreePoolWithTag(entry, VIRT_RNG_MEMORY_TAG);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %!FUNC!");
}
