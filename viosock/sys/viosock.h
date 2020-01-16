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

EVT_WDF_DRIVER_DEVICE_ADD VIOSockEvtDeviceAdd;

EVT_WDF_INTERRUPT_ISR                           VIOSockInterruptIsr;
EVT_WDF_INTERRUPT_DPC                           VIOSockInterruptDpc;
EVT_WDF_INTERRUPT_DPC                           VIOSockWdfInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE                        VIOSockInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE                       VIOSockInterruptDisable;

EVT_WDF_DEVICE_FILE_CREATE VIOSockCreate;
EVT_WDF_FILE_CLOSE VIOSockClose;

typedef struct virtqueue VIOSOCK_VQ, *PVIOSOCK_VQ;
typedef struct VirtIOBufferDescriptor VIOSOCK_SG_DESC, *PVIOSOCK_SG_DESC;

typedef struct _DEVICE_CONTEXT {

    VIRTIO_WDF_DRIVER   VDevice;

    PVIOSOCK_VQ    RxQueue;
    PVIOSOCK_VQ    TxQueue;
    PVIOSOCK_VQ    EvtQueue;

    WDFINTERRUPT        WdfInterrupt;

    WDFCOLLECTION   SocketList;

    WDFQUEUE            IoCtlQueue;

    VIRTIO_VSOCK_CONFIG Config;
 } DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);


typedef struct _SOCKET_CONTEXT {

    ULONG64 dst_cid;
    ULONG32 src_port;
    ULONG32 dst_port;
    ULONG32 flags;
    ULONG32 buf_alloc;
    ULONG32 fwd_cnt;

    ULONG State;
    WDFFILEOBJECT ListenSocket;
    BOOLEAN IsControl;
} SOCKET_CONTEXT, *PSOCKET_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOCKET_CONTEXT, GetSocketContext);

#define VIOSOCK_DRIVER_MEMORY_TAG (ULONG)'cosV'

#define  VIOSOCK_DEVICE_NAME L"\\Device\\Viosock"

#define VIOSOCK_DMA_TRX_LEN 0x10000
#define VIOSOCK_DMA_TRX_PAGES (VIOSOCK_DMA_TRX_LEN/PAGE_SIZE+1)

#endif /* VIOSOCK_H */
