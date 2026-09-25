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

#define VIOSOCK_DMA_RX_PAGES   1 // contiguous buffer

#define VIOSOCK_BYTES_TO_MERGE 128 // max bytes to merge with prev buffer

#define VIOSOCK_CB_ENTRIES(n)  ((n) + (n >> 1)) // default chained buffer queue size

typedef struct _VIOSOCK_RX_PKT
{
    VIRTIO_VSOCK_HDR Header;
    PVIOSOCK_RX_CB Buffer; // Chained buffer
    union {
        BYTE IndirectDescs[SIZE_OF_SINGLE_INDIRECT_DESC * (1 + VIOSOCK_DMA_RX_PAGES)]; // Header + buffer
        SINGLE_LIST_ENTRY RxPktListEntry;
        LIST_ENTRY CompletionListEntry;
    };
} VIOSOCK_RX_PKT, *PVIOSOCK_RX_PKT;

typedef struct _VIOSOCK_RX_CONTEXT
{
    ULONG BufferLen;
    ULONG FreeBytes;
    // SG (RECV_EX) cursor over the locked MDL chain; non-NULL CurrentMdl marks an
    // SG request. Kept outside the union: completion paths repurpose the union
    // arms but still must tell SG from flat to set completion information.
    PMDL CurrentMdl;
    ULONG MdlFreeBytes; // free bytes left in CurrentMdl
    union {
        struct
        {
            VIOSOCK_TIMER_ENTRY TimerEntry;
            LONG Counter;
            ULONG Flags;
            PCHAR FreePtr;
        };
        struct
        {
            LIST_ENTRY ListEntry;
            WDFREQUEST ThisRequest;
        };
    };
} VIOSOCK_RX_CONTEXT, *PVIOSOCK_RX_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSOCK_RX_CONTEXT, GetRequestRxContext);

BOOLEAN
VIOSockRxCbInit(IN PDEVICE_CONTEXT pContext);

BOOLEAN
VIOSockRxCbAdd(IN PDEVICE_CONTEXT pContext);

VOID VIOSockRxCbCleanup(IN PDEVICE_CONTEXT pContext);

EVT_WDF_IO_QUEUE_IO_READ VIOSockRead;
EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE VIOSockReadIoCanceledOnQueue;
EVT_WDF_TIMER VIOSockReadTimerFunc;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VIOSockRxCbInit)
#pragma alloc_text(PAGE, VIOSockRxCbAdd)
#pragma alloc_text(PAGE, VIOSockRxCbCleanup)
#pragma alloc_text(PAGE, VIOSockReadQueueInit)
#pragma alloc_text(PAGE, VIOSockReadSocketQueueInit)
#endif

//////////////////////////////////////////////////////////////////////////
_Requires_lock_held_(pContext->RxLock) __inline VOID VIOSockRxCbPush(PDEVICE_CONTEXT pContext, PVIOSOCK_RX_CB pCb)
{
    PushEntryList(&pContext->RxCbBuffers, &pCb->FreeListEntry);
}

_Requires_lock_not_held_(pContext->RxLock) __inline VOID VIOSockRxCbPushLocked(PDEVICE_CONTEXT pContext,
                                                                               PVIOSOCK_RX_CB pCb)
{
    WdfSpinLockAcquire(pContext->RxLock);
    VIOSockRxCbPush(pContext, pCb);
    WdfSpinLockRelease(pContext->RxLock);
}

_Requires_lock_held_(pContext->RxLock) __inline PVIOSOCK_RX_CB VIOSockRxCbPop(IN PDEVICE_CONTEXT pContext)
{
    PSINGLE_LIST_ENTRY pListEntry = PopEntryList(&pContext->RxCbBuffers);

    if (pListEntry)
    {
        return CONTAINING_RECORD(pListEntry, VIOSOCK_RX_CB, FreeListEntry);
    }

    return NULL;
}

__inline VOID VIOSockRxCbFree(IN PDEVICE_CONTEXT pContext, IN PVIOSOCK_RX_CB pCb)
{
    ASSERT(pCb && pCb->BufferVA);

    ASSERT(pCb->BufferPA.QuadPart && pCb->TxEntry == NULL);

    if (pCb->BufferPA.QuadPart)
    {
        VirtIOWdfDeviceFreeDmaMemory(&pContext->VDevice.VIODevice, pCb->BufferVA);
    }

    WdfObjectDelete(pCb->Memory);
}

static BOOLEAN VIOSockRxCbAdd(IN PDEVICE_CONTEXT pContext)
{
    NTSTATUS status;
    PVIOSOCK_RX_CB pCb;
    WDFMEMORY Memory;
    BOOLEAN bRes = FALSE;

    PAGED_CODE();

    status = WdfMemoryCreateFromLookaside(pContext->RxCbBufferMemoryList, &Memory);
    if (NT_SUCCESS(status))
    {
        pCb = WdfMemoryGetBuffer(Memory, NULL);

        RtlZeroMemory(pCb, sizeof(*pCb));

        pCb->Memory = Memory;
        pCb->BufferVA = VirtIOWdfDeviceAllocDmaMemory(&pContext->VDevice.VIODevice,
                                                      VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE,
                                                      VIOSOCK_DRIVER_MEMORY_TAG);

        if (!pCb->BufferVA)
        {
            TraceEvents(TRACE_LEVEL_ERROR,
                        DBG_HW_ACCESS,
                        "VirtIOWdfDeviceAllocDmaMemory(%u bytes for Rx buffer) failed\n",
                        VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE);
            WdfObjectDelete(pCb->Memory);
        }
        else
        {
            pCb->BufferPA = VirtIOWdfDeviceGetPhysicalAddress(&pContext->VDevice.VIODevice, pCb->BufferVA);

            ASSERT(pCb->BufferPA.QuadPart);

            if (!pCb->BufferPA.QuadPart)
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfDeviceGetPhysicalAddress failed\n");
                VIOSockRxCbFree(pContext, pCb);
            }
            else
            {
                // no need to lock, init call
                VIOSockRxCbPush(pContext, pCb);
                bRes = TRUE;
            }
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
    }

    return bRes;
}

static BOOLEAN VIOSockRxCbInit(IN PDEVICE_CONTEXT pContext)
{
    ULONG i;
    BOOLEAN bRes = TRUE;
    WDF_OBJECT_ATTRIBUTES lockAttributes, memAttributes;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pContext->RxCbBuffersNum = VIOSOCK_CB_ENTRIES(pContext->RxPktNum);

    if (pContext->RxCbBuffersNum < pContext->RxPktNum)
    {
        pContext->RxCbBuffersNum = pContext->RxPktNum;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&memAttributes);
    memAttributes.ParentObject = pContext->ThisDevice;

    status = WdfLookasideListCreate(&memAttributes,
                                    sizeof(VIOSOCK_RX_CB),
                                    NonPagedPoolNx,
                                    &memAttributes,
                                    VIOSOCK_DRIVER_MEMORY_TAG,
                                    &pContext->RxCbBufferMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfLookasideListCreate failed: 0x%x\n", status);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_READ,
                "Initialize chained buffer with %u entries\n",
                pContext->RxCbBuffersNum);

    pContext->RxCbBuffers.Next = NULL;

    for (i = 0; i < pContext->RxCbBuffersNum; ++i)
    {
        if (!VIOSockRxCbAdd(pContext))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "VIOSockRxCbAdd failed, cleanup chained buffer\n");
            bRes = FALSE;
            break;
        }
    }

    if (!bRes)
    {
        VIOSockRxCbCleanup(pContext);
    }

    return bRes;
}

static VOID VIOSockRxCbCleanup(IN PDEVICE_CONTEXT pContext)
{
    PVIOSOCK_RX_CB pCb;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    // no need to lock
    while (pCb = VIOSockRxCbPop(pContext))
    {
        VIOSockRxCbFree(pContext, pCb);
    }
}

//////////////////////////////////////////////////////////////////////////
__inline VOID VIOSockRxPktCleanup(IN PDEVICE_CONTEXT pContext, IN PVIOSOCK_RX_PKT pPkt)
{
    ASSERT(pPkt);

    if (pPkt->Buffer)
    {
        VIOSockRxCbPush(pContext, pPkt->Buffer);
        pPkt->Buffer = NULL;
    }
}

C_ASSERT((VIOSOCK_DMA_RX_PAGES + 1) == 2);

#define VIOSOCK_PKT_PA_FROM_VA(ctx, pva)                                                                               \
    (((ctx)->RxPktPA.QuadPart) + (ULONGLONG)((PCHAR)(pva) - (PCHAR)((ctx)->RxPktVA)))

_Requires_lock_held_(pContext->RxLock) static BOOLEAN VIOSockRxPktInsert(IN PDEVICE_CONTEXT pContext,
                                                                         IN PVIOSOCK_RX_PKT pPkt)
{
    BOOLEAN bRes = TRUE;
    VIOSOCK_SG_DESC sg[VIOSOCK_DMA_RX_PAGES + 1];
    PHYSICAL_ADDRESS pPKtPA;
    int ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    if (!pPkt->Buffer)
    {
        pPkt->Buffer = VIOSockRxCbPop(pContext);
        if (!pPkt->Buffer)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "VIOSockRxCbPop returns NULL\n");
            return FALSE;
        }
    }

    ASSERT(pPkt->Buffer->TxEntry == NULL);

    pPKtPA.QuadPart = VIOSOCK_PKT_PA_FROM_VA(pContext, pPkt);

    sg[0].length = sizeof(VIRTIO_VSOCK_HDR);
    sg[0].physAddr.QuadPart = pPKtPA.QuadPart + FIELD_OFFSET(VIOSOCK_RX_PKT, Header);

    sg[1].length = VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE;
    sg[1].physAddr.QuadPart = pPkt->Buffer->BufferPA.QuadPart;

    ret = virtqueue_add_buf(pContext->RxVq,
                            sg,
                            0,
                            2,
                            pPkt,
                            &pPkt->IndirectDescs,
                            pPKtPA.QuadPart + FIELD_OFFSET(VIOSOCK_RX_PKT, IndirectDescs));

    ASSERT(ret >= 0);
    if (ret < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "Error adding buffer to Rx queue (ret = %d)\n", ret);
        VIOSockRxPktCleanup(pContext, pPkt);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return TRUE;
}

_Requires_lock_held_(pContext->RxLock) __inline VOID VIOSockRxPktListProcess(IN PDEVICE_CONTEXT pContext)
{
    PSINGLE_LIST_ENTRY pListEntry;

    while (pListEntry = PopEntryList(&pContext->RxPktList))
    {
        PVIOSOCK_RX_PKT pPkt = CONTAINING_RECORD(pListEntry, VIOSOCK_RX_PKT, RxPktListEntry);

        if (!VIOSockRxPktInsert(pContext, pPkt))
        {
            PushEntryList(&pContext->RxPktList, &pPkt->RxPktListEntry);
            break;
        }
    }
}

_Requires_lock_not_held_(pContext->RxLock) __inline VOID VIOSockRxPktListProcessLockedNotify(IN PDEVICE_CONTEXT pContext)
{
    WdfSpinLockAcquire(pContext->RxLock);
    VIOSockRxPktListProcess(pContext);
    bool bNotify = virtqueue_kick_prepare(pContext->RxVq);
    WdfSpinLockRelease(pContext->RxLock);

    if (bNotify)
    {
        virtqueue_notify(pContext->RxVq);
    }
}

_Requires_lock_not_held_(pContext->RxLock) __inline VOID VIOSockRxPktInsertOrPostpone(IN PDEVICE_CONTEXT pContext,
                                                                                      IN PVIOSOCK_RX_PKT pPkt)
{
    bool bNotify = false;
    WdfSpinLockAcquire(pContext->RxLock);
    if (!VIOSockRxPktInsert(pContext, pPkt))
    {
        // postpone packet
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "Postpone packet\n");
        PushEntryList(&pContext->RxPktList, &pPkt->RxPktListEntry);
    }
    else
    {
        VIOSockRxPktListProcess(pContext);
        bNotify = virtqueue_kick_prepare(pContext->RxVq);
    }
    WdfSpinLockRelease(pContext->RxLock);

    if (bNotify)
    {
        virtqueue_notify(pContext->RxVq);
    }
}

_Requires_lock_held_(pSocket->RxLock) __inline BOOLEAN VIOSockRxPktInc(IN PSOCKET_CONTEXT pSocket, IN ULONG uPktLen)
{
    if (pSocket->RxBytes + uPktLen > pSocket->buf_alloc)
    {
        return FALSE;
    }

    pSocket->RxBytes += uPktLen;
    return TRUE;
}

_Requires_lock_held_(pSocket->RxLock) __inline VOID VIOSockRxPktDec(IN PSOCKET_CONTEXT pSocket, IN ULONG uPktLen)
{
    pSocket->RxBytes -= uPktLen;
    pSocket->fwd_cnt += uPktLen;
}

_Requires_lock_not_held_(pContext->RxLock) VOID VIOSockRxVqCleanup(IN PDEVICE_CONTEXT pContext)
{
    PVIOSOCK_RX_PKT pPkt;
    PSINGLE_LIST_ENTRY pListEntry;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    ASSERT(pContext->RxVq && pContext->RxPktVA);

    // drain queue
    WdfSpinLockAcquire(pContext->RxLock);
    while (pPkt = (PVIOSOCK_RX_PKT)virtqueue_detach_unused_buf(pContext->RxVq))
    {
        VIOSockRxPktCleanup(pContext, pPkt);
    }

    while (pListEntry = PopEntryList(&pContext->RxPktList))
    {
        VIOSockRxPktCleanup(pContext, CONTAINING_RECORD(pListEntry, VIOSOCK_RX_PKT, RxPktListEntry));
    }
    WdfSpinLockRelease(pContext->RxLock);

    VIOSockRxCbCleanup(pContext);

    if (pContext->RxPktVA)
    {
        VirtIOWdfDeviceFreeDmaMemory(&pContext->VDevice.VIODevice, pContext->RxPktVA);
        pContext->RxPktVA = NULL;
    }

    pContext->RxVq = NULL;
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VIOSockRxVqInit(IN PDEVICE_CONTEXT pContext)
{
    NTSTATUS status = STATUS_SUCCESS;
    USHORT uNumEntries;
    ULONG uBufferSize;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    pContext->RxPktList.Next = NULL;

    uNumEntries = (USHORT)virtio_get_queue_size(pContext->RxVq);

    pContext->RxPktNum = uNumEntries;

    uBufferSize = sizeof(VIOSOCK_RX_PKT) * uNumEntries;

    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_HW_ACCESS,
                "Allocating common buffer of %u bytes for %u Rx packets\n",
                uBufferSize,
                uNumEntries);

    pContext->RxPktVA = (PVIOSOCK_RX_PKT)VirtIOWdfDeviceAllocDmaMemory(&pContext->VDevice.VIODevice,
                                                                       uBufferSize,
                                                                       VIOSOCK_DRIVER_MEMORY_TAG);

    ASSERT(pContext->RxPktVA);
    if (!pContext->RxPktVA)
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_HW_ACCESS,
                    "VirtIOWdfDeviceAllocDmaMemory(%u bytes for RxPackets) failed\n",
                    uBufferSize);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else if (VIOSockRxCbInit(pContext))
    {
        ULONG i;
        PVIOSOCK_RX_PKT RxPktVA = (PVIOSOCK_RX_PKT)pContext->RxPktVA;

        pContext->RxPktPA = VirtIOWdfDeviceGetPhysicalAddress(&pContext->VDevice.VIODevice, pContext->RxPktVA);
        ASSERT(pContext->RxPktPA.QuadPart);

        // fill queue, no lock
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
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VIOSockRxCbInit failed\n");
        status = STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(status))
    {
        VIOSockRxVqCleanup(pContext);
    }

    return status;
}

_Requires_lock_not_held_(pSocket->StateLock) VOID VIOSockRxPktHandleConnecting(IN PSOCKET_CONTEXT pSocket,
                                                                               IN PVIRTIO_VSOCK_HDR pPkt,
                                                                               IN BOOLEAN bTxHasSpace)
{
    WDFREQUEST PendedRequest;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = VIOSockPendedRequestGetLocked(pSocket, &PendedRequest);

    if (NT_SUCCESS(status))
    {
        if (PendedRequest == WDF_NO_HANDLE && !VIOSockIsNonBlocking(pSocket))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "No PendedRequest found\n");
            status = STATUS_CANCELLED;
        }
        else
        {
            BOOLEAN bSetEvent = FALSE;
            switch (pPkt->op)
            {
                case VIRTIO_VSOCK_OP_RESPONSE:
                    WdfSpinLockAcquire(pSocket->StateLock);
                    VIOSockStateSet(pSocket, VIOSOCK_STATE_CONNECTED);
                    bSetEvent = VIOSockEventMarkBit(pSocket, FD_CONNECT_BIT, STATUS_SUCCESS);
                    if (bTxHasSpace)
                    {
                        bSetEvent |= VIOSockEventMarkBit(pSocket, FD_WRITE_BIT, STATUS_SUCCESS);
                    }
                    VIOSockEventNotify(pSocket, bSetEvent);
                    WdfSpinLockRelease(pSocket->StateLock);
                    status = STATUS_SUCCESS;
                    break;
                case VIRTIO_VSOCK_OP_INVALID:
                    if (PendedRequest != WDF_NO_HANDLE)
                    {
                        status = VIOSockPendedRequestSetResumeLocked(pSocket, PendedRequest);
                        if (NT_SUCCESS(status))
                        {
                            PendedRequest = WDF_NO_HANDLE;
                        }
                    }
                    break;
                case VIRTIO_VSOCK_OP_RST:
                    /* Peer replied RST while we were connecting: on Windows
                     * connect() to a non-listening address is WSAECONNREFUSED,
                     * not WSAECONNRESET (RESET is reserved for an established
                     * connection getting torn down). Map STATUS_CONNECTION_REFUSED
                     * so both loopback and wire paths surface the standard
                     * Winsock error to userland. */
                    status = STATUS_CONNECTION_REFUSED;
                    break;
                default:
                    status = STATUS_CONNECTION_INVALID;
            }
        }
    }

    if (!NT_SUCCESS(status))
    {
        WdfSpinLockAcquire(pSocket->StateLock);
        VIOSockEventSetBit(pSocket, FD_CONNECT_BIT, status);
        VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSE);
        WdfSpinLockRelease(pSocket->StateLock);
        if (pPkt->op != VIRTIO_VSOCK_OP_RST)
        {
            VIOSockSendReset(pSocket, TRUE);
        }
    }

    if (PendedRequest)
    {
        WdfRequestComplete(PendedRequest, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
}

_Requires_lock_not_held_(pSocket->RxLock) static BOOLEAN VIOSockRxPktEnqueueCb(IN PSOCKET_CONTEXT pSocket,
                                                                               IN PVIOSOCK_RX_PKT pPkt)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_RX_CB pCurrentCb = NULL;
    ULONG BufferFree, PktLen;
    BOOLEAN bCbEnqueued = TRUE;
    BOOLEAN bDataAdded = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    ASSERT(pPkt && pPkt->Buffer && pPkt->Header.len);

    PktLen = pPkt->Header.len;

    // Merge buffers
    WdfSpinLockAcquire(pSocket->RxLock);
    if (!IsListEmpty(&pSocket->RxCbList) && PktLen <= VIOSOCK_BYTES_TO_MERGE)
    {
        pCurrentCb = CONTAINING_RECORD(pSocket->RxCbList.Blink, VIOSOCK_RX_CB, ListEntry);

        BufferFree = VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE - pCurrentCb->DataLen;

        if (BufferFree >= PktLen)
        {
            if (VIOSockRxPktInc(pSocket, PktLen))
            {
                TraceEvents(TRACE_LEVEL_INFORMATION,
                            DBG_READ,
                            "RxCb merged: %d + %d bytes\n",
                            pCurrentCb->DataLen,
                            PktLen);
                memcpy((PCHAR)pCurrentCb->BufferVA + pCurrentCb->DataLen, pPkt->Buffer->BufferVA, PktLen);
                pCurrentCb->DataLen += PktLen;
                pCurrentCb->BytesToRead += PktLen;
                bDataAdded = TRUE;
            }
            else
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Rx buffer full, drop packet\n");
            }
            bCbEnqueued = FALSE;
        }
    }

    if (bCbEnqueued)
    {
        // Enqueue buffer
        if (VIOSockRxPktInc(pSocket, PktLen))
        {
            pPkt->Buffer->DataLen = PktLen;
            pPkt->Buffer->BytesToRead = PktLen;
            pPkt->Buffer->ReadPtr = pPkt->Buffer->BufferVA;
            InsertTailList(&pSocket->RxCbList, &pPkt->Buffer->ListEntry);
            pSocket->RxBuffers++;
            pPkt->Buffer = NULL; // remove buffer from pkt
            bDataAdded = TRUE;
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "RxCb enqueued: %d bytes\n", PktLen);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Rx buffer full, drop packet\n");
            bCbEnqueued = FALSE;
        }
    }

    WdfSpinLockRelease(pSocket->RxLock);

    if (bDataAdded)
    {
        VIOSockEventSetReadBitLocked(pSocket);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
    return bCbEnqueued;
}

// Loopback delivery: the peer's ready TX entry becomes this socket's receive
// CB as is — no copy, no cancel machinery (the writer's request completed at
// slicing), no DMA buffer (BufferPA stays 0, it never enters the rx vq). On
// success the entry belongs to this socket's Rx chain until read out.
_Requires_lock_not_held_(pSocket->RxLock) BOOLEAN VIOSockRxEnqueueTxEntry(IN PSOCKET_CONTEXT pSocket,
                                                                          IN PVIOSOCK_TX_ENTRY pTxEntry)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_RX_CB pCurrentCb = &pTxEntry->BufferDesc.Cb;
    BOOLEAN bCbEnqueued = TRUE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    // No result assignment: the SDV OS model declares RtlZeroMemory as void
    // (the regular WDK macro over memset returns the pointer).
    RtlZeroMemory(pCurrentCb, sizeof(*pCurrentCb));

    pCurrentCb->TxEntry = pTxEntry;
    pCurrentCb->BufferVA = pTxEntry->Buffer;
    pCurrentCb->ReadPtr = pTxEntry->Buffer;
    pCurrentCb->BytesToRead = pCurrentCb->DataLen = pTxEntry->PacketHeader.len;

    WdfSpinLockAcquire(pSocket->RxLock);

    // Enqueue buffer
    if (VIOSockRxPktInc(pSocket, pCurrentCb->DataLen))
    {
        InsertTailList(&pSocket->RxCbList, &pCurrentCb->ListEntry);
        pSocket->RxBuffers++;
    }
    else
    {
        // Cannot happen while the sender honors the credit window it was
        // granted, but keep the guard: the caller drops the chunk.
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Rx buffer full, drop packet\n");
        bCbEnqueued = FALSE;
    }

    WdfSpinLockRelease(pSocket->RxLock);

    if (bCbEnqueued)
    {
        VIOSockEventSetReadBitLocked(pSocket);
    }

    return bCbEnqueued;
}

VOID VIOSockRxProcessTxHasSpace(IN PSOCKET_CONTEXT pSocket)
{
    if (VIOSockResetFlag(pSocket, SOCK_TX_WAIT))
    {
        VIOSockEventSetBitLocked(pSocket, FD_WRITE_BIT, STATUS_SUCCESS);
    }

    // Same trigger, other consumer: sends parked in the per-socket
    // WriteQueue on zero credit resume here. Unconditional on
    // SOCK_TX_WAIT - parked blocking requests never set that flag.
    VIOSockWriteProcessQueue(pSocket);
}

// Returns TRUE iff the payload was enqueued on the socket's Rx queue.
// For a loopback OP_RW that also means the Tx entry's ownership moved
// there — the caller must not free it in that case.
static BOOLEAN VIOSockRxPktHandleConnected(IN PSOCKET_CONTEXT pSocket,
                                           IN PVIRTIO_VSOCK_HDR pPktHdr,
                                           IN BOOLEAN bLoopback,
                                           IN BOOLEAN bTxHasSpace)
{
    BOOLEAN bEnqueued = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    // FD_WRITE is edge-triggered (Winsock contract): re-recorded only once a
    // send has blocked (SOCK_TX_WAIT) and TX space has reopened — checked on
    // every op, since the peer piggybacks fwd_cnt on any packet, not just on
    // OP_CREDIT_UPDATE.
    if (bTxHasSpace)
    {
        VIOSockRxProcessTxHasSpace(pSocket);
    }

    switch (pPktHdr->op)
    {
        case VIRTIO_VSOCK_OP_RW:
            if (pSocket->Shutdown & VIRTIO_VSOCK_SHUTDOWN_RCV)
            {
                // Our receive side is shut (SD_RECEIVE): nothing is delivered any
                // more, so count the bytes as consumed for the peer's credit and let
                // the caller free the packet.
                WdfSpinLockAcquire(pSocket->RxLock);
                pSocket->fwd_cnt += pPktHdr->len;
                WdfSpinLockRelease(pSocket->RxLock);
                VIOSockSendCreditUpdate(pSocket);
                break;
            }

            if (bLoopback)
            {
                bEnqueued = VIOSockRxEnqueueTxEntry(pSocket,
                                                    CONTAINING_RECORD(pPktHdr, VIOSOCK_TX_ENTRY, PacketHeader));
            }
            else
            {
                bEnqueued = VIOSockRxPktEnqueueCb(pSocket, CONTAINING_RECORD(pPktHdr, VIOSOCK_RX_PKT, Header));
            }

            if (bEnqueued)
            {
                VIOSockReadProcessQueue(pSocket);
            }

            break;
        case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
            // TX-space accounting and the FD_WRITE re-arm are handled above for all ops.
            break;
        case VIRTIO_VSOCK_OP_SHUTDOWN:
            // The read-side EOF FD_CLOSE indication is raised inside
            // VIOSockShutdownFromPeer. Here we only tear the circuit down once the
            // peer has shut down both directions and nothing is left to read.
            if (VIOSockShutdownFromPeer(pSocket, pPktHdr->flags & VIRTIO_VSOCK_SHUTDOWN_MASK) &&
                !VIOSockIsDone(pSocket) && !VIOSockRxHasData(pSocket))
            {
                VIOSockSendReset(pSocket, FALSE);
                VIOSockDoClose(pSocket);
            }
            break;
        case VIRTIO_VSOCK_OP_RST:
            {
                /* If peer already sent a graceful SHUTDOWN (full mask), the RST
                 * is just socket cleanup — not an unexpected connection reset. */
                NTSTATUS closeStatus = ((pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_MASK) == VIRTIO_VSOCK_SHUTDOWN_MASK) ? STATUS_SUCCESS
                                                                                                                            : STATUS_CONNECTION_RESET;
                if (closeStatus == STATUS_CONNECTION_RESET)
                {
                    // Mark the reason before DoClose pokes the write pump, so
                    // parked sends complete with WSAECONNRESET, not WSAENOTCONN
                    VIOSockSetFlag(pSocket, SOCK_RESET);
                }
                VIOSockDoClose(pSocket);
                VIOSockEventSetBitLocked(pSocket, FD_CLOSE_BIT, closeStatus);
            }
            break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return bEnqueued;
}

VOID VIOSockRxPktHandleDisconnecting(IN PSOCKET_CONTEXT pSocket, IN PVIRTIO_VSOCK_HDR pPkt)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (pPkt->op == VIRTIO_VSOCK_OP_RST)
    {
        VIOSockDoClose(pSocket);
    }
}

static VOID VIOSockRxPktHandleListen(IN PSOCKET_CONTEXT pSocket, IN PVIRTIO_VSOCK_HDR pPkt)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (pPkt->op == VIRTIO_VSOCK_OP_RST)
    {
        // remove pended accept
        VIOSockAcceptRemovePkt(pSocket, pPkt);
        return;
    }
    else if (pPkt->op != VIRTIO_VSOCK_OP_REQUEST)
    {
        // OP_RW / OP_SHUTDOWN / OP_CREDIT_* on a listening socket are stragglers
        // from a previously-accepted-and-closed connection whose 4-tuple no
        // longer matches any ConnectedList entry. Reply with RST so the peer
        // discovers the connection is gone (Linux does the same in
        // virtio_transport_recv_pkt — fall through to reset_no_sock for any
        // non-REQUEST non-RST packet without a socket match).
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Unexpected packet for listening socket, op: %!Op!\n", pPkt->op);
        VIOSockSendResetNoSock(pContext, pPkt);
        return;
    }

    status = VIOSockAcceptEnqueuePkt(pSocket, pPkt);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockAcceptEnqueuePkt failed: 0x%x\n", status);
        VIOSockSendResetNoSock(pContext, pPkt);
    }
}

VOID VIOSockRxPktHandle(IN PDEVICE_CONTEXT pContext, IN PVIRTIO_VSOCK_HDR pPktHdr, IN BOOLEAN bLoopback)
{
    BOOLEAN bTxHasSpace;
    BOOLEAN bEnqueued = FALSE;
    PSOCKET_CONTEXT pSocket = NULL;

    // find socket
    pSocket = VIOSockConnectedFindByRxPktLocked(pContext, pPktHdr);
    if (!pSocket)
    {
        // no connected socket for incoming packet
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "No connected socket found\n");
        pSocket = VIOSockBoundFindByPortLocked(pContext, pPktHdr->dst_port);
    }

    if (pSocket && pSocket->type != pPktHdr->type)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid socket %d type\n", pSocket->SocketId);
        pSocket = NULL;
    }
    if (!pSocket)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Socket for packet is not exists, op: %!Op!\n", pPktHdr->op);
        // Per virtio-vsock semantics (Linux virtio_transport_common.c::
        // virtio_transport_reset_no_sock): reply with RST for any packet
        // that does not match an existing socket, EXCEPT for OP_RST itself
        // — replying to RST creates a ping-pong of late-arriving cleanup
        // RSTs that surfaces on the peer as spurious `Connection reset by
        // peer` on unrelated sockets.
        if (pPktHdr->op != VIRTIO_VSOCK_OP_RST)
        {
            VIOSockSendResetNoSock(pContext, pPktHdr);
        }
    }
    else
    {
        // Wire pkts carry guest_cid; loopback pkts carry VMADDR_CID_LOCAL.
        ASSERT((ULONG32)pPktHdr->dst_cid == pContext->Config.guest_cid ||
               (ULONG32)pPktHdr->dst_cid == VMADDR_CID_LOCAL);

        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_SOCKET,
                    "Socket %d found, state %!State!, op %!Op!, loopback %u\n",
                    pSocket->SocketId,
                    VIOSockStateGet(pSocket),
                    pPktHdr->op,
                    bLoopback);

        bTxHasSpace = !!VIOSockTxSpaceUpdate(pSocket, pPktHdr);

        switch (VIOSockStateGet(pSocket))
        {
            case VIOSOCK_STATE_CONNECTING:
                VIOSockRxPktHandleConnecting(pSocket, pPktHdr, bTxHasSpace);
                break;
            case VIOSOCK_STATE_CONNECTED:
                bEnqueued = VIOSockRxPktHandleConnected(pSocket, pPktHdr, bLoopback, bTxHasSpace);
                break;
            case VIOSOCK_STATE_CLOSING:
                VIOSockRxPktHandleDisconnecting(pSocket, pPktHdr);
                break;
            case VIOSOCK_STATE_LISTEN:
                VIOSockRxPktHandleListen(pSocket, pPktHdr);
                break;
            default:
                // The socket exists (bound on this port) but is not in a state
                // that can accept the incoming packet — typically a STATE_CLOSE
                // bind-without-listen target of a connect attempt. Linux
                // virtio_transport_recv_pkt replies with RST in this case so
                // the peer's connect() fails fast instead of timing out. Skip
                // the reply for OP_RST to avoid the cleanup-RST ping-pong (same
                // rule the no-socket fallback above follows).
                TraceEvents(TRACE_LEVEL_WARNING,
                            DBG_SOCKET,
                            "Unhandled packet for socket %d, state %!State!, op: %!Op!\n",
                            pSocket->SocketId,
                            VIOSockStateGet(pSocket),
                            pPktHdr->op);
                if (pPktHdr->op != VIRTIO_VSOCK_OP_RST)
                {
                    VIOSockSendResetNoSock(pContext, pPktHdr);
                }
        }
    }

    if (bLoopback)
    {
        // Rx-queue handoff (OP_RW) already took ownership of the Tx entry.
        if (!bEnqueued)
        {
            VIOSockTxFreeEntry(CONTAINING_RECORD(pPktHdr, VIOSOCK_TX_ENTRY, PacketHeader));
        }
    }
    else
    {
        VIOSockRxPktInsertOrPostpone(pContext, CONTAINING_RECORD(pPktHdr, VIOSOCK_RX_PKT, Header));
    }
}

_Requires_lock_not_held_(pContext->RxLock) VOID VIOSockRxVqProcess(IN PDEVICE_CONTEXT pContext)
{
    PVIOSOCK_RX_PKT pPkt;
    UINT len;
    LIST_ENTRY CompletionList;
    PSINGLE_LIST_ENTRY pCurrentEntry;
    BOOLEAN bStop = FALSE;

    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->RxLock);
    do
    {
        virtqueue_disable_cb(pContext->RxVq);

        while (TRUE)
        {
            if (!VIOSockTxMoreReplies(pContext))
            {
                /* Stop rx until the device processes already
                 * pending replies.  Leave rx virtqueue
                 * callbacks disabled.
                 */
                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "Stop rx\n");

                bStop = TRUE;
                break;
            }

            pPkt = (PVIOSOCK_RX_PKT)virtqueue_get_buf(pContext->RxVq, &len);
            if (!pPkt)
            {
                break;
            }

            /* Drop short/long packets */

            if (len < sizeof(pPkt->Header) || len > sizeof(pPkt->Header) + pPkt->Header.len)
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "Short/Long packet (%d)\n", len);
                VIOSockRxPktInsert(pContext, pPkt);
                continue;
            }

            ASSERT(pPkt->Header.len == len - sizeof(pPkt->Header));

            TraceEvents(TRACE_LEVEL_INFORMATION,
                        DBG_READ,
                        "Recv packet %!Op! (%u:%u <-- %u:%u), len: %u, flags: %u, buf_alloc: %u, fwd_cnt: %u\n",
                        pPkt->Header.op,
                        (ULONG)pPkt->Header.dst_cid,
                        pPkt->Header.dst_port,
                        (ULONG)pPkt->Header.src_cid,
                        pPkt->Header.src_port,
                        pPkt->Header.len,
                        pPkt->Header.flags,
                        pPkt->Header.buf_alloc,
                        pPkt->Header.fwd_cnt);

            //"complete" buffers later
            InsertTailList(&CompletionList, &pPkt->CompletionListEntry);
        }
    } while (!virtqueue_enable_cb(pContext->RxVq) && !bStop);

    WdfSpinLockRelease(pContext->RxLock);

    // complete buffers
    while (!IsListEmpty(&CompletionList))
    {
        pPkt = CONTAINING_RECORD(RemoveHeadList(&CompletionList), VIOSOCK_RX_PKT, CompletionListEntry);
        VIOSockRxPktHandle(pContext, &pPkt->Header, FALSE);
    };

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

//////////////////////////////////////////////////////////////////////////
_Requires_lock_not_held_(pSocket->RxLock) static NTSTATUS VIOSockReadForward(IN PSOCKET_CONTEXT pSocket,
                                                                             IN WDFREQUEST Request,
                                                                             IN ULONG Flags)
{
    PVIOSOCK_RX_CONTEXT pRequest;
    PVIOSOCK_SG_CTX pSgCtx;
    WDF_OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;
    SIZE_T Length;
    VIOSOCK_STATE state = VIOSockStateGet(pSocket);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    if (VIOSockIsFlag(pSocket, SOCK_CONTROL))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Invalid socket %d for recv\n", pSocket->SocketId);
        return STATUS_NOT_SOCKET;
    }

    if (Flags & ~(MSG_PEEK | MSG_WAITALL))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Unsupported flags: 0x%x\n", Flags & ~(MSG_PEEK | MSG_WAITALL));
        return STATUS_NOT_SUPPORTED;
    }

    // Both MSG_WAITALL misuses are reported as unsupported (WSAEOPNOTSUPP), the
    // code Winsock documents for them: combining it with another flag, and asking
    // for it on a non-blocking socket, which cannot promise to wait for a full
    // buffer.
    if (Flags & MSG_WAITALL && Flags & ~MSG_WAITALL)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Incompatible flags: 0x%x\n", Flags);
        return STATUS_NOT_SUPPORTED;
    }

    if (Flags & MSG_WAITALL && VIOSockIsNonBlocking(pSocket))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "MSG_WAITALL on a non-blocking socket\n");
        return STATUS_NOT_SUPPORTED;
    }

    // recv is valid only on a connected socket - CONNECTED, or CLOSING to drain the
    // final EOF after the peer closed. CLOSE is also allowed while buffered data
    // remains: a transport reset (VM save/restore, migration) moves a CONNECTED
    // socket straight to CLOSE without draining the Rx buffers, and the bytes
    // already received must stay readable. Otherwise reject with WSAENOTCONN
    // instead of parking the request in the read queue, where it would never
    // complete.
    if (state != VIOSOCK_STATE_CONNECTED && state != VIOSOCK_STATE_CLOSING &&
        !(state == VIOSOCK_STATE_CLOSE && VIOSockRxHasData(pSocket)))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_READ,
                    "recv on socket %d in state %!State! rejected (not connected)\n",
                    pSocket->SocketId,
                    state);
        return STATUS_CONNECTION_INVALID;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VIOSOCK_RX_CONTEXT);
    status = WdfObjectAllocateContext(Request, &attributes, &pRequest);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return status;
    }

    pSgCtx = GetRequestSgContext(Request);
    if (pSgCtx)
    {
        // SG read (RECV_EX): the cursor starts at the head of the locked MDL
        // chain; the flat/SG split is decided here once, the copy path picks
        // its handler by CurrentMdl
        pRequest->FreePtr = VIOSockSgMapMdl(pSgCtx->MdlChain);
        if (!pRequest->FreePtr)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "Failed to map the first Mdl\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        pRequest->CurrentMdl = pSgCtx->MdlChain;
        pRequest->MdlFreeBytes = MmGetMdlByteCount(pSgCtx->MdlChain);
        pRequest->BufferLen = pRequest->FreeBytes = pSgCtx->TotalLen;
    }
    else
    {
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &pRequest->FreePtr, &Length);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
            return status;
        }

        // CurrentMdl/MdlFreeBytes stay zero (WDF zero-inits the context)
        pRequest->BufferLen = pRequest->FreeBytes = (ULONG)Length;
    }

    pRequest->Flags = Flags;
    VIOSockTimerEntryInit(&pRequest->TimerEntry);

    if (!VIOSockIsNonBlocking(pSocket) && pSocket->RecvTimeout != MAXLONGLONG)
    {
        pRequest->Counter = 0;

        WdfSpinLockAcquire(pSocket->RxLock);
        VIOSockTimerStartTimeout(&pSocket->ReadTimer, &pRequest->TimerEntry, pSocket->RecvTimeout);
        WdfSpinLockRelease(pSocket->RxLock);
    }

    VIOSockEventClearBitLocked(pSocket, FD_READ_BIT);

    status = WdfRequestForwardToIoQueue(Request, pSocket->ReadQueue);
    if (NT_SUCCESS(status))
    {
        ULONG QueueRequests;

        // The ReadQueue has no ready-notify callback to avoid recursion, so start the dequeue worker
        // ourselves on the empty->non-empty edge. QueueRequests counts only requests
        // still in the queue (not ones already pulled into a running worker), so == 1
        // means our forward made the queue non-empty and no worker is draining it. If a
        // worker is already running it will pick this request up on its own - no need to
        // kick it per request.
        WdfIoQueueGetState(pSocket->ReadQueue, &QueueRequests, NULL);
        if (QueueRequests == 1)
        {
            VIOSockReadProcessQueue(pSocket);
        }
    }
    else
    {

        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestForwardToIoQueue failed: 0x%x\n", status);
        if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry))
        {
            WdfSpinLockAcquire(pSocket->RxLock);
            VIOSockTimerDeref(&pSocket->ReadTimer, TRUE);
            WdfSpinLockRelease(pSocket->RxLock);
        }
    }

    return status;
}

static VOID VIOSockRead(IN WDFQUEUE Queue, IN WDFREQUEST Request, IN size_t Length)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(Length);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    status = VIOSockReadForward(GetSocketContextFromRequest(Request), Request, 0);

    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}

// IOCTL_SOCKET_RECV handler: the buffered small-recv path — data is copied into
// the system buffer, which the io-manager hands back to the caller on completion
NTSTATUS
VIOSockRecvBuffered(IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    NTSTATUS status;
    PVIRTIO_VSOCK_READ_PARAMS pReadParams;
    ULONG Flags = 0;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    // validate request
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pReadParams), &pReadParams, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    status = VIOSockReadForward(pSocket, Request, pReadParams->Flags);
    if (NT_SUCCESS(status))
    {
        status = STATUS_PENDING;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

NTSTATUS
VIOSockRecvEx(IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PVIOSOCK_SG_CTX pSgCtx = GetRequestSgContext(Request);
    PVIRTIO_VSOCK_SEND_RECV_PARAMS pParams;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    // The buffer chain is captured and locked in caller context
    // (VIOSockSgPrepare); no context means the IOCTL somehow bypassed
    // EvtIoInCallerContext.
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

    status = VIOSockReadForward(pSocket, Request, pParams->dwFlags);
    if (NT_SUCCESS(status))
    {
        status = STATUS_PENDING;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

NTSTATUS
VIOSockReadQueueInit(IN WDFDEVICE hDevice)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    WDF_IO_QUEUE_CONFIG queueConfig;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = pContext->ThisDevice;

    status = WdfSpinLockCreate(&attributes, &pContext->RxLock);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfSpinLockCreate failed: 0x%x\n", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchParallel);

    queueConfig.EvtIoRead = VIOSockRead;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    // Pure demultiplexer: EvtIoRead immediately forwards each read to the owning socket's
    // ReadQueue and never holds a request. Make it non-power-managed so it neither blocks
    // a power transition nor needs an EvtIoStop callback (SDV does not flag a non-PM queue).
    queueConfig.PowerManaged = WdfFalse;

    status = WdfIoQueueCreate(hDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &pContext->ReadQueue);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfIoQueueCreate failed (Read Queue): 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(hDevice, pContext->ReadQueue, WdfRequestTypeRead);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_READ,
                    "WdfDeviceConfigureRequestDispatching failed (Read Queue): 0x%x\n",
                    status);
        return status;
    }

    return STATUS_SUCCESS;
}

// only loopback buffers carry a peer TX entry
#define VIOSockIsLoopbackCb(cb) ((cb)->TxEntry != NULL)

// Requeue the read request to wait for more data, preserving its receive timer.
// If the deadline has already expired, do not requeue - report STATUS_IO_TIMEOUT
// so the caller completes the request. Otherwise requeue it (silent now that the
// ReadQueue has no ready-notify: this just parks the request until the next explicit
// worker trigger), restart the deadline timer, and the caller hands the request back to
// the queue instead of completing it.
_Requires_lock_held_(pSocket->RxLock) static NTSTATUS VIOSockReadRequeue(IN PSOCKET_CONTEXT pSocket,
                                                                         IN WDFREQUEST ReadRequest)
{
    NTSTATUS status;
    PVIOSOCK_RX_CONTEXT pRequest = GetRequestRxContext(ReadRequest);

    if (VIOSockTimerEntryIsExpired(&pRequest->TimerEntry))
    {
        return STATUS_IO_TIMEOUT;
    }

    status = WdfRequestRequeue(ReadRequest);

    ASSERT(NT_SUCCESS(status));

    if (NT_SUCCESS(status))
    {
        // continue timer
        if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry))
        {
            VIOSockTimerStartDeadline(&pSocket->ReadTimer, NULL, pRequest->TimerEntry.Deadline);
        }
    }

    return status;
}

// Release the loopback entry-CBs consumed by the read dequeue: each returns
// the peer's TX entry with its skb buffer to the lookasides. The peer's TX
// window reopens via the regular CREDIT_UPDATE advertise above.
static VOID VIOSockReadCompleteLoopback(IN PSOCKET_CONTEXT pSocket, IN PLIST_ENTRY pLoopbackList)
{
    while (!IsListEmpty(pLoopbackList))
    {
        PVIOSOCK_RX_CB pCurrentCb = CONTAINING_RECORD(RemoveHeadList(pLoopbackList), VIOSOCK_RX_CB, ListEntry);

        ASSERT(pCurrentCb->TxEntry);
        VIOSockTxFreeEntry(pCurrentCb->TxEntry);
    }
}

// Decide what to do with the read request: complete it now (*pStop, with the returned
// status) or requeue it to wait for more data (*pRequeue, which implies *pStop). With
// data present and the receive side open, neither is set. The caller keeps the dequeue
// drain going unless this request is parked (*pRequeue): FIFO, nothing behind a parked
// request can proceed, and re-driving a requeued request would busy-spin. Every other
// completion - shutdown / reset / WOULDBLOCK - applies to all parked recvs alike, so
// draining continues. Must be called under RxLock.
_Requires_lock_held_(pSocket->RxLock) static NTSTATUS VIOSockReadEvaluateRequest(IN PSOCKET_CONTEXT pSocket,
                                                                                 OUT BOOLEAN *pStop,
                                                                                 OUT BOOLEAN *pRequeue)
{
    NTSTATUS status = VIOSockStateValidate(pSocket, FALSE);
    BOOLEAN bStop = FALSE, bRequeue = FALSE;

    if (status == STATUS_LOCAL_DISCONNECT)
    {
        // Our own SD_RECEIVE disallows every further recv (WSAESHUTDOWN), buffered
        // data included.
        bStop = TRUE;
    }
    else if (VIOSockRxHasData(pSocket))
    {
        // Buffered data is delivered whatever the state; the matcher takes it from here.
        status = STATUS_SUCCESS;
    }
    else
    {
        // No buffered data, so this request cannot be filled now: stop matching it.
        bStop = TRUE;

        if (NT_SUCCESS(status))
        {
            // Socket is healthy but empty.
            if (VIOSockIsNonBlocking(pSocket))
            {
                // Fail this recv with WOULDBLOCK; other non-blocking waiters share the
                // same fate, so keep draining them (bRestart stays TRUE).
                status = STATUS_CANT_WAIT;
            }
            else
            {
                // Blocking and connected: park this request and wait for data. FIFO -
                // nothing behind it can proceed either, so the caller stops the drain.
                ASSERT(VIOSockStateGet(pSocket) == VIOSOCK_STATE_CONNECTED);
                bRequeue = TRUE;
            }
        }
        else
        {
            // Terminal state, identical for every parked recv, so drain them all
            // (bRestart stays TRUE). The peer's SHUT_SEND (REMOTE) is reported as EOF
            // (zero bytes); a reset or invalid socket keeps its error status (our own
            // SHUT_RD was handled above).
            if (status == STATUS_REMOTE_DISCONNECT)
            {
                status = STATUS_SUCCESS;
            }
        }
    }

    *pStop = bStop;
    *pRequeue = bRequeue;

    return status;
}

// Copy as much of the current CB window as fits into the flat request buffer,
// advancing the request cursor; returns the number of bytes copied. CB-side
// accounting is the caller's job.
static ULONG VIOSockReadCopyFlat(IN PVIOSOCK_RX_CONTEXT pRequest, IN PVIOSOCK_RX_CB pCb)
{
    ULONG uCopy = (pRequest->FreeBytes < pCb->BytesToRead) ? pRequest->FreeBytes : pCb->BytesToRead;

    memcpy(pRequest->FreePtr, pCb->ReadPtr, uCopy);
    pRequest->FreePtr += uCopy;
    pRequest->FreeBytes -= uCopy;

    return uCopy;
}

// Same contract for an SG request: the destination is the locked MDL chain,
// walked segment by segment as buffers fill. A failed mapping of the next MDL
// truncates the request buffer (FreeBytes = 0): the whole previous MDL has
// already been filled by then, so the request completes as a legal short read.
static ULONG VIOSockReadCopySg(IN PVIOSOCK_RX_CONTEXT pRequest, IN PVIOSOCK_RX_CB pCb)
{
    ULONG uCopied = 0;
    ULONG uToCopy = (pRequest->FreeBytes < pCb->BytesToRead) ? pRequest->FreeBytes : pCb->BytesToRead;

    while (uToCopy)
    {
        ULONG uChunk = (uToCopy < pRequest->MdlFreeBytes) ? uToCopy : pRequest->MdlFreeBytes;

        if (!uChunk)
        {
            // current MDL filled up, switch the cursor to the next one
            PMDL pNextMdl = pRequest->CurrentMdl->Next;

            ASSERT(pNextMdl); // FreeBytes != 0 means the chain has more room

            pRequest->FreePtr = VIOSockSgMapMdl(pNextMdl);
            if (!pRequest->FreePtr)
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "Failed to map the next Mdl\n");
                pRequest->FreeBytes = 0;
                break;
            }
            pRequest->CurrentMdl = pNextMdl;
            pRequest->MdlFreeBytes = MmGetMdlByteCount(pNextMdl);
            continue;
        }

        memcpy(pRequest->FreePtr, pCb->ReadPtr + uCopied, uChunk);
        pRequest->FreePtr += uChunk;
        pRequest->FreeBytes -= uChunk;
        pRequest->MdlFreeBytes -= uChunk;
        uCopied += uChunk;
        uToCopy -= uChunk;
    }

    return uCopied;
}

static __inline ULONG VIOSockReadCopy(IN PVIOSOCK_RX_CONTEXT pRequest, IN PVIOSOCK_RX_CB pCb)
{
    return pRequest->CurrentMdl ? VIOSockReadCopySg(pRequest, pCb) : VIOSockReadCopyFlat(pRequest, pCb);
}

// Match the retrieved read request against buffered Rx data under RxLock: fill it,
// decide complete-vs-requeue (a requeue clears *pReadRequest and returns
// STATUS_SUCCESS, so the caller leaves the request on the queue), advertise credit
// and the FD_READ event, and complete the loopback peer requests matched into the
// read. The caller completes the read request itself after this returns.
// Returns the read request's completion status; *pRestart tells the caller whether
// the worker should retrieve and process the next request.
_Requires_lock_not_held_(pSocket->RxLock) static NTSTATUS VIOSockReadDequeueCb(IN PSOCKET_CONTEXT pSocket,
                                                                               IN OUT WDFREQUEST *pReadRequest,
                                                                               OUT BOOLEAN *pRestart)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    WDFREQUEST ReadRequest = *pReadRequest;
    PVIOSOCK_RX_CONTEXT pRequest = GetRequestRxContext(ReadRequest);
    NTSTATUS status = STATUS_SUCCESS;
    PVIOSOCK_RX_CB pCurrentCb;
    PLIST_ENTRY pCurrentItem;
    LIST_ENTRY LoopbackList;
    ULONG FreeSpace, FwdCntDelta = 0, uRcvTarget = 0;
    BOOLEAN bSetBit, bStop, bRequeue, bRestart = TRUE, bProcessRxPktList = FALSE;
    BOOLEAN bLowRxBytes = FALSE;

    InitializeListHead(&LoopbackList);

    WdfSpinLockAcquire(pSocket->RxLock);

    if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry))
    {
        VIOSockTimerDeref(&pSocket->ReadTimer, TRUE);
    }

    status = VIOSockReadEvaluateRequest(pSocket, &bStop, &bRequeue);

    if (bRequeue)
    {
        bStop = TRUE;
        bRestart = FALSE;
        status = VIOSockReadRequeue(pSocket, ReadRequest);
        if (NT_SUCCESS(status))
        {
            ReadRequest = WDF_NO_HANDLE;
            status = STATUS_SUCCESS;
        }
    }

    if (bStop)
    {
        WdfSpinLockRelease(pSocket->RxLock);
        if (ReadRequest != WDF_NO_HANDLE)
        {
            WdfRequestSetInformation(ReadRequest, pRequest->BufferLen - pRequest->FreeBytes);
        }
        *pReadRequest = ReadRequest;
        *pRestart = bRestart;
        return status;
    }

    // process chained buffer
    for (pCurrentItem = pSocket->RxCbList.Flink; pCurrentItem != &pSocket->RxCbList; pCurrentItem = pCurrentItem->Flink)
    {
        ULONG uCopied;

        // peek the first buffer
        pCurrentCb = CONTAINING_RECORD(pCurrentItem, VIOSOCK_RX_CB, ListEntry);

        uCopied = VIOSockReadCopy(pRequest, pCurrentCb);

        // did the whole CB fit?
        if (uCopied == pCurrentCb->BytesToRead)
        {
            if (!(pRequest->Flags & MSG_PEEK))
            {
                pCurrentCb->BytesToRead = 0;
                pCurrentItem = pCurrentItem->Blink;
                RemoveEntryList(&pCurrentCb->ListEntry);
                VIOSockRxPktDec(pSocket, uCopied);

                if (VIOSockIsLoopbackCb(pCurrentCb))
                {
                    InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); // release loopback entries later
                }
                else
                {
                    VIOSockRxCbPushLocked(pContext, pCurrentCb);
                    bProcessRxPktList = TRUE;
                }
            }
        }
        else // request buffer exhausted, CB consumed partially
        {
            if (!(pRequest->Flags & MSG_PEEK))
            {
                // update current CB data ptr
                pCurrentCb->ReadPtr += uCopied;
                pCurrentCb->BytesToRead -= uCopied;
                VIOSockRxPktDec(pSocket, uCopied);
            }
            ASSERT(pRequest->FreeBytes == 0);

            break;
        }
    }

    if (bProcessRxPktList)
    {
        VIOSockRxPktListProcessLockedNotify(pContext);
    }

    // SO_RCVLOWAT: a blocking stream read must not return fewer than
    // min(RcvLowat, BufferLen) bytes, so keep waiting until that many are copied.
    // RcvLowat defaults to 1, so this matches the previous "return any data" path.
    uRcvTarget = (pSocket->RcvLowat < pRequest->BufferLen) ? pSocket->RcvLowat : pRequest->BufferLen;

    if (!pRequest->FreeBytes || pRequest->Flags & MSG_PEEK)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "Should complete read request\n");
    }
    else if (pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_SEND)
    {
        // Peer FIN'd: no more data will arrive, so return what we copied
        // (0 = EOF, or partial < SO_RCVLOWAT / MSG_WAITALL) instead of
        // requeueing. Mirrors Linux virtio_transport_stream_do_dequeue.
    }
    else if (pRequest->BufferLen == pRequest->FreeBytes || (pRequest->Flags & MSG_WAITALL) ||
             (!VIOSockIsNonBlocking(pSocket) && (pRequest->BufferLen - pRequest->FreeBytes) < uRcvTarget))
    {
        // requeue request
        status = VIOSockReadRequeue(pSocket, ReadRequest);
        if (NT_SUCCESS(status))
        {
            // requeued: the request left our hands, stop restarting the worker
            ReadRequest = WDF_NO_HANDLE;
            status = STATUS_SUCCESS;
            bRestart = FALSE;
        }
    }

    bSetBit = (ReadRequest != WDF_NO_HANDLE && VIOSockRxReadReady(pSocket));
    FwdCntDelta = pSocket->fwd_cnt - pSocket->last_fwd_cnt;
    FreeSpace = pSocket->buf_alloc - FwdCntDelta;
    bLowRxBytes = (BOOLEAN)(pSocket->RxBytes < pSocket->RcvLowat);

    WdfSpinLockRelease(pSocket->RxLock);

    if (bSetBit)
    {
        VIOSockEventSetBitLocked(pSocket, FD_READ_BIT, STATUS_SUCCESS);
    }
    else
    {
        VIOSockEventClearBitLocked(pSocket, FD_READ_BIT);
    }

    // Mirror virtio_transport_stream_do_dequeue(): only advertise when we have
    // newly consumed bytes to report, and do so when free space is low or the
    // queued data is still below the reader's SO_RCVLOWAT mark.
    if (FwdCntDelta != 0 && (FreeSpace < VIRTIO_VSOCK_MAX_PKT_BUF_SIZE || bLowRxBytes))
    {
        VIOSockSendCreditUpdate(pSocket);
    }

    if (ReadRequest != WDF_NO_HANDLE)
    {
        ASSERT(pSocket->PendedRequest == WDF_NO_HANDLE);
        WdfRequestSetInformation(ReadRequest, pRequest->BufferLen - pRequest->FreeBytes);
    }

    VIOSockReadCompleteLoopback(pSocket, &LoopbackList);

    *pReadRequest = ReadRequest;
    *pRestart = bRestart;

    return status;
}

_Requires_lock_not_held_(pSocket->RxLock) static BOOLEAN VIOSockReadProcessNextRequest(IN PSOCKET_CONTEXT pSocket);

//////////////////////////////////////////////////////////////////////////
// Per-socket recv-dequeue serialization.
// A producer bumps the counter; only the 0->1 edge starts the dequeue worker. The
// worker re-arms the counter to 1 at the start of each pass and stops only when it can
// clear 1->0, i.e. no producer bumped it during the pass.

// Bump the counter. Returns FALSE on the 0->1 edge (the caller must start the dequeue
// worker) and TRUE otherwise (a worker is already running; the bump makes it take
// another pass).
static __inline BOOLEAN VIOSockReadDequeueAlreadyActive(IN PSOCKET_CONTEXT pSocket)
{
    return InterlockedIncrement(&pSocket->ReadDequeueInProgress) != 1;
}

// Re-arm the counter for another worker pass.
static __inline VOID VIOSockReadDequeueReset(IN PSOCKET_CONTEXT pSocket)
{
    InterlockedExchange(&pSocket->ReadDequeueInProgress, 1);
}

// Try to finish the worker by clearing 1->0. Returns FALSE when it cleared (the value
// was exactly 1, no bump arrived during the pass, so the worker may stop) and TRUE when
// a bump pushed the count past 1 during the pass (take another pass).
static __inline BOOLEAN VIOSockReadDequeueWorkArrived(IN PSOCKET_CONTEXT pSocket)
{
    return (BOOLEAN)(InterlockedCompareExchange(&pSocket->ReadDequeueInProgress, 0, 1) != 1);
}

_Requires_lock_not_held_(pSocket->RxLock) VOID VIOSockReadProcessQueue(IN PSOCKET_CONTEXT pSocket)
{
    BOOLEAN bRestart;

    // Single-instance dequeue worker. The 0->1 bump elects the worker owner; any later
    // caller only bumps the counter and returns, so triggers never recurse or run
    // concurrently. The owner re-arms at the top of each pass and stops only when the
    // queue is drained (VIOSockReadProcessNextRequest returns FALSE) AND no bump arrived
    // during the pass; a bump makes it take another pass to pick up the new request.
    if (VIOSockReadDequeueAlreadyActive(pSocket))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Dequeue worker already running, bump and return\n");
        return;
    }

    do
    {
        VIOSockReadDequeueReset(pSocket);
        bRestart = VIOSockReadProcessNextRequest(pSocket);
    } while (bRestart || VIOSockReadDequeueWorkArrived(pSocket));
}

_Requires_lock_not_held_(pSocket->RxLock) static BOOLEAN VIOSockReadProcessNextRequest(IN PSOCKET_CONTEXT pSocket)
{
    WDFREQUEST ReadRequest = WDF_NO_HANDLE;
    NTSTATUS status = STATUS_SUCCESS;
    BOOLEAN bRestart;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    status = WdfIoQueueRetrieveNextRequest(pSocket->ReadQueue, &ReadRequest);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_READ,
                    "The read queue is empty, exiting from VIOSockReadProcessNextRequest\n");
        return FALSE;
    }

    status = VIOSockReadDequeueCb(pSocket, &ReadRequest, &bRestart);

    if (ReadRequest != WDF_NO_HANDLE)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Complete read request: 0x%08x\n", status);
        WdfRequestComplete(ReadRequest, status);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "No read request to complete\n");
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);

    return bRestart;
}

_Requires_lock_not_held_(pSocket->RxLock) VOID VIOSockReadCleanupCb(IN PSOCKET_CONTEXT pSocket)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_RX_CB pCurrentCb;
    LIST_ENTRY LoopbackList;
    BOOLEAN bAlwaysTrue = TRUE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    InitializeListHead(&LoopbackList);

    WdfSpinLockAcquire(pSocket->RxLock);

    // process chained buffer
    while (!IsListEmpty(&pSocket->RxCbList))
    {
        // peek the first buffer
        pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&pSocket->RxCbList), VIOSOCK_RX_CB, ListEntry);

        if (VIOSockIsLoopbackCb(pCurrentCb))
        {
            InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); // release loopback entries later
        }
        else
        {
            VIOSockRxCbPushLocked(pContext, pCurrentCb);
        }
    }

    WdfSpinLockRelease(pSocket->RxLock);

    VIOSockRxPktListProcessLockedNotify(pContext);

    // Static Driver Verifier(SDV) tracks only one request, and when SDV unwinds the loop,
    // it treats completion of the second request as another completion of the first request.
    // The 'assume' below causes SDV to skip loop analysis.
    __analysis_assume(bAlwaysTrue == FALSE);

    // Release loopback entries. The socket is being cleaned up — no writer wake
    // (its sends against this socket fail state validation anyway).
    if (bAlwaysTrue)
    {
        while (!IsListEmpty(&LoopbackList))
        {
            pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&LoopbackList), VIOSOCK_RX_CB, ListEntry);
            VIOSockTxFreeEntry(pCurrentCb->TxEntry);
        }
    }
}

// SD_RECEIVE: drop everything buffered for this socket. The unread bytes count as
// consumed for the peer's credit window (fwd_cnt advances, a CREDIT_UPDATE goes out),
// wire CBs return to the device pool and loopback entries to their writers. Unlike
// cleanup the socket lives on - only its receive side is gone.
_Requires_lock_not_held_(pSocket->RxLock) VOID VIOSockReadDiscardCb(IN PSOCKET_CONTEXT pSocket)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_RX_CB pCurrentCb;
    LIST_ENTRY LoopbackList;
    ULONG FwdCntDelta;
    BOOLEAN bProcessRxPktList = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    InitializeListHead(&LoopbackList);

    WdfSpinLockAcquire(pSocket->RxLock);

    while (!IsListEmpty(&pSocket->RxCbList))
    {
        pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&pSocket->RxCbList), VIOSOCK_RX_CB, ListEntry);
        VIOSockRxPktDec(pSocket, pCurrentCb->BytesToRead);

        if (VIOSockIsLoopbackCb(pCurrentCb))
        {
            InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); // release loopback entries later
        }
        else
        {
            VIOSockRxCbPushLocked(pContext, pCurrentCb);
            bProcessRxPktList = TRUE;
        }
    }

    FwdCntDelta = pSocket->fwd_cnt - pSocket->last_fwd_cnt;

    WdfSpinLockRelease(pSocket->RxLock);

    if (bProcessRxPktList)
    {
        VIOSockRxPktListProcessLockedNotify(pContext);
    }

    VIOSockReadCompleteLoopback(pSocket, &LoopbackList);

    if (FwdCntDelta != 0)
    {
        VIOSockSendCreditUpdate(pSocket);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}

VOID VIOSockReadIoCanceledOnQueue(IN WDFQUEUE Queue, IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PVIOSOCK_RX_CONTEXT pRequest = GetRequestRxContext(Request);

    UNREFERENCED_PARAMETER(Queue);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Read request canceled on queue\n");

    WdfSpinLockAcquire(pSocket->RxLock);
    if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry))
    {
        VIOSockTimerDeref(&pSocket->ReadTimer, TRUE);
    }
    WdfSpinLockRelease(pSocket->RxLock);

    if (pRequest->BufferLen != pRequest->FreeBytes)
    {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, pRequest->BufferLen - pRequest->FreeBytes);
    }
    else
    {
        WdfRequestComplete(Request, STATUS_CANCELLED);
    }
}

NTSTATUS
VIOSockReadSocketQueueInit(IN PSOCKET_CONTEXT pSocket)
{
    WDFDEVICE hDevice = WdfFileObjectGetDevice(pSocket->ThisSocket);
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    WDF_OBJECT_ATTRIBUTES queueAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

    queueConfig.EvtIoCanceledOnQueue = VIOSockReadIoCanceledOnQueue;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    // Power-managed: a blocking read parks here (framework-owned) until data arrives, so on
    // D0 exit WDF holds it and re-delivers it on D0 entry - the wanted pause/resume. It is
    // never held driver-owned for an indeterminate time (the read worker retrieves it and
    // completes or requeues it right away), so per the WDF docs EvtIoStop can be safely
    // omitted; the assume below suppresses the resulting (false-positive) SDV warning.
    queueConfig.PowerManaged = WdfTrue;

    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ParentObject = pSocket->ThisSocket;

    __analysis_assume(queueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(hDevice, &queueConfig, &queueAttributes, &pSocket->ReadQueue);
    __analysis_assume(queueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfIoQueueCreate failed (Socket Read Queue): 0x%x\n", status);
        return status;
    }

    status = VIOSockTimerCreate(&pSocket->ReadTimer, pSocket->ThisSocket, VIOSockReadTimerFunc);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "VIOSockTimerCreate failed (Socket Read Queue): 0x%x\n", status);
        return status;
    }

    return STATUS_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
VOID VIOSockReadTimerFunc(WDFTIMER Timer)
{
    static LONG lCounter;
    PSOCKET_CONTEXT pSocket = GetSocketContext(WdfTimerGetParentObject(Timer));
    LONGLONG MinRemainingTicks = MAXLONGLONG;
    WDFREQUEST PrevTagRequest = WDF_NO_HANDLE, TagRequest = WDF_NO_HANDLE, Request;
    NTSTATUS status;
    LIST_ENTRY CompletionList;
    PVIOSOCK_RX_CONTEXT pRequest;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    InterlockedIncrement(&lCounter);
    InitializeListHead(&CompletionList);

    do
    {
        status = WdfIoQueueFindRequest(pSocket->ReadQueue, PrevTagRequest, WDF_NO_HANDLE, NULL, &TagRequest);

        if (PrevTagRequest != WDF_NO_HANDLE)
        {
            WdfObjectDereference(PrevTagRequest);
        }

        if (NT_SUCCESS(status))
        {
            pRequest = GetRequestRxContext(TagRequest);

            WdfSpinLockAcquire(pSocket->RxLock);

            if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry) && pRequest->Counter < lCounter)
            {
                LONGLONG llRemainingTicks = VIOSockTimerEntryRemainingTicks(&pRequest->TimerEntry);
                if (llRemainingTicks <= VIOSOCK_TIMER_TOLERANCE_TICKS)
                {
                    status = WdfIoQueueRetrieveFoundRequest(pSocket->ReadQueue, TagRequest, &Request);

                    WdfObjectDereference(TagRequest);

                    if (status == STATUS_NOT_FOUND)
                    {
                        TagRequest = PrevTagRequest = WDF_NO_HANDLE;
                        status = STATUS_SUCCESS;
                    }
                    else if (!NT_SUCCESS(status))
                    {
                        TraceEvents(TRACE_LEVEL_ERROR,
                                    DBG_READ,
                                    "WdfIoQueueRetrieveFoundRequest failed: 0x%08x\n",
                                    status);
                        WdfSpinLockRelease(pSocket->RxLock);
                        break;
                    }
                    else
                    {
                        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "Complete expired queued Rx request %p\n", Request);

                        VIOSockTimerDeref(&pSocket->ReadTimer, FALSE);
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

            WdfSpinLockRelease(pSocket->RxLock);
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
        WdfSpinLockAcquire(pSocket->RxLock);
        VIOSockTimerSetDeadline(&pSocket->ReadTimer, VIOSockTimerTicksToDeadline(MinRemainingTicks));
        WdfSpinLockRelease(pSocket->RxLock);
    }

    while (!IsListEmpty(&CompletionList))
    {
        pRequest = CONTAINING_RECORD(RemoveHeadList(&CompletionList), VIOSOCK_RX_CONTEXT, ListEntry);
        WdfRequestCompleteWithInformation(pRequest->ThisRequest,
                                          STATUS_IO_TIMEOUT,
                                          pRequest->BufferLen - pRequest->FreeBytes);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s, status: 0x%08x, counter: %u\n", __FUNCTION__, status, lCounter);
}
