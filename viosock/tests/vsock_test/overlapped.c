/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Overlapped Winsock2 surface for the `overlapped` variant of
 * vsock_test.
 *
 * Same source-level API as wsa (WSASocket / WSAConnect / WSAAccept /
 * WSASend / WSARecv / closesocket) but the socket is created with
 * WSA_FLAG_OVERLAPPED and every send / recv is issued with a live
 * WSAOVERLAPPED - routing each I/O through the LSP + driver
 * OVERLAPPED-completion tract instead of the synchronous fast path
 * covered by wsa.
 *
 * Hybrid completion split: send uses an APC completion routine
 * (LPWSAOVERLAPPED_COMPLETION_ROUTINE) waited on with an alertable
 * WaitForSingleObjectEx, recv uses an event-based OVERLAPPED
 * waited on with WSAGetOverlappedResult(fWait=TRUE).  So every test
 * body exercises both async-completion tracts in the same run - the
 * APC-driven send path and the event-driven WSAGetOverlappedResult
 * recv path - without needing two separate variants.
 *
 * connect / accept keep their synchronous WSAConnect / WSAAccept
 * form - MSDN documents no lpOverlapped parameter on WSAConnect and
 * async accept requires ConnectEx / AcceptEx (VSTOR-143723 tracks
 * the LSP work needed to expose them).  poll dispatches to
 * wsa_poll_dispatch (WSAPoll), shared with wsa.
 */

#define COMPAT_IMPL
#include "compat.h"
#include "sock_ops.h"

/* Send path: APC. */
struct overlapped_apc_ctx
{
    WSAOVERLAPPED ov;
    DWORD bytes;
    DWORD err;
    HANDLE done_ev;
};

static void CALLBACK overlapped_apc_cb(DWORD dwError, DWORD cbTransferred, LPWSAOVERLAPPED lpOverlapped, DWORD dwFlags)
{
    struct overlapped_apc_ctx *c = (struct overlapped_apc_ctx *)lpOverlapped->hEvent;
    (void)dwFlags;
    c->err = dwError;
    c->bytes = cbTransferred;
    SetEvent(c->done_ev);
}

/*
 * Park in an alertable wait until the APC has fired.  Wraps the
 * WAIT_IO_COMPLETION -> re-arm loop so callers do not have to open
 * it.  Returns true on success (ctx populated), false on wait
 * failure with the WSA error stashed in ctx.err.
 */
static bool overlapped_wait_apc(struct overlapped_apc_ctx *ctx)
{
    for (;;)
    {
        DWORD dw = WaitForSingleObjectEx(ctx->done_ev, INFINITE, TRUE);
        if (dw == WAIT_OBJECT_0)
        {
            return true;
        }
        if (dw == WAIT_IO_COMPLETION)
        {
            continue;
        }
        ctx->err = WSAGetLastError();
        return false;
    }
}

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
 * Send: issue WSASend with a non-NULL lpCompletionRoutine and park
 * in an alertable wait; the APC delivers bytes/error into the
 * per-request ctx.  WSABUF split mirrors wsa_send so the LSP's
 * scatter-send path is exercised with the same call shape.
 */
/* MSG_DONTWAIT emulation: Winsock WSASend/WSARecv have no per-call
 * non-blocking flag, so we toggle FIONBIO around the call. Not on any
 * hot path - MSG_DONTWAIT appears in a handful of vsock_test recv sites
 * only, no test issues send + MSG_DONTWAIT. */
static ssize_t overlapped_send(int fd, const void *buf, size_t len, int flags)
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

    struct overlapped_apc_ctx ctx = {0};
    ctx.done_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ctx.done_ev)
    {
        if (dontwait)
        {
            u_long nb = 0;
            ioctlsocket((SOCKET)fd, FIONBIO, &nb);
        }
        wsa_set_errno();
        return -1;
    }
    /* WSASend / WSARecv with a completion routine ignore
     * lpOverlapped->hEvent for signalling; reuse the field as a ctx
     * back-pointer for the APC. */
    ctx.ov.hEvent = (HANDLE)&ctx;

    DWORD sent = 0;
    int r = WSASend((SOCKET)fd, wb, count, &sent, (DWORD)flags, &ctx.ov, overlapped_apc_cb);
    int saved_err = 0;
    if (r == SOCKET_ERROR)
    {
        saved_err = WSAGetLastError();
        if (saved_err != WSA_IO_PENDING)
        {
            CloseHandle(ctx.done_ev);
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

    bool ok = overlapped_wait_apc(&ctx);
    CloseHandle(ctx.done_ev);

    if (dontwait)
    {
        u_long nb = 0;
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
    }

    if (!ok || ctx.err != 0)
    {
        WSASetLastError(ok ? (int)ctx.err : ctx.err);
        wsa_set_errno();
        return -1;
    }
    return (ssize_t)ctx.bytes;
}

/*
 * Recv: WSARecv with a live WSAOVERLAPPED, waited on via
 * WSAGetOverlappedResult(fWait=TRUE) - the event-based completion
 * tract.  Same WSABUF split shape as overlapped_send so the
 * scatter-recv path is hit identically.
 */
static ssize_t overlapped_recv(int fd, void *buf, size_t len, int flags)
{
    /* MSG_DONTWAIT: see overlapped_send for the FIONBIO toggle rationale. */
    bool dontwait = (flags & MSG_DONTWAIT) != 0;
    flags &= ~MSG_DONTWAIT;

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
    /* WDF driver does not populate the user-mode IoStatusBlock until
     * completion; the WSAOVERLAPPED lives in the caller.  If Internal
     * stays at 0 (STATUS_SUCCESS) when WSAGetOverlappedResult runs, it
     * returns immediately with bytes=InternalHigh=0.  Mark the op as
     * pending here so the WaitForSingleObjectEx branch inside
     * WSAGetOverlappedResult actually blocks on hEvent. */
    ov.Internal = (ULONG_PTR)STATUS_PENDING;
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
    DWORD bytes = 0;
    BOOL ok;

    if (r == 0)
    {
        /* Immediate completion: bytes are in `got`, ov may not have
         * been populated and hEvent may not be signalled - do NOT
         * call WSAGetOverlappedResult, it would read zeros. */
        bytes = got;
        ok = TRUE;
    }
    else
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
        DWORD ov_flags = 0;
        ok = WSAGetOverlappedResult((SOCKET)fd, &ov, &bytes, TRUE, &ov_flags);
        saved_err = ok ? 0 : WSAGetLastError();
    }
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
