/*
 * Public include file
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
#if !defined(PUBLIC_H)
#define PUBLIC_H

#define  VIOSOCK_NAME L"\\??\\Viosock"
#define  VIOSOCK_SYMLINK_NAME L"\\DosDevices\\Viosock"

 // {6B58DC1F-01C3-440F-BE1C-B95D000F1FF5}
DEFINE_GUID(GUID_DEVINTERFACE_VIOSOCK,
    0x6b58dc1f, 0x1c3, 0x440f, 0xbe, 0x1c, 0xb9, 0x5d, 0x0, 0xf, 0x1f, 0xf5);

#define FILE_DEVICE_SOCKET      0x0801

#define DEFINE_SOCKET_IOCTL(Function)   CTL_CODE(FILE_DEVICE_SOCKET, 0x800|(Function), METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEFINE_DEVICE_IOCTL(Function)   CTL_CODE(FILE_DEVICE_SOCKET, 0xC00|(Function), METHOD_BUFFERED, FILE_ANY_ACCESS)

//device ioctls
#define IOCTL_GET_CONFIG                DEFINE_DEVICE_IOCTL(1)
#define IOCTL_SELECT                    DEFINE_DEVICE_IOCTL(2)

//socket ioctls
#define IOCTL_SOCKET_BIND               DEFINE_SOCKET_IOCTL(1)
#define IOCTL_SOCKET_CONNECT            DEFINE_SOCKET_IOCTL(2)
#define IOCTL_SOCKET_READ               CTL_CODE(FILE_DEVICE_SOCKET, 0x800|(3), METHOD_OUT_DIRECT, FILE_READ_ACCESS)
#define IOCTL_SOCKET_SHUTDOWN           DEFINE_SOCKET_IOCTL(4)
#define IOCTL_SOCKET_LISTEN             DEFINE_SOCKET_IOCTL(5)
#define IOCTL_SOCKET_ENUM_NET_EVENTS    DEFINE_SOCKET_IOCTL(6)
#define IOCTL_SOCKET_EVENT_SELECT       DEFINE_SOCKET_IOCTL(7)
#define IOCTL_SOCKET_GET_PEER_NAME      DEFINE_SOCKET_IOCTL(8)
#define IOCTL_SOCKET_GET_SOCK_NAME      DEFINE_SOCKET_IOCTL(9)
#define IOCTL_SOCKET_GET_SOCK_OPT       DEFINE_SOCKET_IOCTL(10)
#define IOCTL_SOCKET_SET_SOCK_OPT       DEFINE_SOCKET_IOCTL(11)
#define IOCTL_SOCKET_IOCTL              DEFINE_SOCKET_IOCTL(12)

typedef struct _VIRTIO_VSOCK_CONFIG {
    ULONG32 guest_cid;
}VIRTIO_VSOCK_CONFIG, *PVIRTIO_VSOCK_CONFIG;

#define VIRTIO_VSOCK_HOST_CID   2

typedef enum _VIRTIO_VSOCK_TYPE {
    VIRTIO_VSOCK_TYPE_STREAM = 1,
}VIRTIO_VSOCK_TYPE;

typedef struct _VIRTIO_VSOCK_PARAMS {
    ULONGLONG           Socket;
    VIRTIO_VSOCK_TYPE   Type;
}VIRTIO_VSOCK_PARAMS, *PVIRTIO_VSOCK_PARAMS;

#ifndef MSG_PEEK
#define MSG_PEEK        0x2
#endif

#ifndef MSG_WAITALL
#define MSG_WAITALL     0x8
#endif

typedef struct _VIRTIO_VSOCK_READ_PARAMS
{
    ULONG   Flags;
}VIRTIO_VSOCK_READ_PARAMS,*PVIRTIO_VSOCK_READ_PARAMS;

//microsecs to 100-nanosec intervals
#define USEC_TO_NANO(us) ((us) * 10)
//millisecs to 100-nanosec intervals
#define MSEC_TO_NANO(ms) (USEC_TO_NANO(ms) * 1000)
//secs to 100-nanosec intervals
#define SEC_TO_NANO(s) (MSEC_TO_NANO(s) * 1000)

#ifndef FD_MAX_EVENTS
#define FD_MAX_EVENTS    10
#endif

#ifndef FD_ALL_EVENTS
#define FD_ALL_EVENTS    ((1 << FD_MAX_EVENTS) - 1)
#endif

typedef struct _VIRTIO_VSOCK_NETWORK_EVENTS {
    ULONG    NetworkEvents;
    NTSTATUS Status[FD_MAX_EVENTS];
} VIRTIO_VSOCK_NETWORK_EVENTS, *PVIRTIO_VSOCK_NETWORK_EVENTS;

typedef struct _VIRTIO_VSOCK_EVENT_SELECT {
    ULONGLONG   hEventObject;
    long        lNetworkEvents;
}VIRTIO_VSOCK_EVENT_SELECT, *PVIRTIO_VSOCK_EVENT_SELECT;

typedef struct _VIRTIO_VSOCK_OPT {
    int         level;
    int         optname;
    ULONGLONG   optval;
    int         optlen;
}VIRTIO_VSOCK_OPT, *PVIRTIO_VSOCK_OPT;

typedef struct _VIRTIO_VSOCK_IOCTL_IN {
    ULONG       dwIoControlCode;
    ULONG       cbInBuffer;
    ULONGLONG   lpvInBuffer;
}VIRTIO_VSOCK_IOCTL_IN, *PVIRTIO_VSOCK_IOCTL_IN;

#ifndef FD_SETSIZE
#define FD_SETSIZE      64
#endif /* FD_SETSIZE */

typedef struct _VIRTIO_VSOCK_FD_SET {
    UINT        fd_count;               /* how many are SET? */
    ULONGLONG   fd_array[FD_SETSIZE];   /* an array of SOCKETs */
} VIRTIO_VSOCK_FD_SET, *PVIRTIO_VSOCK_FD_SET;

typedef struct _VIRTIO_VSOCK_SELECT {
    VIRTIO_VSOCK_FD_SET ReadFds;
    VIRTIO_VSOCK_FD_SET WriteFds;
    VIRTIO_VSOCK_FD_SET ExceptFds;
    LARGE_INTEGER       Timeout;
}VIRTIO_VSOCK_SELECT, *PVIRTIO_VSOCK_SELECT;


#endif /* PUBLIC_H */
