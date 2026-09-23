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
    /* _open_osfhandle transferred ownership; _close closes both the
     * CRT fd and the underlying socket.  Do NOT closesocket(s) after
     * _close - the handle is gone. */
    _close(fd);
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
 * required size (sizeof(WSAPROTOCOL_INFOW)).
 */
static void validate_so_protocol_info_short(void)
{
    SOCKET s = fresh_vsock_socket("SO_PROTOCOL_INFOW(short)");
    WSAPROTOCOL_INFOW info;
    int info_len = 1;
    int rc = getsockopt(s, SOL_SOCKET, SO_PROTOCOL_INFOW, (char *)&info, &info_len);
    int got = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    int len_out = info_len;
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
 * Positive timeout probe: an unbound vsock socket in readfds with a
 * short finite timeout has nothing readable and must return 0 (timed
 * out).  Complements validate_select_all_null so both the error and
 * the timeout branches of the LSP select entry are exercised.
 */
static void validate_select_timeout(void)
{
    SOCKET s = fresh_vsock_socket("select(timeout)");
    struct sockaddr_vm addr = {0};
    addr.svm_family = g_vsock_af;
    addr.svm_cid = VMADDR_CID_ANY;
    addr.svm_port = 0;
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

void posix_validate_all(void)
{
    validate_getfiletype_and_osfhandle();
    validate_getsockname_unbound();
    validate_so_protocol_info_short();
    validate_so_error_fresh();
    validate_unknown_sockopt();
    validate_select_all_null();
    validate_select_timeout();
}
