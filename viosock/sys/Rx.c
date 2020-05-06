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

    ULONG               DataLen;    //Valid data len (pkt.header.len)
    WDFREQUEST          Request;    //Write request for loopback
    WDFMEMORY           Memory;
}VIOSOCK_RX_CB, *PVIOSOCK_RX_CB;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSOCK_RX_CB, GetRequestRxContext);

typedef struct _VIOSOCK_RX_PKT
{
    VIRTIO_VSOCK_HDR    Header;
    PVIOSOCK_RX_CB      Buffer;     //Chained buffer
    union {
        BYTE IndirectDescs[SIZE_OF_SINGLE_INDIRECT_DESC * (1 + VIOSOCK_DMA_RX_PAGES)]; //Header + buffer
        SINGLE_LIST_ENTRY ListEntry;
    };
}VIOSOCK_RX_PKT, *PVIOSOCK_RX_PKT;

PVIOSOCK_RX_CB
VIOSockRxCbAdd(
    IN PDEVICE_CONTEXT pContext
);

BOOLEAN
VIOSockRxCbInit(
    IN PDEVICE_CONTEXT  pContext
);

VOID
VIOSockRxCbCleanup(
    IN PDEVICE_CONTEXT pContext
);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockRxCbAdd)
#pragma alloc_text (PAGE, VIOSockRxCbInit)
#pragma alloc_text (PAGE, VIOSockRxCbCleanup)
#pragma alloc_text (PAGE, VIOSockRxVqInit)
#pragma alloc_text (PAGE, VIOSockReadQueueInit)
#pragma alloc_text (PAGE, VIOSockReadSocketQueueInit)
#endif

//////////////////////////////////////////////////////////////////////////
#define VIOSockRxCbPush(c,b) PushEntryList(&(c)->RxCbBuffers, &(b)->FreeListEntry)

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
PVIOSOCK_RX_CB
VIOSockRxCbAdd(
    IN PDEVICE_CONTEXT pContext
)
{
    NTSTATUS        status;
    PVIOSOCK_RX_CB  pCb = NULL;
    WDFMEMORY       Memory;

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
            pCb = NULL;
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
                pCb = NULL;
            }
            else
            {
                VIOSockRxCbPushLocked(pContext, pCb);
            }
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
    }
    return pCb;
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

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
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return NULL;
    }

    pCb->Memory = WDF_NO_HANDLE;
    pCb->BufferPA.QuadPart = 0;
    InitializeListHead(&pCb->ListEntry);

    status = WdfRequestRetrieveInputBuffer(Request, 0, &pCb->BufferVA, NULL);
    if (NT_SUCCESS(status))
    {
        pCb->Request = Request;
        pCb->DataLen = (ULONG)Length;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        pCb = NULL;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);

    return pCb;
}

//-
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    PAGED_CODE();

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
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
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

    pPKtPA.QuadPart = pContext->RxPktPA.QuadPart +
        (ULONGLONG)((PCHAR)pPkt - (PCHAR)pContext->RxPktVA);

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

__inline
VOID
VIOSockRxPktListProcess(
    IN PDEVICE_CONTEXT pContext
)
{
    PSINGLE_LIST_ENTRY pListEntry;

    while (pListEntry = PopEntryList(&pContext->RxPktList))
    {
        PVIOSOCK_RX_PKT pPkt = CONTAINING_RECORD(pListEntry, VIOSOCK_RX_PKT, ListEntry);

        if (!VIOSockRxPktInsert(pContext, pPkt))
        {
            PushEntryList(&pContext->RxPktList, &pPkt->ListEntry);
            break;
        }
    }
}

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
        PushEntryList(&pContext->RxPktList, &pPkt->ListEntry);
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
        VIOSockRxPktCleanup(pContext, CONTAINING_RECORD(pListEntry, VIOSOCK_RX_PKT, ListEntry));
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

    PAGED_CODE();

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
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);

    return status;
}

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
            !VIOSockIsFlag(pSocket, SOCK_NON_BLOCK))
        {
            status = STATUS_CANCELLED;
        }
        else
        {
            switch (pPkt->Header.op)
            {
            case VIRTIO_VSOCK_OP_RESPONSE:
                VIOSockStateSet(pSocket, VIOSOCK_STATE_CONNECTED);
                VIOSockEventSetBit(pSocket, FD_CONNECT_BIT, STATUS_SUCCESS);
                if (bTxHasSpace)
                    VIOSockEventSetBit(pSocket, FD_WRITE_BIT, STATUS_SUCCESS);
                status = STATUS_SUCCESS;
                break;
            case VIRTIO_VSOCK_OP_INVALID:
                if (PendedRequest != WDF_NO_HANDLE)
                {
                    status = VIOSockPendedRequestSetLocked(pSocket, PendedRequest);
                    if (NT_SUCCESS(status))
                        PendedRequest = WDF_NO_HANDLE;
                }
                break;
            case VIRTIO_VSOCK_OP_RST:
                status = STATUS_CONNECTION_RESET;
                break;
            default:
                status = STATUS_CONNECTION_INVALID;
            }
        }
    }

    if (!NT_SUCCESS(status))
    {
        VIOSockEventSetBit(pSocket, FD_CONNECT_BIT, status);
        VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSE);
        if (pPkt->Header.op != VIRTIO_VSOCK_OP_RST)
            VIOSockSendReset(pSocket, TRUE);
    }

    if (PendedRequest)
        WdfRequestComplete(PendedRequest, status);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
}

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
                memcpy((PCHAR)pCurrentCb->BufferVA + pCurrentCb->DataLen, pPkt->Buffer->BufferVA, PktLen);
                pCurrentCb->DataLen += PktLen;
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
        InsertTailList(&pSocket->RxCbList, &pPkt->Buffer->ListEntry);
        pSocket->RxBuffers++;
        pPkt->Buffer = NULL; //remove buffer from pkt
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
    PVIOSOCK_RX_CB  pCb = GetRequestRxContext(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pSocket->RxLock);
    RemoveEntryList(&pCb->ListEntry);
    WdfSpinLockRelease(pSocket->RxLock);

    WdfRequestComplete(Request, STATUS_CANCELLED);
}

//SRxLock-
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
            TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request canceled\n");
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Rx buffer full, drop packet\n");
        status = STATUS_BUFFER_TOO_SMALL;
    }

    WdfSpinLockRelease(pSocket->RxLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
    return status;
}

//SRxLock-
static
VOID
VIOSockRxPktHandleConnected(
    IN PSOCKET_CONTEXT  pSocket,
    IN PVIOSOCK_RX_PKT  pPkt,
    IN BOOLEAN          bTxHasSpace
)
{
    NTSTATUS    status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    switch (pPkt->Header.op)
    {
    case VIRTIO_VSOCK_OP_RW:
        if (VIOSockRxPktEnqueueCb(pSocket, pPkt))
        {
            VIOSockEventSetBit(pSocket, FD_READ_BIT, STATUS_SUCCESS);
            VIOSockReadDequeueCb(pSocket, WDF_NO_HANDLE);
        }
        break;//TODO: Remove break?
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
        if (bTxHasSpace)
        {
            VIOSockEventSetBit(pSocket, FD_WRITE_BIT, STATUS_SUCCESS);
        }
        break;
    case VIRTIO_VSOCK_OP_SHUTDOWN:
        if (VIOSockShutdownFromPeer(pSocket,
            pPkt->Header.flags & VIRTIO_VSOCK_SHUTDOWN_MASK))
        {
            VIOSockSendReset(pSocket, FALSE);
        }
        break;
    case VIRTIO_VSOCK_OP_RST:
        VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSING);
        VIOSockShutdownFromPeer(pSocket, VIRTIO_VSOCK_SHUTDOWN_MASK);
        VIOSockEventSetBit(pSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
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
        VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSE);
        VIOSockShutdownFromPeer(pSocket, VIRTIO_VSOCK_SHUTDOWN_MASK);

//         if (pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_MASK != VIRTIO_VSOCK_SHUTDOWN_MASK)
//         {
//             VIOSockEventSetBit(pSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
//         }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
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
    BOOLEAN             bStop = FALSE;

    NTSTATUS            status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    CompletionList.Next = NULL;

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
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "Short/Long packet\n");
                VIOSockRxPktInsert(pContext, pPkt);
                continue;
            }

            ASSERT(pPkt->Header.len == len - sizeof(pPkt->Header));

            //"complete" buffers later
            PushEntryList(&CompletionList, &pPkt->ListEntry);
        }
    } while (!virtqueue_enable_cb(pContext->RxVq) && !bStop);

    WdfSpinLockRelease(pContext->RxLock);

    //complete buffers
    while ((pCurrentEntry = PopEntryList(&CompletionList)) != NULL)
    {
        BOOLEAN bTxHasSpace;

        pPkt = CONTAINING_RECORD(pCurrentEntry, VIOSOCK_RX_PKT, ListEntry);

        //find socket
        pSocket = VIOSockConnectedFindByRxPkt(pContext, &pPkt->Header);
        if (!pSocket)
        {
            //no connected socket for incoming packet
            pSocket = VIOSockBoundFindByPort(pContext, pPkt->Header.dst_port);
        }

        if (pSocket && pSocket->type != pPkt->Header.type)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid socket state or type\n");
            pSocket = NULL;
        }
        if (!pSocket)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Socket for packet is not exists\n");
            VIOSockSendResetNoSock(pContext, &pPkt->Header);
            VIOSockRxPktInsertOrPostpone(pContext, pPkt);
            continue;
        }


        //Update CID in case it has changed after a transport reset event
        //pContext->Config.guest_cid = (ULONG32)pPkt->Header.dst_cid;
        ASSERT(pContext->Config.guest_cid == (ULONG32)pPkt->Header.dst_cid);

        bTxHasSpace = !!VIOSockTxSpaceUpdate(pSocket, &pPkt->Header);

        switch (pSocket->State)
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
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid socket state for Rx packet\n");
        }

        //reinsert handled packet
        VIOSockRxPktInsertOrPostpone(pContext, pPkt);
    };

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

//////////////////////////////////////////////////////////////////////////
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
        TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Invalid read request\n");
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    status = WdfRequestForwardToIoQueue(Request, pSocket->ReadQueue);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestForwardToIoQueue failed: 0x%x\n", status);
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VIOSockReadWithFlags(
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    NTSTATUS status;
    PVIRTIO_VSOCK_READ_PARAMS pReadParams;

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
        PVOID pBuffer;
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &pBuffer, NULL);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
                "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        }
        else
            WdfRequestSetInformation(Request, pReadParams->Flags);
    }

    if (NT_SUCCESS(status))
    {
        VIOSockEventClearBit(pSocket, FD_READ_BIT);

        status = WdfRequestForwardToIoQueue(Request, pSocket->ReadQueue);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_READ, "WdfRequestForwardToIoQueue failed: 0x%x\n", status);
        }
        else
            status = STATUS_PENDING;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VIOSockReadQueueInit(
    IN WDFDEVICE hDevice
)
{
    PDEVICE_CONTEXT              pContext = GetDeviceContext(hDevice);
    WDF_IO_QUEUE_CONFIG          queueConfig;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;

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
        return FALSE;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchParallel
    );

    queueConfig.EvtIoRead = VIOSockRead;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    status = WdfIoQueueCreate(hDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pContext->ReadQueue
    );

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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOSockReadGetRequestParameters(
    IN WDFREQUEST       Request,
    OUT PVOID           *pBuffer,
    OUT ULONG           *pLength,
    OUT ULONG           *pFlags
)
{
    NTSTATUS        status;
    WDF_REQUEST_PARAMETERS  parameters;
    size_t  stLength;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WDF_REQUEST_PARAMETERS_INIT(&parameters);
    WdfRequestGetParameters(Request, &parameters);

    if (parameters.Type == WdfRequestTypeDeviceControl)
    {
        *pFlags = (ULONG)WdfRequestGetInformation(Request);
        WdfRequestSetInformation(Request, 0);
    }
    else
        *pFlags = 0;

    status = WdfRequestRetrieveOutputBuffer(Request, 0, pBuffer, &stLength);
    if (!NT_SUCCESS(status))
    {
        ASSERT(FALSE);
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
    }
    else
    {
        *pLength = (ULONG)stLength;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);

    return status;
}

//SRxLock-
VOID
VIOSockReadDequeueCb(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       ReadRequest OPTIONAL
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    NTSTATUS        status;
    ULONG           ReadRequestFlags = 0;
    PCHAR           ReadRequestPtr = NULL;
    ULONG           ReadRequestFree, ReadRequestLength;
    PVIOSOCK_RX_CB  pCurrentCb;
    LIST_ENTRY      LoopbackList, *pCurrentItem;
    ULONG           FreeSpace;
    BOOLEAN         bSetBit, bStop = FALSE, bPend = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pSocket->RxLock);

    if (VIOSockRxHasData(pSocket) ||
        VIOSockIsFlag(pSocket, SOCK_NON_BLOCK) ||
        VIOSockStateGet(pSocket) != VIOSOCK_STATE_CONNECTED)
    {
        if (ReadRequest != WDF_NO_HANDLE)
        {
            VIOSockEventClearBit(pSocket, FD_READ_BIT);

            status = VIOSockReadGetRequestParameters(
                ReadRequest,
                &ReadRequestPtr,
                &ReadRequestLength,
                &ReadRequestFlags);

            ReadRequestFree = ReadRequestLength;
        }
        else
        {
            status = VIOSockPendedRequestGet(pSocket, &ReadRequest);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Pended Read request canceled\n");
                status = STATUS_SUCCESS; //do not complete canceled request
                ASSERT(ReadRequest == WDF_NO_HANDLE);
            }
            else if (ReadRequest != WDF_NO_HANDLE)
            {
                ReadRequestPtr = pSocket->ReadRequestPtr;
                ReadRequestFree = pSocket->ReadRequestFree;
                ReadRequestLength = pSocket->ReadRequestLength;
                ReadRequestFlags = pSocket->ReadRequestFlags;
            }
        }

        if (ReadRequest == WDF_NO_HANDLE || !NT_SUCCESS(status))
        {
            bStop = TRUE;
        }
        else if (!VIOSockRxHasData(pSocket))
        {
            VIOSOCK_STATE State = VIOSockStateGet(pSocket);

            //complete request
            bStop = TRUE;
            if (State != VIOSOCK_STATE_CONNECTED)
            {
                //TODO: set appropriate status
                if (State == VIOSOCK_STATE_CONNECTING)
                    status = STATUS_CONNECTION_DISCONNECTED;
                else if (State == VIOSOCK_STATE_LISTEN)
                    status = STATUS_INVALID_PARAMETER;
                else
                    status = STATUS_SUCCESS; //return zero bytes on closing/close
            }
            else if (VIOSockIsFlag(pSocket, SOCK_NON_BLOCK))
            {
                status = STATUS_CANT_WAIT;
            }
            else
            {
                ASSERT(FALSE);
                bPend = TRUE;
            }
        }
    }
    else
    {
        bPend = TRUE;
    }

    if (bPend)
    {
        bStop = TRUE;

        ASSERT(ReadRequest != WDF_NO_HANDLE);
        if (ReadRequest != WDF_NO_HANDLE)
        {
            status = VIOSockReadGetRequestParameters(
                ReadRequest,
                &pSocket->ReadRequestPtr,
                &pSocket->ReadRequestLength,
                &pSocket->ReadRequestFlags);

            if (NT_SUCCESS(status))
            {
                pSocket->ReadRequestFree = pSocket->ReadRequestLength;
                status = VIOSockPendedRequestSet(pSocket, ReadRequest);
                if (NT_SUCCESS(status))
                {
                    ReadRequest = WDF_NO_HANDLE;
                }
            }
        }
    }

    if (ReadRequest == WDF_NO_HANDLE || bStop)
    {
        WdfSpinLockRelease(pSocket->RxLock);

        if (ReadRequest != WDF_NO_HANDLE)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "Complete request without CB dequeue: 0x%x\n", status);
            WdfRequestComplete(ReadRequest, status);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_READ, "No read request available\n");
        }
        return;
    }

    ASSERT(ReadRequestPtr);

    InitializeListHead(&LoopbackList);

    //process chained buffer
    for (pCurrentItem = pSocket->RxCbList.Flink;
        pCurrentItem != &pSocket->RxCbList;
        pCurrentItem = pCurrentItem->Flink)
    {
        //peek the first buffer
        pCurrentCb = CONTAINING_RECORD(pCurrentItem, VIOSOCK_RX_CB, ListEntry);

        if (pCurrentCb->Request != WDF_NO_HANDLE)
        {
            status = WdfRequestUnmarkCancelable(pCurrentCb->Request);
            if (!NT_SUCCESS(status))
            {
                ASSERT(status == STATUS_CANCELLED);
                RemoveEntryList(&pCurrentCb->ListEntry);
                pSocket->RxCbReadPtr = NULL;
                pSocket->RxCbReadLen = 0;
                TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request canceled\n");
                continue;
            }
        }

        if (!pSocket->RxCbReadPtr)
        {
            pSocket->RxCbReadPtr = pCurrentCb->BufferVA;
            pSocket->RxCbReadLen = pCurrentCb->DataLen;
        }

        //can we copy the whole buffer?
        if (ReadRequestFree >= pSocket->RxCbReadLen)
        {
            memcpy(ReadRequestPtr, pSocket->RxCbReadPtr, pSocket->RxCbReadLen);

            //update request buffer
            ReadRequestPtr += pSocket->RxCbReadLen;
            ReadRequestFree -= pSocket->RxCbReadLen;


            if (!(ReadRequestFlags & MSG_PEEK))
            {
                VIOSockRxPktDec(pSocket, pCurrentCb->DataLen);

                ASSERT(pCurrentItem->Blink == &pSocket->RxCbList);
                RemoveEntryList(pCurrentItem);
                pCurrentItem = &pSocket->RxCbList; //set current item pointer to the list head

                if (pCurrentCb->Request != WDF_NO_HANDLE)
                    InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); //complete loopback requests later
                else
                    VIOSockRxCbPushLocked(pContext, pCurrentCb);

            }

            pSocket->RxCbReadPtr = NULL;
            pSocket->RxCbReadLen = 0;
        }
        else
        {
            memcpy(ReadRequestPtr, pSocket->RxCbReadPtr, ReadRequestFree);

            ReadRequestFree = 0;

            if (!(ReadRequestFlags & MSG_PEEK))
            {
                //update buffer entry
                pSocket->RxCbReadPtr += ReadRequestLength;
                pSocket->RxCbReadLen -= ReadRequestLength;
            }

            if (pCurrentCb->Request != WDF_NO_HANDLE)
            {
                status = WdfRequestMarkCancelableEx(pCurrentCb->Request, VIOSockRxRequestCancelCb);
                if (!NT_SUCCESS(status))
                {
                    TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request canceled\n");
                    pSocket->RxCbReadPtr = NULL;
                    pSocket->RxCbReadLen = 0;
                    RemoveEntryList(pCurrentItem);
                    pCurrentCb->DataLen -= pSocket->RxCbReadLen;
                    InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); //complete loopback requests later
                }
            }
            break;
        }
    }

    if (!ReadRequestFree || ReadRequestFlags & MSG_PEEK)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "Should complete request\n");
    }
    else if (ReadRequestLength == ReadRequestFree || ReadRequestFlags & MSG_WAITALL)
    {
        //pend request
        ASSERT(!(ReadRequestFlags & MSG_PEEK));
        pSocket->ReadRequestPtr = ReadRequestPtr;
        pSocket->ReadRequestFree = ReadRequestFree;
        pSocket->ReadRequestLength = ReadRequestLength;
        pSocket->ReadRequestFlags = ReadRequestFlags;

        status = VIOSockPendedRequestSet(pSocket, ReadRequest);
        if (!NT_SUCCESS(status))
        {
            ASSERT(status == STATUS_CANCELLED);
            if (status == STATUS_CANCELLED)
                ReadRequest = WDF_NO_HANDLE; //already completed

            ReadRequestLength = ReadRequestFree = 0;
        }
        else
            ReadRequest = WDF_NO_HANDLE;
    }

    bSetBit = (ReadRequest != WDF_NO_HANDLE && VIOSockRxHasData(pSocket));
    FreeSpace = pSocket->buf_alloc - (pSocket->fwd_cnt - pSocket->last_fwd_cnt);

    WdfSpinLockRelease(pSocket->RxLock);

    if (bSetBit)
        VIOSockEventSetBit(pSocket, FD_READ_BIT, STATUS_SUCCESS);

    if (FreeSpace < VIRTIO_VSOCK_MAX_PKT_BUF_SIZE)
        VIOSockSendCreditUpdate(pSocket);

    if (ReadRequest != WDF_NO_HANDLE)
    {
        ASSERT(pSocket->PendedRequest == WDF_NO_HANDLE);
        //pSocket->ReadRequestPtr = NULL;
        //pSocket->ReadRequestFree = 0;

        WdfRequestCompleteWithInformation(ReadRequest, status, ReadRequestLength - ReadRequestFree);
        status = STATUS_MORE_ENTRIES;
    }
    else
        status = STATUS_SUCCESS;

    //complete loopback
    while (!IsListEmpty(&LoopbackList))
    {
        pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&LoopbackList), VIOSOCK_RX_CB, ListEntry);
        WdfRequestCompleteWithInformation(pCurrentCb->Request, STATUS_SUCCESS, pCurrentCb->DataLen);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}

VOID
VIOSockReadCleanupCb(
    IN PSOCKET_CONTEXT pSocket
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PVIOSOCK_RX_CB  pCurrentCb;
    LIST_ENTRY      LoopbackList;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    InitializeListHead(&LoopbackList);

    WdfSpinLockAcquire(pSocket->RxLock);

    pSocket->RxCbReadLen = 0;
    pSocket->RxCbReadPtr = NULL;

    //process chained buffer
    while (!IsListEmpty(&pSocket->RxCbList))
    {
        //peek the first buffer
        pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&pSocket->RxCbList), VIOSOCK_RX_CB, ListEntry);

        if (pCurrentCb->Request)
        {
            NTSTATUS status = WdfRequestUnmarkCancelable(pCurrentCb->Request);
            if (!NT_SUCCESS(status))
            {
                ASSERT(status == STATUS_CANCELLED);
                TraceEvents(TRACE_LEVEL_WARNING, DBG_READ, "Loopback request canceled\n");
            }
            else
                InsertTailList(&LoopbackList, &pCurrentCb->ListEntry); //complete loopback requests later
        }
        else
            VIOSockRxCbPushLocked(pContext, pCurrentCb);
    }

    WdfSpinLockRelease(pSocket->RxLock);

    //complete loopback
    while (!IsListEmpty(&LoopbackList))
    {
        pCurrentCb = CONTAINING_RECORD(RemoveHeadList(&LoopbackList), VIOSOCK_RX_CB, ListEntry);
        WdfRequestComplete(pCurrentCb->Request, STATUS_CANCELLED);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);
}

static
VOID
VIOSockReadSocketIoDefault(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    VIOSockReadDequeueCb(pSocket, Request);
}

static
VOID
VIOSockReadSocketIoStop(IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN ULONG ActionFlags)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> %s\n", __FUNCTION__);

    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        WdfRequestStopAcknowledge(Request, FALSE);
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {

        if (ActionFlags & WdfRequestStopRequestCancelable)
        {
            PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);

            WdfSpinLockAcquire(pSocket->RxLock);
            if (pSocket->PendedRequest == Request)
            {
                pSocket->PendedRequest = WDF_NO_HANDLE;
                WdfObjectDereference(Request);
            }
            WdfSpinLockRelease(pSocket->RxLock);

            if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
            {
                WdfRequestComplete(Request, STATUS_CANCELLED);
            }
        }
    }

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
        WdfIoQueueDispatchSequential
    );

    queueConfig.EvtIoDefault = VIOSockReadSocketIoDefault;
    queueConfig.EvtIoStop = VIOSockReadSocketIoStop;
    queueConfig.AllowZeroLengthRequests = WdfFalse;

    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ParentObject = pSocket->ThisSocket;

    status = WdfIoQueueCreate(hDevice,
        &queueConfig,
        &queueAttributes,
        &pSocket->ReadQueue
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfIoQueueCreate failed (Socket Read Queue): 0x%x\n", status);
        return status;
    }

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
            "WdfIoQueueReadyNotify failed (Socket Read Queue): 0x%x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}
