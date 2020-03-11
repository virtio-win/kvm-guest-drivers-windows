/*
 * Copyright (C) 2019 Red Hat, Inc.
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

#include "viofs.h"
#include "isrdpc.tmh"

NTSTATUS VirtFsEvtInterruptEnable(IN WDFINTERRUPT Interrupt,
                                  IN WDFDEVICE AssociatedDevice)
{
    PDEVICE_CONTEXT context;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT,
        "--> %!FUNC! Interrupt: %p Device: %p",
        Interrupt, AssociatedDevice);

    context = GetDeviceContext(WdfInterruptGetDevice(Interrupt));

    for (ULONG i = 0; i < context->RequestQueues; i++)
    {
        struct virtqueue *vq = context->VirtQueues[i];

        virtqueue_enable_cb(vq);
        virtqueue_kick(vq);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

NTSTATUS VirtFsEvtInterruptDisable(IN WDFINTERRUPT Interrupt,
                                   IN WDFDEVICE AssociatedDevice)
{
    PDEVICE_CONTEXT context;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT,
        "--> %!FUNC! Interrupt: %p Device: %p",
        Interrupt, AssociatedDevice);

    context = GetDeviceContext(WdfInterruptGetDevice(Interrupt));

    for (ULONG i = 0; i < context->RequestQueues; i++)
    {
        struct virtqueue *vq = context->VirtQueues[i];

        virtqueue_disable_cb(vq);
    }    

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

BOOLEAN VirtFsEvtInterruptIsr(IN WDFINTERRUPT Interrupt, IN ULONG MessageId)
{
    PDEVICE_CONTEXT context;
    WDF_INTERRUPT_INFO info;
    BOOLEAN serviced;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT,
        "--> %!FUNC! Interrupt: %p MessageId: %u", Interrupt, MessageId);

    context = GetDeviceContext(WdfInterruptGetDevice(Interrupt));

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(context->WdfInterrupt, &info);

    if ((info.MessageSignaled && (MessageId < context->RequestQueues)) ||
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

static VOID VirtFsReadFromQueue(PDEVICE_CONTEXT context,
                                struct virtqueue *vq,
                                WDFSPINLOCK vq_lock)
{
    PVIRTIO_FS_REQUEST fs_req;
    PSINGLE_LIST_ENTRY iter;
    NTSTATUS status;
    PVOID out_buf_va;
    PUCHAR out_buf;    
    size_t out_len;
    unsigned int length;

    for (;;)
    {
        WdfSpinLockAcquire(vq_lock);

        fs_req = virtqueue_get_buf(vq, &length);
        if (fs_req == NULL)
        {
            WdfSpinLockRelease(vq_lock);
            break;
        }

        WdfSpinLockRelease(vq_lock);

        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC,
            "Got %p Request: %p", fs_req, fs_req->Request);

        WdfSpinLockAcquire(context->RequestsLock);
        iter = &context->RequestsList;
        while (iter->Next != NULL)
        {
            PVIRTIO_FS_REQUEST removed = CONTAINING_RECORD(iter->Next,
                VIRTIO_FS_REQUEST, ListEntry);

            if (fs_req == removed)
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC,
                    "Delete %p Request: %p", removed, removed->Request);
                iter->Next = removed->ListEntry.Next;
                break;
            }
            else
            {
                iter = iter->Next;
            }
        };
        WdfSpinLockRelease(context->RequestsLock);

        if ((fs_req->Request == NULL) ||
            (WdfRequestUnmarkCancelable(fs_req->Request) == STATUS_CANCELLED))
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,
                "Ignoring a cancelled request: %p", fs_req->Request);

            fs_req->Request = NULL;
        }

        if (fs_req->Request != NULL)
        {
            status = WdfRequestRetrieveOutputBuffer(fs_req->Request, length,
                &out_buf, &out_len);

            if (NT_SUCCESS(status))
            {
                length = min(length, (unsigned)out_len);

                out_buf_va = MmMapLockedPagesSpecifyCache(
                    fs_req->OutputBuffer, KernelMode, MmNonCached, NULL,
                    FALSE, NormalPagePriority);

                if (out_buf_va != NULL)
                {
                    RtlCopyMemory(out_buf, out_buf_va, length);
                    MmUnmapLockedPages(out_buf_va, fs_req->OutputBuffer);
                }
                else
                {
                    TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
                        "MmMapLockedPages failed");
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    length = 0;
                }
            }
            else
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_DPC,
                    "WdfRequestRetrieveOutputBuffer failed");
            }

            TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC,
                "Complete Request: %p Status: %!STATUS! Length: %d",
                fs_req->Request, status, length);

            WdfRequestCompleteWithInformation(fs_req->Request, status,
                (ULONG_PTR)length);
        }

        FreeVirtFsRequest(fs_req);
    }
}

VOID VirtFsEvtInterruptDpc(IN WDFINTERRUPT Interrupt,
                           IN WDFOBJECT AssociatedObject)
{
    PDEVICE_CONTEXT context;
    WDF_INTERRUPT_INFO info;
    struct virtqueue *vq = NULL;
    WDFSPINLOCK vq_lock = NULL;
    ULONG i;

    UNREFERENCED_PARAMETER(AssociatedObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC,
        "--> %!FUNC! Interrupt: %p", Interrupt);

    context = GetDeviceContext(WdfInterruptGetDevice(Interrupt));

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(context->WdfInterrupt, &info);

    if ((info.MessageSignaled == TRUE) &&
        (info.MessageNumber < context->RequestQueues))
    {
        vq = context->VirtQueues[info.MessageNumber];
        vq_lock = context->VirtQueueLocks[info.MessageNumber];
    }

    if (vq != NULL)
    {
        VirtFsReadFromQueue(context, vq, vq_lock);
    }
    else
    {
        for (i = 0; i < context->RequestQueues; i++)
        {
            vq = context->VirtQueues[i];
            vq_lock = context->VirtQueueLocks[i];

            VirtFsReadFromQueue(context, vq, vq_lock);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %!FUNC!");
}
