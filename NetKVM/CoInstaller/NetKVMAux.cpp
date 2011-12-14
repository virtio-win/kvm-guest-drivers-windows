#include "stdafx.h"
#include "NetKVMAux.h"

typedef LONG NTSTATUS;

#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

wstring NetKVMGetKeyPathFromKKEY(HKEY hKey)
{
    std::wstring wstrKeyPath;
    if (hKey != NULL)
    {
        HMODULE hNTDll = LoadLibrary(TEXT("ntdll.dll"));
        if (hNTDll != NULL) {
                typedef DWORD (__stdcall *ZwQueryKeyType)(HANDLE  KeyHandle,
                                                          int KeyInformationClass,
                                                          PVOID  KeyInformation,
                                                          ULONG  Length,
                                                          PULONG  ResultLength);

                ZwQueryKeyType pZwQueryKey = reinterpret_cast<ZwQueryKeyType>(::GetProcAddress(hNTDll, "ZwQueryKey"));

                if (pZwQueryKey != NULL) {
                        DWORD dwSize = 0;
                        DWORD dwResult = 0;
                        dwResult = pZwQueryKey(hKey, 3, 0, 0, &dwSize);
                        if (dwResult == STATUS_BUFFER_TOO_SMALL)
                        {
                                dwSize += sizeof(wchar_t);
                                wchar_t* szNameBuf = new wchar_t[dwSize];
                                dwResult = pZwQueryKey(hKey, 3, szNameBuf, dwSize, &dwSize);
                                if (dwResult == STATUS_SUCCESS)
                                {
                                        szNameBuf[dwSize / sizeof(wchar_t)] = L'\0';
                                        wstrKeyPath = wstring(szNameBuf + 2);
                                }

                                delete[] szNameBuf;
                        }
                }

                FreeLibrary(hNTDll);
        }
    }
    return wstrKeyPath;
}

