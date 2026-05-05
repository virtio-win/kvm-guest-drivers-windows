/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Windows POSIX compatibility shim for vsock tests.
 *
 * Include this header instead of all Linux-specific headers.
 * All inline wrappers are defined BEFORE the macro redirections so the
 * wrappers themselves still call the real Winsock2 functions (macros are
 * not yet visible at that point in the translation unit).
 */

#pragma once
#ifndef COMPAT_H
#define COMPAT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>  /* sig_atomic_t, SIG_DFL, SIG_IGN */
#include <process.h> /* _getpid() */

#include "..\\inc\\vio_sockets.h"

/* ------------------------------------------------------------------ */
/* Type compatibility                                                   */
/* ------------------------------------------------------------------ */

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif

typedef unsigned int useconds_t;

/* struct timeval is defined by Winsock2, but uses 32-bit long on Windows. */

struct iovec
{
    void *iov_base;
    size_t iov_len;
};

/* msghdr stub -- only needed for compilation of skipped test functions. */
struct msghdr
{
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    int msg_iovlen;
    void *msg_control;
    socklen_t msg_controllen;
    int msg_flags;
};

/* ------------------------------------------------------------------ */
/* AF_VSOCK: obtained at runtime via ViosockGetAF()                    */
/* ------------------------------------------------------------------ */

extern ADDRESS_FAMILY g_vsock_af;

/* ------------------------------------------------------------------ */
/* WSA error -> errno mapping                                           */
/* ------------------------------------------------------------------ */

static inline void wsa_set_errno(void)
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

/* ------------------------------------------------------------------ */
/* Socket wrapper inline functions (defined BEFORE the macros so they  */
/* call the real Winsock2 functions, not themselves recursively).       */
/* ------------------------------------------------------------------ */

static inline int compat_socket(int af, int type, int proto)
{
    SOCKET s = socket(af, type, proto);
    if (s == INVALID_SOCKET)
    {
        wsa_set_errno();
        return -1;
    }
    return (int)s;
}

static inline int compat_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (connect((SOCKET)fd, addr, len) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static inline int compat_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (bind((SOCKET)fd, addr, len) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static inline int compat_listen(int fd, int backlog)
{
    if (listen((SOCKET)fd, backlog) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static inline int compat_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    SOCKET s = accept((SOCKET)fd, addr, addrlen);
    if (s == INVALID_SOCKET)
    {
        wsa_set_errno();
        return -1;
    }
    return (int)s;
}

static inline ssize_t compat_send(int fd, const void *buf, size_t len, int flags)
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

    if (dontwait)
    {
        u_long nb = 0;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (r == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return r;
}

static inline ssize_t compat_recv(int fd, void *buf, size_t len, int flags)
{
    bool dontwait = (flags & 0x40) != 0; /* MSG_DONTWAIT */
    flags &= ~0x40;

    if (dontwait)
    {
        u_long nb = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    int r = recv((SOCKET)fd, (char *)buf, (int)len, flags);

    if (dontwait)
    {
        u_long nb = 0;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (r == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return r;
}

static inline ssize_t compat_read(int fd, void *buf, size_t len)
{
    return compat_recv(fd, buf, len, 0);
}

static inline int compat_getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (getsockname((SOCKET)fd, addr, addrlen) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static inline int compat_setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (setsockopt((SOCKET)fd, level, optname, (const char *)optval, optlen) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static inline int compat_getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (getsockopt((SOCKET)fd, level, optname, (char *)optval, optlen) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static inline int compat_shutdown(int fd, int how)
{
    if (shutdown((SOCKET)fd, how) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

static inline int compat_closesocket(int fd)
{
    if (closesocket((SOCKET)fd) == SOCKET_ERROR)
    {
        wsa_set_errno();
        return -1;
    }
    return 0;
}

/* recvmsg/sendmsg: stubs for skipped tests (SEQPACKET msg bounds, zerocopy). */
static inline ssize_t recvmsg(int fd, struct msghdr *msg, int flags)
{
    (void)fd;
    (void)msg;
    (void)flags;
    errno = EOPNOTSUPP;
    return -1;
}
static inline ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
    (void)fd;
    (void)msg;
    (void)flags;
    errno = EOPNOTSUPP;
    return -1;
}

/* ------------------------------------------------------------------ */
/* VirtualAlloc-based mmap/munmap                                       */
/* ------------------------------------------------------------------ */

#define MAP_FAILED    ((void *)(intptr_t)-1)
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_POPULATE  0x08000
#define PROT_READ     0x1
#define PROT_WRITE    0x2

static inline void *compat_mmap(void *addr, size_t len, int prot, int flags, int fd, long off)
{
    (void)addr;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)off;
    void *p = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return p ? p : MAP_FAILED;
}

/*
 * Use MEM_DECOMMIT so partial ranges can be freed (e.g. middle page of a
 * 3-page allocation).  Decommitted pages cause STATUS_ACCESS_VIOLATION when
 * accessed, which the driver reports as WSAEFAULT -> EFAULT -- matching the
 * Linux invalid-buffer test expectation.
 */
static inline int compat_munmap(void *addr, size_t len)
{
    return VirtualFree(addr, len, MEM_DECOMMIT) ? 0 : -1;
}

/* getpagesize */
static inline int getpagesize(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwPageSize;
}

/* ------------------------------------------------------------------ */
/* fcntl O_NONBLOCK -> ioctlsocket(FIONBIO)                            */
/* ------------------------------------------------------------------ */

#define F_GETFL    3
#define F_SETFL    4
#define O_NONBLOCK 0x0004

static inline int compat_fcntl(int fd, int cmd, ...)
{
    if (cmd == F_GETFL)
    {
        return 0;
    }
    if (cmd == F_SETFL)
    {
        va_list ap;
        va_start(ap, cmd);
        int flags = va_arg(ap, int);
        va_end(ap);
        u_long nb = (flags & O_NONBLOCK) ? 1 : 0;
        return ioctlsocket((SOCKET)fd, FIONBIO, &nb) == 0 ? 0 : -1;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* ioctl: SIOCOUTQ/SIOCINQ not available; tests gracefully skip these  */
/* ------------------------------------------------------------------ */

#define SIOCOUTQ 0x5411
#define SIOCINQ  0x541B

static inline int compat_ioctl(int fd, unsigned long op, void *arg)
{
    (void)fd;
    (void)op;
    (void)arg;
    errno = EOPNOTSUPP;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Signals: SIGPIPE/SIGALRM don't exist on Windows; no-op stubs        */
/* ------------------------------------------------------------------ */

#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGALRM
#define SIGALRM 14
#endif
#ifndef SIGUSR1
#define SIGUSR1 10
#endif

struct sigaction
{
    void (*sa_handler)(int);
};

static inline int compat_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    (void)sig;
    (void)act;
    (void)oact;
    return 0;
}

static inline void (*compat_signal(int sig, void (*handler)(int)))(int)
{
    (void)sig;
    (void)handler;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* High-resolution monotonic time in nanoseconds                        */
/* ------------------------------------------------------------------ */

#define NSEC_PER_SEC 1000000000LL

static inline long long current_nsec(void)
{
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (long long)(count.QuadPart * NSEC_PER_SEC / freq.QuadPart);
}

/* ------------------------------------------------------------------ */
/* Linux-specific flags/constants not in Winsock2                       */
/* ------------------------------------------------------------------ */

#ifndef MSG_MORE
#define MSG_MORE 0x8000 /* keep original value; stripped in compat_send */
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 /* no SIGPIPE on Windows */
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
#ifndef MSG_EOR
#define MSG_EOR 0x80
#endif
#ifndef MSG_TRUNC
#define MSG_TRUNC 0x20
#endif
#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY 0x4000000
#endif
#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY 60
#endif

/* SHUT_* -> Windows SD_* */
#ifndef SHUT_RD
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif

#ifndef EPIPE
#define EPIPE 32
#endif
#ifndef EMSGSIZE
#define EMSGSIZE 90
#endif

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* POSIX types and functions missing from MSVC                          */
/* ------------------------------------------------------------------ */

typedef int pid_t;
#define getpid _getpid

typedef void (*__sighandler_t)(int);

/* kill(): used only in skipped transport-change test; always fails. */
static inline int kill(pid_t pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

/* pthread stubs: used only in skipped transport-change test. */
typedef uintptr_t pthread_t;
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
static inline int pthread_create(pthread_t *t, void *a, void *(*f)(void *), void *arg)
{
    (void)t;
    (void)a;
    (void)f;
    (void)arg;
    return 1;
}
static inline int pthread_cancel(pthread_t t)
{
    (void)t;
    return 0;
}
static inline int pthread_join(pthread_t t, void **r)
{
    (void)t;
    (void)r;
    return 0;
}
static inline int pthread_setcanceltype(int type, int *oldtype)
{
    (void)type;
    (void)oldtype;
    return 0;
}

/* SOCK_NONBLOCK: used in skipped transport-UAF test. On Windows the
 * non-blocking mode is set separately via ioctlsocket(FIONBIO). */
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0
#endif

/* CMSG_SPACE: used in skipped zerocopy test. */
#ifndef CMSG_SPACE
#define CMSG_SPACE(len) ((len) + sizeof(size_t))
#endif

/* ------------------------------------------------------------------ */
/* General utility macros                                               */
/* ------------------------------------------------------------------ */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif
#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
#endif

/* Linux bitops used by KNOWN_TRANSPORTS enum in util.h */
#define BIT(n)           (1u << (n))
#define BITS_PER_TYPE(t) (sizeof(t) * 8)

/* static_assert with optional message (C11 / MSVC) */
#if defined(_MSC_VER) && !defined(__cplusplus)
#ifndef static_assert
#define static_assert(expr, ...) _Static_assert(expr, "" __VA_ARGS__)
#endif
#endif

/* ------------------------------------------------------------------ */
/* Deadline-based timeout (replaces Linux alarm/SIGALRM)               */
/* ------------------------------------------------------------------ */

enum
{
    TIMEOUT = 10 /* seconds */
};

void sigalrm(int signo);
void timeout_begin(unsigned int seconds);
void timeout_check(const char *operation);
void timeout_end(void);
int timeout_usleep(unsigned int usec);

/* ------------------------------------------------------------------ */
/* Macro redirections (MUST be last -- after all inline definitions)    */
/* ------------------------------------------------------------------ */

#undef socket
#undef connect
#undef bind
#undef listen
#undef accept
#undef send
#undef recv
#undef getsockname
#undef setsockopt
#undef getsockopt
#undef shutdown

#define socket(af, t, p)                compat_socket(af, t, p)
#define connect(fd, addr, len)          compat_connect(fd, addr, len)
#define bind(fd, addr, len)             compat_bind(fd, addr, len)
#define listen(fd, bl)                  compat_listen(fd, bl)
#define accept(fd, addr, alen)          compat_accept(fd, addr, alen)
#define send(fd, buf, len, fl)          compat_send(fd, buf, len, fl)
#define recv(fd, buf, len, fl)          compat_recv(fd, buf, len, fl)
#define read(fd, buf, len)              compat_read(fd, buf, len)
#define getsockname(fd, addr, alen)     compat_getsockname(fd, addr, alen)
#define setsockopt(fd, lv, nm, v, l)    compat_setsockopt(fd, lv, nm, v, l)
#define getsockopt(fd, lv, nm, v, l)    compat_getsockopt(fd, lv, nm, v, l)
#define shutdown(fd, how)               compat_shutdown(fd, how)
#define close(fd)                       compat_closesocket(fd)
#define mmap(addr, len, p, fl, fd, off) compat_mmap(addr, len, p, fl, fd, off)
#define munmap(addr, len)               compat_munmap(addr, len)
#define fcntl                           compat_fcntl
#define ioctl(fd, op, arg)              compat_ioctl(fd, op, arg)
#define sigaction(sig, act, oact)       compat_sigaction(sig, act, oact)
#define signal(sig, h)                  compat_signal(sig, h)
#define poll                            WSAPoll
#define pollfd                          WSAPOLLFD

#endif /* COMPAT_H */
