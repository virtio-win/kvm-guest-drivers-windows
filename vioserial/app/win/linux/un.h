#ifndef __linux_un_h__
#define __linux_un_h__

struct sockaddr_un
{
    UINT family;
    char sun_path[32];
};

#endif
