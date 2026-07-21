// SPDX-License-Identifier: GPL-2.0-only
/*
 * vsock_test - vsock.ko test suite
 *
 * Copyright (C) 2017 Red Hat, Inc.
 * Author: Stefan Hajnoczi <stefanha@redhat.com>
 *
 * Windows port: Virtuozzo International GmbH
 *
 * The binary intentionally runs every test in test_cases[] by default;
 * selection (unimplemented paths, known regressions, upstream-only
 * debug hooks) is driven externally by the runner scripts in
 * viosock/tests/run/ and their per-direction *.list files. Tests whose
 * IDs must stay stable for --pick but cannot be run as-is on Windows
 * carry run_client/run_server = NULL in the table (no stub body):
 *   - MSG_ZEROCOPY family: blocked by the missing Winsock MSG_ERRQUEUE
 *     / sock_extended_err notification path — not by an absence of
 *     zero-copy in the driver tract (WSP already hands the kernel
 *     MDL-based user pages);
 *   - every SOCK_SEQPACKET case: SEQPACKET is not implemented in the
 *     viosock driver; upstream bodies can be dropped in verbatim once
 *     SEQPACKET (and recvmsg with MSG_EOR) land;
 *   - SIGPIPE, kmemleak, transport-uaf, transport-change: Linux-side
 *     kernel-debug or POSIX-signal regressions with no Windows analogue.
 *
 * run_tests() treats a NULL run pointer as a no-op (prints "ok") so a
 * per-side unimplemented direction (e.g. only .run_client set) still
 * completes cleanly, matching upstream util.c behavior.
 */

#include "compat.h"
#include "control.h"
#include "util.h"

/* Basic messages for control_writeulong()/control_readulong() */
#define CONTROL_CONTINUE 1
#define CONTROL_DONE     0

/* AF_VSOCK runtime value (set in main() via ViosockGetAF()). */
ADDRESS_FAMILY g_vsock_af = AF_UNSPEC;

static void test_stream_connection_reset(const struct test_opts *opts)
{
    union {
        struct sockaddr sa;
        struct sockaddr_vm svm;
    } addr = {
                                                                                                        .svm =
                                                                                                                                                                                                            {
                                                                                                                                                                                                                                                                                                                .svm_family = (ADDRESS_FAMILY)g_vsock_af,
                                                                                                                                                                                                                                                                                                                .svm_port = opts->peer_port,
                                                                                                                                                                                                                                                                                                                .svm_cid =
                                                                                                                                                                                                                                                                                                                                                                                                                    opts
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        ->peer_cid,
                                                                                                                                                                                                            },
    };
    int ret;
    int fd;

    fd = socket(g_vsock_af, SOCK_STREAM, 0);

    timeout_begin(TIMEOUT);
    do
    {
        ret = connect(fd, &addr.sa, sizeof(addr.svm));
        timeout_check("connect");
    } while (ret < 0 && errno == EINTR);
    timeout_end();

    if (ret != -1)
    {
        fprintf(stderr, "expected connect(2) failure, got %d\n", ret);
        exit(EXIT_FAILURE);
    }
    /* Linux vsock returns ECONNRESET for refused connections; viosock returns
     * ECONNREFUSED (standard behavior).  Accept both. */
    if (errno != ECONNRESET && errno != ECONNREFUSED)
    {
        fprintf(stderr, "unexpected connect(2) errno %d\n", errno);
        exit(EXIT_FAILURE);
    }

    close(fd);
}

static void test_stream_bind_only_client(const struct test_opts *opts)
{
    union {
        struct sockaddr sa;
        struct sockaddr_vm svm;
    } addr = {
                                                                                                        .svm =
                                                                                                                                                                                                            {
                                                                                                                                                                                                                                                                                                                .svm_family = (ADDRESS_FAMILY)g_vsock_af,
                                                                                                                                                                                                                                                                                                                .svm_port = opts->peer_port,
                                                                                                                                                                                                                                                                                                                .svm_cid =
                                                                                                                                                                                                                                                                                                                                                                                                                    opts
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        ->peer_cid,
                                                                                                                                                                                                            },
    };
    int ret;
    int fd;

    control_expectln("BIND");

    fd = socket(g_vsock_af, SOCK_STREAM, 0);

    timeout_begin(TIMEOUT);
    do
    {
        ret = connect(fd, &addr.sa, sizeof(addr.svm));
        timeout_check("connect");
    } while (ret < 0 && errno == EINTR);
    timeout_end();

    if (ret != -1)
    {
        fprintf(stderr, "expected connect(2) failure, got %d\n", ret);
        exit(EXIT_FAILURE);
    }
    /* Linux vsock returns ECONNRESET for refused connections; viosock returns
     * ECONNREFUSED (standard behavior).  Accept both. */
    if (errno != ECONNRESET && errno != ECONNREFUSED)
    {
        fprintf(stderr, "unexpected connect(2) errno %d\n", errno);
        exit(EXIT_FAILURE);
    }

    control_writeln("DONE");

    close(fd);
}

static void test_stream_bind_only_server(const struct test_opts *opts)
{
    int fd;

    fd = vsock_bind(VMADDR_CID_ANY, opts->peer_port, SOCK_STREAM);
    control_writeln("BIND");
    control_expectln("DONE");
    close(fd);
}

static void test_stream_client_close_client(const struct test_opts *opts)
{
    int fd;

    fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    send_byte(fd, 1, 0);
    close(fd);
}

static void test_stream_client_close_server(const struct test_opts *opts)
{
    int fd;

    fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    vsock_wait_remote_close(fd);

    send_byte(fd, -EPIPE, 0);
    recv_byte(fd, 1, 0);
    recv_byte(fd, 0, 0);
    close(fd);
}

static void test_stream_server_close_client(const struct test_opts *opts)
{
    int fd;

    fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    vsock_wait_remote_close(fd);

    send_byte(fd, -EPIPE, 0);
    recv_byte(fd, 1, 0);
    recv_byte(fd, 0, 0);
    close(fd);
}

static void test_stream_server_close_server(const struct test_opts *opts)
{
    int fd;

    fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    send_byte(fd, 1, 0);
    close(fd);
}

#define MULTICONN_NFDS 100

static void test_stream_multiconn_client(const struct test_opts *opts)
{
    int fds[MULTICONN_NFDS];
    int i;

    for (i = 0; i < MULTICONN_NFDS; i++)
    {
        fds[i] = vsock_stream_connect(opts->peer_cid, opts->peer_port);
        if (fds[i] < 0)
        {
            perror("connect");
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i < MULTICONN_NFDS; i++)
    {
        if (i % 2)
        {
            recv_byte(fds[i], 1, 0);
        }
        else
        {
            send_byte(fds[i], 1, 0);
        }
    }

    for (i = 0; i < MULTICONN_NFDS; i++)
    {
        close(fds[i]);
    }
}

static void test_stream_multiconn_server(const struct test_opts *opts)
{
    int fds[MULTICONN_NFDS];
    int i;

    for (i = 0; i < MULTICONN_NFDS; i++)
    {
        fds[i] = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
        if (fds[i] < 0)
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i < MULTICONN_NFDS; i++)
    {
        if (i % 2)
        {
            send_byte(fds[i], 1, 0);
        }
        else
        {
            recv_byte(fds[i], 1, 0);
        }
    }

    for (i = 0; i < MULTICONN_NFDS; i++)
    {
        close(fds[i]);
    }
}

#define MSG_PEEK_BUF_LEN 64

static void test_msg_peek_client(const struct test_opts *opts, bool seqpacket)
{
    unsigned char buf[MSG_PEEK_BUF_LEN];
    int fd;
    int i;

    if (seqpacket)
    {
        fd = vsock_seqpacket_connect(opts->peer_cid, opts->peer_port);
    }
    else
    {
        fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
    }

    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < (int)sizeof(buf); i++)
    {
        buf[i] = rand() & 0xFF;
    }

    control_expectln("SRVREADY");

    send_buf(fd, buf, sizeof(buf), 0, sizeof(buf));

    close(fd);
}

static void test_msg_peek_server(const struct test_opts *opts, bool seqpacket)
{
    unsigned char buf_half[MSG_PEEK_BUF_LEN / 2];
    unsigned char buf_normal[MSG_PEEK_BUF_LEN];
    unsigned char buf_peek[MSG_PEEK_BUF_LEN];
    int fd;

    if (seqpacket)
    {
        fd = vsock_seqpacket_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    }
    else
    {
        fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    }

    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    recv_buf(fd, buf_peek, sizeof(buf_peek), MSG_PEEK | MSG_DONTWAIT, -EAGAIN);

    control_writeln("SRVREADY");

    recv_buf(fd, buf_half, sizeof(buf_half), MSG_PEEK, sizeof(buf_half));
    recv_buf(fd, buf_peek, sizeof(buf_peek), MSG_PEEK, sizeof(buf_peek));

    if (memcmp(buf_half, buf_peek, sizeof(buf_half)))
    {
        fprintf(stderr, "Partial peek data mismatch\n");
        exit(EXIT_FAILURE);
    }

    if (seqpacket)
    {
        recv_buf(fd, buf_half, sizeof(buf_half), MSG_PEEK | MSG_TRUNC, sizeof(buf_peek));
    }

    recv_buf(fd, buf_normal, sizeof(buf_normal), 0, sizeof(buf_normal));

    if (memcmp(buf_peek, buf_normal, sizeof(buf_peek)))
    {
        fprintf(stderr, "Full peek data mismatch\n");
        exit(EXIT_FAILURE);
    }

    close(fd);
}

static void test_stream_msg_peek_client(const struct test_opts *opts)
{
    test_msg_peek_client(opts, false);
}

static void test_stream_msg_peek_server(const struct test_opts *opts)
{
    test_msg_peek_server(opts, false);
}

#define SOCK_BUF_SIZE       (2 * 1024 * 1024)
#define SOCK_BUF_SIZE_SMALL (64 * 1024)

#define RCVLOWAT_BUF_SIZE 128

static void test_stream_poll_rcvlowat_server(const struct test_opts *opts)
{
    int fd;
    int i;

    fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    send_byte(fd, 1, 0);
    control_writeln("SRVSENT");
    control_expectln("CLNSENT");

    for (i = 0; i < RCVLOWAT_BUF_SIZE - 1; i++)
    {
        send_byte(fd, 1, 0);
    }

    control_expectln("POLLDONE");
    close(fd);
}

static void test_stream_poll_rcvlowat_client(const struct test_opts *opts)
{
    int lowat_val = RCVLOWAT_BUF_SIZE;
    char buf[RCVLOWAT_BUF_SIZE];
    WSAPOLLFD fds;
    short poll_flags;
    int fd;

    fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    setsockopt_int_check(fd, SOL_SOCKET, SO_RCVLOWAT, lowat_val, "setsockopt(SO_RCVLOWAT)");

    control_expectln("SRVSENT");

    fds.fd = (SOCKET)fd;
    poll_flags = POLLIN | POLLRDNORM;
    fds.events = poll_flags;

    if (WSAPoll(&fds, 1, 1000) < 0)
    {
        perror("poll");
        exit(EXIT_FAILURE);
    }

    if (fds.revents)
    {
        fprintf(stderr, "Unexpected poll result %hx\n", fds.revents);
        exit(EXIT_FAILURE);
    }

    control_writeln("CLNSENT");

    if (WSAPoll(&fds, 1, 10000) < 0)
    {
        perror("poll");
        exit(EXIT_FAILURE);
    }

    // Windows WSAPoll reports POLLRDNORM for readable normal data. POLLRDBAND
    // (the other half of POLLIN) is only raised for OOB/priority data, which
    // vsock streams never carry, so revents == POLLRDNORM here. The Linux
    // original expected EPOLLIN|EPOLLRDNORM (separate bits, both set on Linux).
    if (fds.revents != POLLRDNORM)
    {
        fprintf(stderr, "Unexpected poll result %hx\n", fds.revents);
        exit(EXIT_FAILURE);
    }

    recv_buf(fd, buf, sizeof(buf), MSG_DONTWAIT, RCVLOWAT_BUF_SIZE);
    control_writeln("POLLDONE");
    close(fd);
}

#define INV_BUF_TEST_DATA_LEN 512

static void test_inv_buf_client(const struct test_opts *opts, bool stream)
{
    unsigned char data[INV_BUF_TEST_DATA_LEN] = {0};
    ssize_t expected_ret;
    int fd;

    if (stream)
    {
        fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
    }
    else
    {
        fd = vsock_seqpacket_connect(opts->peer_cid, opts->peer_port);
    }

    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    control_expectln("SENDDONE");

    recv_buf(fd, NULL, sizeof(data), 0, -EFAULT);

    if (stream)
    {
        expected_ret = sizeof(data);
    }
    else
    {
        expected_ret = -EAGAIN;
    }

    recv_buf(fd, data, sizeof(data), MSG_DONTWAIT, expected_ret);

    control_writeln("DONE");
    close(fd);
}

static void test_inv_buf_server(const struct test_opts *opts, bool stream)
{
    unsigned char data[INV_BUF_TEST_DATA_LEN] = {0};
    int fd;

    if (stream)
    {
        fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    }
    else
    {
        fd = vsock_seqpacket_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    }

    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    send_buf(fd, data, sizeof(data), 0, sizeof(data));
    control_writeln("SENDDONE");
    control_expectln("DONE");
    close(fd);
}

static void test_stream_inv_buf_client(const struct test_opts *opts)
{
    test_inv_buf_client(opts, true);
}
static void test_stream_inv_buf_server(const struct test_opts *opts)
{
    test_inv_buf_server(opts, true);
}
#define HELLO_STR "HELLO"
#define WORLD_STR "WORLD"

static void test_stream_virtio_skb_merge_client(const struct test_opts *opts)
{
    int fd;

    fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    send_buf(fd, HELLO_STR, strlen(HELLO_STR), 0, strlen(HELLO_STR));
    control_writeln("SEND0");
    control_expectln("REPLY0");

    send_buf(fd, WORLD_STR, strlen(WORLD_STR), 0, strlen(WORLD_STR));
    control_writeln("SEND1");
    control_expectln("REPLY1");

    close(fd);
}

static void test_stream_virtio_skb_merge_server(const struct test_opts *opts)
{
    size_t rd = 0, to_read;
    unsigned char buf[64];
    int fd;

    fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    control_expectln("SEND0");

    to_read = 2;
    recv_buf(fd, buf + rd, to_read, 0, to_read);
    rd += to_read;

    control_writeln("REPLY0");
    control_expectln("SEND1");

    to_read = strlen(HELLO_STR WORLD_STR) - rd;
    recv_buf(fd, buf + rd, to_read, 0, to_read);
    rd += to_read;

    to_read = sizeof(buf) - rd;
    recv_buf(fd, buf + rd, to_read, MSG_DONTWAIT, -EAGAIN);

    if (memcmp(buf, HELLO_STR WORLD_STR, strlen(HELLO_STR WORLD_STR)))
    {
        fprintf(stderr, "pattern mismatch\n");
        exit(EXIT_FAILURE);
    }

    control_writeln("REPLY1");
    close(fd);
}

static void test_double_bind_connect_server(const struct test_opts *opts)
{
    int listen_fd, client_fd, i;
    struct sockaddr_vm sa_client;
    socklen_t socklen_client = sizeof(sa_client);

    listen_fd = vsock_stream_listen(VMADDR_CID_ANY, opts->peer_port);

    for (i = 0; i < 2; i++)
    {
        control_writeln("LISTENING");

        timeout_begin(TIMEOUT);
        do
        {
            client_fd = accept(listen_fd, (struct sockaddr *)&sa_client, &socklen_client);
            timeout_check("accept");
        } while (client_fd < 0 && errno == EINTR);
        timeout_end();

        if (client_fd < 0)
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        vsock_wait_remote_close(client_fd);
    }

    close(listen_fd);
}

static void test_double_bind_connect_client(const struct test_opts *opts)
{
    int i, client_fd;

    for (i = 0; i < 2; i++)
    {
        control_expectln("LISTENING");

        client_fd = vsock_bind_connect(opts->peer_cid, opts->peer_port, opts->peer_port + 1, SOCK_STREAM);
        close(client_fd);
    }
}

#define MSG_BUF_IOCTL_LEN 64

static void test_unsent_bytes_server(const struct test_opts *opts, int type)
{
    unsigned char buf[MSG_BUF_IOCTL_LEN];
    int client_fd;

    client_fd = vsock_accept(VMADDR_CID_ANY, opts->peer_port, NULL, type);
    if (client_fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    recv_buf(client_fd, buf, sizeof(buf), 0, sizeof(buf));
    control_writeln("RECEIVED");
    close(client_fd);
}

static void test_unsent_bytes_client(const struct test_opts *opts, int type)
{
    unsigned char buf[MSG_BUF_IOCTL_LEN];
    int fd;

    fd = vsock_connect(opts->peer_cid, opts->peer_port, type);
    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < (int)sizeof(buf); i++)
    {
        buf[i] = rand() & 0xFF;
    }

    send_buf(fd, buf, sizeof(buf), 0, sizeof(buf));
    control_expectln("RECEIVED");

    if (!vsock_wait_sent(fd))
    {
        fprintf(stderr, "Test skipped, SIOCOUTQ not supported.\n");
    }

    close(fd);
}

static void test_unread_bytes_server(const struct test_opts *opts, int type)
{
    unsigned char buf[MSG_BUF_IOCTL_LEN];
    int client_fd;

    client_fd = vsock_accept(VMADDR_CID_ANY, opts->peer_port, NULL, type);
    if (client_fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < (int)sizeof(buf); i++)
    {
        buf[i] = rand() & 0xFF;
    }

    send_buf(client_fd, buf, sizeof(buf), 0, sizeof(buf));
    control_writeln("SENT");
    close(client_fd);
}

static void test_unread_bytes_client(const struct test_opts *opts, int type)
{
    unsigned char buf[MSG_BUF_IOCTL_LEN];
    int fd;

    fd = vsock_connect(opts->peer_cid, opts->peer_port, type);
    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    control_expectln("SENT");

    if (!vsock_ioctl_int(fd, SIOCINQ, MSG_BUF_IOCTL_LEN))
    {
        fprintf(stderr, "Test skipped, SIOCINQ not supported.\n");
        goto out;
    }

    recv_buf(fd, buf, sizeof(buf), 0, sizeof(buf));
    vsock_ioctl_int(fd, SIOCINQ, 0);

out:
    close(fd);
}

static void test_stream_unsent_bytes_client(const struct test_opts *opts)
{
    test_unsent_bytes_client(opts, SOCK_STREAM);
}
static void test_stream_unsent_bytes_server(const struct test_opts *opts)
{
    test_unsent_bytes_server(opts, SOCK_STREAM);
}
static void test_stream_unread_bytes_client(const struct test_opts *opts)
{
    test_unread_bytes_client(opts, SOCK_STREAM);
}
static void test_stream_unread_bytes_server(const struct test_opts *opts)
{
    test_unread_bytes_server(opts, SOCK_STREAM);
}
#define RCVLOWAT_CREDIT_UPD_BUF_SIZE  (1024 * 128)
#define VIRTIO_VSOCK_MAX_PKT_BUF_SIZE (1024 * 64)

static void test_stream_rcvlowat_def_cred_upd_client(const struct test_opts *opts)
{
    size_t buf_size;
    void *buf;
    int fd;

    fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    buf_size = RCVLOWAT_CREDIT_UPD_BUF_SIZE + 1;
    buf = malloc(buf_size);
    if (!buf)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    recv_byte(fd, 1, 0);

    if (send(fd, buf, buf_size, 0) != (ssize_t)buf_size)
    {
        perror("send failed");
        exit(EXIT_FAILURE);
    }

    free(buf);
    close(fd);
}

static void test_stream_credit_update_test(const struct test_opts *opts, bool low_rx_bytes_test)
{
    int recv_buf_size;
    WSAPOLLFD fds;
    size_t buf_size;
    unsigned long long sock_buf_size;
    void *buf;
    int fd;

    fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    buf_size = RCVLOWAT_CREDIT_UPD_BUF_SIZE;
    sock_buf_size = buf_size;

    setsockopt_ull_check(fd,
                         g_vsock_af,
                         SO_VM_SOCKETS_BUFFER_SIZE,
                         sock_buf_size,
                         "setsockopt(SO_VM_SOCKETS_BUFFER_SIZE)");

    if (low_rx_bytes_test)
    {
        recv_buf_size = 1 + VIRTIO_VSOCK_MAX_PKT_BUF_SIZE;
        setsockopt_int_check(fd, SOL_SOCKET, SO_RCVLOWAT, recv_buf_size, "setsockopt(SO_RCVLOWAT)");
    }

    send_byte(fd, 1, 0);

    buf = malloc(buf_size);
    if (!buf)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    fds.fd = (SOCKET)fd;
    fds.events = POLLIN;

    /* Read data until connection closes. */
    while (1)
    {
        int npoll;
        ssize_t bytes_read;

        npoll = WSAPoll(&fds, 1, TIMEOUT * 1000);
        if (npoll < 0)
        {
            perror("WSAPoll");
            exit(EXIT_FAILURE);
        }
        if (npoll == 0)
        {
            fprintf(stderr, "WSAPoll timed out\n");
            exit(EXIT_FAILURE);
        }

        if (!(fds.revents & POLLIN))
        {
            break;
        }

        bytes_read = recv(fd, buf, buf_size, 0);
        if (bytes_read <= 0)
        {
            break;
        }
    }

    if (low_rx_bytes_test)
    {
        /* Expect POLLIN | POLLRDNORM after SO_RCVLOWAT is met. */
        if (!(fds.revents & (POLLIN | POLLRDNORM)))
        {
            fprintf(stderr, "POLLIN | POLLRDNORM expected\n");
            exit(EXIT_FAILURE);
        }
    }

    free(buf);
    close(fd);
}

static void test_stream_cred_upd_on_low_rx_bytes(const struct test_opts *opts)
{
    test_stream_credit_update_test(opts, true);
}

static void test_stream_cred_upd_on_set_rcvlowat(const struct test_opts *opts)
{
    test_stream_credit_update_test(opts, false);
}

static void test_stream_connect_retry_client(const struct test_opts *opts)
{
    int fd;

    fd = socket(g_vsock_af, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (!vsock_connect_fd(fd, opts->peer_cid, opts->peer_port))
    {
        fprintf(stderr, "Unexpected connect() #1 success\n");
        exit(EXIT_FAILURE);
    }

    control_writeln("LISTEN");
    control_expectln("LISTENING");

    if (vsock_connect_fd(fd, opts->peer_cid, opts->peer_port))
    {
        perror("connect() #2");
        exit(EXIT_FAILURE);
    }

    close(fd);
}

static void test_stream_connect_retry_server(const struct test_opts *opts)
{
    int fd;

    control_expectln("LISTEN");

    fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    vsock_wait_remote_close(fd);
    close(fd);
}

static void test_stream_linger_client(const struct test_opts *opts)
{
    int fd;

    fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    enable_so_linger(fd, 1);
    close(fd);
}

static void test_stream_linger_server(const struct test_opts *opts)
{
    int fd;

    fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    vsock_wait_remote_close(fd);
    close(fd);
}

#define LINGER_TIMEOUT (TIMEOUT / 2)

static void test_stream_nolinger_client(const struct test_opts *opts)
{
    bool waited;
    long long ns;
    int fd;

    fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    enable_so_linger(fd, LINGER_TIMEOUT);
    send_byte(fd, 1, 0);
    waited = vsock_wait_sent(fd);

    ns = current_nsec();
    close(fd);
    ns = current_nsec() - ns;

    if (!waited)
    {
        fprintf(stderr, "Test skipped, SIOCOUTQ not supported.\n");
    }
    else if (DIV_ROUND_UP(ns, NSEC_PER_SEC) >= LINGER_TIMEOUT)
    {
        fprintf(stderr, "Unexpected lingering\n");
        exit(EXIT_FAILURE);
    }

    control_writeln("DONE");
}

static void test_stream_nolinger_server(const struct test_opts *opts)
{
    int fd;

    fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    control_expectln("DONE");
    close(fd);
}

static void test_stream_tx_credit_bounds_client(const struct test_opts *opts)
{
    unsigned long long sock_buf_size;
    size_t total = 0;
    char buf[4096];
    int fd;

    memset(buf, 'A', sizeof(buf));

    fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
    if (fd < 0)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    sock_buf_size = SOCK_BUF_SIZE_SMALL;

    setsockopt_ull_check(fd,
                         g_vsock_af,
                         SO_VM_SOCKETS_BUFFER_MAX_SIZE,
                         sock_buf_size,
                         "setsockopt(SO_VM_SOCKETS_BUFFER_MAX_SIZE)");

    setsockopt_ull_check(fd,
                         g_vsock_af,
                         SO_VM_SOCKETS_BUFFER_SIZE,
                         sock_buf_size,
                         "setsockopt(SO_VM_SOCKETS_BUFFER_SIZE)");

    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
    {
        perror("fcntl(F_SETFL)");
        exit(EXIT_FAILURE);
    }

    control_expectln("SRVREADY");

    for (;;)
    {
        ssize_t sent = send(fd, buf, sizeof(buf), 0);

        if (sent == 0)
        {
            fprintf(stderr, "unexpected EOF while sending bytes\n");
            exit(EXIT_FAILURE);
        }

        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN)
            {
                break;
            }
            perror("send");
            exit(EXIT_FAILURE);
        }

        total += sent;
    }

    control_writeln("CLIDONE");
    close(fd);

    if (total > sock_buf_size)
    {
        fprintf(stderr, "TX credit too large: queued %zu bytes (expected <= %llu)\n", total, sock_buf_size);
        exit(EXIT_FAILURE);
    }
}

static void test_stream_tx_credit_bounds_server(const struct test_opts *opts)
{
    unsigned long long sock_buf_size;
    int fd;

    fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
    if (fd < 0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    sock_buf_size = SOCK_BUF_SIZE;

    setsockopt_ull_check(fd,
                         g_vsock_af,
                         SO_VM_SOCKETS_BUFFER_MAX_SIZE,
                         sock_buf_size,
                         "setsockopt(SO_VM_SOCKETS_BUFFER_MAX_SIZE)");

    setsockopt_ull_check(fd,
                         g_vsock_af,
                         SO_VM_SOCKETS_BUFFER_SIZE,
                         sock_buf_size,
                         "setsockopt(SO_VM_SOCKETS_BUFFER_SIZE)");

    control_writeln("SRVREADY");
    control_expectln("CLIDONE");
    close(fd);
}

static struct test_case test_cases[] = {
    {
        .name = "SOCK_STREAM connection reset",
        .run_client = test_stream_connection_reset,
    },
    {
        .name = "SOCK_STREAM bind only",
        .run_client = test_stream_bind_only_client,
        .run_server = test_stream_bind_only_server,
    },
    {
        .name = "SOCK_STREAM client close",
        .run_client = test_stream_client_close_client,
        .run_server = test_stream_client_close_server,
    },
    {
        .name = "SOCK_STREAM server close",
        .run_client = test_stream_server_close_client,
        .run_server = test_stream_server_close_server,
    },
    {
        .name = "SOCK_STREAM multiple connections",
        .run_client = test_stream_multiconn_client,
        .run_server = test_stream_multiconn_server,
    },
    {
        .name = "SOCK_STREAM MSG_PEEK",
        .run_client = test_stream_msg_peek_client,
        .run_server = test_stream_msg_peek_server,
    },
    {
        /* TODO: port when SEQPACKET + recvmsg(MSG_EOR) land on Windows */
        .name = "SOCK_SEQPACKET msg bounds",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* TODO: port when SEQPACKET + recvmsg land on Windows */
        .name = "SOCK_SEQPACKET MSG_TRUNC flag",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* SEQPACKET not implemented in viosock driver */
        .name = "SOCK_SEQPACKET timeout",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* SEQPACKET not implemented in viosock driver */
        .name = "SOCK_SEQPACKET invalid receive buffer",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        .name = "SOCK_STREAM poll() + SO_RCVLOWAT",
        .run_client = test_stream_poll_rcvlowat_client,
        .run_server = test_stream_poll_rcvlowat_server,
    },
    {
        /* SEQPACKET not implemented in viosock driver */
        .name = "SOCK_SEQPACKET big message",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        .name = "SOCK_STREAM test invalid buffer",
        .run_client = test_stream_inv_buf_client,
        .run_server = test_stream_inv_buf_server,
    },
    {
        /* SEQPACKET not implemented in viosock driver */
        .name = "SOCK_SEQPACKET test invalid buffer",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        .name = "SOCK_STREAM virtio skb merge",
        .run_client = test_stream_virtio_skb_merge_client,
        .run_server = test_stream_virtio_skb_merge_server,
    },
    {
        /* SEQPACKET not implemented in viosock driver */
        .name = "SOCK_SEQPACKET MSG_PEEK",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* Linux-only: test body is a SIGPIPE regression; Windows has no SIGPIPE */
        .name = "SOCK_STREAM SHUT_WR",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* Linux-only: same SIGPIPE dependency */
        .name = "SOCK_STREAM SHUT_RD",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* Winsock has no MSG_ERRQUEUE / sock_extended_err notification path
         * that the POSIX MSG_ZEROCOPY completion protocol depends on. */
        .name = "SOCK_STREAM MSG_ZEROCOPY",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* SEQPACKET not implemented (also no Winsock MSG_ERRQUEUE) */
        .name = "SOCK_SEQPACKET MSG_ZEROCOPY",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* Same: no MSG_ERRQUEUE — cannot even query the empty state. */
        .name = "SOCK_STREAM MSG_ZEROCOPY empty MSG_ERRQUEUE",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        .name = "SOCK_STREAM double bind connect",
        .run_client = test_double_bind_connect_client,
        .run_server = test_double_bind_connect_server,
    },
    {
        .name = "SOCK_STREAM virtio credit update + SO_RCVLOWAT",
        .run_client = test_stream_rcvlowat_def_cred_upd_client,
        .run_server = test_stream_cred_upd_on_set_rcvlowat,
    },
    {
        .name = "SOCK_STREAM virtio credit update + low rx_bytes",
        .run_client = test_stream_rcvlowat_def_cred_upd_client,
        .run_server = test_stream_cred_upd_on_low_rx_bytes,
    },
    {
        .name = "SOCK_STREAM ioctl(SIOCOUTQ) 0 unsent bytes",
        .run_client = test_stream_unsent_bytes_client,
        .run_server = test_stream_unsent_bytes_server,
    },
    {
        /* SEQPACKET not implemented in viosock driver */
        .name = "SOCK_SEQPACKET ioctl(SIOCOUTQ) 0 unsent bytes",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* Linux-only: kernel-side accept-queue kmemleak regression */
        .name = "SOCK_STREAM leak accept queue",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* Same MSG_ERRQUEUE gap, plus this is a Linux-kmemleak regression
         * test — no equivalent debug facility on Windows. */
        .name = "SOCK_STREAM MSG_ZEROCOPY leak MSG_ERRQUEUE",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* Same — depends on Linux-kmemleak against zerocopy skbs. */
        .name = "SOCK_STREAM MSG_ZEROCOPY leak completion skb",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* Linux-only: /proc/kallsyms + SOCK_NONBLOCK + multi-transport lifetime */
        .name = "SOCK_STREAM transport release use-after-free",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        .name = "SOCK_STREAM retry failed connect()",
        .run_client = test_stream_connect_retry_client,
        .run_server = test_stream_connect_retry_server,
    },
    {
        .name = "SOCK_STREAM SO_LINGER null-ptr-deref",
        .run_client = test_stream_linger_client,
        .run_server = test_stream_linger_server,
    },
    {
        .name = "SOCK_STREAM SO_LINGER close() on unread",
        .run_client = test_stream_nolinger_client,
        .run_server = test_stream_nolinger_server,
    },
    {
        /* Linux-only: needs pthread + kill(SIGUSR1) to race transport change */
        .name = "SOCK_STREAM transport change null-ptr-deref, lockdep warn",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        .name = "SOCK_STREAM ioctl(SIOCINQ) functionality",
        .run_client = test_stream_unread_bytes_client,
        .run_server = test_stream_unread_bytes_server,
    },
    {
        /* SEQPACKET not implemented in viosock driver */
        .name = "SOCK_SEQPACKET ioctl(SIOCINQ) functionality",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* Regression test against a Linux kernel setsockopt(SO_ZEROCOPY)
         * bug on accept()ed fds; Winsock has no SO_ZEROCOPY at all. */
        .name = "SOCK_STREAM accept()ed socket custom setsockopt()",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        /* Same MSG_ERRQUEUE gap — coalescence check reads notifications. */
        .name = "SOCK_STREAM virtio MSG_ZEROCOPY coalescence corruption",
        .run_client = NULL,
        .run_server = NULL,
    },
    {
        .name = "SOCK_STREAM TX credit bounds",
        .run_client = test_stream_tx_credit_bounds_client,
        .run_server = test_stream_tx_credit_bounds_server,
    },
    {0},
};

/* ------------------------------------------------------------------ */
/* getopt / getopt_long (MSVC CRT has no <getopt.h>)                    */
/* ------------------------------------------------------------------ */

struct option
{
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

#define no_argument       0
#define required_argument 1
#define optional_argument 2

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = '?';

static int sp = 1;

static int getopt(int argc, char *const argv[], const char *optstring)
{
    const char *oli;

    if (sp == 1)
    {
        if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
        {
            return -1;
        }
        if (argv[optind][1] == '-' && argv[optind][2] == '\0')
        {
            ++optind;
            return -1;
        }
    }

    optopt = (unsigned char)argv[optind][sp];
    oli = strchr(optstring, optopt);
    if (!oli || optopt == ':')
    {
        if (opterr && *optstring != ':')
        {
            fprintf(stderr, "illegal option -- %c\n", optopt);
        }
        if (argv[optind][++sp] == '\0')
        {
            ++optind;
            sp = 1;
        }
        return '?';
    }

    if (oli[1] != ':')
    {
        optarg = NULL;
        if (argv[optind][++sp] == '\0')
        {
            ++optind;
            sp = 1;
        }
    }
    else
    {
        if (argv[optind][sp + 1] != '\0')
        {
            optarg = (char *)&argv[optind++][sp + 1];
        }
        else if (++optind >= argc)
        {
            sp = 1;
            if (*optstring == ':')
            {
                return ':';
            }
            if (opterr)
            {
                fprintf(stderr, "option requires an argument -- %c\n", optopt);
            }
            return '?';
        }
        else
        {
            optarg = argv[optind++];
        }
        sp = 1;
    }
    return optopt;
}

static int getopt_long(int argc,
                       char *const argv[],
                       const char *optstring,
                       const struct option *longopts,
                       int *longindex)
{
    if (optind < argc && argv[optind][0] == '-' && argv[optind][1] == '-')
    {
        const char *name = argv[optind] + 2;
        const char *eq = strchr(name, '=');
        size_t nlen = eq ? (size_t)(eq - name) : strlen(name);
        int i;

        if (nlen == 0)
        {
            ++optind;
            return -1;
        }

        for (i = 0; longopts[i].name; i++)
        {
            if (strncmp(longopts[i].name, name, nlen) != 0 || strlen(longopts[i].name) != nlen)
            {
                continue;
            }

            if (longindex)
            {
                *longindex = i;
            }
            ++optind;

            if (longopts[i].has_arg == required_argument || longopts[i].has_arg == optional_argument)
            {
                if (eq)
                {
                    optarg = (char *)(eq + 1);
                }
                else if (longopts[i].has_arg == required_argument)
                {
                    if (optind >= argc)
                    {
                        if (opterr)
                        {
                            fprintf(stderr, "option '--%.*s' requires an argument\n", (int)nlen, name);
                        }
                        return '?';
                    }
                    optarg = argv[optind++];
                }
                else
                {
                    optarg = NULL;
                }
            }
            else
            {
                optarg = NULL;
            }

            if (longopts[i].flag)
            {
                *longopts[i].flag = longopts[i].val;
                return 0;
            }
            return longopts[i].val;
        }

        if (opterr)
        {
            fprintf(stderr, "unrecognized option '--%.*s'\n", (int)nlen, name);
        }
        ++optind;
        return '?';
    }

    return getopt(argc, argv, optstring);
}

static const char optstring[] = "";
static const struct option longopts[] = {
                                                                                                    {"control-host",
                                                                                                     required_argument,
                                                                                                     NULL,
                                                                                                     'H'},
                                                                                                    {"control-port",
                                                                                                     required_argument,
                                                                                                     NULL,
                                                                                                     'P'},
                                                                                                    {"mode",
                                                                                                     required_argument,
                                                                                                     NULL,
                                                                                                     'm'},
                                                                                                    {"peer-cid",
                                                                                                     required_argument,
                                                                                                     NULL,
                                                                                                     'p'},
                                                                                                    {"peer-port",
                                                                                                     required_argument,
                                                                                                     NULL,
                                                                                                     'q'},
                                                                                                    {"list",
                                                                                                     no_argument,
                                                                                                     NULL,
                                                                                                     'l'},
                                                                                                    {"skip",
                                                                                                     required_argument,
                                                                                                     NULL,
                                                                                                     's'},
                                                                                                    {"pick",
                                                                                                     required_argument,
                                                                                                     NULL,
                                                                                                     't'},
                                                                                                    {"help",
                                                                                                     no_argument,
                                                                                                     NULL,
                                                                                                     '?'},
                                                                                                    {NULL, 0, NULL, 0},
};

static void usage(void)
{
    fprintf(stderr,
            "Usage: vsock_test [--help] [--control-host=<host>] "
            "--control-port=<port> --mode=client|server --peer-cid=<cid> "
            "[--peer-port=<port>] [--list] [--skip=<test_id>]\n"
            "\n"
            "  Server: vsock_test --control-port=1234 --mode=server --peer-cid=3\n"
            "  Client: vsock_test --control-host=192.168.0.1 "
            "--control-port=1234 --mode=client --peer-cid=2\n"
            "\n"
            "Options:\n"
            "  --control-host <host>  Server IP to connect to (client mode)\n"
            "  --control-port <port>  TCP port for test synchronization\n"
            "  --mode client|server   Run as client or server\n"
            "  --peer-cid <cid>       CID of the remote side\n"
            "  --peer-port <port>     AF_VSOCK port [default: %d]\n"
            "  --list                 List all tests\n"
            "  --pick <id>            Run only this test (repeatable)\n"
            "  --skip <id>            Skip this test (repeatable)\n",
            DEFAULT_PEER_PORT);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    WSADATA wsa_data;
    const char *control_host = NULL;
    const char *control_port = NULL;
    struct test_opts opts = {
                                                                                                        .mode = TEST_MODE_UNSET,
                                                                                                        .peer_cid = (unsigned int)VMADDR_CID_ANY,
                                                                                                        .peer_port = DEFAULT_PEER_PORT,
    };

    /* Initialize Winsock2 */
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data))
    {
        fprintf(stderr, "WSAStartup failed\n");
        return EXIT_FAILURE;
    }

    /* Obtain AF_VSOCK from the driver */
    g_vsock_af = ViosockGetAF();
    if (g_vsock_af == AF_UNSPEC)
    {
        fprintf(stderr, "ViosockGetAF() failed -- is the viosock driver loaded?\n");
        WSACleanup();
        return EXIT_FAILURE;
    }
    fprintf(stderr, "AF_VSOCK = %d\n", (int)g_vsock_af);

    srand((unsigned int)time(NULL));
    init_signals();

    for (;;)
    {
        int opt = getopt_long(argc, argv, optstring, longopts, NULL);

        if (opt == -1)
        {
            break;
        }

        switch (opt)
        {
            case 'H':
                control_host = optarg;
                break;
            case 'm':
                if (strcmp(optarg, "client") == 0)
                {
                    opts.mode = TEST_MODE_CLIENT;
                }
                else if (strcmp(optarg, "server") == 0)
                {
                    opts.mode = TEST_MODE_SERVER;
                }
                else
                {
                    fprintf(stderr, "--mode must be \"client\" or \"server\"\n");
                    goto fail;
                }
                break;
            case 'p':
                opts.peer_cid = parse_cid(optarg);
                break;
            case 'q':
                opts.peer_port = parse_port(optarg);
                break;
            case 'P':
                control_port = optarg;
                break;
            case 'l':
                list_tests(test_cases);
                break;
            case 's':
                skip_test(test_cases, ARRAY_SIZE(test_cases) - 1, optarg);
                break;
            case 't':
                pick_test(test_cases, ARRAY_SIZE(test_cases) - 1, optarg);
                break;
            case '?':
            default:
                usage();
        }
    }

    if (!control_port)
    {
        usage();
    }
    if (opts.mode == TEST_MODE_UNSET)
    {
        usage();
    }
    if (opts.peer_cid == (unsigned int)VMADDR_CID_ANY)
    {
        usage();
    }

    if (!control_host)
    {
        if (opts.mode != TEST_MODE_SERVER)
        {
            usage();
        }
        control_host = "0.0.0.0";
    }

    control_init(control_host, control_port, opts.mode == TEST_MODE_SERVER);
    run_tests(test_cases, &opts);
    control_cleanup();

    WSACleanup();
    return EXIT_SUCCESS;

fail:
    WSACleanup();
    return EXIT_FAILURE;
}
