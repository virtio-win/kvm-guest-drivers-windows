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
#include <bcrypt.h>
#include <setupapi.h>
#include <tchar.h>

#include <stdio.h>

#include "viorngci.h"

#include <bcrypt_provider.h>

#define VIRTRNG_IMAGE_NAME L"viorngum.dll"

PWSTR VirtRngAlgorithmNames[1] = {
    BCRYPT_RNG_ALGORITHM
};

CRYPT_INTERFACE_REG VirtRngInterface = {
    BCRYPT_RNG_INTERFACE, CRYPT_LOCAL, 1, VirtRngAlgorithmNames
};

PCRYPT_INTERFACE_REG VirtRngInterfaces[1] = {
    &VirtRngInterface
};

CRYPT_IMAGE_REG VirtRngImage = {
    VIRTRNG_IMAGE_NAME, 1, VirtRngInterfaces
};

CRYPT_PROVIDER_REG VirtRngProvider = {
    0, NULL, &VirtRngImage, NULL
};

typedef ULONG (WINAPI* RtlNtStatusToDosErrorFunc)(IN NTSTATUS status);

static DWORD ToDosError(NTSTATUS status)
{
    DWORD error = NO_ERROR;
    HMODULE ntdll;
    RtlNtStatusToDosErrorFunc RtlNtStatusToDosError;

    ntdll = LoadLibrary(L"Ntdll.dll");
    if (ntdll != NULL)
    {
        RtlNtStatusToDosError = (RtlNtStatusToDosErrorFunc)
            GetProcAddress(ntdll, "RtlNtStatusToDosError");

        if (RtlNtStatusToDosError != NULL)
        {
            error = RtlNtStatusToDosError(status);
        }
        else
        {
            error = GetLastError();
            SetupWriteTextLogError(SetupGetThreadLogToken(),
                TXTLOG_INSTALLER,
                TXTLOG_ERROR,
                error,
                "RtlNtStatusToDosError function not found.");
        }
    }
    else
    {
        error = GetLastError();
        SetupWriteTextLogError(SetupGetThreadLogToken(),
            TXTLOG_INSTALLER,
            TXTLOG_ERROR,
            error,
            "Failed to load ntdll.dll.");
    }

    return error;
}

NTSTATUS WINAPI RegisterProvider(BOOLEAN KernelMode)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(KernelMode);

    status = BCryptRegisterProvider(VIRTRNG_PROVIDER_NAME, CRYPT_OVERWRITE,
        &VirtRngProvider);

    if (!NT_SUCCESS(status))
    {
        SetupWriteTextLogError(SetupGetThreadLogToken(),
            TXTLOG_INSTALLER,
            TXTLOG_ERROR,
            ToDosError(status),
            "Failed to register as a CNG provider.");
        return status;
    }

    status = BCryptAddContextFunctionProvider(CRYPT_LOCAL, NULL,
        BCRYPT_RNG_INTERFACE, BCRYPT_RNG_ALGORITHM, VIRTRNG_PROVIDER_NAME,
        CRYPT_PRIORITY_BOTTOM);

    if (!NT_SUCCESS(status))
    {
        SetupWriteTextLogError(SetupGetThreadLogToken(),
            TXTLOG_INSTALLER,
            TXTLOG_ERROR,
            ToDosError(status),
            "Failed to add cryptographic function.");
    }

    return status;
}

NTSTATUS WINAPI UnregisterProvider()
{
    NTSTATUS status;

    status = BCryptRemoveContextFunctionProvider(CRYPT_LOCAL, NULL,
        BCRYPT_RNG_INTERFACE, BCRYPT_RNG_ALGORITHM, VIRTRNG_PROVIDER_NAME);

    if (!NT_SUCCESS(status))
    {
        SetupWriteTextLogError(SetupGetThreadLogToken(),
            TXTLOG_INSTALLER,
            TXTLOG_WARNING,
            ToDosError(status),
            "Failed to remove cryptographic function.");
    }

    status = BCryptUnregisterProvider(VIRTRNG_PROVIDER_NAME);
    if (!NT_SUCCESS(status))
    {
        SetupWriteTextLogError(SetupGetThreadLogToken(),
            TXTLOG_INSTALLER,
            TXTLOG_WARNING,
            ToDosError(status),
            "Failed to unregister as a CNG provider.");
    }

    return STATUS_SUCCESS;
}

DWORD CALLBACK VirtRngCoInstaller(IN DI_FUNCTION InstallFunction,
                                  IN HDEVINFO DeviceInfoSet,
                                  IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL,
                                  IN OUT PCOINSTALLER_CONTEXT_DATA Context)
{
    NTSTATUS status;
    DWORD error = NO_ERROR;

    UNREFERENCED_PARAMETER(DeviceInfoSet);
    UNREFERENCED_PARAMETER(DeviceInfoData);
    UNREFERENCED_PARAMETER(Context);

    switch (InstallFunction)
    {
        case DIF_INSTALLDEVICE:
            status = RegisterProvider(FALSE);
            if (!NT_SUCCESS(status))
            {
                error = ToDosError(status);
            }
            break;

        case DIF_REMOVE:
            status = UnregisterProvider();
            if (!NT_SUCCESS(status))
            {
                error = ToDosError(status);
            }
            break;

        default:
            break;
    }

    return error;
}
