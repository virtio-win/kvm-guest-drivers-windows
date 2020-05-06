/*
 * Loopback socket functions
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
#include "Loopback.tmh"
#endif


 //SRxLock-
static
NTSTATUS
VIOSockLoopbackAcceptEnqueue(
    IN PSOCKET_CONTEXT  pListenSocket,
    IN PSOCKET_CONTEXT  pConnectSocket
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

            pAcceptSocket->dst_cid = pContext->Config.guest_cid;
            pAcceptSocket->dst_port = pConnectSocket->src_port;
            pAcceptSocket->peer_buf_alloc = pConnectSocket->buf_alloc;
            pAcceptSocket->peer_fwd_cnt = pConnectSocket->fwd_cnt;

            //link accepted socket to connecting one
            pAcceptSocket->LoopbackSocket = pConnectSocket->ThisSocket;
            WdfObjectReference(pAcceptSocket->LoopbackSocket);
            VIOSockSetFlag(pAcceptSocket, SOCK_LOOPBACK);

            VIOSockStateSet(pAcceptSocket, VIOSOCK_STATE_CONNECTED);
            VIOSockSendResponse(pAcceptSocket);
            WdfRequestComplete(PendedRequest, STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }
    }

    status = WdfMemoryCreateFromLookaside(pContext->AcceptMemoryList, &Memory);

    if (NT_SUCCESS(status))
    {
        PVIOSOCK_ACCEPT_ENTRY pAccept = WdfMemoryGetBuffer(Memory, NULL);

        pAccept->Memory = Memory;
        pAccept->ConnectSocket = pConnectSocket->ThisSocket;
        WdfObjectReference(pAccept->ConnectSocket);

        pAccept->dst_cid = pContext->Config.guest_cid;
        pAccept->dst_port = pConnectSocket->src_port;
        pAccept->peer_buf_alloc = pConnectSocket->buf_alloc;
        pAccept->peer_fwd_cnt = pConnectSocket->fwd_cnt;

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

BOOLEAN
VIOSockLoopbackAcceptDequeue(
    IN PSOCKET_CONTEXT pAcceptSocket,
    IN PVIOSOCK_ACCEPT_ENTRY pAcceptEntry
)
{
    PSOCKET_CONTEXT pConnectSocket = GetSocketContext(pAcceptEntry->ConnectSocket);
    BOOLEAN bRes = TRUE;

    if (pConnectSocket->State == VIOSOCK_STATE_CONNECTING)
    {
        //link accepted socket to connecting one
        pAcceptSocket->LoopbackSocket = pAcceptEntry->ConnectSocket;//referenced on enqueue
        VIOSockSetFlag(pAcceptSocket, SOCK_LOOPBACK);
    }
    else
    {
        ASSERT(FALSE);
        //skip accept entry
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);
        WdfObjectDereference(pAcceptEntry->ConnectSocket);
        bRes = FALSE;
    }
    return bRes;
}

//////////////////////////////////////////////////////////////////////////
//TxLock-
__inline
ULONG32
VIOSockTxGetCreditLocked(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32 uCredit

)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    ULONG32 uRet;

    WdfSpinLockAcquire(pContext->TxLock);
    uRet = VIOSockTxGetCredit(pSocket, uCredit);
    WdfSpinLockRelease(pContext->TxLock);
    return uRet;
}

//TxLock-
__inline
VOID
VIOSockTxPutCreditLocked(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32 uCredit

)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);

    WdfSpinLockAcquire(pContext->TxLock);
    VIOSockTxPutCredit(pSocket, uCredit);
    WdfSpinLockRelease(pContext->TxLock);
}

__inline
LONG
VIOSockLoopbackTxSpaceUpdate(
    IN PSOCKET_CONTEXT pDstSocket,
    IN PSOCKET_CONTEXT pSrcSocket
)
{
    pDstSocket->peer_buf_alloc = pSrcSocket->buf_alloc;
    pDstSocket->peer_fwd_cnt = pSrcSocket->fwd_cnt;
    return VIOSockTxHasSpace(pDstSocket);
}

static
NTSTATUS
VIOSockLoopbackConnect(
    PSOCKET_CONTEXT pSocket
)
{
    NTSTATUS        status;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PSOCKET_CONTEXT pListenSocket = VIOSockBoundFindByPort(pContext, pSocket->dst_port);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    ASSERT(pSocket->State == VIOSOCK_STATE_CONNECTING);

    if (pListenSocket && VIOSockStateGet(pListenSocket) == VIOSOCK_STATE_LISTEN)
    {
        //TODO: Increase buf_alloc for loopback?
        status = VIOSockLoopbackAcceptEnqueue(pListenSocket, pSocket);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockLoopbackAcceptEnqueue failed: 0x%x\n", status);
        }
    }
    else
        status = STATUS_CONNECTION_RESET;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return status;
}

static
NTSTATUS
VIOSockLoopbackHandleConnecting(
    IN PSOCKET_CONTEXT  pDestSocket,
    IN PSOCKET_CONTEXT  pSrcSocket,
    IN VIRTIO_VSOCK_OP  Op,
    IN BOOLEAN          bTxHasSpace
)
{
    WDFREQUEST  PendedRequest;
    NTSTATUS    status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = VIOSockPendedRequestGetLocked(pDestSocket, &PendedRequest);

    if (NT_SUCCESS(status))
    {
        if (PendedRequest == WDF_NO_HANDLE &&
            !VIOSockIsFlag(pDestSocket, SOCK_NON_BLOCK))
        {
            status = STATUS_CANCELLED;
        }
        else
        {
            switch (Op)
            {
            case VIRTIO_VSOCK_OP_RESPONSE:
                ASSERT(pSrcSocket);
                //link connecting socket to accepted one
                pDestSocket->LoopbackSocket = pSrcSocket->ThisSocket;
                WdfObjectReference(pDestSocket->LoopbackSocket);

                VIOSockStateSet(pDestSocket, VIOSOCK_STATE_CONNECTED);
                VIOSockEventSetBit(pDestSocket, FD_CONNECT_BIT, STATUS_SUCCESS);
                if (bTxHasSpace)
                    VIOSockEventSetBit(pDestSocket, FD_WRITE_BIT, STATUS_SUCCESS);
                status = STATUS_SUCCESS;
                break;
            case VIRTIO_VSOCK_OP_INVALID:
                if (PendedRequest != WDF_NO_HANDLE)
                {
                    status = VIOSockPendedRequestSetLocked(pDestSocket, PendedRequest);
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
        VIOSockEventSetBit(pDestSocket, FD_CONNECT_BIT, status);
        VIOSockStateSet(pDestSocket, VIOSOCK_STATE_CLOSE);
        if (Op != VIRTIO_VSOCK_OP_RST)
            VIOSockSendReset(pDestSocket, FALSE);
    }

    if (PendedRequest)
        WdfRequestComplete(PendedRequest, status);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return status;
}

static
NTSTATUS
VIOSockLoopbackHandleConnected(
    IN PSOCKET_CONTEXT  pDestSocket,
    IN VIRTIO_VSOCK_OP  Op,
    IN ULONG32          Flags OPTIONAL,
    IN WDFREQUEST       Request OPTIONAL,
    IN ULONG            Length OPTIONAL,
    IN BOOLEAN          bTxHasSpace
)
{
    NTSTATUS    status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    switch (Op)
    {
    case VIRTIO_VSOCK_OP_RW:
        status = VIOSockRxRequestEnqueueCb(pDestSocket, Request, Length);
        if (NT_SUCCESS(status))
        {
            VIOSockEventSetBit(pDestSocket, FD_READ_BIT, STATUS_SUCCESS);
            VIOSockReadDequeueCb(pDestSocket, NULL);
        }
        break;//TODO: Remove break?
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
        if (bTxHasSpace)
        {
            VIOSockEventSetBit(pDestSocket, FD_WRITE_BIT, STATUS_SUCCESS);
        }
        break;
    case VIRTIO_VSOCK_OP_SHUTDOWN:
        if (VIOSockShutdownFromPeer(pDestSocket,
            Flags & VIRTIO_VSOCK_SHUTDOWN_MASK))
        {
            VIOSockSendReset(pDestSocket, FALSE);
        }
        break;
    case VIRTIO_VSOCK_OP_RST:
        VIOSockStateSet(pDestSocket, VIOSOCK_STATE_CLOSING);
        VIOSockShutdownFromPeer(pDestSocket, VIRTIO_VSOCK_SHUTDOWN_MASK);
        VIOSockEventSetBit(pDestSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
        break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return status;
}

static
VOID
VIOSockLoopbackHandleDisconnecting(
    IN PSOCKET_CONTEXT  pDestSocket,
    IN VIRTIO_VSOCK_OP  Op
)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (Op == VIRTIO_VSOCK_OP_RST)
    {
        VIOSockStateSet(pDestSocket, VIOSOCK_STATE_CLOSE);
        //         if (pDestSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_MASK != VIRTIO_VSOCK_SHUTDOWN_MASK)
        //         {
        //             VIOSockEventSetBit(pDestSocket, FD_CLOSE_BIT, STATUS_CONNECTION_RESET);
        //         }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VIOSockLoopbackTxEnqueue(
    IN PSOCKET_CONTEXT  pSocket,
    IN VIRTIO_VSOCK_OP  Op,
    IN ULONG32          Flags OPTIONAL,
    IN WDFREQUEST       Request OPTIONAL,
    IN ULONG            Length OPTIONAL
)
{
    NTSTATUS            status;
    PDEVICE_CONTEXT     pContext = GetDeviceContextFromSocket(pSocket);
    PSOCKET_CONTEXT     pLoopbackSocket = (pSocket->LoopbackSocket != WDF_NO_HANDLE) ?
        GetSocketContext(pSocket->LoopbackSocket) : NULL;
    ULONG               uCredit;
    BOOLEAN             bTxHasSpace;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s\n", __FUNCTION__);

    if (Request != WDF_NO_HANDLE)
    {
        ASSERT(Length);
        status = VIOSockTxValidateSocketState(pSocket);
        if (!NT_SUCCESS(status))
            return status;
    }

    uCredit = VIOSockTxGetCreditLocked(pSocket, Length);
    if (Length && !uCredit)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (!pLoopbackSocket)
    {
        //only connect request allowed without LoopbackSocket
        if (Op == VIRTIO_VSOCK_OP_REQUEST)
        {
            status = VIOSockLoopbackConnect(pSocket);
        }
        else
        {
            ASSERT(FALSE);
            status = STATUS_UNSUCCESSFUL;
        }
        return status;
    }

    pSocket->last_fwd_cnt = pSocket->fwd_cnt;
    bTxHasSpace = !!VIOSockLoopbackTxSpaceUpdate(pLoopbackSocket, pSocket);

    switch (pLoopbackSocket->State)
    {
    case VIOSOCK_STATE_CONNECTING:
        status = VIOSockLoopbackHandleConnecting(pLoopbackSocket, pSocket, Op, bTxHasSpace);
        break;

    case VIOSOCK_STATE_CONNECTED:
        status = VIOSockLoopbackHandleConnected(pLoopbackSocket, Op, Flags, Request, uCredit, bTxHasSpace);
        break;

    case VIOSOCK_STATE_CLOSING:
        VIOSockLoopbackHandleDisconnecting(pLoopbackSocket, Op);
        status = STATUS_SUCCESS;
        break;
    default:
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid socket state for Loopback Rx command\n");
        status = STATUS_CONNECTION_INVALID;
    }

    if (!NT_SUCCESS(status))
    {
        VIOSockTxPutCreditLocked(pSocket, uCredit);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
    return status;
}
