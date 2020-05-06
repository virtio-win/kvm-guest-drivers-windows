/*
 * Socket object functions
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
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
#include "Socket.tmh"
#endif

NTSTATUS
VIOSockBind(
    IN WDFREQUEST   Request
);

NTSTATUS
VIOSockConnect(
    IN WDFREQUEST   Request
);

NTSTATUS
VIOSockListen(
    IN WDFREQUEST   Request
);

NTSTATUS
VIOSockGetPeerName(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

NTSTATUS
VIOSockGetSockName(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

NTSTATUS
VIOSockGetSockOpt(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

NTSTATUS
VIOSockSetSockOpt(
    IN WDFREQUEST   Request
);

NTSTATUS
VIOSockIoctl(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockCreateStub)
#pragma alloc_text (PAGE, VIOSockClose)

#pragma alloc_text (PAGE, VIOSockBind)
#pragma alloc_text (PAGE, VIOSockConnect)
#pragma alloc_text (PAGE, VIOSockListen)
#pragma alloc_text (PAGE, VIOSockGetPeerName)
#pragma alloc_text (PAGE, VIOSockGetSockName)
#pragma alloc_text (PAGE, VIOSockGetSockOpt)
#pragma alloc_text (PAGE, VIOSockSetSockOpt)
#pragma alloc_text (PAGE, VIOSockIoctl)

#pragma alloc_text (PAGE, VIOSockDeviceControl)
#pragma alloc_text (PAGE, VIOSockSelect)
#endif

//////////////////////////////////////////////////////////////////////////
NTSTATUS
VIOSockBoundListInit(
    IN WDFDEVICE hDevice
)
{
    NTSTATUS                status;
    PDEVICE_CONTEXT         pContext = GetDeviceContext(hDevice);
    WDF_OBJECT_ATTRIBUTES   collectionAttributes;

    // Create collection for socket objects
    WDF_OBJECT_ATTRIBUTES_INIT(&collectionAttributes);
    collectionAttributes.ParentObject = hDevice;

    status = WdfCollectionCreate(&collectionAttributes, &pContext->BoundList);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfCollectionCreate failed - 0x%x\n", status);
    }
    else
    {
        WDF_OBJECT_ATTRIBUTES   lockAttributes;

        WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
        lockAttributes.ParentObject = hDevice;
        status = WdfSpinLockCreate(&lockAttributes, &pContext->BoundLock);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfSpinLockCreate failed - 0x%x\n", status);
        }
    }
    return status;
}

NTSTATUS
VIOSockBoundAdd(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32         svm_port
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    static ULONG32  uSvmPort;
    ULONG           uSeed = (ULONG)(ULONG_PTR)pSocket;
    NTSTATUS        status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (svm_port == VMADDR_PORT_ANY)
    {
        BOOLEAN bFound = FALSE;
        ULONG i;

        for (i = 0; i < MAX_PORT_RETRIES; ++i)
        {
            if (!uSvmPort)
                uSvmPort = RtlRandomEx((PULONG)&uSeed);

            if (uSvmPort == VMADDR_PORT_ANY)
            {
                uSvmPort = 0;
                continue;
            }

            if (uSvmPort <= LAST_RESERVED_PORT)
                uSvmPort += LAST_RESERVED_PORT + 1;

            svm_port = uSvmPort++;

            if (!VIOSockBoundFindByPort(pContext, svm_port))
            {
                bFound = TRUE;
                break;
            }
        }

        if (!bFound)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "No ports available\n");
            return STATUS_NOT_FOUND;
        }
    }
    else
    {
        if (VIOSockBoundFindByPort(pContext, svm_port))
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Local port %u is already in use\n", svm_port);
            return STATUS_ADDRESS_ALREADY_ASSOCIATED;
        }
    }

    pSocket->src_port = svm_port;

    WdfSpinLockAcquire(pContext->BoundLock);
    status = WdfCollectionAdd(pContext->BoundList, pSocket->ThisSocket);
    if (NT_SUCCESS(status))
        VIOSockSetFlag(pSocket, SOCK_BOUND);
    WdfSpinLockRelease(pContext->BoundLock);
    return status;
}

__inline
VOID
VIOSockBoundRemove(
    IN PSOCKET_CONTEXT pSocket
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(pSocket->ThisSocket));

    WdfSpinLockAcquire(pContext->BoundLock);
    if (VIOSockIsFlag(pSocket, SOCK_BOUND))
    {
        WdfCollectionRemove(pContext->BoundList, pSocket->ThisSocket);
        VIOSockResetFlag(pSocket, SOCK_BOUND);
    }
    WdfSpinLockRelease(pContext->BoundLock);
}

typedef
BOOLEAN
(*PSOCKET_CALLBACK)(
    IN PSOCKET_CONTEXT pSocket,
    IN PVOID pCallbackContext
    );

PSOCKET_CONTEXT
VIOSockBoundEnum(
    IN PDEVICE_CONTEXT  pContext,
    IN PSOCKET_CALLBACK pEnumCallback,
    IN PVOID            pCallbackContext
)
{
    PSOCKET_CONTEXT pSocket = NULL;
    ULONG i, ItemCount;

    WdfSpinLockAcquire(pContext->BoundLock);
    ItemCount = WdfCollectionGetCount(pContext->BoundList);
    for (i = 0; i < ItemCount; ++i)
    {
        PSOCKET_CONTEXT pCurrentSocket = GetSocketContext(WdfCollectionGetItem(pContext->BoundList, i));

        if (pEnumCallback(pCurrentSocket, pCallbackContext))
        {
            pSocket = pCurrentSocket;
            break;
        }
    }
    WdfSpinLockRelease(pContext->BoundLock);

    return pSocket;
}

static
BOOLEAN
VIOSockBoundFindByPortCallback(
    IN PSOCKET_CONTEXT  pSocket,
    IN PVOID            pCallbackContext
)
{
    return pSocket->src_port == (ULONG32)(ULONG_PTR)pCallbackContext;
}

PSOCKET_CONTEXT
VIOSockBoundFindByPort(
    IN PDEVICE_CONTEXT pContext,
    IN ULONG32         ulSrcPort
)
{
    return VIOSockBoundEnum(pContext, VIOSockBoundFindByPortCallback, (PVOID)ulSrcPort);
}

static
BOOLEAN
VIOSockFindByFileCallback(
    IN PSOCKET_CONTEXT  pSocket,
    IN PVOID            pCallbackContext
)
{
    return WdfFileObjectWdmGetFileObject(pSocket->ThisSocket) ==
        (PFILE_OBJECT)pCallbackContext;
}

PSOCKET_CONTEXT
VIOSockBoundFindByFile(
    IN PDEVICE_CONTEXT pContext,
    IN PFILE_OBJECT pFileObject
)
{
    return VIOSockBoundEnum(pContext, VIOSockFindByFileCallback, pFileObject);
}

NTSTATUS
VIOSockConnectedListInit(
    IN WDFDEVICE hDevice
)
{
    NTSTATUS                status;
    PDEVICE_CONTEXT         pContext = GetDeviceContext(hDevice);
    WDF_OBJECT_ATTRIBUTES   collectionAttributes;

    // Create collection for socket objects
    WDF_OBJECT_ATTRIBUTES_INIT(&collectionAttributes);
    collectionAttributes.ParentObject = hDevice;

    status = WdfCollectionCreate(&collectionAttributes, &pContext->ConnectedList);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfCollectionCreate failed - 0x%x\n", status);
    }
    else
    {
        WDF_OBJECT_ATTRIBUTES   lockAttributes;

        WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
        lockAttributes.ParentObject = hDevice;
        status = WdfSpinLockCreate(&lockAttributes, &pContext->ConnectedLock);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfSpinLockCreate failed - 0x%x\n", status);
        }
    }

    return status;
}

__inline
NTSTATUS
VIOSockConnectedAdd(
    IN PSOCKET_CONTEXT pSocket
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(pSocket->ThisSocket));
    NTSTATUS status;

    WdfSpinLockAcquire(pContext->ConnectedLock);
    status = WdfCollectionAdd(pContext->ConnectedList, pSocket->ThisSocket);
    WdfSpinLockRelease(pContext->ConnectedLock);
    return status;
}

__inline
VOID
VIOSockConnectedRemove(
    IN PSOCKET_CONTEXT pSocket
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(pSocket->ThisSocket));

    WdfSpinLockAcquire(pContext->ConnectedLock);
    WdfCollectionRemove(pContext->ConnectedList, pSocket->ThisSocket);
    WdfSpinLockRelease(pContext->ConnectedLock);
}

PSOCKET_CONTEXT
VIOSockConnectedEnum(
    IN PDEVICE_CONTEXT  pContext,
    IN PSOCKET_CALLBACK pEnumCallback,
    IN PVOID            pCallbackContext
)
{
    PSOCKET_CONTEXT pSocket = NULL;
    ULONG i, ItemCount;

    WdfSpinLockAcquire(pContext->ConnectedLock);
    ItemCount = WdfCollectionGetCount(pContext->ConnectedList);
    for (i = 0; i < ItemCount; ++i)
    {
        PSOCKET_CONTEXT pCurrentSocket = GetSocketContext(WdfCollectionGetItem(pContext->ConnectedList, i));

        if (pEnumCallback(pCurrentSocket, pCallbackContext))
        {
            pSocket = pCurrentSocket;
            break;
        }
    }
    WdfSpinLockRelease(pContext->ConnectedLock);

    return pSocket;
}

BOOLEAN
VIOSockConnectedFindByRxPktCallback(
    IN PSOCKET_CONTEXT  pSocket,
    IN PVOID            pCallbackContext
)
{
    PVIRTIO_VSOCK_HDR    pPkt = (PVIRTIO_VSOCK_HDR)pCallbackContext;
    return (pPkt->src_cid == pSocket->dst_cid &&
        pPkt->src_port == pSocket->dst_port);
}

PSOCKET_CONTEXT
VIOSockConnectedFindByRxPkt(
    IN PDEVICE_CONTEXT      pContext,
    IN PVIRTIO_VSOCK_HDR    pPkt
)
{
    return VIOSockConnectedEnum(pContext, VIOSockConnectedFindByRxPktCallback, pPkt);
}

PSOCKET_CONTEXT
VIOSockConnectedFindByFile(
    IN PDEVICE_CONTEXT  pContext,
    IN PFILE_OBJECT     pFileObject
)
{
    return VIOSockConnectedEnum(pContext, VIOSockFindByFileCallback, pFileObject);
}

VIOSOCK_STATE
VIOSockStateSet(
    IN PSOCKET_CONTEXT pSocket,
    IN VIOSOCK_STATE   NewState
)
{
    VIOSOCK_STATE PrevState;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "%u --> %u\n", pSocket->State, NewState);


    PrevState = InterlockedExchange((PLONG)&pSocket->State, NewState);

    if (PrevState != NewState)
    {
        if (PrevState == VIOSOCK_STATE_CLOSING)
        {
            ASSERT(NewState == VIOSOCK_STATE_CLOSE);
            if(!IsLoopbackSocket(pSocket))
                VIOSockConnectedRemove(pSocket);
        }
        else if (NewState == VIOSOCK_STATE_CONNECTED)
        {
            ASSERT(PrevState == VIOSOCK_STATE_CONNECTING || IsLoopbackSocket(pSocket));
            if (!IsLoopbackSocket(pSocket))
                VIOSockConnectedAdd(pSocket);
        }
    }

    return PrevState;
}

__inline
BOOLEAN
VIOSockIsBound(
    IN PSOCKET_CONTEXT pSocket
)
{
    return pSocket->src_port != VMADDR_PORT_ANY;
}

//////////////////////////////////////////////////////////////////////////
static
VOID
VIOSockPendedRequestCancel(
    IN WDFREQUEST Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pSocket->RxLock);
    WdfObjectDereference(Request);
    pSocket->PendedRequest = WDF_NO_HANDLE;
    WdfSpinLockRelease(pSocket->RxLock);

    WdfRequestComplete(Request, STATUS_CANCELLED);
}

NTSTATUS
VIOSockPendedRequestSet(
    IN PSOCKET_CONTEXT pSocket,
    IN WDFREQUEST Request
)
{
    NTSTATUS status;

    ASSERT(pSocket->PendedRequest == WDF_NO_HANDLE);
    pSocket->PendedRequest = Request;
    WdfObjectReference(Request);

    status = WdfRequestMarkCancelableEx(pSocket->PendedRequest, VIOSockPendedRequestCancel);
    if (!NT_SUCCESS(status))
    {
        ASSERT(status == STATUS_CANCELLED);
        pSocket->PendedRequest = WDF_NO_HANDLE;
        WdfObjectDereference(Request);

        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Pended request canceled\n");
    }
    return status;
}

NTSTATUS
VIOSockPendedRequestSetLocked(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request
)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->RxLock);
    status = VIOSockPendedRequestSet(pSocket, Request);
    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

NTSTATUS
VIOSockPendedRequestGet(
    IN  PSOCKET_CONTEXT pSocket,
    OUT WDFREQUEST *Request
)
{
    NTSTATUS    status = STATUS_SUCCESS;;

    *Request = pSocket->PendedRequest;
    if (*Request != WDF_NO_HANDLE)
    {
        status = WdfRequestUnmarkCancelable(*Request);
        ASSERT(NT_SUCCESS(status));
        if (!NT_SUCCESS(status))
        {
            ASSERT(status == STATUS_CANCELLED && pSocket->PendedRequest == WDF_NO_HANDLE);
            *Request = WDF_NO_HANDLE; //do not complete canceled request
            status = STATUS_CANCELLED;
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Pended request canceled\n");
        }
        else
        {
            WdfObjectDereference(*Request);
            pSocket->PendedRequest = WDF_NO_HANDLE;
        }
    }

    return status;
}

NTSTATUS
VIOSockPendedRequestGetLocked(
    IN PSOCKET_CONTEXT  pSocket,
    OUT WDFREQUEST      *Request
)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->RxLock);
    status = VIOSockPendedRequestGet(pSocket, Request);
    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

//////////////////////////////////////////////////////////////////////////
NTSTATUS
VIOSockAcceptEnqueuePkt(
    IN PSOCKET_CONTEXT      pListenSocket,
    IN PVIRTIO_VSOCK_HDR    pPkt
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pListenSocket);
    NTSTATUS        status;
    WDFMEMORY       Memory;
    LONG            lAcceptPended;

    lAcceptPended = InterlockedIncrement(&pListenSocket->AcceptPended);
    if (lAcceptPended > pListenSocket->Backlog)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Accept list is full\n");
        InterlockedDecrement(&pListenSocket->AcceptPended);
        return STATUS_CONNECTION_RESET;
    }

    //accepted list is empty
    if (lAcceptPended == 1)
    {
        WDFREQUEST PendedRequest;

        VIOSockPendedRequestGetLocked(pListenSocket, &PendedRequest);
        if (PendedRequest)
        {
            PSOCKET_CONTEXT pAcceptSocket = GetSocketContextFromRequest(PendedRequest);

            InterlockedDecrement(&pListenSocket->AcceptPended);

            pAcceptSocket->type = pListenSocket->type;

            pAcceptSocket->src_port = pListenSocket->src_port;

            pAcceptSocket->ConnectTimeout = pListenSocket->ConnectTimeout;
            pAcceptSocket->BufferMinSize = pListenSocket->BufferMinSize;
            pAcceptSocket->BufferMaxSize = pListenSocket->BufferMaxSize;

            pAcceptSocket->buf_alloc = pListenSocket->buf_alloc;

            pAcceptSocket->dst_cid = (ULONG32)pPkt->src_cid;
            pAcceptSocket->dst_port = pPkt->src_port;
            pAcceptSocket->peer_buf_alloc = pPkt->src_port;
            pAcceptSocket->peer_fwd_cnt = pPkt->fwd_cnt;

            VIOSockStateSet(pAcceptSocket, VIOSOCK_STATE_CONNECTED);
            VIOSockSendResponse(pAcceptSocket);
            WdfRequestComplete(PendedRequest, STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }
    }

    status = WdfMemoryCreateFromLookaside(pContext->AcceptMemoryList,&Memory);

    if (NT_SUCCESS(status))
    {
        PVIOSOCK_ACCEPT_ENTRY pAccept = WdfMemoryGetBuffer(Memory, NULL);

        pAccept->Memory = Memory;
        pAccept->ConnectSocket = WDF_NO_HANDLE;

        pAccept->dst_cid = (ULONG32)pPkt->src_cid;
        pAccept->dst_port = pPkt->src_port;
        pAccept->peer_buf_alloc = pPkt->src_port;
        pAccept->peer_fwd_cnt = pPkt->fwd_cnt;

        WdfSpinLockAcquire(pListenSocket->RxLock);
        InsertTailList(&pListenSocket->AcceptList, &pAccept->ListEntry);
        WdfSpinLockRelease(pListenSocket->RxLock);
        VIOSockEventSetBit(pListenSocket, FD_ACCEPT_BIT, STATUS_SUCCESS);
        status = STATUS_PENDING;
    }
    else
        InterlockedDecrement(&pListenSocket->AcceptPended);

    return status;
}

VOID
VIOSockAcceptRemovePkt(
    IN PSOCKET_CONTEXT      pListenSocket,
    IN PVIRTIO_VSOCK_HDR    pPkt
)
{
    PLIST_ENTRY pCurrentEntry;

    WdfSpinLockAcquire(pListenSocket->RxLock);
    for (pCurrentEntry = pListenSocket->AcceptList.Flink;
        pCurrentEntry != &pListenSocket->AcceptList;
        pCurrentEntry = pCurrentEntry->Flink)
    {
        PVIOSOCK_ACCEPT_ENTRY pAccept = CONTAINING_RECORD(pCurrentEntry, VIOSOCK_ACCEPT_ENTRY, ListEntry);

        if (pAccept->dst_cid == pPkt->src_cid &&
            pAccept->dst_port == pPkt->src_port)
        {
            ASSERT(pAccept->ConnectSocket == WDF_NO_HANDLE);
            RemoveEntryList(pCurrentEntry);
            InterlockedDecrement(&pListenSocket->AcceptPended);
        break;
        }
    }
    WdfSpinLockRelease(pListenSocket->RxLock);
}

static
BOOLEAN
VIOSockAcceptDequeue(
    IN PSOCKET_CONTEXT  pListenSocket,
    IN PSOCKET_CONTEXT  pAcceptSocket,
    OUT PBOOLEAN        pbSetBit
)
{
    BOOLEAN bAccepted = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    do
    {
        PLIST_ENTRY pListEntry;

        WdfSpinLockAcquire(pListenSocket->RxLock);
        pListEntry = IsListEmpty(&pListenSocket->AcceptList) ? NULL
            : RemoveHeadList(&pListenSocket->AcceptList);
        WdfSpinLockRelease(pListenSocket->RxLock);

        *pbSetBit = FALSE;
        if (pListEntry)
        {
            PVIOSOCK_ACCEPT_ENTRY pAccept = CONTAINING_RECORD(pListEntry, VIOSOCK_ACCEPT_ENTRY, ListEntry);
            LONG lAcceptPended = InterlockedDecrement(&pListenSocket->AcceptPended);

            //loopback connect
            if (pAccept->ConnectSocket != WDF_NO_HANDLE)
            {
                if (!VIOSockLoopbackAcceptDequeue(pAcceptSocket, pAccept))
                {
                    WdfObjectDelete(pAccept->Memory);
                    continue;
                }
            }

            *pbSetBit = !!lAcceptPended;

            pAcceptSocket->dst_cid = pAccept->dst_cid;
            pAcceptSocket->dst_port = pAccept->dst_port;
            pAcceptSocket->peer_buf_alloc = pAccept->peer_buf_alloc;
            pAcceptSocket->peer_fwd_cnt = pAccept->peer_fwd_cnt;

            pAcceptSocket->type = pListenSocket->type;

            pAcceptSocket->src_port = pListenSocket->src_port;

            pAcceptSocket->ConnectTimeout = pListenSocket->ConnectTimeout;
            pAcceptSocket->BufferMinSize = pListenSocket->BufferMinSize;
            pAcceptSocket->BufferMaxSize = pListenSocket->BufferMaxSize;

            pAcceptSocket->buf_alloc = pListenSocket->buf_alloc;

            WdfObjectDelete(pAccept->Memory);
            bAccepted = TRUE;
        }
        else
            break;
    } while (!bAccepted);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);

    return bAccepted;
}

static
VOID
VIOSockAcceptCleanup(
    IN PSOCKET_CONTEXT pListenSocket
)
{
    WDFREQUEST PendedRequest;

    WdfSpinLockAcquire(pListenSocket->RxLock);
    while (!IsListEmpty(&pListenSocket->AcceptList))
    {
        PVIOSOCK_ACCEPT_ENTRY pAccept =
            CONTAINING_RECORD(RemoveHeadList(&pListenSocket->AcceptList), VIOSOCK_ACCEPT_ENTRY, ListEntry);

        if (pAccept->ConnectSocket != WDF_NO_HANDLE)
        {
            WdfObjectDereference(pAccept->ConnectSocket);
        }

        WdfObjectDelete(pAccept->Memory);
    }
    pListenSocket->AcceptPended = 0;

    WdfSpinLockRelease(pListenSocket->RxLock);
}

static
NTSTATUS
VIOSockAccept(
    IN HANDLE       hListenSocket,
    IN WDFREQUEST   Request
)
{
    NTSTATUS        status;
    PSOCKET_CONTEXT pAcceptSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pAcceptSocket);
    PFILE_OBJECT    pFileObj;

    PAGED_CODE();

    status = ObReferenceObjectByHandle(hListenSocket, STANDARD_RIGHTS_REQUIRED, *IoFileObjectType,
        KernelMode, (PVOID)&pFileObj, NULL);

    if (NT_SUCCESS(status))
    {
        PSOCKET_CONTEXT pListenSocket = VIOSockBoundFindByFile(pContext, pFileObj);

        ObDereferenceObject(pFileObj);

        if (!pListenSocket || VIOSockStateGet(pListenSocket) != VIOSOCK_STATE_LISTEN)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Listen socket not found\n");
            status = STATUS_INVALID_DEVICE_STATE;
        }
        else
        {
            BOOLEAN bDequeued = FALSE, bSetBit = FALSE;

            VIOSockEventClearBit(pListenSocket, FD_ACCEPT_BIT);

            if (VIOSockAcceptDequeue(pListenSocket, pAcceptSocket, &bSetBit))
            {
                VIOSockStateSet(pAcceptSocket, VIOSOCK_STATE_CONNECTED);
                status = VIOSockSendResponse(pAcceptSocket);
                VIOSockEventSetBit(pListenSocket, FD_ACCEPT_BIT, status);
            }
            else
            {
                if (VIOSockIsFlag(pListenSocket, SOCK_NON_BLOCK))
                    status = STATUS_CANT_WAIT;
                else
                {
                    if (pListenSocket->PendedRequest == WDF_NO_HANDLE)
                        status = VIOSockPendedRequestSetLocked(pListenSocket, Request);
                    else
                        status = STATUS_DEVICE_BUSY;

                    if (NT_SUCCESS(status))
                        status = STATUS_PENDING;
                }
            }
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "ObReferenceObjectByHandle failed: 0x%x\n", status);
        status = STATUS_INVALID_PARAMETER;
    }
    return status;
}

//-
static
NTSTATUS
VIOSockCreate(
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
)
{
    PDEVICE_CONTEXT         pContext = GetDeviceContext(WdfDevice);
    PSOCKET_CONTEXT         pSocket;
    NTSTATUS                status = STATUS_SUCCESS;
    WDF_REQUEST_PARAMETERS  parameters;
    WDF_OBJECT_ATTRIBUTES   lockAttributes;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "%s\n", __FUNCTION__);

    ASSERT(FileObject);
    if (WDF_NO_HANDLE == FileObject)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE,"NULL FileObject\n");
        return status;
    }

    pSocket = GetSocketContext(FileObject);
    pSocket->ThisSocket = FileObject;
    pSocket->LoopbackSocket = WDF_NO_HANDLE;

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = FileObject;
    status = WdfSpinLockCreate(&lockAttributes, &pSocket->StateLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfSpinLockCreate failed - 0x%x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = FileObject;
    status = WdfSpinLockCreate(&lockAttributes, &pSocket->RxLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfSpinLockCreate failed - 0x%x\n", status);
        return status;
    }

    WDF_REQUEST_PARAMETERS_INIT(&parameters);

    //check EA presents
    WdfRequestGetParameters(Request, &parameters);

    if (parameters.Parameters.Create.EaLength)
    {
        PFILE_FULL_EA_INFORMATION EaBuffer=
            (PFILE_FULL_EA_INFORMATION)WdfRequestWdmGetIrp(Request)->AssociatedIrp.SystemBuffer;

        ASSERT(EaBuffer);

        if (EaBuffer->EaValueLength >= sizeof(VIRTIO_VSOCK_PARAMS))
        {
            HANDLE hListenSocket;
            PVIRTIO_VSOCK_PARAMS pParams = (PVIRTIO_VSOCK_PARAMS)((PCHAR)EaBuffer +
                (FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + 1 + EaBuffer->EaNameLength));

            pSocket->src_port = VMADDR_PORT_ANY;//set unbound state
            pSocket->dst_port = VMADDR_PORT_ANY;
            pSocket->dst_cid = VMADDR_CID_ANY;

            pSocket->State = VIOSOCK_STATE_CLOSE;
            pSocket->PendedRequest = WDF_NO_HANDLE;

            status = VIOSockReadSocketQueueInit(pSocket);
            if (!NT_SUCCESS(status))
            {
                return status;
            }

            InitializeListHead(&pSocket->RxCbList);
            pSocket->RxBytes = 0;

#ifdef _WIN64
            if (WdfRequestIsFrom32BitProcess(Request))
            {
                hListenSocket = Handle32ToHandle((void * POINTER_32)(ULONG)pParams->Socket);
            }
            else
#endif //_WIN64
            {
                hListenSocket = (HANDLE)pParams->Socket;
            }

            //find listen socket
            if (hListenSocket)
            {
                status = VIOSockAccept(hListenSocket, Request);
            }
            else
            {
                if (pParams->Type != VIRTIO_VSOCK_TYPE_STREAM)
                {
                    TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "Unsupported socket type: %u\n", pSocket->type);
                    status = STATUS_INVALID_PARAMETER;
                }
                else
                {
                    pSocket->type = pParams->Type;

                    pSocket->ConnectTimeout.QuadPart = VSOCK_DEFAULT_CONNECT_TIMEOUT;
                    pSocket->BufferMinSize = VSOCK_DEFAULT_BUFFER_MIN_SIZE;
                    pSocket->BufferMaxSize = VSOCK_DEFAULT_BUFFER_MAX_SIZE;

                    pSocket->buf_alloc = VSOCK_DEFAULT_BUFFER_SIZE;
                }
            }
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "Invalid EA length\n");
            status = STATUS_INVALID_PARAMETER;
        }
    }
    else
    {
        //Socket for select and config retrieve
        VIOSockSetFlag(pSocket, SOCK_CONTROL);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE,"<-- %s\n", __FUNCTION__);
    return status;
}

VOID
VIOSockCreateStub(
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
)
{
    NTSTATUS status = VIOSockCreate(WdfDevice, Request, FileObject);

    PAGED_CODE();

    if (status != STATUS_PENDING)
        WdfRequestComplete(Request, status);
}

VOID
VIOSockClose(
    IN WDFFILEOBJECT FileObject
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContext(FileObject);
    WDFREQUEST Request;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "--> %s\n", __FUNCTION__);

    if (VIOSockIsFlag(pSocket, SOCK_CONTROL))
        return;

    if (VIOSockStateGet(pSocket) == VIOSOCK_STATE_CONNECTED ||
        VIOSockStateGet(pSocket) == VIOSOCK_STATE_CLOSING)
    {
        /* Already received SHUTDOWN from peer, reply with RST */
        if ((pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_MASK) == VIRTIO_VSOCK_SHUTDOWN_MASK)
        {
            VIOSockSendReset(pSocket, FALSE);
        }
        else if ((pSocket->Shutdown & VIRTIO_VSOCK_SHUTDOWN_MASK) != VIRTIO_VSOCK_SHUTDOWN_MASK)
        {
            VIOSockSendShutdown(pSocket, VIRTIO_VSOCK_SHUTDOWN_MASK);
        }
    }

    //TODO: wait for circuit close

    if (pSocket->EventObject)
    {
        ObDereferenceObject(pSocket->EventObject);
        pSocket->EventObject = NULL;
    }

    if (pSocket->LoopbackSocket != WDF_NO_HANDLE)
        WdfObjectDereference(pSocket->LoopbackSocket);

    if (VIOSockStateGet(pSocket) == VIOSOCK_STATE_LISTEN)
        VIOSockAcceptCleanup(pSocket);
    else
        VIOSockReadCleanupCb(pSocket);

    VIOSockBoundRemove(pSocket);

    VIOSockPendedRequestGetLocked(pSocket, &Request);
    if (Request != WDF_NO_HANDLE)
    {
        WdfRequestComplete(Request, STATUS_CANCELLED);
    }
    VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSE);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "<-- %s\n", __FUNCTION__);
}

__inline
BOOLEAN
VIOSockIsGuestAddrValid(
    IN PSOCKADDR_VM    pAddr
)
{
    return (pAddr->svm_family == AF_VSOCK) &&
        (pAddr->svm_cid != VMADDR_CID_HYPERVISOR) &&
        (pAddr->svm_cid != VMADDR_CID_RESERVED) &&
        (pAddr->svm_cid != VMADDR_CID_HOST);
}

__inline
BOOLEAN
VIOSockIsHostAddrValid(
    IN PSOCKADDR_VM    pAddr,
    IN ULONG32         ulSrcCid
)
{
    return (pAddr->svm_family == AF_VSOCK) &&
        (pAddr->svm_cid == VMADDR_CID_HYPERVISOR ||
            pAddr->svm_cid == VMADDR_CID_HOST ||
            pAddr->svm_cid == ulSrcCid);
}

static
NTSTATUS
VIOSockBind(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PSOCKADDR_VM    pAddr;
    SIZE_T          stAddrLen;
    NTSTATUS        status;
    static ULONG32  uSvmPort;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    if (!VIOSockIsGuestAddrValid(pAddr))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid addr to bind, cid: %u, port: %u\n", pAddr->svm_cid, pAddr->svm_port);
        return STATUS_INVALID_PARAMETER;
    }

    if (pAddr->svm_cid != (ULONG32)pContext->Config.guest_cid &&
        pAddr->svm_cid != VMADDR_CID_ANY)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid cid: %u\n", pAddr->svm_cid);
        return STATUS_INVALID_PARAMETER;

    }

    if (VIOSockStateGet(pSocket) != VIOSOCK_STATE_CLOSE ||
        VIOSockIsBound(pSocket))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid socket state: %u\n", VIOSockStateGet(pSocket));
        return STATUS_INVALID_PARAMETER;
    }

    status = VIOSockBoundAdd(pSocket, pAddr->svm_port);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockBoundAdd failed: 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return status;
}

static
NTSTATUS
VIOSockConnect(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PSOCKADDR_VM    pAddr;
    SIZE_T          stAddrLen;
    NTSTATUS        status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    if(!VIOSockIsHostAddrValid(pAddr, (ULONG32)pContext->Config.guest_cid))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid addr to connect, cid: %u, port: %u\n", pAddr->svm_cid, pAddr->svm_port);
        return STATUS_INVALID_PARAMETER;
    }

    if (VIOSockStateGet(pSocket) != VIOSOCK_STATE_CLOSE)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid socket state: %u\n", pSocket->State);
        return STATUS_INVALID_PARAMETER;
    }

    if (!VIOSockIsBound(pSocket))
    {
        //autobind
        status = VIOSockBoundAdd(pSocket, VMADDR_PORT_ANY);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "VIOSockBoundAdd failed: 0x%x\n", status);
            return STATUS_INVALID_PARAMETER;
        }
    }

    VIOSockEventClearBit(pSocket, FD_CONNECT_BIT);

    pSocket->dst_cid = pAddr->svm_cid;
    pSocket->dst_port = pAddr->svm_port;

    VIOSockStateSet(pSocket, VIOSOCK_STATE_CONNECTING);

    if (!VIOSockIsFlag(pSocket, SOCK_NON_BLOCK))
    {
        status = VIOSockPendedRequestSetLocked(pSocket, Request);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    if (pAddr->svm_cid == (ULONG32)pContext->Config.guest_cid)
        VIOSockSetFlag(pSocket, SOCK_LOOPBACK);
    else
        VIOSockResetFlag(pSocket, SOCK_LOOPBACK);

    status = VIOSockSendConnect(pSocket);

    if (!NT_SUCCESS(status))
    {
        VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSE);
        if (!VIOSockIsFlag(pSocket, SOCK_NON_BLOCK))
        {
            if (!NT_SUCCESS(VIOSockPendedRequestGetLocked(pSocket, &Request)))
            {
                status = STATUS_PENDING; //do not complete canceled request
            }
        }
    }
    else
        status = VIOSockIsFlag(pSocket, SOCK_NON_BLOCK) ? STATUS_CANT_WAIT : STATUS_PENDING;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return status;
}

BOOLEAN
VIOSockShutdownFromPeer(
    PSOCKET_CONTEXT pSocket,
    ULONG uFlags
)
{
    BOOLEAN bRes;
    ULONG32 uDrain;

    WdfSpinLockAcquire(pSocket->StateLock);

    uDrain = ~pSocket->PeerShutdown & uFlags;
    pSocket->PeerShutdown |= uFlags;
    bRes = (pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_MASK) == VIRTIO_VSOCK_SHUTDOWN_MASK;

    WdfSpinLockRelease(pSocket->StateLock);

    if (uDrain & VIRTIO_VSOCK_SHUTDOWN_SEND)
        VIOSockReadDequeueCb(pSocket, WDF_NO_HANDLE);
    if (uDrain & VIRTIO_VSOCK_SHUTDOWN_RCV)
    {
        //TODO: dequeue requests for current socket only
        //        VIOSockTxDequeue(GetDeviceContextFromSocket(pSocket));
    }

    return bRes;
}

static
NTSTATUS
VIOSockShutdown(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    int             *pHow;
    SIZE_T          stHowLen;
    NTSTATUS        status;
    BOOLEAN         bSend = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pHow), &pHow, &stHowLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stHowLen >= sizeof(*pHow));

    /* User level uses SD_RECEIVE (0) and SD_SEND (1), but the kernel uses
    * VIRTIO_VSOCK_SHUTDOWN_RCV (1) and VIRTIO_VSOCK_SHUTDOWN_SEND (2),
    * so we must increment mode here like the other address families do.
    * Note also that the increment makes SD_BOTH (2) into
    * RCV_SHUTDOWN | SEND_SHUTDOWN (3), which is what we want.
    */

    *pHow++;
    if ((*pHow & ~VIRTIO_VSOCK_SHUTDOWN_MASK) || !*pHow)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid shutdown flags: %u\n", *pHow);
        return STATUS_INVALID_PARAMETER;
    }

    WdfSpinLockAcquire(pSocket->StateLock);
    if (VIOSockStateGet(pSocket) == VIOSOCK_STATE_CONNECTED)
    {
        pSocket->Shutdown |= *pHow;
        bSend = TRUE;
    }
    else
        status = STATUS_CONNECTION_DISCONNECTED;
    WdfSpinLockRelease(pSocket->StateLock);

    if (bSend)
    {
        status = VIOSockSendShutdown(pSocket, *pHow);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return status;
}

static
NTSTATUS
VIOSockListen(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    int             *pBacklog;
    SIZE_T          stBacklogLen;
    NTSTATUS        status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pBacklog), &pBacklog, &stBacklogLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stBacklogLen >= sizeof(*pBacklog));

    if (VIOSockStateGet(pSocket) == VIOSOCK_STATE_CLOSE)
    {
        pSocket->Backlog = *pBacklog;
        InitializeListHead(&pSocket->AcceptList);
        VIOSockStateSet(pSocket, VIOSOCK_STATE_LISTEN);
    }
    else
        status = STATUS_INVALID_PARAMETER;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return status;
}
__inline
PKEVENT
VIOSockGetEventFromHandle(
    IN HANDLE hEvent
)
{
    PKEVENT pEvent = NULL;

    ObReferenceObjectByHandle(hEvent, STANDARD_RIGHTS_REQUIRED, *ExEventObjectType,
        KernelMode, (PVOID)&pEvent, NULL);

    return pEvent;
}

static
NTSTATUS
VIOSockEnumNetEvents(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PSOCKET_CONTEXT     pSocket = GetSocketContextFromRequest(Request);
    PULONGLONG          pEventObject;
    SIZE_T              stEventObjectLen;
    PVIRTIO_VSOCK_NETWORK_EVENTS  lpNetworkEvents;
    SIZE_T              stNetworkEventsLen;
    NTSTATUS            status;
    HANDLE              hEvent = NULL;
    PKEVENT             pEvent = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pEventObject), &pEventObject, &stEventObjectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stEventObjectLen >= sizeof(*pEventObject));

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*lpNetworkEvents), &lpNetworkEvents, &stNetworkEventsLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveOutputBuffer above
    _Analysis_assume_(stNetworkEventsLen >= sizeof(*lpNetworkEvents));

#ifdef _WIN64
    if (WdfRequestIsFrom32BitProcess(Request))
    {
        hEvent = Handle32ToHandle((void * POINTER_32)(ULONG)*pEventObject);
    }
    else
#endif //_WIN64
    {
        hEvent = (HANDLE)(ULONG_PTR)*pEventObject;
    }

    if (hEvent)
    {
        pEvent = VIOSockGetEventFromHandle(hEvent);
        if (!pEvent)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "VIOSockGetEventFromHandle failed\n");
            return STATUS_INVALID_PARAMETER;
        }
    }

    WdfSpinLockAcquire(pSocket->StateLock);
    if (pEvent)
    {
        KeClearEvent(pEvent);
        ObDereferenceObject(pEvent);
    }

    lpNetworkEvents->NetworkEvents = pSocket->Events & pSocket->EventsMask;
    RtlCopyMemory(lpNetworkEvents->Status, pSocket->EventsStatus, sizeof(pSocket->EventsStatus));
    pSocket->Events = 0;
    WdfSpinLockRelease(pSocket->StateLock);

    *pLength = sizeof(*lpNetworkEvents);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOSockSetNonBlocking(
    IN PSOCKET_CONTEXT pSocket,
    IN BOOLEAN bNonBlocking
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (!VIOSockSetFlag(pSocket, SOCK_NON_BLOCK))
    {
        VIOSockReadDequeueCb(pSocket, WDF_NO_HANDLE);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOSockEventSelect(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT             pSocket = GetSocketContextFromRequest(Request);
    PVIRTIO_VSOCK_EVENT_SELECT  pEventSelect;
    SIZE_T                      stEventSelectLen;
    NTSTATUS                    status;
    PKEVENT                     pEvent = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pEventSelect), &pEventSelect, &stEventSelectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stEventSelectLen >= sizeof(*pEventSelect));

    if (pEventSelect->lNetworkEvents)
    {
        HANDLE  hEvent;

#ifdef _WIN64
        if (WdfRequestIsFrom32BitProcess(Request))
        {
            hEvent = Handle32ToHandle((void * POINTER_32)pEventSelect->hEventObject);
        }
        else
#endif //_WIN64
        {
            hEvent = (HANDLE)pEventSelect->hEventObject;
        }

        if (!hEvent)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid hEventObject\n");
            return STATUS_INVALID_PARAMETER;
        }

        pEvent = VIOSockGetEventFromHandle(hEvent);
        if (!pEvent)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "VIOSockGetEventFromHandle failed\n");
            return STATUS_INVALID_PARAMETER;
        }
    }

    VIOSockSetNonBlocking(pSocket, TRUE);

    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->EventsMask = pEventSelect->lNetworkEvents;
    if (pSocket->EventObject)
        ObDereferenceObject(pSocket->EventObject);
    pSocket->Events = 0;
    pSocket->EventObject = pEvent;
    WdfSpinLockRelease(pSocket->StateLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOSockGetPeerName(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PSOCKADDR_VM    pAddr;
    SIZE_T          stAddrLen;
    NTSTATUS        status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    //TODO: check socket state
    RtlZeroBytes(pAddr, sizeof(*pAddr));
    pAddr->svm_family = AF_VSOCK;
    pAddr->svm_cid = pSocket->dst_cid;
    pAddr->svm_port = pSocket->dst_port;
    *pLength = sizeof(*pAddr);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOSockGetSockName(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PSOCKADDR_VM    pAddr;
    SIZE_T          stAddrLen;
    NTSTATUS        status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    //TODO: check socket state
    RtlZeroBytes(pAddr, sizeof(*pAddr));
    pAddr->svm_family = AF_VSOCK;
    pAddr->svm_cid = (ULONG32)pContext->Config.guest_cid;
    pAddr->svm_port = pSocket->src_port;
    *pLength = sizeof(*pAddr);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}
/*
 * Structure used for manipulating linger option.
 */
typedef struct  _LINGER {
    USHORT l_onoff;                /* option on/off */
    USHORT l_linger;               /* linger time */
}LINGER, *PLINGER;

#define SO_ACCEPTCONN   0x0002          /* socket has had listen() */
#define SO_LINGER       0x0080          /* linger on close if data present */

#define SOCK_STREAM     1

static
NTSTATUS
VIOSockGetSockOpt(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PSOCKET_CONTEXT     pSocket = GetSocketContextFromRequest(Request);
    PVIRTIO_VSOCK_OPT   pOpt;
    SIZE_T              stOptLen;
    PVOID               pOptVal;
    NTSTATUS            status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pOpt), &pOpt, &stOptLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stOptLen >= sizeof(*pOpt));

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pOpt), &pOpt, &stOptLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stOptLen >= sizeof(*pOpt));

    if (!pOpt->optlen || !pOpt->optval)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid *optval\n");
        return STATUS_INVALID_PARAMETER;
    }

#ifdef _WIN64
    if (WdfRequestIsFrom32BitProcess(Request))
    {
        pOptVal = Ptr32ToPtr((void * POINTER_32)(ULONG)pOpt->optval);
    }
    else
#endif //_WIN64
    {
        pOptVal = (PVOID)pOpt->optval;
    }

    if (WdfRequestGetRequestorMode(Request) == UserMode)
    {
        WDFMEMORY   Memory;
        status = WdfRequestProbeAndLockUserBufferForWrite(Request, pOptVal, pOpt->optlen, &Memory);

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestProbeAndLockUserBufferForWrite failed: 0x%x\n", status);
            return status;
        }
        pOptVal = WdfMemoryGetBuffer(Memory, NULL);
    }

    switch (pOpt->optname)
    {
    case SO_ACCEPTCONN:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = (VIOSockStateGet(pSocket) == VIOSOCK_STATE_LISTEN);
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_TYPE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = SOCK_STREAM;
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_LINGER:
        if (pOpt->optlen < sizeof(LINGER))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        ((PLINGER)pOptVal)->l_onoff = VIOSockIsFlag(pSocket, SOCK_LINGER);
        ((PLINGER)pOptVal)->l_linger = pSocket->LingerTime;

        pOpt->optlen = sizeof(LINGER);
        break;

    case SO_VM_SOCKETS_BUFFER_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = pSocket->buf_alloc;
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_VM_SOCKETS_BUFFER_MAX_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = pSocket->BufferMaxSize;
        pOpt->optlen = sizeof(ULONG);
        break;

    case SO_VM_SOCKETS_BUFFER_MIN_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        *(PULONG)pOptVal = pSocket->BufferMinSize;
        pOpt->optlen = sizeof(ULONG);
        break;

    default:
        status = STATUS_NOT_SUPPORTED;
    }

    if (NT_SUCCESS(status))
        *pLength = sizeof(*pOpt);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return status;
}

__inline
VOID
VIOSockSetLinger(
    IN PSOCKET_CONTEXT pSocket,
    IN PLINGER         pLinger
)
{
    if (pLinger->l_onoff)
    {
        VIOSockSetFlag(pSocket, SOCK_LINGER);
        pSocket->LingerTime = pLinger->l_linger;
    }
    else
        VIOSockResetFlag(pSocket, SOCK_LINGER);
}

static
VOID
VIOSockRxUpdateBufferSize(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG           uBufferSize
)
{
    if (uBufferSize > pSocket->BufferMaxSize)
        uBufferSize = pSocket->BufferMaxSize;

    if (uBufferSize < pSocket->BufferMinSize)
        uBufferSize = pSocket->BufferMinSize;

    if (uBufferSize != pSocket->buf_alloc)
    {
        pSocket->buf_alloc = uBufferSize;
        VIOSockSendCreditUpdate(pSocket);
    }
}

static
NTSTATUS
VIOSockSetSockOpt(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT     pSocket = GetSocketContextFromRequest(Request);
    PVIRTIO_VSOCK_OPT   pOpt;
    SIZE_T              stOptLen;
    PVOID               pOptVal;
    NTSTATUS            status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pOpt), &pOpt, &stOptLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stOptLen >= sizeof(*pOpt));

    if (!pOpt->optlen || !pOpt->optval)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid *optval\n");
        return STATUS_INVALID_PARAMETER;
    }

#ifdef _WIN64
    if (WdfRequestIsFrom32BitProcess(Request))
    {
        pOptVal = Ptr32ToPtr((void * POINTER_32)(ULONG)pOpt->optval);
    }
    else
#endif //_WIN64
    {
        pOptVal = (PVOID)pOpt->optval;
    }

    if (WdfRequestGetRequestorMode(Request) == UserMode)
    {
        WDFMEMORY   Memory;
        status = WdfRequestProbeAndLockUserBufferForRead(Request, pOptVal, pOpt->optlen, &Memory);

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestProbeAndLockUserBufferForRead failed: 0x%x\n", status);
            return status;
        }
        pOptVal = WdfMemoryGetBuffer(Memory, NULL);

    }

    switch (pOpt->optname)
    {
    case SO_LINGER:
        if (pOpt->optlen < sizeof(LINGER))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        VIOSockSetLinger(pSocket, (PLINGER)pOptVal);
        break;

    case SO_VM_SOCKETS_BUFFER_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        VIOSockRxUpdateBufferSize(pSocket, *(PULONG)pOptVal);
        break;

    case SO_VM_SOCKETS_BUFFER_MAX_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        pSocket->BufferMaxSize = *(PULONG)pOptVal;
        VIOSockRxUpdateBufferSize(pSocket, pSocket->buf_alloc);
        break;

    case SO_VM_SOCKETS_BUFFER_MIN_SIZE:
        if (pOpt->optlen < sizeof(ULONG))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        pSocket->BufferMinSize = *(PULONG)pOptVal;
        VIOSockRxUpdateBufferSize(pSocket, pSocket->buf_alloc);
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return status;
}

#define IOCPARM_MASK    0x7f            /* parameters must be < 128 bytes */
#define IOC_VOID        0x20000000      /* no parameters */
#define IOC_OUT         0x40000000      /* copy out parameters */
#define IOC_IN          0x80000000      /* copy in parameters */

#define _IOR(x,y,t)     (IOC_OUT|(((long)sizeof(t)&IOCPARM_MASK)<<16)|((x)<<8)|(y))
#define _IOW(x,y,t)     (IOC_IN|(((long)sizeof(t)&IOCPARM_MASK)<<16)|((x)<<8)|(y))

#define FIONREAD    _IOR('f', 127, ULONG) /* get # bytes to read */
#define FIONBIO     _IOW('f', 126, ULONG) /* set/clear non-blocking i/o */

#define IOC_WS2                       0x08000000

#define _WSAIO(x,y)                   (IOC_VOID|(x)|(y))
#define _WSAIOR(x,y)                  (IOC_OUT|(x)|(y))

#define SIO_ADDRESS_LIST_QUERY        _WSAIOR(IOC_WS2,22)
#define SIO_ADDRESS_LIST_CHANGE       _WSAIO(IOC_WS2,23)

static
NTSTATUS
VIOSockIoctl(
    IN WDFREQUEST   Request,
    OUT size_t      *pLength
)
{
    PSOCKET_CONTEXT     pSocket = GetSocketContextFromRequest(Request);
    PVIRTIO_VSOCK_IOCTL_IN pInParams;
    SIZE_T              stInParamsLen;
    PVOID               lpvOutBuffer = NULL;
    SIZE_T              stOutBufferLen = 0;
    NTSTATUS            status;
    PVOID               lpvInBuffer = NULL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pInParams), &pInParams, &stInParamsLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stInParamsLen >= sizeof(*pInParams));

    if (pInParams->dwIoControlCode == FIONREAD ||
        pInParams->dwIoControlCode == SIO_ADDRESS_LIST_QUERY)
    {
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &lpvOutBuffer, &stOutBufferLen);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
            return status;
        }
    }

    if (pInParams->cbInBuffer)
    {
#ifdef _WIN64
        if (WdfRequestIsFrom32BitProcess(Request))
        {
            lpvInBuffer = Ptr32ToPtr((void * POINTER_32)(ULONG)pInParams->lpvInBuffer);
        }
        else
#endif //_WIN64
        {
            lpvInBuffer = (PVOID)pInParams->lpvInBuffer;
        }

        if (lpvInBuffer)
        {
            if (WdfRequestGetRequestorMode(Request) == UserMode)
            {
                WDFMEMORY   Memory;
                status = WdfRequestProbeAndLockUserBufferForRead(Request, lpvInBuffer, pInParams->cbInBuffer, &Memory);

                if (!NT_SUCCESS(status))
                {
                    TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestProbeAndLockUserBufferForRead failed: 0x%x\n", status);
                    return status;
                }
                lpvInBuffer = WdfMemoryGetBuffer(Memory, NULL);

            }
        }
        else
            status = STATUS_INVALID_PARAMETER;
    }

    if (NT_SUCCESS(status))
    {
        switch (pInParams->dwIoControlCode)
        {
        case FIONREAD:
            if (stOutBufferLen >= sizeof(ULONG))
            {
                *(PULONG)lpvOutBuffer = VIOSockRxHasData(pSocket);
                *pLength = sizeof(ULONG);
                status = STATUS_SUCCESS;
            }
            else
                status = STATUS_INVALID_PARAMETER;
            break;

        case FIONBIO:
            if (pInParams->cbInBuffer >= sizeof(ULONG))
            {
                status = VIOSockSetNonBlocking(pSocket, !!(*(PULONG)lpvInBuffer));
            }
            else
                status = STATUS_INVALID_PARAMETER;
            break;

            //TODO: implement address list change
        case SIO_ADDRESS_LIST_QUERY:
        case SIO_ADDRESS_LIST_CHANGE:
        default:
            status = STATUS_NOT_SUPPORTED;
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return status;
}

NTSTATUS
VIOSockDeviceControl(
    IN WDFREQUEST Request,
    IN ULONG      IoControlCode,
    IN OUT size_t *pLength
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    *pLength = 0;

    switch (IoControlCode)
    {
    case IOCTL_SOCKET_BIND:
        status = VIOSockBind(Request);
        break;
    case IOCTL_SOCKET_CONNECT:
        status = VIOSockConnect(Request);
        break;
    case IOCTL_SOCKET_READ:
        status = VIOSockReadWithFlags(Request);
        break;
    case IOCTL_SOCKET_SHUTDOWN:
        status = VIOSockShutdown(Request);
        break;
    case IOCTL_SOCKET_LISTEN:
        status = VIOSockListen(Request);
        break;
    case IOCTL_SOCKET_ENUM_NET_EVENTS:
        status = VIOSockEnumNetEvents(Request, pLength);
        break;
    case IOCTL_SOCKET_EVENT_SELECT:
        status = VIOSockEventSelect(Request);
        break;
    case IOCTL_SOCKET_GET_PEER_NAME:
        status = VIOSockGetPeerName(Request, pLength);
        break;
    case IOCTL_SOCKET_GET_SOCK_NAME:
        status = VIOSockGetSockName(Request, pLength);
        break;
    case IOCTL_SOCKET_GET_SOCK_OPT:
        status = VIOSockGetSockOpt(Request, pLength);
        break;
    case IOCTL_SOCKET_SET_SOCK_OPT:
        status = VIOSockSetSockOpt(Request);
        break;
    case IOCTL_SOCKET_IOCTL:
        status = VIOSockIoctl(Request, pLength);
        break;
    default:
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "Invalid socket ioctl\n");
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);
    return status;
}


WDFFILEOBJECT
VIOSockGetSocketFromHandle(
    IN PDEVICE_CONTEXT pContext,
    IN ULONGLONG       uSocket,
    IN BOOLEAN         bIs32BitProcess
)
{
    NTSTATUS        status;
    PFILE_OBJECT    pFileObj;
    HANDLE          hSocket;


#ifdef _WIN64
    if (bIs32BitProcess)
    {
        hSocket = Handle32ToHandle((void * POINTER_32)(ULONG)uSocket);
    }
    else
#endif //_WIN64
    {
        hSocket = (HANDLE)uSocket;
    }

    status = ObReferenceObjectByHandle(hSocket, STANDARD_RIGHTS_REQUIRED, *IoFileObjectType,
        KernelMode, (PVOID)&pFileObj, NULL);

    if (NT_SUCCESS(status))
    {
        PSOCKET_CONTEXT pSocket = VIOSockConnectedFindByFile(pContext, pFileObj);
        if (!pSocket)
        {
            pSocket = VIOSockBoundFindByFile(pContext, pFileObj);
        }

        ObDereferenceObject(pFileObj);

        if (pSocket)
            return pSocket->ThisSocket;
    }

    return WDF_NO_HANDLE;
}

NTSTATUS
VIOSockSelect(
    IN WDFREQUEST Request,
    IN OUT size_t *pLength
)
{
    PDEVICE_CONTEXT         pContext = GetDeviceContextFromRequest(Request);
    PVIRTIO_VSOCK_SELECT    pSelect;
    SIZE_T                  stSelectLen;
    NTSTATUS                status;
    BOOLEAN                 bIs32BitProcess = FALSE;
    ULONG                   i, j;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    *pLength = 0;

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pSelect), &pSelect, &stSelectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stSelectLen >= sizeof(*pSelect));

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pSelect), &pSelect, &stSelectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stSelectLen >= sizeof(*pSelect));

    if (FD_SETSIZE < pSelect->ReadFds.fd_count +
        pSelect->WriteFds.fd_count +
        pSelect->ExceptFds.fd_count)
    {
        return STATUS_INVALID_PARAMETER;
    }

#ifdef _WIN64
    bIs32BitProcess = WdfRequestIsFrom32BitProcess(Request);
#endif //_WIN64

    //TODO: handle select with wait and timeout
    for (i = 0, j = 0; i < pSelect->ReadFds.fd_count; ++i)
    {
        ULONGLONG hSocket = pSelect->ReadFds.fd_array[i];
        if (hSocket)
        {
            WDFFILEOBJECT Socket = VIOSockGetSocketFromHandle(pContext, hSocket, bIs32BitProcess);
            if (Socket != WDF_NO_HANDLE)
            {
                PSOCKET_CONTEXT pSocket = GetSocketContext(Socket);
                if (pSocket->Events & (FD_ACCEPT_BIT | FD_READ_BIT | FD_CLOSE_BIT))
                {
                    pSelect->ReadFds.fd_array[j++] = hSocket;
                }
            }
        }
    }
    pSelect->ReadFds.fd_count = j;

    for (i = 0, j = 0; i < pSelect->WriteFds.fd_count; ++i)
    {
        ULONGLONG hSocket = pSelect->WriteFds.fd_array[i];
        if (hSocket)
        {
            WDFFILEOBJECT Socket = VIOSockGetSocketFromHandle(pContext, hSocket, bIs32BitProcess);
            if (Socket != WDF_NO_HANDLE)
            {
                PSOCKET_CONTEXT pSocket = GetSocketContext(Socket);
                if (pSocket->Events & (FD_CONNECT_BIT | FD_WRITE_BIT))
                {
                    pSelect->WriteFds.fd_array[j++] = hSocket;
                }
            }
        }
    }
    pSelect->WriteFds.fd_count = j;

    for (i = 0, j = 0; i < pSelect->ExceptFds.fd_count; ++i)
    {
        ULONGLONG hSocket = pSelect->ExceptFds.fd_array[i];
        if (hSocket)
        {
            WDFFILEOBJECT Socket = VIOSockGetSocketFromHandle(pContext, hSocket, bIs32BitProcess);
            if (Socket != WDF_NO_HANDLE)
            {
                PSOCKET_CONTEXT pSocket = GetSocketContext(Socket);
                if ((pSocket->Events & FD_CONNECT_BIT) &&
                    !NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT]))
                {
                    pSelect->ExceptFds.fd_array[j++] = hSocket;
                }
            }
        }
    }
    pSelect->ExceptFds.fd_count = j;

    *pLength = sizeof(*pSelect);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

VOID
VIOSockHandleTransportReset(
    IN PDEVICE_CONTEXT pContext
)
{
    WDFFILEOBJECT Socket;
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    while (WDF_NO_HANDLE != (Socket = WdfCollectionGetFirstItem(pContext->ConnectedList)))
    {
        PSOCKET_CONTEXT pCurrentSocket = GetSocketContext(Socket);

        ASSERT(VIOSockStateGet(pCurrentSocket) == VIOSOCK_STATE_CONNECTED);

        VIOSockStateSet(pCurrentSocket, VIOSOCK_STATE_CLOSE);
        VIOSockEventSetBit(pCurrentSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}
