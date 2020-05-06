/*
 * Core definitions for virtio VSOCK address family,
 * based on linux/vm_sockets.h
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

#ifndef _VIO_SOCKETS_H
#define _VIO_SOCKETS_H

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER

#include <ws2def.h>

/*VSOCK address family value*/
#ifndef AF_VSOCK
#define AF_VSOCK    40
#endif

#ifndef PF_VSOCK
#define PF_VSOCK    AF_VSOCK
#endif

/* Option name for STREAM socket buffer size.  Use as the option name in
 * setsockopt or getsockopt to set or get an unsigned long long that
 * specifies the size of the buffer underlying a vSockets STREAM socket.
 * Value is clamped to the MIN and MAX.
 */

#define SO_VM_SOCKETS_BUFFER_SIZE       0x6000

/* Option name for STREAM socket minimum buffer size.  Use as the option name
 * in setsockopt or getsockopt to set or get an unsigned long long that
 * specifies the minimum size allowed for the buffer underlying a vSockets
 * STREAM socket.
 */

#define SO_VM_SOCKETS_BUFFER_MIN_SIZE   0x6001

 /* Option name for STREAM socket maximum buffer size.  Use as the option name
  * in setsockopt or getsockopt to set or get an unsigned long long
  * that specifies the maximum size allowed for the buffer underlying a
  * vSockets STREAM socket.
  */

#define SO_VM_SOCKETS_BUFFER_MAX_SIZE   0x6002

/* Any address  for  binding, equivalent of INADDR_ANY.  This works for the svm_cid field of
 * sockaddr_vm and indicates the context ID of the current endpoint.
 */
#define VMADDR_CID_ANY          (-1)

/* Bind to any available port.  Works for the svm_port field of sockaddr_vm. */
#define VMADDR_PORT_ANY         (-1)

/* Use this as the destination CID in an address when referring to the hypervisor.*/
#define VMADDR_CID_HYPERVISOR   0

/* Reserved, must not be used. */
#define VMADDR_CID_RESERVED     1

/* Use this as the destination CID in an address when referring to the host
 * (any process other than the hypervisor).
 */
#define VMADDR_CID_HOST         2

/* Address structure for virtio vsockets. The address family should be set to
 * AF_VSOCK.  The structure members should all align on their natural
 * boundaries without resorting to compiler packing directives.
 */
typedef struct sockaddr_vm
{
    ADDRESS_FAMILY  svm_family;     /* Address family: AF_VSOCK */
    USHORT          svm_reserved1;
    UINT            svm_port;       /* Port # in host byte order */
    UINT            svm_cid;        /* Address in host byte order */
}SOCKADDR_VM, *PSOCKADDR_VM;

#define IOCTL_VM_SOCKETS_GET_LOCAL_CID		_IO(7, 0xb9)

#endif /* _VIO_SOCKETS_H */
