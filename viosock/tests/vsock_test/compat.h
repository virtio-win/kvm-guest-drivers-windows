/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Windows POSIX compatibility shim for vsock tests.
 *
 * Include this header instead of all Linux-specific headers.
 *
 * Two variants share the test bodies:
 *   posix - compat_* functions defined in compat.c, wired through
 *           ops_posix; macros in this header route socket/send/... to
 *           the current g_ops table.
 *   wsa   - wsa_* functions defined in wsa.c, wired through ops_wsa.
 *
 * Translation units that IMPLEMENT the shim (compat.c, wsa.c) must
 * #define COMPAT_IMPL before including this header so the macro
 * redirections at the bottom don't rewrite their own bodies.
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

#include "..\\..\\inc\\vio_sockets.h"
#include "sock_ops.h"

/* Fallback for an upstream vio_sockets.h that doesn't yet expose
 * SIO_VSOCK_OUTQ (the vsock analog of Linux SIOCOUTQ, WSAIoctl'd through
 * the LSP).  The IOCTL code stays the same; on drivers that don't
 * implement it, WSAIoctl fails at runtime and the SIOCOUTQ test skips
 * gracefully with "not supported". */
#ifndef SIO_VSOCK_OUTQ
#define SIO_VSOCK_OUTQ _WSAIOR(IOC_VENDOR, 1)
#endif

/* ------------------------------------------------------------------ */
/* Type compatibility                                                   */
/* ------------------------------------------------------------------ */

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

/* Pure translation: WSA* code -> POSIX errno. Use when the WSA value is
 * carried in an out-parameter (SO_ERROR, WSAEnumNetworkEvents.iErrorCode[],
 * WSAOVERLAPPED.Internal) rather than in thread-local WSAGetLastError. */
int wsa_to_errno(int wsa_err);

/* Same table, applied to the current thread's WSAGetLastError. */
void wsa_set_errno(void);

/* ------------------------------------------------------------------ */
/* Variant-selectable socket surface (definitions in compat.c/wsa.c;   */
/* dispatch via ops_posix / ops_wsa through g_ops).                    */
/* ------------------------------------------------------------------ */

int compat_socket(int af, int type, int proto);
int compat_connect(int fd, const struct sockaddr *addr, socklen_t len);
int compat_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t compat_send(int fd, const void *buf, size_t len, int flags);
ssize_t compat_recv(int fd, void *buf, size_t len, int flags);
ssize_t compat_read(int fd, void *buf, size_t len);
int compat_closesocket(int fd);

/* ------------------------------------------------------------------ */
/* Variant-invariant socket wrappers (same behaviour in both variants) */
/* ------------------------------------------------------------------ */

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
/* ioctl: map Linux SIOCINQ/SIOCOUTQ to their Windows/vsock equivalents */
/* ------------------------------------------------------------------ */

#define SIOCOUTQ 0x5411
#define SIOCINQ  0x541B

static inline int compat_ioctl(int fd, unsigned long op, void *arg)
{
    /* SIOCINQ (bytes available to read) is Winsock FIONREAD. */
    if (op == SIOCINQ)
    {
        u_long val = 0;
        if (ioctlsocket((SOCKET)fd, FIONREAD, &val) != 0)
        {
            wsa_set_errno();
            return -1;
        }
        *(int *)arg = (int)val;
        return 0;
    }

    /* SIOCOUTQ (unsent/unacknowledged bytes) has no Winsock equivalent; the
     * viosock driver exposes the vsock analog SIO_VSOCK_OUTQ via WSAIoctl. */
    if (op == SIOCOUTQ)
    {
        u_long val = 0;
        DWORD cb = 0;
        if (WSAIoctl((SOCKET)fd, SIO_VSOCK_OUTQ, NULL, 0, &val, sizeof(val), &cb, NULL, NULL) != 0)
        {
            wsa_set_errno();
            return -1;
        }
        *(int *)arg = (int)val;
        return 0;
    }

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
/* Macro redirections (MUST be last -- after all inline definitions).  */
/*                                                                     */
/* Variant-affected calls (socket/connect/accept/send/recv/read/close/ */
/* poll) route through g_ops so tests binding a --variant switch pick  */
/* up the matching implementation at run time.  Variant-invariant      */
/* calls (bind/listen/shutdown/getsockopt/setsockopt/fcntl/ioctl) go   */
/* straight to their compat_* inline wrappers.                         */
/*                                                                     */
/* Skipped when COMPAT_IMPL is defined - compat.c and wsa.c set that   */
/* so the macros don't rewrite the very calls they are implementing.   */
/* ------------------------------------------------------------------ */

#ifndef COMPAT_IMPL

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

#define socket(af, t, p)                g_ops->sock_socket(af, t, p)
#define connect(fd, addr, len)          g_ops->sock_connect(fd, addr, len)
#define accept(fd, addr, alen)          g_ops->sock_accept(fd, addr, alen)
#define send(fd, buf, len, fl)          g_ops->sock_send(fd, buf, len, fl)
#define recv(fd, buf, len, fl)          g_ops->sock_recv(fd, buf, len, fl)
#define read(fd, buf, len)              g_ops->sock_read(fd, buf, len)
#define close(fd)                       g_ops->sock_close(fd)
#define poll(fds, n, t)                 g_ops->sock_poll((WSAPOLLFD *)(fds), (ULONG)(n), (INT)(t))
#define pollfd                          WSAPOLLFD

#define bind(fd, addr, len)             compat_bind(fd, addr, len)
#define listen(fd, bl)                  compat_listen(fd, bl)
#define getsockname(fd, addr, alen)     compat_getsockname(fd, addr, alen)
#define setsockopt(fd, lv, nm, v, l)    compat_setsockopt(fd, lv, nm, v, l)
#define getsockopt(fd, lv, nm, v, l)    compat_getsockopt(fd, lv, nm, v, l)
#define shutdown(fd, how)               compat_shutdown(fd, how)
#define mmap(addr, len, p, fl, fd, off) compat_mmap(addr, len, p, fl, fd, off)
#define munmap(addr, len)               compat_munmap(addr, len)
#define fcntl                           compat_fcntl
#define ioctl(fd, op, arg)              compat_ioctl(fd, op, arg)
#define sigaction(sig, act, oact)       compat_sigaction(sig, act, oact)
#define signal(sig, h)                  compat_signal(sig, h)

#endif /* !COMPAT_IMPL */

#endif /* COMPAT_H */
