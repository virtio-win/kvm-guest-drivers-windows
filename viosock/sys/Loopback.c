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

_Requires_lock_not_held_(pListenSocket->RxLock) static NTSTATUS VIOSockLoopbackAcceptEnqueue(IN PSOCKET_CONTEXT pListenSocket,
                                                                                             IN PSOCKET_CONTEXT pConnectSocket)
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

    // accepted list is empty
    if (lAcceptPended == 1)
    {
        WDFREQUEST PendedRequest;

        VIOSockPendedRequestGetLocked(pListenSocket, &PendedRequest);
        if (PendedRequest)
        {
            PSOCKET_CONTEXT pAcceptSocket = GetSocketContextFromRequest(PendedRequest);

            InterlockedDecrement(&pListenSocket->AcceptPended);

            pAcceptSocket->dst_cid = pContext->Config.guest_cid;
            pAcceptSocket->dst_port = pConnectSocket->src_port;
            pAcceptSocket->peer_buf_alloc = pConnectSocket->buf_alloc;
            pAcceptSocket->peer_fwd_cnt = pConnectSocket->fwd_cnt;

            // link accepted socket to connecting one
            pAcceptSocket->LoopbackSocket = pConnectSocket->ThisSocket;
            VioSockReference(pAcceptSocket->LoopbackSocket);
            VIOSockSetFlag(pAcceptSocket, SOCK_LOOPBACK);

            VIOSockAcceptInitSocket(pAcceptSocket, pListenSocket);

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
        VioSockReference(pAccept->ConnectSocket);

        pAccept->dst_cid = pContext->Config.guest_cid;
        pAccept->dst_port = pConnectSocket->src_port;
        pAccept->peer_buf_alloc = pConnectSocket->buf_alloc;
        pAccept->peer_fwd_cnt = pConnectSocket->fwd_cnt;

        WdfSpinLockAcquire(pListenSocket->RxLock);
        InsertTailList(&pListenSocket->AcceptList, &pAccept->ListEntry);
        WdfSpinLockRelease(pListenSocket->RxLock);

        VIOSockEventSetBitLocked(pListenSocket, FD_ACCEPT_BIT, STATUS_SUCCESS);
        status = STATUS_PENDING;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "WdfMemoryCreateFromLookaside failed: 0x%x\n", status);
        InterlockedDecrement(&pListenSocket->AcceptPended);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);

    return status;
}

BOOLEAN
VIOSockLoopbackAcceptDequeue(IN PSOCKET_CONTEXT pAcceptSocket, IN PVIOSOCK_ACCEPT_ENTRY pAcceptEntry)
{
    PSOCKET_CONTEXT pConnectSocket = GetSocketContext(pAcceptEntry->ConnectSocket);
    BOOLEAN bRes = TRUE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (pConnectSocket->State == VIOSOCK_STATE_CONNECTING)
    {
        // link accepted socket to connecting one
        pAcceptSocket->LoopbackSocket = pAcceptEntry->ConnectSocket; // referenced on enqueue
        VIOSockSetFlag(pAcceptSocket, SOCK_LOOPBACK);
    }
    else
    {
        ASSERT(FALSE);
        // skip accept entry
        VioSockDereference(pAcceptEntry->ConnectSocket);
        bRes = FALSE;
    }

    return bRes;
}

//////////////////////////////////////////////////////////////////////////
static NTSTATUS VIOSockLoopbackConnect(PSOCKET_CONTEXT pSocket)
{
    NTSTATUS status;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PSOCKET_CONTEXT pListenSocket = VIOSockBoundFindByPortLocked(pContext, pSocket->dst_port);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    ASSERT(VIOSockStateGet(pSocket) == VIOSOCK_STATE_CONNECTING);

    if (pListenSocket)
    {
        if (VIOSockStateGet(pListenSocket) == VIOSOCK_STATE_LISTEN)
        {
            // TODO: Increase buf_alloc for loopback?
            status = VIOSockLoopbackAcceptEnqueue(pListenSocket, pSocket);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockLoopbackAcceptEnqueue failed: 0x%x\n", status);
            }
        }
        else
        {
            status = STATUS_CONNECTION_RESET;
        }
    }
    else
    {
        status = STATUS_CONNECTION_RESET;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

_Requires_lock_not_held_(pDestSocket->StateLock) static NTSTATUS VIOSockLoopbackHandleConnecting(IN PSOCKET_CONTEXT pDestSocket,
                                                                                                 IN PVIOSOCK_TX_ENTRY pTxEntry,
                                                                                                 IN BOOLEAN bTxHasSpace)
{
    WDFREQUEST PendedRequest;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    status = VIOSockPendedRequestGetLocked(pDestSocket, &PendedRequest);

    if (NT_SUCCESS(status))
    {
        if (PendedRequest == WDF_NO_HANDLE && !VIOSockIsNonBlocking(pDestSocket))
        {
            status = STATUS_CANCELLED;
        }
        else
        {
            BOOLEAN bSetEvent = FALSE;

            switch (pTxEntry->op)
            {
                case VIRTIO_VSOCK_OP_RESPONSE:
                    // link connecting socket to accepted one
                    pDestSocket->LoopbackSocket = pTxEntry->Socket;
                    VioSockReference(pDestSocket->LoopbackSocket);

                    ASSERT(bTxHasSpace);
                    WdfSpinLockAcquire(pDestSocket->StateLock);
                    VIOSockStateSet(pDestSocket, VIOSOCK_STATE_CONNECTED);
                    bSetEvent = VIOSockEventMarkBit(pDestSocket, FD_CONNECT_BIT, STATUS_SUCCESS);
                    if (bTxHasSpace)
                    {
                        bSetEvent |= VIOSockEventMarkBit(pDestSocket, FD_WRITE_BIT, STATUS_SUCCESS);
                    }
                    VIOSockEventNotify(pDestSocket, bSetEvent);
                    WdfSpinLockRelease(pDestSocket->StateLock);
                    status = STATUS_SUCCESS;
                    break;
                case VIRTIO_VSOCK_OP_INVALID:
                    if (PendedRequest != WDF_NO_HANDLE)
                    {
                        status = VIOSockPendedRequestSetResumeLocked(pDestSocket, PendedRequest);
                        if (NT_SUCCESS(status))
                        {
                            PendedRequest = WDF_NO_HANDLE;
                        }
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
        WdfSpinLockAcquire(pDestSocket->StateLock);
        VIOSockEventSetBit(pDestSocket, FD_CONNECT_BIT, status);
        VIOSockStateSet(pDestSocket, VIOSOCK_STATE_CLOSE);
        WdfSpinLockRelease(pDestSocket->StateLock);
        if (pTxEntry->op != VIRTIO_VSOCK_OP_RST)
        {
            VIOSockSendReset(pDestSocket, FALSE);
        }
    }

    if (PendedRequest)
    {
        WdfRequestComplete(PendedRequest, status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
    return status;
}

static VOID VIOSockLoopbackHandleConnected(IN PSOCKET_CONTEXT pDestSocket,
                                           IN PVIOSOCK_TX_ENTRY *ppTxEntry,
                                           IN BOOLEAN bTxHasSpace)
{
    PVIOSOCK_TX_ENTRY pTxEntry = *ppTxEntry;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    // FD_WRITE re-arm, mirroring VIOSockRxPktHandleConnected: evaluated for
    // every op — the peer piggybacks fwd_cnt on OP_RW, not just CREDIT_UPDATE.
    if (bTxHasSpace)
    {
        VIOSockRxProcessTxHasSpace(pDestSocket);
    }

    switch (pTxEntry->op)
    {
        case VIRTIO_VSOCK_OP_RW:
            // TRUE means the entry was queued as the peer's CB (FD_READ is
            // raised inside): drain the read queue into pending reads.
            if (VIOSockRxEnqueueTxEntry(pDestSocket, pTxEntry))
            {
                *ppTxEntry = NULL; // free Tx entry on read processing
                VIOSockReadProcessQueue(pDestSocket);
            }
            else
            {
                VIOSockTxPutCredit(pDestSocket, pTxEntry->len);
            }
            break;
        case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
            // TX-space accounting and FD_WRITE re-arm are handled above for all ops
            break;
        case VIRTIO_VSOCK_OP_SHUTDOWN:
            // The read-side EOF FD_CLOSE indication is raised inside
            // VIOSockShutdownFromPeer; here we only tear the circuit down.
            if (VIOSockShutdownFromPeer(pDestSocket, pTxEntry->flags & VIRTIO_VSOCK_SHUTDOWN_MASK) &&
                !VIOSockIsDone(pDestSocket) && !VIOSockRxHasData(pDestSocket))
            {
                VIOSockSendReset(pDestSocket, FALSE);
                VIOSockDoClose(pDestSocket);
            }
            break;
        case VIRTIO_VSOCK_OP_RST:
            {
                NTSTATUS closeStatus = ((pDestSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_MASK) == VIRTIO_VSOCK_SHUTDOWN_MASK) ? STATUS_SUCCESS
                                                                                                                                : STATUS_CONNECTION_RESET;
                if (closeStatus == STATUS_CONNECTION_RESET)
                {
                    // sends against the reset socket complete with
                    // WSAECONNRESET (mirrors the wire OP_RST arm in Rx.c)
                    VIOSockSetFlag(pDestSocket, SOCK_RESET);
                }
                VIOSockDoClose(pDestSocket);
                VIOSockEventSetBitLocked(pDestSocket, FD_CLOSE_BIT, closeStatus);
            }
            break;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
}

static VOID VIOSockLoopbackHandleDisconnecting(IN PSOCKET_CONTEXT pDestSocket, IN PVIOSOCK_TX_ENTRY pTxEntry)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (pTxEntry->op == VIRTIO_VSOCK_OP_RST)
    {
        VIOSockDoClose(pDestSocket);
    }
}

// Refresh the receiving peer's TX window from the entry's credit snapshot —
// the loopback twin of the wire receiver reading buf_alloc/fwd_cnt from the
// packet header. Live source-socket state must not be used here: the snapshot
// was stamped at entry creation (FIFO consistency included).
_Requires_lock_not_held_(pDstSocket->StateLock) __inline LONG VIOSockLoopbackTxSpaceUpdate(IN PSOCKET_CONTEXT pDstSocket,
                                                                                           IN PVIOSOCK_TX_ENTRY pTxEntry)
{
    LONG uSpace;

    WdfSpinLockAcquire(pDstSocket->StateLock);
    pDstSocket->peer_buf_alloc = pTxEntry->buf_alloc;
    pDstSocket->peer_fwd_cnt = pTxEntry->fwd_cnt;
    uSpace = VIOSockTxHasSpace(pDstSocket);
    WdfSpinLockRelease(pDstSocket->StateLock);

    return uSpace;
}

VOID VIOSockLoopbackProcessTxEntry(IN PVIOSOCK_TX_ENTRY pTxEntry)
{
    PSOCKET_CONTEXT pSocket = GetSocketContext(pTxEntry->Socket);
    PSOCKET_CONTEXT pLoopbackSocket = (pSocket->LoopbackSocket != WDF_NO_HANDLE) ? GetSocketContext(pSocket->LoopbackSocket)
                                                                                 : NULL;
    BOOLEAN bTxHasSpace;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "--> %s, %!Op!\n", __FUNCTION__, pTxEntry->op);

    if (!pLoopbackSocket)
    {
        // only a connect request is allowed without a loopback peer
        if (pTxEntry->op == VIRTIO_VSOCK_OP_REQUEST)
        {
            NTSTATUS status = VIOSockLoopbackConnect(pSocket);

            if (!NT_SUCCESS(status))
            {
                // No listener: emulate the peer RST the wire would deliver,
                // so connect() fails fast instead of waiting out the timeout.
                WDFREQUEST PendedRequest;

                WdfSpinLockAcquire(pSocket->StateLock);
                VIOSockEventSetBit(pSocket, FD_CONNECT_BIT, status);
                VIOSockStateSet(pSocket, VIOSOCK_STATE_CLOSE);
                WdfSpinLockRelease(pSocket->StateLock);

                VIOSockPendedRequestGetLocked(pSocket, &PendedRequest);
                if (PendedRequest != WDF_NO_HANDLE)
                {
                    WdfRequestComplete(PendedRequest, status);
                }
            }
        }
        else
        {
            ASSERT(FALSE);
            TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "no Loopback socket\n");
        }

        VIOSockTxFreeEntry(pTxEntry);
        return;
    }

    bTxHasSpace = !!VIOSockLoopbackTxSpaceUpdate(pLoopbackSocket, pTxEntry);

    switch (pLoopbackSocket->State)
    {
        case VIOSOCK_STATE_CONNECTING:
            VIOSockLoopbackHandleConnecting(pLoopbackSocket, pTxEntry, bTxHasSpace);
            break;

        case VIOSOCK_STATE_CONNECTED:
            VIOSockLoopbackHandleConnected(pLoopbackSocket, &pTxEntry, bTxHasSpace);
            break;

        case VIOSOCK_STATE_CLOSING:
            VIOSockLoopbackHandleDisconnecting(pLoopbackSocket, pTxEntry);
            break;

        default:
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid socket state for Loopback command\n");
    }

    if (pTxEntry)
    {
        VIOSockTxFreeEntry(pTxEntry);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}
