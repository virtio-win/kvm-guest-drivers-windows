// SPDX-License-Identifier: GPL-2.0-only
/*
 * Control socket for client/server test synchronization.
 * Ported to Windows: standard Winsock2 via compat.h.
 * MSG_MORE removed (not available on Windows; TCP Nagle coalesces small
 * writes anyway for the short control strings used here).
 */

#include "compat.h"
#include "control.h"
#include "util.h"

static int control_fd = -1;

void control_init(const char *control_host, const char *control_port, bool server)
{
    struct addrinfo hints = {.ai_socktype = SOCK_STREAM};
    struct addrinfo *result = NULL;
    struct addrinfo *ai;
    int ret;

    ret = getaddrinfo(control_host, control_port, &hints, &result);
    if (ret != 0)
    {
        fprintf(stderr, "%s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }

    for (ai = result; ai; ai = ai->ai_next)
    {
        int fd;

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
        {
            continue;
        }

        if (!server)
        {
            if (connect(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen) < 0)
            {
                goto next;
            }
            control_fd = fd;
            printf("Control socket connected to %s:%s.\n", control_host, control_port);
            break;
        }

        setsockopt_int_check(fd, SOL_SOCKET, SO_REUSEADDR, 1, "setsockopt SO_REUSEADDR");

        if (bind(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen) < 0)
        {
            goto next;
        }
        if (listen(fd, 1) < 0)
        {
            goto next;
        }

        printf("Control socket listening on %s:%s\n", control_host, control_port);
        fflush(stdout);

        control_fd = accept(fd, NULL, NULL);
        close(fd);

        if (control_fd < 0)
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        printf("Control socket connection accepted...\n");
        break;

    next:
        close(fd);
    }

    if (control_fd < 0)
    {
        fprintf(stderr, "Control socket initialization failed. Invalid address %s:%s?\n", control_host, control_port);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(result);
}

void control_cleanup(void)
{
    close(control_fd);
    control_fd = -1;
}

void control_writeln(const char *str)
{
    ssize_t len = (ssize_t)strlen(str);
    ssize_t ret;

    timeout_begin(TIMEOUT);

    /* Send body then newline as two separate sends (no MSG_MORE on Windows). */
    do
    {
        ret = send(control_fd, str, (size_t)len, 0);
        timeout_check("send");
    } while (ret < 0 && errno == EINTR);

    if (ret != len)
    {
        perror("send");
        exit(EXIT_FAILURE);
    }

    do
    {
        ret = send(control_fd, "\n", 1, 0);
        timeout_check("send");
    } while (ret < 0 && errno == EINTR);

    if (ret != 1)
    {
        perror("send");
        exit(EXIT_FAILURE);
    }

    timeout_end();
}

void control_writeulong(unsigned long value)
{
    char str[32];

    if (snprintf(str, sizeof(str), "%lu", value) >= (int)sizeof(str))
    {
        perror("snprintf");
        exit(EXIT_FAILURE);
    }

    control_writeln(str);
}

unsigned long control_readulong(void)
{
    unsigned long value;
    char *str;

    str = control_readln();
    if (!str)
    {
        exit(EXIT_FAILURE);
    }

    value = strtoul(str, NULL, 10);
    free(str);

    return value;
}

char *control_readln(void)
{
    char *buf = NULL;
    size_t idx = 0;
    size_t buflen = 0;

    timeout_begin(TIMEOUT);

    for (;;)
    {
        ssize_t ret;

        if (idx >= buflen)
        {
            char *new_buf = (char *)realloc(buf, buflen + 80);
            if (!new_buf)
            {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            buf = new_buf;
            buflen += 80;
        }

        do
        {
            ret = recv(control_fd, &buf[idx], 1, 0);
            timeout_check("recv");
        } while (ret < 0 && errno == EINTR);

        if (ret == 0)
        {
            fprintf(stderr, "unexpected EOF on control socket\n");
            exit(EXIT_FAILURE);
        }

        if (ret != 1)
        {
            perror("recv");
            exit(EXIT_FAILURE);
        }

        if (buf[idx] == '\n')
        {
            buf[idx] = '\0';
            break;
        }

        idx++;
    }

    timeout_end();

    return buf;
}

void control_expectln(const char *str)
{
    char *line = control_readln();
    control_cmpln(line, str, true);
    free(line);
}

bool control_cmpln(char *line, const char *str, bool fail)
{
    if (strcmp(str, line) == 0)
    {
        return true;
    }

    if (fail)
    {
        fprintf(stderr, "expected \"%s\" on control socket, got \"%s\"\n", str, line);
        exit(EXIT_FAILURE);
    }

    return false;
}
