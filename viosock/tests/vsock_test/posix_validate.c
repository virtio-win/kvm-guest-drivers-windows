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
        fprintf(stderr,
                "posix-validate: SO_PROTOCOL_INFOW(short buffer): expected WSAEFAULT, got WSA %d\n",
                got);
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

void posix_validate_all(void)
{
    validate_getfiletype_and_osfhandle();
    validate_getsockname_unbound();
    validate_so_protocol_info_short();
}
