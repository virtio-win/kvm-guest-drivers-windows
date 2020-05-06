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

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockCreateStub)
#pragma alloc_text (PAGE, VIOSockClose)

#pragma alloc_text (PAGE, VIOSockBind)
#pragma alloc_text (PAGE, VIOSockConnect)
#pragma alloc_text (PAGE, VIOSockListen)

#pragma alloc_text (PAGE, VIOSockDeviceControl)
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
            VIOSockConnectedRemove(pSocket);
        }
        else if (NewState == VIOSOCK_STATE_CONNECTED)
        {
            ASSERT(PrevState == VIOSOCK_STATE_CONNECTING || IsLoopbackSocket(pSocket));
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
    IN PSOCKET_CONTEXT  pAcceptSocket
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

        if (pListEntry)
        {
            PVIOSOCK_ACCEPT_ENTRY pAccept = CONTAINING_RECORD(pListEntry, VIOSOCK_ACCEPT_ENTRY, ListEntry);
            LONG lAcceptPended = InterlockedDecrement(&pListenSocket->AcceptPended);

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
            BOOLEAN bDequeued = FALSE;

            if (VIOSockAcceptDequeue(pListenSocket, pAcceptSocket))
            {
                VIOSockStateSet(pAcceptSocket, VIOSOCK_STATE_CONNECTED);
                status = VIOSockSendResponse(pAcceptSocket);
            }
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
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(FileObject));
    PSOCKET_CONTEXT pSocket = GetSocketContext(FileObject);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "--> %s\n", __FUNCTION__);

    if (VIOSockIsFlag(pSocket, SOCK_CONTROL))
        return;

    if (VIOSockStateGet(pSocket) == VIOSOCK_STATE_LISTEN)
        VIOSockAcceptCleanup(pSocket);
    else
        VIOSockReadCleanupCb(pSocket);

    VIOSockBoundRemove(pSocket);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "<-- %s\n", __FUNCTION__);
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

    pSocket->dst_cid = pAddr->svm_cid;
    pSocket->dst_port = pAddr->svm_port;

    VIOSockStateSet(pSocket, VIOSOCK_STATE_CONNECTING);

    status = VIOSockPendedRequestSetLocked(pSocket, Request);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = VIOSockSendConnect(pSocket);

    if (!NT_SUCCESS(status))
    {
        VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSE);
        if (!NT_SUCCESS(VIOSockPendedRequestGetLocked(pSocket, &Request)))
        {
            status = STATUS_PENDING; //do not complete canceled request
        }
    }

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
    default:
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "Invalid socket ioctl\n");
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);
    return status;
}