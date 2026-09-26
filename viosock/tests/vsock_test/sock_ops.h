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
extern const struct sock_ops ops_overlapped;
extern const struct sock_ops *g_ops;

int sock_ops_select(const char *variant);

/*
 * Negative-path smoke checks per variant.  Each variant tests a
 * different slice of the surface; total coverage is the union of
 * whatever the caller runs.  Both are fail-fast: on mismatch the
 * binary exits(EXIT_FAILURE) with a single diagnostic line so a
 * regression in the LSP/driver surfaces before any test body runs.
 *
 *  posix_validate_all - Winsock/LSP semantic reachable from the
 *      POSIX shim path: GetFileType, _open_osfhandle, unbound
 *      getsockname, short-buffer SO_PROTOCOL_INFOW.  Called from
 *      sock_ops_select("posix").
 *
 *  wsa_validate_all   - documented-invalid inputs to the six
 *      variant-selectable WSA functions.  Called from
 *      sock_ops_select("wsa").
 */
void posix_validate_all(void);
void wsa_validate_all(void);

/*
 * Positive-path targeted checks split by variant so each side of
 * the dispatch table exercises exactly its own primitive:
 *   posix_events_all - WSPSelect (readfds/writefds/exceptfds via
 *       native select()).  Called from main() after "posix" is
 *       selected and opts.peer_cid is known.
 *   wsa_events_all   - WSAEventSelect (FD_* mask + WSAWaitFor* +
 *       WSAEnumNetworkEvents).  Called from main() after "wsa"
 *       is selected and opts.peer_cid is known.
 *
 * Both use the guest's own CID (passed in from --peer-cid) to form
 * in-process connected pairs; a variant-native self-loopback probe
 * gates the pair-based helpers so a routing gap in the guest does
 * not hang the smoke.
 */
void posix_events_all(unsigned int self_cid);
void wsa_events_all(unsigned int self_cid);

/*
 * WSA-variant primitives shared with wsa_events.c so its probes hit
 * the same LSP entries as the rest of the WSA-variant surface. All
 * three return the native Winsock indicator (INVALID_SOCKET / socket
 * SOCKET / SOCKET_ERROR) without touching errno — the caller uses
 * WSAGetLastError().
 *
 *  wsa_socket_new  — WSASocketW with dwFlags==0 (no WSA_FLAG_OVERLAPPED,
 *                    unlike the CRT `socket()`).
 *  wsa_connect_new — WSAConnect with no QoS / callerdata / calleedata.
 *  wsa_accept_new  — WSAAccept with no condition callback / cookie.
 *  wsa_send_new    — WSASend on an LPWSABUF array (callers pass 1 or more
 *                    entries; multi-buffer is the reason to prefer this
 *                    over the CRT `send()`).
 *  wsa_recv_new    — WSARecv on an LPWSABUF array; same rationale as send.
 *
 * send/recv return bytes transferred, or -1 (SOCKET_ERROR) on failure.
 */
SOCKET wsa_socket_new(int af, int type, int proto);
int wsa_connect_new(SOCKET s, const struct sockaddr *addr, int len);
SOCKET wsa_accept_new(SOCKET s, struct sockaddr *addr, int *addrlen);
ssize_t wsa_send_new(SOCKET s, LPWSABUF wb, DWORD count, DWORD flags);
ssize_t wsa_recv_new(SOCKET s, LPWSABUF wb, DWORD count, DWORD flags);

#endif /* SOCK_OPS_H */
