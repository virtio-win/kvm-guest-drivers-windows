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
#include "..\inc\vio_sockets.h"

#define VIOSOCK_DRIVER_MEMORY_TAG (ULONG)'cosV'

#pragma pack(push)
#pragma pack(1)

typedef enum _VIRTIO_VSOCK_OP
{
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
} VIRTIO_VSOCK_OP;

/* VIRTIO_VSOCK_OP_SHUTDOWN flags values */
enum virtio_vsock_shutdown
{
    VIRTIO_VSOCK_SHUTDOWN_RCV = 1,
    VIRTIO_VSOCK_SHUTDOWN_SEND = 2,
    VIRTIO_VSOCK_SHUTDOWN_MASK = VIRTIO_VSOCK_SHUTDOWN_RCV | VIRTIO_VSOCK_SHUTDOWN_SEND,
};

typedef struct _VIRTIO_VSOCK_HDR
{
    ULONG64 src_cid;
    ULONG64 dst_cid;
    ULONG32 src_port;
    ULONG32 dst_port;
    ULONG32 len;
    USHORT type;
    USHORT op;
    ULONG32 flags;
    ULONG32 buf_alloc;
    ULONG32 fwd_cnt;
} VIRTIO_VSOCK_HDR, *PVIRTIO_VSOCK_HDR;

typedef enum _VIRTIO_VSOCK_EVENT_ID
{
    VIRTIO_VSOCK_EVENT_TRANSPORT_RESET = 0,
} VIRTIO_VSOCK_EVENT_ID;

typedef struct _VIRTIO_VSOCK_EVENT
{
    ULONG32 id;
} VIRTIO_VSOCK_EVENT, *PVIRTIO_VSOCK_EVENT;

#pragma pack(pop)

typedef struct virtqueue VIOSOCK_VQ, *PVIOSOCK_VQ;
typedef struct VirtIOBufferDescriptor VIOSOCK_SG_DESC, *PVIOSOCK_SG_DESC;

#define VIOSOCK_VQ_RX                    0
#define VIOSOCK_VQ_TX                    1
#define VIOSOCK_VQ_EVT                   2
#define VIOSOCK_VQ_MAX                   3

#define VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE (1024 * 4)
#define VIRTIO_VSOCK_MAX_PKT_BUF_SIZE    (1024 * 64)

#define VSOCK_CLOSE_TIMEOUT              SEC_TO_NANO(8)
#define VSOCK_DEFAULT_CONNECT_TIMEOUT    SEC_TO_NANO(2)
#define VSOCK_DEFAULT_BUFFER_SIZE        (1024 * 256)
#define VSOCK_DEFAULT_BUFFER_MAX_SIZE    (1024 * 256)
#define VSOCK_DEFAULT_BUFFER_MIN_SIZE    128

#define VIRTIO_VSOCK_MAX_EVENTS          8

#define LAST_RESERVED_PORT               1023
#define MAX_PORT_RETRIES                 24
//////////////////////////////////////////////////////////////////////////
#define VIOSOCK_TIMER_TOLERANCE          MSEC_TO_NANO(50)
#define VIOSOCK_TIMER_TOLERANCE_TICKS    llTimerToleranceTicks
#define VIOSOCK_TIMER_TIME_INCREMENT     ulTimeIncrement

extern ULONG ulTimeIncrement;
extern LONGLONG llTimerToleranceTicks;

// All fields of VIOSOCK_TIMER must be read and written under the caller's lock
// (e.g. WdfWaitLock or WdfSpinLock that guards the associated request list).
typedef struct _VIOSOCK_TIMER
{
    WDFTIMER Timer;
    LONGLONG Deadline; // absolute tick count of expiry
    ULONG StartRefs;   // stop timer if refs = 0
} VIOSOCK_TIMER, *PVIOSOCK_TIMER;

// Per-request deadline tracking; must be accessed under the same lock as VIOSOCK_TIMER.
// Deadline == MAXLONGLONG means no expiry. Always initialize with VIOSockTimerEntryInit
// because WDF zero-initializes object contexts (Deadline=0 would look like tick 0 = expired).
typedef struct _VIOSOCK_TIMER_ENTRY
{
    LONGLONG Deadline; // absolute tick count of expiry (MAXLONGLONG = no deadline, 0 = not initialized)
} VIOSOCK_TIMER_ENTRY, *PVIOSOCK_TIMER_ENTRY;

#define VIOSOCK_DEVICE_NAME L"\\Device\\Viosock"

typedef struct _DEVICE_CONTEXT
{

    VIRTIO_WDF_DRIVER VDevice;

    WDFDEVICE ThisDevice;

    // Recv packets
    WDFQUEUE ReadQueue;

    WDFSPINLOCK RxLock;
    _Guarded_by_(RxLock) PVIOSOCK_VQ RxVq;
    PVOID RxPktVA; // contiguous array of VIOSOCK_RX_PKT
    PHYSICAL_ADDRESS RxPktPA;
    _Guarded_by_(RxLock) SINGLE_LIST_ENTRY RxPktList; // postponed requests
    ULONG RxPktNum;
    ULONG RxCbBuffersNum;
    WDFLOOKASIDE RxCbBufferMemoryList;
    _Guarded_by_(RxLock) SINGLE_LIST_ENTRY RxCbBuffers; // list or Rx buffers

    // Send packets
    WDFQUEUE WriteQueue;

    WDFSPINLOCK TxLock;
    _Guarded_by_(TxLock) PVIOSOCK_VQ TxVq;
    _Guarded_by_(TxLock) PVIRTIO_DMA_MEMORY_SLICED TxPktSliced;
    LONG TxPktNum; // Num of slices in TxPktSliced
    _Guarded_by_(TxLock) LONG TxQueuedReply;
    _Guarded_by_(TxLock) LIST_ENTRY TxList;
    WDFLOOKASIDE TxMemoryList;            // TX entries (one shape, full-chunk sg list)
    WDFLOOKASIDE TxBufferMemoryList;      // 64K skb copy buffers
    WDFLOOKASIDE TxSmallBufferMemoryList; // 2-page skb copy buffers (short sends, tail/credit-squeezed chunks)
    _Interlocked_ volatile LONG TxPktAllocated;

    WDFLOOKASIDE AcceptMemoryList;

    // Events
    PVIOSOCK_VQ EvtVq;
    PVIRTIO_VSOCK_EVENT EvtVA;
    PHYSICAL_ADDRESS EvtPA;
    ULONG EvtRstOccured;

    WDFSPINLOCK BoundLock;
    _Guarded_by_(BoundLock) WDFCOLLECTION BoundList;

    WDFSPINLOCK ConnectedLock;
    _Guarded_by_(ConnectedLock) WDFCOLLECTION ConnectedList;

    WDFWAITLOCK SelectLock;
    _Guarded_by_(SelectLock) LIST_ENTRY SelectList;
    _Interlocked_ volatile LONG SelectInProgress;
    WDFWORKITEM SelectWorkitem;
    _Guarded_by_(SelectLock) VIOSOCK_TIMER SelectTimer;
    // Set in EvtDeviceSelfManagedIoSuspend (device leaving D0), cleared on restart.
    // While set, pending and incoming select()/poll() are completed instead of pended,
    // so the power-managed IoCtl queue has nothing driver-owned to wait on at D0 exit.
    _Guarded_by_(SelectLock) BOOLEAN PowerSuspended;

    WDFQUEUE IoCtlQueue;
    WDFQUEUE CreateQueue;

    WDFINTERRUPT WdfInterrupt;

    _Interlocked_ volatile LONG SocketId; // for debug

    VIRTIO_VSOCK_CONFIG Config;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

typedef enum _VIOSOCK_STATE
{
    VIOSOCK_STATE_CLOSE = 0,
    VIOSOCK_STATE_CONNECTING = 1,
    VIOSOCK_STATE_CONNECTED = 2,
    VIOSOCK_STATE_CLOSING = 3,
    VIOSOCK_STATE_LISTEN = 4,
} VIOSOCK_STATE;

#define SOCK_CONTROL   0x01
#define SOCK_BOUND     0x02
#define SOCK_LINGER    0x04
#define SOCK_NON_BLOCK 0x08
#define SOCK_LOOPBACK  0x10
#define SOCK_CLEANUP   0x20
#define SOCK_CONNECT   0x40  // outbound: connect() was issued on this socket (distinguishes it from an accepted socket)
#define SOCK_TX_WAIT   0x80  // a non-blocking send returned STATUS_CANT_WAIT; re-arm FD_WRITE when TX space reopens
#define SOCK_RESET     0x100 // the peer reset the connection (OP_RST while connected) — sends report WSAECONNRESET

//
// WSAPoll entry point on the LSP path: ws2_32!WSAPoll packs the caller's
// WSAPOLLFD array into a single WSAPOLLDATA and dispatches via WSPIoctl.
// Kernel cannot include winsock2.h / mswsock.h, so mirror the layouts here.
//
#define SIO_EXT_POLL   _WSAIORW(IOC_WS2, 31)

#ifndef POLLRDNORM
#define POLLRDNORM 0x0100
#define POLLRDBAND 0x0200
#define POLLIN     (POLLRDNORM | POLLRDBAND)
#define POLLPRI    0x0400

#define POLLWRNORM 0x0010
#define POLLOUT    (POLLWRNORM)
#define POLLWRBAND 0x0020

#define POLLERR    0x0001
#define POLLHUP    0x0002
#define POLLNVAL   0x0004
#endif

#define INVALID_HANDLE    ((ULONGLONG)(-1))
#define INVALID_HANDLE_32 ((ULONG)(-1))

//
// VIOSOCK_POLL_FD / VIOSOCK_POLL_DATA mirror WSAPOLLFD / WSAPOLLDATA for native
// (64-bit-process) callers; VIOSOCK_POLL_FD32 / VIOSOCK_POLL_DATA32 mirror the
// WOW64 (32-bit-process) layout where SOCKET shrinks to 4 bytes and the
// header carries no 8-byte-alignment padding.
//
//   Native x64: FD = 16 bytes (8 + 2 + 2 + 4 pad),
//               DATA header = 16 bytes (4 + 4 + 4 + 4 pad before 8-aligned array).
//   WOW64    :  FD32 = 8 bytes (4 + 2 + 2),
//               DATA32 header = 12 bytes (4 + 4 + 4, no trailing pad).
//
typedef struct _VIOSOCK_POLL_FD
{
    ULONGLONG Fd;
    SHORT Events;
    SHORT Revents;
} VIOSOCK_POLL_FD, *PVIOSOCK_POLL_FD;

typedef struct _VIOSOCK_POLL_DATA
{
    INT Result;
    ULONG Fds;
    INT Timeout;
    VIOSOCK_POLL_FD FdArray[1];
} VIOSOCK_POLL_DATA, *PVIOSOCK_POLL_DATA;

typedef struct _VIOSOCK_POLL_FD32
{
    ULONG Fd;
    SHORT Events;
    SHORT Revents;
} VIOSOCK_POLL_FD32, *PVIOSOCK_POLL_FD32;

typedef struct _VIOSOCK_POLL_DATA32
{
    INT Result;
    ULONG Fds;
    INT Timeout;
    VIOSOCK_POLL_FD32 FdArray[1];
} VIOSOCK_POLL_DATA32, *PVIOSOCK_POLL_DATA32;

C_ASSERT(FIELD_OFFSET(VIOSOCK_POLL_DATA, Result) == FIELD_OFFSET(VIOSOCK_POLL_DATA32, Result));
C_ASSERT(FIELD_OFFSET(VIOSOCK_POLL_DATA, Fds) == FIELD_OFFSET(VIOSOCK_POLL_DATA32, Fds));
C_ASSERT(FIELD_OFFSET(VIOSOCK_POLL_DATA, Timeout) == FIELD_OFFSET(VIOSOCK_POLL_DATA32, Timeout));

typedef struct _VIOSOCK_ACCEPT_ENTRY
{
    LIST_ENTRY ListEntry;
    WDFMEMORY Memory;
    WDFFILEOBJECT ConnectSocket;
    ULONG32 dst_cid;
    ULONG32 dst_port;
    ULONG32 peer_buf_alloc;
    ULONG32 peer_fwd_cnt;
} VIOSOCK_ACCEPT_ENTRY, *PVIOSOCK_ACCEPT_ENTRY;

typedef struct _SOCKET_CONTEXT
{

    WDFFILEOBJECT ThisSocket;

    _Interlocked_ volatile LONG Flags;
    LONG SocketId; // for debug

    VIRTIO_VSOCK_TYPE type;

    ULONG32 dst_cid;
    ULONG32 src_port;
    ULONG32 dst_port;

    WDFSPINLOCK StateLock;
    _Interlocked_ volatile VIOSOCK_STATE State;
    LONGLONG ConnectTimeout;
    LONGLONG SendTimeout;
    LONGLONG RecvTimeout;
    ULONG32 BufferMinSize;
    ULONG32 BufferMaxSize;
    ULONG32 PeerShutdown;
    ULONG32 Shutdown;

    KEVENT CloseEvent;

    _Guarded_by_(StateLock) PKEVENT EventObject;
    _Guarded_by_(StateLock) ULONG EventsMask;
    _Guarded_by_(StateLock) ULONG Events;
    _Guarded_by_(StateLock) NTSTATUS EventsStatus[FD_MAX_EVENTS];

    WDFSPINLOCK RxLock;                                // accept list lock for listen socket
    _Interlocked_ volatile LONG ReadDequeueInProgress; // recv-dequeue worker serialization
    _Guarded_by_(RxLock) LIST_ENTRY RxCbList;
    _Guarded_by_(RxLock) volatile ULONG RxBytes; // used bytes in rx buffer
    _Guarded_by_(RxLock) ULONG RxBuffers;        // used rx buffers (for debug)

    WDFQUEUE ReadQueue;
    _Guarded_by_(RxLock) VIOSOCK_TIMER ReadTimer;

    // Per-socket send FIFO (Tx refactor): data-carrying requests from all three
    // entry points (IRP_MJ_WRITE, IOCTL_SOCKET_SEND, IOCTL_SOCKET_SEND_EX) park
    // here, framework-cancellable, until the slicer pump consumes them head-first.
    WDFSPINLOCK TxLock;
    _Interlocked_ volatile LONG WriteEnqueueInProgress; // send-enqueue worker serialization
    WDFQUEUE WriteQueue;
    _Guarded_by_(TxLock) VIOSOCK_TIMER WriteTimer;
    _Interlocked_ volatile LONG BytesUnsent; // SIOCOUTQ: bytes sliced but not yet consumed by the host
    KEVENT TxIdleEvent;                      // signaled while BytesUnsent == 0; SO_LINGER close waits on it

    _Guarded_by_(RxLock) WDFREQUEST PendedRequest;
    _Guarded_by_(RxLock) VIOSOCK_TIMER PendedTimer;

    _Guarded_by_(RxLock) LIST_ENTRY AcceptList;
    LONG Backlog;
    _Interlocked_ volatile LONG AcceptPended;

    // Pending accept() requests (multiple), power-managed WDF queue on the listen
    // socket. Framework-owned while parked, so it does not block the D0->Dx
    // transition. Matched against AcceptList by VIOSockAcceptProcessQueue.
    WDFQUEUE AcceptQueue;
    _Interlocked_ volatile LONG AcceptMatchInProgress;

    ULONG32 buf_alloc;
    _Guarded_by_(RxLock) ULONG32 fwd_cnt;
    ULONG32 last_fwd_cnt;
    ULONG RcvLowat; // SO_RCVLOWAT: report the socket readable only once RxBytes >= RcvLowat

    _Guarded_by_(StateLock) ULONG32 peer_buf_alloc;
    _Guarded_by_(StateLock) ULONG32 peer_fwd_cnt;
    _Guarded_by_(StateLock) ULONG32 tx_cnt;

    USHORT LingerTime;
    WDFFILEOBJECT LoopbackSocket;

    volatile LONG SelectRefs[FDSET_MAX];
} SOCKET_CONTEXT, *PSOCKET_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOCKET_CONTEXT, GetSocketContext);

#define VIOSockIsFlag(s, f)            ((s)->Flags & (f))
#define VIOSockSetFlag(s, f)           (InterlockedOr(&(s)->Flags, (f)) & (f))
#define VIOSockResetFlag(s, f)         (InterlockedAnd(&(s)->Flags, ~(f)) & (f))

#define VIOSockIsNonBlocking(s)        VIOSockIsFlag((s), SOCK_NON_BLOCK)

#define GetSocketContextFromRequest(r) GetSocketContext(WdfRequestGetFileObject((r)))

#define GetDeviceContextFromRequest(r) GetDeviceContext(WdfFileObjectGetDevice(WdfRequestGetFileObject((r))))

#define GetDeviceContextFromSocket(s)  GetDeviceContext(WdfFileObjectGetDevice((s)->ThisSocket))

#define IsControlRequest(r)            VIOSockIsFlag(GetSocketContextFromRequest(r), SOCK_CONTROL)

#define IsLoopbackSocket(s)            (VIOSockIsFlag(s, SOCK_LOOPBACK))

//////////////////////////////////////////////////////////////////////////
// Device functions

EVT_WDF_DRIVER_DEVICE_ADD VIOSockEvtDeviceAdd;

NTSTATUS
VIOSockInterruptInit(IN WDFDEVICE hDevice);

//////////////////////////////////////////////////////////////////////////
// Socket functions
EVT_WDF_FILE_CLEANUP VIOSockCleanup;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VIOSockObjectCleanup;

NTSTATUS
VIOSockCreate(IN WDFDEVICE WdfDevice, IN WDFREQUEST Request);

NTSTATUS
VIOSockDeviceControl(IN WDFREQUEST Request, IN ULONG IoControlCode, IN OUT size_t *pLength);

WDFFILEOBJECT
VIOSockGetSocketFromHandle(IN PDEVICE_CONTEXT pContext, IN ULONGLONG uSocket, IN BOOLEAN bIs32BitProcess);

VOID VIOSockHandleTransportReset(IN PDEVICE_CONTEXT pContext);

NTSTATUS
VIOSockBoundListInit(IN WDFDEVICE hDevice);

NTSTATUS
VIOSockBoundAdd(IN PSOCKET_CONTEXT pSocket, IN ULONG32 svm_port);

_Requires_lock_not_held_(pContext->BoundLock) PSOCKET_CONTEXT VIOSockBoundFindByPortLocked(IN PDEVICE_CONTEXT pContext,
                                                                                           IN ULONG32 ulSrcPort);

_Requires_lock_held_(pContext->BoundLock) PSOCKET_CONTEXT VIOSockBoundFindByPort(IN PDEVICE_CONTEXT pContext,
                                                                                 IN ULONG32 ulSrcPort);

_Requires_lock_not_held_(pContext->BoundLock) PSOCKET_CONTEXT VIOSockBoundFindByFileLocked(IN PDEVICE_CONTEXT pContext,
                                                                                           IN PFILE_OBJECT pFileObject);

NTSTATUS
VIOSockConnectedListInit(IN WDFDEVICE hDevice);

_Requires_lock_not_held_(pContext->ConnectedLock) PSOCKET_CONTEXT VIOSockConnectedFindByRxPktLocked(IN PDEVICE_CONTEXT pContext,
                                                                                                    IN PVIRTIO_VSOCK_HDR pPkt);

_Requires_lock_held_(pSocket->StateLock) VIOSOCK_STATE VIOSockStateSet(IN PSOCKET_CONTEXT pSocket,
                                                                       IN VIOSOCK_STATE NewState);

_Requires_lock_not_held_(pSocket->StateLock) VIOSOCK_STATE VIOSockStateSetLocked(IN PSOCKET_CONTEXT pSocket,
                                                                                 IN VIOSOCK_STATE NewState);

#define VIOSockStateGet(s) ((s)->State)

_Requires_lock_not_held_(pSocket->StateLock) BOOLEAN VIOSockShutdownFromPeer(PSOCKET_CONTEXT pSocket, ULONG uFlags);

VOID VIOSockDoClose(PSOCKET_CONTEXT pSocket);

__inline BOOLEAN VIOSockIsDone(PSOCKET_CONTEXT pSocket)
{
    return !!KeReadStateEvent(&pSocket->CloseEvent);
}

_Requires_lock_held_(pSocket->RxLock) NTSTATUS VIOSockPendedRequestSet(IN PSOCKET_CONTEXT pSocket,
                                                                       IN WDFREQUEST Request,
                                                                       IN LONGLONG Deadline);

_Requires_lock_not_held_(pSocket->RxLock) NTSTATUS VIOSockPendedRequestSetLocked(IN PSOCKET_CONTEXT pSocket,
                                                                                 IN WDFREQUEST Request,
                                                                                 IN LONGLONG Deadline);

#define VIOSockPendedRequestSetResume(s, r) VIOSockPendedRequestSet(s, r, 0)

_Requires_lock_not_held_(pSocket->RxLock) NTSTATUS VIOSockPendedRequestSetResumeLocked(IN PSOCKET_CONTEXT pSocket,
                                                                                       IN WDFREQUEST Request);

_Requires_lock_held_(pSocket->RxLock) NTSTATUS VIOSockPendedRequestGet(IN PSOCKET_CONTEXT pSocket,
                                                                       OUT WDFREQUEST *Request);

_Requires_lock_not_held_(pSocket->RxLock) __declspec(noinline) NTSTATUS VIOSockPendedRequestGetLocked(
                                                                                                    IN PSOCKET_CONTEXT pSocket,
                                                                                                    OUT WDFREQUEST *Request);

_Requires_lock_not_held_(pListenSocket->StateLock) NTSTATUS VIOSockAcceptInitSocket(PSOCKET_CONTEXT pAcceptSocket,
                                                                                    PSOCKET_CONTEXT pListenSocket);

_Requires_lock_not_held_(pListenSocket->RxLock) NTSTATUS VIOSockAcceptEnqueuePkt(IN PSOCKET_CONTEXT pListenSocket,
                                                                                 IN PVIRTIO_VSOCK_HDR pPkt);

_Requires_lock_not_held_(pListenSocket->RxLock) VOID VIOSockAcceptRemovePkt(IN PSOCKET_CONTEXT pListenSocket,
                                                                            IN PVIRTIO_VSOCK_HDR pPkt);

// Pair backlogged connections (AcceptList) with pending accept requests
// (AcceptQueue). Safe to call from any context that just enqueued to
// either side; serialized internally.
VOID VIOSockAcceptProcessQueue(IN PSOCKET_CONTEXT pListenSocket);

_IRQL_requires_max_(DISPATCH_LEVEL) VOID VIOSockSelectRun(IN PSOCKET_CONTEXT pSocket);
_IRQL_requires_max_(DISPATCH_LEVEL) VOID VIOSockSelectEnqueueWorkItem(IN PDEVICE_CONTEXT pContext);

NTSTATUS
VIOSockPoll(IN WDFREQUEST Request,
            IN BOOLEAN bIs32BitProcess,
            IN PVIOSOCK_POLL_DATA pPollDataIn,
            IN size_t stPollDataInLen,
            IN PVIOSOCK_POLL_DATA pPollDataOut,
            IN size_t stPollDataOutLen,
            IN OUT size_t *pLength);

/*
 * WinSock 2 extension -- bit values and indices for FD_XXX network events
 */
#define FD_READ_BIT                     0
#define FD_READ                         (1 << FD_READ_BIT)
#define FD_WRITE_BIT                    1
#define FD_WRITE                        (1 << FD_WRITE_BIT)
#define FD_OOB_BIT                      2
#define FD_OOB                          (1 << FD_OOB_BIT)
#define FD_ACCEPT_BIT                   3
#define FD_ACCEPT                       (1 << FD_ACCEPT_BIT)
#define FD_CONNECT_BIT                  4
#define FD_CONNECT                      (1 << FD_CONNECT_BIT)
#define FD_CLOSE_BIT                    5
#define FD_CLOSE                        (1 << FD_CLOSE_BIT)
#define FD_QOS_BIT                      6
#define FD_QOS                          (1 << FD_QOS_BIT)
#define FD_GROUP_QOS_BIT                7
#define FD_GROUP_QOS                    (1 << FD_GROUP_QOS_BIT)
#define FD_ROUTING_INTERFACE_CHANGE_BIT 8
#define FD_ROUTING_INTERFACE_CHANGE     (1 << FD_ROUTING_INTERFACE_CHANGE_BIT)
#define FD_ADDRESS_LIST_CHANGE_BIT      9
#define FD_ADDRESS_LIST_CHANGE          (1 << FD_ADDRESS_LIST_CHANGE_BIT)

BOOLEAN VIOSockEventMarkBit(IN PSOCKET_CONTEXT pSocket, IN ULONG uSetBit, IN NTSTATUS Status);

_IRQL_requires_max_(DISPATCH_LEVEL) VOID VIOSockEventNotify(IN PSOCKET_CONTEXT pSocket, IN BOOLEAN bSetEvent);

VOID VIOSockEventSetBit(IN PSOCKET_CONTEXT pSocket, IN ULONG uSetBit, IN NTSTATUS Status);

_Requires_lock_not_held_(pSocket->StateLock) VOID VIOSockEventSetBitLocked(IN PSOCKET_CONTEXT pSocket,
                                                                           IN ULONG uSetBit,
                                                                           IN NTSTATUS Status);

_Requires_lock_not_held_(pSocket->StateLock) VOID VIOSockEventClearBitLocked(IN PSOCKET_CONTEXT pSocket,
                                                                             IN ULONG uClearBit);

//////////////////////////////////////////////////////////////////////////
// Chained Buffer entry
typedef struct _VIOSOCK_RX_CB
{
    union {
        SINGLE_LIST_ENTRY FreeListEntry;
        LIST_ENTRY ListEntry; // Request buffer list
    };

    PVOID BufferVA;            // common buffer of VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE bytes
    PHYSICAL_ADDRESS BufferPA; // common buffer PA

    PCHAR ReadPtr;
    ULONG BytesToRead;
    ULONG DataLen; // Valid data len (pkt.header.len)

    WDFMEMORY Memory;
    struct _VIOSOCK_TX_ENTRY *TxEntry; // loopback: the peer's TX entry backing BufferVA
} VIOSOCK_RX_CB, *PVIOSOCK_RX_CB;

//////////////////////////////////////////////////////////////////////////
// Tx functions

#define VIOSOCK_DMA_TX_PAGES       BYTES_TO_PAGES(VIRTIO_VSOCK_MAX_PKT_BUF_SIZE)

// A 64K chunk may start at an arbitrary page offset, hence +1
#define VIOSOCK_TX_SG_MAX_ELEMENTS (VIOSOCK_DMA_TX_PAGES + 1)

// One entry shape for everything (TxMemoryList), the SgList is sized for the
// largest chunk. Kinds differ only by content:
//  command   (len == 0)                — header only, SgList unused;
//  zero-copy (len != 0, Buffer == 0)   — SgList to be filled from the request
//                                        MDL pages;
//  skb       (len != 0, Buffer != 0)   — a copy buffer from TxBufferMemoryList,
//                                        its physical pages pre-fill the SgList.
// Commands never outlive the vq insert, so their oversized tail costs only
// transient TxList residency (≤ the reply throttle) — not worth a second shape.
typedef struct _VIOSOCK_TX_ENTRY
{
    LIST_ENTRY ListEntry;
    WDFMEMORY Memory;
    WDFREQUEST Request;
    WDFFILEOBJECT Socket;
    PVOID Buffer;           // skb copy buffer VA, NULL otherwise
    WDFMEMORY BufferMemory; // lookaside handle of Buffer, WDF_NO_HANDLE otherwise

    ULONG64 dst_cid;
    ULONG32 src_port;
    ULONG32 dst_port;

    ULONG32 len;
    USHORT op;
    BOOLEAN reply;
    ULONG32 flags;
    USHORT type;

    // Credit snapshot taken at entry creation (Linux inc_tx_pkt parity): the
    // entry is fully self-contained afterwards and never touches the socket
    // again, so it may legally outlive it while draining through the TxList.
    ULONG32 buf_alloc;
    ULONG32 fwd_cnt;

    ULONG SgCount; // filled SgList elements (0 for command entries)
    union {
        VIOSOCK_SG_DESC SgList[VIOSOCK_TX_SG_MAX_ELEMENTS]; // trailing: VIOSOCK_TX_SG_MAX_ELEMENTS for data entries
        VIOSOCK_RX_CB Cb;
    } BufferDesc;
} VIOSOCK_TX_ENTRY, *PVIOSOCK_TX_ENTRY;

VOID VIOSockTxFreeEntry(PVIOSOCK_TX_ENTRY pTxEntry);

NTSTATUS
VIOSockWriteQueueInit(IN WDFDEVICE hDevice);

NTSTATUS
VIOSockWriteSocketQueueInit(IN PSOCKET_CONTEXT pSocket);

// Per-socket Tx pump, common for wire and loopback sockets — the paths diverge
// at VIOSockTxInsertEntry (wire → TxList/vq, loopback → synchronous delivery
// via VIOSockLoopbackInsertEntry).
_Requires_lock_not_held_(pSocket->TxLock) VOID VIOSockWriteProcessQueue(IN PSOCKET_CONTEXT pSocket);

VOID VIOSockLoopbackProcessTxEntry(IN PVIOSOCK_TX_ENTRY pTxEntry);

NTSTATUS
VIOSockTxVqInit(IN PDEVICE_CONTEXT pContext);

_Requires_lock_not_held_(pContext->TxLock) VOID VIOSockTxVqCleanup(IN PDEVICE_CONTEXT pContext);

_Requires_lock_not_held_(pContext->TxLock) VOID VIOSockTxVqProcess(IN PDEVICE_CONTEXT pContext);

_Requires_lock_not_held_(pSocket->StateLock) NTSTATUS VIOSockStateValidate(PSOCKET_CONTEXT pSocket, BOOLEAN bTx);

NTSTATUS
VIOSockTxEnqueueCommand(IN PSOCKET_CONTEXT pSocket, IN VIRTIO_VSOCK_OP Op, IN ULONG32 Flags OPTIONAL, IN BOOLEAN Reply);

#define VIOSockSendCreditUpdate(s) VIOSockTxEnqueueCommand(s, VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0, FALSE)

#define VIOSockSendConnect(s)      VIOSockTxEnqueueCommand(s, VIRTIO_VSOCK_OP_REQUEST, 0, FALSE)

#define VIOSockSendShutdown(s, f)  VIOSockTxEnqueueCommand(s, VIRTIO_VSOCK_OP_SHUTDOWN, f, FALSE)

#define VIOSockSendReset(s, r)     VIOSockTxEnqueueCommand(s, VIRTIO_VSOCK_OP_RST, 0, r)

#define VIOSockSendResponse(s)     VIOSockTxEnqueueCommand(s, VIRTIO_VSOCK_OP_RESPONSE, 0, TRUE)

_Requires_lock_not_held_(pContext->TxLock) NTSTATUS VIOSockSendResetNoSock(IN PDEVICE_CONTEXT pContext,
                                                                           IN PVIRTIO_VSOCK_HDR pHeader);

_Requires_lock_not_held_(pSocket->StateLock) __inline ULONG32 VIOSockTxGetCredit(IN PSOCKET_CONTEXT pSocket,
                                                                                 IN ULONG32 uCredit

)
{
    ULONG32 uRet, uTxBuf;
    LONG lAvail;

    WdfSpinLockAcquire(pSocket->StateLock);
    // Cap the TX window at min(peer rx buffer, our own buffer config) —
    // mirrors Linux tx_buf_size. Signed arithmetic guards against underflow
    // when the peer shrinks buf_alloc while bytes are in flight.
    uTxBuf = (pSocket->peer_buf_alloc < pSocket->buf_alloc) ? pSocket->peer_buf_alloc : pSocket->buf_alloc;
    lAvail = (LONG)uTxBuf - (LONG)(pSocket->tx_cnt - pSocket->peer_fwd_cnt);
    uRet = (lAvail > 0) ? (ULONG32)lAvail : 0;
    if (uRet > uCredit)
    {
        uRet = uCredit;
    }
    pSocket->tx_cnt += uRet;
    WdfSpinLockRelease(pSocket->StateLock);

    return uRet;
}

_Requires_lock_not_held_(pSocket->StateLock) __inline VOID VIOSockTxPutCredit(IN PSOCKET_CONTEXT pSocket,
                                                                              IN ULONG32 uCredit

)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->tx_cnt -= uCredit;
    WdfSpinLockRelease(pSocket->StateLock);
}

_Requires_lock_held_(pSocket->StateLock) __inline LONG VIOSockTxHasSpace(IN PSOCKET_CONTEXT pSocket)
{
    // TX window capped at min(peer rx buffer, our own buffer) — see VIOSockTxGetCredit.
    ULONG32 uTxBuf = (pSocket->peer_buf_alloc < pSocket->buf_alloc) ? pSocket->peer_buf_alloc : pSocket->buf_alloc;
    LONG lBytes = (LONG)uTxBuf - (LONG)(pSocket->tx_cnt - pSocket->peer_fwd_cnt);
    if (lBytes < 0)
    {
        lBytes = 0;
    }
    return lBytes;
}

_Requires_lock_not_held_(pSocket->StateLock) __inline LONG VIOSockTxSpaceUpdate(IN PSOCKET_CONTEXT pSocket,
                                                                                IN PVIRTIO_VSOCK_HDR pPkt)
{
    LONG uSpace;

    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->peer_buf_alloc = pPkt->buf_alloc;
    pSocket->peer_fwd_cnt = pPkt->fwd_cnt;
    uSpace = VIOSockTxHasSpace(pSocket);
    WdfSpinLockRelease(pSocket->StateLock);

    return uSpace;
}

__inline BOOLEAN VIOSockTxMoreReplies(IN PDEVICE_CONTEXT pContext)
{
    return pContext->TxQueuedReply < (LONG)pContext->RxPktNum;
}

_Requires_lock_not_held_(pContext->TxLock) VOID VIOSockTxCleanup(PDEVICE_CONTEXT pContext,
                                                                 WDFFILEOBJECT Socket,
                                                                 NTSTATUS Status);

VOID VIOSockWriteIoSuspend(IN PDEVICE_CONTEXT pContext);

VOID VIOSockWriteIoRestart(IN PDEVICE_CONTEXT pContext);

NTSTATUS
VIOSockSendBuffered(IN WDFREQUEST Request);

NTSTATUS
VIOSockSendEx(IN WDFREQUEST Request);

//////////////////////////////////////////////////////////////////////////
// Rx functions

NTSTATUS
VIOSockRxVqInit(IN PDEVICE_CONTEXT pContext);

_Requires_lock_not_held_(pContext->RxLock) VOID VIOSockRxVqCleanup(IN PDEVICE_CONTEXT pContext);

_Requires_lock_not_held_(pSocket->StateLock) __inline VOID VIOSockRxIncTxPkt(IN PSOCKET_CONTEXT pSocket,
                                                                             IN OUT PVIRTIO_VSOCK_HDR pPkt)
{
    WdfSpinLockAcquire(pSocket->StateLock);
    pSocket->last_fwd_cnt = pSocket->fwd_cnt;
    pPkt->fwd_cnt = pSocket->fwd_cnt;
    pPkt->buf_alloc = pSocket->buf_alloc;
    WdfSpinLockRelease(pSocket->StateLock);
}

_Requires_lock_not_held_(pContext->RxLock) VOID VIOSockRxVqProcess(IN PDEVICE_CONTEXT pContext);

// Loopback delivery: wrap the peer's TX entry into an Rx CB of pSocket.
_Requires_lock_not_held_(pSocket->RxLock) BOOLEAN VIOSockRxEnqueueTxEntry(IN PSOCKET_CONTEXT pSocket,
                                                                          IN PVIOSOCK_TX_ENTRY pTxEntry);

VOID VIOSockRxProcessTxHasSpace(IN PSOCKET_CONTEXT pSocket);

__inline ULONG VIOSockRxHasData(IN PSOCKET_CONTEXT pSocket)
{
    return pSocket->RxBytes;
}

// SO_RCVLOWAT readiness: the socket is reported readable only when at least
// RcvLowat bytes are buffered (mirrors Linux vsock_data_ready()/poll()).
// RcvLowat defaults to 1, so the comparison matches the prior "has any data"
// behavior for sockets that never set SO_RCVLOWAT.
__inline BOOLEAN VIOSockRxReadReady(IN PSOCKET_CONTEXT pSocket)
{
    return (pSocket->RxBytes >= pSocket->RcvLowat);
}

_Requires_lock_not_held_(pSocket->StateLock) __inline VOID VIOSockEventSetReadBitLocked(IN PSOCKET_CONTEXT pSocket)
{
    if (VIOSockRxReadReady(pSocket))
    {
        VIOSockEventSetBitLocked(pSocket, FD_READ_BIT, STATUS_SUCCESS);
    }
}

NTSTATUS
VIOSockReadQueueInit(IN WDFDEVICE hDevice);

NTSTATUS
VIOSockReadSocketQueueInit(IN PSOCKET_CONTEXT pSocket);

_Requires_lock_not_held_(pSocket->RxLock) VOID VIOSockReadProcessQueue(IN PSOCKET_CONTEXT pSocket);

_Requires_lock_not_held_(pSocket->RxLock) VOID VIOSockReadCleanupCb(IN PSOCKET_CONTEXT pSocket);

NTSTATUS
VIOSockRecvBuffered(IN WDFREQUEST Request);

NTSTATUS
VIOSockRecvEx(IN WDFREQUEST Request);

//////////////////////////////////////////////////////////////////////////
// Scatter-gather buffer chain attached to a data-carrying request.
// Built from the user WSABUF array of IOCTL_SOCKET_RECV_EX by VIOSockSgPrepare;
// plain Read/Write requests may later be wrapped into the same context as a
// single borrowed MDL.

typedef struct _VIOSOCK_SG_CTX
{
    PMDL MdlChain;  // locked MDLs linked via Mdl->Next in WSABUF order;
                    // per-buffer lengths are MmGetMdlByteCount().
                    // MDL ownership is encoded by the context cleanup callback:
                    // VIOSockSgPrepare builds its own chain and attaches the
                    // callback that unlocks/frees it (unlocking also releases the
                    // system mappings); a context wrapping a borrowed MDL (e.g.
                    // the WDF request MDL — locked and possibly mapped by the I/O
                    // manager) is allocated without the callback, leaving the MDL
                    // to its owner.
    ULONG TotalLen; // sum of all chain buffer lengths
} VIOSOCK_SG_CTX, *PVIOSOCK_SG_CTX;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSOCK_SG_CTX, GetRequestSgContext);

NTSTATUS
VIOSockSgPrepare(IN WDFREQUEST Request, IN LOCK_OPERATION LockOp);

// System mapping is only needed by the copying paths (Rx CB copy-out, buffered Tx);
// the zero-copy Tx path feeds the virtqueue with physical addresses straight from
// the MDL and never maps. The mapping is created once and cached inside the MDL
// itself; MmUnlockPages releases it. Callable at <= DISPATCH_LEVEL, NULL on failure.
__inline PVOID VIOSockSgMapMdl(IN PMDL Mdl)
{
    return MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority | MdlMappingNoExecute);
}

//////////////////////////////////////////////////////////////////////////
// Event functions
_IRQL_requires_max_(PASSIVE_LEVEL) NTSTATUS VIOSockEvtVqInit(IN PDEVICE_CONTEXT pContext);

_IRQL_requires_max_(PASSIVE_LEVEL) VOID VIOSockEvtVqCleanup(IN PDEVICE_CONTEXT pContext);

_IRQL_requires_max_(DISPATCH_LEVEL) VOID VIOSockEvtVqProcess(IN PDEVICE_CONTEXT pContext);

//////////////////////////////////////////////////////////////////////////
BOOLEAN
VIOSockLoopbackAcceptDequeue(IN PSOCKET_CONTEXT pAcceptSocket, IN PVIOSOCK_ACCEPT_ENTRY pAcceptEntry);

//////////////////////////////////////////////////////////////////////////
__inline LONGLONG VIOSockTimerTimeoutToTicks(IN LONGLONG Timeout)
{
    return Timeout / VIOSOCK_TIMER_TIME_INCREMENT;
}

__inline LONGLONG VIOSockTimerTicksToTimeout(IN LONGLONG Ticks)
{
    return Ticks * VIOSOCK_TIMER_TIME_INCREMENT;
}

__inline LONGLONG VIOSockTimerTicksToDeadline(IN LONGLONG Ticks)
{
    LARGE_INTEGER liTicks;

    KeQueryTickCount(&liTicks);
    return liTicks.QuadPart + Ticks;
}

__inline LONGLONG VIOSockTimerDeadlineRemainingTicks(IN LONGLONG Deadline)
{
    LARGE_INTEGER liNow;
    LONGLONG llRemaining;

    KeQueryTickCount(&liNow);
    llRemaining = (Deadline - liNow.QuadPart);
    return llRemaining > 0 ? llRemaining : 0;
}

__inline BOOLEAN VIOSockTimerDeadlineIsExpired(IN LONGLONG Deadline)
{
    return VIOSockTimerDeadlineRemainingTicks(Deadline) <= VIOSOCK_TIMER_TOLERANCE_TICKS;
}

__inline LONGLONG VIOSockTimerTimeoutToDeadline(IN LONGLONG Timeout)
{
    return Timeout == MAXLONGLONG ? MAXLONGLONG : VIOSockTimerTicksToDeadline(VIOSockTimerTimeoutToTicks(Timeout));
}

// Caller must ensure Deadline != MAXLONGLONG; passing MAXLONGLONG overflows on multiply.
__inline LONGLONG VIOSockTimerDeadlineToTimeout(IN LONGLONG Deadline)
{
    return Deadline == MAXLONGLONG ? MAXLONGLONG
                                   : VIOSockTimerTicksToTimeout(VIOSockTimerDeadlineRemainingTicks(Deadline));
}

__inline BOOLEAN VIOSockTimerEntryIsSet(IN PVIOSOCK_TIMER_ENTRY pEntry)
{
    return pEntry->Deadline != MAXLONGLONG;
}

__inline VOID VIOSockTimerEntryInit(IN PVIOSOCK_TIMER_ENTRY pEntry)
{
    pEntry->Deadline = MAXLONGLONG;
}

__inline LONGLONG VIOSockTimerEntryRemainingTicks(IN PVIOSOCK_TIMER_ENTRY pEntry)
{
    if (!VIOSockTimerEntryIsSet(pEntry))
    {
        return MAXLONGLONG;
    }

    return VIOSockTimerDeadlineRemainingTicks(pEntry->Deadline);
}

// Safe to call without a prior IsSet check: MAXLONGLONG - now is always a large positive
// value, so RemainingTicks > TOLERANCE_TICKS and the function returns FALSE for unset entries.
__inline BOOLEAN VIOSockTimerEntryIsExpired(IN PVIOSOCK_TIMER_ENTRY pEntry)
{
    return VIOSockTimerDeadlineIsExpired(pEntry->Deadline);
}

NTSTATUS VIOSockTimerCreate(IN PVIOSOCK_TIMER pTimer, IN WDFOBJECT ParentObject, IN PFN_WDF_TIMER EvtTimerFunc);

VOID VIOSockTimerStartDeadline(IN PVIOSOCK_TIMER pTimer, IN PVIOSOCK_TIMER_ENTRY pEntry OPTIONAL, IN LONGLONG Deadline);

__inline VOID VIOSockTimerStartTimeout(IN PVIOSOCK_TIMER pTimer,
                                       IN PVIOSOCK_TIMER_ENTRY pEntry OPTIONAL,
                                       IN LONGLONG Timeout)
{
    ASSERT(Timeout > 0);
    if (Timeout > 0)
    {
        VIOSockTimerStartDeadline(pTimer, pEntry, VIOSockTimerTimeoutToDeadline(Timeout));
    }
}

__inline VOID VIOSockTimerSetDeadline(IN PVIOSOCK_TIMER pTimer, IN LONGLONG Deadline)
{
    LONGLONG Timeout;

    ASSERT(Deadline > 0);
    if (!Deadline || Deadline == MAXLONGLONG)
    {
        ASSERT(!pTimer->StartRefs);
        pTimer->Deadline = MAXLONGLONG;
        return;
    }

    pTimer->Deadline = Deadline;
    Timeout = VIOSockTimerDeadlineToTimeout(Deadline);
    WdfTimerStart(pTimer->Timer, -Timeout);
}

__inline VOID VIOSockTimerSetTimeout(IN PVIOSOCK_TIMER pTimer, IN LONGLONG Timeout)
{
    ASSERT(Timeout > 0);
    VIOSockTimerSetDeadline(pTimer, VIOSockTimerTimeoutToDeadline(Timeout));
}

__inline VOID VIOSockTimerCancel(IN PVIOSOCK_TIMER pTimer)
{
    WdfTimerStop(pTimer->Timer, FALSE);
    pTimer->Deadline = MAXLONGLONG;
}

__inline VOID VIOSockTimerDeref(IN PVIOSOCK_TIMER pTimer, IN BOOLEAN bStop)
{
    ASSERT(pTimer->StartRefs);
    if (--pTimer->StartRefs == 0)
    {
        pTimer->Deadline = MAXLONGLONG;
        if (bStop)
        {
            WdfTimerStop(pTimer->Timer, FALSE);
        }
    }
}

//////////////////////////////////////////////////////////////////////////

__inline VOID VioSockReference(IN WDFFILEOBJECT Socket)
{
    ObReferenceObject(WdfFileObjectWdmGetFileObject(Socket));
    WdfObjectReference(Socket);

    return;
}

__inline VOID VioSockDereference(IN WDFFILEOBJECT Socket)
{
    WdfObjectDereference(Socket);
    ObDereferenceObject(WdfFileObjectWdmGetFileObject(Socket));

    return;
}

#endif /* VIOSOCK_H */
