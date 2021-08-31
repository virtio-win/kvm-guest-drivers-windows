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

#define VIOSOCK_BYTES_TO_MERGE  128         //max bytes to merge with prev buffer

#define VIOSOCK_CB_ENTRIES(n) ((n)+(n>>1))  //default chained buffer queue size

 //Chained Buffer entry
typedef struct _VIOSOCK_RX_CB
{
    union {
        SINGLE_LIST_ENTRY   FreeListEntry;
        LIST_ENTRY          ListEntry;      //Request buffer list
    };

    PVOID               BufferVA;   //common buffer of VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE bytes
    PHYSICAL_ADDRESS    BufferPA;   //common buffer PA

    PCHAR               ReadPtr;
    ULONG               BytesToRead;

    ULONG               DataLen;    //Valid data len (pkt.header.len)
    WDFREQUEST          Request;    //Write request for loopback
    WDFMEMORY           Memory;
}VIOSOCK_RX_CB, *PVIOSOCK_RX_CB;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSOCK_RX_CB, GetRequestRxCb);

typedef struct _VIOSOCK_RX_PKT
{
    VIRTIO_VSOCK_HDR    Header;
    PVIOSOCK_RX_CB      Buffer;     //Chained buffer
    union {
        BYTE IndirectDescs[SIZE_OF_SINGLE_INDIRECT_DESC * (1 + VIOSOCK_DMA_RX_PAGES)]; //Header + buffer
        SINGLE_LIST_ENTRY   RxPktListEntry;
        LIST_ENTRY          CompletionListEntry;
    };
}VIOSOCK_RX_PKT, *PVIOSOCK_RX_PKT;

typedef struct _VIOSOCK_RX_CONTEXT {
    ULONG BufferLen;
    ULONG FreeBytes;
    union {
        struct
        {
            LONGLONG    Timeout; //100ns
            LONG        Counter;
            ULONG       Flags;
            PCHAR       FreePtr;
        };
        struct
        {
            LIST_ENTRY ListEntry;
            WDFREQUEST ThisRequest;
        };
    };
}VIOSOCK_RX_CONTEXT, *PVIOSOCK_RX_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSOCK_RX_CONTEXT, GetRequestRxContext);

BOOLEAN
VIOSockRxCbInit(
    IN PDEVICE_CONTEXT  pContext
);

BOOLEAN
VIOSockRxCbAdd(
    IN PDEVICE_CONTEXT pContext
);

VOID
VIOSockRxCbCleanup(
    IN PDEVICE_CONTEXT pContext
);

EVT_WDF_IO_QUEUE_IO_READ    VIOSockRead;
EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE       VIOSockReadIoCanceledOnQueue;
EVT_WDF_IO_QUEUE_STATE      VIOSockReadSocketState;
EVT_WDF_REQUEST_CANCEL      VIOSockRxRequestCancelCb;
EVT_WDF_TIMER               VIOSockReadTimerFunc;


#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockRxCbInit)
#pragma alloc_text (PAGE, VIOSockRxCbAdd)
#pragma alloc_text (PAGE, VIOSockRxCbCleanup)
#pragma alloc_text (PAGE, VIOSockReadQueueInit)
#pragma alloc_text (PAGE, VIOSockReadSocketQueueInit)
#endif

//////////////////////////////////////////////////////////////////////////
_Requires_lock_held_(pContext->RxLock)
__inline
VOID
VIOSockRxCbPush(
    PDEVICE_CONTEXT pContext,
    PVIOSOCK_RX_CB pCb
)
{
    PushEntryList(&pContext->RxCbBuffers, &pCb->FreeListEntry);
}

_Requires_lock_not_held_(pContext->RxLock)
__inline
VOID
VIOSockRxCbPushLocked(
    PDEVICE_CONTEXT pContext,
    PVIOSOCK_RX_CB pCb
)
{
    WdfSpinLockAcquire(pContext->RxLock);
    VIOSockRxCbPush(pContext, pCb);
    WdfSpinLockRelease(pContext->RxLock);
}

_Requires_lock_held_(pContext->RxLock)
__inline
PVIOSOCK_RX_CB
VIOSockRxCbPop(
    IN PDEVICE_CONTEXT pContext
)
{
    PSINGLE_LIST_ENTRY pListEntry = PopEntryList(&pContext->RxCbBuffers);

    if (pListEntry)
    {
        return CONTAINING_RECORD(pListEntry, VIOSOCK_RX_CB, FreeListEntry);
    }

    return NULL;
}

__inline
VOID
VIOSockRxCbFree(
    IN PDEVICE_CONTEXT pContext,
    IN PVIOSOCK_RX_CB pCb
)
{
    ASSERT(pCb && pCb->BufferVA);

    ASSERT(pCb->BufferPA.QuadPart && pCb->Request == WDF_NO_HANDLE);

    if (pCb->BufferPA.QuadPart)
        VirtIOWdfDeviceFreeDmaMemory(&pContext->VDevice.VIODevice, pCb->BufferVA);

    WdfObjectDelete(pCb->Memory);
}

static
BOOLEAN
VIOSockRxCbAdd(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS        status;
    PVIOSOCK_RX_CB  pCb;
    WDFMEMORY       Memory;
    BOOLEAN         bRes = FALSE;

    PAGED_CODE();

    status = WdfMemoryCreateFromLookaside(pContext->RxCbBufferMemoryList, &Memory);
    if (NT_SUCCESS(status))
    {
        pCb = WdfMemoryGetBuffer(Memory, NULL);

        RtlZeroMemory(pCb, sizeof(*pCb));

        pCb->Memory = Memory;
        pCb->BufferVA = VirtIOWdfDeviceAllocDmaMemory(&pContext->VDevice.VIODevice,
            VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE, VIOSOCK_DRIVER_MEMORY_TAG);

        if (!pCb->BufferVA)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
                "VirtIOWdfDeviceAllocDmaMemory(%u bytes for Rx buffer) failed\n", VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE);
            WdfObjectDelete(pCb->Memory);
        }
        else
        {
            pCb->BufferPA = VirtIOWdfDeviceGetPhysicalAddress(&pContext->VDevice.VIODevice,
                pCb->BufferVA);

            ASSERT(pCb->BufferPA.QuadPart);

            if (!pCb->BufferPA.QuadPart)
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfDeviceGetPhysicalAddress failed\n");
                VIOSockRxCbFree(pContext, pCb);
            }
            else
            {
                //no need to lock, init call
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

static
PVIOSOCK_RX_CB
VIOSockRxCbEntryForRequest(
    IN PDEVICE_CONTEXT  pContext,
    IN WDFREQUEST       Request,
    IN ULONG            Length
)
{
    NTSTATUS                status;
    WDF_OBJECT_ATTRIBUTES   attributes;
    PVIOSOCK_RX_CB          pCb = NULL;

    ASSERT(Request != WDF_NO_HANDLE);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        VIOSOCK_RX_CB
    );
    status = WdfObjectAllocateContext(
        Request,
        &attributes,
        &pCb
    );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return NULL;
    }

    pCb->Memory = WDF_NO_HANDLE;
    pCb->BufferPA.QuadPart = 0;
    InitializeListHead(&pCb->ListEntry);

    status = WdfRequestRetrieveInputBuffer(Request, 0, &pCb->BufferVA, NULL);
    if (NT_SUCCESS(status))
    {
        pCb->Request = Request;
        pCb->BytesToRead = pCb->DataLen = (ULONG)Length;
        pCb->ReadPtr = pCb->BufferVA;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        pCb = NULL;
    }

    return pCb;
}

static
BOOLEAN
VIOSockRxCbInit(
    IN PDEVICE_CONTEXT  pContext
)
{
    ULONG i;
    BOOLEAN bRes = TRUE;
    WDF_OBJECT_ATTRIBUTES lockAttributes, memAttributes;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pContext->RxCbBuffersNum = VIOSOCK_CB_ENTRIES(pContext->RxPktNum);

    if (pContext->RxCbBuffersNum < pContext->RxPktNum)
        pContext->RxCbBuffersNum = pContext->RxPktNum;

    WDF_OBJECT_ATTRIBUTES_INIT(&memAttributes);
    memAttributes.ParentObject = pContext->ThisDevice;

    status = WdfLookasideListCreate(&memAttributes,
        sizeof(VIOSOCK_RX_CB), NonPagedPoolNx,
        &memAttributes, VIOSOCK_DRIVER_MEMORY_TAG,
        &pContext->RxCbBufferMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfLookasideListCreate failed: 0x%x\n", status);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ,
        "Initialize chained buffer with %u entries\n", pContext->RxCbBuffersNum);

    pContext->RxCbBuffers.Next = NULL;

    for (i = 0; i < pContext->RxCbBuffersNum; ++i)
    {
        if (!VIOSockRxCbAdd(pContext))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
                "VIOSockRxCbAdd failed, cleanup chained buffer\n");
            bRes = FALSE;
            break;
        }
    }

    if (!bRes)
        VIOSockRxCbCleanup(pContext);

    return bRes;
}

static
VOID
VIOSockRxCbCleanup(
    IN PDEVICE_CONTEXT pContext
)
{
    PVIOSOCK_RX_CB pCb;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    //no need to lock
    while (pCb = VIOSockRxCbPop(pContext))
    {
        VIOSockRxCbFree(pContext, pCb);
    }

}

//////////////////////////////////////////////////////////////////////////
__inline
VOID
VIOSockRxPktCleanup(
    IN PDEVICE_CONTEXT pContext,
    IN PVIOSOCK_RX_PKT pPkt
)
{
    ASSERT(pPkt);

    if (pPkt->Buffer)
    {
        VIOSockRxCbPush(pContext, pPkt->Buffer);
        pPkt->Buffer = NULL;
    }
}

C_ASSERT((VIOSOCK_DMA_RX_PAGES + 1) == 2);

#define VIOSOCK_PKT_PA_FROM_VA(ctx,pva) (((ctx)->RxPktPA.QuadPart) + (ULONGLONG)((PCHAR)(pva) - (PCHAR)((ctx)->RxPktVA)))

_Requires_lock_held_(pContext->RxLock)
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

    if (!pPkt->Buffer)
    {
        pPkt->Buffer = VIOSockRxCbPop(pContext);
        if (!pPkt->Buffer)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "VIOSockRxCbPop returns NULL\n");
            return FALSE;
        }
    }

    ASSERT(pPkt->Buffer->Request == WDF_NO_HANDLE);

    pPKtPA.QuadPart = VIOSOCK_PKT_PA_FROM_VA(pContext, pPkt);

    sg[0].length = sizeof(VIRTIO_VSOCK_HDR);
    sg[0].physAddr.QuadPart = pPKtPA.QuadPart + FIELD_OFFSET(VIOSOCK_RX_PKT, Header);

    sg[1].length = VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE;
    sg[1].physAddr.QuadPart = pPkt->Buffer->BufferPA.QuadPart;

    ret = virtqueue_add_buf(pContext->RxVq, sg, 0, 2, pPkt, &pPkt->IndirectDescs,
        pPKtPA.QuadPart + FIELD_OFFSET(VIOSOCK_RX_PKT, IndirectDescs));

    ASSERT(ret >= 0);
    if (ret < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Error adding buffer to Rx queue (ret = %d)\n", ret);
        VIOSockRxPktCleanup(pContext, pPkt);
        return FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return TRUE;
}

_Requires_lock_held_(pContext->RxLock)
__inline
VOID
VIOSockRxPktListProcess(
    IN PDEVICE_CONTEXT pContext
)
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

_Requires_lock_not_held_(pContext->RxLock)
__inline
VOID
VIOSockRxPktInsertOrPostpone(
    IN PDEVICE_CONTEXT pContext,
    IN PVIOSOCK_RX_PKT pPkt
)
{
    bool bNotify = false;
    WdfSpinLockAcquire(pContext->RxLock);
    if (!VIOSockRxPktInsert(pContext, pPkt))
    {
        //postpone packet
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
        virtqueue_notify(pContext->RxVq);
}

_Requires_lock_held_(pSocket->RxLock)
__inline
BOOLEAN
VIOSockRxPktInc(
    IN PSOCKET_CONTEXT  pSocket,
    IN ULONG            uPktLen
)
{
    if (pSocket->RxBytes + uPktLen > pSocket->buf_alloc)
        return FALSE;

    pSocket->RxBytes += uPktLen;
    return TRUE;

}

_Requires_lock_held_(pSocket->RxLock)
__inline
VOID
VIOSockRxPktDec(
    IN PSOCKET_CONTEXT  pSocket,
    IN ULONG            uPktLen
)
{
    pSocket->RxBytes -= uPktLen;
    pSocket->fwd_cnt += uPktLen;
}

_Requires_lock_not_held_(pContext->RxLock)
VOID
VIOSockRxVqCleanup(
    IN PDEVICE_CONTEXT pContext
)
{
    PVIOSOCK_RX_PKT pPkt;
    PSINGLE_LIST_ENTRY pListEntry;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    ASSERT(pContext->RxVq && pContext->RxPktVA);

    //drain queue
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
VIOSockRxVqInit(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    USHORT uNumEntries;
    ULONG uRingSize, uHeapSize, uBufferSize;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    pContext->RxPktList.Next = NULL;

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
    else if (VIOSockRxCbInit(pContext))
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

_Requires_lock_not_held_(pSocket->StateLock)
static
VOID
VIOSockRxPktHandleConnecting(
    IN PSOCKET_CONTEXT  pSocket,
    IN PVIOSOCK_RX_PKT  pPkt,
    IN BOOLEAN          bTxHasSpace
)
{
    WDFREQUEST  PendedRequest;
    NTSTATUS    status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = VIOSockPendedRequestGetLocked(pSocket, &PendedRequest);

    if (NT_SUCCESS(status))
    {
        if (PendedRequest == WDF_NO_HANDLE &&
            !VIOSockIsNonBlocking(pSocket))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "No PendedRequest found\n");
            status = STATUS_CANCELLED;
        }
        else
        {
            switch (pPkt->Header.op)
            {
            case VIRTIO_VSOCK_OP_RESPONSE:
                WdfSpinLockAcquire(pSocket->StateLock);
                VIOSockStateSet(pSocket, VIOSOCK_STATE_CONNECTED);
                VIOSockEventSetBit(pSocket, FD_CONNECT_BIT, STATUS_SUCCESS);
                if (bTxHasSpace)
                    VIOSockEventSetBit(pSocket, FD_WRITE_BIT, STATUS_SUCCESS);
                WdfSpinLockRelease(pSocket->StateLock);
                status = STATUS_SUCCESS;
                break;
            case VIRTIO_VSOCK_OP_INVALID:
                if (PendedRequest != WDF_NO_HANDLE)
                {
                    status = VIOSockPendedRequestSetResumeLocked(pSocket, PendedRequest);
                    if (NT_SUCCESS(status))
                        PendedRequest = WDF_NO_HANDLE;
                }
                break;
            case VIRTIO_VSOCK_OP_RST:
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
        if (pPkt->Header.op != VIRTIO_VSOCK_OP_RST)
            VIOSockSendReset(pSocket, TRUE);
    }

    if (PendedRequest)
    {
        WdfRequestComplete(PendedRequest, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
}

_Requires_lock_not_held_(pSocket->RxLock)
static
BOOLEAN
VIOSockRxPktEnqueueCb(
    IN PSOCKET_CONTEXT pSocket,
    IN PVIOSOCK_RX_PKT pPkt
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_RX_CB pCurrentCb = NULL;
    ULONG BufferFree, PktLen;
    BOOLEAN bRes = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    ASSERT(pPkt && pPkt->Buffer && pPkt->Header.len);

    PktLen = pPkt->Header.len;

    //Merge buffers
    WdfSpinLockAcquire(pSocket->RxLock);
    if (!IsListEmpty(&pSocket->RxCbList) && PktLen <= VIOSOCK_BYTES_TO_MERGE)
    {
        pCurrentCb = CONTAINING_RECORD(pSocket->RxCbList.Blink, VIOSOCK_RX_CB, ListEntry);

        BufferFree = VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE - pCurrentCb->DataLen;

        if (BufferFree >= PktLen)
        {
            if (VIOSockRxPktInc(pSocket, PktLen))
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "RxCb merged: %d + %d bytes\n", pCurrentCb->DataLen, PktLen);
                memcpy((PCHAR)pCurrentCb->BufferVA + pCurrentCb->DataLen, pPkt->Buffer->BufferVA, PktLen);
                pCurrentCb->DataLen += PktLen;
                pCurrentCb->BytesToRead += PktLen;
            }
            else
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Rx buffer full, drop packet\n");
            }
            //just leave buffer with pkt
            WdfSpinLockRelease(pSocket->RxLock);
            return FALSE;
        }
    }
    else
    {
        bRes = TRUE;    //Scan read queue
    }

    //Enqueue buffer
    if (VIOSockRxPktInc(pSocket, PktLen))
    {
        pPkt->Buffer->DataLen = PktLen;
        pPkt->Buffer->BytesToRead = PktLen;
        pPkt->Buffer->ReadPtr = pPkt->Buffer->BufferVA;
        InsertTailList(&pSocket->RxCbList, &pPkt->Buffer->ListEntry);
        pSocket->RxBuffers++;
        pPkt->Buffer = NULL; //remove buffer from pkt
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "RxCb enqueued: %d bytes\n", PktLen);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Rx buffer full, drop packet\n");
        bRes = FALSE;
    }

    WdfSpinLockRelease(pSocket->RxLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
    return bRes;
}

static
VOID
VIOSockRxRequestCancelCb(
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PVIOSOCK_RX_CB  pCb = GetRequestRxCb(Request);
    NTSTATUS status = STATUS_CANCELLED;;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pSocket->RxLock);
    VIOSockRxPktDec(pSocket, pCb->BytesToRead);
    RemoveEntryList(&pCb->ListEntry);
    WdfSpinLockRelease(pSocket->RxLock);

    if (pCb->BytesToRead != pCb->DataLen)
        status = STATUS_SUCCESS;

    WdfRequestCompleteWithInformation(Request, status, pCb->DataLen - pCb->BytesToRead);
}

_Requires_lock_not_held_(pSocket->RxLock)
NTSTATUS
VIOSockRxRequestEnqueueCb(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request,
    IN ULONG            Length
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_RX_CB  pCurrentCb = NULL;
    ULONG           BufferFree;
    NTSTATUS        status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    pCurrentCb = VIOSockRxCbEntryForRequest(pContext, Request, Length);
    if (!pCurrentCb)
        return STATUS_INSUFFICIENT_RESOURCES;

    WdfSpinLockAcquire(pSocket->RxLock);

    //Enqueue buffer
    if (VIOSockRxPktInc(pSocket, pCurrentCb->DataLen))
    {
        status = WdfRequestMarkCancelableEx(Request, VIOSockRxRequestCancelCb);
        if (NT_SUCCESS(status))
        {
            InsertTailList(&pSocket->RxCbList, &pCurrentCb->ListEntry);
            pSocket->RxBuffers++;
            status = STATUS_SUCCESS;
        }
        else
        {
            ASSERT(status == STATUS_CANCELLED);
            TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request canceled: 0x%x\n", status);
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Rx buffer full, drop packet\n");
        status = STATUS_BUFFER_TOO_SMALL;
    }

    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

static
VOID
VIOSockRxPktHandleConnected(
    IN PSOCKET_CONTEXT  pSocket,
    IN PVIOSOCK_RX_PKT  pPkt,
    IN BOOLEAN          bTxHasSpace
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    switch (pPkt->Header.op)
    {
    case VIRTIO_VSOCK_OP_RW:
        if (VIOSockRxPktEnqueueCb(pSocket, pPkt))
        {
            VIOSockEventSetBitLocked(pSocket, FD_READ_BIT, STATUS_SUCCESS);
            VIOSockReadProcessDequeueCb(pSocket);
        }
        break;
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
        if (bTxHasSpace)
        {
            VIOSockEventSetBitLocked(pSocket, FD_WRITE_BIT, STATUS_SUCCESS);
        }
        break;
    case VIRTIO_VSOCK_OP_SHUTDOWN:
        if (VIOSockShutdownFromPeer(pSocket,
            pPkt->Header.flags & VIRTIO_VSOCK_SHUTDOWN_MASK) &&
            !VIOSockRxHasData(pSocket) &&
            !VIOSockIsDone(pSocket))
        {
            VIOSockSendReset(pSocket, FALSE);
            VIOSockDoClose(pSocket);
        }
        break;
    case VIRTIO_VSOCK_OP_RST:
        VIOSockDoClose(pSocket);
        VIOSockEventSetBitLocked(pSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
}

static
VOID
VIOSockRxPktHandleDisconnecting(
    IN PSOCKET_CONTEXT pSocket,
    IN PVIOSOCK_RX_PKT pPkt
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (pPkt->Header.op == VIRTIO_VSOCK_OP_RST)
    {
        VIOSockDoClose(pSocket);

//         if (pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_MASK != VIRTIO_VSOCK_SHUTDOWN_MASK)
//         {
//             VIOSockEventSetBit(pSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
//         }
    }
}

static
VOID
VIOSockRxPktHandleListen(
    IN PSOCKET_CONTEXT pSocket,
    IN PVIOSOCK_RX_PKT pPkt
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    NTSTATUS    status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (pPkt->Header.op == VIRTIO_VSOCK_OP_RST)
    {
        //remove pended accept
        VIOSockAcceptRemovePkt(pSocket, &pPkt->Header);
        return;
    }
    else if (pPkt->Header.op != VIRTIO_VSOCK_OP_REQUEST)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid packet: %u\n", pPkt->Header.op);
        VIOSockSendResetNoSock(pContext, &pPkt->Header);
        return;
    }

    status = VIOSockAcceptEnqueuePkt(pSocket, &pPkt->Header);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockAcceptEnqueuePkt failed: 0x%x\n", status);
        VIOSockSendResetNoSock(pContext, &pPkt->Header);
    }
}

_Requires_lock_not_held_(pContext->RxLock)
VOID
VIOSockRxVqProcess(
    IN PDEVICE_CONTEXT pContext
)
{
    PVIOSOCK_RX_PKT     pPkt;
    UINT                len;
    LIST_ENTRY          CompletionList;
    PSINGLE_LIST_ENTRY  pCurrentEntry;
    PSOCKET_CONTEXT     pSocket = NULL;
    BOOLEAN             bStop = FALSE;

    NTSTATUS            status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    InitializeListHead(&CompletionList);

    WdfSpinLockAcquire(pContext->RxLock);
    do
    {
        virtqueue_disable_cb(pContext->RxVq);

        while (TRUE)
        {
            if (!VIOSockTxMoreReplies(pContext)) {
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
                break;

            /* Drop short/long packets */

            if (len < sizeof(pPkt->Header) ||
                len > sizeof(pPkt->Header) + pPkt->Header.len)
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "Short/Long packet (%d)\n", len);
                VIOSockRxPktInsert(pContext, pPkt);
                continue;
            }

            ASSERT(pPkt->Header.len == len - sizeof(pPkt->Header));

            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Recv packet %!Op! (%d:%d <-- %d:%d), len: %d, flags: %d, buf_alloc: %d, fwd_cnt: %d\n",
                pPkt->Header.op,
                (ULONG)pPkt->Header.dst_cid, pPkt->Header.dst_port,
                (ULONG)pPkt->Header.src_cid, pPkt->Header.src_port,
                pPkt->Header.len, pPkt->Header.flags, pPkt->Header.buf_alloc, pPkt->Header.fwd_cnt);

            //"complete" buffers later
            InsertTailList(&CompletionList, &pPkt->CompletionListEntry);
        }
    } while (!virtqueue_enable_cb(pContext->RxVq) && !bStop);

    WdfSpinLockRelease(pContext->RxLock);

    //complete buffers
    while (!IsListEmpty(&CompletionList))
    {
        BOOLEAN bTxHasSpace;

        pPkt = CONTAINING_RECORD(RemoveHeadList(&CompletionList), VIOSOCK_RX_PKT, CompletionListEntry);

        //find socket
        pSocket = VIOSockConnectedFindByRxPkt(pContext, &pPkt->Header);
        if (!pSocket)
        {
            //no connected socket for incoming packet
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "No connected socket found\n");
            pSocket = VIOSockBoundFindByPort(pContext, pPkt->Header.dst_port);
        }

        if (pSocket && pSocket->type != pPkt->Header.type)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid socket %d type\n", pSocket->SocketId);
            pSocket = NULL;
        }
        if (!pSocket)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Socket for packet is not exists\n");
            VIOSockSendResetNoSock(pContext, &pPkt->Header);
            VIOSockRxPktInsertOrPostpone(pContext, pPkt);
            continue;
        }


        ASSERT(pContext->Config.guest_cid == (ULONG32)pPkt->Header.dst_cid);

        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Socket %d found, state %!State!\n",
            pSocket->SocketId, VIOSockStateGet(pSocket));

        bTxHasSpace = !!VIOSockTxSpaceUpdate(pSocket, &pPkt->Header);

        switch (VIOSockStateGet(pSocket))
        {
        case VIOSOCK_STATE_CONNECTING:
            VIOSockRxPktHandleConnecting(pSocket, pPkt, bTxHasSpace);
            break;
        case VIOSOCK_STATE_CONNECTED:
            VIOSockRxPktHandleConnected(pSocket, pPkt, bTxHasSpace);
            break;
        case VIOSOCK_STATE_CLOSING:
            VIOSockRxPktHandleDisconnecting(pSocket, pPkt);
            break;
        case VIOSOCK_STATE_LISTEN:
            VIOSockRxPktHandleListen(pSocket, pPkt);
            break;
        default:
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET,
                "Invalid socket %d state for Rx packet %d\n", pSocket->SocketId, pPkt->Header.op);
        }

        //reinsert handled packet
        VIOSockRxPktInsertOrPostpone(pContext, pPkt);
    };

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

//////////////////////////////////////////////////////////////////////////
_Requires_lock_not_held_(pSocket->RxLock)
static
NTSTATUS
VIOSockReadForward(
    IN PSOCKET_CONTEXT pSocket,
    IN WDFREQUEST Request,
    IN ULONG Flags
)
{
    PVIOSOCK_RX_CONTEXT pRequest;
    WDF_OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;
    BOOLEAN bTimer = FALSE;
    SIZE_T Length;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        VIOSOCK_RX_CONTEXT
    );
    status = WdfObjectAllocateContext(
        Request,
        &attributes,
        &pRequest
    );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return status;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, 0, &pRequest->FreePtr, &Length);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }


    pRequest->BufferLen = pRequest->FreeBytes = (ULONG)Length;
    pRequest->Flags = Flags;

    if (!VIOSockIsNonBlocking(pSocket) &&
        pSocket->RecvTimeout != LONG_MAX)
    {
        pRequest->Timeout = WDF_ABS_TIMEOUT_IN_MS(pSocket->RecvTimeout);
        pRequest->Counter = 0;

        WdfSpinLockAcquire(pSocket->RxLock);
        VIOSockTimerStart(&pSocket->ReadTimer, pRequest->Timeout);
        WdfSpinLockRelease(pSocket->RxLock);

        bTimer = TRUE;
    }

    VIOSockEventClearBit(pSocket, FD_READ_BIT);

    status = WdfRequestForwardToIoQueue(Request, pSocket->ReadQueue);
    if (!NT_SUCCESS(status))
    {

        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestForwardToIoQueue failed: 0x%x\n", status);
        if (bTimer)
        {
            WdfSpinLockAcquire(pSocket->RxLock);
            VIOSockTimerDeref(&pSocket->ReadTimer, TRUE);
            WdfSpinLockRelease(pSocket->RxLock);
        }
    }

    return status;
}

static
VOID
VIOSockRead(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);
    //check Request
    if (VIOSockIsFlag(pSocket, SOCK_CONTROL))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Invalid socket %d for read\n", pSocket->SocketId);
        WdfRequestComplete(Request, STATUS_NOT_SOCKET);
        return;
    }

    status = VIOSockReadForward(pSocket, Request, 0);

    if (!NT_SUCCESS(status))
        WdfRequestComplete(Request, status);
}

NTSTATUS
VIOSockReadWithFlags(
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    NTSTATUS status;
    PVIRTIO_VSOCK_READ_PARAMS pReadParams;
    ULONG Flags = 0;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    //validate request
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pReadParams), &pReadParams, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
    }
    else if (pReadParams->Flags & ~(MSG_PEEK | MSG_WAITALL))
    {

        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ,
            "Unsupported flags: 0x%x\n", pReadParams->Flags & ~(MSG_PEEK | MSG_WAITALL));
        status = STATUS_NOT_SUPPORTED;
    }
    else if ((pReadParams->Flags & (MSG_PEEK | MSG_WAITALL)) == (MSG_PEEK | MSG_WAITALL))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ,
            "Incompatible flags: 0x%x\n", MSG_PEEK | MSG_WAITALL);
        status = STATUS_NOT_SUPPORTED;
    }
    else
    {
        Flags = pReadParams->Flags;
    }

    if (NT_SUCCESS(status))
    {
        if (NT_SUCCESS(VIOSockReadForward(pSocket, Request, Flags)))
            status = STATUS_PENDING;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

NTSTATUS
VIOSockReadQueueInit(
    IN WDFDEVICE hDevice
)
{
    PDEVICE_CONTEXT         pContext = GetDeviceContext(hDevice);
    WDF_IO_QUEUE_CONFIG     queueConfig;
    NTSTATUS                status;
    WDF_OBJECT_ATTRIBUTES   attributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = pContext->ThisDevice;

    status = WdfSpinLockCreate(
        &attributes,
        &pContext->RxLock
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfSpinLockCreate failed: 0x%x\n", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchParallel
    );

    queueConfig.EvtIoRead = VIOSockRead;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

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
        &pContext->ReadQueue
    );
    __analysis_assume(queueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfIoQueueCreate failed (Read Queue): 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(hDevice,
        pContext->ReadQueue,
        WdfRequestTypeRead);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfDeviceConfigureRequestDispatching failed (Read Queue): 0x%x\n", status);
        return status;
    }

    return STATUS_SUCCESS;
}


//only loopback buffers have request assigned
#define VIOSockIsLoopbackCb(cb) ((cb)->Request != WDF_NO_HANDLE)

_Requires_lock_not_held_(pSocket->RxLock)
BOOLEAN
VIOSockReadDequeueCb(
    IN PSOCKET_CONTEXT  pSocket
)
{
    static volatile LONG    lInProgress;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    WDFREQUEST      ReadRequest = WDF_NO_HANDLE;
    NTSTATUS        status = STATUS_SUCCESS;
    PVIOSOCK_RX_CB  pCurrentCb;
    LIST_ENTRY      LoopbackList, *pCurrentItem;
    ULONG           FreeSpace;
    BOOLEAN         bSetBit, bStop = FALSE, bRequeue = FALSE, bRestart = TRUE, bAlwaysTrue = TRUE;
    PVIOSOCK_RX_CONTEXT pRequest = NULL;
    LONGLONG llTimeout = 0;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    if (InterlockedCompareExchange(&lInProgress, 1, 0) == 1)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Another instance of VIOSockReadDequeueCb already running, stop Cb dequeue\n");
        return FALSE; //one running instance allowed
    }

    InitializeListHead(&LoopbackList);

    status = WdfIoQueueRetrieveNextRequest(pSocket->ReadQueue, &ReadRequest);
    if (!NT_SUCCESS(status))
        return FALSE;

    pRequest = GetRequestRxContext(ReadRequest);

    WdfSpinLockAcquire(pSocket->RxLock);

    if (pRequest->Timeout)
    {
        llTimeout = VIOSockTimerPassed(&pSocket->ReadTimer);
        VIOSockTimerDeref(&pSocket->ReadTimer, TRUE);
        if (llTimeout < pRequest->Timeout)
            llTimeout = pRequest->Timeout - llTimeout;
        else
            llTimeout = VIOSOCK_TIMER_TOLERANCE;
    }

    if (!VIOSockRxHasData(pSocket))
    {
        if (VIOSockIsNonBlocking(pSocket) ||
            VIOSockStateGet(pSocket) != VIOSOCK_STATE_CONNECTED)
        {
//            VIOSockEventClearBit(pSocket, FD_READ_BIT);

            //complete request
            bStop = TRUE;

            status = VIOSockStateValidate(pSocket, FALSE);

            if (NT_SUCCESS(status))
            {
                if (VIOSockIsNonBlocking(pSocket))
                {
                    status = STATUS_CANT_WAIT;
                }
                else
                {
                    ASSERT(FALSE);
                    bRequeue = TRUE;
                }
            }
            else if (status == STATUS_REMOTE_DISCONNECT)
                status = STATUS_SUCCESS; //return zero bytes on peer shutdown
        }
        else
        {
            bRequeue = TRUE;
        }
    }

    if (bRequeue)
    {
        bStop = TRUE;
        if (llTimeout && llTimeout <= VIOSOCK_TIMER_TOLERANCE)
        {
            status = STATUS_TIMEOUT;
        }
        else
        {
            status = WdfRequestRequeue(ReadRequest);
            if (NT_SUCCESS(status))
            {
                //continue timer
                if (llTimeout)
                    VIOSockTimerStart(&pSocket->ReadTimer, llTimeout);
                ReadRequest = WDF_NO_HANDLE;
                status = STATUS_SUCCESS;
            }
        }
    }

    if (bStop)
    {
        InterlockedExchange(&lInProgress, 0);
        WdfSpinLockRelease(pSocket->RxLock);

        if (ReadRequest != WDF_NO_HANDLE)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Complete request without CB dequeue: 0x%08x\n", status);
            WdfRequestCompleteWithInformation(ReadRequest, status, pRequest->BufferLen - pRequest->FreeBytes);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "No read request to complete\n");
        }
        return FALSE;
    }

    //process chained buffer
    for (pCurrentItem = pSocket->RxCbList.Flink;
        pCurrentItem != &pSocket->RxCbList;
        pCurrentItem = pCurrentItem->Flink)
    {
        //peek the first buffer
        pCurrentCb = CONTAINING_RECORD(pCurrentItem, VIOSOCK_RX_CB, ListEntry);

        //can we copy the whole CB?
        if (pRequest->FreeBytes >= pCurrentCb->BytesToRead)
        {
            memcpy(pRequest->FreePtr, pCurrentCb->ReadPtr, pCurrentCb->BytesToRead);

            //update request buffer data ptr
            pRequest->FreePtr += pCurrentCb->BytesToRead;
            pRequest->FreeBytes -= pCurrentCb->BytesToRead;

            if (!(pRequest->Flags & MSG_PEEK))
            {

                pCurrentItem = pCurrentItem->Blink;
                RemoveEntryList(&pCurrentCb->ListEntry);

                pCurrentCb->BytesToRead = 0;

                if (VIOSockIsLoopbackCb(pCurrentCb))
                {
                    status = WdfRequestUnmarkCancelable(pCurrentCb->Request);

                    if (NT_SUCCESS(status))
                    {
                        InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); //complete loopback requests later
                        VIOSockRxPktDec(pSocket, pCurrentCb->BytesToRead);
                    }
                    else
                    {
                        ASSERT(status == STATUS_CANCELLED);
                        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request is canceling: 0x%x\n", status);
                        status = STATUS_SUCCESS;
                        InitializeListHead(&pCurrentCb->ListEntry);//cancellation routine removes element from the list
                    }
                }
                else
                {
                    VIOSockRxCbPushLocked(pContext, pCurrentCb);
                    VIOSockRxPktDec(pSocket, pCurrentCb->BytesToRead);
                }
            }
        }
        else //request buffer is not big enough
        {
            memcpy(pRequest->FreePtr, pCurrentCb->ReadPtr, pRequest->FreeBytes);

            pRequest->FreeBytes = 0;

            if (!(pRequest->Flags & MSG_PEEK))
            {
                //update current CB data ptr
                pCurrentCb->ReadPtr += pRequest->FreeBytes;
                pCurrentCb->BytesToRead -= pRequest->FreeBytes;
                VIOSockRxPktDec(pSocket, pRequest->FreeBytes);
            }

            break;
        }
    }

    if (!pRequest->FreeBytes || pRequest->Flags & MSG_PEEK)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "Should complete read request\n");
    }
    else if (pRequest->BufferLen == pRequest->FreeBytes || pRequest->Flags & MSG_WAITALL)
    {
        if (llTimeout && llTimeout <= VIOSOCK_TIMER_TOLERANCE)
        {
            status = STATUS_TIMEOUT;
        }
        else
        {
            //requeue request
            status = WdfRequestRequeue(ReadRequest);
            if (NT_SUCCESS(status))
            {
                //continue timer
                if (llTimeout)
                    VIOSockTimerStart(&pSocket->ReadTimer, llTimeout);
                ReadRequest = WDF_NO_HANDLE;
                status = STATUS_SUCCESS;
                bRestart = FALSE;
            }
        }
    }

    bSetBit = (ReadRequest != WDF_NO_HANDLE && VIOSockRxHasData(pSocket));
    FreeSpace = pSocket->buf_alloc - (pSocket->fwd_cnt - pSocket->last_fwd_cnt);

    InterlockedExchange(&lInProgress, 0);
    WdfSpinLockRelease(pSocket->RxLock);

    if (bSetBit)
        VIOSockEventSetBit(pSocket, FD_READ_BIT, STATUS_SUCCESS);
    else
        VIOSockEventClearBit(pSocket, FD_READ_BIT);

    if (FreeSpace < VIRTIO_VSOCK_MAX_PKT_BUF_SIZE)
        VIOSockSendCreditUpdate(pSocket);

    if (ReadRequest != WDF_NO_HANDLE)
    {
        ASSERT(pSocket->PendedRequest == WDF_NO_HANDLE);

        WdfRequestCompleteWithInformation(ReadRequest, status, pRequest->BufferLen - pRequest->FreeBytes);
    }

    //Static Driver Verifier(SDV) tracks only one request, and when SDV unwinds the loop,
    //it treats completion of the second request as another completion of the first request.
    //The 'assume' below causes SDV to skip loop analysis.

    __analysis_assume(bAlwaysTrue == FALSE);

    //complete loopback requests (succeed and canceled)
    if (bAlwaysTrue)
    {
        while (!IsListEmpty(&LoopbackList))
        {
            pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&LoopbackList), VIOSOCK_RX_CB, ListEntry);

            //NOTE! SDV thinks we are completing INVALID request marked as cancelable,
            //but request is appeared in this list only if WdfRequestMarkCancelableEx failed
            WdfRequestCompleteWithInformation(pCurrentCb->Request,
                pCurrentCb->DataLen ? STATUS_SUCCESS : STATUS_CANCELLED,
                pCurrentCb->DataLen);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);

    return bRestart;
}

_Requires_lock_not_held_(pSocket->RxLock)
VOID
VIOSockReadCleanupCb(
    IN PSOCKET_CONTEXT pSocket
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_RX_CB  pCurrentCb;
    LIST_ENTRY      LoopbackList;
    BOOLEAN         bAlwaysTrue = TRUE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    InitializeListHead(&LoopbackList);

    WdfSpinLockAcquire(pSocket->RxLock);

    //process chained buffer
    while (!IsListEmpty(&pSocket->RxCbList))
    {
        //peek the first buffer
        pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&pSocket->RxCbList), VIOSOCK_RX_CB, ListEntry);

        if (VIOSockIsLoopbackCb(pCurrentCb))
        {
            NTSTATUS status = WdfRequestUnmarkCancelable(pCurrentCb->Request);
            if (!NT_SUCCESS(status))
            {
                ASSERT(status == STATUS_CANCELLED);
                TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request canceled\n");
                InitializeListHead(&pCurrentCb->ListEntry);//cancellation routine removes element from the list
            }
            else
                InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); //complete loopback requests later
        }
        else
            VIOSockRxCbPushLocked(pContext, pCurrentCb);
    }

    WdfSpinLockRelease(pSocket->RxLock);

    //Static Driver Verifier(SDV) tracks only one request, and when SDV unwinds the loop,
    //it treats completion of the second request as another completion of the first request.
    //The 'assume' below causes SDV to skip loop analysis.
    __analysis_assume(bAlwaysTrue == FALSE);

    //complete loopback
    if (bAlwaysTrue)
    {
        while (!IsListEmpty(&LoopbackList))
        {
            pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&LoopbackList), VIOSOCK_RX_CB, ListEntry);
            WdfRequestComplete(pCurrentCb->Request, STATUS_CANCELLED);
        }
    }

}

static
VOID
VIOSockReadSocketState(
    WDFQUEUE Queue,
    WDFCONTEXT Context
)
{
    VIOSockReadProcessDequeueCb((PSOCKET_CONTEXT)Context);
}

VOID
VIOSockReadIoCanceledOnQueue(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request
)
{
    PVIOSOCK_RX_CONTEXT pRequest = GetRequestRxContext(Request);

    ASSERT(pRequest);
    if (pRequest->BufferLen != pRequest->FreeBytes)
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, pRequest->BufferLen - pRequest->FreeBytes);
    else
        WdfRequestComplete(Request, STATUS_CANCELLED);
}

NTSTATUS
VIOSockReadSocketQueueInit(
    IN PSOCKET_CONTEXT pSocket
)
{
    WDFDEVICE               hDevice = WdfFileObjectGetDevice(pSocket->ThisSocket);
    PDEVICE_CONTEXT         pContext = GetDeviceContext(hDevice);
    WDF_OBJECT_ATTRIBUTES   queueAttributes;
    WDF_IO_QUEUE_CONFIG     queueConfig;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchManual
    );

    queueConfig.EvtIoCanceledOnQueue = VIOSockReadIoCanceledOnQueue;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ParentObject = pSocket->ThisSocket;

    //
    // By default, Static Driver Verifier (SDV) displays a warning if it
    // doesn't find the EvtIoStop callback on a power-managed queue.
    // The 'assume' below causes SDV to suppress this warning.
    //
    // No need to handle EvtIoStop:

    __analysis_assume(queueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(hDevice,
        &queueConfig,
        &queueAttributes,
        &pSocket->ReadQueue
    );
    __analysis_assume(queueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfIoQueueCreate failed (Socket Read Queue): 0x%x\n", status);
        return status;
    }

    status = WdfIoQueueReadyNotify(pSocket->ReadQueue, VIOSockReadSocketState, pSocket);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfIoQueueReadyNotify failed (Socket Read Queue): 0x%x\n", status);
        return status;
    }

    status = VIOSockTimerCreate(&pSocket->ReadTimer, pSocket->ThisSocket, VIOSockReadTimerFunc);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "VIOSockTimerCreate failed (Socket Read Queue): 0x%x\n", status);
        return status;
    }

    return STATUS_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
VOID
VIOSockReadTimerFunc(
    WDFTIMER Timer
)
{
    static LONG lCounter;
    PSOCKET_CONTEXT pSocket = GetSocketContext(WdfTimerGetParentObject(Timer));
    LONGLONG Timeout = LONGLONG_MAX;
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

            if (pRequest->Timeout && pRequest->Counter < lCounter)
            {
                if (pRequest->Timeout <= pSocket->ReadTimer.Timeout + VIOSOCK_TIMER_TOLERANCE)
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
                        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfIoQueueRetrieveFoundRequest failed: 0x%08x\n", status);
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
                    pRequest->Counter = lCounter;
                    pRequest->Timeout -= pSocket->ReadTimer.Timeout;

                    if (pRequest->Timeout < Timeout)
                        Timeout = pRequest->Timeout;

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

    if (Timeout != LONGLONG_MAX)
    {
        WdfSpinLockAcquire(pSocket->RxLock);
        VIOSockTimerSet(&pSocket->ReadTimer, Timeout);
        WdfSpinLockRelease(pSocket->RxLock);
    }

    while (!IsListEmpty(&CompletionList))
    {
        pRequest = CONTAINING_RECORD(RemoveHeadList(&CompletionList), VIOSOCK_RX_CONTEXT, ListEntry);
        WdfRequestCompleteWithInformation(pRequest->ThisRequest, STATUS_TIMEOUT, pRequest->BufferLen - pRequest->FreeBytes);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s, status: 0x%08x, counter: %u\n", __FUNCTION__, status, lCounter);
}