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

EVT_WDF_IO_QUEUE_IO_WRITE VIOSockWrite;
EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE VIOSockWriteIoCanceledOnQueue;
EVT_WDF_TIMER VIOSockWriteTimerFunc;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VIOSockWriteQueueInit)
#pragma alloc_text(PAGE, VIOSockWriteSocketQueueInit)
#endif

typedef struct _VIOSOCK_TX_PKT
{
    VIRTIO_VSOCK_HDR Header;
    PHYSICAL_ADDRESS PhysAddr; // packet addr
    // Producing entry; its buffers must outlive the flight, so it is
    // released only when the host consumes the packet.
    PVIOSOCK_TX_ENTRY TxEntry;
    union {
        BYTE IndirectDescs[SIZE_OF_SINGLE_INDIRECT_DESC *
                           (1 + (VIOSOCK_DMA_TX_PAGES + 1))]; // Header + sglist(maybe not aligned)
        LIST_ENTRY ListEntry; // consumed-packet completion list (IndirectDescs are done by then)
    };
} VIOSOCK_TX_PKT, *PVIOSOCK_TX_PKT;

// Separate two-page lookaside for small chunks (acks, tail/credit-squeezed
// bites) instead of wasting a 64K buffer; matches the lib's small-send
// threshold, but not coupled to it.
#define VIOSOCK_TX_SMALL_BUFFER_SIZE (2 * PAGE_SIZE)

typedef struct _VIOSOCK_TX_CONTEXT
{
    // In-flight zero-copy chunks + 1 slicer bias held until the request is
    // fully sliced (including while parked in the WriteQueue), so the counter
    // cannot hit zero while the request is framework-owned. Whoever
    // decrements to zero completes the request.
    LONG RefCounter;
    BOOLEAN ZeroCopy; // snapshot at forward: MSG_ZEROCOPY in the SEND_EX params on a non-loopback socket
    ULONG BufferLen;
    ULONG BytesToWrite;
    PVOID ToWritePtr;
    PMDL CurrentMdl;
    ULONG MdlBytesToWrite;

    union {
        struct
        {
            VIOSOCK_TIMER_ENTRY TimerEntry;
            LONG Counter; // timer-scan generation (see VIOSockWriteTimerFunc)
        };
        struct
        {
            LIST_ENTRY ListEntry; // expired-request completion list of the timer scan
            WDFREQUEST ThisRequest;
        };
    };
} VIOSOCK_TX_CONTEXT, *PVIOSOCK_TX_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSOCK_TX_CONTEXT, GetRequestTxContext);

// Drop one reference on a send request; the one who reaches zero completes it.
// Sliced bytes are committed to the wire, so any progress completes as success
// with the byte count (an error would make the application resend them);
// Status applies only when nothing was sliced. The caller stops the deadline
// timer BEFORE the deref — the deref never touches it (the union arm may be gone).
static VOID VIOSockTxRequestComplete(IN WDFREQUEST Request, IN NTSTATUS Status)
{
    PVIOSOCK_TX_CONTEXT pRequest = GetRequestTxContext(Request);

    if (InterlockedDecrement(&pRequest->RefCounter) == 0)
    {
        ULONG uBytesWritten = pRequest->BufferLen - pRequest->BytesToWrite;

        WdfRequestCompleteWithInformation(Request, uBytesWritten ? STATUS_SUCCESS : Status, uBytesWritten);
    }
}

_Requires_lock_not_held_(pContext->TxLock) VOID VIOSockTxDequeue(PDEVICE_CONTEXT pContext);

static VOID VIOSockTxEntryUnsentSub(IN PVIOSOCK_TX_ENTRY pTxEntry);

//////////////////////////////////////////////////////////////////////////
_Requires_lock_held_(pContext->TxLock) static PVIOSOCK_TX_PKT VIOSockTxPktAlloc(IN PDEVICE_CONTEXT pContext,
                                                                                IN PVIOSOCK_TX_ENTRY pTxEntry)
{
    PHYSICAL_ADDRESS PA;
    PVIOSOCK_TX_PKT pPkt;

    ASSERT(pContext->TxPktSliced);
    pPkt = pContext->TxPktSliced->get_slice(pContext->TxPktSliced, &PA);
    if (pPkt)
    {
        pPkt->PhysAddr = PA;
        pPkt->TxEntry = NULL;
        pPkt->Header.buf_alloc = pTxEntry->buf_alloc;
        pPkt->Header.fwd_cnt = pTxEntry->fwd_cnt;
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

_Requires_lock_held_(pContext->TxLock) __inline VOID VIOSockTxPktFree(IN PDEVICE_CONTEXT pContext,
                                                                      IN PVIOSOCK_TX_PKT pPkt)
{
    pContext->TxPktSliced->return_slice(pContext->TxPktSliced, pPkt);
    InterlockedDecrement(&pContext->TxPktAllocated);
}

_Requires_lock_not_held_(pContext->TxLock) VOID VIOSockTxVqCleanup(IN PDEVICE_CONTEXT pContext)
{
    PVIOSOCK_TX_PKT pPkt;
    LIST_ENTRY CompletionList, *CurrentItem;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    ASSERT(pContext->TxVq);

    InitializeListHead(&CompletionList);

    // drain queue
    WdfSpinLockAcquire(pContext->TxLock);

    while (pPkt = (PVIOSOCK_TX_PKT)virtqueue_detach_unused_buf(pContext->TxVq))
    {
        if (pPkt->TxEntry)
        {
            // postpone the entry release: it may complete a zero-copy request
            InsertTailList(&CompletionList, &pPkt->ListEntry);
        }
        else
        {
            // just free packet
            VIOSockTxPktFree(pContext, pPkt);
        }
    }

    WdfSpinLockRelease(pContext->TxLock);

    for (CurrentItem = CompletionList.Flink; CurrentItem != &CompletionList; CurrentItem = CurrentItem->Flink)
    {
        PVIOSOCK_TX_ENTRY pTxEntry;
        BOOLEAN bAlwaysTrue = TRUE;

        pPkt = CONTAINING_RECORD(CurrentItem, VIOSOCK_TX_PKT, ListEntry);
        pTxEntry = pPkt->TxEntry;

        // Packets never reached the host: zero-copy chunks still complete
        // through the refcount, buffers go back to the lookasides.
        //
        // Hidden from SDV (here and in the other consumption-side completions):
        // it cannot track pTxEntry->Request through the lookaside buffer, so
        // the field aliases whatever request its one-request model holds and
        // the completion gets flagged as invalidreqaccess. Real ownership is
        // refcount-based (only zero-copy sets pTxEntry->Request).
        __analysis_assume(bAlwaysTrue == FALSE);

        if (bAlwaysTrue && pTxEntry->Request != WDF_NO_HANDLE)
        {
            VIOSockTxRequestComplete(pTxEntry->Request, STATUS_SUCCESS);
        }

        VIOSockTxEntryUnsentSub(pTxEntry);
        VIOSockTxFreeEntry(pTxEntry);
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
VIOSockTxVqInit(IN PDEVICE_CONTEXT pContext)
{
    NTSTATUS status = STATUS_SUCCESS;
    USHORT uNumEntries;
    ULONG uBufferSize;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    uNumEntries = (USHORT)virtio_get_queue_size(pContext->TxVq);

    uBufferSize = sizeof(VIOSOCK_TX_PKT) * uNumEntries;

    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_HW_ACCESS,
                "Allocating sliced buffer of %u bytes for %u Tx packets\n",
                uBufferSize,
                uNumEntries);

    pContext->TxPktSliced = VirtIOWdfDeviceAllocDmaMemorySliced(&pContext->VDevice.VIODevice,
                                                                uBufferSize,
                                                                sizeof(VIOSOCK_TX_PKT));

    ASSERT(pContext->TxPktSliced);
    if (!pContext->TxPktSliced)
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_HW_ACCESS,
                    "VirtIOWdfDeviceAllocDmaMemorySliced(%u bytes for TxPackets) failed\n",
                    uBufferSize);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    pContext->TxPktNum = uNumEntries;

    return status;
}

_Requires_lock_held_(pContext->TxLock) static BOOLEAN VIOSockTxPktInsert(IN PDEVICE_CONTEXT pContext,
                                                                         IN PVIOSOCK_TX_PKT pPkt,
                                                                         IN PVIOSOCK_TX_ENTRY pTxEntry)
{
    VIOSOCK_SG_DESC sg[VIOSOCK_DMA_TX_PAGES + 2];
    ULONG uElements = 1;
    PVOID va_indirect = NULL;
    ULONGLONG phys_indirect = 0;

    int ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    sg[0].length = sizeof(VIRTIO_VSOCK_HDR);
    sg[0].physAddr.QuadPart = pPkt->PhysAddr.QuadPart + FIELD_OFFSET(VIOSOCK_TX_PKT, Header);

    // The payload spans come pre-built in the entry: the skb buffer pages for
    // copy chunks, the request MDL pages for zero-copy chunks, none for
    // commands. Their total length equals Header.len by construction.
    if (pTxEntry->SgCount)
    {
        ULONG i;

        ASSERT(pTxEntry->SgCount <= VIOSOCK_TX_SG_MAX_ELEMENTS);

        for (i = 0; i < pTxEntry->SgCount; i++)
        {
            sg[i + 1] = pTxEntry->BufferDesc.SgList[i];
        }
        uElements += pTxEntry->SgCount;
    }

    if (uElements > 1)
    {
        va_indirect = &pPkt->IndirectDescs;
        phys_indirect = pPkt->PhysAddr.QuadPart + FIELD_OFFSET(VIOSOCK_TX_PKT, IndirectDescs);
    }

    pPkt->TxEntry = pTxEntry;

    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_WRITE,
                "Send packet %!Op! (%u:%u --> %u:%u), len: %u, flags: %u, buf_alloc: %u, fwd_cnt: %u\n",
                pPkt->Header.op,
                (ULONG)pPkt->Header.src_cid,
                pPkt->Header.src_port,
                (ULONG)pPkt->Header.dst_cid,
                pPkt->Header.dst_port,
                pPkt->Header.len,
                pPkt->Header.flags,
                pPkt->Header.buf_alloc,
                pPkt->Header.fwd_cnt);

    ret = virtqueue_add_buf(pContext->TxVq, sg, uElements, 0, pPkt, va_indirect, phys_indirect);

    ASSERT(ret >= 0);
    if (ret < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "Error adding buffer to queue (ret = %d)\n", ret);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return TRUE;
}

_Requires_lock_not_held_(pContext->TxLock) VOID VIOSockTxVqProcess(IN PDEVICE_CONTEXT pContext)
{
    PVIOSOCK_TX_PKT pPkt;
    UINT len;
    LIST_ENTRY CompletionList, *CurrentItem;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->TxLock);
    do
    {
        virtqueue_disable_cb(pContext->TxVq);

        while ((pPkt = (PVIOSOCK_TX_PKT)virtqueue_get_buf(pContext->TxVq, &len)) != NULL)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION,
                        DBG_WRITE,
                        "Free packet %!Op! (%u:%u --> %u:%u)\n",
                        pPkt->Header.op,
                        (ULONG)pPkt->Header.src_cid,
                        pPkt->Header.src_port,
                        (ULONG)pPkt->Header.dst_cid,
                        pPkt->Header.dst_port);

            if (pPkt->TxEntry)
            {
                // postpone the entry release: it may complete a zero-copy
                // request, which must not happen under the spinlock
                InsertTailList(&CompletionList, &pPkt->ListEntry);
            }
            else
            {
                // just free packet
                VIOSockTxPktFree(pContext, pPkt);
            }
        }
    } while (!virtqueue_enable_cb(pContext->TxVq));

    WdfSpinLockRelease(pContext->TxLock);

    for (CurrentItem = CompletionList.Flink; CurrentItem != &CompletionList; CurrentItem = CurrentItem->Flink)
    {
        PVIOSOCK_TX_ENTRY pTxEntry;
        BOOLEAN bAlwaysTrue = TRUE;

        pPkt = CONTAINING_RECORD(CurrentItem, VIOSOCK_TX_PKT, ListEntry);
        pTxEntry = pPkt->TxEntry;

        // Host consumed the packet: drop the zero-copy request reference
        // (the last one completes the request), release the entry.
        // Hidden from SDV — see the consumption-side note in VIOSockTxVqCleanup
        __analysis_assume(bAlwaysTrue == FALSE);

        if (bAlwaysTrue && pTxEntry->Request != WDF_NO_HANDLE)
        {
            VIOSockTxRequestComplete(pTxEntry->Request, STATUS_SUCCESS);
        }
        VIOSockTxEntryUnsentSub(pTxEntry);
        VIOSockTxFreeEntry(pTxEntry);
    };

    // cleanup pkt locked
    WdfSpinLockAcquire(pContext->TxLock);
    while (!IsListEmpty(&CompletionList))
    {
        VIOSockTxPktFree(pContext, CONTAINING_RECORD(RemoveHeadList(&CompletionList), VIOSOCK_TX_PKT, ListEntry));
    };
    WdfSpinLockRelease(pContext->TxLock);

    VIOSockTxDequeue(pContext);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

_Requires_lock_not_held_(pContext->TxLock) static VOID VIOSockTxDequeue(PDEVICE_CONTEXT pContext)
{
    static volatile LONG lInProgress;
    BOOLEAN bKick = FALSE, bReply, bRestartRx = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (InterlockedCompareExchange(&lInProgress, 1, 0) == 1)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_WRITE,
                    "Another instance of VIOSockTxDequeue already running, stop tx dequeue\n");
        return; // one running instance allowed
    }

    WdfSpinLockAcquire(pContext->TxLock);

    while (!IsListEmpty(&pContext->TxList))
    {
        PVIOSOCK_TX_ENTRY pTxEntry = CONTAINING_RECORD(pContext->TxList.Flink, VIOSOCK_TX_ENTRY, ListEntry);
        PVIOSOCK_TX_PKT pPkt = VIOSockTxPktAlloc(pContext, pTxEntry);

        // can't allocate packet, stop dequeue
        if (!pPkt)
        {
            break;
        }

        RemoveHeadList(&pContext->TxList);

        bReply = pTxEntry->reply;

        if (!VIOSockTxPktInsert(pContext, pPkt, pTxEntry))
        {
            ASSERT(FALSE);
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIOSockTxPktInsert failed\n");

            InsertHeadList(&pContext->TxList, &pTxEntry->ListEntry);

            VIOSockTxPktFree(pContext, pPkt);
            break;
        }

        bKick = TRUE;

        if (bReply)
        {
            LONG lVal = --pContext->TxQueuedReply;

            /* Do we now have resources to resume rx processing? */
            bRestartRx = (lVal + 1 == pContext->RxPktNum);
        }
    }

    InterlockedExchange(&lInProgress, 0);

    WdfSpinLockRelease(pContext->TxLock);

    if (bKick)
    {
        virtqueue_kick(pContext->TxVq);
    }

    if (bRestartRx)
    {
        VIOSockRxVqProcess(pContext);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

_Requires_lock_not_held_(pContext->TxLock) VOID VIOSockTxCleanup(PDEVICE_CONTEXT pContext,
                                                                 WDFFILEOBJECT Socket,
                                                                 NTSTATUS Status)
{
    LONG lCnt = 0;
    PLIST_ENTRY CurrentEntry;
    LIST_ENTRY CompletionList;
    BOOLEAN bProcessVq = FALSE, bAlwaysTrue = TRUE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->TxLock);

    for (CurrentEntry = pContext->TxList.Flink; CurrentEntry != &pContext->TxList; CurrentEntry = CurrentEntry->Flink)
    {
        PVIOSOCK_TX_ENTRY pTxEntry = CONTAINING_RECORD(CurrentEntry, VIOSOCK_TX_ENTRY, ListEntry);

        if (Socket == WDF_NO_HANDLE || pTxEntry->Socket == Socket)
        {
            CurrentEntry = CurrentEntry->Blink;
            RemoveEntryList(&pTxEntry->ListEntry);

            InsertTailList(&CompletionList, &pTxEntry->ListEntry); // complete later

            if (pTxEntry->reply)
            {
                ++lCnt;
            }
        }
    }

    if (lCnt && Socket != WDF_NO_HANDLE)
    {
        pContext->TxQueuedReply -= lCnt;
        if (pContext->TxQueuedReply + lCnt >= (LONG)pContext->RxPktNum &&
            pContext->TxQueuedReply < (LONG)pContext->RxPktNum)
        {
            bProcessVq = TRUE;
        }
    }

    WdfSpinLockRelease(pContext->TxLock);

    // Static Driver Verifier(SDV) tracks only one request, and when SDV unwinds the loop,
    // it treats completion of the second request as another completion of the first request.
    // The 'assume' below causes SDV to skip loop analysis.
    __analysis_assume(bAlwaysTrue == FALSE);

    if (bAlwaysTrue)
    {
        while (!IsListEmpty(&CompletionList))
        {
            PVIOSOCK_TX_ENTRY pTxEntry = CONTAINING_RECORD(RemoveHeadList(&CompletionList),
                                                           VIOSOCK_TX_ENTRY,
                                                           ListEntry);

            // A dropped zero-copy chunk releases its request reference.
            // Hidden from SDV — see the consumption-side note in VIOSockTxVqCleanup
            if (pTxEntry->Request != WDF_NO_HANDLE)
            {
                VIOSockTxRequestComplete(pTxEntry->Request, Status);
            }

            VIOSockTxEntryUnsentSub(pTxEntry);
            VIOSockTxFreeEntry(pTxEntry);
        }
    }

    if (bProcessVq)
    {
        VIOSockRxVqProcess(pContext);
    }
}

//////////////////////////////////////////////////////////////////////////
static PVIOSOCK_TX_ENTRY VIOSockTxAllocateEntry(IN PDEVICE_CONTEXT pContext,
                                                IN PSOCKET_CONTEXT pSocket OPTIONAL,
                                                IN ULONG BufferLen)
{
    NTSTATUS status;
    PVIOSOCK_TX_ENTRY pTxEntry = NULL;
    WDFMEMORY Memory;
    BOOLEAN bSkipSgList = FALSE;

    status = WdfMemoryCreateFromLookaside(pContext->TxMemoryList, &Memory);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
        return NULL;
    }

    pTxEntry = WdfMemoryGetBuffer(Memory, NULL);
    pTxEntry->Memory = Memory;
    pTxEntry->Request = WDF_NO_HANDLE;
    pTxEntry->Buffer = NULL;
    pTxEntry->BufferMemory = WDF_NO_HANDLE;
    pTxEntry->SgCount = 0;
    pTxEntry->len = BufferLen;

    if (pSocket)
    {
        VIRTIO_VSOCK_HDR CreditHdr;

        pTxEntry->Socket = pSocket->ThisSocket;
        pTxEntry->src_port = pSocket->src_port;
        pTxEntry->dst_cid = pSocket->dst_cid;
        pTxEntry->dst_port = pSocket->dst_port;
        pTxEntry->type = (USHORT)pSocket->type;

        // Snapshot the credit fields (as Linux does at skb creation): the
        // entry never touches the socket afterwards; the Socket handle above
        // stays a plain cookie (loopback routing at insert, debugging).
        VIOSockRxIncTxPkt(pSocket, &CreditHdr);
        pTxEntry->buf_alloc = CreditHdr.buf_alloc;
        pTxEntry->fwd_cnt = CreditHdr.fwd_cnt;

        WdfObjectReference(pTxEntry->Socket);
        bSkipSgList = IsLoopbackSocket(pSocket);
    }
    else
    {
        pTxEntry->Socket = WDF_NO_HANDLE;
        pTxEntry->buf_alloc = 0;
        pTxEntry->fwd_cnt = 0;
    }

    if (BufferLen)
    {
        WDFMEMORY BufferMemory;
        PCHAR va;
        ULONG uRemaining;

        // WDFMEMORY remembers its parent lookaside, so the free needs no
        // extra bookkeeping.
        status = WdfMemoryCreateFromLookaside((BufferLen <= VIOSOCK_TX_SMALL_BUFFER_SIZE) ? pContext->TxSmallBufferMemoryList
                                                                                          : pContext->TxBufferMemoryList,
                                              &BufferMemory);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfMemoryCreateFromLookaside(skb) failed: 0x%x\n", status);
            VIOSockTxFreeEntry(pTxEntry);
            return NULL;
        }

        pTxEntry->BufferMemory = BufferMemory;
        pTxEntry->Buffer = WdfMemoryGetBuffer(BufferMemory, NULL);

        // we don't need sglist for loopback socket
        if (bSkipSgList)
        {
            return pTxEntry;
        }
        // Describe the buffer page by page for the virtqueue. NOTE: raw
        // physical addresses — fine while VIRTIO_F_ACCESS_PLATFORM is off.
        va = (PCHAR)pTxEntry->Buffer;
        uRemaining = BufferLen;
        while (uRemaining)
        {
            ULONG cb = min(uRemaining, PAGE_SIZE - BYTE_OFFSET(va));

            ASSERT(pTxEntry->SgCount < VIOSOCK_TX_SG_MAX_ELEMENTS);
            pTxEntry->BufferDesc.SgList[pTxEntry->SgCount].physAddr = MmGetPhysicalAddress(va);
            pTxEntry->BufferDesc.SgList[pTxEntry->SgCount].length = cb;
            ++pTxEntry->SgCount;

            va += cb;
            uRemaining -= cb;
        }
    }

    return pTxEntry;
}

VOID VIOSockTxFreeEntry(PVIOSOCK_TX_ENTRY pTxEntry)
{
    if (pTxEntry->BufferMemory != WDF_NO_HANDLE)
    {
        WdfObjectDelete(pTxEntry->BufferMemory);
    }

    if (pTxEntry->Socket != WDF_NO_HANDLE)
    {
        WdfObjectDereference(pTxEntry->Socket);
    }
    WdfObjectDelete(pTxEntry->Memory);
}

// Undo the SIOCOUTQ accounting of a wire data chunk once the host has
// consumed it (or the chunk is dropped at cleanup). Only sliced data entries
// carry both a socket cookie and a payload; loopback entries are never
// counted and never reach the callers of this helper.
static VOID VIOSockTxEntryUnsentSub(IN PVIOSOCK_TX_ENTRY pTxEntry)
{
    if (pTxEntry->Socket != WDF_NO_HANDLE && pTxEntry->len)
    {
        InterlockedExchangeAdd(&GetSocketContext(pTxEntry->Socket)->BytesUnsent, -(LONG)pTxEntry->len);
    }
}

static __inline VOID VIOSockTxEntrySetRequest(PVIOSOCK_TX_ENTRY pTxEntry, IN WDFREQUEST Request)
{
    pTxEntry->Request = Request;
    InterlockedIncrement(&GetRequestTxContext(Request)->RefCounter);
}

// Append a fully initialized entry to the global TxList. Returns TRUE when
// the caller should kick TxVq processing (a future fast path may kick itself
// and return FALSE).
static BOOLEAN VIOSockTxInsertEntry(IN PDEVICE_CONTEXT pContext, IN PVIOSOCK_TX_ENTRY pTxEntry)
{
    // The wire/loopback divergence point: a loopback entry is delivered
    // synchronously and never touches the TxList/vq machinery.
    if (pTxEntry->Socket != WDF_NO_HANDLE && IsLoopbackSocket(GetSocketContext(pTxEntry->Socket)))
    {
        VIOSockLoopbackProcessTxEntry(pTxEntry);
        return FALSE;
    }

    WdfSpinLockAcquire(pContext->TxLock);

    if (pTxEntry->reply)
    {
        pContext->TxQueuedReply++;
    }

    InsertTailList(&pContext->TxList, &pTxEntry->ListEntry);

    WdfSpinLockRelease(pContext->TxLock);

    return TRUE;
}

//////////////////////////////////////////////////////////////////////////
// The write-side twin of the VIOSockReadCopy* family: move one chunk from the
// request source into the entry, advancing the request cursor. Unlike the
// read side, a copied chunk cannot come out short (credit already taken, the
// SgList pre-sized), hence the NTSTATUS contract.

// Flat source (IRP_MJ_WRITE / buffered IOCTL_SOCKET_SEND): one mapped span
// behind ToWritePtr.
static NTSTATUS VIOSockWriteCopyFlat(IN PVIOSOCK_TX_CONTEXT pRequest, IN PVIOSOCK_TX_ENTRY pTxEntry, IN ULONG uLen)
{
    memcpy(pTxEntry->Buffer, pRequest->ToWritePtr, uLen);
    pRequest->ToWritePtr = (PCHAR)pRequest->ToWritePtr + uLen;

    return STATUS_SUCCESS;
}

// SG source (IOCTL_SOCKET_SEND_EX): walk the locked MDL chain segment by
// segment, mapping each next MDL on first touch (the mapping is cached in the
// MDL and released by MmUnlockPages at request cleanup).
static NTSTATUS VIOSockWriteCopySg(IN PVIOSOCK_TX_CONTEXT pRequest, IN PVIOSOCK_TX_ENTRY pTxEntry, IN ULONG uLen)
{
    PCHAR pDst = (PCHAR)pTxEntry->Buffer;

    while (uLen)
    {
        ULONG uChunk = (uLen < pRequest->MdlBytesToWrite) ? uLen : pRequest->MdlBytesToWrite;

        if (!uChunk)
        {
            // current MDL drained, switch the cursor to the next one
            PMDL pNextMdl = pRequest->CurrentMdl->Next;

            ASSERT(pNextMdl); // BytesToWrite != 0 means the chain has more data
            if (!pNextMdl)
            {
                return STATUS_INTERNAL_ERROR;
            }

            pRequest->ToWritePtr = VIOSockSgMapMdl(pNextMdl);
            if (!pRequest->ToWritePtr)
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "Failed to map the next Mdl\n");
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            pRequest->CurrentMdl = pNextMdl;
            pRequest->MdlBytesToWrite = MmGetMdlByteCount(pNextMdl);
            continue;
        }

        memcpy(pDst, pRequest->ToWritePtr, uChunk);
        pDst += uChunk;
        pRequest->ToWritePtr = (PCHAR)pRequest->ToWritePtr + uChunk;
        pRequest->MdlBytesToWrite -= uChunk;
        uLen -= uChunk;
    }

    return STATUS_SUCCESS;
}

// Zero-copy "copy": no data moves — the entry SgList is filled with the
// physical pages of the request MDL chain at the cursor. The chunk may
// legally come out SHORT: a scattered chain can exhaust the SG slots before
// uLen bytes are placed. *puLen returns the bytes actually placed; the caller
// gives the credit for the remainder back. Contiguous spans are merged.
static NTSTATUS VIOSockWriteCopyZeroCopy(IN PVIOSOCK_TX_CONTEXT pRequest,
                                         IN PVIOSOCK_TX_ENTRY pTxEntry,
                                         IN OUT PULONG puLen)
{
    ULONG uLen = *puLen;
    ULONG uPlaced = 0;

    ASSERT(pRequest->CurrentMdl);
    if (!pRequest->CurrentMdl)
    {
        return STATUS_INVALID_PARAMETER;
    }

    while (uLen)
    {
        PPFN_NUMBER pPfn;
        PHYSICAL_ADDRESS Pa;
        ULONG uMdlOffset, uPageOffset, cb;

        if (!pRequest->MdlBytesToWrite)
        {
            // current MDL drained, switch the cursor to the next one
            PMDL pNextMdl = pRequest->CurrentMdl->Next;

            ASSERT(pNextMdl); // BytesToWrite != 0 means the chain has more data
            if (!pNextMdl)
            {
                return STATUS_INTERNAL_ERROR;
            }

            pRequest->CurrentMdl = pNextMdl;
            pRequest->MdlBytesToWrite = MmGetMdlByteCount(pNextMdl);
        }

        // Position within the current MDL: the PFN array is indexed from the
        // page containing the MDL start (ByteOffset within that page).
        uMdlOffset = MmGetMdlByteOffset(pRequest->CurrentMdl) +
                     (MmGetMdlByteCount(pRequest->CurrentMdl) - pRequest->MdlBytesToWrite);
        pPfn = MmGetMdlPfnArray(pRequest->CurrentMdl);
        uPageOffset = uMdlOffset & (PAGE_SIZE - 1);

        cb = min(uLen, min(pRequest->MdlBytesToWrite, PAGE_SIZE - uPageOffset));

        Pa.QuadPart = ((ULONGLONG)pPfn[uMdlOffset >> PAGE_SHIFT] << PAGE_SHIFT) + uPageOffset;

        if (pTxEntry->SgCount &&
            pTxEntry->BufferDesc.SgList[pTxEntry->SgCount - 1].physAddr.QuadPart + pTxEntry->BufferDesc.SgList[pTxEntry->SgCount - 1].length ==
                                                                                                                (ULONGLONG)Pa.QuadPart)
        {
            // physically contiguous with the previous span — extend it
            pTxEntry->BufferDesc.SgList[pTxEntry->SgCount - 1].length += cb;
        }
        else if (pTxEntry->SgCount < VIOSOCK_TX_SG_MAX_ELEMENTS)
        {
            pTxEntry->BufferDesc.SgList[pTxEntry->SgCount].physAddr = Pa;
            pTxEntry->BufferDesc.SgList[pTxEntry->SgCount].length = cb;
            ++pTxEntry->SgCount;
        }
        else
        {
            // out of SG slots — close the chunk short, the caller returns the
            // credit for the unplaced remainder
            break;
        }

        pRequest->MdlBytesToWrite -= cb;
        uLen -= cb;
        uPlaced += cb;
    }

    // At least the first span always fits: the entry arrives with an empty SgList
    ASSERT(uPlaced);

    *puLen = uPlaced;
    return STATUS_SUCCESS;
}

static __inline NTSTATUS VIOSockWriteCopy(IN PVIOSOCK_TX_CONTEXT pRequest,
                                          IN PVIOSOCK_TX_ENTRY pTxEntry,
                                          IN OUT PULONG puLen)
{
    if (pRequest->ZeroCopy)
    {
        return VIOSockWriteCopyZeroCopy(pRequest, pTxEntry, puLen);
    }

    // The copying variants never shorten the chunk
    return pRequest->CurrentMdl ? VIOSockWriteCopySg(pRequest, pTxEntry, *puLen)
                                : VIOSockWriteCopyFlat(pRequest, pTxEntry, *puLen);
}

// One bite of the slicer: re-validate the socket (it may have died while the
// request was parked), take up to one packet worth of credit, push the chunk.
// The return status drives the pump loop:
//   STATUS_MORE_ENTRIES — chunk enqueued, bytes remain: bite again;
//   STATUS_SUCCESS      — request fully sliced;
//   STATUS_CANT_WAIT    — peer credit exhausted, nothing bitten (would-block:
//                         the NB path passes it to completion as is, the
//                         blocking path requeues);
//   anything else       — failure, credit returned; complete with it.
static NTSTATUS VIOSockWriteEnqueueRequest(IN PSOCKET_CONTEXT pSocket, IN WDFREQUEST Request)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_TX_CONTEXT pRequest = GetRequestTxContext(Request);
    PVIOSOCK_TX_ENTRY pTxEntry = NULL;
    ULONG uLen, uPlaced;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (!pRequest->BytesToWrite)
    {
        return STATUS_SUCCESS;
    }

    // REMOTE→LOCAL is the same EPIPE unification Linux sendmsg does (and
    // STATUS_REMOTE_DISCONNECT has no NtStatusToWsaError mapping at all).
    status = VIOSockStateValidate(pSocket, TRUE);
    if (status == STATUS_REMOTE_DISCONNECT)
    {
        status = STATUS_LOCAL_DISCONNECT;
    }
    else if (status == STATUS_CONNECTION_INVALID && VIOSockIsFlag(pSocket, SOCK_RESET))
    {
        // the peer reset the connection: WSAECONNRESET, not WSAENOTCONN
        status = STATUS_CONNECTION_RESET;
    }

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    uLen = VIOSockTxGetCredit(pSocket,
                              (pRequest->BytesToWrite < VIRTIO_VSOCK_MAX_PKT_BUF_SIZE) ? pRequest->BytesToWrite
                                                                                       : VIRTIO_VSOCK_MAX_PKT_BUF_SIZE);
    if (!uLen)
    {
        return STATUS_CANT_WAIT;
    }

    // a zero-copy entry needs no skb buffer: its SgList points at the request pages
    pTxEntry = VIOSockTxAllocateEntry(pContext, pSocket, pRequest->ZeroCopy ? 0 : uLen);
    if (!pTxEntry)
    {
        VIOSockTxPutCredit(pSocket, uLen);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pTxEntry->op = VIRTIO_VSOCK_OP_RW;
    pTxEntry->reply = FALSE;
    pTxEntry->flags = 0;

    uPlaced = uLen;
    status = VIOSockWriteCopy(pRequest, pTxEntry, &uPlaced);
    if (!NT_SUCCESS(status))
    {
        VIOSockTxFreeEntry(pTxEntry);
        VIOSockTxPutCredit(pSocket, uLen);
        return status;
    }

    if (uPlaced < uLen)
    {
        // zero-copy chunk closed short on SG capacity — return the extra credit
        VIOSockTxPutCredit(pSocket, uLen - uPlaced);
    }
    pTxEntry->len = uPlaced;

    if (pRequest->ZeroCopy)
    {
        // the chunk references the request pages — hold the request until consumed
        VIOSockTxEntrySetRequest(pTxEntry, Request);
    }

    pRequest->BytesToWrite -= uPlaced;

    if (!IsLoopbackSocket(pSocket))
    {
        // SIOCOUTQ accounting (Linux unsent_bytes parity): the chunk counts
        // from slicing until the host consumes it.
        InterlockedExchangeAdd(&pSocket->BytesUnsent, (LONG)pTxEntry->len);
    }

    if (VIOSockTxInsertEntry(pContext, pTxEntry))
    {
        VIOSockTxVqProcess(pContext);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return STATUS_MORE_ENTRIES;
}

NTSTATUS VIOSockTxEnqueueCommand(IN PSOCKET_CONTEXT pSocket,
                                 IN VIRTIO_VSOCK_OP Op,
                                 IN ULONG32 Flags OPTIONAL,
                                 IN BOOLEAN Reply)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_TX_ENTRY pTxEntry = NULL;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    pTxEntry = VIOSockTxAllocateEntry(pContext, pSocket, 0);
    if (!pTxEntry)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pTxEntry->op = Op;
    pTxEntry->reply = Reply;
    pTxEntry->flags = Flags;

    if (VIOSockTxInsertEntry(pContext, pTxEntry))
    {
        VIOSockTxVqProcess(pContext);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

// Park a blocking request back into the WriteQueue to wait for credit.
// Mirrors VIOSockReadRequeue: an expired deadline refuses the requeue with
// STATUS_IO_TIMEOUT, otherwise the deadline timer is re-armed.
_Requires_lock_held_(pSocket->TxLock) static NTSTATUS VIOSockWriteRequeue(IN PSOCKET_CONTEXT pSocket,
                                                                          IN WDFREQUEST Request)
{
    NTSTATUS status;
    PVIOSOCK_TX_CONTEXT pRequest = GetRequestTxContext(Request);

    if (VIOSockTimerEntryIsExpired(&pRequest->TimerEntry))
    {
        return STATUS_IO_TIMEOUT;
    }

    status = WdfRequestRequeue(Request);

    ASSERT(NT_SUCCESS(status));

    if (NT_SUCCESS(status))
    {
        // continue timer
        if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry))
        {
            VIOSockTimerStartDeadline(&pSocket->WriteTimer, NULL, pRequest->TimerEntry.Deadline);
        }
    }

    return status;
}

_Requires_lock_not_held_(pSocket->TxLock) static BOOLEAN VIOSockWriteProcessNextRequest(IN PSOCKET_CONTEXT pSocket)
{
    WDFREQUEST Request;
    PVIOSOCK_TX_CONTEXT pRequest;
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    status = WdfIoQueueRetrieveNextRequest(pSocket->WriteQueue, &Request);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_WRITE,
                    "The write queue is empty, exiting from VIOSockWriteProcessNextRequest\n");
        return FALSE;
    }

    pRequest = GetRequestTxContext(Request);

    // the deadline timer pauses while the request is driver-owned
    WdfSpinLockAcquire(pSocket->TxLock);
    if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry))
    {
        VIOSockTimerDeref(&pSocket->WriteTimer, TRUE);
    }
    WdfSpinLockRelease(pSocket->TxLock);

    do
    {
        status = VIOSockWriteEnqueueRequest(pSocket, Request);
    } while (status == STATUS_MORE_ENTRIES);

    if (status == STATUS_CANT_WAIT)
    {
        // Peer credit exhausted, nothing bitten on the last pass.
        if (VIOSockIsNonBlocking(pSocket))
        {
            // FD_WRITE is edge-triggered: drop it and arm the re-record.
            // The clear must precede the arm so a concurrent Rx re-arm cannot
            // be wiped; a transient miss leaves SOCK_TX_WAIT armed and
            // self-heals on the next inbound packet.
            VIOSockEventClearBitLocked(pSocket, FD_WRITE_BIT);
            VIOSockSetFlag(pSocket, SOCK_TX_WAIT);

            // CANT_WAIT rides into the completion below: partial progress
            // completes as success with the byte count, zero progress as
            // WSAEWOULDBLOCK. Keep draining the queue.
        }
        else
        {
            WdfSpinLockAcquire(pSocket->TxLock);
            status = VIOSockWriteRequeue(pSocket, Request);
            WdfSpinLockRelease(pSocket->TxLock);

            if (NT_SUCCESS(status))
            {
                return FALSE;
            }
            // expired deadline or a failed requeue: complete below
        }
    }

    // Drop the slicer bias; the last reference completes the request.
    VIOSockTxRequestComplete(Request, status);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return TRUE;
}

static __inline BOOLEAN VIOSockWriteEnqueueAlreadyActive(IN PSOCKET_CONTEXT pSocket)
{
    return InterlockedIncrement(&pSocket->WriteEnqueueInProgress) != 1;
}

static __inline VOID VIOSockWriteEnqueueReset(IN PSOCKET_CONTEXT pSocket)
{
    InterlockedExchange(&pSocket->WriteEnqueueInProgress, 1);
}

static __inline BOOLEAN VIOSockWriteEnqueueWorkArrived(IN PSOCKET_CONTEXT pSocket)
{
    return (BOOLEAN)(InterlockedCompareExchange(&pSocket->WriteEnqueueInProgress, 0, 1) != 1);
}

_Requires_lock_not_held_(pSocket->TxLock) VOID VIOSockWriteProcessQueue(IN PSOCKET_CONTEXT pSocket)
{
    BOOLEAN bRestart;

    // Single-instance enqueue worker: the 0->1 bump elects the owner, any
    // later caller only bumps the counter and returns, so triggers never
    // recurse or run concurrently. The owner re-arms at the top of each pass
    // and stops only when the queue is drained AND no bump arrived meanwhile.
    if (VIOSockWriteEnqueueAlreadyActive(pSocket))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "Write enqueue worker already running, bump and return\n");
        return;
    }

    do
    {
        VIOSockWriteEnqueueReset(pSocket);
        bRestart = VIOSockWriteProcessNextRequest(pSocket);
    } while (bRestart || VIOSockWriteEnqueueWorkArrived(pSocket));
}

//////////////////////////////////////////////////////////////////////////
// Park a data-carrying send request (IRP_MJ_WRITE, IOCTL_SOCKET_SEND[_EX]) in
// the per-socket write queue, where it stays framework-owned (cancellable)
// until the slicer pump retrieves it.
_Requires_lock_not_held_(pSocket->TxLock) static NTSTATUS VIOSockWriteForward(IN PSOCKET_CONTEXT pSocket,
                                                                              IN WDFREQUEST Request,
                                                                              IN ULONG Flags)
{
    PVIOSOCK_SG_CTX pSgCtx;
    PVIOSOCK_TX_CONTEXT pRequest;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_REQUEST_PARAMETERS Parameters;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (VIOSockIsFlag(pSocket, SOCK_CONTROL))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_WRITE, "Invalid socket %d for write\n", pSocket->SocketId);
        return STATUS_NOT_SOCKET;
    }

    if (Flags & MSG_ZEROCOPY)
    {
        // Refuse zero-copy on a non-blocking socket rather than silently
        // degrade to a copy: the completion inherently waits for host
        // consumption and the flag is an explicit no-copy contract.
        // Deliberately stricter than Linux (there it is a best-effort hint).
        if (VIOSockIsNonBlocking(pSocket))
        {
            TraceEvents(TRACE_LEVEL_WARNING,
                        DBG_WRITE,
                        "Zero-copy send on non-blocking socket %d refused",
                        pSocket->SocketId);
            return STATUS_CANT_WAIT;
        }
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VIOSOCK_TX_CONTEXT);
    status = WdfObjectAllocateContext(Request, &attributes, &pRequest);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return status;
    }

    pRequest->ZeroCopy = FALSE;
    pSgCtx = GetRequestSgContext(Request);
    if (pSgCtx)
    {
        // Loopback silently degrades to the copy path — delivery there is a
        // synchronous copy anyway, pinning pages would buy nothing.
        pRequest->ZeroCopy = (Flags & MSG_ZEROCOPY) && !IsLoopbackSocket(pSocket);

        if (!pRequest->ZeroCopy)
        {
            // only the copying path needs a system mapping
            pRequest->ToWritePtr = VIOSockSgMapMdl(pSgCtx->MdlChain);
            if (!pRequest->ToWritePtr)
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "Failed to map the first Mdl\n");
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        pRequest->CurrentMdl = pSgCtx->MdlChain;
        pRequest->MdlBytesToWrite = MmGetMdlByteCount(pSgCtx->MdlChain);
        pRequest->BufferLen = pRequest->BytesToWrite = pSgCtx->TotalLen;
    }
    else
    {
        SIZE_T Length;

        status = WdfRequestRetrieveInputBuffer(Request, 0, &pRequest->ToWritePtr, &Length);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
            return status;
        }

        // CurrentMdl/MdlBytesToWrite stay zero (WDF zero-inits the context)
        pRequest->BufferLen = pRequest->BytesToWrite = (ULONG)Length;
    }

    // the slicer bias — see VIOSOCK_TX_CONTEXT.RefCounter
    pRequest->RefCounter = 1;
    VIOSockTimerEntryInit(&pRequest->TimerEntry);

    if (!VIOSockIsNonBlocking(pSocket) && pSocket->SendTimeout != MAXLONGLONG)
    {
        pRequest->Counter = 0;

        WdfSpinLockAcquire(pSocket->TxLock);
        VIOSockTimerStartTimeout(&pSocket->WriteTimer, &pRequest->TimerEntry, pSocket->SendTimeout);
        WdfSpinLockRelease(pSocket->TxLock);
    }

    status = WdfRequestForwardToIoQueue(Request, pSocket->WriteQueue);
    if (NT_SUCCESS(status))
    {
        ULONG QueueRequests;

        // No ready-notify callback (recursion — see VIOSockReadForward):
        // start the pump ourselves on the empty->non-empty edge; a running
        // pump pass picks the request up on its own.
        WdfIoQueueGetState(pSocket->WriteQueue, &QueueRequests, NULL);
        if (QueueRequests == 1)
        {
            VIOSockWriteProcessQueue(pSocket);
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfRequestForwardToIoQueue failed: 0x%x\n", status);
        if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry))
        {
            WdfSpinLockAcquire(pSocket->TxLock);
            VIOSockTimerDeref(&pSocket->WriteTimer, TRUE);
            WdfSpinLockRelease(pSocket->TxLock);
        }
    }

    return status;
}

static VOID VIOSockWrite(IN WDFQUEUE Queue, IN WDFREQUEST Request, IN size_t Length)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(Length);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    status = VIOSockWriteForward(GetSocketContextFromRequest(Request), Request, 0);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

// IOCTL_SOCKET_SEND handler: the buffered small-send path, the input buffer
// is the bare payload.
NTSTATUS
VIOSockSendBuffered(IN WDFREQUEST Request)
{
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    status = VIOSockWriteForward(GetSocketContextFromRequest(Request), Request, 0);
    if (NT_SUCCESS(status))
    {
        status = STATUS_PENDING;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

// IOCTL_SOCKET_SEND_EX handler: the scatter-gather send path — the WSABUF
// chain was captured and locked in caller context (VIOSockSgPrepare).
NTSTATUS
VIOSockSendEx(IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PVIOSOCK_SG_CTX pSgCtx = GetRequestSgContext(Request);
    PVIRTIO_VSOCK_SEND_RECV_PARAMS pParams;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    // No SG context means the IOCTL somehow bypassed EvtIoInCallerContext.
    if (!pSgCtx)
    {
        return STATUS_INVALID_PARAMETER;
    }

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pParams), &pParams, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    status = VIOSockWriteForward(pSocket, Request, pParams->dwFlags);
    if (NT_SUCCESS(status))
    {
        status = STATUS_PENDING;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

VOID VIOSockWriteIoCanceledOnQueue(IN WDFQUEUE Queue, IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PVIOSOCK_TX_CONTEXT pRequest = GetRequestTxContext(Request);

    UNREFERENCED_PARAMETER(Queue);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "Write request canceled on queue\n");

    // A parked request always holds its deadline-timer reference — stop it
    // before the deref (the deref may clobber the union arm on completion).
    WdfSpinLockAcquire(pSocket->TxLock);
    if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry))
    {
        VIOSockTimerDeref(&pSocket->WriteTimer, TRUE);
    }
    WdfSpinLockRelease(pSocket->TxLock);

    // Drop the slicer bias: untouched completes as CANCELLED, partially
    // sliced as success with the committed bytes (possibly later, when the
    // in-flight zero-copy chunks drain). No further slicing either way.
    VIOSockTxRequestComplete(Request, STATUS_CANCELLED);
}

NTSTATUS
VIOSockWriteSocketQueueInit(IN PSOCKET_CONTEXT pSocket)
{
    WDFDEVICE hDevice = WdfFileObjectGetDevice(pSocket->ThisSocket);
    WDF_OBJECT_ATTRIBUTES queueAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

    queueConfig.EvtIoCanceledOnQueue = VIOSockWriteIoCanceledOnQueue;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    // Power-managed like the per-socket read queue: a send waiting for credit
    // parks here framework-owned. The pump never holds a request driver-owned
    // for an indeterminate time, so EvtIoStop can be omitted (the assume
    // below suppresses the false-positive SDV warning).
    queueConfig.PowerManaged = WdfTrue;

    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ParentObject = pSocket->ThisSocket;

    __analysis_assume(queueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(hDevice, &queueConfig, &queueAttributes, &pSocket->WriteQueue);
    __analysis_assume(queueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfIoQueueCreate failed (Socket Write Queue): 0x%x\n", status);
    }

    status = VIOSockTimerCreate(&pSocket->WriteTimer, pSocket->ThisSocket, VIOSockWriteTimerFunc);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "VIOSockTimerCreate failed (Socket Write Queue): 0x%x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOSockWriteQueueInit(IN WDFDEVICE hDevice)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    WDF_IO_QUEUE_CONFIG queueConfig;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES lockAttributes, memAttributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = pContext->ThisDevice;

    status = WdfSpinLockCreate(&lockAttributes, &pContext->TxLock);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfSpinLockCreate failed: 0x%x\n", status);
        return FALSE;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&memAttributes);
    memAttributes.ParentObject = pContext->ThisDevice;

    status = WdfLookasideListCreate(&memAttributes,
                                    sizeof(VIOSOCK_TX_ENTRY),
                                    NonPagedPoolNx,
                                    &memAttributes,
                                    VIOSOCK_DRIVER_MEMORY_TAG,
                                    &pContext->TxMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfLookasideListCreate failed: 0x%x\n", status);
        return status;
    }

    status = WdfLookasideListCreate(&memAttributes,
                                    VIRTIO_VSOCK_MAX_PKT_BUF_SIZE,
                                    NonPagedPoolNx,
                                    &memAttributes,
                                    VIOSOCK_DRIVER_MEMORY_TAG,
                                    &pContext->TxBufferMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfLookasideListCreate(skb) failed: 0x%x\n", status);
        return status;
    }

    status = WdfLookasideListCreate(&memAttributes,
                                    VIOSOCK_TX_SMALL_BUFFER_SIZE,
                                    NonPagedPoolNx,
                                    &memAttributes,
                                    VIOSOCK_DRIVER_MEMORY_TAG,
                                    &pContext->TxSmallBufferMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfLookasideListCreate(small skb) failed: 0x%x\n", status);
        return status;
    }

    InitializeListHead(&pContext->TxList);

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchParallel);

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
    status = WdfIoQueueCreate(hDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &pContext->WriteQueue);
    __analysis_assume(queueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfIoQueueCreate failed (Write Queue): 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(hDevice, pContext->WriteQueue, WdfRequestTypeWrite);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_WRITE,
                    "WdfDeviceConfigureRequestDispatching failed (Write Queue): 0x%x\n",
                    status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

VOID VIOSockWriteIoSuspend(IN PDEVICE_CONTEXT pContext)
{
    // stop handling write requests and cleanup queue
    WdfIoQueuePurge(pContext->WriteQueue, NULL, WDF_NO_HANDLE);

    VIOSockTxCleanup(pContext, WDF_NO_HANDLE, STATUS_INVALID_DEVICE_STATE);
}

VOID VIOSockWriteIoRestart(IN PDEVICE_CONTEXT pContext)
{
    WdfIoQueueStart(pContext->WriteQueue);
}

//////////////////////////////////////////////////////////////////////////
_Requires_lock_not_held_(pContext->TxLock) NTSTATUS VIOSockSendResetNoSock(IN PDEVICE_CONTEXT pContext,
                                                                           IN PVIRTIO_VSOCK_HDR pHeader)
{
    PVIOSOCK_TX_ENTRY pTxEntry;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    /* Send RST only if the original pkt is not a RST pkt */
    if (pHeader->op == VIRTIO_VSOCK_OP_RST)
    {
        return STATUS_SUCCESS;
    }

    pTxEntry = VIOSockTxAllocateEntry(pContext, NULL, 0);
    if (!pTxEntry)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "VIOSockTxAllocateEntry failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pTxEntry->src_port = pHeader->dst_port;
    pTxEntry->dst_cid = pHeader->src_cid;
    pTxEntry->dst_port = pHeader->src_port;
    pTxEntry->type = pHeader->type;

    pTxEntry->op = VIRTIO_VSOCK_OP_RST;
    pTxEntry->reply = FALSE;
    pTxEntry->flags = 0;

    if (VIOSockTxInsertEntry(pContext, pTxEntry))
    {
        VIOSockTxVqProcess(pContext);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
// SO_SNDTIMEO scan over the parked write requests — the write-side twin of
// VIOSockReadTimerFunc: take ownership of the expired ones, dispose outside
// the lock. Disposal is a deref, not a completion: only an untouched request
// gets STATUS_IO_TIMEOUT, partial progress finishes as success.
VOID VIOSockWriteTimerFunc(WDFTIMER Timer)
{
    static LONG lCounter;
    PSOCKET_CONTEXT pSocket = GetSocketContext(WdfTimerGetParentObject(Timer));
    LONGLONG MinRemainingTicks = MAXLONGLONG;
    WDFREQUEST PrevTagRequest = WDF_NO_HANDLE, TagRequest = WDF_NO_HANDLE, Request;
    NTSTATUS status;
    LIST_ENTRY CompletionList;
    PVIOSOCK_TX_CONTEXT pRequest;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    InterlockedIncrement(&lCounter);
    InitializeListHead(&CompletionList);

    do
    {
        status = WdfIoQueueFindRequest(pSocket->WriteQueue, PrevTagRequest, WDF_NO_HANDLE, NULL, &TagRequest);

        if (PrevTagRequest != WDF_NO_HANDLE)
        {
            WdfObjectDereference(PrevTagRequest);
        }

        if (NT_SUCCESS(status))
        {
            pRequest = GetRequestTxContext(TagRequest);

            WdfSpinLockAcquire(pSocket->TxLock);

            if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry) && pRequest->Counter < lCounter)
            {
                LONGLONG llRemainingTicks = VIOSockTimerEntryRemainingTicks(&pRequest->TimerEntry);
                if (llRemainingTicks <= VIOSOCK_TIMER_TOLERANCE_TICKS)
                {
                    status = WdfIoQueueRetrieveFoundRequest(pSocket->WriteQueue, TagRequest, &Request);

                    WdfObjectDereference(TagRequest);

                    if (status == STATUS_NOT_FOUND)
                    {
                        TagRequest = PrevTagRequest = WDF_NO_HANDLE;
                        status = STATUS_SUCCESS;
                    }
                    else if (!NT_SUCCESS(status))
                    {
                        TraceEvents(TRACE_LEVEL_ERROR,
                                    DBG_WRITE,
                                    "WdfIoQueueRetrieveFoundRequest failed: 0x%08x\n",
                                    status);
                        WdfSpinLockRelease(pSocket->TxLock);
                        break;
                    }
                    else
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "Dispose expired queued Tx request %p\n", Request);

                        VIOSockTimerDeref(&pSocket->WriteTimer, FALSE);
                        InsertTailList(&CompletionList, &pRequest->ListEntry);
                        pRequest->ThisRequest = Request;
                    }
                }
                else
                {
                    if (llRemainingTicks < MinRemainingTicks)
                    {
                        MinRemainingTicks = llRemainingTicks;
                    }

                    pRequest->Counter = lCounter;
                    PrevTagRequest = TagRequest;
                }
            }

            WdfSpinLockRelease(pSocket->TxLock);
        }
        else if (status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        else if (status == STATUS_NOT_FOUND)
        {
            TagRequest = PrevTagRequest = WDF_NO_HANDLE;
            status = STATUS_SUCCESS;
        }
    } while (NT_SUCCESS(status));

    if (MinRemainingTicks != MAXLONGLONG)
    {
        WdfSpinLockAcquire(pSocket->TxLock);
        VIOSockTimerSetDeadline(&pSocket->WriteTimer, VIOSockTimerTicksToDeadline(MinRemainingTicks));
        WdfSpinLockRelease(pSocket->TxLock);
    }

    while (!IsListEmpty(&CompletionList))
    {
        pRequest = CONTAINING_RECORD(RemoveHeadList(&CompletionList), VIOSOCK_TX_CONTEXT, ListEntry);
        VIOSockTxRequestComplete(pRequest->ThisRequest, STATUS_IO_TIMEOUT);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE,
                DBG_WRITE,
                "<-- %s, status: 0x%08x, counter: %u\n",
                __FUNCTION__,
                status,
                lCounter);
}
