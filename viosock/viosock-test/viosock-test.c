// viosock-test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "socket.h"

ULONG64
GetGuestCid()
{
    ULONG64 uRes = VMADDR_CID_ANY;
    SOCKET hViosock = VIOSockCreateSocket(NULL);
    VIRTIO_VSOCK_CONFIG vsConfig;

    _tprintf(L"--> %s\n", TEXT(__FUNCTION__));

    if (hViosock == INVALID_SOCKET)
    {
        _tprintf(L"VIOSockCreateSocket error: %d\n", GetLastError());
        return uRes;
    }

    if (VIOSockGetConfig(hViosock, &vsConfig))
        uRes = vsConfig.guest_cid;
    else
        _tprintf(L"VIOSockGetConfig error: %d\n", GetLastError());

    VIOSockCloseSocket(hViosock);

    _tprintf(L"<-- %s\n", TEXT(__FUNCTION__));
    return uRes;
}

ULONG64
GetGuestCidFromNewSocket()
{
    ULONG64 uRes = VMADDR_CID_ANY;
    SOCKET hViosock;
    VIRTIO_VSOCK_CONFIG vsConfig;
    VIRTIO_VSOCK_PARAMS SocketParams = { 0 };

    _tprintf(L"--> %s\n", TEXT(__FUNCTION__));

    hViosock = VIOSockCreateSocket(&SocketParams);
    if (hViosock == INVALID_SOCKET)
    {
        _tprintf(L"VIOSockCreateSocket(new) error: %d\n", GetLastError());
        return uRes;
    }

    if (VIOSockGetConfig(hViosock, &vsConfig))
    {
        uRes = vsConfig.guest_cid;
    }
    else
        _tprintf(L"VIOSockGetConfig error: %d\n", GetLastError());

    VIOSockCloseSocket(hViosock);

    _tprintf(L"<-- %s\n", TEXT(__FUNCTION__));
    return uRes;
}

ULONG64
GetGuestCidFromAcceptSocket()
{
    ULONG64 uRes = VMADDR_CID_ANY;
    SOCKET hListenSock, hAcceptSocket;
    VIRTIO_VSOCK_CONFIG vsConfig;
    VIRTIO_VSOCK_PARAMS SocketParams = { 0 };

    _tprintf(L"--> %s\n", TEXT(__FUNCTION__));

    hListenSock = VIOSockCreateSocket(&SocketParams);
    if (hListenSock == INVALID_SOCKET)
    {
        _tprintf(L"VIOSockCreateSocket(new) error: %d\n", GetLastError());
        return uRes;
    }

    SocketParams.Socket = hListenSock;
    hAcceptSocket = VIOSockCreateSocket(&SocketParams);
    if (hAcceptSocket == INVALID_SOCKET)
    {
        _tprintf(L"VIOSockCreateSocket(accept) error: %d\n", GetLastError());
        VIOSockCloseSocket(hListenSock);
        return uRes;
    }

    if (VIOSockGetConfig(hAcceptSocket, &vsConfig))
    {
        uRes = vsConfig.guest_cid;
    }
    else
        _tprintf(L"VIOSockGetConfig error: %d\n", GetLastError());

    VIOSockCloseSocket(hAcceptSocket);
    VIOSockCloseSocket(hListenSock);

    _tprintf(L"<-- %s\n", TEXT(__FUNCTION__));
    return uRes;
}

#define TEST_PORT 2222

int __cdecl main()
{
    ULONG64 uGuestCid = GetGuestCid();

    if (uGuestCid != VMADDR_CID_ANY)
    {
        _tprintf(L"GetGuestCid cid: %d\n", (DWORD)uGuestCid);
    }

    uGuestCid = GetGuestCidFromNewSocket();
    if (uGuestCid != VMADDR_CID_ANY)
    {
        _tprintf(L"GetGuestCidFromNewSocket cid: %d\n", (DWORD)uGuestCid);
    }

    uGuestCid = GetGuestCidFromAcceptSocket();
    if (uGuestCid != VMADDR_CID_ANY)
    {
        _tprintf(L"GetGuestCidFromAcceptSocket cid: %d\n", (DWORD)uGuestCid);
    }

    return 0;
}
