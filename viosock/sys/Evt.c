/*
 * Placeholder for the Event handling functions
 *
 * Copyright (c) 2020 Virtuozzo International GmbH
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

#include "precomp.h"
#include "viosock.h"

#if defined(EVENT_TRACING)
#include "Evt.tmh"
#endif


#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockEvtVqInit)
#pragma alloc_text (PAGE, VIOSockEvtVqCleanup)
#endif

static
BOOLEAN
VIOSockEvtPktInsert(
    IN PDEVICE_CONTEXT pContext,
    IN PVIRTIO_VSOCK_EVENT pEvent
)
{
    BOOLEAN bRes = TRUE;
    VIOSOCK_SG_DESC sg;
    int ret;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    sg.length = sizeof(VIRTIO_VSOCK_EVENT);
    sg.physAddr.QuadPart = pContext->EvtPA.QuadPart +
        (ULONGLONG)((PCHAR)pEvent - (PCHAR)pContext->EvtVA);

    ret = virtqueue_add_buf(pContext->EvtVq, &sg, 0, 1, pEvent, NULL, 0);

    ASSERT(ret >= 0);
    if (ret < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Error adding buffer to Evt queue (ret = %d)\n", ret);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return TRUE;
}

VOID
VIOSockEvtVqCleanup(
    IN PDEVICE_CONTEXT pContext
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    ASSERT(pContext->EvtVq && pContext->EvtVA);

    //drain queue
    while (virtqueue_detach_unused_buf(pContext->EvtVq));

    //process??
    if (pContext->EvtVA)
    {
        VirtIOWdfDeviceFreeDmaMemory(&pContext->VDevice.VIODevice, pContext->EvtVA);
        pContext->EvtVA = NULL;
    }

    pContext->EvtVq = NULL;
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VIOSockEvtVqInit(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG uRingSize, uHeapSize, uBufferSize;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    uBufferSize = sizeof(VIRTIO_VSOCK_EVENT) * VIRTIO_VSOCK_MAX_EVENTS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Allocating common buffer of %u bytes for %u Events\n",
        uBufferSize, VIRTIO_VSOCK_MAX_EVENTS);

    pContext->EvtVA = (PVIRTIO_VSOCK_EVENT)VirtIOWdfDeviceAllocDmaMemory(&pContext->VDevice.VIODevice,
        uBufferSize, VIOSOCK_DRIVER_MEMORY_TAG);

    ASSERT(pContext->EvtVA);
    if (!pContext->EvtVA)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "VirtIOWdfDeviceAllocDmaMemory(%u bytes for Events) failed\n", uBufferSize);
        status = STATUS_INSUFFICIENT_RESOURCES;
        pContext->EvtVq = NULL;
    }
    else
    {
        ULONG i;

        pContext->EvtPA = VirtIOWdfDeviceGetPhysicalAddress(&pContext->VDevice.VIODevice, pContext->EvtVA);
        ASSERT(pContext->EvtPA.QuadPart);

        //fill queue
        for (i = 0; i < VIRTIO_VSOCK_MAX_EVENTS; i++)
        {
            if (!VIOSockEvtPktInsert(pContext, &pContext->EvtVA[i]))
            {
                status = STATUS_UNSUCCESSFUL;
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIOSockEventInsert[%u] failed\n", i);
            }
        }
        if (!NT_SUCCESS(status))
            VIOSockEvtVqCleanup(pContext);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);

    return status;
}

VOID
VIOSockEvtVqProcess(
    IN PDEVICE_CONTEXT pContext
)
{
    PVIRTIO_VSOCK_EVENT pEvt;
    BOOLEAN bNotify = FALSE, bKick = FALSE;
    UINT len;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    do
    {
        virtqueue_disable_cb(pContext->EvtVq);

        while ((pEvt = (PVIRTIO_VSOCK_EVENT)virtqueue_get_buf(pContext->EvtVq, &len)) != NULL)
        {
            /* Drop short/long events */

            if (len != sizeof(pEvt))
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "Invalid event\n");
            }
            else
            {
                ASSERT(pEvt->id == VIRTIO_VSOCK_EVENT_TRANSPORT_RESET);
                if (pEvt->id == VIRTIO_VSOCK_EVENT_TRANSPORT_RESET)
                {
                    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Reset event occurred\n");
                    ++pContext->EvtRstOccured;
                    bNotify = TRUE;
                }
            }
            VIOSockEvtPktInsert(pContext, pEvt);
            bKick = TRUE;
        }
    } while (!virtqueue_enable_cb(pContext->EvtVq));


    if (bNotify)
    {
        VirtIOWdfDeviceGet(&pContext->VDevice,
            0,
            &pContext->Config,
            sizeof(pContext->Config));

        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS,
            "New guest_cid %lld\n", pContext->Config.guest_cid);

        VIOSockHandleTransportReset(pContext);

    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}
