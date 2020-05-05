/*
 * Placeholder for the Send path functions
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
#include "Tx.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockTxVqInit)
#pragma alloc_text (PAGE, VIOSockTxVqCleanup)
#endif

#define VIOSOCK_DMA_TX_PAGES BYTES_TO_PAGES(VIRTIO_VSOCK_MAX_PKT_BUF_SIZE)

typedef struct _VIOSOCK_TX_PKT
{
    VIRTIO_VSOCK_HDR Header;
    PHYSICAL_ADDRESS PhysAddr; //packet addr
    WDFDMATRANSACTION Transaction;
    union
    {
        BYTE IndirectDescs[SIZE_OF_SINGLE_INDIRECT_DESC * (1 + VIOSOCK_DMA_TX_PAGES)]; //Header + sglist
        struct
        {
            SINGLE_LIST_ENTRY ListEntry;
            WDFREQUEST Request;
        };
    };
}VIOSOCK_TX_PKT, *PVIOSOCK_TX_PKT;

//////////////////////////////////////////////////////////////////////////

VOID
VIOSockTxVqCleanup(
    IN PDEVICE_CONTEXT pContext
)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    ASSERT(pContext->TxVq);
    if (pContext->TxPktSliced)
    {
        pContext->TxPktSliced->destroy(pContext->TxPktSliced);
        pContext->TxPktSliced = NULL;
        pContext->TxPktNum = 0;
    }
    pContext->TxVq = NULL;
}

NTSTATUS
VIOSockTxVqInit(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    USHORT uNumEntries;
    ULONG uRingSize, uHeapSize, uBufferSize;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    status = virtio_query_queue_allocation(&pContext->VDevice.VIODevice, VIOSOCK_VQ_TX, &uNumEntries, &uRingSize, &uHeapSize);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "virtio_query_queue_allocation(VIOSOCK_VQ_TX) failed\n");
        pContext->TxVq = NULL;
        return status;
    }

    uBufferSize = sizeof(VIOSOCK_TX_PKT) * uNumEntries;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Allocating sliced buffer of %u bytes for %u Tx packets\n",
        uBufferSize, uNumEntries);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = pContext->ThisDevice;
    status = WdfSpinLockCreate(
        &attributes,
        &pContext->TxLock
    );

    pContext->TxPktSliced = VirtIOWdfDeviceAllocDmaMemorySliced(&pContext->VDevice.VIODevice,
        uBufferSize, sizeof(VIOSOCK_TX_PKT));

    ASSERT(pContext->TxPktSliced);
    if (!pContext->TxPktSliced)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "VirtIOWdfDeviceAllocDmaMemorySliced(%u butes for TxPackets) failed\n", uBufferSize);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    pContext->TxPktNum = uNumEntries;
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);

    return status;
}

//SRxLock+
static
PVIOSOCK_TX_PKT
VIOSockTxPktAlloc(
    IN PSOCKET_CONTEXT pSocket
)
{
    PHYSICAL_ADDRESS PA;
    PVIOSOCK_TX_PKT pPkt;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);

    ASSERT(pContext->TxPktSliced);
    pPkt = pContext->TxPktSliced->get_slice(pContext->TxPktSliced, &PA);
    if (pPkt)
    {
        pPkt->PhysAddr = PA;

        pPkt->Header.src_cid = pContext->Config.guest_cid;
        pPkt->Header.dst_cid = pSocket->dst_cid;
        pPkt->Header.src_port = pSocket->src_port;
        pPkt->Header.dst_port = pSocket->dst_port;
        pPkt->Header.type = VIRTIO_VSOCK_TYPE_STREAM;
    }
    return pPkt;
}

#define VIOSockTxPktFree(cx,va) (cx)->TxPktSliced->return_slice((cx)->TxPktSliced, va)

//TxLock+
static
BOOLEAN
VIOSockTxPktInsert(
    IN PDEVICE_CONTEXT pContext,
    IN PVIOSOCK_TX_PKT pPkt,
    IN PVIRTIO_DMA_TRANSACTION_PARAMS pParams OPTIONAL
)
{
    VIOSOCK_SG_DESC sg[VIOSOCK_DMA_TX_PAGES + 1];
    ULONG uElements = 1, uPktLen = 0;
    PVOID va_indirect = NULL;
    ULONGLONG phys_indirect = 0;
    PSCATTER_GATHER_LIST SgList = NULL;

    int ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    if (pParams)
    {
        ASSERT(pParams->transaction);
        pPkt->Transaction = pParams->transaction;
        SgList = pParams->sgList;
    }

    sg[0].length = sizeof(VIRTIO_VSOCK_HDR);
    sg[0].physAddr.QuadPart = pPkt->PhysAddr.QuadPart + FIELD_OFFSET(VIOSOCK_TX_PKT, Header);

    if (SgList)
    {
        ULONG i;

        ASSERT(SgList->NumberOfElements <= VIOSOCK_DMA_TX_PAGES);
        for (i = 0; i < SgList->NumberOfElements; i++)
        {
            sg[i + 1].length = SgList->Elements[i].Length;
            sg[i + 1].physAddr = SgList->Elements[i].Address;
        }
        uElements += i;
    }

    if (uElements > 1)
    {
        va_indirect = &pPkt->IndirectDescs;
        phys_indirect = pPkt->PhysAddr.QuadPart + FIELD_OFFSET(VIOSOCK_TX_PKT, IndirectDescs);
    }

    ret = virtqueue_add_buf(pContext->TxVq, sg, uElements, 0, pPkt, va_indirect, phys_indirect);

    ASSERT(ret >= 0);
    if (ret < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Error adding buffer to queue (ret = %d)\n", ret);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return TRUE;
}

//TxLock-
VOID
VIOSockTxVqProcess(
    IN PDEVICE_CONTEXT pContext
)
{
    PVIOSOCK_TX_PKT pPkt;
    UINT len;
    SINGLE_LIST_ENTRY CompletionList;
    PSINGLE_LIST_ENTRY CurrentItem;
    NTSTATUS status;
    WDFREQUEST Request;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    CompletionList.Next = NULL;

    WdfSpinLockAcquire(pContext->TxLock);
    do
    {
        virtqueue_disable_cb(pContext->TxVq);

        while ((pPkt = (PVIOSOCK_TX_PKT)virtqueue_get_buf(pContext->TxVq, &len)) != NULL)
        {
            if (pPkt->Transaction != WDF_NO_HANDLE)
            {
                pPkt->Request = WdfDmaTransactionGetRequest(pPkt->Transaction);

                //postpone to complete transaction
                PushEntryList(&CompletionList, &pPkt->ListEntry);
            }
            else
            {
                //just free packet
                pPkt->Request = WDF_NO_HANDLE;
                VIOSockTxPktFree(pContext, pPkt);
            }
        }
    } while (!virtqueue_enable_cb(pContext->TxVq));

    WdfSpinLockRelease(pContext->TxLock);

    while ((CurrentItem = PopEntryList(&CompletionList)) != NULL)
    {
        pPkt = CONTAINING_RECORD(CurrentItem, VIOSOCK_TX_PKT, ListEntry);

        if (pPkt->Transaction != WDF_NO_HANDLE)
        {
            VirtIOWdfDeviceDmaTxComplete(&pContext->VDevice.VIODevice, pPkt->Transaction);
            if (pPkt->Request != WDF_NO_HANDLE)
                WdfRequestCompleteWithInformation(pPkt->Request, STATUS_SUCCESS, pPkt->Header.len);
        }
        VIOSockTxPktFree(pContext, pPkt);
    };

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}


static
BOOLEAN
VIOSockTxDequeueCallback(
    IN PVIRTIO_DMA_TRANSACTION_PARAMS pParams
)
{
    PVIOSOCK_TX_PKT pPkt = pParams->param1;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket((PSOCKET_CONTEXT)pParams->param2);
    BOOLEAN         bRes;

    WdfSpinLockAcquire(pContext->TxLock);
    bRes = VIOSockTxPktInsert(pContext, pPkt, pParams);
    WdfSpinLockRelease(pContext->TxLock);

    if (!bRes)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "VIOSockTxPktInsert failed\n");

        VIOSockTxPktFree(pContext, pPkt);
        VirtIOWdfDeviceDmaTxComplete(&pContext->VDevice.VIODevice, pParams->transaction);
        WdfRequestComplete(pParams->req, STATUS_INSUFFICIENT_RESOURCES);
    }
    else
    {
        virtqueue_kick(pContext->TxVq);
    }

    return bRes;
}

//////////////////////////////////////////////////////////////////////////