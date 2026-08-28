/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Overlapped Winsock2 surface for the `overlapped` variant of
 * vsock_test.
 *
 * Same source-level API as wsa (WSASocket / WSAConnect / WSAAccept /
 * WSASend / WSARecv / closesocket) but the socket is created with
 * WSA_FLAG_OVERLAPPED and every send / recv is issued with a live
 * WSAOVERLAPPED whose hEvent we wait on via WSAGetOverlappedResult
 * (fWait=TRUE).  That routes each I/O through the LSP + driver
 * OVERLAPPED-completion tract instead of the synchronous fast path
 * covered by wsa; the test bodies themselves stay unchanged.
 *
 * connect / accept keep their synchronous WSAConnect / WSAAccept
 * form - MSDN documents no lpOverlapped parameter on WSAConnect and
 * async accept requires AcceptEx, both non-trivial reworks we do
 * not need for send/recv coverage.  poll dispatches to the same
 * WSAEventSelect wait primitive as the wsa variant.
 */

#define COMPAT_IMPL
#include "compat.h"
#include "sock_ops.h"

static int overlapped_socket(int af, int type, int proto)
{
    SOCKET s = WSASocketW(af, type, proto, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET)
    {
        wsa_set_errno();
        return -1;
    }
    return (int)s;
}

static int overlapped_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (WSAConnect((SOCKET)fd, addr, len, NULL, NULL, NULL, NULL) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static int overlapped_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    SOCKET s = WSAAccept((SOCKET)fd, addr, addrlen, NULL, 0);
    if (s == INVALID_SOCKET)
    {
        wsa_set_errno();
        return -1;
    }
    return (int)s;
}

/*
 * Issue WSASend with a live WSAOVERLAPPED, then wait for completion
 * via WSAGetOverlappedResult(fWait=TRUE).  Sender-side WSABUF split
 * mirrors wsa_send so both variants exercise the LSP's scatter-send
 * path with the same call shape.
 */
static ssize_t overlapped_send(int fd, const void *buf, size_t len, int flags)
{
    bool dontwait = (flags & 0x40) != 0; /* MSG_DONTWAIT */
    flags &= ~(0x40 | 0x8000);           /* strip MSG_DONTWAIT | MSG_MORE */

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

    WSAOVERLAPPED ov = {0};
    ov.hEvent = WSACreateEvent();
    if (ov.hEvent == WSA_INVALID_EVENT)
    {
        if (dontwait)
        {
            u_long nb = 0;
            ioctlsocket((SOCKET)fd, FIONBIO, &nb);
        }
        wsa_set_errno();
        return -1;
    }

    DWORD sent = 0;
    int r = WSASend((SOCKET)fd, wb, count, &sent, (DWORD)flags, &ov, NULL);
    int saved_err = 0;
    if (r == SOCKET_ERROR)
    {
        saved_err = WSAGetLastError();
        if (saved_err != WSA_IO_PENDING)
        {
            WSACloseEvent(ov.hEvent);
            if (dontwait)
            {
                u_long nb = 0;
                ioctlsocket((SOCKET)fd, FIONBIO, &nb);
            }
            WSASetLastError(saved_err);
            wsa_set_errno();
            return -1;
        }
    }

    DWORD bytes = 0;
    DWORD ov_flags = 0;
    BOOL ok = WSAGetOverlappedResult((SOCKET)fd, &ov, &bytes, TRUE, &ov_flags);
    saved_err = ok ? 0 : WSAGetLastError();
    WSACloseEvent(ov.hEvent);

    if (dontwait)
    {
        u_long nb = 0;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (!ok)
    {
        WSASetLastError(saved_err);
        wsa_set_errno();
        return -1;
    }
    return (ssize_t)bytes;
}

/*
 * WSARecv counterpart of overlapped_send.  Same split-buffer shape as
 * wsa_recv (WSABUF[2] for len >= 2) so the scatter-recv path is hit
 * identically.
 */
static ssize_t overlapped_recv(int fd, void *buf, size_t len, int flags)
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

    WSAOVERLAPPED ov = {0};
    ov.hEvent = WSACreateEvent();
    if (ov.hEvent == WSA_INVALID_EVENT)
    {
        if (dontwait)
        {
            u_long nb = 0;
            ioctlsocket((SOCKET)fd, FIONBIO, &nb);
        }
        wsa_set_errno();
        return -1;
    }

    DWORD got = 0;
    DWORD dwFlags = (DWORD)flags;
    int r = WSARecv((SOCKET)fd, wb, count, &got, &dwFlags, &ov, NULL);
    int saved_err = 0;
    if (r == SOCKET_ERROR)
    {
        saved_err = WSAGetLastError();
        if (saved_err != WSA_IO_PENDING)
        {
            WSACloseEvent(ov.hEvent);
            if (dontwait)
            {
                u_long nb = 0;
                ioctlsocket((SOCKET)fd, FIONBIO, &nb);
            }
            WSASetLastError(saved_err);
            wsa_set_errno();
            return -1;
        }
    }

    DWORD bytes = 0;
    DWORD ov_flags = 0;
    BOOL ok = WSAGetOverlappedResult((SOCKET)fd, &ov, &bytes, TRUE, &ov_flags);
    saved_err = ok ? 0 : WSAGetLastError();
    WSACloseEvent(ov.hEvent);

    if (dontwait)
    {
        u_long nb = 0;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (!ok)
    {
        WSASetLastError(saved_err);
        wsa_set_errno();
        return -1;
    }
    return (ssize_t)bytes;
}

static ssize_t overlapped_read(int fd, void *buf, size_t len)
{
    return overlapped_recv(fd, buf, len, 0);
}

static int overlapped_close(int fd)
{
    if (closesocket((SOCKET)fd) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

/*
 * The wsa poll primitive - WSAEventSelect + WSAWaitForMultipleEvents
 * + WSAEnumNetworkEvents - is variant-agnostic and already covers the
 * event-object dispatch.  Reuse it here instead of routing overlapped
 * through a second copy of the same code.  Local extern declaration
 * keeps the ops table self-contained without a shared header.
 */
extern int wsa_poll_dispatch(WSAPOLLFD *fds, ULONG nfds, INT timeout);

const struct sock_ops ops_overlapped = {
    .sock_socket = overlapped_socket,
    .sock_connect = overlapped_connect,
    .sock_accept = overlapped_accept,
    .sock_send = overlapped_send,
    .sock_recv = overlapped_recv,
    .sock_read = overlapped_read,
    .sock_close = overlapped_close,
    .sock_poll = wsa_poll_dispatch,
};
