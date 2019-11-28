// viosock-test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "..\sys\public.h"

BOOL
GetConfig(
    HANDLE hViosock,
    PVIRTIO_VSOCK_CONFIG pConfig
)
{
    DWORD dwBytes;
    return DeviceIoControl(hViosock, IOCTL_GET_CONFIG, NULL, 0, pConfig, sizeof(*pConfig), &dwBytes, NULL);
}

#define TEST_PORT 2222

VIRTIO_VSOCK_CONFIG g_Config;

int __cdecl main()
{
    HANDLE hViosock =
        CreateFile(VIOSOCK_NAME, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hViosock == INVALID_HANDLE_VALUE)
    {
        _tprintf(L"Can't open %s, error: %d\n", VIOSOCK_NAME, GetLastError());
        return 1;
    }

    if (GetConfig(hViosock, &g_Config))
        _tprintf(L"Guest cid: %d\n", (DWORD)g_Config.guest_cid);
    else
        _tprintf(L"Can't get config, error: %d\n", GetLastError());

    CloseHandle(hViosock);
    return 0;
}
