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

EVT_WDF_IO_QUEUE_IO_WRITE   VIOSockWrite;
EVT_WDF_REQUEST_CANCEL      VIOSockTxEnqueueCancel;
EVT_WDF_TIMER               VIOSockTxTimerFunc;


#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockWriteQueueInit)
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
            LIST_ENTRY ListEntry;
            WDFREQUEST Request;
        };
    };
}VIOSOCK_TX_PKT, *PVIOSOCK_TX_PKT;

typedef enum _VIOSOCK_TX_STATE
{
    VioSockTxUninitialized = 0,
    VioSockTxEnqueued,
    VioSockTxReady,
    VioSockTxTimeout,
    VioSockTxStopped
}VIOSOCK_TX_STATE;

typedef struct _VIOSOCK_TX_ENTRY {
    LIST_ENTRY      ListEntry;
    WDFMEMORY       Memory;
    WDFREQUEST      Request;
    WDFFILEOBJECT   Socket;
//    VIOSOCK_TX_STATE State;

    ULONG64         dst_cid;
    ULONG32         src_port;
    ULONG32         dst_port;

    ULONG32         len;
    USHORT          op;
    BOOLEAN         reply;
    ULONG32         flags;
    USHORT          type;

    LONGLONG        Timeout; //100ns
}VIOSOCK_TX_ENTRY, *PVIOSOCK_TX_ENTRY;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSOCK_TX_ENTRY, GetRequestTxContext);

_Requires_lock_not_held_(pContext->TxLock)
VOID
VIOSockTxDequeue(
    PDEVICE_CONTEXT pContext
);

//////////////////////////////////////////////////////////////////////////
_Requires_lock_held_(pContext->TxLock)
static
PVIOSOCK_TX_PKT
VIOSockTxPktAlloc(
    IN PDEVICE_CONTEXT pContext,
    IN PVIOSOCK_TX_ENTRY pTxEntry
)
{
    PHYSICAL_ADDRESS PA;
    PVIOSOCK_TX_PKT pPkt;

    ASSERT(pContext->TxPktSliced);
    pPkt = pContext->TxPktSliced->get_slice(pContext->TxPktSliced, &PA);
    if (pPkt)
    {
        pPkt->PhysAddr = PA;
        pPkt->Transaction = WDF_NO_HANDLE;
        if (pTxEntry->Socket != WDF_NO_HANDLE)
        {
            VIOSockRxIncTxPkt(GetSocketContext(pTxEntry->Socket), &pPkt->Header);
        }
        pPkt->Header.src_cid = pContext->Config.guest_cid;
        pPkt->Header.dst_cid = pTxEntry->dst_cid;
        pPkt->Header.src_port = pTxEntry->src_port;
        pPkt->Header.dst_port = pTxEntry->dst_port;
        pPkt->Header.len = pTxEntry->len;
        pPkt->Header.type = pTxEntry->type;
        pPkt->Header.op = pTxEntry->op;
        pPkt->Header.flags = pTxEntry->flags;
        InterlockedIncrement(&pContext->TxPktAllocated);
    }
    return pPkt;
}

_Requires_lock_held_(pContext->TxLock)
__inline
VOID
VIOSockTxPktFree(
    IN PDEVICE_CONTEXT pContext,
    IN PVIOSOCK_TX_PKT pPkt
)
{
    pContext->TxPktSliced->return_slice(pContext->TxPktSliced, pPkt);
    InterlockedDecrement(&pContext->TxPktAllocated);
}

_Requires_lock_not_held_(pContext->TxLock)
VOID
VIOSockTxVqCleanup(
    IN PDEVICE_CONTEXT pContext
)
{
    PVIOSOCK_TX_PKT pPkt;
    LIST_ENTRY CompletionList, *CurrentItem;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    ASSERT(pContext->TxVq);

    InitializeListHead(&CompletionList);

    //drain queue
    WdfSpinLockAcquire(pContext->TxLock);


    while (pPkt = (PVIOSOCK_TX_PKT)virtqueue_detach_unused_buf(pContext->TxVq))
    {
        if (pPkt->Transaction != WDF_NO_HANDLE)
        {
            pPkt->Request = WdfDmaTransactionGetRequest(pPkt->Transaction);

            //postpone to complete transaction
            InsertTailList(&CompletionList, &pPkt->ListEntry);
        }
        else
        {
            //just free packet
            pPkt->Request = WDF_NO_HANDLE;
            VIOSockTxPktFree(pContext, pPkt);
        }
    }

    WdfSpinLockRelease(pContext->TxLock);

    for (CurrentItem = CompletionList.Flink;
        CurrentItem != &CompletionList;
        CurrentItem = CurrentItem->Flink)
    {
        pPkt = CONTAINING_RECORD(CurrentItem, VIOSOCK_TX_PKT, ListEntry);

        VirtIOWdfDeviceDmaTxComplete(&pContext->VDevice.VIODevice, pPkt->Transaction);
        if (pPkt->Request != WDF_NO_HANDLE)
            WdfRequestCompleteWithInformation(pPkt->Request, STATUS_INVALID_DEVICE_STATE, pPkt->Header.len);
    };

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

    pContext->TxPktSliced = VirtIOWdfDeviceAllocDmaMemorySliced(&pContext->VDevice.VIODevice,
        uBufferSize, sizeof(VIOSOCK_TX_PKT));

    ASSERT(pContext->TxPktSliced);
    if (!pContext->TxPktSliced)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "VirtIOWdfDeviceAllocDmaMemorySliced(%u bytes for TxPackets) failed\n", uBufferSize);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    pContext->TxPktNum = uNumEntries;

    return status;
}

_Requires_lock_held_(pContext->TxLock)
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

            uPktLen += SgList->Elements[i].Length;
            if (uPktLen >= pPkt->Header.len)
            {
                sg[++i].length -= uPktLen - pPkt->Header.len;
                break;
            }
        }
        uElements += i;
    }

    if (uElements > 1)
    {
        va_indirect = &pPkt->IndirectDescs;
        phys_indirect = pPkt->PhysAddr.QuadPart + FIELD_OFFSET(VIOSOCK_TX_PKT, IndirectDescs);
    }


    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "Send packet %!Op! (%d:%d --> %d:%d), len: %d, flags: %d, buf_alloc: %d, fwd_cnt: %d\n",
        pPkt->Header.op,
        (ULONG)pPkt->Header.src_cid, pPkt->Header.src_port,
        (ULONG)pPkt->Header.dst_cid, pPkt->Header.dst_port,
        pPkt->Header.len, pPkt->Header.flags, pPkt->Header.buf_alloc, pPkt->Header.fwd_cnt);

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

_Requires_lock_not_held_(pContext->TxLock)
VOID
VIOSockTxVqProcess(
    IN PDEVICE_CONTEXT pContext
)
{
    PVIOSOCK_TX_PKT pPkt;
    UINT len;
    LIST_ENTRY CompletionList, *CurrentItem;
    NTSTATUS status;
    WDFREQUEST Request;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->TxLock);
    do
    {
        virtqueue_disable_cb(pContext->TxVq);

        while ((pPkt = (PVIOSOCK_TX_PKT)virtqueue_get_buf(pContext->TxVq, &len)) != NULL)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "Free packet %!Op! (%d:%d --> %d:%d)\n",
                pPkt->Header.op, (ULONG)pPkt->Header.src_cid, pPkt->Header.src_port,
                (ULONG)pPkt->Header.dst_cid, pPkt->Header.dst_port);

            if (pPkt->Transaction != WDF_NO_HANDLE)
            {
                pPkt->Request = WdfDmaTransactionGetRequest(pPkt->Transaction);

                //postpone to complete transaction
                InsertTailList(&CompletionList, &pPkt->ListEntry);
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

    for (CurrentItem = CompletionList.Flink;
        CurrentItem != &CompletionList;
        CurrentItem = CurrentItem->Flink)
    {
        pPkt = CONTAINING_RECORD(CurrentItem, VIOSOCK_TX_PKT, ListEntry);

        VirtIOWdfDeviceDmaTxComplete(&pContext->VDevice.VIODevice, pPkt->Transaction);
        if (pPkt->Request != WDF_NO_HANDLE)
            WdfRequestCompleteWithInformation(pPkt->Request, STATUS_SUCCESS, pPkt->Header.len);
    };

    //cleanup pkt locked
    WdfSpinLockAcquire(pContext->TxLock);
    while (!IsListEmpty(&CompletionList))
    {
        VIOSockTxPktFree(pContext, CONTAINING_RECORD(RemoveHeadList(&CompletionList), VIOSOCK_TX_PKT, ListEntry));
    };
    WdfSpinLockRelease(pContext->TxLock);

    VIOSockTxDequeue(pContext);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static
BOOLEAN
VIOSockTxDequeueCallback(
    IN PVIRTIO_DMA_TRANSACTION_PARAMS pParams
)
{
    PVIOSOCK_TX_PKT pPkt = pParams->param1;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket((PSOCKET_CONTEXT)pParams->param2);
    BOOLEAN         bRes;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pContext->TxLock);
    bRes = VIOSockTxPktInsert(pContext, pPkt, pParams);
    if (!bRes)
        VIOSockTxPktFree(pContext, pPkt);
    WdfSpinLockRelease(pContext->TxLock);

    if (!bRes)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIOSockTxPktInsert failed\n");

        VirtIOWdfDeviceDmaTxComplete(&pContext->VDevice.VIODevice, pParams->transaction);
        WdfRequestComplete(pParams->req, STATUS_INSUFFICIENT_RESOURCES);
    }
    else
    {
        virtqueue_kick(pContext->TxVq);
    }

    return bRes;
}

_Requires_lock_not_held_(pContext->TxLock)
static
VOID
VIOSockTxDequeue(
    PDEVICE_CONTEXT pContext
)
{
    static volatile LONG    lInProgress;
    BOOLEAN                 bKick = FALSE, bReply, bRestartRx = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (InterlockedCompareExchange(&lInProgress, 1, 0) == 1)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "Another instance of VIOSockTxDequeue already running, stop tx dequeue\n");
        return; //one running instance allowed
    }

    WdfSpinLockAcquire(pContext->TxLock);

    while (!IsListEmpty(&pContext->TxList))
    {
        PVIOSOCK_TX_ENTRY   pTxEntry = CONTAINING_RECORD(pContext->TxList.Flink,
            VIOSOCK_TX_ENTRY, ListEntry);
        PVIOSOCK_TX_PKT     pPkt = VIOSockTxPktAlloc(pContext, pTxEntry);
        NTSTATUS            status;

        //can't allocate packet, stop dequeue
        if (!pPkt)
            break;

        RemoveHeadList(&pContext->TxList);
        InterlockedDecrement(&pContext->TxEnqueued);
        //pTxEntry->State = VioSockTxReady;

        bReply = pTxEntry->reply;

        if (pTxEntry->Request)
        {
            ASSERT(pTxEntry->Socket != WDF_NO_HANDLE);
            ASSERT(pTxEntry->len && !bReply);
            status = WdfRequestUnmarkCancelable(pTxEntry->Request);

            if (NT_SUCCESS(status))
            {
                PSOCKET_CONTEXT pSocket = GetSocketContext(pTxEntry->Socket);
                VIRTIO_DMA_TRANSACTION_PARAMS params = { 0 };

                if (pTxEntry->Timeout)
                    VIOSockTimerDeref(&pContext->TxTimer, TRUE);


                WdfSpinLockRelease(pContext->TxLock);

                status = VIOSockStateValidate(pSocket, TRUE);
                if (status == STATUS_REMOTE_DISCONNECT)
                    status = STATUS_LOCAL_DISCONNECT;

                if (NT_SUCCESS(status))
                {
                    params.req = pTxEntry->Request;

                    params.param1 = pPkt;
                    params.param2 = pSocket;

                    //create transaction
                    if (!VirtIOWdfDeviceDmaTxAsync(&pContext->VDevice.VIODevice, &params, VIOSockTxDequeueCallback))
                    {
                        if (params.transaction)
                            VirtIOWdfDeviceDmaTxComplete(&pContext->VDevice.VIODevice, params.transaction);
                        status = STATUS_INSUFFICIENT_RESOURCES;
                    }
                }

                if (!NT_SUCCESS(status))
                    WdfRequestComplete(pTxEntry->Request, status);

                WdfSpinLockAcquire(pContext->TxLock);

                if (!NT_SUCCESS(status))
                    VIOSockTxPktFree(pContext, pPkt);
            }
            else
            {
                ASSERT(status == STATUS_CANCELLED);
                TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE, "Write request canceled\n");
                InitializeListHead(&pTxEntry->ListEntry);//cancellation routine removes element from the list
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
                InterlockedIncrement(&pContext->TxEnqueued);
                //pTxEntry->State = VioSockTxEnqueued;

                VIOSockTxPktFree(pContext, pPkt);
                bReply = FALSE;
                break;
            }

            if (bReply)
            {
                LONG lVal = --pContext->TxQueuedReply;

                /* Do we now have resources to resume rx processing? */
                bRestartRx = (lVal + 1 == pContext->RxPktNum);
            }
        }
    }

    InterlockedExchange(&lInProgress, 0);

    WdfSpinLockRelease(pContext->TxLock);

    if (bKick)
        virtqueue_kick(pContext->TxVq);

    if (bRestartRx)
        VIOSockRxVqProcess(pContext);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

_Requires_lock_not_held_(pContext->TxLock)
VOID
VIOSockTxCleanup(
    PDEVICE_CONTEXT pContext,
    WDFFILEOBJECT   Socket,
    NTSTATUS        Status
)
{
    LONG lCnt = 0;
    PLIST_ENTRY CurrentEntry;
    LIST_ENTRY  CompletionList;
    BOOLEAN     bProcessVq = FALSE, bAlwaysTrue = TRUE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->TxLock);

    for (CurrentEntry = pContext->TxList.Flink;
        CurrentEntry != &pContext->TxList;
        CurrentEntry = CurrentEntry->Flink)
    {
        PVIOSOCK_TX_ENTRY   pTxEntry = CONTAINING_RECORD(CurrentEntry,
            VIOSOCK_TX_ENTRY, ListEntry);

        if (Socket == WDF_NO_HANDLE || pTxEntry->Socket == Socket)
        {
            if (pTxEntry->Request)
            {
                if (!NT_SUCCESS(WdfRequestUnmarkCancelable(pTxEntry->Request)))
                    continue;
            }

            CurrentEntry = CurrentEntry->Blink;
            RemoveEntryList(&pTxEntry->ListEntry);

            InsertTailList(&CompletionList, &pTxEntry->ListEntry); //complete later

            if (pTxEntry->Timeout)
                VIOSockTimerDeref(&pContext->TxTimer, TRUE);

            if (pTxEntry->reply)
                ++lCnt;
        }
    }

    if (lCnt && Socket != WDF_NO_HANDLE)
    {
        pContext->TxQueuedReply -= lCnt;
        if (pContext->TxQueuedReply + lCnt >= (LONG)pContext->RxPktNum &&
            pContext->TxQueuedReply < (LONG)pContext->RxPktNum)
            bProcessVq = TRUE;
    }

    WdfSpinLockRelease(pContext->TxLock);

    //Static Driver Verifier(SDV) tracks only one request, and when SDV unwinds the loop,
    //it treats completion of the second request as another completion of the first request.
    //The 'assume' below causes SDV to skip loop analysis.
    __analysis_assume(bAlwaysTrue == FALSE);

    if (bAlwaysTrue)
    {
        while (!IsListEmpty(&CompletionList))
        {
            PVIOSOCK_TX_ENTRY pTxEntry = CONTAINING_RECORD(RemoveHeadList(&CompletionList),
                VIOSOCK_TX_ENTRY, ListEntry);

            if (pTxEntry->Request)
                WdfRequestComplete(pTxEntry->Request, Status);

            if (pTxEntry->Memory)
                WdfObjectDelete(pTxEntry->Memory);
        }
    }

    if (bProcessVq)
    {
        VIOSockRxVqProcess(pContext);
    }
}

//////////////////////////////////////////////////////////////////////////
static
VOID
VIOSockTxEnqueueCancel(
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_TX_ENTRY pTxEntry = GetRequestTxContext(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pContext->TxLock);
    RemoveEntryList(&pTxEntry->ListEntry);
    VIOSockTxPutCredit(pSocket, pTxEntry->len);

    if (pTxEntry->Timeout)
        VIOSockTimerDeref(&pContext->TxTimer, TRUE);

    WdfSpinLockRelease(pContext->TxLock);

    WdfRequestComplete(Request, STATUS_CANCELLED);
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

    if (IsLoopbackSocket(pSocket))
        return VIOSockLoopbackTxEnqueue(pSocket, Op, Flags, Request,
        (Request == WDF_NO_HANDLE) ? 0 : GetRequestTxContext(Request)->len);

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
        status = VIOSockStateValidate(pSocket, TRUE);
        if (status == STATUS_REMOTE_DISCONNECT)
            status = STATUS_LOCAL_DISCONNECT;

        if (NT_SUCCESS(status))
        {
            pTxEntry = GetRequestTxContext(Request);
            pTxEntry->Request = Request;
            pTxEntry->Memory = WDF_NO_HANDLE;
        }
    }

    if (!NT_SUCCESS(status))
        return status;

    ASSERT(pTxEntry);
    pTxEntry->Socket = pSocket->ThisSocket;
    pTxEntry->src_port = pSocket->src_port;
    pTxEntry->dst_cid = pSocket->dst_cid;
    pTxEntry->dst_port = pSocket->dst_port;
    pTxEntry->type = (USHORT)pSocket->type;
    pTxEntry->op = Op;
    pTxEntry->reply = Reply;
    pTxEntry->flags = Flags;
    pTxEntry->Timeout = 0;

    uLen = VIOSockTxGetCredit(pSocket, pTxEntry->len);
    if (pTxEntry->len && !uLen)
    {
        ASSERT(pTxEntry->Request);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "No free space on peer\n");

        return STATUS_BUFFER_TOO_SMALL;
    }

    WdfSpinLockAcquire(pContext->TxLock);

    if (Request != WDF_NO_HANDLE)
    {
        pTxEntry->len = uLen;
        status = WdfRequestMarkCancelableEx(Request, VIOSockTxEnqueueCancel);
        if (!NT_SUCCESS(status))
        {
            ASSERT(status == STATUS_CANCELLED);
            TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE, "Write request canceled: 0x%x\n", status);

            VIOSockTxPutCredit(pSocket, pTxEntry->len);
            WdfSpinLockRelease(pContext->TxLock);

            if (pTxEntry->Memory != WDF_NO_HANDLE)
                WdfObjectDelete(pTxEntry->Memory);

            return status; //caller completes failed requests
        }

        if (pSocket->SendTimeout != LONG_MAX)
        {
            pTxEntry->Timeout = WDF_ABS_TIMEOUT_IN_MS(pSocket->SendTimeout);
            VIOSockTimerStart(&pContext->TxTimer, pTxEntry->Timeout);
        }
    }

    if (pTxEntry->reply)
        pContext->TxQueuedReply++;

    InterlockedIncrement(&pContext->TxEnqueued);
    InsertTailList(&pContext->TxList, &pTxEntry->ListEntry);
    //pTxEntry->State = VioSockTxEnqueued;

    WdfSpinLockRelease(pContext->TxLock);

    VIOSockTxVqProcess(pContext);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return status;
}

//////////////////////////////////////////////////////////////////////////
static
BOOLEAN
VIOSockWriteIsAvailable(
    PDEVICE_CONTEXT pContext,
    PSOCKET_CONTEXT pSocket
)
{
    if (VIOSockIsNonBlocking(pSocket))
    {
        //do not enqueue request for non-blocking socket if queue if full
        if (pContext->TxEnqueued + pContext->TxPktAllocated >= pContext->TxPktNum)
            return FALSE;
    }
    return TRUE;
}

static
VOID
VIOSockWrite(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
)
{
    PDEVICE_CONTEXT         pContext = GetDeviceContext(WdfIoQueueGetDevice(Queue));
    NTSTATUS                status;
    WDF_OBJECT_ATTRIBUTES   attributes;
    PSOCKET_CONTEXT         pSocket = GetSocketContextFromRequest(Request);
    PVIOSOCK_TX_ENTRY       pRequest;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (IsControlRequest(Request))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE, "Invalid socket %d for write\n", pSocket->SocketId);

        WdfRequestComplete(Request, STATUS_NOT_SOCKET);
        return;
    }

    if (Length > VIRTIO_VSOCK_MAX_PKT_BUF_SIZE)
        Length = VIRTIO_VSOCK_MAX_PKT_BUF_SIZE;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        VIOSOCK_TX_ENTRY
    );
    status = WdfObjectAllocateContext(
        Request,
        &attributes,
        &pRequest
    );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfObjectAllocateContext failed: 0x%x\n", status);

        WdfRequestComplete(Request, status);
        return;
    }
    else
    {
        pRequest->len = (ULONG32)Length;
    }

    if (IsLoopbackSocket(pSocket) || VIOSockWriteIsAvailable(pContext, pSocket))
        status = VIOSockSendWrite(pSocket, Request);
    else
        status = STATUS_CANT_WAIT;


    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "VIOSockSendWrite failed for socket %d: 0x%x\n",
            pSocket->SocketId, status);
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VIOSockWriteQueueInit(
    IN WDFDEVICE hDevice
)
{
    PDEVICE_CONTEXT              pContext = GetDeviceContext(hDevice);
    WDF_IO_QUEUE_CONFIG          queueConfig;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES lockAttributes, memAttributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = pContext->ThisDevice;

    status = WdfSpinLockCreate(
        &lockAttributes,
        &pContext->TxLock
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfSpinLockCreate failed: 0x%x\n", status);
        return FALSE;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&memAttributes);
    memAttributes.ParentObject = pContext->ThisDevice;

    status = WdfLookasideListCreate(&memAttributes,
        sizeof(VIOSOCK_TX_ENTRY), NonPagedPoolNx,
        &memAttributes, VIOSOCK_DRIVER_MEMORY_TAG,
        &pContext->TxMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "WdfLookasideListCreate failed: 0x%x\n", status);
        return status;
    }

    InitializeListHead(&pContext->TxList);

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchParallel
    );

    queueConfig.EvtIoWrite = VIOSockWrite;
    queueConfig.AllowZeroLengthRequests = WdfFalse;
    queueConfig.PowerManaged = WdfFalse;

    //
    // By default, Static Driver Verifier (SDV) displays a warning if it
    // doesn't find the EvtIoStop callback on a power-managed queue.
    // The 'assume' below causes SDV to suppress this warning.
    //
    // No need to handle EvtIoStop:

    __analysis_assume(queueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->WriteQueue
    );
    __analysis_assume(queueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "WdfIoQueueCreate failed (Write Queue): 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(hDevice,
        pContext->WriteQueue,
        WdfRequestTypeWrite);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "WdfDeviceConfigureRequestDispatching failed (Write Queue): 0x%x\n", status);
        return status;
    }

    VIOSockTimerCreate(&pContext->TxTimer, pContext->ThisDevice, VIOSockTxTimerFunc);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "VIOSockTimerCreate failed (Write Queue): 0x%x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

VOID
VIOSockWriteIoSuspend(
    IN PDEVICE_CONTEXT pContext
)
{
    //stop handling write requests and cleanup queue
    WdfIoQueuePurge(pContext->WriteQueue, NULL, WDF_NO_HANDLE);

    VIOSockTxCleanup(pContext, WDF_NO_HANDLE, STATUS_INVALID_DEVICE_STATE);
}

VOID
VIOSockWriteIoRestart(
    IN PDEVICE_CONTEXT pContext
)
{
    WdfIoQueueStart(pContext->WriteQueue);
}

//////////////////////////////////////////////////////////////////////////
_Requires_lock_not_held_(pContext->TxLock)
NTSTATUS
VIOSockSendResetNoSock(
    IN PDEVICE_CONTEXT pContext,
    IN PVIRTIO_VSOCK_HDR pHeader
)
{
    PVIOSOCK_TX_ENTRY   pTxEntry;
    NTSTATUS            status;
    WDFMEMORY           Memory;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    /* Send RST only if the original pkt is not a RST pkt */
    if (pHeader->op == VIRTIO_VSOCK_OP_RST)
        return STATUS_SUCCESS;

    status = WdfMemoryCreateFromLookaside(pContext->TxMemoryList, &Memory);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
        return status;
    }

    pTxEntry = WdfMemoryGetBuffer(Memory, NULL);
    pTxEntry->Memory = Memory;
    pTxEntry->Request = WDF_NO_HANDLE;
    pTxEntry->len = 0;

    pTxEntry->src_port = pHeader->dst_port;
    pTxEntry->dst_cid = pHeader->src_cid;
    pTxEntry->dst_port = pHeader->src_port;
    pTxEntry->type = pHeader->type;

    pTxEntry->Socket = WDF_NO_HANDLE;
    pTxEntry->op = VIRTIO_VSOCK_OP_RST;
    pTxEntry->reply = FALSE;
    pTxEntry->flags = 0;

    WdfSpinLockAcquire(pContext->TxLock);
    InsertTailList(&pContext->TxList, &pTxEntry->ListEntry);
    WdfSpinLockRelease(pContext->TxLock);

    VIOSockTxVqProcess(pContext);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return status;
}

//////////////////////////////////////////////////////////////////////////
VOID
VIOSockTxTimerFunc(
    WDFTIMER Timer
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfTimerGetParentObject(Timer));
    PLIST_ENTRY CurrentEntry;
    LONGLONG Timeout = LONGLONG_MAX;
    LIST_ENTRY CompletionList;
    BOOLEAN SetTimer = FALSE, bAlwaysTrue = TRUE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->TxLock);

    for (CurrentEntry = pContext->TxList.Flink;
        CurrentEntry != &pContext->TxList;
        CurrentEntry = CurrentEntry->Flink)
    {
        PVIOSOCK_TX_ENTRY pTxEntry = CONTAINING_RECORD(CurrentEntry,
            VIOSOCK_TX_ENTRY, ListEntry);

        if (pTxEntry->Timeout)
        {
            if (pTxEntry->Timeout <= pContext->TxTimer.Timeout + VIOSOCK_TIMER_TOLERANCE)
            {
                CurrentEntry = CurrentEntry->Blink;
                RemoveEntryList(&pTxEntry->ListEntry);
                InterlockedDecrement(&pContext->TxEnqueued);
                //pTxEntry->State = VioSockTxTimeout;

                ASSERT(pTxEntry->Request);
                if (pTxEntry->Request)
                {
                    NTSTATUS status = WdfRequestUnmarkCancelable(pTxEntry->Request);
                    if (NT_SUCCESS(status))
                    {
                        InsertTailList(&CompletionList, &pTxEntry->ListEntry);
                        VIOSockTimerDeref(&pContext->TxTimer, FALSE);
                    }
                    else
                    {
                        ASSERT(status == STATUS_CANCELLED);
                        TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE, "Write request canceled\n");
                        InitializeListHead(&pTxEntry->ListEntry);//cancellation routine removes element from the list
                    }
                }
                else
                {
                    ASSERT(pTxEntry->Memory);
                    WdfObjectDelete(pTxEntry->Memory);
                    VIOSockTimerDeref(&pContext->TxTimer, FALSE);
                }
            }
            else
            {
                SetTimer = TRUE;

                pTxEntry->Timeout -= pContext->TxTimer.Timeout;

                if (pTxEntry->Timeout < Timeout)
                    Timeout = pTxEntry->Timeout;
            }
        }
    }

    if (SetTimer)
        VIOSockTimerSet(&pContext->TxTimer, Timeout);

    WdfSpinLockRelease(pContext->TxLock);

    //Static Driver Verifier(SDV) tracks only one request, and when SDV unwinds the loop,
    //it treats completion of the second request as another completion of the first request.
    //The 'assume' below causes SDV to skip loop analysis.
    __analysis_assume(bAlwaysTrue == FALSE);

    if (bAlwaysTrue)
    {
        while (!IsListEmpty(&CompletionList))
        {
            PVIOSOCK_TX_ENTRY pTxEntry = CONTAINING_RECORD(RemoveHeadList(&CompletionList),
                VIOSOCK_TX_ENTRY, ListEntry);

            WdfRequestComplete(pTxEntry->Request, STATUS_TIMEOUT);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}