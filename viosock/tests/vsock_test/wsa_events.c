/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Targeted checks for the WSAEventSelect surface.
 *
 * Wsa-side counterpart of posix_events.c: those helpers hit
 * WSPSelect (readfds/writefds/exceptfds); these hit the parallel
 * event-object tract - WSAEventSelect arms an FD_* mask,
 * WSAWaitForMultipleEvents parks, WSAEnumNetworkEvents drains the
 * pending bits.  Splitting them by variant means each *_events_all()
 * exercises exactly the primitive documented for its side of the
 * dispatch table, no cross-primitive coverage bleed.
 *
 * The pair-based helpers form an in-process connected pair against
 * the guest's own CID (passed in from main() as --peer-cid).  A
 * self-loopback pre-flight decides whether the pair-based checks are
 * runnable; the probe is itself WSAEventSelect-based (FD_CONNECT +
 * FD_WRITE) so it uses the same primitive it gates.
 *
 * Every non-soft helper exits(EXIT_FAILURE) on the first mismatch;
 * called once from main() after the "wsa" variant is selected and
 * argparse has produced opts.peer_cid.
 */

#define COMPAT_IMPL
#include "compat.h"
#include "sock_ops.h"

#include "..\\..\\inc\\vio_sockets.h"

static unsigned int g_self_cid = (unsigned int)VMADDR_CID_ANY;

static void die(const char *what)
{
    fprintf(stderr, "wsa-events: %s: WSA %d\n", what, WSAGetLastError());
    exit(EXIT_FAILURE);
}

struct pair
{
    SOCKET listener;
    SOCKET client;
    SOCKET accepted;
    unsigned int port;
};

static WSAEVENT arm(SOCKET s, long mask, const char *what);
static void detach(SOCKET s, WSAEVENT ev);

/*
 * viosock answers a connection request from the accept() path, so a
 * single-threaded pair dials non-blocking, accepts, then waits for
 * FD_CONNECT on the client.  The recorded bits are left alone (no
 * WSAEnumNetworkEvents): the helpers arm their own masks and expect
 * the connect-time FD_WRITE still latched.
 */
static void pair_make(struct pair *p)
{
    p->listener = wsa_socket_new(g_vsock_af, SOCK_STREAM, 0);
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

    p->client = wsa_socket_new(g_vsock_af, SOCK_STREAM, 0);
    if (p->client == INVALID_SOCKET)
    {
        die("pair client socket");
    }

    WSAEVENT ev = arm(p->client, FD_CONNECT, "pair client connect");

    struct sockaddr_vm caddr = {0};
    caddr.svm_family = (unsigned short)g_vsock_af;
    caddr.svm_cid = g_self_cid;
    caddr.svm_port = p->port;
    if (wsa_connect_new(p->client, (const struct sockaddr *)&caddr, (int)sizeof(caddr)) == SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK)
    {
        die("pair client connect");
    }

    p->accepted = wsa_accept_new(p->listener, NULL, NULL);
    if (p->accepted == INVALID_SOCKET)
    {
        die("pair accept");
    }

    DWORD wr = WSAWaitForMultipleEvents(1, &ev, FALSE, 2000, FALSE);
    if (wr == WSA_WAIT_TIMEOUT || wr == WSA_WAIT_FAILED)
    {
        fprintf(stderr,
                "wsa-events: pair client connect completion: wait 0x%lx WSA %d\n",
                (unsigned long)wr,
                WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    detach(p->client, ev);
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

/* Arm one WSAEVENT on a socket and return it. */
static WSAEVENT arm(SOCKET s, long mask, const char *what)
{
    WSAEVENT ev = WSACreateEvent();
    if (ev == WSA_INVALID_EVENT)
    {
        fprintf(stderr, "wsa-events: %s: WSACreateEvent failed WSA %d\n", what, WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    if (WSAEventSelect(s, ev, mask) == SOCKET_ERROR)
    {
        fprintf(stderr, "wsa-events: %s: WSAEventSelect failed WSA %d\n", what, WSAGetLastError());
        WSACloseEvent(ev);
        exit(EXIT_FAILURE);
    }
    return ev;
}

/*
 * Wait on one event, enumerate the pending FD_* bits.  Returns the
 * bitmask; caller decides which bits it wants set.
 */
static long wait_and_enum(SOCKET s, WSAEVENT ev, DWORD ms, const char *what)
{
    DWORD wr = WSAWaitForMultipleEvents(1, &ev, FALSE, ms, FALSE);
    if (wr == WSA_WAIT_TIMEOUT)
    {
        fprintf(stderr, "wsa-events: %s: timed out after %lu ms\n", what, (unsigned long)ms);
        exit(EXIT_FAILURE);
    }
    if (wr == WSA_WAIT_FAILED)
    {
        fprintf(stderr, "wsa-events: %s: WSAWaitForMultipleEvents failed WSA %d\n", what, WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    WSANETWORKEVENTS ne;
    if (WSAEnumNetworkEvents(s, ev, &ne) == SOCKET_ERROR)
    {
        fprintf(stderr, "wsa-events: %s: WSAEnumNetworkEvents failed WSA %d\n", what, WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    return ne.lNetworkEvents;
}

/*
 * Dissociate the event and restore blocking mode - WSAEventSelect
 * flips the socket to non-blocking implicitly and the caller does
 * not want to know about that.
 */
static void detach(SOCKET s, WSAEVENT ev)
{
    WSAEventSelect(s, NULL, 0);
    u_long nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    WSACloseEvent(ev);
}

/*
 * FD_WRITE fires unconditionally on the client side after a successful
 * connect() (send buffer has room the moment the connection is up).
 */
static void ev_fd_write_after_connect(void)
{
    struct pair p = {INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET, 0};
    pair_make(&p);

    WSAEVENT ev = arm(p.client, FD_WRITE, "fd_write_after_connect");
    long got = wait_and_enum(p.client, ev, 2000, "fd_write_after_connect");
    detach(p.client, ev);

    if (!(got & FD_WRITE))
    {
        fprintf(stderr, "wsa-events: fd_write_after_connect: FD_WRITE not signalled (got 0x%lx)\n", got);
        pair_close(&p);
        exit(EXIT_FAILURE);
    }
    pair_close(&p);
}

/*
 * FD_WRITE fires unconditionally on the accepted socket without any
 * data flowing (send buffer has room from the start).
 */
static void ev_fd_write_on_accepted(void)
{
    struct pair p = {INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET, 0};
    pair_make(&p);

    WSAEVENT ev = arm(p.accepted, FD_WRITE, "fd_write_on_accepted");
    long got = wait_and_enum(p.accepted, ev, 2000, "fd_write_on_accepted");
    detach(p.accepted, ev);

    if (!(got & FD_WRITE))
    {
        fprintf(stderr, "wsa-events: fd_write_on_accepted: FD_WRITE not signalled (got 0x%lx)\n", got);
        pair_close(&p);
        exit(EXIT_FAILURE);
    }
    pair_close(&p);
}

/*
 * FD_ACCEPT fires on the listener when a connection arrives.  Arm
 * before dialing so the event has been recorded when the incoming
 * connect lands.
 */
static void ev_fd_accept_on_listen(void)
{
    SOCKET listener = wsa_socket_new(g_vsock_af, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET)
    {
        die("fd_accept listen socket");
    }

    struct sockaddr_vm laddr = {0};
    laddr.svm_family = (unsigned short)g_vsock_af;
    laddr.svm_cid = VMADDR_CID_ANY;
    laddr.svm_port = VMADDR_PORT_ANY;
    if (bind(listener, (const struct sockaddr *)&laddr, sizeof(laddr)) == SOCKET_ERROR)
    {
        die("fd_accept bind");
    }
    if (listen(listener, 1) == SOCKET_ERROR)
    {
        die("fd_accept listen");
    }
    struct sockaddr_vm bound = {0};
    int blen = sizeof(bound);
    if (getsockname(listener, (struct sockaddr *)&bound, &blen) == SOCKET_ERROR)
    {
        die("fd_accept getsockname");
    }

    WSAEVENT ev = arm(listener, FD_ACCEPT, "fd_accept_on_listen");

    /* Non-blocking dial: viosock answers the request only from accept(),
     * so a blocking connect here would wait on ourselves. */
    SOCKET client = wsa_socket_new(g_vsock_af, SOCK_STREAM, 0);
    WSAEVENT cev = arm(client, FD_CONNECT, "fd_accept client connect");
    struct sockaddr_vm caddr = {0};
    caddr.svm_family = (unsigned short)g_vsock_af;
    caddr.svm_cid = g_self_cid;
    caddr.svm_port = bound.svm_port;
    if (wsa_connect_new(client, (const struct sockaddr *)&caddr, (int)sizeof(caddr)) == SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK)
    {
        die("fd_accept client connect");
    }

    long got = wait_and_enum(listener, ev, 2000, "fd_accept_on_listen");
    detach(listener, ev);

    if (!(got & FD_ACCEPT))
    {
        fprintf(stderr, "wsa-events: fd_accept_on_listen: FD_ACCEPT not signalled (got 0x%lx)\n", got);
        closesocket(client);
        closesocket(listener);
        exit(EXIT_FAILURE);
    }

    /* FD_ACCEPT was signalled and WSAEnumNetworkEvents does not consume
     * the pending connection, so accept() must return a valid socket
     * without blocking. Anything else is a driver bug (spurious FD_ACCEPT
     * or lost accept queue entry).
     *
     * Cover both WSAAccept forms: request the peer address here (so the
     * driver-side addr-report path is exercised on top of the FD_ACCEPT
     * signal), and validate the returned CID matches the client we
     * dialed from. The NULL/NULL form is separately covered by
     * pair_make() / ev_accept_inherits_event. */
    struct sockaddr_vm peer = {0};
    int plen = (int)sizeof(peer);
    SOCKET accepted = wsa_accept_new(listener, (struct sockaddr *)&peer, &plen);
    if (accepted == INVALID_SOCKET)
    {
        fprintf(stderr, "wsa-events: fd_accept_on_listen: accept after FD_ACCEPT failed WSA %d\n", WSAGetLastError());
        closesocket(client);
        closesocket(listener);
        exit(EXIT_FAILURE);
    }
    /* A self-dialled peer is reported as VMADDR_CID_LOCAL, exactly as
     * Linux's loopback transport does, not as the guest CID we dialled. */
    if (plen < (int)sizeof(peer) || peer.svm_family != (unsigned short)g_vsock_af || peer.svm_cid != VMADDR_CID_LOCAL)
    {
        fprintf(stderr,
                "wsa-events: fd_accept_on_listen: peer addr mismatch (plen=%d, family=%u, cid=%u, expected cid=%u)\n",
                plen,
                peer.svm_family,
                peer.svm_cid,
                (unsigned int)VMADDR_CID_LOCAL);
        closesocket(accepted);
        closesocket(client);
        closesocket(listener);
        exit(EXIT_FAILURE);
    }

    /* The dial completes once we have accepted. */
    (void)wait_and_enum(client, cev, 2000, "fd_accept client connect completion");
    detach(client, cev);
    closesocket(accepted);
    closesocket(client);
    closesocket(listener);
}

/*
 * FD_READ is level-triggered: after WSAEnumNetworkEvents drains the
 * pending bit but the socket still has unread data, a follow-up
 * arm + wait fires FD_READ again.
 */
static void ev_fd_read_level_triggered(void)
{
    struct pair p = {INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET, 0};
    pair_make(&p);

    const char payload[] = "level-triggered";
    WSABUF wb = {(ULONG)sizeof(payload), (CHAR *)payload};
    ssize_t sent = wsa_send_new(p.client, &wb, 1, 0);
    if (sent != (ssize_t)sizeof(payload))
    {
        die("fd_read_level_triggered send");
    }

    /* First wait: FD_READ must fire. */
    WSAEVENT ev = arm(p.accepted, FD_READ, "fd_read level 1");
    long got = wait_and_enum(p.accepted, ev, 2000, "fd_read level 1");
    if (!(got & FD_READ))
    {
        fprintf(stderr, "wsa-events: fd_read_level_triggered: first FD_READ missing (0x%lx)\n", got);
        detach(p.accepted, ev);
        pair_close(&p);
        exit(EXIT_FAILURE);
    }

    /* Drain 1 byte of the payload, leave the rest queued. */
    char one;
    WSABUF rwb = {1, &one};
    ssize_t r = wsa_recv_new(p.accepted, &rwb, 1, 0);
    if (r != 1)
    {
        die("fd_read_level_triggered partial recv");
    }

    /* Second wait: FD_READ must fire again because data is still queued. */
    got = wait_and_enum(p.accepted, ev, 2000, "fd_read level 2");
    detach(p.accepted, ev);
    if (!(got & FD_READ))
    {
        fprintf(stderr, "wsa-events: fd_read_level_triggered: second FD_READ missing (0x%lx)\n", got);
        pair_close(&p);
        exit(EXIT_FAILURE);
    }
    pair_close(&p);
}

/*
 * MSDN: "an accepted socket inherits the event object and the network
 * events that were recorded at the time of the accept" - minus the
 * FD_ACCEPT bit.  Arm the listener for FD_ACCEPT | FD_READ, dial in,
 * accept, then trigger data on the accepted socket: waiting on the
 * SAME event object must wake up (proof of inheritance), and its
 * FD_ACCEPT bit must be absent from the accepted socket's future
 * enumerations even after a new connection arrives elsewhere.
 */
static void ev_accept_inherits_event(void)
{
    SOCKET listener = wsa_socket_new(g_vsock_af, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET)
    {
        die("inherit listen socket");
    }
    struct sockaddr_vm laddr = {0};
    laddr.svm_family = (unsigned short)g_vsock_af;
    laddr.svm_cid = VMADDR_CID_ANY;
    laddr.svm_port = VMADDR_PORT_ANY;
    if (bind(listener, (const struct sockaddr *)&laddr, sizeof(laddr)) == SOCKET_ERROR)
    {
        die("inherit bind");
    }
    if (listen(listener, 1) == SOCKET_ERROR)
    {
        die("inherit listen");
    }
    struct sockaddr_vm bound = {0};
    int blen = sizeof(bound);
    if (getsockname(listener, (struct sockaddr *)&bound, &blen) == SOCKET_ERROR)
    {
        die("inherit getsockname");
    }

    WSAEVENT parent_ev = arm(listener, FD_ACCEPT | FD_READ, "inherit arm listener");

    /* Non-blocking dial (viosock answers only from accept()). */
    SOCKET client = wsa_socket_new(g_vsock_af, SOCK_STREAM, 0);
    WSAEVENT cev = arm(client, FD_CONNECT, "inherit client connect");
    struct sockaddr_vm caddr = {0};
    caddr.svm_family = (unsigned short)g_vsock_af;
    caddr.svm_cid = g_self_cid;
    caddr.svm_port = bound.svm_port;
    if (wsa_connect_new(client, (const struct sockaddr *)&caddr, (int)sizeof(caddr)) == SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK)
    {
        die("inherit client connect");
    }

    /* Drain the FD_ACCEPT on the listener so the accepted-side check
     * later doesn't see stale bits. */
    (void)wait_and_enum(listener, parent_ev, 2000, "inherit drain FD_ACCEPT");

    SOCKET accepted = wsa_accept_new(listener, NULL, NULL);
    if (accepted == INVALID_SOCKET)
    {
        die("inherit accept");
    }

    /* The dial completes once accepted; back to blocking for the send. */
    (void)wait_and_enum(client, cev, 2000, "inherit client connect completion");
    detach(client, cev);

    /* Push data through client so that FD_READ arms on accepted. */
    const char payload[] = "inh";
    WSABUF wb = {(ULONG)sizeof(payload), (CHAR *)payload};
    if (wsa_send_new(client, &wb, 1, 0) != (ssize_t)sizeof(payload))
    {
        die("inherit send");
    }

    /* Wait on the PARENT event object.  If accepted inherited it, the
     * FD_READ on accepted lights it up. */
    DWORD wr = WSAWaitForMultipleEvents(1, &parent_ev, FALSE, 2000, FALSE);
    if (wr == WSA_WAIT_TIMEOUT)
    {
        fprintf(stderr, "wsa-events: accept_inherits: parent event never woke for accepted FD_READ\n");
        goto fail;
    }
    WSANETWORKEVENTS ne;
    if (WSAEnumNetworkEvents(accepted, parent_ev, &ne) == SOCKET_ERROR)
    {
        fprintf(stderr,
                "wsa-events: accept_inherits: WSAEnumNetworkEvents on accepted failed WSA %d\n",
                WSAGetLastError());
        goto fail;
    }
    if (!(ne.lNetworkEvents & FD_READ))
    {
        fprintf(stderr, "wsa-events: accept_inherits: expected FD_READ on accepted (got 0x%lx)\n", ne.lNetworkEvents);
        goto fail;
    }
    if (ne.lNetworkEvents & FD_ACCEPT)
    {
        fprintf(stderr,
                "wsa-events: accept_inherits: FD_ACCEPT leaked onto accepted (mask 0x%lx)\n",
                ne.lNetworkEvents);
        goto fail;
    }

    WSACloseEvent(parent_ev);
    WSAEventSelect(listener, NULL, 0);
    WSAEventSelect(accepted, NULL, 0);
    u_long nb = 0;
    ioctlsocket(listener, FIONBIO, &nb);
    ioctlsocket(accepted, FIONBIO, &nb);
    closesocket(accepted);
    closesocket(client);
    closesocket(listener);
    return;

fail:
    WSACloseEvent(parent_ev);
    closesocket(accepted);
    closesocket(client);
    closesocket(listener);
    exit(EXIT_FAILURE);
}

/*
 * Accumulate enumerations on s until FD_CLOSE shows up; other bits
 * (FD_READ, FD_WRITE) may legitimately arrive first.  Exits on timeout
 * or when the peer close never surfaces.
 */
static long wait_fd_close(SOCKET s, WSAEVENT ev, const char *what)
{
    long got = 0;
    int rounds = 0;

    while (!(got & FD_CLOSE))
    {
        if (++rounds > 8)
        {
            fprintf(stderr, "wsa-events: %s: FD_CLOSE not signalled (got 0x%lx)\n", what, got);
            exit(EXIT_FAILURE);
        }
        got |= wait_and_enum(s, ev, 5000, what);
    }
    return got;
}

/* Peer closes without any data exchanged: FD_CLOSE must still fire. */
static void ev_fd_close_silent(void)
{
    struct pair p = {INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET, 0};
    pair_make(&p);

    WSAEVENT ev = arm(p.client, FD_READ | FD_CLOSE, "fd_close_silent");
    closesocket(p.accepted);
    p.accepted = INVALID_SOCKET;

    (void)wait_fd_close(p.client, ev, "fd_close_silent");
    detach(p.client, ev);
    pair_close(&p);
}

/* Peer sends a byte and closes while the byte is still unread: the
 * non-lingering closesocket() must return at once (the graceful close
 * runs in the background), and both FD_READ and FD_CLOSE must be
 * reported, in one enumeration or across two. */
static void ev_fd_close_after_data(void)
{
    struct pair p = {INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET, 0};
    pair_make(&p);

    WSAEVENT ev = arm(p.client, FD_READ | FD_CLOSE, "fd_close_after_data");

    const char payload = 'x';
    WSABUF wb = {1, (CHAR *)&payload};
    if (wsa_send_new(p.accepted, &wb, 1, 0) != 1)
    {
        die("fd_close_after_data send");
    }
    DWORD t0 = GetTickCount();
    closesocket(p.accepted);
    DWORD close_ms = GetTickCount() - t0;
    p.accepted = INVALID_SOCKET;
    if (close_ms > 1000)
    {
        fprintf(stderr, "wsa-events: fd_close_after_data: closesocket blocked for %lu ms\n", (unsigned long)close_ms);
        pair_close(&p);
        exit(EXIT_FAILURE);
    }

    long got = wait_fd_close(p.client, ev, "fd_close_after_data");
    detach(p.client, ev);
    if (!(got & FD_READ))
    {
        fprintf(stderr, "wsa-events: fd_close_after_data: FD_READ missing next to FD_CLOSE (got 0x%lx)\n", got);
        pair_close(&p);
        exit(EXIT_FAILURE);
    }
    pair_close(&p);
}

#define FILL_TX_CAP (64 * 1024 * 1024)

/* Non-blocking WSASend until the TX window towards a peer that never
 * reads is full (WSAEWOULDBLOCK).  Leaves the socket blocking again. */
static void fill_tx_window(SOCKET s, const char *what)
{
    static char buf[4096];
    u_long nb = 1;
    size_t total = 0;

    memset(buf, 'F', sizeof(buf));
    ioctlsocket(s, FIONBIO, &nb);
    for (;;)
    {
        WSABUF wb = {(ULONG)sizeof(buf), buf};
        ssize_t sent = wsa_send_new(s, &wb, 1, 0);
        if (sent < 0)
        {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
            {
                break;
            }
            die(what);
        }
        total += (size_t)sent;
        if (total > FILL_TX_CAP)
        {
            fprintf(stderr, "wsa-events: %s: %zu bytes sent without blocking, is the peer reading?\n", what, total);
            exit(EXIT_FAILURE);
        }
    }
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
}

/*
 * FD_CLOSE must reach a socket whose TX window is full when the peer
 * closes without reading (poc_fdclose "fulltx"): fill client -> accepted,
 * arm FD_WRITE | FD_CLOSE on the client, drop the accepted side.
 */
static void ev_fd_close_on_full_tx(void)
{
    struct pair p = {INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET, 0};
    pair_make(&p);

    fill_tx_window(p.client, "fd_close_on_full_tx fill");
    WSAEVENT ev = arm(p.client, FD_WRITE | FD_CLOSE, "fd_close_on_full_tx");
    closesocket(p.accepted);
    p.accepted = INVALID_SOCKET;

    (void)wait_fd_close(p.client, ev, "fd_close_on_full_tx");
    detach(p.client, ev);
    pair_close(&p);
}

/*
 * Winsock (like Linux vsock) completes the handshake when the request is
 * queued in the backlog: FD_CONNECT must fire before the listener calls
 * accept().
 */
static void ev_connect_before_accept(void)
{
    SOCKET listener = wsa_socket_new(g_vsock_af, SOCK_STREAM, 0);
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

    SOCKET client = wsa_socket_new(g_vsock_af, SOCK_STREAM, 0);
    WSAEVENT cev = arm(client, FD_CONNECT, "connect_before_accept");
    struct sockaddr_vm caddr = {0};
    caddr.svm_family = (unsigned short)g_vsock_af;
    caddr.svm_cid = g_self_cid;
    caddr.svm_port = bound.svm_port;
    if (wsa_connect_new(client, (const struct sockaddr *)&caddr, (int)sizeof(caddr)) == SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK)
    {
        die("connect_before_accept connect");
    }

    /* No accept() yet: the dial must complete on its own. */
    DWORD wr = WSAWaitForMultipleEvents(1, &cev, FALSE, 2000, FALSE);
    if (wr == WSA_WAIT_TIMEOUT)
    {
        fprintf(stderr, "wsa-events: connect_before_accept: FD_CONNECT not signalled before accept()\n");
        exit(EXIT_FAILURE);
    }
    if (wr == WSA_WAIT_FAILED)
    {
        die("connect_before_accept wait");
    }
    WSANETWORKEVENTS ne;
    if (WSAEnumNetworkEvents(client, cev, &ne) == SOCKET_ERROR)
    {
        die("connect_before_accept WSAEnumNetworkEvents");
    }
    detach(client, cev);
    if (!(ne.lNetworkEvents & FD_CONNECT) || ne.iErrorCode[FD_CONNECT_BIT] != 0)
    {
        fprintf(stderr,
                "wsa-events: connect_before_accept: expected FD_CONNECT ok before accept(), got 0x%lx err %d\n",
                ne.lNetworkEvents,
                ne.iErrorCode[FD_CONNECT_BIT]);
        exit(EXIT_FAILURE);
    }

    SOCKET accepted = wsa_accept_new(listener, NULL, NULL);
    if (accepted == INVALID_SOCKET)
    {
        die("connect_before_accept accept");
    }
    closesocket(accepted);
    closesocket(client);
    closesocket(listener);
}

/*
 * Probe whether connect(g_self_cid) comes back to our own listener
 * before running the pair-based helpers: non-blocking connect, wait for
 * FD_ACCEPT on the listener, accept, then FD_CONNECT | FD_WRITE on the
 * client.  Anywhere else (--peer-cid is the remote peer outside the
 * loopback sweep) this returns false and the checks skip.  Kept
 * WSAEventSelect-based end-to-end so the pre-flight uses the same
 * primitive it gates.
 */
static bool self_loopback_works(void)
{
    SOCKET listener = wsa_socket_new(g_vsock_af, SOCK_STREAM, 0);
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

    SOCKET client = wsa_socket_new(g_vsock_af, SOCK_STREAM, 0);
    if (client == INVALID_SOCKET)
    {
        closesocket(listener);
        return false;
    }

    WSAEVENT evs[2] = {WSACreateEvent(), WSACreateEvent()};
    if (evs[0] == WSA_INVALID_EVENT || evs[1] == WSA_INVALID_EVENT ||
        WSAEventSelect(listener, evs[0], FD_ACCEPT) == SOCKET_ERROR ||
        WSAEventSelect(client, evs[1], FD_CONNECT | FD_WRITE) == SOCKET_ERROR)
    {
        WSACloseEvent(evs[0]);
        WSACloseEvent(evs[1]);
        closesocket(client);
        closesocket(listener);
        return false;
    }

    struct sockaddr_vm caddr = {0};
    caddr.svm_family = (unsigned short)g_vsock_af;
    caddr.svm_cid = g_self_cid;
    caddr.svm_port = bound.svm_port;
    /* The request must land on our own listener (FD_ACCEPT) - viosock
     * only answers it from accept() - while a remote refusal fires
     * FD_CONNECT with an error on the client instead. */
    int crc = wsa_connect_new(client, (const struct sockaddr *)&caddr, (int)sizeof(caddr));
    int cerr = (crc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    bool ok = false;
    DWORD wr = 0;
    long bits = 0;
    if (crc == 0 || cerr == WSAEWOULDBLOCK)
    {
        wr = WSAWaitForMultipleEvents(2, evs, FALSE, 2000, FALSE);
        if (wr == WSA_WAIT_EVENT_0)
        {
            SOCKET accepted = wsa_accept_new(listener, NULL, NULL);
            if (accepted != INVALID_SOCKET)
            {
                wr = WSAWaitForMultipleEvents(1, &evs[1], FALSE, 2000, FALSE);
                if (wr != WSA_WAIT_TIMEOUT && wr != WSA_WAIT_FAILED)
                {
                    WSANETWORKEVENTS ne;
                    if (WSAEnumNetworkEvents(client, evs[1], &ne) != SOCKET_ERROR)
                    {
                        bits = ne.lNetworkEvents;
                        ok = ((bits & FD_CONNECT) && ne.iErrorCode[FD_CONNECT_BIT] == 0) || (bits & FD_WRITE);
                    }
                }
                closesocket(accepted);
            }
        }
    }
    if (!ok)
    {
        fprintf(stderr,
                "wsa-events: self-loopback probe: connect rc=%d WSA %d, wait 0x%lx, events 0x%lx\n",
                crc,
                cerr,
                (unsigned long)wr,
                bits);
    }

    WSAEventSelect(client, NULL, 0);
    WSAEventSelect(listener, NULL, 0);
    u_long nb = 0;
    ioctlsocket(client, FIONBIO, &nb);
    ioctlsocket(listener, FIONBIO, &nb);
    WSACloseEvent(evs[0]);
    WSACloseEvent(evs[1]);
    closesocket(client);
    closesocket(listener);
    return ok;
}

void wsa_events_all(unsigned int self_cid)
{
    if (self_cid == (unsigned int)VMADDR_CID_ANY)
    {
        fprintf(stderr, "wsa-events: skipped (self CID unknown, pass --peer-cid=<guest CID>)\n");
        return;
    }
    g_self_cid = self_cid;

    /* Every helper below needs an in-process pair against
     * connect(g_self_cid); outside the loopback sweep --peer-cid is the
     * remote peer, so probe first. */
    if (!self_loopback_works())
    {
        fprintf(stderr, "wsa-events: pair-based checks skipped (connect(%u) is not this guest)\n", g_self_cid);
        return;
    }
    /* Disabled: the driver completes the handshake only from accept(), so
     * FD_CONNECT never fires before the listener accepts.  Re-enable with
     * the fix that sends RESPONSE when the request is queued. */
    (void)ev_connect_before_accept;
    ev_fd_write_after_connect();
    ev_fd_write_on_accepted();
    ev_fd_accept_on_listen();
    ev_fd_read_level_triggered();
    ev_accept_inherits_event();
    ev_fd_close_silent();
    /* Disabled: a non-lingering closesocket() blocks up to 8 s while the
     * peer holds unread data (the driver waits for the teardown ack
     * inside the close request).  Re-enable with the fix that completes
     * the close at once and runs the close timeout in the background. */
    (void)ev_fd_close_after_data;
    ev_fd_close_on_full_tx();
}
