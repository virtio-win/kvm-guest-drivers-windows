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
VIOSockBind(IN WDFREQUEST Request);

NTSTATUS
VIOSockConnect(IN WDFREQUEST Request);

NTSTATUS
VIOSockShutdown(IN WDFREQUEST Request);

NTSTATUS
VIOSockListen(IN WDFREQUEST Request);

NTSTATUS
VIOSockEnumNetEvents(IN WDFREQUEST Request, OUT size_t *pLength);

NTSTATUS
VIOSockEventSelect(IN WDFREQUEST Request);

NTSTATUS
VIOSockGetPeerName(IN WDFREQUEST Request, OUT size_t *pLength);

NTSTATUS
VIOSockGetSockName(IN WDFREQUEST Request, OUT size_t *pLength);

NTSTATUS
VIOSockGetSockOpt(IN WDFREQUEST Request, OUT size_t *pLength);

NTSTATUS
VIOSockSetSockOpt(IN WDFREQUEST Request);

NTSTATUS
VIOSockIoctl(IN WDFREQUEST Request, OUT size_t *pLength);

typedef struct _VIOSOCK_PENDED_CONTEXT
{
    WDFFILEOBJECT ParentSocket;
    VIOSOCK_TIMER_ENTRY TimerEntry;
} VIOSOCK_PENDED_CONTEXT, *PVIOSOCK_PENDED_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSOCK_PENDED_CONTEXT, GetRequestPendedContext);

EVT_WDF_REQUEST_CANCEL VIOSockPendedRequestCancel;
EVT_WDF_TIMER VIOSockPendedTimerFunc;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VIOSockBoundListInit)
#pragma alloc_text(PAGE, VIOSockConnectedListInit)

#pragma alloc_text(PAGE, VIOSockCleanup)

#pragma alloc_text(PAGE, VIOSockBind)
#pragma alloc_text(PAGE, VIOSockConnect)
#pragma alloc_text(PAGE, VIOSockShutdown)
#pragma alloc_text(PAGE, VIOSockListen)
#pragma alloc_text(PAGE, VIOSockEnumNetEvents)
#pragma alloc_text(PAGE, VIOSockEventSelect)
#pragma alloc_text(PAGE, VIOSockGetPeerName)
#pragma alloc_text(PAGE, VIOSockGetSockName)
#pragma alloc_text(PAGE, VIOSockGetSockOpt)
#pragma alloc_text(PAGE, VIOSockSetSockOpt)
#pragma alloc_text(PAGE, VIOSockIoctl)
#pragma alloc_text(PAGE, VIOSockCreate)

#pragma alloc_text(PAGE, VIOSockDeviceControl)
#endif

//////////////////////////////////////////////////////////////////////////

_Requires_lock_not_held_(pSocket->RxLock) __declspec(noinline) NTSTATUS VIOSockPendedRequestGetLocked(
                                                                                                    IN PSOCKET_CONTEXT pSocket,
                                                                                                    OUT WDFREQUEST *Request)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->RxLock);
    status = VIOSockPendedRequestGet(pSocket, Request);
    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

_Requires_lock_not_held_(pSocket->RxLock) NTSTATUS VIOSockPendedRequestSetLocked(IN PSOCKET_CONTEXT pSocket,
                                                                                 IN WDFREQUEST Request,
                                                                                 IN LONGLONG Deadline)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->RxLock);
    status = VIOSockPendedRequestSet(pSocket, Request, Deadline);
    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

_Requires_lock_not_held_(pSocket->RxLock) NTSTATUS VIOSockPendedRequestSetResumeLocked(IN PSOCKET_CONTEXT pSocket,
                                                                                       IN WDFREQUEST Request)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->RxLock);
    status = VIOSockPendedRequestSetResume(pSocket, Request);
    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

NTSTATUS
VIOSockBoundListInit(IN WDFDEVICE hDevice)
{
    NTSTATUS status;
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    WDF_OBJECT_ATTRIBUTES collectionAttributes;

    PAGED_CODE();

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
        WDF_OBJECT_ATTRIBUTES lockAttributes;

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
VIOSockBoundAdd(IN PSOCKET_CONTEXT pSocket, IN ULONG32 svm_port)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    static ULONG32 uSvmPort;
    ULONG uSeed = (ULONG)(ULONG_PTR)pSocket;
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (svm_port == VMADDR_PORT_ANY)
    {
        BOOLEAN bFound = FALSE;
        ULONG i;

        for (i = 0; i < MAX_PORT_RETRIES; ++i)
        {
            if (!uSvmPort)
            {
                uSvmPort = RtlRandomEx((PULONG)&uSeed);
            }

            if (uSvmPort == VMADDR_PORT_ANY)
            {
                uSvmPort = 0;
                continue;
            }

            if (uSvmPort <= LAST_RESERVED_PORT)
            {
                uSvmPort += LAST_RESERVED_PORT + 1;
            }

            svm_port = uSvmPort++;

            WdfSpinLockAcquire(pContext->BoundLock);
            if (!VIOSockBoundFindByPort(pContext, svm_port))
            {
                bFound = TRUE;
                pSocket->src_port = svm_port;
                status = WdfCollectionAdd(pContext->BoundList, pSocket->ThisSocket);
            }
            WdfSpinLockRelease(pContext->BoundLock);

            if (bFound)
            {
                break;
            }
        }

        if (!bFound)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "No ports available\n");
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        WdfSpinLockAcquire(pContext->BoundLock);
        if (VIOSockBoundFindByPort(pContext, svm_port))
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Local port %u is already in use\n", svm_port);
            status = STATUS_ADDRESS_ALREADY_ASSOCIATED;
        }
        else
        {
            pSocket->src_port = svm_port;
            status = WdfCollectionAdd(pContext->BoundList, pSocket->ThisSocket);
        }
        WdfSpinLockRelease(pContext->BoundLock);
    }

    if (NT_SUCCESS(status))
    {
        VIOSockSetFlag(pSocket, SOCK_BOUND);
    }

    return status;
}

__inline VOID VIOSockBoundRemove(IN PSOCKET_CONTEXT pSocket)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(pSocket->ThisSocket));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pContext->BoundLock);
    if (VIOSockResetFlag(pSocket, SOCK_BOUND))
    {
        WdfCollectionRemove(pContext->BoundList, pSocket->ThisSocket);
    }
    WdfSpinLockRelease(pContext->BoundLock);
}

typedef BOOLEAN (*PSOCKET_CALLBACK)(IN PSOCKET_CONTEXT pSocket, IN PVOID pCallbackContext);

_Requires_lock_held_(pContext->BoundLock) PSOCKET_CONTEXT VIOSockBoundEnum(IN PDEVICE_CONTEXT pContext,
                                                                           IN PSOCKET_CALLBACK pEnumCallback,
                                                                           IN PVOID pCallbackContext)
{
    PSOCKET_CONTEXT pSocket = NULL;
    ULONG i, ItemCount;

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

    return pSocket;
}

_Requires_lock_not_held_(pContext->BoundLock) PSOCKET_CONTEXT VIOSockBoundEnumLocked(IN PDEVICE_CONTEXT pContext,
                                                                                     IN PSOCKET_CALLBACK pEnumCallback,
                                                                                     IN PVOID pCallbackContext)
{
    PSOCKET_CONTEXT pSocket;

    WdfSpinLockAcquire(pContext->BoundLock);
    pSocket = VIOSockBoundEnum(pContext, pEnumCallback, pCallbackContext);
    WdfSpinLockRelease(pContext->BoundLock);

    return pSocket;
}

static BOOLEAN VIOSockBoundFindByPortCallback(IN PSOCKET_CONTEXT pSocket, IN PVOID pCallbackContext)
{
    return pSocket->src_port == (ULONG32)(ULONG_PTR)pCallbackContext;
}

_Requires_lock_not_held_(pContext->BoundLock) PSOCKET_CONTEXT VIOSockBoundFindByPortLocked(IN PDEVICE_CONTEXT pContext,
                                                                                           IN ULONG32 ulSrcPort)
{
    return VIOSockBoundEnumLocked(pContext, VIOSockBoundFindByPortCallback, (PVOID)ulSrcPort);
}

_Requires_lock_not_held_(pContext->BoundLock) PSOCKET_CONTEXT VIOSockBoundFindByPortLockedReferenced(
                                                                                                    IN PDEVICE_CONTEXT pContext,
                                                                                                    IN ULONG32 ulSrcPort)
{
    PSOCKET_CONTEXT pSocket;

    WdfSpinLockAcquire(pContext->BoundLock);
    pSocket = VIOSockBoundEnum(pContext, VIOSockBoundFindByPortCallback, (PVOID)ulSrcPort);
    if (pSocket)
    {
        WdfObjectReference(pSocket->ThisSocket);
    }
    WdfSpinLockRelease(pContext->BoundLock);

    return pSocket;
}

_Requires_lock_held_(pContext->BoundLock) PSOCKET_CONTEXT VIOSockBoundFindByPort(IN PDEVICE_CONTEXT pContext,
                                                                                 IN ULONG32 ulSrcPort)
{
    return VIOSockBoundEnum(pContext, VIOSockBoundFindByPortCallback, (PVOID)ulSrcPort);
}

static BOOLEAN VIOSockFindByFileCallback(IN PSOCKET_CONTEXT pSocket, IN PVOID pCallbackContext)
{
    return WdfFileObjectWdmGetFileObject(pSocket->ThisSocket) == (PFILE_OBJECT)pCallbackContext;
}

_Requires_lock_not_held_(pContext->BoundLock) PSOCKET_CONTEXT VIOSockBoundFindByFileLocked(IN PDEVICE_CONTEXT pContext,
                                                                                           IN PFILE_OBJECT pFileObject)
{
    return VIOSockBoundEnumLocked(pContext, VIOSockFindByFileCallback, pFileObject);
}

_Requires_lock_not_held_(pContext->BoundLock) PSOCKET_CONTEXT VIOSockBoundFindByFileLockedReferenced(
                                                                                                    IN PDEVICE_CONTEXT pContext,
                                                                                                    IN PFILE_OBJECT pFileObject)
{
    PSOCKET_CONTEXT pSocket;

    WdfSpinLockAcquire(pContext->BoundLock);
    pSocket = VIOSockBoundEnum(pContext, VIOSockFindByFileCallback, pFileObject);
    if (pSocket)
    {
        WdfObjectReference(pSocket->ThisSocket);
    }
    WdfSpinLockRelease(pContext->BoundLock);

    return pSocket;
}

NTSTATUS
VIOSockConnectedListInit(IN WDFDEVICE hDevice)
{
    NTSTATUS status;
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    WDF_OBJECT_ATTRIBUTES collectionAttributes;

    PAGED_CODE();

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
        WDF_OBJECT_ATTRIBUTES lockAttributes;

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

__inline NTSTATUS VIOSockConnectedAdd(IN PSOCKET_CONTEXT pSocket)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(pSocket->ThisSocket));
    NTSTATUS status;

    WdfSpinLockAcquire(pContext->ConnectedLock);
    // Reject a connect/accept that raced a transport reset: it captured an older
    // ResetGeneration, so it would become a lost CONNECTED entry past the reset
    // drain (which bumps the generation under this same lock). Loopback does not
    // ride the transport, so a reset never invalidates it.
    if (!IsLoopbackSocket(pSocket) && pSocket->ConnectGeneration != pContext->ResetGeneration)
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_SOCKET,
                    "Socket %d: transport reset raced connect (gen %u != %u), failing\n",
                    pSocket->SocketId,
                    pSocket->ConnectGeneration,
                    pContext->ResetGeneration);
        status = STATUS_CONNECTION_RESET;
    }
    else
    {
        status = WdfCollectionAdd(pContext->ConnectedList, pSocket->ThisSocket);
    }
    WdfSpinLockRelease(pContext->ConnectedLock);
    return status;
}

__inline VOID VIOSockConnectedRemove(IN PSOCKET_CONTEXT pSocket)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(pSocket->ThisSocket));

    WdfSpinLockAcquire(pContext->ConnectedLock);
    WdfCollectionRemove(pContext->ConnectedList, pSocket->ThisSocket);
    WdfSpinLockRelease(pContext->ConnectedLock);
}

_Requires_lock_held_(pContext->ConnectedLock) PSOCKET_CONTEXT VIOSockConnectedEnum(IN PDEVICE_CONTEXT pContext,
                                                                                   IN PSOCKET_CALLBACK pEnumCallback,
                                                                                   IN PVOID pCallbackContext)
{
    PSOCKET_CONTEXT pSocket = NULL;
    ULONG i, ItemCount;

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

    return pSocket;
}

_Requires_lock_not_held_(pContext->ConnectedLock) PSOCKET_CONTEXT VIOSockConnectedEnumLocked(IN PDEVICE_CONTEXT pContext,
                                                                                             IN PSOCKET_CALLBACK pEnumCallback,
                                                                                             IN PVOID pCallbackContext)
{
    PSOCKET_CONTEXT pSocket;

    WdfSpinLockAcquire(pContext->ConnectedLock);
    pSocket = VIOSockConnectedEnum(pContext, pEnumCallback, pCallbackContext);
    WdfSpinLockRelease(pContext->ConnectedLock);

    return pSocket;
}

static BOOLEAN VIOSockConnectedFindByRxPktCallback(IN PSOCKET_CONTEXT pSocket, IN PVOID pCallbackContext)
{
    PVIRTIO_VSOCK_HDR pPkt = (PVIRTIO_VSOCK_HDR)pCallbackContext;
    return (pPkt->src_cid == pSocket->dst_cid && pPkt->src_port == pSocket->dst_port &&
            pPkt->dst_port == pSocket->src_port);
}

_Requires_lock_not_held_(pContext->ConnectedLock) PSOCKET_CONTEXT VIOSockConnectedFindByRxPktLocked(IN PDEVICE_CONTEXT pContext,
                                                                                                    IN PVIRTIO_VSOCK_HDR pPkt)
{
    return VIOSockConnectedEnumLocked(pContext, VIOSockConnectedFindByRxPktCallback, pPkt);
}

_Requires_lock_not_held_(pContext->ConnectedLock) PSOCKET_CONTEXT VIOSockConnectedFindByFileLocked(IN PDEVICE_CONTEXT pContext,
                                                                                                   IN PFILE_OBJECT pFileObject)
{
    return VIOSockConnectedEnumLocked(pContext, VIOSockFindByFileCallback, pFileObject);
}

_Requires_lock_not_held_(pContext->ConnectedLock) PSOCKET_CONTEXT VIOSockConnectedFindByFileLockedReferenced(
                                                                                                    IN PDEVICE_CONTEXT pContext,
                                                                                                    IN PFILE_OBJECT pFileObject)
{
    PSOCKET_CONTEXT pSocket;

    WdfSpinLockAcquire(pContext->ConnectedLock);
    pSocket = VIOSockConnectedEnum(pContext, VIOSockFindByFileCallback, pFileObject);
    if (pSocket)
    {
        WdfObjectReference(pSocket->ThisSocket);
    }
    WdfSpinLockRelease(pContext->ConnectedLock);

    return pSocket;
}

_Requires_lock_held_(pSocket->StateLock) VIOSOCK_STATE VIOSockStateSet(IN PSOCKET_CONTEXT pSocket,
                                                                       IN VIOSOCK_STATE NewState)
{
    VIOSOCK_STATE PrevState;

    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_SOCKET,
                "Socket %d, %!State! --> %!State!\n",
                pSocket->SocketId,
                pSocket->State,
                NewState);

    PrevState = InterlockedExchange((PLONG)&pSocket->State, NewState);

    // Keep the connected list in sync with the socket state: a socket is a member
    // iff its state is CONNECTED. It is added on entry to CONNECTED and removed as
    // soon as it leaves CONNECTED (peer close -> CLOSING, or reset/abort/app-close
    // -> CLOSE). Removing on CLOSING - not only on CLOSE - stops a dying socket
    // from shadowing a new connect() that reuses the same 4-tuple: the OP_REQUEST
    // would otherwise be matched to it and dropped instead of reaching the listener.
    // Loopback sockets are members too: the connected list is the handle-lookup
    // path used by select/poll (VIOSockGetSocketFromHandle), and an accepted
    // loopback socket must be findable there. The Rx and transport-reset consumers
    // filter loopback out at their own callsites.
    if (PrevState != NewState)
    {
        switch (NewState)
        {
            case VIOSOCK_STATE_CONNECTED:
                ASSERT(PrevState == VIOSOCK_STATE_CONNECTING || PrevState == VIOSOCK_STATE_CLOSE);
                if (!NT_SUCCESS(VIOSockConnectedAdd(pSocket)))
                {
                    // Add refused (a transport reset raced this connect/accept, or
                    // OOM): do not leave a CONNECTED socket that is not in the list.
                    // The caller sees State != CONNECTED and fails the connect.
                    TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockConnectedAdd refused; socket -> CLOSE\n");
                    InterlockedExchange((PLONG)&pSocket->State, VIOSOCK_STATE_CLOSE);
                }
                break;

            case VIOSOCK_STATE_CLOSING:
            case VIOSOCK_STATE_CLOSE:
                if (PrevState == VIOSOCK_STATE_CONNECTED)
                {
                    VIOSockConnectedRemove(pSocket);
                }
                break;

            default:
                break;
        }
    }

    return PrevState;
}

_Requires_lock_not_held_(pSocket->StateLock) VIOSOCK_STATE VIOSockStateSetLocked(IN PSOCKET_CONTEXT pSocket,
                                                                                 IN VIOSOCK_STATE NewState)
{
    VIOSOCK_STATE PrevState;

    WdfSpinLockAcquire(pSocket->StateLock);
    PrevState = VIOSockStateSet(pSocket, NewState);
    WdfSpinLockRelease(pSocket->StateLock);

    return PrevState;
}

__inline BOOLEAN VIOSockIsBound(IN PSOCKET_CONTEXT pSocket)
{
    return pSocket->src_port != VMADDR_PORT_ANY;
}

//////////////////////////////////////////////////////////////////////////
_Requires_lock_held_(pSocket->RxLock) static VOID VIOSockPendedRequestCleanup(PSOCKET_CONTEXT pSocket,
                                                                              PVIOSOCK_PENDED_CONTEXT pRequest)
{
    if (VIOSockTimerEntryIsSet(&pRequest->TimerEntry))
    {
        VIOSockTimerDeref(&pSocket->PendedTimer, TRUE);
    }
    pSocket->PendedRequest = WDF_NO_HANDLE;
    WdfObjectDereference(pSocket->ThisSocket);
}

static VOID VIOSockPendedRequestCancel(IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket;
    PVIOSOCK_PENDED_CONTEXT pRequest = GetRequestPendedContext(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, Request: %p\n", __FUNCTION__, Request);

    ASSERT(pRequest->ParentSocket);
    pSocket = GetSocketContext(pRequest->ParentSocket);
    ASSERT(pSocket->PendedRequest == Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "Socket %d\n", pSocket->SocketId);

    WdfSpinLockAcquire(pSocket->RxLock);
    VIOSockPendedRequestCleanup(pSocket, pRequest);
    WdfSpinLockRelease(pSocket->RxLock);

    WdfRequestComplete(Request, STATUS_CANCELLED);
}

VOID VIOSockPendedTimerFunc(WDFTIMER Timer)
{
    PSOCKET_CONTEXT pSocket = GetSocketContext(WdfTimerGetParentObject(Timer));
    WDFREQUEST PendedRequest;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    //     ASSERT(VIOSockStateGet(pSocket) == VIOSOCK_STATE_CONNECTING);

    if (NT_SUCCESS(VIOSockPendedRequestGetLocked(pSocket, &PendedRequest)) && PendedRequest != WDF_NO_HANDLE)
    {
        WdfRequestComplete(PendedRequest, STATUS_IO_TIMEOUT);
        //        VIOSockTxCancel(GetDeviceContextFromSocket(pSocket), pSocket, STATUS_IO_TIMEOUT);
    }
}

_Requires_lock_held_(pSocket->RxLock) NTSTATUS VIOSockPendedRequestSet(IN PSOCKET_CONTEXT pSocket,
                                                                       IN WDFREQUEST Request,
                                                                       IN LONGLONG Deadline)
{
    NTSTATUS status;
    PSOCKET_CONTEXT pRequestSocket = GetSocketContextFromRequest(Request);
    PVIOSOCK_PENDED_CONTEXT pRequest = GetRequestPendedContext(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE,
                DBG_SOCKET,
                "--> %s, Socket %d, Request %p\n",
                __FUNCTION__,
                pSocket->SocketId,
                Request);

    ASSERT(pSocket && pRequestSocket);
    ASSERT(pSocket->PendedRequest == WDF_NO_HANDLE);

    if (!pRequest)
    {
        WDF_OBJECT_ATTRIBUTES attributes;

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VIOSOCK_PENDED_CONTEXT);
        status = WdfObjectAllocateContext(Request, &attributes, &pRequest);

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "WdfObjectAllocateContext failed: 0x%x\n", status);
            return status;
        }

        // store parent socket handle
        VIOSockTimerEntryInit(&pRequest->TimerEntry);
    }

    if (!Deadline)
    {
        ASSERT(VIOSockTimerEntryIsSet(&pRequest->TimerEntry));
        Deadline = pRequest->TimerEntry.Deadline;
    }

    if (VIOSockTimerDeadlineIsExpired(Deadline))
    {
        return STATUS_IO_TIMEOUT;
    }

    WdfObjectReference(pSocket->ThisSocket);
    pRequest->ParentSocket = pSocket->ThisSocket;
    pSocket->PendedRequest = Request;

    status = WdfRequestMarkCancelableEx(pSocket->PendedRequest, VIOSockPendedRequestCancel);
    if (!NT_SUCCESS(status))
    {
        ASSERT(status == STATUS_CANCELLED);
        VIOSockPendedRequestCleanup(pSocket, pRequest);

        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Pended request canceled: 0x%x\n", status);
    }
    else if (Deadline != MAXLONGLONG)
    {
        VIOSockTimerStartDeadline(&pSocket->PendedTimer, &pRequest->TimerEntry, Deadline);
    }

    return status;
}

_Requires_lock_held_(pSocket->RxLock) NTSTATUS VIOSockPendedRequestGet(IN PSOCKET_CONTEXT pSocket,
                                                                       OUT WDFREQUEST *Request)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, Socket %d\n", __FUNCTION__, pSocket->SocketId);

    *Request = pSocket->PendedRequest;
    if (*Request != WDF_NO_HANDLE)
    {
        status = WdfRequestUnmarkCancelable(*Request);
        if (!NT_SUCCESS(status))
        {
            ASSERT(status == STATUS_CANCELLED && pSocket->PendedRequest == WDF_NO_HANDLE);
            *Request = WDF_NO_HANDLE; // do not complete canceled request
            status = STATUS_CANCELLED;
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Pended request canceled\n");
        }
        else
        {
            PVIOSOCK_PENDED_CONTEXT pRequest = GetRequestPendedContext(*Request);

            ASSERT(pRequest->ParentSocket == pSocket->ThisSocket);
            VIOSockPendedRequestCleanup(pSocket, pRequest);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, pended request: %p\n", __FUNCTION__, *Request);

    return status;
}

//////////////////////////////////////////////////////////////////////////
_Requires_lock_not_held_(pListenSocket->RxLock) NTSTATUS VIOSockAcceptEnqueuePkt(IN PSOCKET_CONTEXT pListenSocket,
                                                                                 IN PVIRTIO_VSOCK_HDR pPkt)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pListenSocket);
    NTSTATUS status;
    WDFMEMORY Memory;
    LONG lAcceptPended;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    lAcceptPended = InterlockedIncrement(&pListenSocket->AcceptPended);
    if (lAcceptPended > pListenSocket->Backlog)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Accept list is full\n");
        InterlockedDecrement(&pListenSocket->AcceptPended);
        return STATUS_CONNECTION_RESET;
    }

    // Queue the incoming connection, then hand it to a waiting accept (if any)
    // via VIOSockAcceptProcessQueue. Pending accept()s live on the listen
    // socket's AcceptQueue (multiple), not a single pended slot.
    status = WdfMemoryCreateFromLookaside(pContext->AcceptMemoryList, &Memory);

    if (NT_SUCCESS(status))
    {
        PVIOSOCK_ACCEPT_ENTRY pAccept = WdfMemoryGetBuffer(Memory, NULL);

        pAccept->Memory = Memory;

        pAccept->dst_cid = (ULONG32)pPkt->src_cid;
        pAccept->dst_port = pPkt->src_port;
        pAccept->peer_buf_alloc = pPkt->buf_alloc;
        pAccept->peer_fwd_cnt = pPkt->fwd_cnt;
        // Stamp the generation at OP_REQUEST time: this is when the connection is
        // born, so an accept that arrived before a later reset carries the stale
        // value and is rejected even if accept() runs after the reset.
        pAccept->Generation = pContext->ResetGeneration;

        WdfSpinLockAcquire(pListenSocket->RxLock);
        InsertTailList(&pListenSocket->AcceptList, &pAccept->ListEntry);
        WdfSpinLockRelease(pListenSocket->RxLock);
        VIOSockEventSetBitLocked(pListenSocket, FD_ACCEPT_BIT, STATUS_SUCCESS);
        status = STATUS_SUCCESS;

        VIOSockAcceptProcessQueue(pListenSocket);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
        InterlockedDecrement(&pListenSocket->AcceptPended);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return status;
}

_Requires_lock_not_held_(pListenSocket->RxLock) VOID VIOSockAcceptRemovePkt(IN PSOCKET_CONTEXT pListenSocket,
                                                                            IN PVIRTIO_VSOCK_HDR pPkt)
{
    PLIST_ENTRY pCurrentEntry;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pListenSocket->RxLock);
    for (pCurrentEntry = pListenSocket->AcceptList.Flink; pCurrentEntry != &pListenSocket->AcceptList;
         pCurrentEntry = pCurrentEntry->Flink)
    {
        PVIOSOCK_ACCEPT_ENTRY pAccept = CONTAINING_RECORD(pCurrentEntry, VIOSOCK_ACCEPT_ENTRY, ListEntry);

        if (pAccept->dst_cid == pPkt->src_cid && pAccept->dst_port == pPkt->src_port)
        {
            RemoveEntryList(pCurrentEntry);
            InterlockedDecrement(&pListenSocket->AcceptPended);
            WdfObjectDelete(pAccept->Memory);
            break;
        }
    }
    WdfSpinLockRelease(pListenSocket->RxLock);
}

_Requires_lock_not_held_(pListenSocket->StateLock) NTSTATUS VIOSockAcceptInitSocket(PSOCKET_CONTEXT pAcceptSocket,
                                                                                    PSOCKET_CONTEXT pListenSocket)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    pAcceptSocket->type = pListenSocket->type;

    pAcceptSocket->src_port = pListenSocket->src_port;

    if (VIOSockIsNonBlocking(pListenSocket))
    {
        VIOSockSetFlag(pAcceptSocket, SOCK_NON_BLOCK);
    }

    pAcceptSocket->ConnectTimeout = pListenSocket->ConnectTimeout;
    pAcceptSocket->BufferMinSize = pListenSocket->BufferMinSize;
    pAcceptSocket->BufferMaxSize = pListenSocket->BufferMaxSize;
    pAcceptSocket->SendTimeout = MAXLONGLONG;
    pAcceptSocket->RecvTimeout = MAXLONGLONG;

    pAcceptSocket->buf_alloc = pListenSocket->buf_alloc;
    pAcceptSocket->RcvLowat = 1; // SO_RCVLOWAT is not inherited from the listening socket

    WdfSpinLockAcquire(pListenSocket->StateLock);
    pAcceptSocket->EventsMask = ~FD_ACCEPT & pListenSocket->EventsMask;
    if (pAcceptSocket->EventsMask && pListenSocket->EventObject)
    {
        ObReferenceObject(pListenSocket->EventObject);
        pAcceptSocket->EventObject = pListenSocket->EventObject;
    }
    WdfSpinLockRelease(pListenSocket->StateLock);

    if (VIOSockIsLocalCid(pAcceptSocket->dst_cid, GetDeviceContextFromSocket(pAcceptSocket)->Config.guest_cid))
    {
        VIOSockSetFlag(pAcceptSocket, SOCK_LOOPBACK);
    }

    // ConnectGeneration was inherited from the accept entry (stamped at OP_REQUEST
    // time) by the caller, so a reset that raced this accept is caught here.

    // From CONNECTED on the socket is findable by the Rx/loopback paths, so the
    // peer's first packet can already race this bit - take the lock.
    VIOSockStateSet(pAcceptSocket, VIOSOCK_STATE_CONNECTED);
    if (VIOSockStateGet(pAcceptSocket) != VIOSOCK_STATE_CONNECTED)
    {
        // A transport reset raced this accept: VIOSockConnectedAdd refused and
        // rolled the state to CLOSE. Fail the accept (the caller completes the
        // request with this error and the half-built socket is torn down) instead
        // of handing the app a dead socket; don't respond on the dead transport.
        // To the app it is a connection reset before it was accepted - it retries.
        return STATUS_CONNECTION_RESET;
    }
    status = VIOSockSendResponse(pAcceptSocket);
    VIOSockEventSetBitLocked(pAcceptSocket, FD_WRITE_BIT, status);

    return status;
}

_Requires_lock_not_held_(pListenSocket->RxLock) static BOOLEAN VIOSockAcceptDequeue(IN PSOCKET_CONTEXT pListenSocket,
                                                                                    IN PSOCKET_CONTEXT pAcceptSocket)
{
    BOOLEAN bAccepted = FALSE;
    PLIST_ENTRY pListEntry;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pListenSocket->RxLock);
    pListEntry = IsListEmpty(&pListenSocket->AcceptList) ? NULL : RemoveHeadList(&pListenSocket->AcceptList);
    WdfSpinLockRelease(pListenSocket->RxLock);

    if (pListEntry)
    {
        PVIOSOCK_ACCEPT_ENTRY pAccept = CONTAINING_RECORD(pListEntry, VIOSOCK_ACCEPT_ENTRY, ListEntry);
        LONG lAcceptPended = InterlockedDecrement(&pListenSocket->AcceptPended);

        pAcceptSocket->dst_cid = pAccept->dst_cid;
        pAcceptSocket->dst_port = pAccept->dst_port;
        pAcceptSocket->peer_buf_alloc = pAccept->peer_buf_alloc;
        pAcceptSocket->peer_fwd_cnt = pAccept->peer_fwd_cnt;
        pAcceptSocket->ConnectGeneration = pAccept->Generation;
        VIOSockAcceptInitSocket(pAcceptSocket, pListenSocket);

        WdfObjectDelete(pAccept->Memory);
        bAccepted = TRUE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, accepted: %d\n", __FUNCTION__, bAccepted);

    return bAccepted;
}

static NTSTATUS VIOSockListenSocketInit(IN PSOCKET_CONTEXT pListenSocket, IN int Backlog)
{
    WDFDEVICE hDevice = WdfFileObjectGetDevice(pListenSocket->ThisSocket);
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_OBJECT_ATTRIBUTES queueAttributes;
    NTSTATUS status;

    pListenSocket->Backlog = Backlog;
    InitializeListHead(&pListenSocket->AcceptList);
    pListenSocket->AcceptPended = 0;
    pListenSocket->AcceptMatchInProgress = 0;

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

    // Power-managed: a parked accept is framework-owned, so it does not hold the
    // D0->Dx transition (no EvtIoStop needed - same rationale as the read queue).
    queueConfig.PowerManaged = WdfTrue;

    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ParentObject = pListenSocket->ThisSocket;

    __analysis_assume(queueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(hDevice, &queueConfig, &queueAttributes, &pListenSocket->AcceptQueue);
    __analysis_assume(queueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "WdfIoQueueCreate failed (Accept Queue): 0x%x\n", status);
        return status;
    }

    VIOSockStateSet(pListenSocket, VIOSOCK_STATE_LISTEN);

    return STATUS_SUCCESS;
}

static __inline BOOLEAN VIOSockAcceptDequeueAlreadyActive(IN PSOCKET_CONTEXT pListenSocket)
{
    return InterlockedIncrement(&pListenSocket->AcceptMatchInProgress) != 1;
}

// Re-arm the counter for another worker pass.
static __inline VOID VIOSockAcceptDequeueReset(IN PSOCKET_CONTEXT pListenSocket)
{
    InterlockedExchange(&pListenSocket->AcceptMatchInProgress, 1);
}

// Try to finish the worker by clearing 1->0. Returns FALSE when it cleared (the value
// was exactly 1, no bump arrived during the pass, so the worker may stop) and TRUE when
// a bump pushed the count past 1 during the pass (take another pass).
static __inline BOOLEAN VIOSockAcceptDequeueWorkArrived(IN PSOCKET_CONTEXT pListenSocket)
{
    return (BOOLEAN)(InterlockedCompareExchange(&pListenSocket->AcceptMatchInProgress, 0, 1) != 1);
}

static BOOLEAN VIOSockAcceptProcessNextRequest(IN PSOCKET_CONTEXT pListenSocket)
{
    WDFREQUEST AcceptRequest = WDF_NO_HANDLE;
    PSOCKET_CONTEXT pAcceptSocket;
    NTSTATUS status = STATUS_SUCCESS;
    BOOLEAN bRestart = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfIoQueueRetrieveNextRequest(pListenSocket->AcceptQueue, &AcceptRequest);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_SOCKET,
                    "The accept queue is empty, exiting from VIOSockAcceptProcessNextRequest\n");
        return FALSE;
    }

    pAcceptSocket = GetSocketContextFromRequest(AcceptRequest);
    if (!VIOSockAcceptDequeue(pListenSocket, pAcceptSocket))
    {
        if (VIOSockIsNonBlocking(pListenSocket))
        {
            status = STATUS_CANT_WAIT;
        }
        else
        {
            status = WdfRequestRequeue(AcceptRequest);
            if (NT_SUCCESS(status))
            {
                AcceptRequest = WDF_NO_HANDLE;
            }
        }
    }
    else
    {
        // A transport reset can race VIOSockAcceptInitSocket and roll the dequeued
        // socket back to CLOSE; fail the accept then so the app is not handed a
        // dead socket (it retries, as for a connection reset before accept()).
        status = (VIOSockStateGet(pAcceptSocket) == VIOSOCK_STATE_CONNECTED) ? STATUS_SUCCESS : STATUS_CONNECTION_RESET;
    }

    if (AcceptRequest != WDF_NO_HANDLE)
    {
        WdfRequestComplete(AcceptRequest, status);
        bRestart = TRUE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);

    return bRestart;
}

VOID VIOSockAcceptProcessQueue(IN PSOCKET_CONTEXT pListenSocket)
{
    BOOLEAN bRestart;

    if (VIOSockAcceptDequeueAlreadyActive(pListenSocket))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Dequeue worker already running, bump and return\n");
        return;
    }

    do
    {
        VIOSockAcceptDequeueReset(pListenSocket);
        bRestart = VIOSockAcceptProcessNextRequest(pListenSocket);
    } while (bRestart || VIOSockAcceptDequeueWorkArrived(pListenSocket));
}

_Requires_lock_not_held_(pListenSocket->RxLock) static VOID VIOSockAcceptCleanup(IN PSOCKET_CONTEXT pListenSocket)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pListenSocket);
    LIST_ENTRY CleanupList;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    // Cancel every parked accept request (the framework completes each with
    // STATUS_CANCELLED) and wait for them to drain.
    if (pListenSocket->AcceptQueue != WDF_NO_HANDLE)
    {
        WdfIoQueuePurgeSynchronously(pListenSocket->AcceptQueue);
    }

    InitializeListHead(&CleanupList);

    WdfSpinLockAcquire(pListenSocket->RxLock);
    if (!IsListEmpty(&pListenSocket->AcceptList))
    {
        CleanupList = pListenSocket->AcceptList;
        CleanupList.Flink->Blink = &CleanupList;
        CleanupList.Blink->Flink = &CleanupList;
        InitializeListHead(&pListenSocket->AcceptList);
    }
    pListenSocket->AcceptPended = 0;
    WdfSpinLockRelease(pListenSocket->RxLock);

    while (!IsListEmpty(&CleanupList))
    {
        PVIOSOCK_ACCEPT_ENTRY pAccept = CONTAINING_RECORD(RemoveHeadList(&CleanupList),
                                                          VIOSOCK_ACCEPT_ENTRY,
                                                          ListEntry);

        VIRTIO_VSOCK_HDR PktHdr = {0};

        PktHdr.type = pListenSocket->type;
        PktHdr.dst_port = pListenSocket->src_port;
        PktHdr.src_cid = pAccept->dst_cid;
        PktHdr.src_port = pAccept->dst_port;
        VIOSockSendResetNoSock(pContext, &PktHdr);

        WdfObjectDelete(pAccept->Memory);
    }
}

static NTSTATUS VIOSockStreamSocketInit(IN PSOCKET_CONTEXT pSocket)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES Attributes;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "%s\n", __FUNCTION__);
    pSocket->ReadDequeueInProgress = 0;
    pSocket->WriteEnqueueInProgress = 0;
    pSocket->BytesUnsent = 0;
    KeInitializeEvent(&pSocket->TxIdleEvent, NotificationEvent, TRUE);

    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_SOCKET,
                "Stream socket %d (%p) initializing\n",
                pSocket->SocketId,
                pSocket->ThisSocket);

    ASSERT(pSocket->ThisSocket);

    KeInitializeEvent(&pSocket->CloseEvent, NotificationEvent, FALSE);

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = pSocket->ThisSocket;
    status = WdfSpinLockCreate(&Attributes, &pSocket->StateLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "WdfSpinLockCreate failed (StateLock) - 0x%x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = pSocket->ThisSocket;
    status = WdfSpinLockCreate(&Attributes, &pSocket->RxLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "WdfSpinLockCreate failed (RxLock) - 0x%x\n", status);
        return status;
    }

    status = VIOSockReadSocketQueueInit(pSocket);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    InitializeListHead(&pSocket->RxCbList);
    pSocket->RxBytes = 0;
    pSocket->RcvLowat = 1; // SO_RCVLOWAT default: any byte makes the socket readable

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = pSocket->ThisSocket;
    status = WdfSpinLockCreate(&Attributes, &pSocket->TxLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "WdfSpinLockCreate failed (TxLock) - 0x%x\n", status);
        return status;
    }
    status = VIOSockWriteSocketQueueInit(pSocket);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = VIOSockTimerCreate(&pSocket->PendedTimer, pSocket->ThisSocket, VIOSockPendedTimerFunc);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "VIOSockTimerCreate failed (PendedTimer) - 0x%x\n", status);
        return status;
    }
    pSocket->PendedRequest = WDF_NO_HANDLE;

    pSocket->src_port = VMADDR_PORT_ANY; // set unbound state
    pSocket->dst_port = VMADDR_PORT_ANY;
    pSocket->dst_cid = VMADDR_CID_ANY;

    pSocket->State = VIOSOCK_STATE_CLOSE;

    pSocket->ConnectTimeout = VSOCK_DEFAULT_CONNECT_TIMEOUT;
    pSocket->BufferMinSize = VSOCK_DEFAULT_BUFFER_MIN_SIZE;
    pSocket->BufferMaxSize = VSOCK_DEFAULT_BUFFER_MAX_SIZE;
    pSocket->SendTimeout = MAXLONGLONG;
    pSocket->RecvTimeout = MAXLONGLONG;

    pSocket->buf_alloc = VSOCK_DEFAULT_BUFFER_SIZE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "<-- %s\n", __FUNCTION__);
    return status;
}

static NTSTATUS VIOSockAcceptForward(IN WDFFILEOBJECT ListenSocket, IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pListenSocket = GetSocketContext(ListenSocket);
    NTSTATUS status;

    if (VIOSockStateGet(pListenSocket) != VIOSOCK_STATE_LISTEN || VIOSockIsFlag(pListenSocket, SOCK_CLEANUP))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "Invalid listen socket state\n");
        return STATUS_INVALID_PARAMETER;
    }

    status = WdfRequestForwardToIoQueue(Request, pListenSocket->AcceptQueue);
    if (NT_SUCCESS(status))
    {
        VIOSockAcceptProcessQueue(pListenSocket);
        status = STATUS_PENDING;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "WdfRequestForwardToIoQueue failed: 0x%x\n", status);
    }

    return status;
}

NTSTATUS VIOSockCreate(IN WDFDEVICE WdfDevice, IN WDFREQUEST Request)
{
    WDFFILEOBJECT FileObject = WdfRequestGetFileObject(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfDevice);
    PSOCKET_CONTEXT pSocket;
    NTSTATUS status = STATUS_SUCCESS;
    WDF_REQUEST_PARAMETERS parameters;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "%s\n", __FUNCTION__);

    ASSERT(FileObject);
    if (WDF_NO_HANDLE == FileObject)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "NULL FileObject\n");
        return STATUS_UNSUCCESSFUL;
    }

    pSocket = GetSocketContext(FileObject);
    pSocket->ThisSocket = FileObject;
    pSocket->SocketId = InterlockedIncrement(&pContext->SocketId);

    WDF_REQUEST_PARAMETERS_INIT(&parameters);

    // check EA presents
    WdfRequestGetParameters(Request, &parameters);

    if (parameters.Parameters.Create.EaLength)
    {
        PFILE_FULL_EA_INFORMATION EaBuffer = (PFILE_FULL_EA_INFORMATION)WdfRequestWdmGetIrp(Request)->AssociatedIrp.SystemBuffer;

        ASSERT(EaBuffer);

        if (EaBuffer->EaValueLength >= sizeof(VIRTIO_VSOCK_PARAMS))
        {
            PVIRTIO_VSOCK_PARAMS pParams = (PVIRTIO_VSOCK_PARAMS)((PCHAR)EaBuffer +
                                                                  ((ULONG_PTR)UFIELD_OFFSET(FILE_FULL_EA_INFORMATION,
                                                                                            EaName) +
                                                                   1 + EaBuffer->EaNameLength));

            // accept() passes only the listening socket handle in the EA (Type stays
            // 0): the accepted socket inherits its type from the listener at match
            // time (VIOSockAcceptInitSocket), so the STREAM gate applies only to a
            // plain socket() create.
            if (pParams->Socket)
            {
                status = VIOSockStreamSocketInit(pSocket);
                if (NT_SUCCESS(status))
                {
                    WDFFILEOBJECT ListenSocket = VIOSockGetSocketFromHandle(pContext,
                                                                            pParams->Socket,
                                                                            WdfRequestIsFrom32BitProcess(Request),
                                                                            WdfRequestGetRequestorMode(Request));

                    if (ListenSocket != WDF_NO_HANDLE)
                    {
                        status = VIOSockAcceptForward(ListenSocket, Request);
                        WdfObjectDereference(ListenSocket);
                    }
                    else
                    {
                        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "Invalid listen socket\n");
                        status = STATUS_INVALID_PARAMETER;
                    }
                }
            }
            else if (pParams->Type == VIRTIO_VSOCK_TYPE_STREAM)
            {
                pSocket->type = pParams->Type;
                status = VIOSockStreamSocketInit(pSocket);
            }
            else
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_CREATE_CLOSE, "Unsupported socket type: %u\n", pParams->Type);
                status = STATUS_NOT_SUPPORTED;
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
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_SOCKET,
                    "Control socket %d (%p) initializing\n",
                    pSocket->SocketId,
                    pSocket->ThisSocket);

        // Socket for select and config retrieve
        VIOSockSetFlag(pSocket, SOCK_CONTROL);
        status = STATUS_SUCCESS;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID VIOSockDoClose(PSOCKET_CONTEXT pSocket)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    pSocket->PeerShutdown = VIRTIO_VSOCK_SHUTDOWN_MASK;

    if (!VIOSockRxHasData(pSocket))
    {
        VIOSockStateSetLocked(pSocket, VIOSOCK_STATE_CLOSING);
    }

    KeSetEvent(&pSocket->CloseEvent, IO_NO_INCREMENT, FALSE);
}

VOID VIOSockCleanup(IN WDFFILEOBJECT FileObject)
{
    PSOCKET_CONTEXT pSocket = GetSocketContext(FileObject);
    WDFREQUEST Request;
    VIOSOCK_STATE PrevState;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "--> %s\n", __FUNCTION__);

    if (VIOSockIsFlag(pSocket, SOCK_CONTROL))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "Control socket %d closed\n", pSocket->SocketId);
        return;
    }

    VIOSockBoundRemove(pSocket);

    if (VIOSockStateGet(pSocket) == VIOSOCK_STATE_CONNECTED || VIOSockStateGet(pSocket) == VIOSOCK_STATE_CLOSING)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_CREATE_CLOSE,
                    "Socket %d, peershutdown: %x\n",
                    pSocket->SocketId,
                    pSocket->PeerShutdown);

        /* Already received SHUTDOWN from peer, reply with RST */
        if ((pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_MASK) == VIRTIO_VSOCK_SHUTDOWN_MASK)
        {
            if (VIOSockStateGet(pSocket) != VIOSOCK_STATE_CLOSING)
            {
                VIOSockSendReset(pSocket, FALSE);
            }
        }
        else if (VIOSockIsFlag(pSocket, SOCK_LINGER) && pSocket->LingerTime == 0)
        {
            // SO_LINGER with a zero timeout is the documented hard close:
            // discard whatever is pending and reset the connection.
            VIOSockSendReset(pSocket, FALSE);
            VIOSockDoClose(pSocket);
        }
        else
        {
            if ((pSocket->Shutdown & VIRTIO_VSOCK_SHUTDOWN_MASK) != VIRTIO_VSOCK_SHUTDOWN_MASK)
            {
                VIOSockSendShutdown(pSocket, VIRTIO_VSOCK_SHUTDOWN_MASK);
            }

            // Never wait for the peer's teardown ack: it may legally take
            // forever (the peer replies only once it has read the data out).
            // The wire converges without us: the peer's eventual SHUTDOWN or
            // RST is answered at the device level. A lingering close
            // (Linux virtio_transport_wait_close parity) waits only for the
            // unsent bytes to drain, bounded by the linger timeout.
            if (VIOSockIsFlag(pSocket, SOCK_LINGER))
            {
                LARGE_INTEGER liTimeout;

                liTimeout.QuadPart = -WDF_ABS_TIMEOUT_IN_SEC(pSocket->LingerTime);
                if (KeWaitForSingleObject(&pSocket->TxIdleEvent, Executive, KernelMode, FALSE, &liTimeout) ==
                    STATUS_TIMEOUT)
                {
                    TraceEvents(TRACE_LEVEL_WARNING,
                                DBG_CREATE_CLOSE,
                                "Socket %d, linger timeout expires with unsent data\n",
                                pSocket->SocketId);
                }
            }
        }
    }

    PrevState = VIOSockStateSetLocked(pSocket, VIOSOCK_STATE_CLOSE);

    VIOSockPendedRequestGetLocked(pSocket, &Request);
    if (Request != WDF_NO_HANDLE)
    {
        WdfRequestComplete(Request, STATUS_CANCELLED);
    }

    // Loopback CONNECTING close: emit RST via the generic path so the pending
    // accept entry on the listener gets cleaned up (VIOSockRxPktHandleListen).
    if (IsLoopbackSocket(pSocket) && PrevState == VIOSOCK_STATE_CONNECTING)
    {
        VIOSockSendReset(pSocket, FALSE);
    }

    if (PrevState == VIOSOCK_STATE_LISTEN)
    {
        VIOSockAcceptCleanup(pSocket);
    }
    else
    {
        // Whatever reached the TxList is considered sent and drains on its
        // own; only the parked WriteQueue needs attention — the pump's
        // per-bite validation sees the CLOSE state and completes each request.
        VIOSockWriteProcessQueue(pSocket);
        VIOSockReadProcessQueue(pSocket);
        VIOSockReadCleanupCb(pSocket);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE, "Socket %d closed\n", pSocket->SocketId);

    VIOSockSetFlag(pSocket, SOCK_CLEANUP);
    if (pSocket->SelectRefs[FDSET_READ] || pSocket->SelectRefs[FDSET_WRITE] || pSocket->SelectRefs[FDSET_EXCPT])
    {
        VIOSockSelectEnqueueWorkItem(GetDeviceContextFromSocket(pSocket));
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_CREATE_CLOSE, "<-- %s\n", __FUNCTION__);
}

VOID VIOSockObjectCleanup(IN WDFOBJECT Object)
{
    PSOCKET_CONTEXT pSocket = GetSocketContext(Object);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (pSocket->EventObject)
    {
        ObDereferenceObject(pSocket->EventObject);
        pSocket->EventObject = NULL;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
}

__inline BOOLEAN VIOSockIsGuestAddrValid(IN PSOCKADDR_VM pAddr)
{
    return (pAddr->svm_family == AF_VSOCK) && (pAddr->svm_cid != VMADDR_CID_HYPERVISOR) &&
           (pAddr->svm_cid != VMADDR_CID_LOCAL) && (pAddr->svm_cid != VMADDR_CID_HOST);
}

__inline BOOLEAN VIOSockIsHostAddrValid(IN PSOCKADDR_VM pAddr, IN ULONG32 ulSrcCid)
{
    return (pAddr->svm_family == AF_VSOCK) &&
           (pAddr->svm_cid == VMADDR_CID_HYPERVISOR || pAddr->svm_cid == VMADDR_CID_HOST ||
            VIOSockIsLocalCid(pAddr->svm_cid, ulSrcCid));
}

static NTSTATUS VIOSockBind(IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PSOCKADDR_VM pAddr;
    SIZE_T stAddrLen;
    NTSTATUS status;
    static ULONG32 uSvmPort;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

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
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_SOCKET,
                    "Invalid addr to bind, cid: %u, port: %u\n",
                    pAddr->svm_cid,
                    pAddr->svm_port);
        return STATUS_INVALID_ADDRESS_COMPONENT;
    }

    if (pAddr->svm_cid != (ULONG32)pContext->Config.guest_cid && pAddr->svm_cid != VMADDR_CID_ANY &&
        pAddr->svm_cid != VMADDR_CID_LOCAL)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid cid: %u\n", pAddr->svm_cid);
        return STATUS_INVALID_ADDRESS_COMPONENT;
    }

    if (VIOSockIsBound(pSocket))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Socket %d already bound\n", pSocket->SocketId);
        return STATUS_INVALID_PARAMETER;
    }

    if (VIOSockStateGet(pSocket) != VIOSOCK_STATE_CLOSE)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid socket state: %u\n", VIOSockStateGet(pSocket));
        return STATUS_UNSUCCESSFUL;
    }

    status = VIOSockBoundAdd(pSocket, pAddr->svm_port);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockBoundAdd failed: 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);

    return status;
}

static NTSTATUS VIOSockConnect(IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PSOCKADDR_VM pAddr;
    SIZE_T stAddrLen;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    if (!VIOSockIsHostAddrValid(pAddr, (ULONG32)pContext->Config.guest_cid))
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_SOCKET,
                    "Invalid addr to connect, cid: %u, port: %u\n",
                    pAddr->svm_cid,
                    pAddr->svm_port);
        return STATUS_INVALID_ADDRESS_COMPONENT;
    }

    switch (VIOSockStateGet(pSocket))
    {
        case VIOSOCK_STATE_CONNECTING:
            status = STATUS_CONNECTION_ESTABLISHING;
            break;
        case VIOSOCK_STATE_CONNECTED:
        case VIOSOCK_STATE_CLOSING:
            status = STATUS_CONNECTION_ACTIVE;
            break;
        case VIOSOCK_STATE_LISTEN:
            status = STATUS_INVALID_PARAMETER;
            break;
        default:
            status = STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid socket state: %u\n", pSocket->State);
        return status;
    }

    if (!VIOSockIsBound(pSocket))
    {
        // autobind
        status = VIOSockBoundAdd(pSocket, VMADDR_PORT_ANY);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockBoundAdd failed: 0x%x\n", status);
            return status;
        }
    }

    VIOSockEventClearBitLocked(pSocket, FD_CONNECT_BIT);

    // Local dst stored as VMADDR_CID_LOCAL: survives Config.guest_cid mutation on migration.
    pSocket->dst_cid = VIOSockIsLocalCid(pAddr->svm_cid, pContext->Config.guest_cid) ? VMADDR_CID_LOCAL
                                                                                     : pAddr->svm_cid;
    pSocket->dst_port = pAddr->svm_port;

    // Mark this as an outbound socket: FD_CONNECT applies only to a socket that
    // issued connect(), never to an accepted one.
    VIOSockSetFlag(pSocket, SOCK_CONNECT);

    // Capture the reset generation this connect rides; VIOSockConnectedAdd fails
    // the connect later if a transport reset bumped the generation meanwhile.
    pSocket->ConnectGeneration = pContext->ResetGeneration;

    // no lock required
    VIOSockStateSet(pSocket, VIOSOCK_STATE_CONNECTING);

    if (!VIOSockIsNonBlocking(pSocket))
    {
        ASSERT(pSocket->ConnectTimeout && pSocket->ConnectTimeout != MAXLONGLONG);
        status = VIOSockPendedRequestSetLocked(pSocket,
                                               Request,
                                               VIOSockTimerTimeoutToDeadline(pSocket->ConnectTimeout));
        if (!NT_SUCCESS(status))
        {
            VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSE);
            return status;
        }
    }

    if (VIOSockIsLocalCid(pSocket->dst_cid, pContext->Config.guest_cid))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "Connect to local socket (loopback)\n");
        VIOSockSetFlag(pSocket, SOCK_LOOPBACK);
    }
    else
    {
        VIOSockResetFlag(pSocket, SOCK_LOOPBACK);
    }

    status = VIOSockSendConnect(pSocket);

    if (!NT_SUCCESS(status))
    {
        // no lock required
        VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSE);
        if (!VIOSockIsNonBlocking(pSocket))
        {
            if (!NT_SUCCESS(VIOSockPendedRequestGetLocked(pSocket, &Request)))
            {
                status = STATUS_PENDING; // do not complete canceled request
            }
        }
    }
    else
    {
        status = VIOSockIsNonBlocking(pSocket) ? STATUS_CANT_WAIT : STATUS_PENDING;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

_Requires_lock_not_held_(pSocket->StateLock) BOOLEAN VIOSockShutdownFromPeer(PSOCKET_CONTEXT pSocket, ULONG uFlags)
{
    BOOLEAN bRes;
    ULONG32 uDrain;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, uFlags: 0x%x\n", __FUNCTION__, uFlags);

    WdfSpinLockAcquire(pSocket->StateLock);
    uDrain = ~pSocket->PeerShutdown & uFlags;
    pSocket->PeerShutdown |= uFlags;
    bRes = (pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_MASK) == VIRTIO_VSOCK_SHUTDOWN_MASK;
    WdfSpinLockRelease(pSocket->StateLock);
    if (uDrain & VIRTIO_VSOCK_SHUTDOWN_SEND)
    {
        // Peer closed its send: no more data will arrive, so wake every parked recv.
        // The matcher drains the whole queue in one pass now - each request completes
        // with EOF and keeps the worker going - so a single call empties it.
        VIOSockReadProcessQueue(pSocket);

        // Raise the graceful FD_CLOSE indication. Per Winsock this fires when the
        // peer performs shutdown(SD_SEND) or closesocket (read-side close), not
        // only on a full both-directions close, and the close is signaled by
        // FD_CLOSE rather than a final FD_READ. uDrain gates this to the first
        // send-shutdown so the edge-triggered event is recorded exactly once.
        if (!VIOSockIsDone(pSocket))
        {
            VIOSockEventSetBitLocked(pSocket, FD_CLOSE_BIT, STATUS_SUCCESS);
        }
    }

    if (uDrain & VIRTIO_VSOCK_SHUTDOWN_RCV)
    {
        // Peer closed its receive side: parked sends can never proceed, the
        // pump completes them via the per-bite validation (WSAESHUTDOWN).
        VIOSockWriteProcessQueue(pSocket);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, bRes: %u\n", __FUNCTION__, bRes);

    return bRes;
}

_Requires_lock_not_held_(pSocket->StateLock) static NTSTATUS VIOSockStateValidateShutdown(PSOCKET_CONTEXT pSocket,
                                                                                          ULONG32 uHow)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->StateLock);

    if ((pSocket->Shutdown & uHow) == uHow)
    {
        status = STATUS_LOCAL_DISCONNECT;
    }
    else if (pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_MASK)
    {
        status = STATUS_SUCCESS;
    }
    else if (VIOSockStateGet(pSocket) == VIOSOCK_STATE_CLOSING)
    {
        status = STATUS_CONNECTION_RESET;
    }
    else if (VIOSockStateGet(pSocket) != VIOSOCK_STATE_CONNECTED)
    {
        status = STATUS_CONNECTION_INVALID;
    }
    else
    {
        status = STATUS_SUCCESS;
    }

    WdfSpinLockRelease(pSocket->StateLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

static NTSTATUS VIOSockShutdown(IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    int *pHow;
    SIZE_T stHowLen;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

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

    (*pHow)++;
    if ((*pHow & ~VIRTIO_VSOCK_SHUTDOWN_MASK) || !*pHow)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid shutdown flags: %u\n", *pHow);
        return STATUS_INVALID_PARAMETER;
    }

    status = VIOSockStateValidateShutdown(pSocket, *pHow);

    if (NT_SUCCESS(status))
    {
        ULONG32 uDrain = ~pSocket->Shutdown & (ULONG32)*pHow;

        pSocket->Shutdown |= (ULONG32)*pHow;
        status = VIOSockSendShutdown(pSocket, *pHow);

        if (uDrain & VIRTIO_VSOCK_SHUTDOWN_RCV)
        {
            // We closed our own receive side: drop whatever is buffered (its credit
            // goes back to the peer) and wake every parked recv - the matcher fails
            // each with WSAESHUTDOWN and drains the rest.
            VIOSockReadDiscardCb(pSocket);
            VIOSockReadProcessQueue(pSocket);
        }

        if (uDrain & VIRTIO_VSOCK_SHUTDOWN_SEND)
        {
            // We closed our own send side: parked sends complete via the
            // per-bite validation (PIPE_DISCONNECTED -> WSAESHUTDOWN).
            VIOSockWriteProcessQueue(pSocket);
        }
    }
    else if (status == STATUS_LOCAL_DISCONNECT)
    {
        status = STATUS_SUCCESS;
    }

    return status;
}

static NTSTATUS VIOSockListen(IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    int *pBacklog;
    SIZE_T stBacklogLen;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pBacklog), &pBacklog, &stBacklogLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stBacklogLen >= sizeof(*pBacklog));

    switch (VIOSockStateGet(pSocket))
    {
        case VIOSOCK_STATE_CLOSE:
            status = VIOSockIsBound(pSocket) ? VIOSockListenSocketInit(pSocket, *pBacklog) : STATUS_INVALID_PARAMETER;
            break;
        case VIOSOCK_STATE_LISTEN:
            pSocket->Backlog = *pBacklog;
            status = STATUS_SUCCESS;
            break;
        default:
            status = STATUS_CONNECTION_ACTIVE;
            break;
    }

    return status;
}

__inline NTSTATUS VIOSockGetEventFromHandle(IN HANDLE hEvent, IN KPROCESSOR_MODE AccessMode, OUT PKEVENT *pEvent)
{
    // hEvent normally arrives from user mode (WSAEventSelect / WSAEnumNetworkEvents
    // IOCTL input buffer); the caller passes its requestor mode so the lookup goes
    // through the right domain and does not trip Driver Verifier's 0xC4/0xF6 check.
    // DesiredAccess=0 keeps the reference alive without the full-access rights the
    // event's user opener typically does not grant; KeSetEvent/KeClearEvent operate
    // on the raw object pointer and do not re-check.
    return ObReferenceObjectByHandle(hEvent, 0, *ExEventObjectType, AccessMode, (PVOID)pEvent, NULL);
}

_Requires_lock_not_held_(pSocket->StateLock) static VOID VIOSockEventUnregister(IN PSOCKET_CONTEXT pSocket,
                                                                                IN PVIRTIO_VSOCK_NETWORK_EVENTS lpNetworkEvents,
                                                                                IN PKEVENT pEvent)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    if (pEvent)
    {
        pSocket->EventObject = NULL;
    }
    lpNetworkEvents->NetworkEvents = pSocket->Events & pSocket->EventsMask;
    RtlCopyMemory(lpNetworkEvents->Status, pSocket->EventsStatus, sizeof(pSocket->EventsStatus));
    pSocket->Events = 0;
    WdfSpinLockRelease(pSocket->StateLock);

    if (pEvent)
    {
        KeClearEvent(pEvent);
        ObDereferenceObject(pEvent);
    }
}

_Requires_lock_not_held_(pSocket->StateLock) static VOID VIOSockEventsReport(IN PSOCKET_CONTEXT pSocket,
                                                                             IN PVIRTIO_VSOCK_NETWORK_EVENTS lpNetworkEvents,
                                                                             IN PKEVENT pEvent)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    lpNetworkEvents->NetworkEvents = pSocket->Events & pSocket->EventsMask;
    RtlCopyMemory(lpNetworkEvents->Status, pSocket->EventsStatus, sizeof(pSocket->EventsStatus));
    pSocket->Events = 0;
    if (pEvent)
    {
        KeClearEvent(pEvent);
    }

    WdfSpinLockRelease(pSocket->StateLock);
}

static NTSTATUS VIOSockEnumNetEvents(IN WDFREQUEST Request, OUT size_t *pLength)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PULONGLONG pEventObject;
    SIZE_T stEventObjectLen;
    PVIRTIO_VSOCK_NETWORK_EVENTS lpNetworkEvents;
    SIZE_T stNetworkEventsLen;
    NTSTATUS status;
    HANDLE hEvent = NULL;
    PKEVENT pEvent = NULL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

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
        hEvent = Handle32ToHandle((void *POINTER_32)(ULONG)*pEventObject);
    }
    else
#endif //_WIN64
    {
        hEvent = (HANDLE)(ULONG_PTR)*pEventObject;
    }

    if (hEvent)
    {
        status = VIOSockGetEventFromHandle(hEvent, WdfRequestGetRequestorMode(Request), &pEvent);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockGetEventFromHandle failed: 0x%x\n", status);
            return status;
        }
    }

    if (pEvent && pEvent != pSocket->EventObject)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid event handle\n");
        ObDereferenceObject(pEvent);
        return STATUS_INVALID_PARAMETER;
    }

    VIOSockEventsReport(pSocket, lpNetworkEvents, pEvent);
    if (pEvent)
    {
        ObDereferenceObject(pEvent);
    }

    *pLength = sizeof(*lpNetworkEvents);

    return STATUS_SUCCESS;
}

__inline NTSTATUS VIOSockSetNonBlocking(IN PSOCKET_CONTEXT pSocket, IN BOOLEAN bNonBlocking)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, bNonBlocking: %u\n", __FUNCTION__, bNonBlocking);

    if (bNonBlocking)
    {
        // VIOSockSetFlag returns the previous masked value: 0 on first set (blocking→non-blocking
        // transition), non-zero when the flag was already set. Drain queued blocking ops only
        // on the transition.
        if (!VIOSockSetFlag(pSocket, SOCK_NON_BLOCK))
        {
            // Parked blocking sends: the pump re-evaluates them — whatever
            // the credit admits is sliced, the rest completes with partial
            // Information or WSAEWOULDBLOCK.
            if (VIOSockStateGet(pSocket) == VIOSOCK_STATE_LISTEN)
            {
                VIOSockAcceptProcessQueue(pSocket);
            }
            else
            {
                VIOSockWriteProcessQueue(pSocket);
                VIOSockReadProcessQueue(pSocket);
            }
        }
    }
    else
    {
        if (pSocket->EventsMask)
        {
            return STATUS_INVALID_DEVICE_STATE;
        }
        VIOSockResetFlag(pSocket, SOCK_NON_BLOCK);
    }

    return STATUS_SUCCESS;
}

_Requires_lock_not_held_(pSocket->StateLock) static VOID VIOSockEventRegister(IN PSOCKET_CONTEXT pSocket,
                                                                              IN LONG lNetworkEvents,
                                                                              IN PKEVENT pEvent)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->EventsMask = lNetworkEvents;
    if (pSocket->EventObject)
    {
        ObDereferenceObject(pSocket->EventObject);
    }

    pSocket->EventObject = pEvent;

    /* Persistence of network events: re-record on arm any event that already
     * happened, from the socket's durable state, so a re-arm after
     * WSAEnumNetworkEvents (which zeroed Events) reactivates it. */
    if ((lNetworkEvents & FD_READ) && VIOSockRxReadReady(pSocket))
    {
        VIOSockEventMarkBit(pSocket, FD_READ_BIT, STATUS_SUCCESS);
    }
    if ((lNetworkEvents & FD_ACCEPT) && pSocket->AcceptPended)
    {
        VIOSockEventMarkBit(pSocket, FD_ACCEPT_BIT, STATUS_SUCCESS);
    }
    if ((lNetworkEvents & FD_CLOSE) && (pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_SEND))
    {
        VIOSockEventMarkBit(pSocket, FD_CLOSE_BIT, pSocket->EventsStatus[FD_CLOSE_BIT]);
    }
    if ((lNetworkEvents & FD_CONNECT) && VIOSockIsFlag(pSocket, SOCK_CONNECT))
    {
        // Outbound connect that already completed: CONNECTED = success,
        // CLOSE with a non-success status = failure. CONNECTING is still in
        // flight, so there is nothing to persist yet. The SOCK_CONNECT guard
        // keeps accepted sockets (also CONNECTED) out of this.
        VIOSOCK_STATE state = VIOSockStateGet(pSocket);
        if (state == VIOSOCK_STATE_CONNECTED)
        {
            VIOSockEventMarkBit(pSocket, FD_CONNECT_BIT, STATUS_SUCCESS);
        }
        else if (state == VIOSOCK_STATE_CLOSE && !NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT_BIT]))
        {
            VIOSockEventMarkBit(pSocket, FD_CONNECT_BIT, pSocket->EventsStatus[FD_CONNECT_BIT]);
        }
    }

    if (pSocket->EventObject && (pSocket->Events & pSocket->EventsMask) != 0)
    {
        KeSetEvent(pSocket->EventObject, IO_NO_INCREMENT, FALSE);
    }

    WdfSpinLockRelease(pSocket->StateLock);
}

static NTSTATUS VIOSockEventSelect(IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PVIRTIO_VSOCK_EVENT_SELECT pEventSelect;
    SIZE_T stEventSelectLen;
    NTSTATUS status;
    PKEVENT pEvent = NULL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

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
        HANDLE hEvent;

#ifdef _WIN64
        if (WdfRequestIsFrom32BitProcess(Request))
        {
            hEvent = Handle32ToHandle((void *POINTER_32)pEventSelect->hEventObject);
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

        status = VIOSockGetEventFromHandle(hEvent, WdfRequestGetRequestorMode(Request), &pEvent);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockGetEventFromHandle failed: 0x%x\n", status);
            return status;
        }
    }

    VIOSockSetNonBlocking(pSocket, TRUE);

    VIOSockEventRegister(pSocket, pEventSelect->lNetworkEvents, pEvent);

    return STATUS_SUCCESS;
}

BOOLEAN VIOSockEventMarkBit(IN PSOCKET_CONTEXT pSocket, IN ULONG uSetBit, IN NTSTATUS Status)
{
    ULONG uEvent = (1 << uSetBit);
    BOOLEAN bSetEvent;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, uSetBit: 0x%x\n", __FUNCTION__, uSetBit);

    bSetEvent = !!(~pSocket->Events & uEvent & pSocket->EventsMask);

    pSocket->Events |= uEvent;
    pSocket->EventsStatus[uSetBit] = Status;

    if (!NT_SUCCESS(Status))
    {
        // keep the failure for getsockopt(SO_ERROR): a failed connect or a
        // reset is otherwise visible only through the network event status
        pSocket->SoError = Status;
    }

    return bSetEvent;
}

_IRQL_requires_max_(DISPATCH_LEVEL) VOID VIOSockEventNotify(IN PSOCKET_CONTEXT pSocket, IN BOOLEAN bSetEvent)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, bSetEvent: 0x%x\n", __FUNCTION__, bSetEvent);

    VIOSockSelectRun(pSocket);

    if (bSetEvent && pSocket->EventObject)
    {
        KeSetEvent(pSocket->EventObject, IO_NO_INCREMENT, FALSE);
    }
}

VOID VIOSockEventSetBit(IN PSOCKET_CONTEXT pSocket, IN ULONG uSetBit, IN NTSTATUS Status)
{
    BOOLEAN bSetEvent = VIOSockEventMarkBit(pSocket, uSetBit, Status);
    VIOSockEventNotify(pSocket, bSetEvent);
}

_Requires_lock_not_held_(pSocket->StateLock) VOID VIOSockEventSetBitLocked(IN PSOCKET_CONTEXT pSocket,
                                                                           IN ULONG uSetBit,
                                                                           IN NTSTATUS Status)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    VIOSockEventSetBit(pSocket, uSetBit, Status);
    WdfSpinLockRelease(pSocket->StateLock);
}

_Requires_lock_not_held_(pSocket->StateLock) VOID VIOSockEventClearBitLocked(IN PSOCKET_CONTEXT pSocket,
                                                                             IN ULONG uClearBit)
{
    ULONG uEvent = (1 << uClearBit);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, uClearBit: 0x%x\n", __FUNCTION__, uClearBit);

    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->Events &= ~uEvent;
    WdfSpinLockRelease(pSocket->StateLock);
}

static NTSTATUS VIOSockGetPeerName(IN WDFREQUEST Request, OUT size_t *pLength)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PSOCKADDR_VM pAddr;
    SIZE_T stAddrLen;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    switch (VIOSockStateGet(pSocket))
    {
        case VIOSOCK_STATE_CONNECTED:
        case VIOSOCK_STATE_CLOSING:
        case VIOSOCK_STATE_CLOSE:
            // Keep answering after the connection went down: WSPAccept
            // fetches the peer name right after accepting, and the peer may
            // legally have closed/reset by then (deterministically so on
            // loopback). A never-connected socket keeps
            // dst_port == VMADDR_PORT_ANY and still fails below.
            if (pSocket->dst_port != VMADDR_PORT_ANY)
            {
                RtlZeroBytes(pAddr, sizeof(*pAddr));
                pAddr->svm_family = AF_VSOCK;
                pAddr->svm_cid = pSocket->dst_cid;
                pAddr->svm_port = pSocket->dst_port;
                *pLength = sizeof(*pAddr);
                status = STATUS_SUCCESS;
                break;
            }
            __fallthrough;
        default:
            status = STATUS_INVALID_DEVICE_STATE;
    }

    return status;
}

static NTSTATUS VIOSockGetSockName(IN WDFREQUEST Request, OUT size_t *pLength)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PSOCKADDR_VM pAddr;
    SIZE_T stAddrLen;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    if (VIOSockIsBound(pSocket))
    {
        RtlZeroBytes(pAddr, sizeof(*pAddr));
        pAddr->svm_family = AF_VSOCK;
        pAddr->svm_cid = IsLoopbackSocket(pSocket) ? VMADDR_CID_LOCAL : (ULONG32)pContext->Config.guest_cid;
        pAddr->svm_port = pSocket->src_port;
        *pLength = sizeof(*pAddr);
        status = STATUS_SUCCESS;
    }
    else
    {
        status = STATUS_INVALID_DEVICE_STATE;
    }

    return status;
}
/*
 * Structure used for manipulating linger option.
 */
typedef struct _LINGER
{
    USHORT l_onoff;  /* option on/off */
    USHORT l_linger; /* linger time */
} LINGER, *PLINGER;

#define SO_ACCEPTCONN 0x0002 /* socket has had listen() */
#define SO_LINGER     0x0080 /* linger on close if data present */
#define SO_RCVLOWAT   0x1004 /* receive low-water mark */

#define SOCK_STREAM   1

/*
 * Structure used in select() call, taken from the BSD file sys/time.h.
 */
struct timeval
{
    long tv_sec;  /* seconds */
    long tv_usec; /* and microseconds */
};

#define USEC_PER_SEC 1000000L

// SoError read-and-clear under StateLock, kept out of the paged getsockopt
// handler so the spinlock is not taken in pageable code (SDV C28150).
static NTSTATUS VIOSockReadClearSoError(IN PSOCKET_CONTEXT pSocket)
{
    NTSTATUS SoError;

    WdfSpinLockAcquire(pSocket->StateLock);
    SoError = pSocket->SoError;
    pSocket->SoError = STATUS_SUCCESS;
    WdfSpinLockRelease(pSocket->StateLock);

    return SoError;
}

static NTSTATUS VIOSockGetSockOpt(IN WDFREQUEST Request, OUT size_t *pLength)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PVIRTIO_VSOCK_OPT pOpt;
    SIZE_T stOptLen;
    PVOID pOptVal;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

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
        pOptVal = Ptr32ToPtr((void *POINTER_32)(ULONG)pOpt->optval);
    }
    else
#endif //_WIN64
    {
        pOptVal = (PVOID)pOpt->optval;
    }

    if (WdfRequestGetRequestorMode(Request) == UserMode)
    {
        WDFMEMORY Memory;
        status = WdfRequestProbeAndLockUserBufferForWrite(Request, pOptVal, pOpt->optlen, &Memory);

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR,
                        DBG_IOCTLS,
                        "WdfRequestProbeAndLockUserBufferForWrite failed: 0x%x\n",
                        status);
            return status;
        }
        pOptVal = WdfMemoryGetBuffer(Memory, NULL);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "OptName: 0x%04x\n", pOpt->optname);

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

        case SO_ERROR:
            if (pOpt->optlen < sizeof(ULONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            // read-and-clear, as the option is specified; the value is the
            // NTSTATUS of the failure, viosocklib maps it to a Winsock error
            *(PULONG)pOptVal = (ULONG)VIOSockReadClearSoError(pSocket);
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

        case SO_SNDTIMEO:
            if (pOpt->optlen < sizeof(ULONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            *(PULONG)pOptVal = pSocket->SendTimeout == MAXLONGLONG ? 0
                                                                   : (ULONG)(pSocket->SendTimeout / WDF_TIMEOUT_TO_MS);
            pOpt->optlen = sizeof(ULONG);
            break;

        case SO_RCVTIMEO:
            if (pOpt->optlen < sizeof(ULONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            *(PULONG)pOptVal = pSocket->RecvTimeout == MAXLONGLONG ? 0
                                                                   : (ULONG)(pSocket->RecvTimeout / WDF_TIMEOUT_TO_MS);
            pOpt->optlen = sizeof(ULONG);
            break;

        case SO_RCVLOWAT:
            if (pOpt->optlen < sizeof(ULONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            *(PULONG)pOptVal = pSocket->RcvLowat;
            pOpt->optlen = sizeof(ULONG);
            break;

        case SO_VM_SOCKETS_BUFFER_SIZE:
            if (pOpt->optlen < sizeof(ULONGLONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            *(PULONGLONG)pOptVal = pSocket->buf_alloc;
            pOpt->optlen = sizeof(ULONGLONG);
            break;

        case SO_VM_SOCKETS_BUFFER_MAX_SIZE:
            if (pOpt->optlen < sizeof(ULONGLONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            *(PULONGLONG)pOptVal = pSocket->BufferMaxSize;
            pOpt->optlen = sizeof(ULONGLONG);
            break;

        case SO_VM_SOCKETS_BUFFER_MIN_SIZE:
            if (pOpt->optlen < sizeof(ULONGLONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            *(PULONGLONG)pOptVal = pSocket->BufferMinSize;
            pOpt->optlen = sizeof(ULONGLONG);
            break;

        case SO_VM_SOCKETS_CONNECT_TIMEOUT:
            if (pOpt->optlen < sizeof(struct timeval))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            else
            {
                struct timeval *tv = (struct timeval *)pOptVal;

                tv->tv_sec = (long)NANO_TO_SEC(pSocket->ConnectTimeout);
                tv->tv_usec = (long)NANO_TO_USEC(pSocket->ConnectTimeout - SEC_TO_NANO((LONGLONG)tv->tv_sec));

                pOpt->optlen = sizeof(*tv);
            }
            break;
        default:
            status = STATUS_NOT_IMPLEMENTED;
    }

    if (NT_SUCCESS(status))
    {
        *pLength = sizeof(*pOpt);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

__inline VOID VIOSockSetLinger(IN PSOCKET_CONTEXT pSocket, IN PLINGER pLinger)
{
    if (pLinger->l_onoff)
    {
        pSocket->LingerTime = pLinger->l_linger;
        VIOSockSetFlag(pSocket, SOCK_LINGER);
    }
    else
    {
        VIOSockResetFlag(pSocket, SOCK_LINGER);
    }
}

static VOID VIOSockRxUpdateBufferSize(IN PSOCKET_CONTEXT pSocket, IN ULONG uBufferSize)
{
    if (uBufferSize > pSocket->BufferMaxSize)
    {
        uBufferSize = pSocket->BufferMaxSize;
    }

    if (uBufferSize < pSocket->BufferMinSize)
    {
        uBufferSize = pSocket->BufferMinSize;
    }

    if (uBufferSize != pSocket->buf_alloc)
    {
        pSocket->buf_alloc = uBufferSize;
        VIOSockSendCreditUpdate(pSocket);
    }
}

// Not pageable - acquires RxLock, which raises IRQL to DISPATCH_LEVEL, so it must
// not sit in the PAGE section like its paged caller VIOSockSetSockOpt. Applies the
// SO_RCVLOWAT mark (coercing 0 to 1 like Linux), then reconciles the sticky FD_READ
// event against the new mark and kicks a credit update if the sender is unaware of
// bytes we already consumed.
static VOID VIOSockSetRcvLowat(IN PSOCKET_CONTEXT pSocket, IN ULONG Val)
{
    BOOLEAN bSendUpdate;
    BOOLEAN bReadReady;

    if (Val == 0)
    {
        Val = 1; // mirror Linux 'val ? : 1'
    }

    WdfSpinLockAcquire(pSocket->RxLock);
    // Kick the sender if we don't have enough buffered to satisfy the new low-water
    // mark and the peer is unaware of bytes we already consumed.
    bSendUpdate = (pSocket->RxBytes < Val) && (pSocket->fwd_cnt != pSocket->last_fwd_cnt);
    pSocket->RcvLowat = Val;
    bReadReady = VIOSockRxReadReady(pSocket);
    WdfSpinLockRelease(pSocket->RxLock);

    // The FD_READ event bit is sticky, unlike Linux which recomputes readability from
    // rx_bytes on every poll. Reconcile it against the new mark: data may already be
    // buffered (it can arrive before this call), so set FD_READ when the mark is now
    // met and clear it otherwise.
    if (bReadReady)
    {
        VIOSockEventSetBitLocked(pSocket, FD_READ_BIT, STATUS_SUCCESS);
    }
    else
    {
        VIOSockEventClearBitLocked(pSocket, FD_READ_BIT);
    }

    if (bSendUpdate)
    {
        VIOSockSendCreditUpdate(pSocket);
    }
}

static NTSTATUS VIOSockSetSockOpt(IN WDFREQUEST Request)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PVIRTIO_VSOCK_OPT pOpt;
    SIZE_T stOptLen;
    PVOID pOptVal;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

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
        pOptVal = Ptr32ToPtr((void *POINTER_32)(ULONG)pOpt->optval);
    }
    else
#endif //_WIN64
    {
        pOptVal = (PVOID)pOpt->optval;
    }

    if (WdfRequestGetRequestorMode(Request) == UserMode)
    {
        WDFMEMORY Memory;
        status = WdfRequestProbeAndLockUserBufferForRead(Request, pOptVal, pOpt->optlen, &Memory);

        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR,
                        DBG_IOCTLS,
                        "WdfRequestProbeAndLockUserBufferForRead failed: 0x%x\n",
                        status);
            return status;
        }
        pOptVal = WdfMemoryGetBuffer(Memory, NULL);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "OptName: 0x%04x\n", pOpt->optname);

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

        case SO_SNDTIMEO:
            if (pOpt->optlen < sizeof(ULONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            pSocket->SendTimeout = *(PULONG)pOptVal ? WDF_ABS_TIMEOUT_IN_MS(*(PULONG)pOptVal) : MAXLONGLONG;
            break;

        case SO_RCVTIMEO:
            if (pOpt->optlen < sizeof(ULONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            pSocket->RecvTimeout = *(PULONG)pOptVal ? WDF_ABS_TIMEOUT_IN_MS(*(PULONG)pOptVal) : MAXLONGLONG;
            break;

        case SO_RCVLOWAT:
            {
                ULONG val;

                if (pOpt->optlen < sizeof(ULONG))
                {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }

                val = *(PULONG)pOptVal;

                // Linux coerces a negative SO_RCVLOWAT to INT_MAX, which then fails the
                // "must not exceed the rx buffer" check; reject negatives the same way.
                if ((ULONG32)val > pSocket->buf_alloc)
                {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }

                VIOSockSetRcvLowat(pSocket, val);
                break;
            }

        case SO_VM_SOCKETS_BUFFER_SIZE:
            if (pOpt->optlen < sizeof(ULONGLONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            // BufferMinSize/MaxSize and buf_alloc are stored as ULONG32 (the wire
            // virtio_vsock_hdr.buf_alloc is u32); reject values that don't fit.
            if (*(PULONGLONG)pOptVal > MAXULONG32)
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            VIOSockRxUpdateBufferSize(pSocket, *(PULONG)pOptVal);
            break;

        case SO_VM_SOCKETS_BUFFER_MAX_SIZE:
            if (pOpt->optlen < sizeof(ULONGLONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (*(PULONGLONG)pOptVal > MAXULONG32)
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            pSocket->BufferMaxSize = *(PULONG)pOptVal;
            VIOSockRxUpdateBufferSize(pSocket, pSocket->buf_alloc);
            break;

        case SO_VM_SOCKETS_BUFFER_MIN_SIZE:
            if (pOpt->optlen < sizeof(ULONGLONG))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (*(PULONGLONG)pOptVal > MAXULONG32)
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            pSocket->BufferMinSize = *(PULONG)pOptVal;
            VIOSockRxUpdateBufferSize(pSocket, pSocket->buf_alloc);
            break;

        case SO_VM_SOCKETS_CONNECT_TIMEOUT:
            if (pOpt->optlen < sizeof(struct timeval))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            else
            {
                struct timeval *tv = (struct timeval *)pOptVal;
                LONGLONG llTimeout = 0;

                if (tv->tv_sec >= 0 && tv->tv_usec < USEC_PER_SEC && tv->tv_sec < (LONG_MAX / 1000 - 1))
                {
                    llTimeout = SEC_TO_NANO(tv->tv_sec) + USEC_TO_NANO(tv->tv_usec);
                }

                if (!llTimeout)
                {
                    llTimeout = VSOCK_DEFAULT_CONNECT_TIMEOUT;
                }

                pSocket->ConnectTimeout = llTimeout;
            }
            break;

        default:
            // an unknown option must fail, otherwise setsockopt silently
            // succeeds; WSAENOPROTOOPT (see NtStatusToWsaError)
            status = STATUS_NOT_IMPLEMENTED;
            break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

static NTSTATUS VIOSockIoctl(IN WDFREQUEST Request, OUT size_t *pLength)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PVIRTIO_VSOCK_IOCTL_IN pInParams;
    BOOLEAN bIs32BitProcess = FALSE;
    SIZE_T stInParamsLen;
    PVOID lpvOutBuffer = NULL;
    SIZE_T stOutBufferLen = 0;
    NTSTATUS status;
    PVOID lpvInBuffer = NULL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pInParams), &pInParams, &stInParamsLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stInParamsLen >= sizeof(*pInParams));

    if (pInParams->dwIoControlCode & IOC_OUT)
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
            bIs32BitProcess = TRUE;
            lpvInBuffer = Ptr32ToPtr((void *POINTER_32)(ULONG)pInParams->lpvInBuffer);
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
                WDFMEMORY Memory;
                status = WdfRequestProbeAndLockUserBufferForRead(Request, lpvInBuffer, pInParams->cbInBuffer, &Memory);

                if (!NT_SUCCESS(status))
                {
                    TraceEvents(TRACE_LEVEL_ERROR,
                                DBG_IOCTLS,
                                "WdfRequestProbeAndLockUserBufferForRead failed: 0x%x\n",
                                status);
                    return status;
                }
                lpvInBuffer = WdfMemoryGetBuffer(Memory, NULL);
            }
        }
        else
        {
            status = STATUS_INVALID_PARAMETER;
        }
    }

    if (NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "dwIoControlCode: 0x%08x\n", pInParams->dwIoControlCode);

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
                {
                    status = STATUS_INVALID_PARAMETER;
                }
                break;

            case SIO_VSOCK_OUTQ:
                if (stOutBufferLen >= sizeof(ULONG))
                {
                    // Bytes accepted from the application but not yet consumed
                    // by the host — Linux SIOCOUTQ (unsent_bytes) parity, NOT
                    // the peer-acked window (tx_cnt - peer_fwd_cnt): the value
                    // must drain to zero without the peer reading. Loopback
                    // delivery is synchronous, so loopback sockets report 0.
                    *(PULONG)lpvOutBuffer = (ULONG)pSocket->BytesUnsent;
                    *pLength = sizeof(ULONG);
                    status = STATUS_SUCCESS;
                }
                else
                {
                    status = STATUS_INVALID_PARAMETER;
                }
                break;

            case FIONBIO:
                if (pInParams->cbInBuffer >= sizeof(ULONG))
                {
                    status = VIOSockSetNonBlocking(pSocket, !!(*(PULONG)lpvInBuffer));
                }
                else
                {
                    status = STATUS_INVALID_PARAMETER;
                }
                break;

            case SIO_EXT_POLL:
                status = VIOSockPoll(Request,
                                     bIs32BitProcess,
                                     (PVIOSOCK_POLL_DATA)lpvInBuffer,
                                     pInParams->cbInBuffer,
                                     (PVIOSOCK_POLL_DATA)lpvOutBuffer,
                                     stOutBufferLen,
                                     pLength);
                break;

            case SIO_ADDRESS_LIST_QUERY:
                // The only local address is the guest CID. Return it as a flat
                // SOCKADDR_VM; the WSP library (which owns the user-mode output
                // buffer) wraps it into the caller's SOCKET_ADDRESS_LIST with a
                // valid user-mode lpSockaddr pointer.
                if (stOutBufferLen >= sizeof(SOCKADDR_VM))
                {
                    PSOCKADDR_VM pAddr = (PSOCKADDR_VM)lpvOutBuffer;

                    RtlZeroBytes(pAddr, sizeof(*pAddr));
                    pAddr->svm_family = AF_VSOCK;
                    pAddr->svm_cid = (ULONG32)pContext->Config.guest_cid;
                    pAddr->svm_port = 0;
                    *pLength = sizeof(SOCKADDR_VM);
                    status = STATUS_SUCCESS;
                }
                else
                {
                    *pLength = sizeof(SOCKADDR_VM);
                    status = STATUS_BUFFER_TOO_SMALL;
                }
                break;

            case SIO_ADDRESS_LIST_CHANGE:
                // Pend until the local address (guest CID) changes on a transport
                // reset; completed from VIOSockEvtVqProcess -> VIOSockAddressListChange.
                // The device-wide manual queue holds any number of pending requests
                // and owns their cancellation.
                status = WdfRequestForwardToIoQueue(Request, pContext->AddrChangeQueue);
                if (NT_SUCCESS(status))
                {
                    status = STATUS_PENDING;
                }
                break;

            default:
                status = STATUS_NOT_SUPPORTED;
                break;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

NTSTATUS
VIOSockDeviceControl(IN WDFREQUEST Request, IN ULONG IoControlCode, IN OUT size_t *pLength)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE,
                DBG_IOCTLS,
                "<-- %s, code: 0x%08x, socket %d\n",
                __FUNCTION__,
                IoControlCode,
                GetSocketContextFromRequest(Request)->SocketId);

    *pLength = 0;

    switch (IoControlCode)
    {
        case IOCTL_SOCKET_BIND:
            status = VIOSockBind(Request);
            break;
        case IOCTL_SOCKET_CONNECT:
            status = VIOSockConnect(Request);
            break;
        case IOCTL_SOCKET_RECV:
            status = VIOSockRecvBuffered(Request);
            break;
        case IOCTL_SOCKET_RECV_EX:
            status = VIOSockRecvEx(Request);
            break;
        case IOCTL_SOCKET_SEND:
            status = VIOSockSendBuffered(Request);
            break;
        case IOCTL_SOCKET_SEND_EX:
            status = VIOSockSendEx(Request);
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

_Requires_lock_not_held_(pSocket->StateLock) NTSTATUS VIOSockStateValidate(PSOCKET_CONTEXT pSocket, BOOLEAN bTx)
{
    NTSTATUS status;
    BOOLEAN bIsShutdown, bIsPeerShutdown;

    WdfSpinLockAcquire(pSocket->StateLock);

    if (bTx)
    {

        bIsShutdown = pSocket->Shutdown & VIRTIO_VSOCK_SHUTDOWN_SEND;
        bIsPeerShutdown = pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_RCV;
    }
    else
    {
        bIsShutdown = pSocket->Shutdown & VIRTIO_VSOCK_SHUTDOWN_RCV;
        bIsPeerShutdown = pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_SEND;
    }

    if (bIsShutdown)
    {
        status = STATUS_LOCAL_DISCONNECT;
    }
    else if (VIOSockIsFlag(pSocket, SOCK_RESET))
    {
        // Abortive close (peer RST / transport reset): both directions fail with
        // WSAECONNRESET, ahead of the PeerShutdown closed-marker that would
        // otherwise read as a graceful peer shutdown.
        status = STATUS_CONNECTION_RESET;
    }
    else if (bIsPeerShutdown)
    {
        status = STATUS_REMOTE_DISCONNECT;
    }
    else if (VIOSockStateGet(pSocket) == VIOSOCK_STATE_CLOSING)
    {
        status = STATUS_CONNECTION_RESET;
    }
    else if (VIOSockStateGet(pSocket) != VIOSOCK_STATE_CONNECTED)
    {
        status = STATUS_CONNECTION_INVALID;
    }
    else
    {
        status = STATUS_SUCCESS;
    }

    WdfSpinLockRelease(pSocket->StateLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}

WDFFILEOBJECT
VIOSockGetSocketFromHandle(IN PDEVICE_CONTEXT pContext,
                           IN ULONGLONG uSocket,
                           IN BOOLEAN bIs32BitProcess,
                           IN KPROCESSOR_MODE AccessMode)
{
    NTSTATUS status;
    PFILE_OBJECT pFileObj;
    HANDLE hSocket;

#ifdef _WIN64
    if (bIs32BitProcess)
    {
        hSocket = Handle32ToHandle((void *POINTER_32)(ULONG)uSocket);
    }
    else
#endif //_WIN64
    {
        hSocket = (HANDLE)uSocket;
    }

    // hSocket may come from an IOCTL input buffer (UserMode) or from an in-driver
    // caller (KernelMode); the caller passes its requestor mode so the per-process
    // handle-table lookup + access check is done against the right domain. Driver
    // Verifier catches KernelMode dereferences of user-scoped HANDLEs as 0xC4/0xF6.
    // Request no specific access — a socket file object grants only SYNCHRONIZE
    // to its user opener, so STANDARD_RIGHTS_REQUIRED under UserMode is refused.
    // The lookup itself is what we need; we do not use the reference for any
    // access-checked operation.
    status = ObReferenceObjectByHandle(hSocket, 0, *IoFileObjectType, AccessMode, (PVOID)&pFileObj, NULL);

    if (NT_SUCCESS(status))
    {
        PSOCKET_CONTEXT pSocket = VIOSockConnectedFindByFileLockedReferenced(pContext, pFileObj);
        if (!pSocket)
        {
            pSocket = VIOSockBoundFindByFileLockedReferenced(pContext, pFileObj);
        }

        ObDereferenceObject(pFileObj);

        if (pSocket)
        {
            return pSocket->ThisSocket;
        }
    }

    return WDF_NO_HANDLE;
}

// Drop a listener's stale wire pends on a transport reset (OP_REQUEST predates
// it). Loopback pends and post-reset wire pends (current generation) survive.
_Requires_lock_not_held_(pListenSocket->RxLock) static VOID VIOSockAcceptDrainWire(IN PSOCKET_CONTEXT pListenSocket)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pListenSocket);
    LIST_ENTRY drainList;
    PLIST_ENTRY pEntry;

    InitializeListHead(&drainList);

    WdfSpinLockAcquire(pListenSocket->RxLock);
    for (pEntry = pListenSocket->AcceptList.Flink; pEntry != &pListenSocket->AcceptList;)
    {
        PVIOSOCK_ACCEPT_ENTRY pAccept = CONTAINING_RECORD(pEntry, VIOSOCK_ACCEPT_ENTRY, ListEntry);

        pEntry = pEntry->Flink;
        if (!VIOSockIsLocalCid(pAccept->dst_cid, pContext->Config.guest_cid) &&
            pAccept->Generation != pContext->ResetGeneration)
        {
            RemoveEntryList(&pAccept->ListEntry);
            InsertTailList(&drainList, &pAccept->ListEntry);
            InterlockedDecrement(&pListenSocket->AcceptPended);
        }
    }
    WdfSpinLockRelease(pListenSocket->RxLock);

    while (!IsListEmpty(&drainList))
    {
        PVIOSOCK_ACCEPT_ENTRY pAccept = CONTAINING_RECORD(RemoveHeadList(&drainList), VIOSOCK_ACCEPT_ENTRY, ListEntry);
        WdfObjectDelete(pAccept->Memory);
    }
}

VOID VIOSockHandleTransportReset(IN PDEVICE_CONTEXT pContext)
{
    LIST_ENTRY resetList;  // CONNECTED, removed from ConnectedList
    LIST_ENTRY listenList; // LISTEN, left in BoundList
    ULONG i;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    InitializeListHead(&resetList);
    InitializeListHead(&listenList);

    WdfSpinLockAcquire(pContext->ConnectedLock);
    // Bump the generation under the lock: a connect/accept whose VIOSockConnectedAdd
    // has not yet taken this lock will see the newer value and fail, instead of
    // becoming a lost CONNECTED socket this drain can no longer see.
    ++pContext->ResetGeneration;
    for (i = 0; i < WdfCollectionGetCount(pContext->ConnectedList);)
    {
        WDFFILEOBJECT Socket = WdfCollectionGetItem(pContext->ConnectedList, i);
        PSOCKET_CONTEXT pCurrentSocket = GetSocketContext(Socket);

        if (IsLoopbackSocket(pCurrentSocket))
        {
            ++i;
            continue;
        }

        // Remove now (a late Rx mustn't match a torn-down socket); don't advance
        // i, the removed entry's successor slides into slot i.
        VIOSockReferenceSocketObject(pCurrentSocket);
        WdfCollectionRemove(pContext->ConnectedList, Socket);
        InsertTailList(&resetList, &pCurrentSocket->ResetLink);
    }
    WdfSpinLockRelease(pContext->ConnectedLock);

    // Model as a peer RST: SOCK_RESET makes recv/send report WSAECONNRESET (via
    // VIOSockStateValidate), plus the generic PeerShutdown closed-marker the
    // readiness predicate keys off.
    while (!IsListEmpty(&resetList))
    {
        PSOCKET_CONTEXT pCurrentSocket = CONTAINING_RECORD(RemoveHeadList(&resetList), SOCKET_CONTEXT, ResetLink);

        BOOLEAN bWasConnected;

        WdfSpinLockAcquire(pCurrentSocket->StateLock);
        bWasConnected = (VIOSockStateSet(pCurrentSocket, VIOSOCK_STATE_CLOSE) == VIOSOCK_STATE_CONNECTED);
        if (bWasConnected)
        {
            VIOSockSetFlag(pCurrentSocket, SOCK_RESET);
            pCurrentSocket->PeerShutdown = VIRTIO_VSOCK_SHUTDOWN_MASK;
            VIOSockEventSetBit(pCurrentSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
        }
        WdfSpinLockRelease(pCurrentSocket->StateLock);

        // Drain parked blocking recvs: PeerShutdown is set, so they complete with
        // EOF now instead of hanging until the socket is closed.
        if (bWasConnected)
        {
            VIOSockReadProcessQueue(pCurrentSocket);
        }

        VIOSockDereferenceSocketObject(pCurrentSocket);
    }

    // CONNECTING and LISTEN sockets live in BoundList; reference but do not remove
    // them - their port binding must survive the reset.
    WdfSpinLockAcquire(pContext->BoundLock);
    for (i = 0; i < WdfCollectionGetCount(pContext->BoundList); ++i)
    {
        WDFFILEOBJECT Socket = WdfCollectionGetItem(pContext->BoundList, i);
        PSOCKET_CONTEXT pCurrentSocket = GetSocketContext(Socket);

        if (IsLoopbackSocket(pCurrentSocket))
        {
            continue;
        }

        switch (VIOSockStateGet(pCurrentSocket))
        {
            case VIOSOCK_STATE_CONNECTING:
                VIOSockReferenceSocketObject(pCurrentSocket);
                InsertTailList(&resetList, &pCurrentSocket->ResetLink);
                break;
            case VIOSOCK_STATE_LISTEN:
                VIOSockReferenceSocketObject(pCurrentSocket);
                InsertTailList(&listenList, &pCurrentSocket->ResetLink);
                break;
            default:
                break;
        }
    }
    WdfSpinLockRelease(pContext->BoundLock);

    // Fail each in-flight connect (FD_CONNECT + complete the pended IRP). The
    // RxLock pended-request slot arbitrates the race with a concurrent Rx.
    while (!IsListEmpty(&resetList))
    {
        PSOCKET_CONTEXT pCurrentSocket = CONTAINING_RECORD(RemoveHeadList(&resetList), SOCKET_CONTEXT, ResetLink);
        WDFREQUEST PendedRequest = WDF_NO_HANDLE;

        VIOSockPendedRequestGetLocked(pCurrentSocket, &PendedRequest);

        WdfSpinLockAcquire(pCurrentSocket->StateLock);
        if (VIOSockStateGet(pCurrentSocket) == VIOSOCK_STATE_CONNECTING)
        {
            VIOSockStateSet(pCurrentSocket, VIOSOCK_STATE_CLOSE);
            VIOSockEventSetBit(pCurrentSocket, FD_CONNECT_BIT, STATUS_CONNECTION_RESET);
        }
        WdfSpinLockRelease(pCurrentSocket->StateLock);

        if (PendedRequest != WDF_NO_HANDLE)
        {
            WdfRequestComplete(PendedRequest, STATUS_CONNECTION_RESET);
        }

        VIOSockDereferenceSocketObject(pCurrentSocket);
    }

    // Drop each listener's stale wire pending connections.
    while (!IsListEmpty(&listenList))
    {
        PSOCKET_CONTEXT pCurrentSocket = CONTAINING_RECORD(RemoveHeadList(&listenList), SOCKET_CONTEXT, ResetLink);

        VIOSockAcceptDrainWire(pCurrentSocket);
        VIOSockDereferenceSocketObject(pCurrentSocket);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

VOID VIOSockAddressListChange(IN PDEVICE_CONTEXT pContext)
{
    WDFREQUEST Request;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    // The local address list (the guest CID) changed - complete every pended
    // SIO_ADDRESS_LIST_CHANGE notification. Runs at DISPATCH_LEVEL from the event
    // virtqueue path, so it must not be paged.
    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(pContext->AddrChangeQueue, &Request)))
    {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
}
