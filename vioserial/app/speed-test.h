#pragma once
/*
typical qemu command-line
-chardev socket,path=/tmp/foo,id=foo -device
virtserialport,bus=virtio-serial0.0,nr=2,chardev=foo,id=test0,name=test0
*/
#include <errno.h>
#include <fcntl.h>
#include <linux/un.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif
  int speed_test (int client);
#ifdef __cplusplus
}
#endif
