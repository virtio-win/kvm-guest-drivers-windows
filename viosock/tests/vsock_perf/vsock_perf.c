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
 *   - MSG_ZEROCOPY is not supported by the Windows viosock driver, so the
 *     --zerocopy sender path is omitted.
 *   - Monotonic timing uses current_nsec() from compat.h (QueryPerformanceCounter).
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

static void vsock_increase_buf_size(int fd)
{
    if (setsockopt(fd, g_vsock_af, SO_VM_SOCKETS_BUFFER_MAX_SIZE, &vsock_buf_bytes, sizeof(vsock_buf_bytes)))
    {
        error("setsockopt(SO_VM_SOCKETS_BUFFER_MAX_SIZE)");
    }

    if (setsockopt(fd, g_vsock_af, SO_VM_SOCKETS_BUFFER_SIZE, &vsock_buf_bytes, sizeof(vsock_buf_bytes)))
    {
        error("setsockopt(SO_VM_SOCKETS_BUFFER_SIZE)");
    }
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
static void run_receiver(int rcvlowat_bytes, bool use_poll)
{
    unsigned int read_cnt;
    long long rx_begin_ns;
    long long in_read_ns;
    size_t total_recv;
    int client_fd;
    char *data;
    int fd;
    struct sockaddr_vm svm = {
                                                                                                        .svm_family = (ADDRESS_FAMILY)g_vsock_af,
                                                                                                        .svm_port = port,
                                                                                                        .svm_cid = VMADDR_CID_ANY,
    };
    struct sockaddr_vm clientaddr;
    socklen_t clientaddr_len = sizeof(clientaddr);

    printf("Run as receiver\n");
    printf("Listen port %u\n", port);
    printf("RX buffer %llu bytes\n", buf_size_bytes);
    printf("vsock buffer %llu bytes\n", vsock_buf_bytes);
    printf("SO_RCVLOWAT %d bytes\n", rcvlowat_bytes);

    fd = socket(g_vsock_af, SOCK_STREAM, 0);

    if (fd < 0)
    {
        error("socket");
    }

    if (bind(fd, (struct sockaddr *)&svm, sizeof(svm)) < 0)
    {
        error("bind");
    }

    if (listen(fd, 1) < 0)
    {
        error("listen");
    }

    client_fd = accept(fd, (struct sockaddr *)&clientaddr, &clientaddr_len);

    if (client_fd < 0)
    {
        error("accept");
    }

    vsock_increase_buf_size(client_fd);

    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVLOWAT, &rcvlowat_bytes, sizeof(rcvlowat_bytes)))
    {
        error("setsockopt(SO_RCVLOWAT)");
    }

    data = malloc(buf_size_bytes);

    if (!data)
    {
        fprintf(stderr, "'malloc()' failed\n");
        exit(EXIT_FAILURE);
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
                error("poll");
            }

            if (fds.revents & POLLERR)
            {
                fprintf(stderr, "'poll()' error\n");
                exit(EXIT_FAILURE);
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
        bytes_read = read(client_fd, data, buf_size_bytes);
        in_read_ns += (current_nsec() - t);
        read_cnt++;

        if (bytes_read == 0)
        {
            break;
        }

        if (bytes_read < 0)
        {
            perror("read");
            exit(EXIT_FAILURE);
        }

        total_recv += bytes_read;
    }

    printf("total bytes received: %zu\n", total_recv);
    printf("rx performance: %f Gbits/s\n", get_gbps((unsigned long long)total_recv * 8, current_nsec() - rx_begin_ns));
    printf("total time in 'read()': %f sec\n", (float)in_read_ns / NSEC_PER_SEC);
    printf("average time in 'read()': %f ns\n", read_cnt ? (float)in_read_ns / read_cnt : 0.0f);
    printf("%s: %u\n", use_poll ? "POLLIN wakeups" : "read() calls", read_cnt);

    free(data);
    close(client_fd);
    close(fd);
}

static void run_sender(int peer_cid, unsigned long long to_send_bytes)
{
    long long tx_begin_ns;
    long long tx_total_ns;
    size_t total_send;
    long long time_in_send;
    void *data;
    int fd;

    printf("Run as sender\n");
    printf("Connect to %i:%u\n", peer_cid, port);
    printf("Send %llu bytes\n", to_send_bytes);
    printf("TX buffer %llu bytes\n", buf_size_bytes);

    fd = vsock_connect(peer_cid, port);

    if (fd < 0)
    {
        exit(EXIT_FAILURE);
    }

    data = malloc(buf_size_bytes);

    if (!data)
    {
        fprintf(stderr, "'malloc()' failed\n");
        exit(EXIT_FAILURE);
    }

    memset(data, 0, buf_size_bytes);
    total_send = 0;
    time_in_send = 0;
    tx_begin_ns = current_nsec();

    while (total_send < to_send_bytes)
    {
        ssize_t sent;
        size_t rest_bytes;
        long long before;

        rest_bytes = to_send_bytes - total_send;

        before = current_nsec();
        sent = send(fd, data, (rest_bytes > buf_size_bytes) ? buf_size_bytes : rest_bytes, 0);
        time_in_send += (current_nsec() - before);

        if (sent <= 0)
        {
            error("send");
        }

        total_send += sent;
    }

    tx_total_ns = current_nsec() - tx_begin_ns;

    printf("total bytes sent: %zu\n", total_send);
    printf("tx performance: %f Gbits/s\n", get_gbps((unsigned long long)total_send * 8, time_in_send));
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
static const struct
                                                                                                    option
                                                                                                                                                                                                        longopts
                                                                                                                                                                                                                                                                                                            [] =
                                                                                                                                                                                                                                                                                                                                                                                                                {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .name =
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            "help",
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .has_arg = no_argument,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .val = 'H',
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .name = "sender",
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .has_arg = required_argument,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .val = 'S',
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .name =
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            "port",
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .has_arg = required_argument,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .val = 'P',
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .name =
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            "bytes",
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .has_arg = required_argument,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .val = 'M',
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .name = "buf-size",
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .has_arg = required_argument,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .val = 'B',
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .name = "vsk-size",
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .has_arg = required_argument,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .val = 'V',
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .name =
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            "rcvlowat",
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .has_arg = required_argument,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        .val = 'R',
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {.name = "no-poll",
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     .has_arg = no_argument,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     .val = 'N'},
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {0},
};

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

int main(int argc, char **argv)
{
    unsigned long long to_send_bytes = DEFAULT_TO_SEND_BYTES;
    int rcvlowat_bytes = DEFAULT_RCVLOWAT_BYTES;
    int peer_cid = -1;
    bool sender = false;
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
            default:
                usage();
        }
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

    if (!sender)
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
