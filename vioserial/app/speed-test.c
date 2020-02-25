#include "speed-test.h"

#define MAGIC 0xaabbccff

typedef struct
{
    int magic;
    int size;
} req_header;

static int write_splitted(int sockfd, char *buf, int size)
{
    int chunk = 0x4000;
    int done = 0;
    int res;
    while (size)
    {
        if (size < chunk)
            chunk = size;
        res = write(sockfd, buf, chunk);
        if (res <= 0)
        {
            return res;
        }
        done += res;
        buf += res;
        size -= res;
    }
    return done;
}

static uint64_t time_ms()
{
    struct timespec ts;
    uint64_t res;
    if (clock_gettime(CLOCK_MONOTONIC, &ts))
        return 0;
    res = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return res;
}

static void do_client_job(int sockfd)
{
    req_header r;
    int size = 1024 * 1024;
    int max_size = 64 * size;
    void *p = malloc(max_size);
    for (; size <= max_size; size *= 2)
    {
        uint64_t t1 = time_ms(), t2;
        r.magic = MAGIC;
        r.size = size;
        if (write(sockfd, &r, sizeof(r)) < 0)
        {
            printf("Can't write %d, error %d\n", (int)sizeof(r), errno);
            return;
        }
        if (write_splitted(sockfd, p, size) < 0)
        {
            printf("Can't write %d, error %d\n", (int)size, errno);
            return;
        }
        r.magic = r.size = 0;
        if (read(sockfd, &r, sizeof(r)) < 0)
        {
            printf("Can't read header, error %d\n", errno);
            return;
        }
        if (r.magic != MAGIC || r.size != size)
        {
            printf("Wrong header received for size %d\n", (int)size);
            return;
        }
        t2 = time_ms();
        printf("%d transferred in %d ms\n", size, (int)(t2 - t1));
        sleep(3);
    }
    r.size = 0;
    if (write(sockfd, &r, sizeof(r)) < 0)
    {
        printf("Can't write %d, error %d\n", (int)sizeof(r), errno);
        return;
    }
    free(p);
}

static int do_server_job(int sockfd)
{
    int max_size = 128 * 1024 * 1024;
    void *p = malloc(max_size);
    req_header r;
    int res;
    int done;
    do
    {
        char *buf = p;
        if (read(sockfd, &r, sizeof(r)) < 0)
        {
            printf("Can't read header, error %d\n", errno);
            return 1;
        }
        if (r.magic != MAGIC)
        {
            printf("Wrong header received\n");
            return 1;
        }
        if (r.size == 0)
            break;
        if (r.size > max_size)
        {
            printf("too large block\n");
            return 1;
        }
        done = 0;
        do
        {
            res = read(sockfd, buf, r.size - done);
            if (res < 0)
            {
                printf("Can't read data, error %d\n", errno);
                return 1;
            }
            if (res == 0)
            {
                printf("Disconnected, error %d\n", errno);
                return 1;
            }
            done += res;
            buf += res;
        } while (done < r.size);

        if (write(sockfd, &r, sizeof(r)) < 0)
        {
            printf("Can't write %d, error %d\n", (int)sizeof(r), errno);
            return 1;
        }
    } while (1);

    free(p);
    return 0;
}

#ifndef WIN32
#define DEVNAME "/dev/virtio-ports/test0"
#else
#define DEVNAME "\\\\.\\test0"
#endif

int speed_test(int client)
{
    int sockfd = 0, ret;
    int type = SOCK_STREAM;
    char *devname = DEVNAME;
    struct  sockaddr_un unix_addr = { AF_UNIX, "/tmp/foo" };

    if (!client)
        sockfd = socket(AF_UNIX, type, 0);
    else
        sockfd = open(devname, O_RDWR);

    if (sockfd <= 0)
    {
        printf("error : Could not create socket, %s\n", strerror(errno));
        return 1;
    }

    if (!client)
    {
        unlink(unix_addr.sun_path);
        ret = bind(sockfd, (struct sockaddr*)&unix_addr, sizeof(unix_addr));
        if (ret < 0)
        {
            printf("error %d on bind\n", errno);
            return 1;
        }
        
        ret = listen(sockfd, 10);
        if (ret < 0)
        {
            printf("error on listen: %d\n", errno);
            return 1;
        }

        int listenfd = sockfd;
        sockfd = accept(listenfd, NULL, NULL);
        if (sockfd < 0)
        {
            printf("Failed on accept, error %d\n", errno);
            return 1;
        }
        close(listenfd);
    }

    if (client)
    {
        do_client_job(sockfd);
    }
    else
    {
        while (!do_server_job(sockfd));
    }

    close(sockfd);

    return 0;
}
