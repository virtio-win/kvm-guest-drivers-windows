/*
 * User-mode counterpart of the viosock WSK test driver
 * (viosock/tests/viosock-wsk-test).
 *
 * Speaks the same message format: 64-byte messages consisting of 32
 * random bytes followed by their SHA-256 digest. The client sends a
 * message, the server verifies it and replies with a message of its
 * own, a fixed number of rounds per connection.
 *
 * Builds for both Windows (Winsock2 + viosock provider) and Linux
 * (native AF_VSOCK). Peer CID and port are command-line parameters.
 *
 * Copyright (c) 2026 Virtuozzo International GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include "vio_sockets.h"
#pragma comment(lib, "ws2_32.lib")

typedef SOCKET sock_t;
typedef int sock_len_t;
#define SOCK_INVALID(s) ((s) == INVALID_SOCKET)
#define sock_close      closesocket

static ADDRESS_FAMILY vsock_af;

static void sock_perror(const char *msg)
{
    fprintf(stderr, "%s: WSA error %d\n", msg, WSAGetLastError());
}

#else /* _WIN32 */

#include <sys/socket.h>
#include <linux/vm_sockets.h>
#include <unistd.h>
#include <errno.h>

typedef int sock_t;
typedef socklen_t sock_len_t;
#define SOCK_INVALID(s) ((s) < 0)
#define sock_close      close

static int vsock_af = AF_VSOCK;

static void sock_perror(const char *msg)
{
    perror(msg);
}

#endif /* _WIN32 */

#define MSG_SIZE       64
#define MSG_DATA_SIZE  32
#define DEFAULT_ROUNDS 16 /* TEST_COUNT of the kernel test */

/* ------------------------------------------------------------------ */
/* SHA-256 (FIPS 180-2), self-contained                                 */
/* ------------------------------------------------------------------ */

typedef struct sha256_ctx
{
    uint32_t h[8];
    uint64_t total;
    uint8_t buf[64];
    size_t buflen;
} sha256_ctx;

/* clang-format off */
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};
/* clang-format on */

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_compress(sha256_ctx *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; ++i)
    {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }

    for (int i = 16; i < 64; ++i)
    {
        uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->h[0];
    b = ctx->h[1];
    c = ctx->h[2];
    d = ctx->h[3];
    e = ctx->h[4];
    f = ctx->h[5];
    g = ctx->h[6];
    h = ctx->h[7];

    for (int i = 0; i < 64; ++i)
    {
        uint32_t s1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + sha256_k[i] + w[i];
        uint32_t s0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->h[0] += a;
    ctx->h[1] += b;
    ctx->h[2] += c;
    ctx->h[3] += d;
    ctx->h[4] += e;
    ctx->h[5] += f;
    ctx->h[6] += g;
    ctx->h[7] += h;
}

static void sha256_init(sha256_ctx *ctx)
{
    static const uint32_t h0[8] = {0x6a09e667,
                                   0xbb67ae85,
                                   0x3c6ef372,
                                   0xa54ff53a,
                                   0x510e527f,
                                   0x9b05688c,
                                   0x1f83d9ab,
                                   0x5be0cd19};

    memcpy(ctx->h, h0, sizeof(h0));
    ctx->total = 0;
    ctx->buflen = 0;
}

static void sha256_update(sha256_ctx *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    ctx->total += len;
    while (len > 0)
    {
        size_t take = sizeof(ctx->buf) - ctx->buflen;

        if (take > len)
        {
            take = len;
        }

        memcpy(ctx->buf + ctx->buflen, p, take);
        ctx->buflen += take;
        p += take;
        len -= take;
        if (ctx->buflen == sizeof(ctx->buf))
        {
            sha256_compress(ctx, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t digest[32])
{
    uint64_t bits = ctx->total * 8;
    static const uint8_t pad = 0x80;
    uint8_t lenbuf[8];

    for (int i = 0; i < 8; ++i)
    {
        lenbuf[i] = (uint8_t)(bits >> (56 - i * 8));
    }

    sha256_update(ctx, &pad, 1);
    while (ctx->buflen != 56)
    {
        static const uint8_t zero = 0;

        sha256_update(ctx, &zero, 1);
    }

    sha256_update(ctx, lenbuf, sizeof(lenbuf));
    for (int i = 0; i < 8; ++i)
    {
        digest[i * 4] = (uint8_t)(ctx->h[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->h[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->h[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)ctx->h[i];
    }
}

static void sha256(const void *data, size_t len, uint8_t digest[32])
{
    sha256_ctx ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

static int sha256_selftest(void)
{
    static const uint8_t expected[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                         0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                         0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    uint8_t digest[32];

    sha256("abc", 3, digest);
    return memcmp(digest, expected, sizeof(digest)) == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Test messages (wire-compatible with the kernel WSK test)            */
/* ------------------------------------------------------------------ */

static void msg_generate(uint8_t msg[MSG_SIZE])
{
    for (int i = 0; i < MSG_DATA_SIZE; ++i)
    {
        msg[i] = (uint8_t)(rand() & 0xFF);
    }

    sha256(msg, MSG_DATA_SIZE, msg + MSG_DATA_SIZE);
}

static int msg_verify(const uint8_t msg[MSG_SIZE])
{
    uint8_t digest[32];

    sha256(msg, MSG_DATA_SIZE, digest);
    return memcmp(digest, msg + MSG_DATA_SIZE, sizeof(digest)) == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* I/O helpers                                                          */
/* ------------------------------------------------------------------ */

static int send_all(sock_t s, const uint8_t *buf, size_t len)
{
    while (len > 0)
    {
        int n = send(s, (const char *)buf, (int)len, 0);

        if (n <= 0)
        {
            sock_perror("send");
            return -1;
        }

        buf += n;
        len -= (size_t)n;
    }

    return 0;
}

/* 1 = full message read, 0 = clean EOF before any byte, -1 = error */
static int recv_all(sock_t s, uint8_t *buf, size_t len)
{
    size_t got = 0;

    while (got < len)
    {
        int n = recv(s, (char *)buf + got, (int)(len - got), 0);

        if (n == 0)
        {
            if (got == 0)
            {
                return 0;
            }

            fprintf(stderr, "recv: unexpected EOF inside a message (%zu/%zu bytes)\n", got, len);
            return -1;
        }

        if (n < 0)
        {
            sock_perror("recv");
            return -1;
        }

        got += (size_t)n;
    }

    return 1;
}

/* ------------------------------------------------------------------ */
/* Roles                                                                */
/* ------------------------------------------------------------------ */

static int run_server(unsigned int port, unsigned long max_connections)
{
    struct sockaddr_vm addr;
    unsigned long served = 0;
    sock_t ls;

    memset(&addr, 0, sizeof(addr));
    addr.svm_family = vsock_af;
    addr.svm_port = port;
    addr.svm_cid = VMADDR_CID_ANY;

    ls = socket(vsock_af, SOCK_STREAM, 0);
    if (SOCK_INVALID(ls))
    {
        sock_perror("socket");
        return 1;
    }

    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        sock_perror("bind");
        sock_close(ls);
        return 1;
    }

    if (listen(ls, 128) != 0)
    {
        sock_perror("listen");
        sock_close(ls);
        return 1;
    }

    printf("[server] listening on port %u\n", port);
    fflush(stdout);

    while (max_connections == 0 || served < max_connections)
    {
        struct sockaddr_vm peer;
        sock_len_t peerlen = sizeof(peer);
        unsigned long rounds = 0;
        int bad = 0;
        sock_t cs;

        cs = accept(ls, (struct sockaddr *)&peer, &peerlen);
        if (SOCK_INVALID(cs))
        {
            sock_perror("accept");
            sock_close(ls);
            return 1;
        }

        printf("[server] connection from %u:%u\n", (unsigned int)peer.svm_cid, (unsigned int)peer.svm_port);
        fflush(stdout);

        for (;;)
        {
            uint8_t in[MSG_SIZE];
            uint8_t out[MSG_SIZE];
            int r = recv_all(cs, in, sizeof(in));

            if (r == 0)
            {
                break; /* client closed the connection */
            }

            if (r < 0 || msg_verify(in) != 0)
            {
                if (r > 0)
                {
                    fprintf(stderr, "[server] message %lu: hash mismatch\n", rounds + 1);
                }

                bad = 1;
                break;
            }

            msg_generate(out);
            if (send_all(cs, out, sizeof(out)) != 0)
            {
                bad = 1;
                break;
            }

            ++rounds;
        }

        sock_close(cs);
        ++served;
        printf("[server] connection %lu finished: %lu messages, %s\n", served, rounds, bad ? "FAILED" : "ok");
        fflush(stdout);
        if (bad)
        {
            sock_close(ls);
            return 1;
        }
    }

    sock_close(ls);
    return 0;
}

static int run_client(unsigned int cid, unsigned int port, unsigned long connections, unsigned long rounds)
{
    for (unsigned long c = 1; c <= connections; ++c)
    {
        struct sockaddr_vm addr;
        sock_t s;

        memset(&addr, 0, sizeof(addr));
        addr.svm_family = vsock_af;
        addr.svm_port = port;
        addr.svm_cid = cid;

        s = socket(vsock_af, SOCK_STREAM, 0);
        if (SOCK_INVALID(s))
        {
            sock_perror("socket");
            return 1;
        }

        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        {
            sock_perror("connect");
            sock_close(s);
            return 1;
        }

        for (unsigned long r = 1; r <= rounds; ++r)
        {
            uint8_t out[MSG_SIZE];
            uint8_t in[MSG_SIZE];

            msg_generate(out);
            if (send_all(s, out, sizeof(out)) != 0)
            {
                sock_close(s);
                return 1;
            }

            if (recv_all(s, in, sizeof(in)) != 1)
            {
                sock_close(s);
                return 1;
            }

            if (msg_verify(in) != 0)
            {
                fprintf(stderr, "[client] connection %lu round %lu: hash mismatch\n", c, r);
                sock_close(s);
                return 1;
            }
        }

        sock_close(s);
        printf("[client] connection %lu: %lu rounds ok\n", c, rounds);
        fflush(stdout);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s --mode server|client [options]\n"
            "  -m, --mode  server|client  role (required)\n"
            "  -p, --port  <port>         vsock port (default 1337)\n"
            "  -c, --cid   <cid>          peer CID (client only, required)\n"
            "  -n, --count <n>            client: connections to make (default 1)\n"
            "                             server: connections to serve, 0 = forever (default 0)\n"
            "  -r, --rounds <n>           message rounds per connection, client side (default %u)\n",
            argv0,
            DEFAULT_ROUNDS);
}

int main(int argc, char *argv[])
{
    const char *mode = NULL;
    unsigned int port = 1337;
    unsigned int cid = 0;
    int cid_set = 0;
    unsigned long count = 0;
    int count_set = 0;
    unsigned long rounds = DEFAULT_ROUNDS;
    int ret;

    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;

        if ((strcmp(arg, "-m") == 0 || strcmp(arg, "--mode") == 0) && val)
        {
            mode = val;
            ++i;
        }
        else if ((strcmp(arg, "-p") == 0 || strcmp(arg, "--port") == 0) && val)
        {
            port = (unsigned int)strtoul(val, NULL, 0);
            ++i;
        }
        else if ((strcmp(arg, "-c") == 0 || strcmp(arg, "--cid") == 0) && val)
        {
            cid = (unsigned int)strtoul(val, NULL, 0);
            cid_set = 1;
            ++i;
        }
        else if ((strcmp(arg, "-n") == 0 || strcmp(arg, "--count") == 0) && val)
        {
            count = strtoul(val, NULL, 0);
            count_set = 1;
            ++i;
        }
        else if ((strcmp(arg, "-r") == 0 || strcmp(arg, "--rounds") == 0) && val)
        {
            rounds = strtoul(val, NULL, 0);
            ++i;
        }
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    if (!mode || (strcmp(mode, "server") != 0 && strcmp(mode, "client") != 0))
    {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(mode, "client") == 0 && !cid_set)
    {
        fprintf(stderr, "client mode requires --cid\n");
        usage(argv[0]);
        return 1;
    }

    if (sha256_selftest() != 0)
    {
        fprintf(stderr, "SHA-256 self-test failed\n");
        return 1;
    }

    srand((unsigned int)time(NULL));

#ifdef _WIN32
    {
        WSADATA wsa_data;

        if (WSAStartup(MAKEWORD(2, 2), &wsa_data))
        {
            fprintf(stderr, "WSAStartup failed\n");
            return 1;
        }

        vsock_af = ViosockGetAF();
        if (vsock_af == AF_UNSPEC)
        {
            fprintf(stderr, "ViosockGetAF() failed -- is the viosock driver loaded?\n");
            WSACleanup();
            return 1;
        }
    }
#endif

    if (strcmp(mode, "server") == 0)
    {
        ret = run_server(port, count);
    }
    else
    {
        ret = run_client(cid, port, count_set ? count : 1, rounds);
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return ret;
}
