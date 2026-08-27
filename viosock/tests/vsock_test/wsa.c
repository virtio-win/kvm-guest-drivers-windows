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

    DWORD sent = 0;
    int r = WSASend((SOCKET)fd, wb, count, &sent, (DWORD)flags, NULL, NULL);
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
 * poll() in the wsa variant is built on WSAEventSelect +
 * WSAWaitForMultipleEvents + WSAEnumNetworkEvents.  Each fd is
 * associated with a WSAEVENT tuned to the mask derived from
 * fds[i].events; WSAWaitForMultipleEvents parks on the pack; on
 * wake-up (or timeout) WSAEnumNetworkEvents drains the pending
 * FD_* bits per fd, which are mapped back to poll's revents.
 *
 * Winsock keeps the socket in non-blocking mode as long as an
 * event-select association is active, so the cleanup path
 * dissociates every fd (WSAEventSelect(fd, NULL, 0)) and then
 * puts the socket back into blocking mode via ioctlsocket
 * FIONBIO to keep this call transparent to the test bodies.
 *
 * Negative-input semantics mirror WSAPoll's docs (fds=NULL ->
 * WSAEFAULT, nfds==0 -> WSAEINVAL) so the validate suite can
 * probe the same failure branches without special-casing.
 */
static int wsa_poll(WSAPOLLFD *fds, ULONG nfds, INT timeout)
{
    if (fds == NULL)
    {
        WSASetLastError(WSAEFAULT);
        wsa_set_errno();
        return -1;
    }
    if (nfds == 0 || nfds > WSA_MAXIMUM_WAIT_EVENTS)
    {
        WSASetLastError(WSAEINVAL);
        wsa_set_errno();
        return -1;
    }

    WSAEVENT events[WSA_MAXIMUM_WAIT_EVENTS];
    long masks[WSA_MAXIMUM_WAIT_EVENTS];
    ULONG i;

    for (i = 0; i < nfds; ++i)
    {
        events[i] = WSA_INVALID_EVENT;
        masks[i] = 0;
        fds[i].revents = 0;

        if (fds[i].fd == INVALID_SOCKET)
            continue;

        if (fds[i].events & (POLLIN | POLLRDNORM))
            masks[i] |= FD_READ | FD_ACCEPT | FD_CLOSE;
        if (fds[i].events & POLLRDBAND)
            masks[i] |= FD_OOB;
        if (fds[i].events & (POLLOUT | POLLWRNORM))
            masks[i] |= FD_WRITE | FD_CONNECT;
    }

    int result = -1;
    int saved_wsa = 0;

    for (i = 0; i < nfds; ++i)
    {
        if (fds[i].fd == INVALID_SOCKET)
            continue;
        events[i] = WSACreateEvent();
        if (events[i] == WSA_INVALID_EVENT)
        {
            saved_wsa = WSAGetLastError();
            goto cleanup;
        }
        if (WSAEventSelect((SOCKET)fds[i].fd, events[i], masks[i]) == SOCKET_ERROR)
        {
            saved_wsa = WSAGetLastError();
            goto cleanup;
        }
    }

    WSAEVENT pack[WSA_MAXIMUM_WAIT_EVENTS];
    ULONG pack_n = 0;
    for (i = 0; i < nfds; ++i)
    {
        if (events[i] != WSA_INVALID_EVENT)
            pack[pack_n++] = events[i];
    }

    if (pack_n == 0)
    {
        /* All fds were INVALID_SOCKET; WSAPoll would return 0. */
        result = 0;
        goto cleanup;
    }

    DWORD dw_timeout = (timeout < 0) ? WSA_INFINITE : (DWORD)timeout;
    DWORD wr = WSAWaitForMultipleEvents(pack_n, pack, FALSE, dw_timeout, FALSE);

    if (wr == WSA_WAIT_FAILED)
    {
        saved_wsa = WSAGetLastError();
        goto cleanup;
    }

    int ready = 0;
    if (wr != WSA_WAIT_TIMEOUT)
    {
        for (i = 0; i < nfds; ++i)
        {
            if (events[i] == WSA_INVALID_EVENT)
                continue;
            WSANETWORKEVENTS ne;
            if (WSAEnumNetworkEvents((SOCKET)fds[i].fd, events[i], &ne) == SOCKET_ERROR)
                continue;

            SHORT revents = 0;
            if (ne.lNetworkEvents & (FD_READ | FD_ACCEPT))
                revents |= (SHORT)(fds[i].events & (POLLIN | POLLRDNORM));
            if (ne.lNetworkEvents & FD_OOB)
                revents |= (SHORT)(fds[i].events & POLLRDBAND);
            if (ne.lNetworkEvents & (FD_WRITE | FD_CONNECT))
                revents |= (SHORT)(fds[i].events & (POLLOUT | POLLWRNORM));
            if (ne.lNetworkEvents & FD_CLOSE)
                revents |= POLLHUP;

            fds[i].revents = revents;
            if (revents)
                ++ready;
        }
    }

    result = ready;

cleanup:
    for (i = 0; i < nfds; ++i)
    {
        if (events[i] != WSA_INVALID_EVENT)
        {
            /* Dissociate; also flip the socket back to blocking so the
             * caller does not have to know that we touched FIONBIO. */
            WSAEventSelect((SOCKET)fds[i].fd, NULL, 0);
            u_long nb = 0;
            ioctlsocket((SOCKET)fds[i].fd, FIONBIO, &nb);
            WSACloseEvent(events[i]);
        }
    }

    if (result < 0)
    {
        WSASetLastError(saved_wsa);
        wsa_set_errno();
    }
    return result;
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
