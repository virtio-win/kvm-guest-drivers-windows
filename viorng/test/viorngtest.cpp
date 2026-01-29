// viorngtest.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#pragma comment(lib, "bcrypt")

static BOOL GetParameters(int argc, LPWSTR *argv, ULONG &bufSize, LPCWSTR &substring)
{
    bufSize = 0;
    WCHAR *end;
    if (argc < 2)
    {
        return false;
    }
    bufSize = wcstol(argv[1], &end, 10);
    if (bufSize == 0)
    {
        return false;
    }
    if (argc < 3)
    {
        return true;
    }
    if (_wcsnicmp(argv[2], L"-name:", 6))
    {
        return false;
    }
    substring = argv[2] + 6;
    return true;
}

static BOOL GetMyProvider(LPCWSTR substring, BCRYPT_ALG_HANDLE &h)
{
    h = NULL;
    ULONG nCount = 0;
    BCRYPT_PROVIDER_NAME *names;
    if (*substring == 0)
    {
        h = NULL;
        printf("Using default provider\n");
        return true;
    }
    BCryptEnumProviders(L"RNG", &nCount, &names, 0);
    for (ULONG i = 0; i < nCount; ++i)
    {
        wprintf(L"%d: %s\n", i, names[i].pszProviderName);
        if (wcsstr(names[i].pszProviderName, substring))
        {
            NTSTATUS status = BCryptOpenAlgorithmProvider(&h, L"RNG", names[i].pszProviderName, 0);
            if (NT_SUCCESS(status))
            {
                printf("Using it\n");
                return true;
            }
            else
            {
                printf("BCryptOpenAlgorithmProvider error: 0x%08x\n", status);
            }
        }
    }
    return false;
}

int __cdecl wmain(int argc, LPWSTR argv[])
{
    NTSTATUS status;
    BCRYPT_ALG_HANDLE h = NULL;
    ULONG bufSize;
    LPCWSTR substring = L"QEMU";
    if (!GetParameters(argc, argv, bufSize, substring))
    {
        puts("viorngtest <buf_size> [-name:<provider>]");
        puts("buf_size - length of random buffer");
        puts("provider - optional substring to select RNG provider.");
        puts("           omit for QEMU or use '-name:' for default provider");
        return 1;
    }

    if (!GetMyProvider(substring, h))
    {
        printf("Provider containing %S not found!\n", substring);
        return ERROR_FILE_NOT_FOUND;
    }

    BYTE *Buffer = new BYTE[bufSize];
    memset(Buffer, 0, bufSize);

    status = BCryptGenRandom(h, Buffer, bufSize, h ? 0 : BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    if (!NT_SUCCESS(status))
    {
        printf("BCryptGenRandom error: 0x%08x\n", status);
    }
    else
    {
        puts("Done OK");
    }

    if (h)
    {
        BCryptCloseAlgorithmProvider(h, 0);
    }

    delete[] Buffer;
    return status;
}
