// viosocklib-test.c : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "..\inc\vio_sockets.h"

#define TEST_PORT 2222

int __cdecl main()
{
    SOCKET sock = INVALID_SOCKET;
    WSADATA wsaData = {0};
    int iRes = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (iRes != ERROR_SUCCESS)
    {
        _tprintf(L"WSAStartup failed: %d\n", iRes);
        return 1;
    }

    _tprintf(L"socket(AF_VSOCK, SOCK_STREAM, 0)\n");
    sock = socket(AF_VSOCK, SOCK_STREAM, 0);
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
    }
    else
        _tprintf(L"socket failed: %d\n", WSAGetLastError());

    WSACleanup();
    return 0;
}
