/*
 * Copyright (C) 2013 Red Hat, Inc.
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
 * Refer to the COPYING file for full details of the license.
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
    BOOLEAN serviced;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT,
        "--> %!FUNC! Interrupt: %p MessageId: %u", Interrupt, MessageId);

    if (MessageId || VirtIODeviceISR(&context->VirtDevice))
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
    WDFREQUEST request;
    unsigned int length;

    UNREFERENCED_PARAMETER(AssociatedObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC,
        "--> %!FUNC! Interrupt: %p", Interrupt);

    WdfSpinLockAcquire(context->VirtQueueLock);
    request = (WDFREQUEST)vq->vq_ops->get_buf(vq, &length);
    WdfSpinLockRelease(context->VirtQueueLock);

    if (request)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "Request: %p", request);

        // Check if the request was not already cancelled or completed.
        if (WdfRequestGetIoQueue(request) &&
            (WdfRequestUnmarkCancelable(request) != STATUS_CANCELLED))
        {
            WdfRequestCompleteWithInformation(request, STATUS_SUCCESS,
                (ULONG_PTR)length);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,
                "Ignoring a cancelled read request: %p", request);
        }

        // Removed the reference after the request was pulled out from the
        // virt queue.
        WdfObjectDereference(request);
    }

	TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %!FUNC!");
}
