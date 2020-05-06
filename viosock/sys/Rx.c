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
    WDFMEMORY           Memory;
}VIOSOCK_RX_CB, *PVIOSOCK_RX_CB;
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
    IN PVIOSOCK_RX_PKT  pPkt
)
{
    WDFREQUEST  PendedRequest;
    NTSTATUS    status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = VIOSockPendedRequestGet(pSocket, &PendedRequest);

    if (NT_SUCCESS(status))
    {
        if (PendedRequest == WDF_NO_HANDLE)
        {
            status = STATUS_CANCELLED;
        }
        else
        {
            switch (pPkt->Header.op)
            {
            case VIRTIO_VSOCK_OP_RESPONSE:
                VIOSockStateSet(pSocket, VIOSOCK_STATE_CONNECTED);
                status = STATUS_SUCCESS;
                break;
            case VIRTIO_VSOCK_OP_INVALID:
                if (PendedRequest != WDF_NO_HANDLE)
                {
                    status = VIOSockPendedRequestSet(pSocket, PendedRequest);
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
        VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSE);
        if (pPkt->Header.op != VIRTIO_VSOCK_OP_RST)
            VIOSockSendReset(pSocket, TRUE);
    }

    if (PendedRequest)
        WdfRequestComplete(PendedRequest, status);

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

        //find socket
        pSocket = VIOSockBoundFindByPort(pContext, pPkt->Header.dst_port);

        if (!pSocket)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Socket for packet is not exists\n");
            WdfSpinLockAcquire(pContext->RxLock);
            VIOSockRxPktInsert(pContext, pPkt);
            WdfSpinLockRelease(pContext->RxLock);
            virtqueue_kick(pContext->RxVq);
            continue;
        }


        //Update CID in case it has changed after a transport reset event
        //pContext->Config.guest_cid = (ULONG32)pPkt->Header.dst_cid;
        ASSERT(pContext->Config.guest_cid == (ULONG32)pPkt->Header.dst_cid);

        switch (pSocket->State)
        {
        case VIOSOCK_STATE_CONNECTING:
            VIOSockRxPktHandleConnecting(pSocket, pPkt);
            break;
        default:
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid socket state for Rx packet\n");
        }

        //reinsert handled packet
        WdfSpinLockAcquire(pContext->RxLock);
        VIOSockRxPktInsert(pContext, pPkt);
        WdfSpinLockRelease(pContext->RxLock);
        virtqueue_kick(pContext->RxVq);
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
VOID
VIOSockReadSocketIoDefault(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request
)
{
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
