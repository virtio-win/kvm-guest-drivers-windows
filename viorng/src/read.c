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
#include "read.tmh"

EVT_WDF_REQUEST_CANCEL VirtRngEvtRequestCancel;

VOID VirtRngEvtIoRead(IN WDFQUEUE Queue,
                      IN WDFREQUEST Request,
                      IN size_t Length)
{
    PDEVICE_CONTEXT context = GetDeviceContext(WdfIoQueueGetDevice(Queue));
    NTSTATUS status;
    PVOID buffer;
    struct virtqueue *vq = context->VirtQueue;
    struct VirtIOBufferDescriptor sg;

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
	}

    sg.physAddr = MmGetPhysicalAddress(buffer);
    sg.ulSize = (ULONG)Length;

    WdfSpinLockAcquire(context->VirtQueueLock);

    if (vq->vq_ops->add_buf(vq, &sg, 0, 1, Request, NULL, 0) < 0)
    {
        // There should always be room for one buffer.
        WDFVERIFY(FALSE);

        WdfRequestComplete(Request, STATUS_UNSUCCESSFUL);
    }
    else
    {
        // Add a reference because the request is send to the virt queue.
        WdfObjectReference(Request);
    }

    vq->vq_ops->kick(vq);

    WdfSpinLockRelease(context->VirtQueueLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %!FUNC!");
}

VOID VirtRngEvtRequestCancel(IN WDFREQUEST Request)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
        "--> %!FUNC! Request: %p", Request);

    WdfRequestComplete(Request, STATUS_CANCELLED);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %!FUNC!");
}
