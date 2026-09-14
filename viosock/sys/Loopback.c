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
    VIRTIO_VSOCK_HDR PktHdr = {0};

    // A loopback peer is always reported as VMADDR_CID_LOCAL, whether the
    // connector dialed CID 1 or the guest's own CID - Linux does the same: the
    // loopback transport stamps its own local CID into every packet it sends.
    PktHdr.src_cid = VMADDR_CID_LOCAL;
    PktHdr.src_port = pConnectSocket->src_port;
    PktHdr.buf_alloc = pConnectSocket->buf_alloc;
    PktHdr.fwd_cnt = pConnectSocket->fwd_cnt;

    return VIOSockAcceptEnqueuePkt(pListenSocket, &PktHdr, pConnectSocket->ThisSocket);
}

BOOLEAN
VIOSockLoopbackAcceptInitSocket(IN PSOCKET_CONTEXT pAcceptSocket, IN WDFFILEOBJECT ConnectSocket)
{
    PSOCKET_CONTEXT pConnectSocket = GetSocketContext(ConnectSocket);
    BOOLEAN bRes;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    // The state check and the cross-links must be installed atomically under the
    // connecting socket's StateLock: VIOSockCleanup moves the socket to CLOSE and
    // harvests LoopbackSocket under the same lock, so without it a concurrent
    // close could miss the link installed below - leaking the reference and
    // leaving the accepted socket half-open.
    WdfSpinLockAcquire(pConnectSocket->StateLock);
    if (VIOSockStateGet(pConnectSocket) == VIOSOCK_STATE_CONNECTING)
    {
        // link accepted and connecting sockets
        pAcceptSocket->LoopbackSocket = ConnectSocket; // referenced on enqueue
        VIOSockSetFlag(pAcceptSocket, SOCK_LOOPBACK);
        WdfObjectReference(pAcceptSocket->ThisSocket);
        pConnectSocket->LoopbackSocket = pAcceptSocket->ThisSocket;
        bRes = TRUE;
    }
    else
    {
        // The connecting socket closed while its entry sat in the backlog -
        // a legitimate race; skip the entry.
        bRes = FALSE;
    }
    WdfSpinLockRelease(pConnectSocket->StateLock);

    if (!bRes)
    {
        WdfObjectDereference(ConnectSocket);
    }

    return bRes;
}

//////////////////////////////////////////////////////////////////////////
static NTSTATUS VIOSockLoopbackConnect(PSOCKET_CONTEXT pSocket)
{
    NTSTATUS status;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    PSOCKET_CONTEXT pListenSocket = VIOSockBoundFindByPortLockedReferenced(pContext, pSocket->dst_port);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    ASSERT(VIOSockStateGet(pSocket) == VIOSOCK_STATE_CONNECTING);

    if (pListenSocket)
    {
        if (VIOSockStateGet(pListenSocket) == VIOSOCK_STATE_LISTEN)
        {
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

        WdfObjectDereference(pListenSocket->ThisSocket);
    }
    else
    {
        status = STATUS_CONNECTION_RESET;
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

static __inline VOID VIOSockTxEntryToPktHdr(IN PVIOSOCK_TX_ENTRY pTxEntry, OUT PVIRTIO_VSOCK_HDR pPktHdr)
{
    pPktHdr->op = pTxEntry->op;
    pPktHdr->buf_alloc = pTxEntry->buf_alloc;
    pPktHdr->fwd_cnt = pTxEntry->fwd_cnt;
}

static VOID VIOSockLoopbackProcessTxEntryInternal(IN PSOCKET_CONTEXT pLoopbackSocket, IN PVIOSOCK_TX_ENTRY pTxEntry)
{
    VIRTIO_VSOCK_HDR PktHdr;
    BOOLEAN bTxHasSpace;

    VIOSockTxEntryToPktHdr(pTxEntry, &PktHdr);

    bTxHasSpace = !!VIOSockTxSpaceUpdate(pLoopbackSocket, &PktHdr);

    switch (pLoopbackSocket->State)
    {
        case VIOSOCK_STATE_CONNECTING:
            VIOSockRxPktHandleConnecting(pLoopbackSocket, &PktHdr, bTxHasSpace);
            break;

        case VIOSOCK_STATE_CONNECTED:
            VIOSockLoopbackHandleConnected(pLoopbackSocket, &pTxEntry, bTxHasSpace);
            break;

        case VIOSOCK_STATE_CLOSING:
            VIOSockRxPktHandleDisconnecting(pLoopbackSocket, &PktHdr);
            break;

        default:
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid socket state for Loopback command\n");
    }

    if (pTxEntry)
    {
        VIOSockTxFreeEntry(pTxEntry);
    }
}

#define GetLoopbackSocketContext(s)                                                                                    \
    (((s)->LoopbackSocket != WDF_NO_HANDLE) ? GetSocketContext((s)->LoopbackSocket) : NULL)

VOID VIOSockLoopbackProcessTxEntry(IN PVIOSOCK_TX_ENTRY pTxEntry)
{
    PSOCKET_CONTEXT pSocket = GetSocketContext(pTxEntry->Socket);
    PSOCKET_CONTEXT pLoopbackSocket = GetLoopbackSocketContext(pSocket);

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

    VIOSockLoopbackProcessTxEntryInternal(pLoopbackSocket, pTxEntry);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

VOID VIOSockLoopbackSendReset(IN PSOCKET_CONTEXT pLoopbackSocket)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pLoopbackSocket);
    PVIOSOCK_TX_ENTRY pTxEntry;

    pTxEntry = VIOSockTxAllocateEntry(pContext, NULL, 0);
    if (pTxEntry)
    {
        pTxEntry->op = VIRTIO_VSOCK_OP_RST;
        pTxEntry->type = (USHORT)pLoopbackSocket->type;
        pTxEntry->dst_cid = pContext->Config.guest_cid;
        pTxEntry->dst_port = pLoopbackSocket->src_port;
        pTxEntry->src_port = pLoopbackSocket->dst_port;

        VIOSockLoopbackProcessTxEntryInternal(pLoopbackSocket, pTxEntry);
    }
}