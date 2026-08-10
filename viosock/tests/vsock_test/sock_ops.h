/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Runtime-selectable socket API surface for vsock_test.
 *
 * Two variants share the same test bodies:
 *   posix - POSIX shim in compat.c (socket/send/recv/... routed through
 *           the CRT wrappers over Winsock2).
 *   wsa   - native Winsock2 in wsa.c (WSASocket/WSASend/WSARecv with
 *           WSABUF[1], WSAConnect/WSAAccept, select() for wait).
 *
 * Only functions whose implementation differs between the two variants
 * live in this table.  bind/listen/shutdown/getsockname/setsockopt/
 * getsockopt/fcntl/ioctl/mmap/munmap are Winsock-native in both
 * variants and keep their direct compat_* macro redirections in
 * compat.h.
 */

#pragma once
#ifndef SOCK_OPS_H
#define SOCK_OPS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif

struct sock_ops
{
    int (*sock_socket)(int af, int type, int proto);
    int (*sock_connect)(int fd, const struct sockaddr *addr, socklen_t len);
    int (*sock_accept)(int fd, struct sockaddr *addr, socklen_t *len);
    ssize_t (*sock_send)(int fd, const void *buf, size_t len, int flags);
    ssize_t (*sock_recv)(int fd, void *buf, size_t len, int flags);
    ssize_t (*sock_read)(int fd, void *buf, size_t len);
    int (*sock_close)(int fd);
    int (*sock_poll)(WSAPOLLFD *fds, ULONG nfds, INT timeout);
};

extern const struct sock_ops ops_posix;
extern const struct sock_ops ops_wsa;
extern const struct sock_ops *g_ops;

int sock_ops_select(const char *variant);

#endif /* SOCK_OPS_H */
