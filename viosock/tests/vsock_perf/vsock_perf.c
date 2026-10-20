// SPDX-License-Identifier: GPL-2.0-only
/*
 * vsock_perf - benchmark utility for vsock (Windows port).
 *
 * Ported from Linux tools/testing/vsock/vsock_perf.c.
 * Original Copyright (C) 2022 SberDevices. Author: Arseniy Krasnov <AVKrasnov@sberdevices.ru>.
 *
 * Windows differences from the Linux original:
 *   - The POSIX/Linux socket API is provided by compat.h (a Winsock2 shim).
 *   - AF_VSOCK is not a fixed constant; it is obtained at runtime via
 *     ViosockGetAF() and stored in g_vsock_af. The same value is used as the
 *     option level for the SO_VM_SOCKETS_* buffer options.
 *   - --zerocopy maps to the Windows viosock MSG_ZEROCOPY semantics: the flag
 *     rides on every send() and the call completes when the host has consumed
 *     the pinned pages (no SO_ZEROCOPY setsockopt, no MSG_ERRQUEUE completion
 *     reaping as on Linux).
 *   - Monotonic timing uses current_nsec() from compat.h (QueryPerformanceCounter).
 *   - --accept <n> (Windows-only): one receiver process serves <n> connections on
 *     one listener, a thread per connection.
 */

#include "compat.h"

#define DEFAULT_BUF_SIZE_BYTES  (128 * 1024)
#define DEFAULT_TO_SEND_BYTES   (64 * 1024)
#define DEFAULT_VSOCK_BUF_BYTES (256 * 1024)
#define DEFAULT_RCVLOWAT_BYTES  1
#define DEFAULT_PORT            1234

/* AF_VSOCK runtime value (set in main() via ViosockGetAF()). */
ADDRESS_FAMILY g_vsock_af = AF_UNSPEC;

static unsigned int port = DEFAULT_PORT;
/* 64-bit: on Windows (LLP64) 'unsigned long' is 32-bit, so a --bytes/--buf-size of
 * 4G or more would silently wrap to 0. Use 'unsigned long long' everywhere a byte
 * count is stored or returned. */
static unsigned long long buf_size_bytes = DEFAULT_BUF_SIZE_BYTES;
static bool msg_zerocopy = false;
static unsigned long long vsock_buf_bytes = DEFAULT_VSOCK_BUF_BYTES;

static void error(const char *s)
{
    perror(s);
    exit(EXIT_FAILURE);
}

/* From lib/cmdline.c. */
static unsigned long long memparse(const char *ptr)
{
    char *endptr;

    unsigned long long ret = strtoull(ptr, &endptr, 0);

    switch (*endptr)
    {
        case 'E':
        case 'e':
            ret <<= 10;
        case 'P':
        case 'p':
            ret <<= 10;
        case 'T':
        case 't':
            ret <<= 10;
        case 'G':
        case 'g':
            ret <<= 10;
        case 'M':
        case 'm':
            ret <<= 10;
        case 'K':
        case 'k':
            ret <<= 10;
            endptr++;
        default:
            break;
    }

    return ret;
}

/*
 * --accept N: one listener serves N connections, one thread each. A failure ends
 * only its own connection. Every line of a connection carries a "conn <i>: " tag
 * and is flushed at once, so a run killed by a timeout still shows which
 * connections finished.
 */
static unsigned int accept_conns = 0;

/* conn < 0 is the single-connection mode: exit on error as upstream does. */
static int rx_fail(int conn, const char *what, bool use_errno)
{
    if (conn < 0)
    {
        if (use_errno)
        {
            perror(what);
        }
        else
        {
            fprintf(stderr, "%s\n", what);
        }
        exit(EXIT_FAILURE);
    }

    if (use_errno)
    {
        fprintf(stderr, "conn %d: %s: %s\n", conn, what, strerror(errno));
    }
    else
    {
        fprintf(stderr, "conn %d: %s\n", conn, what);
    }
    fflush(stderr);
    return -1;
}

static int vsock_increase_buf_size(int fd, int conn)
{
    if (setsockopt(fd, g_vsock_af, SO_VM_SOCKETS_BUFFER_MAX_SIZE, &vsock_buf_bytes, sizeof(vsock_buf_bytes)))
    {
        return rx_fail(conn, "setsockopt(SO_VM_SOCKETS_BUFFER_MAX_SIZE)", true);
    }

    if (setsockopt(fd, g_vsock_af, SO_VM_SOCKETS_BUFFER_SIZE, &vsock_buf_bytes, sizeof(vsock_buf_bytes)))
    {
        return rx_fail(conn, "setsockopt(SO_VM_SOCKETS_BUFFER_SIZE)", true);
    }

    return 0;
}

static int vsock_connect(unsigned int cid, unsigned int connect_port)
{
    struct sockaddr_vm svm = {
                                                                                                        .svm_family = (ADDRESS_FAMILY)g_vsock_af,
                                                                                                        .svm_port = connect_port,
                                                                                                        .svm_cid = cid,
    };
    int fd;

    fd = socket(g_vsock_af, SOCK_STREAM, 0);

    if (fd < 0)
    {
        perror("socket");
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&svm, sizeof(svm)) < 0)
    {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

static float get_gbps(unsigned long long bits, long long ns_delta)
{
    return ((float)bits / 1000000000ULL) / ((float)ns_delta / NSEC_PER_SEC);
}

/*
 * --io=file: the inner loop's send() / read() become WriteFile() /
 * ReadFile() on the socket handle with an event OVERLAPPED, waited on
 * via GetOverlappedResult.  Reaches the driver's EvtIoWrite / EvtIoRead
 * instead of the IOCTL send / recv.  A failure prints its Win32 code and
 * sets errno to EIO for the caller's perror.
 */
static bool io_file = false;

#ifndef STATUS_PENDING
#define STATUS_PENDING ((DWORD)0x00000103L)
#endif

static ssize_t file_io(int fd, void *buf, size_t len, bool write)
{
    OVERLAPPED ov = {0};
    DWORD done = 0;
    BOOL ok;

    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent)
    {
        errno = ENOMEM;
        return -1;
    }
    /* Mark the op pending so GetOverlappedResult really waits on hEvent. */
    ov.Internal = (ULONG_PTR)STATUS_PENDING;

    ok = write ? WriteFile((HANDLE)(SOCKET)fd, buf, (DWORD)len, &done, &ov)
               : ReadFile((HANDLE)(SOCKET)fd, buf, (DWORD)len, &done, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
    {
        ok = GetOverlappedResult((HANDLE)(SOCKET)fd, &ov, &done, TRUE);
    }
    if (!ok && GetLastError() == ERROR_HANDLE_EOF)
    {
        ok = TRUE;
        done = 0;
    }
    if (!ok)
    {
        errno = EIO;
        fprintf(stderr, "%s failed: Win32 error %lu\n", write ? "WriteFile" : "ReadFile", GetLastError());
    }
    CloseHandle(ov.hEvent);
    return ok ? (ssize_t)done : -1;
}

static ssize_t perf_send_file(int fd, const void *buf, size_t len)
{
    return file_io(fd, (void *)buf, len, true);
}

static ssize_t perf_recv_file(int fd, void *buf, size_t len)
{
    return file_io(fd, buf, len, false);
}

/*
 * --no-poll (use_poll = false): switch the receiver's read loop to a
 * plain blocking read() without WSAPoll(POLLIN).
 *
 * The current github.com/virtio-win/kvm-guest-drivers-windows upstream
 * master does not implement WSAPoll on an accept()ed AF_VSOCK socket
 * — WSAPoll returns -1 with an untranslated Winsock error that our
 * compat.h maps to errno=0 ("No error"), so the receiver aborts before
 * the first recv. The blocking-read fallback is upstream-Linux-
 * compatible (POSIX read() on a vsock returns 0 at peer close), so
 * throughput semantics are preserved — only the wakeup accounting
 * changes from "POLLIN wakeups" to "read() calls".
 *
 * The flag is unused on the Linux side; upstream Linux vsock_perf
 * doesn't know about it. Pass it only to the Windows binary.
 */
/* Drain one accepted connection to EOF and report it. Returns 0 on EOF, -1 on a
 * failure (only in --accept mode; the single-connection mode exits instead). */
static int rx_drain(int client_fd, int rcvlowat_bytes, bool use_poll, int conn)
{
    unsigned int read_cnt;
    long long rx_begin_ns;
    long long in_read_ns;
    unsigned long long total_recv;
    char *data = NULL;
    char tag[32] = "";
    int rc = -1;

    if (conn >= 0)
    {
        snprintf(tag, sizeof(tag), "conn %d: ", conn);
    }

    if (vsock_increase_buf_size(client_fd, conn))
    {
        goto out;
    }

    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVLOWAT, &rcvlowat_bytes, sizeof(rcvlowat_bytes)))
    {
        rx_fail(conn, "setsockopt(SO_RCVLOWAT)", true);
        goto out;
    }

    data = malloc((size_t)buf_size_bytes);

    if (!data)
    {
        rx_fail(conn, "'malloc()' failed", false);
        goto out;
    }

    read_cnt = 0;
    in_read_ns = 0;
    total_recv = 0;
    rx_begin_ns = current_nsec();

    while (1)
    {
        ssize_t bytes_read;
        long long t;

        if (use_poll)
        {
            WSAPOLLFD fds = {0};

            fds.fd = client_fd;
            fds.events = POLLIN;

            if (poll(&fds, 1, -1) < 0)
            {
                rx_fail(conn, "poll", true);
                goto out;
            }

            if (fds.revents & POLLERR)
            {
                rx_fail(conn, "'poll()' error", false);
                goto out;
            }

            if (!(fds.revents & POLLIN))
            {
                if (fds.revents & POLLHUP)
                {
                    break;
                }
                continue;
            }
        }

        t = current_nsec();
        bytes_read = io_file ? perf_recv_file(client_fd, data, (size_t)buf_size_bytes)
                             : read(client_fd, data, (size_t)buf_size_bytes);
        in_read_ns += (current_nsec() - t);
        read_cnt++;

        if (bytes_read == 0)
        {
            break;
        }

        if (bytes_read < 0)
        {
            rx_fail(conn, "read", true);
            goto out;
        }

        total_recv += bytes_read;
    }

    printf("%stotal bytes received: %llu\n", tag, total_recv);
    printf("%srx performance: %f Gbits/s\n", tag, get_gbps(total_recv * 8, current_nsec() - rx_begin_ns));
    printf("%stotal time in 'read()': %f sec\n", tag, (float)in_read_ns / NSEC_PER_SEC);
    printf("%saverage time in 'read()': %f ns\n", tag, read_cnt ? (float)in_read_ns / read_cnt : 0.0f);
    printf("%s%s: %u\n", tag, use_poll ? "POLLIN wakeups" : "read() calls", read_cnt);
    fflush(stdout);
    rc = 0;

out:
    free(data);
    close(client_fd);
    return rc;
}

static int rx_listen(int backlog, int rcvlowat_bytes)
{
    int fd;
    struct sockaddr_vm svm = {
                                                                                                        .svm_family = (ADDRESS_FAMILY)g_vsock_af,
                                                                                                        .svm_port = port,
                                                                                                        .svm_cid = VMADDR_CID_ANY,
    };

    printf("Run as receiver\n");
    printf("Listen port %u\n", port);
    printf("RX buffer %llu bytes\n", buf_size_bytes);
    printf("vsock buffer %llu bytes\n", vsock_buf_bytes);
    printf("SO_RCVLOWAT %d bytes\n", rcvlowat_bytes);
    if (io_file)
    {
        printf("File I/O (ReadFile)\n");
    }

    fd = socket(g_vsock_af, SOCK_STREAM, 0);

    if (fd < 0)
    {
        error("socket");
    }

    if (bind(fd, (struct sockaddr *)&svm, sizeof(svm)) < 0)
    {
        error("bind");
    }

    if (listen(fd, backlog) < 0)
    {
        error("listen");
    }

    return fd;
}

static void run_receiver(int rcvlowat_bytes, bool use_poll)
{
    int client_fd;
    int fd;
    struct sockaddr_vm clientaddr;
    socklen_t clientaddr_len = sizeof(clientaddr);

    fd = rx_listen(1, rcvlowat_bytes);

    client_fd = accept(fd, (struct sockaddr *)&clientaddr, &clientaddr_len);

    if (client_fd < 0)
    {
        error("accept");
    }

    rx_drain(client_fd, rcvlowat_bytes, use_poll, -1);
    close(fd);
}

struct rx_conn
{
    int fd;
    int conn;
    int rcvlowat_bytes;
    bool use_poll;
};

static unsigned __stdcall rx_conn_thread(void *arg)
{
    struct rx_conn *c = arg;

    return rx_drain(c->fd, c->rcvlowat_bytes, c->use_poll, c->conn) ? 1 : 0;
}

/* Returns the number of connections that did not complete cleanly. */
static unsigned int run_receiver_accept(int rcvlowat_bytes, bool use_poll)
{
    struct rx_conn *conns;
    HANDLE *threads;
    unsigned int i, started = 0, failed = 0;
    int fd;

    conns = calloc(accept_conns, sizeof(*conns));
    threads = calloc(accept_conns, sizeof(*threads));
    if (!conns || !threads)
    {
        fprintf(stderr, "'calloc()' failed\n");
        exit(EXIT_FAILURE);
    }

    fd = rx_listen((int)accept_conns, rcvlowat_bytes);
    printf("Accept %u connections\n", accept_conns);
    fflush(stdout);

    for (i = 0; i < accept_conns; i++)
    {
        struct sockaddr_vm clientaddr;
        socklen_t clientaddr_len = sizeof(clientaddr);
        int client_fd = accept(fd, (struct sockaddr *)&clientaddr, &clientaddr_len);

        if (client_fd < 0)
        {
            rx_fail((int)i, "accept", true);
            break;
        }

        printf("conn %u: accepted\n", i);
        fflush(stdout);

        conns[i] = (struct rx_conn){client_fd, (int)i, rcvlowat_bytes, use_poll};
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, rx_conn_thread, &conns[i], 0, NULL);
        if (!threads[i])
        {
            rx_fail((int)i, "_beginthreadex", true);
            close(client_fd);
            break;
        }
        started++;
    }

    for (i = 0; i < started; i++)
    {
        DWORD code = 1;

        WaitForSingleObject(threads[i], INFINITE);
        GetExitCodeThread(threads[i], &code);
        CloseHandle(threads[i]);
        if (code)
        {
            failed++;
        }
    }
    failed += accept_conns - started;

    printf("connections completed: %u/%u\n", accept_conns - failed, accept_conns);
    fflush(stdout);

    close(fd);
    free(threads);
    free(conns);
    return failed;
}

static void run_sender(int peer_cid, unsigned long long to_send_bytes)
{
    long long tx_begin_ns;
    long long tx_total_ns;
    unsigned long long total_send;
    long long time_in_send;
    void *data;
    int fd;

    printf("Run as sender\n");
    printf("Connect to %i:%u\n", peer_cid, port);
    printf("Send %llu bytes\n", to_send_bytes);
    printf("TX buffer %llu bytes\n", buf_size_bytes);
    if (msg_zerocopy)
    {
        printf("Zero-copy send (MSG_ZEROCOPY)\n");
    }
    if (io_file)
    {
        printf("File I/O (WriteFile)\n");
    }

    fd = vsock_connect(peer_cid, port);

    if (fd < 0)
    {
        exit(EXIT_FAILURE);
    }

    data = malloc((size_t)buf_size_bytes);

    if (!data)
    {
        fprintf(stderr, "'malloc()' failed\n");
        exit(EXIT_FAILURE);
    }

    memset(data, 0, (size_t)buf_size_bytes);
    total_send = 0;
    time_in_send = 0;
    tx_begin_ns = current_nsec();

    while (total_send < to_send_bytes)
    {
        ssize_t sent;
        unsigned long long rest_bytes;
        long long before;

        rest_bytes = to_send_bytes - total_send;

        size_t chunk = (size_t)((rest_bytes > buf_size_bytes) ? buf_size_bytes : rest_bytes);

        before = current_nsec();
        sent = io_file ? perf_send_file(fd, data, chunk) : send(fd, data, chunk, msg_zerocopy ? MSG_ZEROCOPY : 0);
        time_in_send += (current_nsec() - before);

        if (sent <= 0)
        {
            error("send");
        }

        total_send += sent;
    }

    tx_total_ns = current_nsec() - tx_begin_ns;

    printf("total bytes sent: %llu\n", total_send);
    printf("tx performance: %f Gbits/s\n", get_gbps(total_send * 8, time_in_send));
    printf("total time in tx loop: %f sec\n", (float)tx_total_ns / NSEC_PER_SEC);
    printf("time in 'send()': %f sec\n", (float)time_in_send / NSEC_PER_SEC);

    close(fd);
    free(data);
}

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

static char *optarg = NULL;
static int optind = 1;
static int opterr = 1;
static int optopt = '?';

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
// clang-format off
static const struct option longopts[] = {
    {.name = "help",     .has_arg = no_argument,       .val = 'H'},
    {.name = "sender",   .has_arg = required_argument, .val = 'S'},
    {.name = "port",     .has_arg = required_argument, .val = 'P'},
    {.name = "bytes",    .has_arg = required_argument, .val = 'M'},
    {.name = "buf-size", .has_arg = required_argument, .val = 'B'},
    {.name = "vsk-size", .has_arg = required_argument, .val = 'V'},
    {.name = "rcvlowat", .has_arg = required_argument, .val = 'R'},
    {.name = "no-poll",  .has_arg = no_argument,       .val = 'N'},
    {.name = "zerocopy", .has_arg = no_argument,       .val = 'Z'},
    {.name = "io",       .has_arg = required_argument, .val = 'I'},
    {.name = "accept",   .has_arg = required_argument, .val = 'A'},
    {.name = "addrlist-query", .has_arg = no_argument,  .val = 'q'},
    {.name = "addrlist-watch", .has_arg = no_argument,  .val = 'w'},
    {0},
};
// clang-format on

static void usage(void)
{
    printf("Usage: vsock_perf [--help] [options]\n"
           "\n"
           "This is a benchmarking utility to test vsock performance.\n"
           "It runs in two modes: sender or receiver. In sender mode it\n"
           "connects to the specified CID and starts data transmission.\n"
           "\n"
           "Options:\n"
           "  --help                 This message\n"
           "  --sender   <cid>       Sender mode (receiver default)\n"
           "                         <cid> of the receiver to connect to\n"
           "  --port     <port>      Port (default %d)\n"
           "  --bytes    <bytes>KMG  Bytes to send (default %d)\n"
           "  --buf-size <bytes>KMG  Data buffer size (default %d). In sender mode\n"
           "                         it is the buffer size passed to 'send()'. In\n"
           "                         receiver mode it is the buffer size passed to 'read()'.\n"
           "  --vsk-size <bytes>KMG  Socket buffer size (default %d)\n"
           "  --rcvlowat <bytes>KMG  SO_RCVLOWAT value (default %d)\n"
           "  --no-poll              Receiver: skip WSAPoll and do a plain\n"
           "                         blocking read() loop. Use with viosock\n"
           "                         driver builds that don't implement\n"
           "                         WSAPoll on accept()ed vsock sockets\n"
           "                         (upstream master today).\n"
           "  --zerocopy             Sender: pass MSG_ZEROCOPY to every send().\n"
           "                         Windows viosock semantics: the pages are\n"
           "                         pinned and the call completes when the\n"
           "                         host has consumed them (no MSG_ERRQUEUE).\n"
           "  --io <wsa|file>        Data calls: send()/read() over Winsock (default)\n"
           "                         or WriteFile()/ReadFile() on the socket handle\n"
           "                         (EvtIoWrite/EvtIoRead in the driver). Not with\n"
           "                         --zerocopy: WriteFile carries no flags.\n"
           "  --accept   <n>         Receiver: accept <n> connections on the one\n"
           "                         listening port and drain each in its own\n"
           "                         thread. Lines are tagged \"conn <i>: \"; the\n"
           "                         exit code is non-zero if any connection failed.\n"
           "  --addrlist-query       Print the local vsock CID (SIO_ADDRESS_LIST_QUERY)\n"
           "                         and exit. Soft/observational.\n"
           "  --addrlist-watch       Block on SIO_ADDRESS_LIST_CHANGE and print each\n"
           "                         CID change with a timestamp (e.g. across a live\n"
           "                         migration); runs until killed.\n"
           "\n",
           DEFAULT_PORT,
           DEFAULT_TO_SEND_BYTES,
           DEFAULT_BUF_SIZE_BYTES,
           DEFAULT_VSOCK_BUF_BYTES,
           DEFAULT_RCVLOWAT_BYTES);
    exit(EXIT_FAILURE);
}

static long strtolx(const char *arg)
{
    long value;
    char *end;

    value = strtol(arg, &end, 10);

    if (end != arg + strlen(arg))
    {
        usage();
    }

    return value;
}

/* ------------------------------------------------------------------ */
/* SIO_ADDRESS_LIST monitor. Soft/observational: prints the local     */
/* vsock CID and watches SIO_ADDRESS_LIST_CHANGE, which fires when    */
/* the guest CID changes (e.g. across a live migration).              */
/* ------------------------------------------------------------------ */
static void addrlist_stamp(char *buf, size_t len)
{
    SYSTEMTIME st;

    GetLocalTime(&st);
    snprintf(buf, len, "%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

static int addrlist_query_cid(unsigned int *pCid)
{
    SOCKET s = socket(g_vsock_af, SOCK_STREAM, 0);
    char buf[256];
    DWORD ret = 0;
    int rc = -1;

    if (s == INVALID_SOCKET)
    {
        return -1;
    }

    if (WSAIoctl(s, SIO_ADDRESS_LIST_QUERY, NULL, 0, buf, sizeof(buf), &ret, NULL, NULL) == 0)
    {
        SOCKET_ADDRESS_LIST *list = (SOCKET_ADDRESS_LIST *)buf;

        if (list->iAddressCount > 0)
        {
            *pCid = ((struct sockaddr_vm *)list->Address[0].lpSockaddr)->svm_cid;
            rc = 0;
        }
    }

    closesocket(s);
    return rc;
}

/* One-shot: print the current CID and exit. Used for the start/exit samples
 * (the harness labels which is which). */
static void run_addrlist_query(void)
{
    unsigned int cid = 0;
    char ts[32];

    addrlist_stamp(ts, sizeof(ts));
    if (addrlist_query_cid(&cid) == 0)
    {
        printf("[%s] addrlist: CID = %u\n", ts, cid);
    }
    else
    {
        printf("[%s] addrlist: CID query failed (err=%d)\n", ts, WSAGetLastError());
    }
    fflush(stdout);
}

/* Loop: block on SIO_ADDRESS_LIST_CHANGE and print each change with the
 * moment it fired. Runs until the parent kills it. */
static void run_addrlist_watch(void)
{
    unsigned int cid = 0;
    char ts[32];

    if (addrlist_query_cid(&cid) != 0)
    {
        cid = (unsigned int)-1;
    }
    addrlist_stamp(ts, sizeof(ts));
    printf("[%s] addrlist: watch start, CID = %u\n", ts, cid);
    fflush(stdout);

    for (;;)
    {
        SOCKET s = socket(g_vsock_af, SOCK_STREAM, 0);
        unsigned int newcid = 0;
        DWORD ret = 0;
        int r;

        if (s == INVALID_SOCKET)
        {
            printf("addrlist: socket() failed err=%d\n", WSAGetLastError());
            break;
        }

        r = WSAIoctl(s, SIO_ADDRESS_LIST_CHANGE, NULL, 0, NULL, 0, &ret, NULL, NULL);
        addrlist_stamp(ts, sizeof(ts));
        if (r != 0)
        {
            printf("[%s] addrlist: SIO_ADDRESS_LIST_CHANGE err=%d, stop\n", ts, WSAGetLastError());
            fflush(stdout);
            closesocket(s);
            break;
        }

        if (addrlist_query_cid(&newcid) == 0)
        {
            printf("[%s] addrlist: CHANGE fired, CID %u -> %u\n", ts, cid, newcid);
            cid = newcid;
        }
        else
        {
            printf("[%s] addrlist: CHANGE fired, new CID query failed (err=%d)\n", ts, WSAGetLastError());
        }
        fflush(stdout);
        closesocket(s);
    }
}

int __cdecl main(int argc, char **argv)
{
    unsigned long long to_send_bytes = DEFAULT_TO_SEND_BYTES;
    int rcvlowat_bytes = DEFAULT_RCVLOWAT_BYTES;
    int peer_cid = -1;
    bool sender = false;
    bool addrlist_query = false;
    bool addrlist_watch = false;
    /* Receiver poll strategy: WSAPoll+read by default, blocking read()
     * only when the driver doesn't implement WSAPoll (see run_receiver). */
    bool use_poll = true;
    WSADATA wsa_data;

    while (1)
    {
        int opt = getopt_long(argc, argv, optstring, longopts, NULL);

        if (opt == -1)
        {
            break;
        }

        switch (opt)
        {
            case 'V': /* Peer buffer size. */
                vsock_buf_bytes = memparse(optarg);
                break;
            case 'R': /* SO_RCVLOWAT value. */
                /* setsockopt(SO_RCVLOWAT) takes int; memparse returns
                 * unsigned long long — cast is intentional. */
                rcvlowat_bytes = (int)memparse(optarg);
                break;
            case 'P': /* Port to connect to. */
                port = strtolx(optarg);
                break;
            case 'M': /* Bytes to send. */
                to_send_bytes = memparse(optarg);
                break;
            case 'B': /* Size of rx/tx buffer. */
                buf_size_bytes = memparse(optarg);
                break;
            case 'S': /* Sender mode. CID to connect to. */
                peer_cid = strtolx(optarg);
                sender = true;
                break;
            case 'H': /* Help. */
                usage();
                break;
            case 'N': /* Skip WSAPoll in the receiver (drivers without it). */
                use_poll = false;
                break;
            case 'Z': /* Sender: MSG_ZEROCOPY on every send. */
                msg_zerocopy = true;
                break;
            case 'I': /* Data calls: Winsock (wsa) or WriteFile/ReadFile (file). */
                if (strcmp(optarg, "file") == 0)
                {
                    io_file = true;
                }
                else if (strcmp(optarg, "wsa") != 0)
                {
                    usage();
                }
                break;
            case 'A': /* Receiver: N connections on one listener. */
                accept_conns = (unsigned int)strtolx(optarg);
                if (!accept_conns)
                {
                    usage();
                }
                break;
            case 'q': /* Address-list monitor: one-shot CID query. */
                addrlist_query = true;
                break;
            case 'w': /* Address-list monitor: watch SIO_ADDRESS_LIST_CHANGE. */
                addrlist_watch = true;
                break;
            default:
                usage();
        }
    }

    if (io_file && msg_zerocopy)
    {
        fprintf(stderr, "--zerocopy needs the Winsock path; drop it or use --io wsa\n");
        return EXIT_FAILURE;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data))
    {
        fprintf(stderr, "WSAStartup failed\n");
        return EXIT_FAILURE;
    }

    /* Obtain AF_VSOCK from the driver. */
    g_vsock_af = ViosockGetAF();
    if (g_vsock_af == AF_UNSPEC)
    {
        fprintf(stderr, "ViosockGetAF() failed -- is the viosock driver loaded?\n");
        WSACleanup();
        return EXIT_FAILURE;
    }
    fprintf(stderr, "AF_VSOCK = %d\n", (int)g_vsock_af);

    if (addrlist_query)
    {
        run_addrlist_query();
        WSACleanup();
        return 0;
    }
    if (addrlist_watch)
    {
        run_addrlist_watch();
        WSACleanup();
        return 0;
    }

    if (!sender && accept_conns)
    {
        unsigned int failed = run_receiver_accept(rcvlowat_bytes, use_poll);

        WSACleanup();
        return failed ? EXIT_FAILURE : 0;
    }
    else if (!sender)
    {
        run_receiver(rcvlowat_bytes, use_poll);
    }
    else
    {
        run_sender(peer_cid, to_send_bytes);
    }

    WSACleanup();
    return 0;
}
