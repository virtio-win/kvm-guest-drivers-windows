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

typedef struct _VIOSOCK_TX_ENTRY {
    LIST_ENTRY      ListEntry;
    WDFMEMORY       Memory;
    WDFREQUEST      Request;
    WDFFILEOBJECT   Socket;

    ULONG64         dst_cid;
    ULONG32         src_port;
    ULONG32         dst_port;

    ULONG32         len;
    USHORT          op;
    BOOLEAN         reply;
    ULONG32         flags;
}VIOSOCK_TX_ENTRY, *PVIOSOCK_TX_ENTRY;

VOID
VIOSockTxDequeue(
    PDEVICE_CONTEXT pContext
);

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
    WDF_OBJECT_ATTRIBUTES attributes, memAttributes;

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

    WDF_OBJECT_ATTRIBUTES_INIT(&memAttributes);
    memAttributes.ParentObject = pContext->ThisDevice;

    status = WdfLookasideListCreate(&memAttributes,
        sizeof(VIOSOCK_TX_ENTRY), NonPagedPoolNx,
        &memAttributes, VIOSOCK_DRIVER_MEMORY_TAG,
        &pContext->TxMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfLookasideListCreate failed: 0x%x\n", status);
        return status;
    }

    InitializeListHead(&pContext->TxList);

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

__inline
VOID
VIOSockRxIncTxPkt(
    IN PSOCKET_CONTEXT pSocket,
    IN OUT PVIRTIO_VSOCK_HDR pPkt
)
{
    pSocket->last_fwd_cnt = pSocket->fwd_cnt;
    pPkt->fwd_cnt = pSocket->fwd_cnt;
    pPkt->buf_alloc = pSocket->buf_alloc;
}

static
PVIOSOCK_TX_PKT
VIOSockTxPktAlloc(
    IN PVIOSOCK_TX_ENTRY pTxEntry
)
{
    PHYSICAL_ADDRESS PA;
    PVIOSOCK_TX_PKT pPkt;
    PSOCKET_CONTEXT pSocket = GetSocketContext(pTxEntry->Socket);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);

    ASSERT(pContext->TxPktSliced);
    pPkt = pContext->TxPktSliced->get_slice(pContext->TxPktSliced, &PA);
    if (pPkt)
    {
        pPkt->PhysAddr = PA;
        pPkt->Transaction = WDF_NO_HANDLE;
        VIOSockRxIncTxPkt(pSocket, &pPkt->Header);
        pPkt->Header.src_cid = pContext->Config.guest_cid;
        pPkt->Header.dst_cid = pTxEntry->dst_cid;
        pPkt->Header.src_port = pTxEntry->src_port;
        pPkt->Header.dst_port = pTxEntry->dst_port;
        pPkt->Header.len = pTxEntry->len;
        pPkt->Header.type = VIRTIO_VSOCK_TYPE_STREAM;
        pPkt->Header.op = pTxEntry->op;
        pPkt->Header.flags = pTxEntry->flags;

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

    VIOSockTxDequeue(pContext);

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

static
VOID
VIOSockTxDequeue(
    PDEVICE_CONTEXT pContext
)
{
    static volatile LONG    lInProgress;
    BOOLEAN                 bKick = FALSE, bReply, bRestartRx = FALSE;

    WdfSpinLockAcquire(pContext->TxLock);

    while (!IsListEmpty(&pContext->TxList))
    {
        PVIOSOCK_TX_ENTRY   pTxEntry = CONTAINING_RECORD(pContext->TxList.Flink,
            VIOSOCK_TX_ENTRY, ListEntry);
        PSOCKET_CONTEXT     pSocket = GetSocketContext(pTxEntry->Socket);
        PVIOSOCK_TX_PKT     pPkt = VIOSockTxPktAlloc(pTxEntry);
        NTSTATUS            status;

        //can't allocate packet, stop dequeue
        if (!pPkt)
            break;

        RemoveHeadList(&pContext->TxList);

        bReply = pTxEntry->reply;

        if (pTxEntry->Request)
        {
            ASSERT(pTxEntry->len && !bReply);
            status = WdfRequestUnmarkCancelable(pTxEntry->Request);

            if (NT_SUCCESS(status))
            {
                VIRTIO_DMA_TRANSACTION_PARAMS params = { 0 };

                WdfSpinLockRelease(pContext->TxLock);

                params.req = pTxEntry->Request;

                params.param1 = pPkt;
                params.param2 = pSocket;

                //create transaction
                if (!VirtIOWdfDeviceDmaTxAsync(&pContext->VDevice.VIODevice, &params, VIOSockTxDequeueCallback))
                {
                    if (params.transaction)
                        VirtIOWdfDeviceDmaTxComplete(&pContext->VDevice.VIODevice, params.transaction);
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    WdfRequestComplete(pTxEntry->Request, STATUS_INSUFFICIENT_RESOURCES);
                    VIOSockTxPktFree(pContext, pPkt);
                }

                if (!NT_SUCCESS(status))
                {
                    WdfRequestComplete(pTxEntry->Request, status);
                    VIOSockTxPktFree(pContext, pPkt);
                }

                WdfSpinLockAcquire(pContext->TxLock);
            }
            else
            {
                ASSERT(status == STATUS_CANCELLED);
                TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE, "Write request canceled\n");
            }
        }
        else
        {
            ASSERT(pTxEntry->Memory != WDF_NO_HANDLE);
            if (VIOSockTxPktInsert(pContext, pPkt, NULL))
            {
                bKick = TRUE;
                WdfObjectDelete(pTxEntry->Memory);
            }
            else
            {
                ASSERT(FALSE);
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIOSockTxPktInsert failed\n");
                InsertHeadList(&pContext->TxList, &pTxEntry->ListEntry);
                VIOSockTxPktFree(pContext, pPkt);
                break;
            }
        }
    }

    WdfSpinLockRelease(pContext->TxLock);

    if (bKick)
        virtqueue_kick(pContext->TxVq);

}

//////////////////////////////////////////////////////////////////////////
__inline
ULONG32
VIOSockTxGetCredit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32 uCredit

)
{
    ULONG32 uRet;

    uRet = pSocket->peer_buf_alloc - (pSocket->tx_cnt - pSocket->peer_fwd_cnt);
    if (uRet > uCredit)
        uRet = uCredit;
    pSocket->tx_cnt += uRet;
    return uRet;
}


__inline
VOID
VIOSockTxPutCredit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32 uCredit

)
{
    pSocket->tx_cnt -= uCredit;
}

NTSTATUS
VIOSockTxEnqueue(
    IN PSOCKET_CONTEXT  pSocket,
    IN VIRTIO_VSOCK_OP  Op,
    IN ULONG32          Flags OPTIONAL,
    IN BOOLEAN          Reply,
    IN WDFREQUEST       Request OPTIONAL
)
{
    NTSTATUS            status;
    PDEVICE_CONTEXT     pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_TX_ENTRY   pTxEntry = NULL;
    ULONG               uLen;
    WDFMEMORY           Memory = WDF_NO_HANDLE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (Request == WDF_NO_HANDLE)
    {

        status = WdfMemoryCreateFromLookaside(pContext->TxMemoryList, &Memory);
        if (NT_SUCCESS(status))
        {
            pTxEntry = WdfMemoryGetBuffer(Memory, NULL);
            pTxEntry->Memory = Memory;
            pTxEntry->Request = WDF_NO_HANDLE;
            pTxEntry->len = 0;
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
        }
    }
    else
    {
    }

    if (!NT_SUCCESS(status))
        return status;

    ASSERT(pTxEntry);
    pTxEntry->Socket = pSocket->ThisSocket;
    pTxEntry->src_port = pSocket->src_port;
    pTxEntry->dst_cid = pSocket->dst_cid;
    pTxEntry->dst_port = pSocket->dst_port;
    pTxEntry->op = Op;
    pTxEntry->reply = Reply;
    pTxEntry->flags = Flags;

    WdfSpinLockAcquire(pContext->TxLock);

    uLen = VIOSockTxGetCredit(pSocket, pTxEntry->len);
    if (pTxEntry->len && !uLen)
    {
        ASSERT(pTxEntry->Request);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "No free space on peer\n");

        WdfSpinLockRelease(pContext->TxLock);
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Request != WDF_NO_HANDLE)
    {
    }

    InsertTailList(&pContext->TxList, &pTxEntry->ListEntry);
    WdfSpinLockRelease(pContext->TxLock);

    VIOSockTxVqProcess(pContext);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return status;
}

//////////////////////////////////////////////////////////////////////////
//
