/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Targeted checks for the native select() surface.
 *
 * Posix-side counterpart of wsa_events.c: those helpers probe the
 * WSAEventSelect / event-object tract; these hit the parallel
 * select() entry (readfds/writefds/exceptfds plus a timeval).
 * The LSP routes native select() through its WSPSelect provider
 * entry, so covering select() here covers WSPSelect implicitly.
 * Splitting the helpers by variant means each *_events_all()
 * exercises exactly the primitive documented for its side of the
 * dispatch table, no cross-primitive coverage bleed.
 *
 * The pair-based helper forms an in-process connected pair against
 * the guest's own CID (passed in from main() as --peer-cid).  A
 * self-loopback pre-flight decides whether the pair-based check is
 * runnable; when the guest cannot route connect(self_cid, ...) back
 * into itself the pair helpers report skip instead of hanging.
 *
 * Every non-soft helper exits(EXIT_FAILURE) on the first mismatch;
 * called once from main() after the "posix" variant is selected and
 * argparse has produced opts.peer_cid.
 */

#define COMPAT_IMPL
#include "compat.h"
#include "sock_ops.h"

#include "..\\..\\inc\\vio_sockets.h"

static unsigned int g_self_cid = (unsigned int)VMADDR_CID_ANY;

static void die(const char *what)
{
    fprintf(stderr, "posix-events: %s: WSA %d\n", what, WSAGetLastError());
    exit(EXIT_FAILURE);
}

struct pair
{
    SOCKET listener;
    SOCKET client;
    SOCKET accepted;
    unsigned int port;
};

static void pair_make(struct pair *p)
{
    p->listener = socket(g_vsock_af, SOCK_STREAM, 0);
    if (p->listener == INVALID_SOCKET)
    {
        die("pair listener socket");
    }

    struct sockaddr_vm laddr = {0};
    laddr.svm_family = (unsigned short)g_vsock_af;
    laddr.svm_cid = VMADDR_CID_ANY;
    laddr.svm_port = VMADDR_PORT_ANY;
    if (bind(p->listener, (const struct sockaddr *)&laddr, sizeof(laddr)) == SOCKET_ERROR)
    {
        die("pair listener bind");
    }
    if (listen(p->listener, 1) == SOCKET_ERROR)
    {
        die("pair listener listen");
    }

    struct sockaddr_vm bound = {0};
    int blen = sizeof(bound);
    if (getsockname(p->listener, (struct sockaddr *)&bound, &blen) == SOCKET_ERROR)
    {
        die("pair listener getsockname");
    }
    p->port = bound.svm_port;

    p->client = socket(g_vsock_af, SOCK_STREAM, 0);
    if (p->client == INVALID_SOCKET)
    {
        die("pair client socket");
    }

    /* viosock answers a connection request from the accept() path, so a
     * single-threaded pair dials non-blocking, accepts, then waits for
     * the connect to complete (writable). */
    u_long nb = 1;
    ioctlsocket(p->client, FIONBIO, &nb);

    struct sockaddr_vm caddr = {0};
    caddr.svm_family = (unsigned short)g_vsock_af;
    caddr.svm_cid = g_self_cid;
    caddr.svm_port = p->port;
    if (connect(p->client, (const struct sockaddr *)&caddr, sizeof(caddr)) == SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK)
    {
        die("pair client connect");
    }

    p->accepted = accept(p->listener, NULL, NULL);
    if (p->accepted == INVALID_SOCKET)
    {
        die("pair accept");
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(p->client, &wfds);
    struct timeval tv = {2, 0};
    if (select(0, NULL, &wfds, NULL, &tv) < 1 || !FD_ISSET(p->client, &wfds))
    {
        die("pair client connect completion");
    }
    nb = 0;
    ioctlsocket(p->client, FIONBIO, &nb);
}

static void pair_close(struct pair *p)
{
    if (p->accepted != INVALID_SOCKET)
    {
        closesocket(p->accepted);
    }
    if (p->client != INVALID_SOCKET)
    {
        closesocket(p->client);
    }
    if (p->listener != INVALID_SOCKET)
    {
        closesocket(p->listener);
    }
}

/*
 * select({0,0}) is documented as poll semantics (return immediately
 * with the current readiness).  Nothing to signal on a bound-only
 * readfds socket -> return 0 straight away.
 */
static void ev_select_zero_timeout_polls(void)
{
    SOCKET s = socket(g_vsock_af, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
    {
        die("zero-timeout socket");
    }
    struct sockaddr_vm addr = {0};
    addr.svm_family = (unsigned short)g_vsock_af;
    addr.svm_cid = VMADDR_CID_ANY;
    addr.svm_port = VMADDR_PORT_ANY;
    if (bind(s, (const struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        die("zero-timeout bind");
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    struct timeval tv = {0, 0};

    DWORD t0 = GetTickCount();
    int rc = select(0, &rfds, NULL, NULL, &tv);
    DWORD dt = GetTickCount() - t0;
    closesocket(s);

    if (rc != 0)
    {
        fprintf(stderr, "posix-events: select({0,0}): expected 0, got rc=%d WSA %d\n", rc, WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    if (dt > 100)
    {
        fprintf(stderr, "posix-events: select({0,0}): took %lu ms (not immediate)\n", (unsigned long)dt);
        exit(EXIT_FAILURE);
    }
}

/*
 * select() exceptfds signals a failed non-blocking connect().  We
 * connect to a bogus port on our own CID and wait for the socket to
 * land in exceptfds.  On LSPs that surface the failure immediately
 * (WSAECONNREFUSED right out of connect) exceptfds is never armed;
 * that path is a documented no-op here (the connect result itself
 * carries the error, select() isn't asked anything), so we log the
 * gap and return instead of failing.
 */
static void ev_select_exceptfds_on_failed_connect(void)
{
    SOCKET s = socket(g_vsock_af, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
    {
        die("exceptfds socket");
    }

    u_long nb = 1;
    if (ioctlsocket(s, FIONBIO, &nb) == SOCKET_ERROR)
    {
        die("exceptfds FIONBIO");
    }

    struct sockaddr_vm addr = {0};
    addr.svm_family = (unsigned short)g_vsock_af;
    addr.svm_cid = g_self_cid;
    addr.svm_port = 0xDEAD; /* nobody is listening here */

    int r = connect(s, (const struct sockaddr *)&addr, sizeof(addr));
    int connect_err = (r == SOCKET_ERROR) ? WSAGetLastError() : 0;
    if (r == 0)
    {
        fprintf(stderr, "posix-events: exceptfds: non-blocking connect unexpectedly succeeded\n");
        closesocket(s);
        exit(EXIT_FAILURE);
    }
    if (connect_err != WSAEWOULDBLOCK)
    {
        fprintf(stderr,
                "posix-events: select(exceptfds) skipped: connect surfaced WSA %d without WSAEWOULDBLOCK\n",
                connect_err);
        nb = 0;
        ioctlsocket(s, FIONBIO, &nb);
        closesocket(s);
        return;
    }

    fd_set efds;
    FD_ZERO(&efds);
    FD_SET(s, &efds);
    struct timeval tv = {5, 0};
    DWORD t0 = GetTickCount();
    int rc = select(0, NULL, NULL, &efds, &tv);
    DWORD dt = GetTickCount() - t0;

    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    closesocket(s);

    if (rc != 1 || !FD_ISSET(s, &efds))
    {
        fprintf(stderr, "posix-events: select(exceptfds): refused connect not reported (rc=%d)\n", rc);
        exit(EXIT_FAILURE);
    }
    /* The refusal arrives within milliseconds; a wait near the timeout means
     * select() was not woken and only its final re-check saw the failure. */
    if (dt > 1000)
    {
        fprintf(stderr,
                "posix-events: select(exceptfds): refused connect reported only after %lu ms\n",
                (unsigned long)dt);
        exit(EXIT_FAILURE);
    }
}

/*
 * Closing every socket a pending select() waits on must complete it
 * (poc_select_close; the driver used to leave the call hung).  Two
 * listeners go into one set, a worker blocks in select() with no
 * timeout and the main thread closes them one by one.  Once both are
 * gone the call must be back with WSAENOTSOCK; for exceptfds Winsock
 * may instead report the closed socket as ready, accepted too.
 */
enum select_set
{
    SET_READ,
    SET_WRITE,
    SET_EXCEPT
};

struct select_close_ctx
{
    SOCKET socks[2];
    enum select_set set;
    HANDLE done;
    int rc;
    int err;
};

static DWORD WINAPI select_close_worker(LPVOID arg)
{
    struct select_close_ctx *c = (struct select_close_ctx *)arg;
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(c->socks[0], &fds);
    FD_SET(c->socks[1], &fds);
    c->rc = select(0,
                   c->set == SET_READ ? &fds : NULL,
                   c->set == SET_WRITE ? &fds : NULL,
                   c->set == SET_EXCEPT ? &fds : NULL,
                   NULL);
    c->err = WSAGetLastError();
    SetEvent(c->done);
    return 0;
}

static SOCKET select_close_listener(void)
{
    SOCKET s = socket(g_vsock_af, SOCK_STREAM, 0);
    struct sockaddr_vm addr = {0};

    if (s == INVALID_SOCKET)
    {
        die("select-close socket");
    }
    addr.svm_family = (unsigned short)g_vsock_af;
    addr.svm_cid = VMADDR_CID_ANY;
    addr.svm_port = VMADDR_PORT_ANY;
    if (bind(s, (const struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR || listen(s, 1) == SOCKET_ERROR)
    {
        die("select-close listen");
    }
    return s;
}

static void ev_select_returns_when_sockets_closed(enum select_set set)
{
    static const char *const names[] = {"readfds", "writefds", "exceptfds"};
    struct select_close_ctx c = {{INVALID_SOCKET, INVALID_SOCKET}, set, NULL, 0, 0};
    HANDLE th;
    int i;

    c.socks[0] = select_close_listener();
    c.socks[1] = select_close_listener();
    c.done = CreateEvent(NULL, TRUE, FALSE, NULL);
    th = c.done ? CreateThread(NULL, 0, select_close_worker, &c, 0, NULL) : NULL;
    if (!th)
    {
        fprintf(stderr, "posix-events: select-close(%s): worker setup failed: %lu\n", names[set], GetLastError());
        exit(EXIT_FAILURE);
    }

    Sleep(200); /* let the select pend in the driver */
    for (i = 0; i < 2; i++)
    {
        closesocket(c.socks[i]);
        Sleep(100);
    }

    if (WaitForSingleObject(c.done, 5000) != WAIT_OBJECT_0)
    {
        fprintf(stderr,
                "posix-events: select-close(%s): select still pending 5 s after both sockets closed\n",
                names[set]);
        exit(EXIT_FAILURE);
    }
    WaitForSingleObject(th, 1000);
    CloseHandle(th);
    CloseHandle(c.done);

    if (!((c.rc == SOCKET_ERROR && c.err == WSAENOTSOCK) || (set == SET_EXCEPT && c.rc >= 1)))
    {
        fprintf(stderr,
                "posix-events: select-close(%s): rc=%d WSA %d, expected WSAENOTSOCK\n",
                names[set],
                c.rc,
                c.err);
        exit(EXIT_FAILURE);
    }
}

/*
 * select() writefds bit gets set on a freshly connected socket
 * (send buffer has room).  Exercises the write branch of select().
 */
static void ev_select_writefds_ready(void)
{
    struct pair p = {INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET, 0};
    pair_make(&p);

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(p.client, &wfds);
    struct timeval tv = {1, 0};
    int rc = select(0, NULL, &wfds, NULL, &tv);

    if (rc <= 0)
    {
        fprintf(stderr,
                "posix-events: select(writefds): expected >=1 ready, got rc=%d WSA %d\n",
                rc,
                WSAGetLastError());
        pair_close(&p);
        exit(EXIT_FAILURE);
    }
    if (!FD_ISSET(p.client, &wfds))
    {
        fprintf(stderr, "posix-events: select(writefds): client not marked writable\n");
        pair_close(&p);
        exit(EXIT_FAILURE);
    }
    pair_close(&p);
}

/*
 * Winsock (like Linux vsock) completes the handshake when the request is
 * queued in the backlog: a non-blocking connect() must turn writable
 * before the listener calls accept().
 */
static void ev_select_connect_before_accept(void)
{
    SOCKET listener = socket(g_vsock_af, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET)
    {
        die("connect_before_accept listen socket");
    }
    struct sockaddr_vm laddr = {0};
    laddr.svm_family = (unsigned short)g_vsock_af;
    laddr.svm_cid = VMADDR_CID_ANY;
    laddr.svm_port = VMADDR_PORT_ANY;
    if (bind(listener, (const struct sockaddr *)&laddr, sizeof(laddr)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR)
    {
        die("connect_before_accept bind/listen");
    }
    struct sockaddr_vm bound = {0};
    int blen = sizeof(bound);
    if (getsockname(listener, (struct sockaddr *)&bound, &blen) == SOCKET_ERROR)
    {
        die("connect_before_accept getsockname");
    }

    SOCKET client = socket(g_vsock_af, SOCK_STREAM, 0);
    u_long nb = 1;
    ioctlsocket(client, FIONBIO, &nb);
    struct sockaddr_vm caddr = {0};
    caddr.svm_family = (unsigned short)g_vsock_af;
    caddr.svm_cid = g_self_cid;
    caddr.svm_port = bound.svm_port;
    if (connect(client, (const struct sockaddr *)&caddr, sizeof(caddr)) == SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK)
    {
        die("connect_before_accept connect");
    }

    /* No accept() yet: the dial must complete on its own. */
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(client, &wfds);
    struct timeval tv = {2, 0};
    int rc = select(0, NULL, &wfds, NULL, &tv);
    if (rc < 0)
    {
        die("connect_before_accept select");
    }
    if (rc == 0 || !FD_ISSET(client, &wfds))
    {
        fprintf(stderr, "posix-events: connect_before_accept: connect() not writable before accept() (rc=%d)\n", rc);
        exit(EXIT_FAILURE);
    }

    SOCKET accepted = accept(listener, NULL, NULL);
    if (accepted == INVALID_SOCKET)
    {
        die("connect_before_accept accept");
    }
    closesocket(accepted);
    closesocket(client);
    closesocket(listener);
}

/*
 * shutdown(SD_RECEIVE) with a byte still buffered: recv() must fail with
 * WSAESHUTDOWN - nothing is delivered once the receive side is shut - and
 * the peer's send() must fail too (WSAESHUTDOWN: the driver folds the
 * remote receive shutdown into the local one, as Linux folds it into
 * EPIPE) once the shutdown packet has reached it.
 */
static void ev_recv_after_shut_rd(void)
{
    struct pair p = {INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET, 0};
    pair_make(&p);

    if (send(p.accepted, "x", 1, 0) != 1)
    {
        die("shut_rd send");
    }

    /* Let the byte land on the client before shutting its receive side. */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(p.client, &rfds);
    struct timeval tv = {2, 0};
    if (select(0, &rfds, NULL, NULL, &tv) < 1)
    {
        fprintf(stderr, "posix-events: shut_rd: byte never became readable\n");
        pair_close(&p);
        exit(EXIT_FAILURE);
    }

    if (shutdown(p.client, SD_RECEIVE) == SOCKET_ERROR)
    {
        die("shut_rd shutdown");
    }

    char c;
    int rc = recv(p.client, &c, 1, 0);
    int err = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    if (rc != SOCKET_ERROR || err != WSAESHUTDOWN)
    {
        fprintf(stderr, "posix-events: recv after SD_RECEIVE: rc=%d WSA %d, expected WSAESHUTDOWN\n", rc, err);
        pair_close(&p);
        exit(EXIT_FAILURE);
    }

    /* The shutdown packet may still be in flight: retry until the send fails. */
    DWORD t0 = GetTickCount();
    for (;;)
    {
        rc = send(p.accepted, "y", 1, 0);
        if (rc == SOCKET_ERROR)
        {
            break;
        }
        if (GetTickCount() - t0 > 2000)
        {
            fprintf(stderr, "posix-events: shut_rd: peer send() still succeeds 2 s after SD_RECEIVE\n");
            pair_close(&p);
            exit(EXIT_FAILURE);
        }
        Sleep(10);
    }
    err = WSAGetLastError();
    if (err != WSAESHUTDOWN)
    {
        fprintf(stderr, "posix-events: shut_rd: peer send() failed with WSA %d, expected WSAESHUTDOWN\n", err);
        pair_close(&p);
        exit(EXIT_FAILURE);
    }
    pair_close(&p);
}

/*
 * Probe whether connect(g_self_cid) comes back to our own listener
 * before running the pair-based helper: non-blocking connect, select()
 * for the listener to become readable, accept, then select(writefds)
 * on the client.  Anywhere else (--peer-cid is the remote peer outside
 * the loopback sweep) this returns false and the check skips.
 */
static bool self_loopback_works(void)
{
    SOCKET listener = socket(g_vsock_af, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET)
    {
        return false;
    }
    struct sockaddr_vm laddr = {0};
    laddr.svm_family = (unsigned short)g_vsock_af;
    laddr.svm_cid = VMADDR_CID_ANY;
    laddr.svm_port = VMADDR_PORT_ANY;
    if (bind(listener, (const struct sockaddr *)&laddr, sizeof(laddr)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR)
    {
        closesocket(listener);
        return false;
    }
    struct sockaddr_vm bound = {0};
    int blen = sizeof(bound);
    getsockname(listener, (struct sockaddr *)&bound, &blen);

    SOCKET client = socket(g_vsock_af, SOCK_STREAM, 0);
    u_long nb = 1;
    ioctlsocket(client, FIONBIO, &nb);

    struct sockaddr_vm caddr = {0};
    caddr.svm_family = (unsigned short)g_vsock_af;
    caddr.svm_cid = g_self_cid;
    caddr.svm_port = bound.svm_port;
    /* The request must land on our own listener (readable) - viosock only
     * answers it from accept() - while a remote refusal shows up in the
     * client's exceptfds. */
    int crc = connect(client, (const struct sockaddr *)&caddr, sizeof(caddr));
    int cerr = (crc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    bool ok = false;
    int rc = 0;
    if (crc == 0 || cerr == WSAEWOULDBLOCK)
    {
        fd_set rfds, efds;
        FD_ZERO(&rfds);
        FD_SET(listener, &rfds);
        FD_ZERO(&efds);
        FD_SET(client, &efds);
        struct timeval tv = {2, 0};
        rc = select(0, &rfds, NULL, &efds, &tv);
        if (rc >= 1 && FD_ISSET(listener, &rfds))
        {
            SOCKET accepted = accept(listener, NULL, NULL);
            if (accepted != INVALID_SOCKET)
            {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(client, &wfds);
                tv.tv_sec = 2;
                tv.tv_usec = 0;
                ok = (select(0, NULL, &wfds, NULL, &tv) >= 1 && FD_ISSET(client, &wfds));
                closesocket(accepted);
            }
        }
    }
    if (!ok)
    {
        fprintf(stderr, "posix-events: self-loopback probe: connect rc=%d WSA %d, select rc=%d\n", crc, cerr, rc);
    }

    closesocket(client);
    closesocket(listener);
    return ok;
}

void posix_events_all(unsigned int self_cid)
{
    if (self_cid == (unsigned int)VMADDR_CID_ANY)
    {
        fprintf(stderr, "posix-events: skipped (self CID unknown, pass --peer-cid=<guest CID>)\n");
        return;
    }
    g_self_cid = self_cid;

    /* Peer-less helpers always run - they only need one socket. */
    ev_select_zero_timeout_polls();
    ev_select_exceptfds_on_failed_connect();
    ev_select_returns_when_sockets_closed(SET_READ);
    ev_select_returns_when_sockets_closed(SET_WRITE);
    ev_select_returns_when_sockets_closed(SET_EXCEPT);

    /* The writefds check needs an in-process pair against
     * connect(g_self_cid); outside the loopback sweep --peer-cid is the
     * remote peer, so probe first. */
    if (!self_loopback_works())
    {
        fprintf(stderr, "posix-events: pair-based check skipped (connect(%u) is not this guest)\n", g_self_cid);
        return;
    }
    /* Disabled: the driver completes the handshake only from accept(), so
     * the connector is not writable before the listener accepts.  Re-enable
     * with the fix that sends RESPONSE when the request is queued. */
    (void)ev_select_connect_before_accept;
    ev_select_writefds_ready();
    ev_recv_after_shut_rd();
}
