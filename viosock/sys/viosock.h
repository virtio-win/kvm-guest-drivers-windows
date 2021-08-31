/*
 * Main include file
 * This file contains various routines and globals
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
#if !defined(VIOSOCK_H)
#define VIOSOCK_H
#include "public.h"

#define VIOSOCK_DRIVER_MEMORY_TAG (ULONG)'cosV'

#pragma pack (push)
#pragma pack (1)

typedef enum _VIRTIO_VSOCK_OP {
    VIRTIO_VSOCK_OP_INVALID = 0,

    /* Connect operations */
    VIRTIO_VSOCK_OP_REQUEST = 1,
    VIRTIO_VSOCK_OP_RESPONSE = 2,
    VIRTIO_VSOCK_OP_RST = 3,
    VIRTIO_VSOCK_OP_SHUTDOWN = 4,

    /* To send payload */
    VIRTIO_VSOCK_OP_RW = 5,

    /* Tell the peer our credit info */
    VIRTIO_VSOCK_OP_CREDIT_UPDATE = 6,
    /* Request the peer to send the credit info to us */
    VIRTIO_VSOCK_OP_CREDIT_REQUEST = 7,
}VIRTIO_VSOCK_OP;

/* VIRTIO_VSOCK_OP_SHUTDOWN flags values */
enum virtio_vsock_shutdown {
    VIRTIO_VSOCK_SHUTDOWN_RCV = 1,
    VIRTIO_VSOCK_SHUTDOWN_SEND = 2,
    VIRTIO_VSOCK_SHUTDOWN_MASK = VIRTIO_VSOCK_SHUTDOWN_RCV | VIRTIO_VSOCK_SHUTDOWN_SEND,
};

typedef struct _VIRTIO_VSOCK_HDR {
    ULONG64 src_cid;
    ULONG64 dst_cid;
    ULONG32 src_port;
    ULONG32 dst_port;
    ULONG32 len;
    USHORT  type;
    USHORT  op;
    ULONG32 flags;
    ULONG32 buf_alloc;
    ULONG32 fwd_cnt;
}VIRTIO_VSOCK_HDR, *PVIRTIO_VSOCK_HDR;

typedef enum _VIRTIO_VSOCK_EVENT_ID {
    VIRTIO_VSOCK_EVENT_TRANSPORT_RESET = 0,
}VIRTIO_VSOCK_EVENT_ID;

typedef struct _VIRTIO_VSOCK_EVENT {
    ULONG32 id;
}VIRTIO_VSOCK_EVENT, *PVIRTIO_VSOCK_EVENT;

#pragma pack (pop)

typedef struct virtqueue VIOSOCK_VQ, *PVIOSOCK_VQ;
typedef struct VirtIOBufferDescriptor VIOSOCK_SG_DESC, *PVIOSOCK_SG_DESC;

#define VIOSOCK_VQ_RX  0
#define VIOSOCK_VQ_TX  1
#define VIOSOCK_VQ_EVT 2
#define VIOSOCK_VQ_MAX 3

#define VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE	(1024 * 4)
#define VIRTIO_VSOCK_MAX_PKT_BUF_SIZE		(1024 * 64)

#define VSOCK_CLOSE_TIMEOUT                 SEC_TO_NANO(8)
#define VSOCK_DEFAULT_CONNECT_TIMEOUT       SEC_TO_NANO(2)
#define VSOCK_DEFAULT_BUFFER_SIZE           (1024 * 256)
#define VSOCK_DEFAULT_BUFFER_MAX_SIZE       (1024 * 256)
#define VSOCK_DEFAULT_BUFFER_MIN_SIZE       128

#define VIRTIO_VSOCK_MAX_EVENTS 8

#define LAST_RESERVED_PORT  1023
#define MAX_PORT_RETRIES    24
//////////////////////////////////////////////////////////////////////////
#define VIOSOCK_TIMER_TOLERANCE MSEC_TO_NANO(50)
typedef struct _VIOSOCK_TIMER
{
    WDFTIMER    Timer;
    LONGLONG    StartTime; //ticks when timer started
    LONGLONG    Timeout;   //timeout in 100ns
    ULONG       StartRefs;
}VIOSOCK_TIMER,*PVIOSOCK_TIMER;

#define VIOSOCK_DEVICE_NAME L"\\Device\\Viosock"

typedef struct _DEVICE_CONTEXT {

    VIRTIO_WDF_DRIVER           VDevice;

    WDFDEVICE                   ThisDevice;

    //Recv packets
    WDFQUEUE                    ReadQueue;

    WDFSPINLOCK                 RxLock;
    _Guarded_by_(RxLock) PVIOSOCK_VQ                 RxVq;
    PVOID                       RxPktVA;        //contiguous array of VIOSOCK_RX_PKT
    PHYSICAL_ADDRESS            RxPktPA;
    _Guarded_by_(RxLock) SINGLE_LIST_ENTRY           RxPktList;      //postponed requests
    ULONG                       RxPktNum;
    ULONG                       RxCbBuffersNum;
    WDFLOOKASIDE                RxCbBufferMemoryList;
    _Guarded_by_(RxLock) SINGLE_LIST_ENTRY           RxCbBuffers;    //list or Rx buffers

    //Send packets
    WDFQUEUE                    WriteQueue;

    WDFSPINLOCK                 TxLock;
    _Guarded_by_(TxLock) PVIOSOCK_VQ                 TxVq;
    _Guarded_by_(TxLock) PVIRTIO_DMA_MEMORY_SLICED   TxPktSliced;
    LONG                       TxPktNum;       //Num of slices in TxPktSliced
    _Guarded_by_(TxLock) LONG                        TxQueuedReply;
    _Guarded_by_(TxLock) LIST_ENTRY                  TxList;
    _Guarded_by_(TxLock) VIOSOCK_TIMER               TxTimer;
    WDFLOOKASIDE                TxMemoryList;
    _Interlocked_ volatile LONG               TxEnqueued;
    _Interlocked_ volatile LONG               TxPktAllocated;

    WDFLOOKASIDE                AcceptMemoryList;

    //Events
    PVIOSOCK_VQ                 EvtVq;
    PVIRTIO_VSOCK_EVENT         EvtVA;
    PHYSICAL_ADDRESS            EvtPA;
    ULONG                       EvtRstOccured;

    WDFSPINLOCK                 BoundLock;
    _Guarded_by_(BoundLock) WDFCOLLECTION               BoundList;

    WDFSPINLOCK                 ConnectedLock;
    _Guarded_by_(ConnectedLock) WDFCOLLECTION               ConnectedList;

    WDFWAITLOCK                 SelectLock;
    _Guarded_by_(SelectLock) LIST_ENTRY                  SelectList;
    _Interlocked_ volatile LONG               SelectInProgress;
    WDFWORKITEM                 SelectWorkitem;
    _Guarded_by_(SelectLock) VIOSOCK_TIMER               SelectTimer;

    WDFQUEUE                    IoCtlQueue;

    WDFINTERRUPT                WdfInterrupt;

    _Interlocked_ volatile LONG             SocketId; //for debug

    VIRTIO_VSOCK_CONFIG         Config;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

typedef enum _VIOSOCK_STATE
{
    VIOSOCK_STATE_CLOSE = 0,
    VIOSOCK_STATE_CONNECTING = 1,
    VIOSOCK_STATE_CONNECTED = 2,
    VIOSOCK_STATE_CLOSING = 3,
    VIOSOCK_STATE_LISTEN = 4,
}VIOSOCK_STATE;

#define SOCK_CONTROL    0x01
#define SOCK_BOUND      0x02
#define SOCK_LINGER     0x04
#define SOCK_NON_BLOCK  0x08
#define SOCK_LOOPBACK   0x10

typedef struct _VIOSOCK_ACCEPT_ENTRY
{
    LIST_ENTRY      ListEntry;
    WDFMEMORY       Memory;
    WDFFILEOBJECT   ConnectSocket;
    ULONG32         dst_cid;
    ULONG32         dst_port;
    ULONG32         peer_buf_alloc;
    ULONG32         peer_fwd_cnt;
}VIOSOCK_ACCEPT_ENTRY, *PVIOSOCK_ACCEPT_ENTRY;

typedef struct _SOCKET_CONTEXT {

    WDFFILEOBJECT   ThisSocket;

    _Interlocked_ volatile LONG             Flags;
    LONG            SocketId; //for debug

    VIRTIO_VSOCK_TYPE  type;

    ULONG32         dst_cid;
    ULONG32         src_port;
    ULONG32         dst_port;

    WDFSPINLOCK     StateLock;
    _Interlocked_ volatile VIOSOCK_STATE    State;
    LONGLONG        ConnectTimeout;
    ULONG           SendTimeout;
    ULONG           RecvTimeout;
    ULONG32         BufferMinSize;
    ULONG32         BufferMaxSize;
    ULONG32         PeerShutdown;
    ULONG32         Shutdown;

    KEVENT          CloseEvent;

    _Guarded_by_(StateLock) PKEVENT         EventObject;
    _Guarded_by_(StateLock) ULONG           EventsMask;
    _Guarded_by_(StateLock) ULONG           Events;
    _Guarded_by_(StateLock) NTSTATUS        EventsStatus[FD_MAX_EVENTS];

    WDFSPINLOCK     RxLock;         //accept list lock for listen socket
    _Guarded_by_(RxLock) LIST_ENTRY      RxCbList;
    _Guarded_by_(RxLock) volatile ULONG           RxBytes;        //used bytes in rx buffer
    _Guarded_by_(RxLock) ULONG           RxBuffers;      //used rx buffers (for debug)

    WDFQUEUE        ReadQueue;
    _Guarded_by_(RxLock) VIOSOCK_TIMER   ReadTimer;

    _Guarded_by_(RxLock) WDFREQUEST      PendedRequest;
    VIOSOCK_TIMER        PendedTimer;

    _Guarded_by_(RxLock) LIST_ENTRY      AcceptList;
    LONG            Backlog;
    _Interlocked_ volatile LONG   AcceptPended;

    ULONG32         buf_alloc;
    _Guarded_by_(RxLock) ULONG32         fwd_cnt;
    ULONG32         last_fwd_cnt;

    _Guarded_by_(StateLock) ULONG32         peer_buf_alloc;
    _Guarded_by_(StateLock) ULONG32         peer_fwd_cnt;
    _Guarded_by_(StateLock) ULONG32         tx_cnt;

    USHORT          LingerTime;
    WDFFILEOBJECT   LoopbackSocket;

    volatile LONG   SelectRefs[FDSET_MAX];
} SOCKET_CONTEXT, *PSOCKET_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOCKET_CONTEXT, GetSocketContext);

#define VIOSockIsFlag(s,f) ((s)->Flags & (f))
#define VIOSockSetFlag(s,f) (InterlockedOr(&(s)->Flags, (f)) & (f))
#define VIOSockResetFlag(s,f) (InterlockedAnd(&(s)->Flags, ~(f)) & (f))

#define VIOSockIsNonBlocking(s) VIOSockIsFlag((s), SOCK_NON_BLOCK)

#define GetSocketContextFromRequest(r) GetSocketContext(WdfRequestGetFileObject((r)))

#define GetDeviceContextFromRequest(r) GetDeviceContext(WdfFileObjectGetDevice(WdfRequestGetFileObject((r))))

#define GetDeviceContextFromSocket(s) GetDeviceContext(WdfFileObjectGetDevice((s)->ThisSocket))

#define IsControlRequest(r) VIOSockIsFlag(GetSocketContextFromRequest(r), SOCK_CONTROL)

#define IsLoopbackSocket(s) (VIOSockIsFlag(s,SOCK_LOOPBACK))

//////////////////////////////////////////////////////////////////////////
//Device functions

EVT_WDF_DRIVER_DEVICE_ADD   VIOSockEvtDeviceAdd;

NTSTATUS
VIOSockInterruptInit(
    IN WDFDEVICE hDevice
);

//////////////////////////////////////////////////////////////////////////
//Socket functions
EVT_WDF_DEVICE_FILE_CREATE  VIOSockCreateStub;
EVT_WDF_FILE_CLOSE          VIOSockClose;

NTSTATUS
VIOSockDeviceControl(
    IN WDFREQUEST Request,
    IN ULONG      IoControlCode,
    IN OUT size_t *pLength
);

WDFFILEOBJECT
VIOSockGetSocketFromHandle(
    IN PDEVICE_CONTEXT pContext,
    IN ULONGLONG       uSocket,
    IN BOOLEAN         bIs32BitProcess
);

VOID
VIOSockHandleTransportReset(
    IN PDEVICE_CONTEXT pContext
);

NTSTATUS
VIOSockBoundListInit(
    IN WDFDEVICE hDevice
);

NTSTATUS
VIOSockBoundAdd(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32         svm_port
);

PSOCKET_CONTEXT
VIOSockBoundFindByPort(
    IN PDEVICE_CONTEXT pContext,
    IN ULONG32         ulSrcPort
);

PSOCKET_CONTEXT
VIOSockBoundFindByPortUnlocked(
    IN PDEVICE_CONTEXT pContext,
    IN ULONG32         ulSrcPort
);

PSOCKET_CONTEXT
VIOSockBoundFindByFile(
    IN PDEVICE_CONTEXT pContext,
    IN PFILE_OBJECT pFileObject
);

NTSTATUS
VIOSockConnectedListInit(
    IN WDFDEVICE hDevice
);

PSOCKET_CONTEXT
VIOSockConnectedFindByRxPkt(
    IN PDEVICE_CONTEXT      pContext,
    IN PVIRTIO_VSOCK_HDR    pPkt
);

_Requires_lock_held_(pSocket->StateLock)
VIOSOCK_STATE
VIOSockStateSet(
    IN PSOCKET_CONTEXT pSocket,
    IN VIOSOCK_STATE   NewState
);

_Requires_lock_not_held_(pSocket->StateLock)
VIOSOCK_STATE
VIOSockStateSetLocked(
    IN PSOCKET_CONTEXT pSocket,
    IN VIOSOCK_STATE   NewState
);

#define VIOSockStateGet(s) ((s)->State)

_Requires_lock_not_held_(pSocket->StateLock)
BOOLEAN
VIOSockShutdownFromPeer(
    PSOCKET_CONTEXT pSocket,
    ULONG uFlags
);

VOID
VIOSockDoClose(
    PSOCKET_CONTEXT pSocket
);

__inline
BOOLEAN
VIOSockIsDone(
    PSOCKET_CONTEXT pSocket
)
{
    return !!KeReadStateEvent(&pSocket->CloseEvent);
}

_Requires_lock_held_(pSocket->RxLock)
NTSTATUS
VIOSockPendedRequestSetEx(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST Request,
    IN LONGLONG Timeout,
    IN BOOLEAN Resume
);

#define VIOSockPendedRequestSet(s,r,t) VIOSockPendedRequestSetEx(s,r,t,FALSE)

_Requires_lock_not_held_(pSocket->RxLock)
__inline
NTSTATUS
VIOSockPendedRequestSetLocked(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST Request,
    IN LONGLONG Timeout
)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->RxLock);
    status = VIOSockPendedRequestSet(pSocket, Request, Timeout);
    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

#define VIOSockPendedRequestSetResume(s,r) VIOSockPendedRequestSetEx(s,r,0,TRUE)

_Requires_lock_not_held_(pSocket->RxLock)
__inline
NTSTATUS
VIOSockPendedRequestSetResumeLocked(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request
)
{
    NTSTATUS status;

    WdfSpinLockAcquire(pSocket->RxLock);
    status = VIOSockPendedRequestSetResume(pSocket, Request);
    WdfSpinLockRelease(pSocket->RxLock);

    return status;
}

_Requires_lock_held_(pSocket->RxLock)
NTSTATUS
VIOSockPendedRequestGet(
    IN PSOCKET_CONTEXT  pSocket,
    OUT WDFREQUEST      *Request
);

__inline
_Requires_lock_not_held_(pSocket->RxLock)
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

NTSTATUS
VIOSockAcceptInitSocket(
    PSOCKET_CONTEXT pAcceptSocket,
    PSOCKET_CONTEXT pListenSocket
);

_Requires_lock_not_held_(pListenSocket->RxLock)
NTSTATUS
VIOSockAcceptEnqueuePkt(
    IN PSOCKET_CONTEXT      pListenSocket,
    IN PVIRTIO_VSOCK_HDR    pPkt
);

_Requires_lock_not_held_(pListenSocket->RxLock)
VOID
VIOSockAcceptRemovePkt(
    IN PSOCKET_CONTEXT      pListenSocket,
    IN PVIRTIO_VSOCK_HDR    pPkt
);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
VIOSockSelectRun(
    IN PSOCKET_CONTEXT pSocket
);

/*
 * WinSock 2 extension -- bit values and indices for FD_XXX network events
 */
#define FD_READ_BIT      0
#define FD_READ          (1 << FD_READ_BIT)
#define FD_WRITE_BIT     1
#define FD_WRITE         (1 << FD_WRITE_BIT)
#define FD_OOB_BIT       2
#define FD_OOB           (1 << FD_OOB_BIT)
#define FD_ACCEPT_BIT    3
#define FD_ACCEPT        (1 << FD_ACCEPT_BIT)
#define FD_CONNECT_BIT   4
#define FD_CONNECT       (1 << FD_CONNECT_BIT)
#define FD_CLOSE_BIT     5
#define FD_CLOSE         (1 << FD_CLOSE_BIT)
#define FD_QOS_BIT       6
#define FD_QOS           (1 << FD_QOS_BIT)
#define FD_GROUP_QOS_BIT 7
#define FD_GROUP_QOS     (1 << FD_GROUP_QOS_BIT)
#define FD_ROUTING_INTERFACE_CHANGE_BIT 8
#define FD_ROUTING_INTERFACE_CHANGE     (1 << FD_ROUTING_INTERFACE_CHANGE_BIT)
#define FD_ADDRESS_LIST_CHANGE_BIT 9
#define FD_ADDRESS_LIST_CHANGE     (1 << FD_ADDRESS_LIST_CHANGE_BIT)

_Requires_lock_not_held_(pSocket->StateLock)
VOID
VIOSockEventSetBit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uSetBit,
    IN NTSTATUS Status
);

_Requires_lock_not_held_(pSocket->StateLock)
VOID
VIOSockEventSetBitLocked(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uSetBit,
    IN NTSTATUS Status
);

_Requires_lock_not_held_(pSocket->StateLock)
VOID
VIOSockEventClearBit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uClearBit
);

//////////////////////////////////////////////////////////////////////////
//Tx functions

NTSTATUS
VIOSockWriteQueueInit(
    IN WDFDEVICE hDevice
);

NTSTATUS
VIOSockTxVqInit(
    IN PDEVICE_CONTEXT pContext
);

_Requires_lock_not_held_(pContext->TxLock)
VOID
VIOSockTxVqCleanup(
    IN PDEVICE_CONTEXT pContext
);

_Requires_lock_not_held_(pContext->TxLock)
VOID
VIOSockTxVqProcess(
    IN PDEVICE_CONTEXT pContext
);

_Requires_lock_not_held_(pSocket->StateLock)
NTSTATUS
VIOSockStateValidate(
    PSOCKET_CONTEXT pSocket,
    BOOLEAN         bTx
);

NTSTATUS
VIOSockTxEnqueue(
    IN PSOCKET_CONTEXT  pSocket,
    IN VIRTIO_VSOCK_OP  Op,
    IN ULONG32          Flags OPTIONAL,
    IN BOOLEAN          Reply,
    IN WDFREQUEST       Request OPTIONAL
);

#define VIOSockSendCreditUpdate(s) VIOSockTxEnqueue(s, VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0, FALSE, WDF_NO_HANDLE)

#define VIOSockSendConnect(s) VIOSockTxEnqueue(s, VIRTIO_VSOCK_OP_REQUEST, 0, FALSE, WDF_NO_HANDLE)

#define VIOSockSendShutdown(s, f) VIOSockTxEnqueue(s, VIRTIO_VSOCK_OP_SHUTDOWN, f, FALSE, WDF_NO_HANDLE)

#define VIOSockSendWrite(s, rq) VIOSockTxEnqueue(s, VIRTIO_VSOCK_OP_RW, 0, FALSE, rq)

#define VIOSockSendReset(s, r) VIOSockTxEnqueue(s, VIRTIO_VSOCK_OP_RST, 0, r, WDF_NO_HANDLE)

#define VIOSockSendResponse(s) VIOSockTxEnqueue(s, VIRTIO_VSOCK_OP_RESPONSE, 0, TRUE, WDF_NO_HANDLE)

_Requires_lock_not_held_(pContext->TxLock)
NTSTATUS
VIOSockSendResetNoSock(
    IN PDEVICE_CONTEXT pContext,
    IN PVIRTIO_VSOCK_HDR pHeader
);

_Requires_lock_not_held_(pSocket->StateLock)
__inline
ULONG32
VIOSockTxGetCredit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32 uCredit

)
{
    ULONG32 uRet;

    WdfSpinLockAcquire(pSocket->StateLock);
    uRet = pSocket->peer_buf_alloc - (pSocket->tx_cnt - pSocket->peer_fwd_cnt);
    if (uRet > uCredit)
        uRet = uCredit;
    pSocket->tx_cnt += uRet;
    WdfSpinLockRelease(pSocket->StateLock);

    return uRet;
}

_Requires_lock_not_held_(pSocket->StateLock)
__inline
VOID
VIOSockTxPutCredit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32 uCredit

)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->tx_cnt -= uCredit;
    WdfSpinLockRelease(pSocket->StateLock);
}

_Requires_lock_held_(pSocket->StateLock)
__inline
LONG
VIOSockTxHasSpace(
    IN PSOCKET_CONTEXT pSocket
)
{
    LONG lBytes = (LONG)pSocket->peer_buf_alloc - (pSocket->tx_cnt - pSocket->peer_fwd_cnt);
    if (lBytes < 0)
        lBytes = 0;
    return lBytes;
}

_Requires_lock_not_held_(pSocket->StateLock)
__inline
LONG
VIOSockTxSpaceUpdate(
    IN PSOCKET_CONTEXT pSocket,
    IN PVIRTIO_VSOCK_HDR pPkt
)
{
    LONG uSpace;

    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->peer_buf_alloc = pPkt->buf_alloc;
    pSocket->peer_fwd_cnt = pPkt->fwd_cnt;
    uSpace = VIOSockTxHasSpace(pSocket);
    WdfSpinLockRelease(pSocket->StateLock);

    return uSpace;
}

__inline
BOOLEAN
VIOSockTxMoreReplies(
    IN PDEVICE_CONTEXT  pContext
)
{
    return pContext->TxQueuedReply < (LONG)pContext->RxPktNum;
}

_Requires_lock_not_held_(pContext->TxLock)
VOID
VIOSockTxCleanup(
    PDEVICE_CONTEXT pContext,
    WDFFILEOBJECT   Socket,
    NTSTATUS        Status
);

VOID
VIOSockWriteIoSuspend(
    IN PDEVICE_CONTEXT pContext
);

VOID
VIOSockWriteIoRestart(
    IN PDEVICE_CONTEXT pContext
);

//////////////////////////////////////////////////////////////////////////
//Rx functions

NTSTATUS
VIOSockRxVqInit(
    IN PDEVICE_CONTEXT pContext
);

_Requires_lock_not_held_(pContext->RxLock)
VOID
VIOSockRxVqCleanup(
    IN PDEVICE_CONTEXT pContext
);

_Requires_lock_not_held_(pSocket->StateLock)
__inline
VOID
VIOSockRxIncTxPkt(
    IN PSOCKET_CONTEXT pSocket,
    IN OUT PVIRTIO_VSOCK_HDR pPkt
)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->last_fwd_cnt = pSocket->fwd_cnt;
    pPkt->fwd_cnt = pSocket->fwd_cnt;
    pPkt->buf_alloc = pSocket->buf_alloc;
    WdfSpinLockRelease(pSocket->StateLock);
}

_Requires_lock_not_held_(pContext->RxLock)
VOID
VIOSockRxVqProcess(
    IN PDEVICE_CONTEXT pContext
);

_Requires_lock_not_held_(pSocket->RxLock)
NTSTATUS
VIOSockRxRequestEnqueueCb(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request,
    IN ULONG            Length
);

__inline
ULONG
VIOSockRxHasData(
    IN PSOCKET_CONTEXT pSocket
)
{
    return pSocket->RxBytes;
}

NTSTATUS
VIOSockReadQueueInit(
    IN WDFDEVICE hDevice
);

NTSTATUS
VIOSockReadSocketQueueInit(
    IN PSOCKET_CONTEXT pSocket
);

_Requires_lock_not_held_(pSocket->RxLock)
BOOLEAN
VIOSockReadDequeueCb(
    IN PSOCKET_CONTEXT  pSocket
);

_Requires_lock_not_held_(pSocket->RxLock)
__inline
VIOSockReadProcessDequeueCb(
    IN PSOCKET_CONTEXT pSocket
)
{
    while (VIOSockReadDequeueCb(pSocket));
}

_Requires_lock_not_held_(pSocket->RxLock)
VOID
VIOSockReadCleanupCb(
    IN PSOCKET_CONTEXT pSocket
);

NTSTATUS
VIOSockReadWithFlags(
    IN WDFREQUEST Request
);

//////////////////////////////////////////////////////////////////////////
//Event functions
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
VIOSockEvtVqInit(
    IN PDEVICE_CONTEXT pContext
);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
VIOSockEvtVqCleanup(
    IN PDEVICE_CONTEXT pContext
);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
VIOSockEvtVqProcess(
    IN PDEVICE_CONTEXT pContext
);

//////////////////////////////////////////////////////////////////////////
BOOLEAN
VIOSockLoopbackAcceptDequeue(
    IN PSOCKET_CONTEXT pAcceptSocket,
    IN PVIOSOCK_ACCEPT_ENTRY pAcceptEntry
);

NTSTATUS
VIOSockLoopbackTxEnqueue(
    IN PSOCKET_CONTEXT  pSocket,
    IN VIRTIO_VSOCK_OP  Op,
    IN ULONG32          Flags OPTIONAL,
    IN WDFREQUEST       Request OPTIONAL,
    IN ULONG            Length OPTIONAL
);

//////////////////////////////////////////////////////////////////////////
__inline
NTSTATUS
VIOSockTimerCreate(
    IN PVIOSOCK_TIMER   pTimer,
    IN WDFOBJECT        ParentObject,
    IN PFN_WDF_TIMER    EvtTimerFunc
)
{
    WDF_OBJECT_ATTRIBUTES   Attributes;
    WDF_TIMER_CONFIG        timerConfig;

    pTimer->Timeout = 0;
    pTimer->StartTime = 0;
    pTimer->StartRefs = 0;

    WDF_TIMER_CONFIG_INIT(&timerConfig, EvtTimerFunc);

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = ParentObject;

    return WdfTimerCreate(&timerConfig, &Attributes, &pTimer->Timer);
}

VOID
VIOSockTimerStart(
    IN PVIOSOCK_TIMER   pTimer,
    IN LONGLONG         Timeout
);

__inline
VOID
VIOSockTimerSet(
    IN PVIOSOCK_TIMER   pTimer,
    IN LONGLONG         Timeout
)
{
    LARGE_INTEGER liTicks;

    if (!Timeout || Timeout == LONGLONG_MAX)
    {
        ASSERT(!pTimer->StartRefs);
        pTimer->StartTime = 0;
        pTimer->Timeout = 0;
        return;
    }

    ASSERT(Timeout > VIOSOCK_TIMER_TOLERANCE);
    if (Timeout <= VIOSOCK_TIMER_TOLERANCE)
        Timeout = VIOSOCK_TIMER_TOLERANCE + 1;

    KeQueryTickCount(&liTicks);

    pTimer->StartTime = liTicks.QuadPart;
    pTimer->Timeout = Timeout;
    WdfTimerStart(pTimer->Timer, -Timeout);
}

__inline
VOID
VIOSockTimerCancel(
    IN PVIOSOCK_TIMER pTimer
)
{
    WdfTimerStop(pTimer->Timer, FALSE);
    pTimer->Timeout = 0;
    pTimer->StartTime = 0;
}

__inline
VOID
VIOSockTimerDeref(
    IN PVIOSOCK_TIMER   pTimer,
    IN BOOLEAN          bStop
)
{
    ASSERT(pTimer->StartRefs);
    if (--pTimer->StartRefs == 0 && bStop)
        VIOSockTimerCancel(pTimer);
}

__inline
LONGLONG
VIOSockTimerPassed(
    IN PVIOSOCK_TIMER pTimer
)
{
    LARGE_INTEGER liTicks;

    KeQueryTickCount(&liTicks);

    return (liTicks.QuadPart - pTimer->StartTime) * KeQueryTimeIncrement();
}

__inline
BOOLEAN
VIOSockTimerSuspend(
    IN PVIOSOCK_TIMER pTimer
)
{
    if (WdfTimerStop(pTimer->Timer, FALSE))
    {
        return pTimer->Timeout > VIOSockTimerPassed(pTimer) + VIOSOCK_TIMER_TOLERANCE;
    }
    return TRUE;
}

__inline
BOOLEAN
VIOSockTimerResume(
    IN PVIOSOCK_TIMER pTimer
)
{
    LONGLONG llTimeout = VIOSockTimerPassed(pTimer);

    if (pTimer->Timeout > llTimeout + VIOSOCK_TIMER_TOLERANCE)
    {
        VIOSockTimerSet(pTimer, pTimer->Timeout - llTimeout);
        return TRUE;
    }
    return FALSE;
}

//////////////////////////////////////////////////////////////////////////

#endif /* VIOSOCK_H */
