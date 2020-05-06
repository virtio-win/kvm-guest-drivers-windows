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

#define VSOCK_DEFAULT_CONNECT_TIMEOUT       MSEC_TO_NANO(2 * 1000)
#define VSOCK_DEFAULT_BUFFER_SIZE           (1024 * 256)
#define VSOCK_DEFAULT_BUFFER_MAX_SIZE       (1024 * 256)
#define VSOCK_DEFAULT_BUFFER_MIN_SIZE       128

#define VIRTIO_VSOCK_MAX_EVENTS 8

#define LAST_RESERVED_PORT  1023
#define MAX_PORT_RETRIES    24
//////////////////////////////////////////////////////////////////////////

#define VIOSOCK_DEVICE_NAME L"\\Device\\Viosock"

typedef struct _DEVICE_CONTEXT {

    VIRTIO_WDF_DRIVER           VDevice;

    WDFDEVICE                   ThisDevice;

    //Recv packets
    WDFQUEUE                    ReadQueue;

    WDFSPINLOCK                 RxLock;
    PVIOSOCK_VQ                 RxVq;
    PVOID                       RxPktVA;        //contiguous array of VIOSOCK_RX_PKT
    PHYSICAL_ADDRESS            RxPktPA;
    SINGLE_LIST_ENTRY           RxPktList;      //postponed requests
    ULONG                       RxPktNum;
    WDFLOOKASIDE                RxCbBufferMemoryList;
    SINGLE_LIST_ENTRY           RxCbBuffers;    //list or Rx buffers
    ULONG                       RxCbBuffersNum;

    //Send packets
    WDFQUEUE                    WriteQueue;

    WDFSPINLOCK                 TxLock;
    PVIOSOCK_VQ                 TxVq;
    PVIRTIO_DMA_MEMORY_SLICED   TxPktSliced;
    ULONG                       TxPktNum;       //Num of slices in TxPktSliced
    LIST_ENTRY                  TxList;
    WDFLOOKASIDE                TxMemoryList;
    LONG                        QueuedReply;

    //Events
    PVIOSOCK_VQ                 EvtVq;
    PVIRTIO_VSOCK_EVENT         EvtVA;
    PHYSICAL_ADDRESS            EvtPA;
    ULONG                       EvtRstOccured;

    WDFINTERRUPT                WdfInterrupt;

    WDFSPINLOCK                 BoundLock;
    WDFCOLLECTION               BoundList;
    WDFSPINLOCK                 ConnectedLock;
    WDFCOLLECTION               ConnectedList;

    WDFLOOKASIDE                AcceptMemoryList;

    WDFQUEUE                    IoCtlQueue;

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

    volatile LONG   Flags;

    VIRTIO_VSOCK_TYPE  type;

    ULONG32         dst_cid;
    ULONG32         src_port;
    ULONG32         dst_port;

    WDFSPINLOCK     StateLock;
    volatile VIOSOCK_STATE   State;
    LARGE_INTEGER   ConnectTimeout;
    ULONG32         BufferMinSize;
    ULONG32         BufferMaxSize;
    ULONG32         PeerShutdown;
    ULONG32         Shutdown;

    PKEVENT         EventObject;
    ULONG           EventsMask;
    ULONG           Events;
    NTSTATUS        EventsStatus[FD_MAX_EVENTS];

    WDFSPINLOCK     RxLock;
    LIST_ENTRY      RxCbList;
    PCHAR           RxCbReadPtr;    //read ptr in first CB
    ULONG           RxCbReadLen;    //remaining bytes in first CB
    ULONG           RxBytes;        //used bytes in rx buffer
    ULONG           RxBuffers;      //used rx buffers (for debug)

    WDFQUEUE        ReadQueue;
    PCHAR           ReadRequestPtr;
    ULONG           ReadRequestFree;
    ULONG           ReadRequestLength;
    ULONG           ReadRequestFlags;

    WDFREQUEST      PendedRequest;

    //RxLock
    LIST_ENTRY      AcceptList;
    LONG            Backlog;
    volatile LONG   AcceptPended;

    ULONG32         buf_alloc;
    ULONG32         fwd_cnt;
    ULONG32         last_fwd_cnt;

    ULONG32         peer_buf_alloc;
    ULONG32         peer_fwd_cnt;
    ULONG32         tx_cnt;

    USHORT          LingerTime;
    WDFFILEOBJECT   LoopbackSocket;

} SOCKET_CONTEXT, *PSOCKET_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOCKET_CONTEXT, GetSocketContext);

#define VIOSockIsFlag(s,f) ((s)->Flags & (f))
#define VIOSockSetFlag(s,f) (InterlockedOr(&(s)->Flags, (f)) & (f))
#define VIOSockResetFlag(s,f) (InterlockedAnd(&(s)->Flags, ~(f)) & (f))

#define GetSocketContextFromRequest(r) GetSocketContext(WdfRequestGetFileObject((r)))

#define GetDeviceContextFromRequest(r) GetDeviceContext(WdfFileObjectGetDevice(WdfRequestGetFileObject((r))))

#define GetDeviceContextFromSocket(s) GetDeviceContext(WdfFileObjectGetDevice((s)->ThisSocket))

#define IsControlRequest(r) VIOSockIsFlag(GetSocketContextFromRequest(r), SOCK_CONTROL)

#define IsLoopbackSocket(s) (VIOSockIsFlag(s,SOCK_LOOPBACK))

//////////////////////////////////////////////////////////////////////////
//Device functions

EVT_WDF_DRIVER_DEVICE_ADD   VIOSockEvtDeviceAdd;

EVT_WDF_INTERRUPT_ISR       VIOSockInterruptIsr;
EVT_WDF_INTERRUPT_DPC       VIOSockInterruptDpc;
EVT_WDF_INTERRUPT_DPC       VIOSockWdfInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE    VIOSockInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE   VIOSockInterruptDisable;

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

NTSTATUS
VIOSockSelect(
    IN WDFREQUEST Request,
    IN OUT size_t *pLength
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

VIOSOCK_STATE
VIOSockStateSet(
    IN PSOCKET_CONTEXT pSocket,
    IN VIOSOCK_STATE   NewState
);

#define VIOSockStateGet(s) ((s)->State)

BOOLEAN
VIOSockShutdownFromPeer(
    PSOCKET_CONTEXT pSocket,
    ULONG uFlags
);

NTSTATUS
VIOSockPendedRequestSet(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request
);

NTSTATUS
VIOSockPendedRequestSetLocked(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request
);

NTSTATUS
VIOSockPendedRequestGet(
    IN  PSOCKET_CONTEXT pSocket,
    OUT WDFREQUEST      *Request
);

NTSTATUS
VIOSockPendedRequestGetLocked(
    IN PSOCKET_CONTEXT  pSocket,
    OUT WDFREQUEST      *Request
);

NTSTATUS
VIOSockAcceptEnqueuePkt(
    IN PSOCKET_CONTEXT      pListenSocket,
    IN PVIRTIO_VSOCK_HDR    pPkt
);

VOID
VIOSockAcceptRemovePkt(
    IN PSOCKET_CONTEXT      pListenSocket,
    IN PVIRTIO_VSOCK_HDR    pPkt
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

__inline
VOID
VIOSockEventSetBit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uSetBit,
    IN NTSTATUS Status
)
{
    ULONG uEvent = (1 << uSetBit);
    BOOLEAN bSetEvent;

    //TODO: lock events
    bSetEvent = !!(~pSocket->Events & uEvent & pSocket->EventsMask);

    pSocket->Events |= uEvent;
    pSocket->EventsStatus[uSetBit] = Status;

    if (bSetEvent && pSocket->EventObject)
        KeSetEvent(pSocket->EventObject, IO_NO_INCREMENT, FALSE);
}

__inline
VOID
VIOSockEventClearBit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG uClearBit
)
{
    ULONG uEvent = (1 << uClearBit);

    //TODO: lock events
    pSocket->Events &= ~uEvent;
}

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

VOID
VIOSockTxVqCleanup(
    IN PDEVICE_CONTEXT pContext
);

VOID
VIOSockTxVqProcess(
    IN PDEVICE_CONTEXT pContext
);

NTSTATUS
VIOSockTxValidateSocketState(
    PSOCKET_CONTEXT pSocket
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

NTSTATUS
VIOSockSendResetNoSock(
    IN PDEVICE_CONTEXT pContext,
    IN PVIRTIO_VSOCK_HDR pHeader
);

//TxLock+
__inline
ULONG32
VIOSockTxGetCredit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32 uCredit

)
{
    ULONG32 uRet;

    uRet = pSocket->peer_buf_alloc - (pSocket->tx_cnt - pSocket->peer_fwd_cnt);
    if (uRet > uCredit)
        uRet = uCredit;
    pSocket->tx_cnt += uRet;
    return uRet;
}


//TxLock+
__inline
VOID
VIOSockTxPutCredit(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32 uCredit

)
{
    pSocket->tx_cnt -= uCredit;
}

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

__inline
LONG
VIOSockTxSpaceUpdate(
    IN PSOCKET_CONTEXT pSocket,
    IN PVIRTIO_VSOCK_HDR pPkt
)
{
    pSocket->peer_buf_alloc = pPkt->buf_alloc;
    pSocket->peer_fwd_cnt = pPkt->fwd_cnt;
    return VIOSockTxHasSpace(pSocket);
}

__inline
BOOLEAN
VIOSockTxMoreReplies(
    IN PDEVICE_CONTEXT  pContext
)
{
    return pContext->QueuedReply < (LONG)pContext->RxPktNum;
}

//////////////////////////////////////////////////////////////////////////
//Rx functions

NTSTATUS
VIOSockRxVqInit(
    IN PDEVICE_CONTEXT pContext
);

VOID
VIOSockRxVqCleanup(
    IN PDEVICE_CONTEXT pContext
);

VOID
VIOSockRxVqProcess(
    IN PDEVICE_CONTEXT pContext
);

NTSTATUS
VIOSockRxRequestEnqueueCb(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       Request,
    IN ULONG            Length
);

//SRxLock+
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

VOID
VIOSockReadDequeueCb(
    IN PSOCKET_CONTEXT  pSocket,
    IN WDFREQUEST       ReadRequest OPTIONAL
);

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
NTSTATUS
VIOSockEvtVqInit(
    IN PDEVICE_CONTEXT pContext
);

VOID
VIOSockEvtVqCleanup(
    IN PDEVICE_CONTEXT pContext
);

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

#endif /* VIOSOCK_H */
