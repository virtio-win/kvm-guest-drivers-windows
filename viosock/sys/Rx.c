/*
 * Placeholder for the Recv path functions
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
#include "Rx.tmh"
#endif

#define VIOSOCK_DMA_RX_PAGES    1           //contiguous buffer

typedef struct _VIOSOCK_RX_PKT
{
    VIRTIO_VSOCK_HDR    Header;
    union {
        BYTE IndirectDescs[SIZE_OF_SINGLE_INDIRECT_DESC * (1 + VIOSOCK_DMA_RX_PAGES)]; //Header + buffer
        SINGLE_LIST_ENTRY ListEntry;
    };
}VIOSOCK_RX_PKT, *PVIOSOCK_RX_PKT;

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockRxVqInit)
#pragma alloc_text (PAGE, VIOSockRxVqCleanup)
#endif

//////////////////////////////////////////////////////////////////////////
static
BOOLEAN
VIOSockRxPktInsert(
    IN PDEVICE_CONTEXT pContext,
    IN PVIOSOCK_RX_PKT pPkt
)
{
    BOOLEAN bRes = TRUE;
    VIOSOCK_SG_DESC sg[VIOSOCK_DMA_RX_PAGES + 1];
    PHYSICAL_ADDRESS pPKtPA;
    int ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    pPKtPA.QuadPart = pContext->RxPktPA.QuadPart +
        (ULONGLONG)((PCHAR)pPkt - (PCHAR)pContext->RxPktVA);

    sg[0].length = sizeof(VIRTIO_VSOCK_HDR);
    sg[0].physAddr.QuadPart = pPKtPA.QuadPart + FIELD_OFFSET(VIOSOCK_RX_PKT, Header);

    ret = virtqueue_add_buf(pContext->RxVq, sg, 0, 2, pPkt, &pPkt->IndirectDescs,
        pPKtPA.QuadPart + FIELD_OFFSET(VIOSOCK_RX_PKT, IndirectDescs));

    ASSERT(ret >= 0);
    if (ret < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Error adding buffer to Rx queue (ret = %d)\n", ret);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return TRUE;
}

VOID
VIOSockRxVqCleanup(
    IN PDEVICE_CONTEXT pContext
)
{
    PVIOSOCK_RX_PKT pPkt;
    PSINGLE_LIST_ENTRY pListEntry;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    ASSERT(pContext->RxVq && pContext->RxPktVA);

    if (pContext->RxPktVA)
    {
        VirtIOWdfDeviceFreeDmaMemory(&pContext->VDevice.VIODevice, pContext->RxPktVA);
        pContext->RxPktVA = NULL;
    }

    pContext->RxVq = NULL;
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VIOSockRxVqInit(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    USHORT uNumEntries;
    ULONG uRingSize, uHeapSize, uBufferSize;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = pContext->ThisDevice;

    status = WdfSpinLockCreate(
        &attributes,
        &pContext->RxLock
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfSpinLockCreate failed: 0x%x\n", status);
        return FALSE;
    }

    status = virtio_query_queue_allocation(&pContext->VDevice.VIODevice, VIOSOCK_VQ_RX,
        &uNumEntries, &uRingSize, &uHeapSize);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "virtio_query_queue_allocation(VIOSOCK_VQ_RX) failed\n");
        pContext->RxVq = NULL;
        return status;
    }

    pContext->RxPktNum = uNumEntries;

    uBufferSize = sizeof(VIOSOCK_RX_PKT) * uNumEntries;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Allocating common buffer of %u bytes for %u Rx packets\n",
        uBufferSize, uNumEntries);

    pContext->RxPktVA = (PVIOSOCK_RX_PKT)VirtIOWdfDeviceAllocDmaMemory(&pContext->VDevice.VIODevice,
        uBufferSize, VIOSOCK_DRIVER_MEMORY_TAG);

    ASSERT(pContext->RxPktVA);
    if (!pContext->RxPktVA)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "VirtIOWdfDeviceAllocDmaMemory(%u bytes for RxPackets) failed\n", uBufferSize);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        ULONG i;
        PVIOSOCK_RX_PKT RxPktVA = (PVIOSOCK_RX_PKT)pContext->RxPktVA;

        pContext->RxPktPA = VirtIOWdfDeviceGetPhysicalAddress(&pContext->VDevice.VIODevice, pContext->RxPktVA);
        ASSERT(pContext->RxPktPA.QuadPart);

        //fill queue, no lock
        for (i = 0; i < uNumEntries; i++)
        {
            if (!VIOSockRxPktInsert(pContext, &RxPktVA[i]))
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIOSockRxPktInsert[%u] failed\n", i);
                status = STATUS_UNSUCCESSFUL;
                break;
            }
        }
    }

    if (!NT_SUCCESS(status))
    {
        VIOSockRxVqCleanup(pContext);
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);

    return status;
}

VOID
VIOSockRxVqProcess(
    IN PDEVICE_CONTEXT pContext
)
{
    PVIOSOCK_RX_PKT     pPkt;
    UINT                len;
    SINGLE_LIST_ENTRY   CompletionList;
    PSINGLE_LIST_ENTRY  pCurrentEntry;
    PSOCKET_CONTEXT     pSocket = NULL;

    NTSTATUS            status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    CompletionList.Next = NULL;

    WdfSpinLockAcquire(pContext->RxLock);
    do
    {
        virtqueue_disable_cb(pContext->RxVq);

        while (TRUE)
        {
            pPkt = (PVIOSOCK_RX_PKT)virtqueue_get_buf(pContext->RxVq, &len);
            if (!pPkt)
                break;

            /* Drop short/long packets */

            if (len < sizeof(pPkt->Header) ||
                len > sizeof(pPkt->Header) + pPkt->Header.len)
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "Short/Long packet\n");
                VIOSockRxPktInsert(pContext, pPkt);
                continue;
            }

            ASSERT(pPkt->Header.len == len - sizeof(pPkt->Header));

            //"complete" buffers later
            PushEntryList(&CompletionList, &pPkt->ListEntry);
        }
    } while (!virtqueue_enable_cb(pContext->RxVq));

    WdfSpinLockRelease(pContext->RxLock);

    //complete buffers
    while ((pCurrentEntry = PopEntryList(&CompletionList)) != NULL)
    {
        pPkt = CONTAINING_RECORD(pCurrentEntry, VIOSOCK_RX_PKT, ListEntry);

        //Update CID in case it has changed after a transport reset event
        //pContext->Config.guest_cid = (ULONG32)pPkt->Header.dst_cid;
        ASSERT(pContext->Config.guest_cid == (ULONG32)pPkt->Header.dst_cid);

        //reinsert handled packet
        WdfSpinLockAcquire(pContext->RxLock);
        VIOSockRxPktInsert(pContext, pPkt);
        WdfSpinLockRelease(pContext->RxLock);
    };

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}


