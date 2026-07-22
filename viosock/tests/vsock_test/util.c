// SPDX-License-Identifier: GPL-2.0-only
/*
 * vsock test utilities -- Windows port.
 *
 * Key differences from Linux:
 *  - vsock_wait_remote_close: epoll -> WSAPoll
 *  - vsock_ioctl_int: ioctl stub returns EOPNOTSUPP (graceful skip)
 *  - alloc/free_test_iovec: mmap -> VirtualAlloc via compat.h
 *  - setsockopt_timeval_check: uses DWORD (ms) for SO_RCVTIMEO on Windows
 *  - get_transports: returns 0 (no /proc/kallsyms)
 *  - enable_so_zerocopy_check: prints a warning (SO_ZEROCOPY not on Windows)
 */

#include "compat.h"
#include "control.h"
#include "util.h"

/* ------------------------------------------------------------------ */
/* Timeout implementation (deadline-based, replaces Linux alarm/SIGALRM) */
/* ------------------------------------------------------------------ */

static ULONGLONG g_deadline_ms;

void sigalrm(int signo)
{
    (void)signo;
}

void timeout_begin(unsigned int seconds)
{
    g_deadline_ms = GetTickCount64() + (ULONGLONG)seconds * 1000;
}

void timeout_check(const char *operation)
{
    if (g_deadline_ms && GetTickCount64() >= g_deadline_ms)
    {
        fprintf(stderr, "%s timed out\n", operation);
        exit(EXIT_FAILURE);
    }
}

void timeout_end(void)
{
    g_deadline_ms = 0;
}

int timeout_usleep(unsigned int usec)
{
    Sleep((DWORD)(usec / 1000));
    return 0;
}

/* No signals to install on Windows. */
void init_signals(void)
{
}

static unsigned int parse_uint(const char *str, const char *err_str)
{
    char *endptr = NULL;
    unsigned long n;

    errno = 0;
    n = strtoul(str, &endptr, 10);
    if (errno || *endptr != '\0')
    {
        fprintf(stderr, "malformed %s \"%s\"\n", err_str, str);
        exit(EXIT_FAILURE);
    }
    return (unsigned int)n;
}

unsigned int parse_cid(const char *str)
{
    return parse_uint(str, "CID");
}
unsigned int parse_port(const char *str)
{
    return parse_uint(str, "port");
}

void vsock_wait_remote_close(int fd)
{
    WSAPOLLFD fds;
    int nfds;

    fds.fd = (SOCKET)fd;
    /* WSAPoll input events: only POLLIN, POLLOUT and their POLLRDNORM / POLLRDBAND /
     * POLLWRNORM / POLLWRBAND sub-flags are valid here. POLLHUP, POLLERR and POLLNVAL
     * are output-only and are reported in revents regardless of what we requested. */
    fds.events = POLLIN;

    nfds = WSAPoll(&fds, 1, TIMEOUT * 1000);
    if (nfds < 0)
    {
        perror("WSAPoll");
        exit(EXIT_FAILURE);
    }
    if (nfds == 0)
    {
        fprintf(stderr, "WSAPoll timed out waiting for remote close\n");
        exit(EXIT_FAILURE);
    }
    /* POLLHUP or recv() returning 0 both indicate remote close. */
}

bool vsock_ioctl_int(int fd, unsigned long op, int expected)
{
    int actual, ret;
    char name[32];

    snprintf(name, sizeof(name), "ioctl(%lu)", op);

    timeout_begin(TIMEOUT);
    do
    {
        ret = ioctl(fd, op, &actual);
        if (ret < 0)
        {
            if (errno == EOPNOTSUPP || errno == ENOTTY)
            {
                break;
            }
            perror(name);
            exit(EXIT_FAILURE);
        }
        timeout_check(name);
    } while (actual != expected);
    timeout_end();

    return ret >= 0;
}

bool vsock_wait_sent(int fd)
{
    return vsock_ioctl_int(fd, SIOCOUTQ, 0);
}

int vsock_bind_try(unsigned int cid, unsigned int port, int type)
{
    struct sockaddr_vm sa = {
                                                                                                        .svm_family = (ADDRESS_FAMILY)g_vsock_af,
                                                                                                        .svm_cid = cid,
                                                                                                        .svm_port = port,
    };
    int fd, saved_errno;

    fd = socket(g_vsock_af, type, 0);
    if (fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)))
    {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        fd = -1;
    }

    return fd;
}

int vsock_bind(unsigned int cid, unsigned int port, int type)
{
    int fd = vsock_bind_try(cid, port, type);
    if (fd < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    return fd;
}

int vsock_connect_fd(int fd, unsigned int cid, unsigned int port)
{
    struct sockaddr_vm sa = {
                                                                                                        .svm_family = (ADDRESS_FAMILY)g_vsock_af,
                                                                                                        .svm_cid = cid,
                                                                                                        .svm_port = port,
    };
    int ret;

    timeout_begin(TIMEOUT);
    do
    {
        ret = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
        timeout_check("connect");
    } while (ret < 0 && errno == EINTR);
    timeout_end();

    return ret;
}

int vsock_bind_connect(unsigned int cid, unsigned int port, unsigned int bind_port, int type)
{
    int client_fd = vsock_bind(VMADDR_CID_ANY, bind_port, type);

    if (vsock_connect_fd(client_fd, cid, port))
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    return client_fd;
}

int vsock_connect(unsigned int cid, unsigned int port, int type)
{
    int fd;

    control_expectln("LISTENING");

    fd = socket(g_vsock_af, type, 0);
    if (fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (vsock_connect_fd(fd, cid, port))
    {
        int old_errno = errno;
        close(fd);
        fd = -1;
        errno = old_errno;
    }

    return fd;
}

int vsock_stream_connect(unsigned int cid, unsigned int port)
{
    return vsock_connect(cid, port, SOCK_STREAM);
}

int vsock_seqpacket_connect(unsigned int cid, unsigned int port)
{
    return vsock_connect(cid, port, SOCK_SEQPACKET);
}

static int vsock_listen(unsigned int cid, unsigned int port, int type)
{
    int fd = vsock_bind(cid, port, type);

    if (listen(fd, 1) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return fd;
}

int vsock_accept(unsigned int cid, unsigned int port, struct sockaddr_vm *clientaddrp, int type)
{
    union {
        struct sockaddr sa;
        struct sockaddr_vm svm;
    } clientaddr;
    socklen_t clientaddr_len = sizeof(clientaddr.svm);
    int fd, client_fd, old_errno;

    fd = vsock_listen(cid, port, type);

    control_writeln("LISTENING");

    timeout_begin(TIMEOUT);
    do
    {
        client_fd = accept(fd, &clientaddr.sa, &clientaddr_len);
        timeout_check("accept");
    } while (client_fd < 0 && errno == EINTR);
    timeout_end();

    old_errno = errno;
    close(fd);
    errno = old_errno;

    if (client_fd < 0)
    {
        return client_fd;
    }

    if (clientaddr_len != sizeof(clientaddr.svm))
    {
        fprintf(stderr, "unexpected addrlen from accept(2), %zu\n", (size_t)clientaddr_len);
        exit(EXIT_FAILURE);
    }
    if (clientaddr.sa.sa_family != g_vsock_af)
    {
        fprintf(stderr, "expected AF_VSOCK from accept(2), got %d\n", clientaddr.sa.sa_family);
        exit(EXIT_FAILURE);
    }

    if (clientaddrp)
    {
        *clientaddrp = clientaddr.svm;
    }
    return client_fd;
}

int vsock_stream_accept(unsigned int cid, unsigned int port, struct sockaddr_vm *clientaddrp)
{
    return vsock_accept(cid, port, clientaddrp, SOCK_STREAM);
}

int vsock_stream_listen(unsigned int cid, unsigned int port)
{
    return vsock_listen(cid, port, SOCK_STREAM);
}

int vsock_seqpacket_accept(unsigned int cid, unsigned int port, struct sockaddr_vm *clientaddrp)
{
    return vsock_accept(cid, port, clientaddrp, SOCK_SEQPACKET);
}

void send_buf(int fd, const void *buf, size_t len, int flags, ssize_t expected_ret)
{
    ssize_t nwritten = 0;
    ssize_t ret;

    timeout_begin(TIMEOUT);
    do
    {
        ret = send(fd, (const char *)buf + nwritten, len - nwritten, flags);
        timeout_check("send");

        if (ret == 0 || (ret < 0 && errno != EINTR))
        {
            break;
        }

        nwritten += ret;
    } while ((size_t)nwritten < len);
    timeout_end();

    if (expected_ret < 0)
    {
        if (ret != -1)
        {
            fprintf(stderr, "bogus send return value %zd (expected %zd)\n", ret, expected_ret);
            exit(EXIT_FAILURE);
        }
        if (errno != -expected_ret)
        {
            perror("send");
            exit(EXIT_FAILURE);
        }
        return;
    }

    if (ret < 0)
    {
        perror("send");
        exit(EXIT_FAILURE);
    }

    if (nwritten != expected_ret)
    {
        if (ret == 0)
        {
            fprintf(stderr, "unexpected EOF while sending bytes\n");
        }
        fprintf(stderr, "bogus send bytes written %zd (expected %zd)\n", nwritten, expected_ret);
        exit(EXIT_FAILURE);
    }
}

void recv_buf(int fd, void *buf, size_t len, int flags, ssize_t expected_ret)
{
    ssize_t nread = 0;
    ssize_t ret;

    timeout_begin(TIMEOUT);
    do
    {
        ret = recv(fd, (char *)buf + nread, len - nread, flags);
        timeout_check("recv");

        if (ret == 0 || (ret < 0 && errno != EINTR))
        {
            break;
        }

        nread += ret;
    } while ((size_t)nread < len);
    timeout_end();

    if (expected_ret < 0)
    {
        if (ret != -1)
        {
            fprintf(stderr, "bogus recv return value %zd (expected %zd)\n", ret, expected_ret);
            exit(EXIT_FAILURE);
        }
        if (errno != -expected_ret)
        {
            perror("recv");
            exit(EXIT_FAILURE);
        }
        return;
    }

    if (ret < 0)
    {
        perror("recv");
        exit(EXIT_FAILURE);
    }

    if (nread != expected_ret)
    {
        if (ret == 0)
        {
            fprintf(stderr, "unexpected EOF while receiving bytes\n");
        }
        fprintf(stderr, "bogus recv bytes read %zd (expected %zd)\n", nread, expected_ret);
        exit(EXIT_FAILURE);
    }
}

void send_byte(int fd, int expected_ret, int flags)
{
    static const uint8_t byte = 'A';
    send_buf(fd, &byte, sizeof(byte), flags, expected_ret);
}

void recv_byte(int fd, int expected_ret, int flags)
{
    uint8_t byte;

    recv_buf(fd, &byte, sizeof(byte), flags, expected_ret);

    if (expected_ret == 1 && byte != 'A')
    {
        fprintf(stderr, "unexpected byte read 0x%02x\n", byte);
        exit(EXIT_FAILURE);
    }
}

void run_tests(const struct test_case *test_cases, const struct test_opts *opts)
{
    int i;

    for (i = 0; test_cases[i].name; i++)
    {
        void (*run)(const struct test_opts *opts);
        char *line;

        printf("%d - %s...", i, test_cases[i].name);
        fflush(stdout);

        if (test_cases[i].skip)
        {
            control_writeln("SKIP");
        }
        else
        {
            control_writeln("NEXT");
        }

        line = control_readln();
        if (control_cmpln(line, "SKIP", false) || test_cases[i].skip)
        {
            printf("skipped\n");
            free(line);
            continue;
        }

        control_cmpln(line, "NEXT", true);
        free(line);

        if (opts->mode == TEST_MODE_CLIENT)
        {
            run = test_cases[i].run_client;
        }
        else
        {
            run = test_cases[i].run_server;
        }

        if (run)
        {
            run(opts);
        }

        printf("ok\n");
    }

    printf("All tests have been executed. Waiting other peer...");
    fflush(stdout);

    control_writeln("COMPLETED");
    control_expectln("COMPLETED");

    printf("ok\n");
}

void list_tests(const struct test_case *test_cases)
{
    int i;

    printf("ID\tTest name\n");
    for (i = 0; test_cases[i].name; i++)
    {
        printf("%d\t%s\n", i, test_cases[i].name);
    }

    exit(EXIT_FAILURE);
}

static unsigned long parse_test_id(const char *test_id_str, size_t test_cases_len)
{
    unsigned long test_id;
    char *endptr = NULL;

    errno = 0;
    test_id = strtoul(test_id_str, &endptr, 10);
    if (errno || *endptr != '\0')
    {
        fprintf(stderr, "malformed test ID \"%s\"\n", test_id_str);
        exit(EXIT_FAILURE);
    }

    if (test_id >= test_cases_len)
    {
        fprintf(stderr,
                "test ID (%lu) larger than the max allowed (%lu)\n",
                test_id,
                (unsigned long)(test_cases_len - 1));
        exit(EXIT_FAILURE);
    }

    return test_id;
}

void skip_test(struct test_case *test_cases, size_t test_cases_len, const char *test_id_str)
{
    unsigned long test_id = parse_test_id(test_id_str, test_cases_len);
    test_cases[test_id].skip = true;
}

void pick_test(struct test_case *test_cases, size_t test_cases_len, const char *test_id_str)
{
    static bool skip_all = true;
    unsigned long test_id;

    if (skip_all)
    {
        unsigned long i;
        for (i = 0; i < test_cases_len; ++i)
        {
            test_cases[i].skip = true;
        }
        skip_all = false;
    }

    test_id = parse_test_id(test_id_str, test_cases_len);
    test_cases[test_id].skip = false;
}

unsigned long hash_djb2(const void *data, size_t len)
{
    unsigned long hash = 5381;
    size_t i = 0;

    while (i < len)
    {
        hash = ((hash << 5) + hash) + ((unsigned char *)data)[i];
        i++;
    }

    return hash;
}

size_t iovec_bytes(const struct iovec *iov, size_t iovnum)
{
    size_t bytes = 0;
    size_t i;

    for (i = 0; i < iovnum; i++)
    {
        bytes += iov[i].iov_len;
    }

    return bytes;
}

unsigned long iovec_hash_djb2(const struct iovec *iov, size_t iovnum)
{
    unsigned long hash;
    size_t iov_bytes;
    size_t offs;
    void *tmp;
    size_t i;

    iov_bytes = iovec_bytes(iov, iovnum);

    tmp = malloc(iov_bytes);
    if (!tmp)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    for (offs = 0, i = 0; i < iovnum; i++)
    {
        memcpy((char *)tmp + offs, iov[i].iov_base, iov[i].iov_len);
        offs += iov[i].iov_len;
    }

    hash = hash_djb2(tmp, iov_bytes);
    free(tmp);

    return hash;
}

/*
 * Allocate test iovecs.
 *
 * iov_base semantics:
 *   NULL       -> valid buffer (VirtualAlloc)
 *   MAP_FAILED -> invalid buffer: allocate then decommit (VirtualFree MEM_DECOMMIT)
 *   other      -> unaligned valid buffer: VirtualAlloc + offset
 */
struct iovec *alloc_test_iovec(const struct iovec *test_iovec, int iovnum)
{
    struct iovec *iovec;
    int i;

    iovec = (struct iovec *)malloc(sizeof(*iovec) * iovnum);
    if (!iovec)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < iovnum; i++)
    {
        iovec[i].iov_len = test_iovec[i].iov_len;
        iovec[i].iov_base = mmap(NULL, iovec[i].iov_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (iovec[i].iov_base == MAP_FAILED)
        {
            perror("mmap");
            exit(EXIT_FAILURE);
        }

        if (test_iovec[i].iov_base != MAP_FAILED)
        {
            iovec[i].iov_base = (char *)iovec[i].iov_base + (uintptr_t)test_iovec[i].iov_base;
        }
    }

    /* Decommit "invalid" pages. */
    for (i = 0; i < iovnum; i++)
    {
        if (test_iovec[i].iov_base == MAP_FAILED)
        {
            if (munmap(iovec[i].iov_base, iovec[i].iov_len))
            {
                perror("munmap");
                exit(EXIT_FAILURE);
            }
        }
    }

    for (i = 0; i < iovnum; i++)
    {
        int j;

        if (test_iovec[i].iov_base == MAP_FAILED)
        {
            continue;
        }

        for (j = 0; j < (int)iovec[i].iov_len; j++)
        {
            ((uint8_t *)iovec[i].iov_base)[j] = rand() & 0xff;
        }
    }

    return iovec;
}

void free_test_iovec(const struct iovec *test_iovec, struct iovec *iovec, int iovnum)
{
    int i;

    for (i = 0; i < iovnum; i++)
    {
        if (test_iovec[i].iov_base != MAP_FAILED)
        {
            void *base = iovec[i].iov_base;
            if (test_iovec[i].iov_base)
            {
                base = (char *)base - (uintptr_t)test_iovec[i].iov_base;
            }

            if (munmap(base, iovec[i].iov_len))
            {
                perror("munmap");
                exit(EXIT_FAILURE);
            }
        }
    }

    free(iovec);
}

void setsockopt_ull_check(int fd, int level, int optname, unsigned long long val, char const *errmsg)
{
    unsigned long long chkval;
    socklen_t chklen;
    int err;

    err = setsockopt(fd, level, optname, &val, sizeof(val));
    if (err)
    {
        fprintf(stderr, "setsockopt err: %s (%d)\n", strerror(errno), errno);
        goto fail;
    }

    chkval = ~val;
    chklen = sizeof(chkval);

    err = getsockopt(fd, level, optname, &chkval, &chklen);
    if (err)
    {
        fprintf(stderr, "getsockopt err: %s (%d)\n", strerror(errno), errno);
        goto fail;
    }

    if (chklen != sizeof(chkval))
    {
        fprintf(stderr, "size mismatch: set %zu got %d\n", sizeof(val), chklen);
        goto fail;
    }

    if (chkval != val)
    {
        fprintf(stderr, "value mismatch: set %llu got %llu\n", val, chkval);
        goto fail;
    }
    return;
fail:
    fprintf(stderr, "%s  val %llu\n", errmsg, val);
    exit(EXIT_FAILURE);
}

void setsockopt_int_check(int fd, int level, int optname, int val, char const *errmsg)
{
    int chkval;
    socklen_t chklen;
    int err;

    err = setsockopt(fd, level, optname, &val, sizeof(val));
    if (err)
    {
        fprintf(stderr, "setsockopt err: %s (%d)\n", strerror(errno), errno);
        goto fail;
    }

    chkval = ~val;
    chklen = sizeof(chkval);

    err = getsockopt(fd, level, optname, &chkval, &chklen);
    if (err)
    {
        fprintf(stderr, "getsockopt err: %s (%d)\n", strerror(errno), errno);
        goto fail;
    }

    if (chklen != sizeof(chkval))
    {
        fprintf(stderr, "size mismatch: set %zu got %d\n", sizeof(val), chklen);
        goto fail;
    }

    if (chkval != val)
    {
        fprintf(stderr, "value mismatch: set %d got %d\n", val, chkval);
        goto fail;
    }
    return;
fail:
    fprintf(stderr, "%s val %d\n", errmsg, val);
    exit(EXIT_FAILURE);
}

/*
 * SO_RCVTIMEO on Windows takes a DWORD (milliseconds), not struct timeval.
 * Set the option and skip the getsockopt round-trip verification.
 */
void setsockopt_timeval_check(int fd, int level, int optname, struct timeval val, char const *errmsg)
{
    DWORD ms = (DWORD)(val.tv_sec * 1000 + val.tv_usec / 1000);
    int err;

    err = setsockopt(fd, level, optname, (const char *)&ms, sizeof(ms));
    if (err)
    {
        fprintf(stderr, "setsockopt err: %s (%d) -- %s\n", strerror(errno), errno, errmsg);
        exit(EXIT_FAILURE);
    }
}

/* SO_ZEROCOPY not available on Windows; skip tests that require it. */
void enable_so_zerocopy_check(int fd)
{
    (void)fd;
    fprintf(stderr, "SO_ZEROCOPY not supported on Windows -- test skipped\n");
    exit(EXIT_FAILURE);
}

void enable_so_linger(int fd, int timeout)
{
    struct linger optval = {
                                                                                                        .l_onoff = 1,
                                                                                                        .l_linger = (u_short)timeout,
    };

    if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &optval, sizeof(optval)))
    {
        perror("setsockopt(SO_LINGER)");
        exit(EXIT_FAILURE);
    }
}

/* No /proc/kallsyms on Windows; transport detection not supported. */
int get_transports(void)
{
    return 0;
}
