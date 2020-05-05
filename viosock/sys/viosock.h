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

#define VIRTIO_VSOCK_MAX_EVENTS 8

#define LAST_RESERVED_PORT  1023
#define MAX_PORT_RETRIES    24
//////////////////////////////////////////////////////////////////////////

#define VIOSOCK_DEVICE_NAME L"\\Device\\Viosock"

typedef struct _DEVICE_CONTEXT {

    VIRTIO_WDF_DRIVER           VDevice;

    WDFDEVICE                   ThisDevice;

    WDFSPINLOCK                 RxLock;
    PVIOSOCK_VQ                 RxVq;
    PVOID                       RxPktVA;        //contiguous array of VIOSOCK_RX_PKT
    PHYSICAL_ADDRESS            RxPktPA;
    ULONG                       RxPktNum;

    //Send packets
    WDFSPINLOCK                 TxLock;
    PVIOSOCK_VQ                 TxVq;
    PVIRTIO_DMA_MEMORY_SLICED   TxPktSliced;
    ULONG                       TxPktNum;       //Num of slices in TxPktSliced

    //Events
    PVIOSOCK_VQ                 EvtVq;
    PVIRTIO_VSOCK_EVENT         EvtVA;
    PHYSICAL_ADDRESS            EvtPA;
    ULONG                       EvtRstOccured;

    WDFINTERRUPT                WdfInterrupt;

    WDFSPINLOCK                 BoundLock;
    WDFCOLLECTION               BoundList;

    WDFCOLLECTION               SocketList;

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

typedef struct _SOCKET_CONTEXT {

    WDFFILEOBJECT   ThisSocket;

    volatile LONG   Flags;

    ULONG64 dst_cid;
    ULONG32 src_port;
    ULONG32 dst_port;
    ULONG32 flags;
    ULONG32 buf_alloc;
    ULONG32 fwd_cnt;

    WDFSPINLOCK     StateLock;
    volatile VIOSOCK_STATE   State;

    WDFFILEOBJECT ListenSocket;
} SOCKET_CONTEXT, *PSOCKET_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOCKET_CONTEXT, GetSocketContext);

#define VIOSockIsFlag(s,f) ((s)->Flags & (f))
#define VIOSockSetFlag(s,f) (InterlockedOr(&(s)->Flags, (f)) & (f))
#define VIOSockResetFlag(s,f) (InterlockedAnd(&(s)->Flags, ~(f)) & (f))

#define GetSocketContextFromRequest(r) GetSocketContext(WdfRequestGetFileObject((r)))

#define GetDeviceContextFromRequest(r) GetDeviceContext(WdfFileObjectGetDevice(WdfRequestGetFileObject((r))))

#define GetDeviceContextFromSocket(s) GetDeviceContext(WdfFileObjectGetDevice((s)->ThisSocket))

#define IsControlRequest(r) VIOSockIsFlag(GetSocketContextFromRequest(r), SOCK_CONTROL)

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
EVT_WDF_DEVICE_FILE_CREATE  VIOSockCreate;
EVT_WDF_FILE_CLOSE          VIOSockClose;

NTSTATUS
VIOSockDeviceControl(
    IN WDFREQUEST Request,
    IN ULONG      IoControlCode,
    IN OUT size_t *pLength
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
VIOSOCK_STATE
VIOSockStateSet(
    IN PSOCKET_CONTEXT pSocket,
    IN VIOSOCK_STATE   NewState
);

#define VIOSockStateGet(s) ((s)->State)
//////////////////////////////////////////////////////////////////////////
//Tx functions

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

#endif /* VIOSOCK_H */
