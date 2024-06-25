/*
 * Copyright (C) 2014-2017 Red Hat, Inc.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define STRICT
#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <tchar.h>

#include <stdio.h>

#include "viorngum.h"
#include <bcrypt_provider.h>

DEFINE_GUID(GUID_DEVINTERFACE_VIRT_RNG,
    0x2489fc19, 0xd0fd, 0x4950, 0x83, 0x86, 0xf3, 0xda, 0x3f, 0xa8, 0x5, 0x8);

BCRYPT_RNG_FUNCTION_TABLE RngFunctionTable =
{
    // BCRYPT_RNG_INTERFACE_VERSION_1
    1, 0,

    // RNG Interface
    VirtRngOpenAlgorithmProvider,
    VirtRngGetProperty,
    VirtRngSetProperty,
    VirtRngCloseAlgorithmProvider,
    VirtRngGenRandom
};

static NTSTATUS ReadRngFromDevice(IN HANDLE Device,
                                  IN LPOVERLAPPED Overlapped,
                                  IN OUT PUCHAR Buffer,
                                  IN ULONG Length,
                                  OUT LPDWORD BytesRead)
{
    NTSTATUS status;

    if (ReadFile(Device, Buffer, Length, BytesRead, Overlapped) == TRUE)
    {
        status = STATUS_SUCCESS;
    }
    else if (GetLastError() != ERROR_IO_PENDING)
    {
        status = STATUS_UNSUCCESSFUL;
    }
    else if (GetOverlappedResult(Device, Overlapped, BytesRead, TRUE) == TRUE)
    {
        status = STATUS_SUCCESS;
    }
    else
    {
        status = STATUS_UNSUCCESSFUL;
    }

    return status;
}

NTSTATUS WINAPI GetRngInterface(IN LPCWSTR pszProviderName,
                                OUT BCRYPT_RNG_FUNCTION_TABLE **ppFunctionTable,
                                IN ULONG dwFlags)
{
    UNREFERENCED_PARAMETER(pszProviderName);
    UNREFERENCED_PARAMETER(dwFlags);

    if (ppFunctionTable == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *ppFunctionTable = &RngFunctionTable;

    return STATUS_SUCCESS;
}

HANDLE OpenVirtRngDeviceInterface()
{
    HDEVINFO devInfo;
    HANDLE devIface = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA devIfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA devIfaceDetail = NULL;
    ULONG Length, RequiredLength = 0;
    DWORD Index = 0;
    BOOL bResult;

    devInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_VIRT_RNG, NULL, NULL,
        (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

    if (devInfo == INVALID_HANDLE_VALUE)
    {
        return NULL;
    }

    devIfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    while (SetupDiEnumDeviceInterfaces(devInfo, NULL,
                &GUID_DEVINTERFACE_VIRT_RNG, Index, &devIfaceData) == TRUE)
    {
        SetupDiGetDeviceInterfaceDetail(devInfo, &devIfaceData, NULL, 0,
            &RequiredLength, NULL);

        devIfaceDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)
            LocalAlloc(LMEM_FIXED, RequiredLength);

        if (devIfaceDetail == NULL)
        {
            break;
        }

        devIfaceDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        Length = RequiredLength;

        bResult = SetupDiGetDeviceInterfaceDetail(devInfo, &devIfaceData,
            devIfaceDetail, Length, &RequiredLength, NULL);

        if (bResult == FALSE)
        {
            LocalFree(devIfaceDetail);
            break;
        }

        devIface = CreateFile(devIfaceDetail->DevicePath, GENERIC_READ,
            FILE_SHARE_READ, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED, NULL);

        LocalFree(devIfaceDetail);

        if (devIface != INVALID_HANDLE_VALUE)
        {
            break;
        }

        Index += 1;
    }

    SetupDiDestroyDeviceInfoList(devInfo);

    return devIface;
}

NTSTATUS WINAPI VirtRngOpenAlgorithmProvider(OUT BCRYPT_ALG_HANDLE *Algorithm,
                                             IN LPCWSTR AlgId,
                                             IN ULONG Flags)
{
    NTSTATUS status = STATUS_SUCCESS;
    HANDLE devIface;

    if (Algorithm == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (lstrcmp(AlgId, BCRYPT_RNG_ALGORITHM) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Flags != 0L)
    {
        return STATUS_NOT_SUPPORTED;
    }

    devIface = OpenVirtRngDeviceInterface();
    if (devIface == INVALID_HANDLE_VALUE)
    {
        status = STATUS_PORT_UNREACHABLE;
    }

    *Algorithm = (BCRYPT_ALG_HANDLE)devIface;

    return status;
}

NTSTATUS WINAPI VirtRngGetProperty(IN BCRYPT_HANDLE Object,
                                   IN LPCWSTR Property,
                                   OUT PUCHAR Output,
                                   IN ULONG Length,
                                   OUT ULONG *Result,
                                   IN ULONG Flags)
{
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(Output);
    UNREFERENCED_PARAMETER(Property);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Result);
    UNREFERENCED_PARAMETER(Flags);

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS WINAPI VirtRngSetProperty(IN OUT BCRYPT_HANDLE Object,
                                   IN LPCWSTR Property,
                                   IN PUCHAR Input,
                                   IN ULONG Length,
                                   IN ULONG Flags)
{
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(Property);
    UNREFERENCED_PARAMETER(Input);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Flags);

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS WINAPI VirtRngCloseAlgorithmProvider(IN OUT BCRYPT_ALG_HANDLE Algorithm,
                                              IN ULONG Flags)
{
    NTSTATUS status = STATUS_SUCCESS;
    HANDLE devIface = (HANDLE)Algorithm;
    BOOL bResult;

    UNREFERENCED_PARAMETER(Flags);

    if ((devIface == INVALID_HANDLE_VALUE) || (devIface == NULL))
    {
        return STATUS_INVALID_HANDLE;
    }

    bResult = CloseHandle(devIface);
    if (bResult == FALSE)
    {
        status = STATUS_INVALID_HANDLE;
    }

    return status;
}

NTSTATUS WINAPI VirtRngGenRandom(IN OUT BCRYPT_ALG_HANDLE Algorithm,
                                 IN OUT PUCHAR Buffer,
                                 IN ULONG Length,
                                 IN ULONG Flags)
{
    HANDLE devIface = (HANDLE)Algorithm;
    NTSTATUS status = STATUS_SUCCESS;
    OVERLAPPED ovrlpd;
    DWORD totalBytes;
    DWORD bytesRead;

    if ((devIface == INVALID_HANDLE_VALUE) || (devIface == NULL))
    {
        return STATUS_INVALID_HANDLE;
    }

    if ((Buffer == NULL) || (Length == 0) || (Flags != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    ZeroMemory(&ovrlpd, sizeof(ovrlpd));
    totalBytes = 0;

    while (totalBytes < Length)
    {
        status = ReadRngFromDevice(devIface, &ovrlpd, Buffer + totalBytes,
            Length - totalBytes, &bytesRead);

        if (!NT_SUCCESS(status))
        {
            break;
        }

        totalBytes += bytesRead;
    }

    return status;
}
