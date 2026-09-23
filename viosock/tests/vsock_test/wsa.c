/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Native Winsock2 surface for the `wsa` variant of vsock_test.
 *
 * Everything here is synchronous - no WSA_FLAG_OVERLAPPED, no
 * WSAOVERLAPPED, no completion routines.  WSABUF arrays carry a
 * single entry today; the API contract with the tests is already
 * scatter-gather-shaped so a future scatter test only needs a new
 * helper, not a reshaped table.
 *
 * The wait primitive is WSAEventSelect + WSAWaitForMultipleEvents +
 * WSAEnumNetworkEvents.  posix covers WSAPoll; wsa covers the
 * parallel event-object tract in the LSP so both dispatch paths
 * stay exercised.
 */

#define COMPAT_IMPL
#include "compat.h"
#include "sock_ops.h"

SOCKET wsa_socket_new(int af, int type, int proto)
{
    return WSASocketW(af, type, proto, NULL, 0, 0);
}

int wsa_connect_new(SOCKET s, const struct sockaddr *addr, int len)
{
    return WSAConnect(s, addr, len, NULL, NULL, NULL, NULL);
}

SOCKET wsa_accept_new(SOCKET s, struct sockaddr *addr, int *addrlen)
{
    return WSAAccept(s, addr, addrlen, NULL, 0);
}

ssize_t wsa_send_new(SOCKET s, LPWSABUF wb, DWORD count, DWORD flags)
{
    DWORD sent = 0;
    if (WSASend(s, wb, count, &sent, flags, NULL, NULL) == SOCKET_ERROR)
    {
        return -1;
    }
    return (ssize_t)sent;
}

ssize_t wsa_recv_new(SOCKET s, LPWSABUF wb, DWORD count, DWORD flags)
{
    DWORD got = 0;
    DWORD f = flags;
    if (WSARecv(s, wb, count, &got, &f, NULL, NULL) == SOCKET_ERROR)
    {
        return -1;
    }
    return (ssize_t)got;
}

/* Split a flat buffer into two WSABUFs to exercise the LSP + driver
 * scatter-gather path in the wsa variant. len < 2 falls back to one
 * WSABUF (a 0-length entry would be rejected by some providers). */
static ULONG wsa_split_bufs(void *buf, size_t len, WSABUF wb[2])
{
    if (len >= 2)
    {
        size_t half = len / 2;
        wb[0].len = (ULONG)half;
        wb[0].buf = (CHAR *)buf;
        wb[1].len = (ULONG)(len - half);
        wb[1].buf = (CHAR *)buf + half;
        return 2;
    }
    wb[0].len = (ULONG)len;
    wb[0].buf = (CHAR *)buf;
    return 1;
}

static int wsa_socket(int af, int type, int proto)
{
    SOCKET s = wsa_socket_new(af, type, proto);
    if (s == INVALID_SOCKET)
    {
        wsa_set_errno();
        return -1;
    }
    return (int)s;
}

static int wsa_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (wsa_connect_new((SOCKET)fd, addr, len) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static int wsa_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    SOCKET s = wsa_accept_new((SOCKET)fd, addr, addrlen);
    if (s == INVALID_SOCKET)
    {
        wsa_set_errno();
        return -1;
    }
    return (int)s;
}

/* MSG_DONTWAIT emulation: Winsock WSASend/WSARecv have no per-call
 * non-blocking flag, so we toggle FIONBIO around the call. Not on any
 * hot path - MSG_DONTWAIT appears in a handful of vsock_test recv sites
 * only, no test issues send + MSG_DONTWAIT. */
static ssize_t wsa_send(int fd, const void *buf, size_t len, int flags)
{
    bool dontwait = (flags & MSG_DONTWAIT) != 0;
    /* Strip Linux-only flags; MSG_ZEROCOPY stays - viosocklib
     * (see vio_sockets.h) routes it to SEND_EX / MDL. */
    flags &= ~(MSG_DONTWAIT | MSG_MORE);

    if (dontwait)
    {
        u_long nb = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    WSABUF wb[2];
    ULONG count = wsa_split_bufs((void *)buf, len, wb);
    ssize_t sent = wsa_send_new((SOCKET)fd, wb, count, (DWORD)flags);
    int saved_err = (sent < 0) ? WSAGetLastError() : 0;

    if (dontwait)
    {
        u_long nb = 0;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (sent < 0)
    {
        WSASetLastError(saved_err);
        wsa_set_errno();
        return -1;
    }
    return sent;
}

/*
 * Split the caller buffer into two WSABUFs to exercise the LSP +
 * driver scatter-gather path (WSPSend/WSPRecv over multi-entry
 * WSABUF[]).  Bytes still land in the same underlying buffer, so
 * from the test body's point of view the result is indistinguishable
 * from a single-buffer call - only the code path inside viosocklib
 * and viosock.sys differs.  For len < 2 (empty peek probe, single
 * byte) fall back to one WSABUF; splitting a 1-byte read into
 * (0, 1) would give WSABUF[0] a NULL-length entry that some
 * providers reject.
 */
static ssize_t wsa_recv(int fd, void *buf, size_t len, int flags)
{
    /* MSG_DONTWAIT: see wsa_send for the FIONBIO toggle rationale. */
    bool dontwait = (flags & MSG_DONTWAIT) != 0;
    flags &= ~MSG_DONTWAIT;

    if (dontwait)
    {
        u_long nb = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    WSABUF wb[2];
    ULONG count = wsa_split_bufs(buf, len, wb);
    ssize_t got = wsa_recv_new((SOCKET)fd, wb, count, (DWORD)flags);
    int saved_err = (got < 0) ? WSAGetLastError() : 0;

    if (dontwait)
    {
        u_long nb = 0;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (got < 0)
    {
        WSASetLastError(saved_err);
        wsa_set_errno();
        return -1;
    }
    return got;
}

static ssize_t wsa_read(int fd, void *buf, size_t len)
{
    return wsa_recv(fd, buf, len, 0);
}

static int wsa_close(int fd)
{
    if (closesocket((SOCKET)fd) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

/*
 * poll() in the wsa variant is WSAPoll: the WSA-family sync poll
 * primitive.  Native WSAPoll is the piece that honours SO_RCVLOWAT
 * (edge-triggered FD_READ from WSAEventSelect does not), so tests
 * that rely on the low-water semantics must reach here.  The
 * WSAEventSelect / WSAWaitForMultipleEvents / WSAEnumNetworkEvents
 * tract is exercised separately by wsa_events.c and does not need
 * to hijack the poll dispatch.
 */
/* Exported (not static) so overlapped.c can dispatch its poll here
 * without duplicating the WSAPoll wrapper. */
int wsa_poll_dispatch(WSAPOLLFD *fds, ULONG nfds, INT timeout);
int wsa_poll_dispatch(WSAPOLLFD *fds, ULONG nfds, INT timeout)
{
    int rc = WSAPoll(fds, nfds, timeout);
    if (rc == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return rc;
}

const struct sock_ops ops_wsa = {
                                                                                                    .sock_socket = wsa_socket,
                                                                                                    .sock_connect = wsa_connect,
                                                                                                    .sock_accept = wsa_accept,
                                                                                                    .sock_send = wsa_send,
                                                                                                    .sock_recv = wsa_recv,
                                                                                                    .sock_read = wsa_read,
                                                                                                    .sock_close = wsa_close,
                                                                                                    .sock_poll = wsa_poll_dispatch,
};
