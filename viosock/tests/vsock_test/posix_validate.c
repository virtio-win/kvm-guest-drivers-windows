/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Negative-path smoke check for the `posix` variant of vsock_test.
 *
 * Ports the atomic PoCs collected on `viosock-poc-vhi81` that only
 * probe Winsock/LSP semantic (no peer needed, no state coordination)
 * into a single fail-fast helper invoked from sock_ops_select("posix")
 * before any test body runs.
 *
 * Peer- or state-dependent PoCs (poc_fdclose, poc_send_after_fin,
 * poc_select_close, etc) are not covered here.  They belong in
 * scenario tests, not in this atomic smoke.
 */

#define COMPAT_IMPL
#include "compat.h"
#include "sock_ops.h"

#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "..\\..\\inc\\vio_sockets.h"

static SOCKET fresh_vsock_socket(const char *what)
{
    SOCKET s = socket(g_vsock_af, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
    {
        fprintf(stderr,
                "posix-validate: cannot create scaffolding vsock socket for %s: WSA %d\n",
                what,
                WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    return s;
}

/*
 * poc_device_type: GetFileType on a viosock SOCKET must not report
 * FILE_TYPE_UNKNOWN - the CRT rejects the handle for _open_osfhandle
 * if the underlying device type is wrong.  Regression guard for the
 * NAMED_PIPE device-type fix that landed in 687c7762.
 */
static void validate_getfiletype_and_osfhandle(void)
{
    SOCKET s = fresh_vsock_socket("GetFileType/_open_osfhandle");
    DWORD ft = GetFileType((HANDLE)s);
    if (ft == FILE_TYPE_UNKNOWN)
    {
        fprintf(stderr, "posix-validate: GetFileType returned FILE_TYPE_UNKNOWN on vsock socket\n");
        closesocket(s);
        exit(EXIT_FAILURE);
    }

    int fd = _open_osfhandle((intptr_t)s, _O_RDWR | _O_BINARY);
    if (fd == -1)
    {
        fprintf(stderr,
                "posix-validate: _open_osfhandle failed on vsock socket: errno=%d (%s)\n",
                errno,
                strerror(errno));
        closesocket(s);
        exit(EXIT_FAILURE);
    }
    /* _open_osfhandle parked the handle in a CRT fd slot, but _close()
     * would CloseHandle() the SOCKET directly and bypass the Winsock /
     * provider close path (WSPCloseSocket), leaving ws2_32 and the LSP
     * with stale state for a handle value a later socket may reuse.
     * Close through Winsock and deliberately leak the fd slot. */
    (void)fd;
    closesocket(s);
}

/*
 * poc_getsockname: getsockname on an unbound socket must fail with
 * WSAEINVAL - the docs' "socket has not been bound" case.
 */
static void validate_getsockname_unbound(void)
{
    SOCKET s = fresh_vsock_socket("getsockname(unbound)");
    struct sockaddr_vm addr;
    int addr_len = sizeof(addr);
    int rc = getsockname(s, (struct sockaddr *)&addr, &addr_len);
    int got = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    closesocket(s);

    if (rc != SOCKET_ERROR)
    {
        fprintf(stderr, "posix-validate: getsockname(unbound): expected SOCKET_ERROR + WSAEINVAL, got success\n");
        exit(EXIT_FAILURE);
    }
    if (got != WSAEINVAL)
    {
        fprintf(stderr, "posix-validate: getsockname(unbound): expected WSAEINVAL, got WSA %d\n", got);
        exit(EXIT_FAILURE);
    }
}

/*
 * poc_protocol_info: getsockopt(SO_PROTOCOL_INFOW) with a buffer too
 * small must fail with WSAEFAULT and update the length pointer to the
 * required size (sizeof(WSAPROTOCOL_INFOW)).  A full-size query must
 * then succeed and describe the provider viosocklib registers
 * (lib/install.c g_ProtocolInfo).
 */
static void validate_so_protocol_info_short(void)
{
    SOCKET s = fresh_vsock_socket("SO_PROTOCOL_INFOW(short)");
    WSAPROTOCOL_INFOW info;
    int info_len = 1;
    int rc = getsockopt(s, SOL_SOCKET, SO_PROTOCOL_INFOW, (char *)&info, &info_len);
    int got = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    int len_out = info_len;

    info_len = sizeof(info);
    int rc_full = getsockopt(s, SOL_SOCKET, SO_PROTOCOL_INFOW, (char *)&info, &info_len);
    int got_full = (rc_full == SOCKET_ERROR) ? WSAGetLastError() : 0;
    closesocket(s);

    if (rc != SOCKET_ERROR)
    {
        fprintf(stderr,
                "posix-validate: SO_PROTOCOL_INFOW(short buffer): expected SOCKET_ERROR + WSAEFAULT, got success\n");
        exit(EXIT_FAILURE);
    }
    if (got != WSAEFAULT)
    {
        fprintf(stderr, "posix-validate: SO_PROTOCOL_INFOW(short buffer): expected WSAEFAULT, got WSA %d\n", got);
        exit(EXIT_FAILURE);
    }
    if (len_out != (int)sizeof(WSAPROTOCOL_INFOW))
    {
        fprintf(stderr,
                "posix-validate: SO_PROTOCOL_INFOW(short buffer): expected optlen updated to %d, got %d\n",
                (int)sizeof(WSAPROTOCOL_INFOW),
                len_out);
        exit(EXIT_FAILURE);
    }

    if (rc_full == SOCKET_ERROR)
    {
        fprintf(stderr, "posix-validate: SO_PROTOCOL_INFOW: getsockopt failed WSA %d\n", got_full);
        exit(EXIT_FAILURE);
    }
    if (info.iVersion != 2 || info.iAddressFamily != (int)g_vsock_af || info.iSocketType != SOCK_STREAM ||
        info.iProtocol != 0 || info.iMinSockAddr != (int)sizeof(struct sockaddr_vm) ||
        info.iMaxSockAddr < info.iMinSockAddr || !(info.dwServiceFlags1 & XP1_IFS_HANDLES))
    {
        fprintf(stderr,
                "posix-validate: SO_PROTOCOL_INFOW: unexpected provider fields: iVersion=%d iAddressFamily=%d "
                "iSocketType=%d iProtocol=%d iMinSockAddr=%d iMaxSockAddr=%d dwServiceFlags1=0x%lx\n",
                info.iVersion,
                info.iAddressFamily,
                info.iSocketType,
                info.iProtocol,
                info.iMinSockAddr,
                info.iMaxSockAddr,
                (unsigned long)info.dwServiceFlags1);
        exit(EXIT_FAILURE);
    }
}

/*
 * select() with every fd_set NULL is docs-invalid per MSDN ("at
 * least one of readfds, writefds, or exceptfds must be a non-null
 * pointer") and must fail WSAEINVAL.  Cheap negative check that
 * runs the LSP select entry even before any test body.
 */
static void validate_select_all_null(void)
{
    int rc = select(0, NULL, NULL, NULL, NULL);
    int got = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    if (rc != SOCKET_ERROR)
    {
        fprintf(stderr, "posix-validate: select(all-NULL): expected SOCKET_ERROR + WSAEINVAL, got success\n");
        exit(EXIT_FAILURE);
    }
    if (got != WSAEINVAL)
    {
        fprintf(stderr, "posix-validate: select(all-NULL): expected WSAEINVAL, got WSA %d\n", got);
        exit(EXIT_FAILURE);
    }
}

/*
 * Positive timeout probe: a vsock socket bound to an ephemeral port,
 * sitting idle in readfds with a short finite timeout, has nothing
 * readable and must return 0 (timed out).  Complements
 * validate_select_all_null so both the error and the timeout branches
 * of the LSP select entry are exercised.
 */
static void validate_select_timeout(void)
{
    SOCKET s = fresh_vsock_socket("select(timeout)");
    struct sockaddr_vm addr = {0};
    addr.svm_family = g_vsock_af;
    addr.svm_cid = VMADDR_CID_ANY;
    addr.svm_port = VMADDR_PORT_ANY;
    if (bind(s, (const struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        fprintf(stderr, "posix-validate: select(timeout): bind failed WSA %d\n", WSAGetLastError());
        closesocket(s);
        exit(EXIT_FAILURE);
    }

    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 50 * 1000; /* 50 ms */
    int rc = select(0, &rfds, NULL, NULL, &tv);
    int got = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    closesocket(s);

    if (rc == SOCKET_ERROR)
    {
        fprintf(stderr, "posix-validate: select(timeout): expected 0 (timed out), got WSA %d\n", got);
        exit(EXIT_FAILURE);
    }
    if (rc != 0)
    {
        fprintf(stderr, "posix-validate: select(timeout): expected 0 (timed out), got %d ready fds\n", rc);
        exit(EXIT_FAILURE);
    }
}

/*
 * getsockopt(SO_ERROR) on a fresh, never-failed socket must succeed and
 * report 0.  A buffer smaller than the option value is rejected with
 * WSAEINVAL rather than silently truncated.
 */
static void validate_so_error_fresh(void)
{
    SOCKET s = fresh_vsock_socket("SO_ERROR(fresh)");
    int err = -1;
    int len = sizeof(err);
    int rc = getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
    if (rc == SOCKET_ERROR)
    {
        fprintf(stderr, "posix-validate: SO_ERROR(fresh): getsockopt failed WSA %d\n", WSAGetLastError());
        closesocket(s);
        exit(EXIT_FAILURE);
    }
    if (err != 0)
    {
        fprintf(stderr, "posix-validate: SO_ERROR(fresh): expected 0, got %d\n", err);
        closesocket(s);
        exit(EXIT_FAILURE);
    }

    char tiny;
    int tiny_len = sizeof(tiny);
    rc = getsockopt(s, SOL_SOCKET, SO_ERROR, &tiny, &tiny_len);
    int got = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    closesocket(s);
    if (rc != SOCKET_ERROR)
    {
        fprintf(stderr, "posix-validate: SO_ERROR(short buffer): expected SOCKET_ERROR + WSAEINVAL, got success\n");
        exit(EXIT_FAILURE);
    }
    if (got != WSAEINVAL)
    {
        fprintf(stderr, "posix-validate: SO_ERROR(short buffer): expected WSAEINVAL, got WSA %d\n", got);
        exit(EXIT_FAILURE);
    }
}

/*
 * An unknown SOL_SOCKET option must fail with WSAENOPROTOOPT for both
 * getsockopt and setsockopt (per the Winsock docs) - not WSAEOPNOTSUPP,
 * and not a silent success.
 */
static void validate_unknown_sockopt(void)
{
    SOCKET s = fresh_vsock_socket("unknown sockopt");
    int val = 0;
    int len = sizeof(val);

    int rc = getsockopt(s, SOL_SOCKET, 0x7777, (char *)&val, &len);
    int got = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    if (rc != SOCKET_ERROR)
    {
        fprintf(stderr, "posix-validate: getsockopt(unknown): expected SOCKET_ERROR + WSAENOPROTOOPT, got success\n");
        closesocket(s);
        exit(EXIT_FAILURE);
    }
    if (got != WSAENOPROTOOPT)
    {
        fprintf(stderr, "posix-validate: getsockopt(unknown): expected WSAENOPROTOOPT, got WSA %d\n", got);
        closesocket(s);
        exit(EXIT_FAILURE);
    }

    val = 0;
    rc = setsockopt(s, SOL_SOCKET, 0x7777, (const char *)&val, sizeof(val));
    got = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    closesocket(s);
    if (rc != SOCKET_ERROR)
    {
        fprintf(stderr, "posix-validate: setsockopt(unknown): expected SOCKET_ERROR + WSAENOPROTOOPT, got success\n");
        exit(EXIT_FAILURE);
    }
    if (got != WSAENOPROTOOPT)
    {
        fprintf(stderr, "posix-validate: setsockopt(unknown): expected WSAENOPROTOOPT, got WSA %d\n", got);
        exit(EXIT_FAILURE);
    }
}

/*
 * A close racing bind() and two concurrent bind()s on one socket must not
 * leave the socket in the driver's bound list: once the socket is closed,
 * its ports are bindable again, and only one of two concurrent binds wins
 * (the other gets WSAEINVAL, as for a bind on an already bound socket).
 * Timing-dependent: a pass is weak evidence, a failure is a real leak.
 */
#define BIND_RACE_ROUNDS  1000
#define BIND_RACE_SPAN_US 20

struct bind_race_round
{
    volatile LONG ready;
    volatile LONG go;
};

struct bind_race_ctx
{
    SOCKET s;
    unsigned int port;
    struct bind_race_round *round;
    LONGLONG delay;
    int rc;
    int err;
};

/* Per-process ports: the loopback sweep runs two vsock_test instances on one guest. */
static unsigned int bind_race_port(unsigned int slot)
{
    return 0x40000000u + ((unsigned int)GetCurrentProcessId() & 0xffffu) * 4u + slot;
}

static int bind_port(SOCKET s, unsigned int port)
{
    struct sockaddr_vm addr = {0};
    addr.svm_family = g_vsock_af;
    addr.svm_cid = VMADDR_CID_ANY;
    addr.svm_port = port;
    return bind(s, (const struct sockaddr *)&addr, sizeof(addr));
}

static void spin_ticks(LONGLONG ticks)
{
    LARGE_INTEGER now, end;

    QueryPerformanceCounter(&end);
    end.QuadPart += ticks;
    do
    {
        YieldProcessor();
        QueryPerformanceCounter(&now);
    } while (now.QuadPart < end.QuadPart);
}

/*
 * Signed start skew for a round, sweeping -span..+span QPC ticks: the two
 * paths take a near-constant time, so a fixed start would probe one point.
 */
static LONGLONG bind_race_skew(int round)
{
    LARGE_INTEGER freq;
    LONGLONG span;

    QueryPerformanceFrequency(&freq);
    span = freq.QuadPart * BIND_RACE_SPAN_US / 1000000;
    return (round % (2 * span + 1)) - span;
}

static DWORD WINAPI bind_race_worker(LPVOID arg)
{
    struct bind_race_ctx *c = arg;

    InterlockedIncrement(&c->round->ready);
    while (!c->round->go)
    {
        YieldProcessor();
    }
    spin_ticks(c->delay);
    c->rc = bind_port(c->s, c->port);
    c->err = WSAGetLastError();
    return 0;
}

static HANDLE bind_race_start(struct bind_race_ctx *c, const char *what)
{
    HANDLE th = CreateThread(NULL, 0, bind_race_worker, c, 0, NULL);
    if (!th)
    {
        fprintf(stderr, "posix-validate: %s: CreateThread failed: %lu\n", what, GetLastError());
        exit(EXIT_FAILURE);
    }
    return th;
}

/* Release the workers only once all are spinning, so thread start-up latency doesn't swamp the skew. */
static void bind_race_go(struct bind_race_round *r, LONG workers)
{
    while (r->ready < workers)
    {
        YieldProcessor();
    }
    InterlockedExchange(&r->go, 1);
}

static void bind_race_join(HANDLE th)
{
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
}

static void expect_port_free(unsigned int port, const char *what, int round)
{
    SOCKET s = fresh_vsock_socket(what);
    if (bind_port(s, port) == SOCKET_ERROR)
    {
        fprintf(stderr,
                "posix-validate: %s: round %d: port %u still bound after close: WSA %d\n",
                what,
                round,
                port,
                WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    closesocket(s);
}

static void validate_bind_close_race(void)
{
    const char *what = "bind/close race";
    unsigned int port = bind_race_port(0);
    int i;

    for (i = 0; i < BIND_RACE_ROUNDS; i++)
    {
        struct bind_race_round r = {0, 0};
        LONGLONG skew = bind_race_skew(i);
        struct bind_race_ctx c = {fresh_vsock_socket(what), port, &r, skew > 0 ? skew : 0, 0, 0};
        HANDLE th = bind_race_start(&c, what);

        bind_race_go(&r, 1);
        spin_ticks(skew < 0 ? -skew : 0);
        closesocket(c.s);
        bind_race_join(th);
        expect_port_free(port, what, i);
    }
}

static void validate_bind_concurrent(void)
{
    const char *what = "concurrent bind";
    int i;

    for (i = 0; i < BIND_RACE_ROUNDS; i++)
    {
        struct bind_race_round r = {0, 0};
        LONGLONG skew = bind_race_skew(i);
        SOCKET s = fresh_vsock_socket(what);
        struct bind_race_ctx c[2] = {{s, bind_race_port(1), &r, skew > 0 ? skew : 0, 0, 0},
                                     {s, bind_race_port(2), &r, skew < 0 ? -skew : 0, 0, 0}};
        HANDLE th;
        int loser;

        /* This thread binds c[0] itself: a third spinning thread would starve on 2 vCPUs. */
        th = bind_race_start(&c[1], what);
        bind_race_go(&r, 1);
        spin_ticks(c[0].delay);
        c[0].rc = bind_port(s, c[0].port);
        c[0].err = WSAGetLastError();
        bind_race_join(th);
        closesocket(s);

        loser = (c[0].rc == 0) ? 1 : 0;
        if ((c[0].rc == 0) == (c[1].rc == 0) || c[loser].err != WSAEINVAL)
        {
            fprintf(stderr,
                    "posix-validate: %s: round %d: expected one success and one WSAEINVAL, got rc=%d/%d WSA %d/%d\n",
                    what,
                    i,
                    c[0].rc,
                    c[1].rc,
                    c[0].err,
                    c[1].err);
            exit(EXIT_FAILURE);
        }
        expect_port_free(c[0].port, what, i);
        expect_port_free(c[1].port, what, i);
    }
}

void posix_validate_all(void)
{
    validate_getfiletype_and_osfhandle();
    validate_getsockname_unbound();
    validate_so_protocol_info_short();
    validate_so_error_fresh();
    validate_unknown_sockopt();
    validate_select_all_null();
    validate_select_timeout();
    validate_bind_close_race();
    validate_bind_concurrent();
}
