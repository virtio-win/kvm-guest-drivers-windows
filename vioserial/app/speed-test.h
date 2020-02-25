#pragma once
/*
typical qemu command-line
-chardev socket,path=/tmp/foo,id=foo -device virtserialport,bus=virtio-serial0.0,nr=2,chardev=foo,id=test0,name=test0
*/
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <linux/un.h>

#ifdef __cplusplus
extern "C" {
#endif
    int speed_test(int client);
#ifdef __cplusplus
}
#endif
