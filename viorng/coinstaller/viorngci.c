/*
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Refer to the LICENSE file for full details of the license.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 */

#define STRICT
#include <windows.h>
#include <bcrypt.h>
#include <bcrypt_provider.h>
#include <setupapi.h>
#include <tchar.h>

#include <stdio.h>

#include "viorngci.h"

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
