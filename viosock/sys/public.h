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

#define IOCTL_GET_CONFIG        CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)

typedef struct _VIRTIO_VSOCK_CONFIG {
    ULONG64 guest_cid;
}VIRTIO_VSOCK_CONFIG, *PVIRTIO_VSOCK_CONFIG;

#define VIRTIO_VSOCK_HOST_CID   2

// typedef enum _VIRTIO_VSOCK_EA_TYPE
// {
//     VSOCK_TYPE_NEW=0,
//     VSOCK_TYPE_ACCEPT=1,
//     VSOCK_TYPE_INVALID
// }VIRTIO_VSOCK_EA_TYPE;

typedef struct _VIRTIO_VSOCK_PARAMS {
    ULONGLONG Socket;
}VIRTIO_VSOCK_PARAMS, *PVIRTIO_VSOCK_PARAMS;

#endif /* PUBLIC_H */
