/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * POSIX shim used by the `posix` variant of vsock_test.
 *
 * These are the pieces that used to live as static-inline in compat.h;
 * they were split out so the sock_ops function pointers in
 * `ops_posix` can address them, and so the `wsa` variant can share
 * `wsa_set_errno` without pulling all wrappers into every translation
 * unit.
 */

#define COMPAT_IMPL
#include "compat.h"
#include "sock_ops.h"

void wsa_set_errno(void)
{
    switch (WSAGetLastError())
    {
        case WSAEWOULDBLOCK:
            errno = EAGAIN;
            break;
        /* SO_RCVTIMEO expiry: map to EAGAIN to match Linux POSIX behavior */
        case WSAETIMEDOUT:
            errno = EAGAIN;
            break;
        case WSAEINPROGRESS:
            errno = EINPROGRESS;
            break;
        case WSAEALREADY:
            errno = EALREADY;
            break;
        case WSAENOTSOCK:
            errno = ENOTSOCK;
            break;
        case WSAEDESTADDRREQ:
            errno = EDESTADDRREQ;
            break;
        case WSAEMSGSIZE:
            errno = EMSGSIZE;
            break;
        case WSAEPROTOTYPE:
            errno = EPROTOTYPE;
            break;
        case WSAENOPROTOOPT:
            errno = ENOPROTOOPT;
            break;
        case WSAEPROTONOSUPPORT:
            errno = EPROTONOSUPPORT;
            break;
        case WSAEOPNOTSUPP:
            errno = EOPNOTSUPP;
            break;
        case WSAEAFNOSUPPORT:
            errno = EAFNOSUPPORT;
            break;
        case WSAEADDRINUSE:
            errno = EADDRINUSE;
            break;
        case WSAEADDRNOTAVAIL:
            errno = EADDRNOTAVAIL;
            break;
        case WSAENETDOWN:
            errno = ENETDOWN;
            break;
        case WSAENETUNREACH:
            errno = ENETUNREACH;
            break;
        case WSAENETRESET:
            errno = ENETRESET;
            break;
        case WSAECONNABORTED:
            errno = ECONNABORTED;
            break;
        case WSAECONNRESET:
            errno = ECONNRESET;
            break;
        case WSAESHUTDOWN:
            errno = EPIPE;
            break;
        case WSAENOBUFS:
            errno = ENOBUFS;
            break;
        case WSAEISCONN:
            errno = EISCONN;
            break;
        case WSAENOTCONN:
            errno = ENOTCONN;
            break;
        case WSAECONNREFUSED:
            errno = ECONNREFUSED;
            break;
        case WSAEHOSTUNREACH:
            errno = EHOSTUNREACH;
            break;
        case WSAEINTR:
            errno = EINTR;
            break;
        case WSAEFAULT:
            errno = EFAULT;
            break;
        case 0:
            break;
        default:
            errno = WSAGetLastError();
            break;
    }
}

int compat_socket(int af, int type, int proto)
{
    SOCKET s = socket(af, type, proto);
    if (s == INVALID_SOCKET)
    {
        wsa_set_errno();
        return -1;
    }
    return (int)s;
}

int compat_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (connect((SOCKET)fd, addr, len) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

int compat_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    SOCKET s = accept((SOCKET)fd, addr, addrlen);
    if (s == INVALID_SOCKET)
    {
        wsa_set_errno();
        return -1;
    }
    return (int)s;
}

ssize_t compat_send(int fd, const void *buf, size_t len, int flags)
{
    bool dontwait = (flags & 0x40) != 0; /* MSG_DONTWAIT */
    /* Strip flags that Winsock2 doesn't know */
    flags &= ~(0x40 | 0x8000); /* MSG_DONTWAIT | MSG_MORE */
    /* MSG_NOSIGNAL is 0 on Windows (no SIGPIPE) */

    if (dontwait)
    {
        u_long nb = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    int r = send((SOCKET)fd, (const char *)buf, (int)len, flags);
    /* Capture WSAGetLastError() BEFORE the FIONBIO reset below - a
     * successful ioctlsocket clears the per-thread error and would
     * leave wsa_set_errno() seeing 0, producing perror("send") ==
     * "send: No error". */
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
    return r;
}

ssize_t compat_recv(int fd, void *buf, size_t len, int flags)
{
    bool dontwait = (flags & 0x40) != 0; /* MSG_DONTWAIT */
    flags &= ~0x40;

    if (dontwait)
    {
        u_long nb = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    int r = recv((SOCKET)fd, (char *)buf, (int)len, flags);
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
    return r;
}

ssize_t compat_read(int fd, void *buf, size_t len)
{
    return compat_recv(fd, buf, len, 0);
}

int compat_closesocket(int fd)
{
    if (closesocket((SOCKET)fd) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static int compat_poll(WSAPOLLFD *fds, ULONG nfds, INT timeout)
{
    return WSAPoll(fds, nfds, timeout);
}

const struct sock_ops ops_posix = {
    .sock_socket = compat_socket,
    .sock_connect = compat_connect,
    .sock_accept = compat_accept,
    .sock_send = compat_send,
    .sock_recv = compat_recv,
    .sock_read = compat_read,
    .sock_close = compat_closesocket,
    .sock_poll = compat_poll,
};

const struct sock_ops *g_ops = &ops_posix;

int sock_ops_select(const char *variant)
{
    if (variant == NULL || strcmp(variant, "posix") == 0)
    {
        g_ops = &ops_posix;
        posix_validate_all();
        return 0;
    }
    if (strcmp(variant, "wsa") == 0)
    {
        g_ops = &ops_wsa;
        wsa_validate_all();
        return 0;
    }
    return -1;
}
