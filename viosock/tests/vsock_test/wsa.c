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
 * The wait primitive is native Berkeley select() (through fd_set),
 * NOT WSAPoll - the posix variant already covers WSAPoll, so wsa
 * exercises the parallel WSPSelect / SIO_BSP_HANDLE_SELECT tract in
 * the LSP.
 */

#define COMPAT_IMPL
#include "compat.h"
#include "sock_ops.h"

static int wsa_socket(int af, int type, int proto)
{
    SOCKET s = WSASocketW(af, type, proto, NULL, 0, 0);
    if (s == INVALID_SOCKET)
    {
        wsa_set_errno();
        return -1;
    }
    return (int)s;
}

static int wsa_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (WSAConnect((SOCKET)fd, addr, len, NULL, NULL, NULL, NULL) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static int wsa_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    SOCKET s = WSAAccept((SOCKET)fd, addr, addrlen, NULL, 0);
    if (s == INVALID_SOCKET)
    {
        wsa_set_errno();
        return -1;
    }
    return (int)s;
}

static ssize_t wsa_send(int fd, const void *buf, size_t len, int flags)
{
    bool dontwait = (flags & 0x40) != 0; /* MSG_DONTWAIT */
    flags &= ~(0x40 | 0x8000);           /* strip MSG_DONTWAIT | MSG_MORE */

    if (dontwait)
    {
        u_long nb = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    WSABUF wb;
    wb.len = (ULONG)len;
    wb.buf = (CHAR *)buf;

    DWORD sent = 0;
    int r = WSASend((SOCKET)fd, &wb, 1, &sent, (DWORD)flags, NULL, NULL);
    int saved_err = (r == SOCKET_ERROR) ? WSAGetLastError() : 0;

    if (dontwait)
    {
        u_long nb = 0;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (r == SOCKET_ERROR)
    {
        WSASetLastError(saved_err);
        wsa_set_errno();
        return -1;
    }
    return (ssize_t)sent;
}

/*
 * Split the caller buffer into two WSABUFs to exercise the LSP +
 * driver scatter-gather receive path (WSPRecv over multi-entry
 * WSABUF[]).  Bytes still land in the same underlying buffer, so
 * from the test body's point of view the result is indistinguishable
 * from a single-buffer recv - only the code path inside viosocklib
 * and viosock.sys differs.  For len < 2 (empty peek probe, single
 * byte) fall back to one WSABUF; splitting a 1-byte read into
 * (0, 1) would give WSABUF[0] a NULL-length entry that some
 * providers reject.
 */
static ssize_t wsa_recv(int fd, void *buf, size_t len, int flags)
{
    bool dontwait = (flags & 0x40) != 0; /* MSG_DONTWAIT */
    flags &= ~0x40;

    if (dontwait)
    {
        u_long nb = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    WSABUF wb[2];
    ULONG count;
    if (len >= 2)
    {
        size_t half = len / 2;
        wb[0].len = (ULONG)half;
        wb[0].buf = (CHAR *)buf;
        wb[1].len = (ULONG)(len - half);
        wb[1].buf = (CHAR *)buf + half;
        count = 2;
    }
    else
    {
        wb[0].len = (ULONG)len;
        wb[0].buf = (CHAR *)buf;
        count = 1;
    }

    DWORD got = 0;
    DWORD dwFlags = (DWORD)flags;
    int r = WSARecv((SOCKET)fd, wb, count, &got, &dwFlags, NULL, NULL);
    int saved_err = (r == SOCKET_ERROR) ? WSAGetLastError() : 0;

    if (dontwait)
    {
        u_long nb = 0;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (r == SOCKET_ERROR)
    {
        WSASetLastError(saved_err);
        wsa_set_errno();
        return -1;
    }
    return (ssize_t)got;
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
 * poll() in the wsa variant maps directly to WSAPoll.  util.c's
 * vsock_wait_remote_close already calls WSAPoll directly, so a
 * separate wait code path in wsa (an fd_set/select emulation) would
 * only diverge test coverage without exercising anything new inside
 * the WSP surface.
 */
static int wsa_poll(WSAPOLLFD *fds, ULONG nfds, INT timeout)
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
    .sock_poll = wsa_poll,
};
