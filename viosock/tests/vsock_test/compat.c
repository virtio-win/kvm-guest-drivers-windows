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

#include <fcntl.h>
#include <io.h>

int wsa_to_errno(int wsa_err)
{
    switch (wsa_err)
    {
        case WSAEWOULDBLOCK:
            return EAGAIN;
        /* Winsock keeps WSAEWOULDBLOCK (non-blocking) and WSAETIMEDOUT
         * (SO_RCVTIMEO/SO_SNDTIMEO expiry) as distinct errors; map each
         * to its own errno.  Tests that check timeout must look for
         * ETIMEDOUT specifically (upstream POSIX uses EAGAIN there —
         * flag the divergence at the check site). */
        case WSAETIMEDOUT:
            return ETIMEDOUT;
        case WSAEINPROGRESS:
            return EINPROGRESS;
        case WSAEALREADY:
            return EALREADY;
        case WSAENOTSOCK:
            return ENOTSOCK;
        case WSAEDESTADDRREQ:
            return EDESTADDRREQ;
        case WSAEMSGSIZE:
            return EMSGSIZE;
        case WSAEPROTOTYPE:
            return EPROTOTYPE;
        case WSAENOPROTOOPT:
            return ENOPROTOOPT;
        case WSAEPROTONOSUPPORT:
            return EPROTONOSUPPORT;
        case WSAEOPNOTSUPP:
            return EOPNOTSUPP;
        case WSAEAFNOSUPPORT:
            return EAFNOSUPPORT;
        case WSAEADDRINUSE:
            return EADDRINUSE;
        case WSAEADDRNOTAVAIL:
            return EADDRNOTAVAIL;
        case WSAENETDOWN:
            return ENETDOWN;
        case WSAENETUNREACH:
            return ENETUNREACH;
        case WSAENETRESET:
            return ENETRESET;
        case WSAECONNABORTED:
            return ECONNABORTED;
        case WSAECONNRESET:
            return ECONNRESET;
        case WSAESHUTDOWN:
            return EPIPE;
        case WSAENOBUFS:
            return ENOBUFS;
        case WSAEISCONN:
            return EISCONN;
        case WSAENOTCONN:
            return ENOTCONN;
        case WSAECONNREFUSED:
            return ECONNREFUSED;
        case WSAEHOSTUNREACH:
            return EHOSTUNREACH;
        case WSAEINTR:
            return EINTR;
        case WSAEFAULT:
            return EFAULT;
        case WSAEINVAL:
            return EINVAL;
        case WSAEACCES:
            return EACCES;
        case WSAEBADF:
            return EBADF;
        case WSAEMFILE:
            return EMFILE;
        case 0:
            return 0;
        default:
            return wsa_err;
    }
}

void wsa_set_errno(void)
{
    errno = wsa_to_errno(WSAGetLastError());
}

/* --io knob shared by the posix and overlapped variants: false = Winsock
 * send / recv (default), true = file I/O on the socket handle. */
static bool g_io_file;

int sock_io_select(const char *io)
{
    if (strcmp(io, "wsa") == 0)
    {
        g_io_file = false;
        return 0;
    }
    if (strcmp(io, "file") == 0)
    {
        g_io_file = true;
        return 0;
    }
    return -1;
}

bool sock_io_is_file(void)
{
    return g_io_file;
}

/*
 * File-I/O failures surface as Win32 codes (RtlNtStatusToDosError on the
 * driver's NTSTATUS), coarser than the Winsock set: reset, peer shutdown
 * and our own shutdown all become ERROR_NETNAME_DELETED.  Map the
 * unambiguous ones here; callers re-issue the ambiguous one through the
 * Winsock call, which fails at once with the exact Winsock error.
 */
int file_error_to_wsa(DWORD err)
{
    switch (err)
    {
        case ERROR_CANT_WAIT: /* STATUS_CANT_WAIT: non-blocking socket, nothing to do yet */
            return WSAEWOULDBLOCK;
        case ERROR_CONNECTION_ABORTED:
            return WSAECONNABORTED;
        case ERROR_CONNECTION_REFUSED:
            return WSAECONNREFUSED;
        case ERROR_GRACEFUL_DISCONNECT:
            return WSAEDISCON;
        case ERROR_SEM_TIMEOUT:
            return WSAETIMEDOUT;
        case ERROR_OPERATION_ABORTED:
            return WSAEINTR;
        case ERROR_INVALID_HANDLE:
            return WSAENOTSOCK;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_NO_SYSTEM_RESOURCES:
            return WSAENOBUFS;
        case ERROR_INVALID_PARAMETER:
            return WSAEINVAL;
        case ERROR_NOACCESS:
        case ERROR_INVALID_USER_BUFFER:
            return WSAEFAULT;
        default:
            /* Unmapped: keep the raw code so it shows up in the diagnostic. */
            return (int)err;
    }
}

int compat_socket(int af, int type, int proto)
{
    /* CRT _read / _write wrap ReadFile / WriteFile with no OVERLAPPED, which
     * is defined only on a synchronous handle; socket() opens overlapped
     * ones, so --io=file asks for a synchronous socket (accept() inherits). */
    SOCKET s = g_io_file ? WSASocketW(af, type, proto, NULL, 0, 0) : socket(af, type, proto);
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

/* MSG_DONTWAIT emulation: Winsock send/recv have no per-call non-blocking
 * flag, so we toggle FIONBIO around the call and restore it after. Not on
 * any hot path - MSG_DONTWAIT appears in a handful of vsock_test recv
 * sites only, no test issues send + MSG_DONTWAIT. */
static ssize_t compat_send_wsa(int fd, const void *buf, size_t len, int flags)
{
    bool dontwait = (flags & MSG_DONTWAIT) != 0;
    /* Strip Linux-only send flags that Winsock2 does not know.
     * MSG_ZEROCOPY stays - viosocklib recognises it (see
     * vio_sockets.h) and routes the send through SEND_EX (MDL /
     * zero-copy tract).  compat.c uses the CRT `send()` which
     * forwards flags to WSASend under the hood. */
    flags &= ~(MSG_DONTWAIT | MSG_MORE);
    /* MSG_NOSIGNAL is 0 on Windows (no SIGPIPE) */

    if (dontwait)
    {
        u_long nb = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (len > INT_MAX)
    {
        len = INT_MAX;
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

static ssize_t compat_recv_wsa(int fd, void *buf, size_t len, int flags)
{
    /* MSG_DONTWAIT: see compat_send_wsa for the FIONBIO toggle rationale. */
    bool dontwait = (flags & MSG_DONTWAIT) != 0;
    flags &= ~MSG_DONTWAIT;

    if (dontwait)
    {
        u_long nb = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (len > INT_MAX)
    {
        len = INT_MAX;
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

/*
 * --io=file: an unflagged send / recv goes through a CRT fd (_open_osfhandle
 * + _write / _read), i.e. WriteFile / ReadFile on the socket handle and the
 * driver's EvtIoWrite / EvtIoRead.  The CRT fd owns its handle and _close()
 * would CloseHandle() the socket behind Winsock's back, so it gets a
 * duplicate per call and closesocket() stays the real close.
 */
static ssize_t posix_file_io(int fd, void *buf, size_t len, bool write)
{
    HANDLE dup;
    unsigned long doserr = 0;
    int crt, r;

    if (!DuplicateHandle(GetCurrentProcess(),
                         (HANDLE)(SOCKET)fd,
                         GetCurrentProcess(),
                         &dup,
                         0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS))
    {
        WSASetLastError(file_error_to_wsa(GetLastError()));
        wsa_set_errno();
        return -1;
    }
    crt = _open_osfhandle((intptr_t)dup, _O_RDWR | _O_BINARY);
    if (crt == -1)
    {
        CloseHandle(dup); /* errno is the CRT's */
        return -1;
    }

    if (len > INT_MAX)
    {
        len = INT_MAX;
    }
    r = write ? _write(crt, buf, (unsigned int)len) : _read(crt, buf, (unsigned int)len);
    if (r < 0)
    {
        _get_doserrno(&doserr);
    }
    _close(crt); /* the duplicate only */

    if (r >= 0)
    {
        return r;
    }
    /* Win32 folds reset, peer and own shutdown into 64; the socket stays in
     * that state, so a Winsock retry sends nothing and yields the exact error. */
    if (doserr == ERROR_NETNAME_DELETED)
    {
        return write ? compat_send_wsa(fd, buf, len, 0) : compat_recv_wsa(fd, buf, len, 0);
    }
    WSASetLastError(file_error_to_wsa((DWORD)doserr));
    wsa_set_errno();
    return -1;
}

ssize_t compat_send(int fd, const void *buf, size_t len, int flags)
{
    if (g_io_file && (flags & ~MSG_MORE) == 0)
    {
        return posix_file_io(fd, (void *)buf, len, true);
    }
    return compat_send_wsa(fd, buf, len, flags);
}

ssize_t compat_recv(int fd, void *buf, size_t len, int flags)
{
    if (g_io_file && flags == 0)
    {
        return posix_file_io(fd, buf, len, false);
    }
    return compat_recv_wsa(fd, buf, len, flags);
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
    int r = WSAPoll(fds, nfds, timeout);
    if (r == SOCKET_ERROR)
    {
        wsa_set_errno();
    }
    return r;
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
    if (strcmp(variant, "overlapped") == 0)
    {
        g_ops = &ops_overlapped;
        /* overlapped reuses the WSAEventSelect wait primitive and the
         * WSA-side error mapping; validation runs the same wsa smoke. */
        wsa_validate_all();
        return 0;
    }
    return -1;
}
