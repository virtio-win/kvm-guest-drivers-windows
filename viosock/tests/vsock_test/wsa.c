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

static ssize_t wsa_recv(int fd, void *buf, size_t len, int flags)
{
    bool dontwait = (flags & 0x40) != 0; /* MSG_DONTWAIT */
    flags &= ~0x40;

    if (dontwait)
    {
        u_long nb = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    WSABUF wb;
    wb.len = (ULONG)len;
    wb.buf = (CHAR *)buf;

    DWORD got = 0;
    DWORD dwFlags = (DWORD)flags;
    int r = WSARecv((SOCKET)fd, &wb, 1, &got, &dwFlags, NULL, NULL);
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
 * poll() in the wsa variant maps to native select().  Berkeley select
 * uses fd_set and timeval; we translate the pollfd events/revents
 * bitmask on both edges.
 *
 * POLLIN/POLLRDNORM go to readfds; POLLOUT/POLLWRNORM go to writefds;
 * exceptfds always carries every fd to surface OOB / error state as
 * revents |= POLLERR/POLLHUP once select returns.
 */
static int wsa_poll(WSAPOLLFD *fds, ULONG nfds, INT timeout)
{
    fd_set rfds, wfds, efds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);

    for (ULONG i = 0; i < nfds; ++i)
    {
        fds[i].revents = 0;
        if (fds[i].fd == INVALID_SOCKET)
        {
            continue;
        }
        if (fds[i].events & (POLLRDNORM | POLLRDBAND))
        {
            FD_SET((SOCKET)fds[i].fd, &rfds);
        }
        if (fds[i].events & (POLLWRNORM | POLLWRBAND))
        {
            FD_SET((SOCKET)fds[i].fd, &wfds);
        }
        FD_SET((SOCKET)fds[i].fd, &efds);
    }

    struct timeval tv;
    struct timeval *ptv;
    if (timeout < 0)
    {
        ptv = NULL;
    }
    else
    {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        ptv = &tv;
    }

    int rc = select(0, &rfds, &wfds, &efds, ptv);
    if (rc == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    if (rc == 0)
    {
        return 0;
    }

    int ready = 0;
    for (ULONG i = 0; i < nfds; ++i)
    {
        if (fds[i].fd == INVALID_SOCKET)
        {
            continue;
        }
        SHORT re = 0;
        if (FD_ISSET((SOCKET)fds[i].fd, &rfds))
        {
            re |= POLLRDNORM;
        }
        if (FD_ISSET((SOCKET)fds[i].fd, &wfds))
        {
            re |= POLLWRNORM;
        }
        if (FD_ISSET((SOCKET)fds[i].fd, &efds))
        {
            /* select() puts a stream socket into exceptfds only for OOB
             * data or a failed non-blocking connect().  WSAPoll would
             * additionally raise POLLHUP for a peer-close, but that
             * arrives here as a POLLRDNORM+recv=0 sequence, which the
             * tests already handle. */
            re |= POLLRDBAND;
        }
        fds[i].revents = re;
        if (re)
        {
            ++ready;
        }
    }
    return ready;
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
