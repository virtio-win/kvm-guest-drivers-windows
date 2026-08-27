/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Negative-path smoke check for the `wsa` variant.
 *
 * Each helper dials one wsa function that reaches the LSP/driver
 * with inputs the Winsock docs say must fail a specific way, and
 * asserts the surfaced error code matches.  A single call site -
 * `wsa_validate_all()`, invoked from `sock_ops_select("wsa")` before
 * the first test runs - trips the binary as soon as an error-mapping
 * regression lands, so we don't have to chase it through a green-ish
 * sweep.
 *
 * All helpers exit(EXIT_FAILURE) on mismatch; there is no return
 * channel.  The vsock socket used for the connect/send/recv checks
 * is closed at the bottom of `wsa_validate_all()`.
 */

#define COMPAT_IMPL
#include "compat.h"
#include "sock_ops.h"

#include "..\\..\\inc\\vio_sockets.h"

static void expect_wsa_err(int rc, int expected, const char *what)
{
    if (rc != SOCKET_ERROR)
    {
        fprintf(stderr,
                "wsa-validate: %s: expected SOCKET_ERROR + WSA %d, got rc=%d success\n",
                what,
                expected,
                rc);
        exit(EXIT_FAILURE);
    }
    int got = WSAGetLastError();
    if (got != expected)
    {
        fprintf(stderr,
                "wsa-validate: %s: expected WSA %d, got WSA %d\n",
                what,
                expected,
                got);
        exit(EXIT_FAILURE);
    }
    WSASetLastError(0);
}

static void expect_wsa_err_sock(SOCKET s, int expected, const char *what)
{
    if (s != INVALID_SOCKET)
    {
        fprintf(stderr,
                "wsa-validate: %s: expected INVALID_SOCKET + WSA %d, got a socket\n",
                what,
                expected);
        closesocket(s);
        exit(EXIT_FAILURE);
    }
    int got = WSAGetLastError();
    if (got != expected)
    {
        fprintf(stderr,
                "wsa-validate: %s: expected WSA %d, got WSA %d\n",
                what,
                expected,
                got);
        exit(EXIT_FAILURE);
    }
    WSASetLastError(0);
}

/* WSASocketW: bogus af / bogus type. */
static void validate_wsa_socket(void)
{
    SOCKET s = WSASocketW(0xDEAD, SOCK_STREAM, 0, NULL, 0, 0);
    expect_wsa_err_sock(s, WSAEAFNOSUPPORT, "WSASocketW(af=0xDEAD)");

    s = WSASocketW(g_vsock_af, 0xDEAD, 0, NULL, 0, 0);
    expect_wsa_err_sock(s, WSAESOCKTNOSUPPORT, "WSASocketW(type=0xDEAD)");
}

/* WSAConnect: null name / wrong family. Uses a valid vsock socket. */
static void validate_wsa_connect(SOCKET s)
{
    struct sockaddr_vm addr = {0};
    addr.svm_family = g_vsock_af;
    addr.svm_cid = VMADDR_CID_ANY;
    addr.svm_port = 1;

    int rc = WSAConnect(s, NULL, sizeof(addr), NULL, NULL, NULL, NULL);
    expect_wsa_err(rc, WSAEFAULT, "WSAConnect(name=NULL)");

    struct sockaddr_vm bad_af = addr;
    bad_af.svm_family = 0xBEEF;
    rc = WSAConnect(s, (const struct sockaddr *)&bad_af, sizeof(bad_af), NULL, NULL, NULL, NULL);
    expect_wsa_err(rc, WSAEAFNOSUPPORT, "WSAConnect(sa_family=0xBEEF)");
}

/* WSAAccept: bad socket / non-listening socket. */
static void validate_wsa_accept(SOCKET s_not_listening)
{
    SOCKET s = WSAAccept(INVALID_SOCKET, NULL, NULL, NULL, 0);
    expect_wsa_err_sock(s, WSAENOTSOCK, "WSAAccept(s=INVALID_SOCKET)");

    s = WSAAccept(s_not_listening, NULL, NULL, NULL, 0);
    expect_wsa_err_sock(s, WSAEINVAL, "WSAAccept(non-listening)");
}

/* WSASend: bad socket / null WSABUF.  MSDN does not document a
 * failure code for dwBufferCount=0 (WSP treats it as a zero-length
 * send that succeeds), so we do not assert it here. */
static void validate_wsa_send(SOCKET s)
{
    char payload = 'x';
    WSABUF wb = {.len = 1, .buf = &payload};
    DWORD sent = 0;

    int rc = WSASend(INVALID_SOCKET, &wb, 1, &sent, 0, NULL, NULL);
    expect_wsa_err(rc, WSAENOTSOCK, "WSASend(s=INVALID_SOCKET)");

    rc = WSASend(s, NULL, 1, &sent, 0, NULL, NULL);
    expect_wsa_err(rc, WSAEFAULT, "WSASend(lpBuffers=NULL)");
}

/* WSARecv: bad socket / null WSABUF.  Same reasoning as WSASend for
 * the missing dwBufferCount=0 assertion. */
static void validate_wsa_recv(SOCKET s)
{
    char scratch = 0;
    WSABUF wb = {.len = 1, .buf = &scratch};
    DWORD got = 0;
    DWORD flags = 0;

    int rc = WSARecv(INVALID_SOCKET, &wb, 1, &got, &flags, NULL, NULL);
    expect_wsa_err(rc, WSAENOTSOCK, "WSARecv(s=INVALID_SOCKET)");

    flags = 0;
    rc = WSARecv(s, NULL, 1, &got, &flags, NULL, NULL);
    expect_wsa_err(rc, WSAEFAULT, "WSARecv(lpBuffers=NULL)");
}

/*
 * WSAEventSelect on an invalid socket must fail with WSAENOTSOCK.
 * Reaches the WSP dispatch entry that wsa_poll's arming step uses,
 * so a regression in that call is caught before any wait.
 */
static void validate_wsa_event_select_notsock(void)
{
    WSAEVENT ev = WSACreateEvent();
    if (ev == WSA_INVALID_EVENT)
    {
        fprintf(stderr, "wsa-validate: WSACreateEvent failed WSA %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    int rc = WSAEventSelect(INVALID_SOCKET, ev, FD_READ | FD_CLOSE);
    int got = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    WSACloseEvent(ev);

    if (rc != SOCKET_ERROR)
    {
        fprintf(stderr, "wsa-validate: WSAEventSelect(INVALID_SOCKET): expected SOCKET_ERROR + WSAENOTSOCK, got success\n");
        exit(EXIT_FAILURE);
    }
    if (got != WSAENOTSOCK)
    {
        fprintf(stderr, "wsa-validate: WSAEventSelect(INVALID_SOCKET): expected WSAENOTSOCK, got WSA %d\n", got);
        exit(EXIT_FAILURE);
    }
}

/*
 * Positive timeout probe: a bound-but-not-listening vsock socket
 * armed via WSAEventSelect for FD_READ | FD_CLOSE has nothing to
 * signal, so WSAWaitForMultipleEvents must return WSA_WAIT_TIMEOUT
 * after a short finite wait.  Mirrors the negative probe: the WSA
 * variant's wait primitive is exercised end-to-end without a peer.
 */
static void validate_wsa_event_select_timeout(void)
{
    SOCKET s = WSASocketW(g_vsock_af, SOCK_STREAM, 0, NULL, 0, 0);
    if (s == INVALID_SOCKET)
    {
        fprintf(stderr, "wsa-validate: WSAEventSelect(timeout): WSASocketW failed WSA %d\n", WSAGetLastError());
        exit(EXIT_FAILURE);
    }

    struct sockaddr_vm addr = {0};
    addr.svm_family = g_vsock_af;
    addr.svm_cid = VMADDR_CID_ANY;
    addr.svm_port = 0;
    if (bind(s, (const struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        fprintf(stderr, "wsa-validate: WSAEventSelect(timeout): bind failed WSA %d\n", WSAGetLastError());
        closesocket(s);
        exit(EXIT_FAILURE);
    }

    WSAEVENT ev = WSACreateEvent();
    if (ev == WSA_INVALID_EVENT)
    {
        fprintf(stderr, "wsa-validate: WSAEventSelect(timeout): WSACreateEvent failed WSA %d\n", WSAGetLastError());
        closesocket(s);
        exit(EXIT_FAILURE);
    }

    if (WSAEventSelect(s, ev, FD_READ | FD_CLOSE) == SOCKET_ERROR)
    {
        fprintf(stderr, "wsa-validate: WSAEventSelect(timeout): arm failed WSA %d\n", WSAGetLastError());
        WSACloseEvent(ev);
        closesocket(s);
        exit(EXIT_FAILURE);
    }

    DWORD wr = WSAWaitForMultipleEvents(1, &ev, FALSE, 50 /* ms */, FALSE);

    /* Dissociate + restore blocking mode + close event. */
    WSAEventSelect(s, NULL, 0);
    u_long nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    WSACloseEvent(ev);
    closesocket(s);

    if (wr != WSA_WAIT_TIMEOUT)
    {
        fprintf(stderr,
                "wsa-validate: WSAEventSelect(timeout): expected WSA_WAIT_TIMEOUT, got 0x%lx (WSA %d)\n",
                (unsigned long)wr,
                (wr == WSA_WAIT_FAILED) ? WSAGetLastError() : 0);
        exit(EXIT_FAILURE);
    }
}

void wsa_validate_all(void)
{
    validate_wsa_socket();

    /* Fresh vsock socket used for the four checks that need a valid
     * handle to reach the LSP.  Never bound / never connected. */
    SOCKET s = WSASocketW(g_vsock_af, SOCK_STREAM, 0, NULL, 0, 0);
    if (s == INVALID_SOCKET)
    {
        fprintf(stderr,
                "wsa-validate: cannot create scaffolding vsock socket: WSA %d\n",
                WSAGetLastError());
        exit(EXIT_FAILURE);
    }

    validate_wsa_connect(s);
    validate_wsa_accept(s);
    validate_wsa_send(s);
    validate_wsa_recv(s);
    validate_wsa_event_select_notsock();

    closesocket(s);

    validate_wsa_event_select_timeout();
}
