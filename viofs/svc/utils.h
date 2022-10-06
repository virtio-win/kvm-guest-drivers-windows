/*
 * Copyright (C) 2022 Daynix Computing Ltd.
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

#pragma once

#include <Windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include <functional>
#include <memory>
#include <string>

template <typename EF>
class scope_exit
{
    EF exit_func;

public:
    scope_exit(EF&& exit_func) : exit_func{ exit_func } {};
    ~scope_exit()
    {
        exit_func();
    }
};

#define SCOPE_EXIT(x, action, ...) scope_exit x##_se([x, __VA_ARGS__] action);

template<typename T>
static DWORD RegistryGetVal(PCWSTR SubKey, PCWSTR ValueName, T& Value)
{
    LSTATUS Status;
    DWORD Val;
    DWORD ValSize = sizeof(Val);

    Status = RegGetValueW(HKEY_LOCAL_MACHINE, SubKey, ValueName,
        RRF_RT_REG_DWORD, NULL, &Val, &ValSize);
    if (Status == ERROR_SUCCESS)
    {
        Value = Val;
    }

    return Status;
}

static DWORD RegistryGetVal(PCWSTR SubKey, PCWSTR ValueName, std::wstring& Value)
{
    LSTATUS Status;
    DWORD BufSize = 0;
    std::unique_ptr<WCHAR[]> Buf;

    // Determine required buffer size
    Status = RegGetValueW(HKEY_LOCAL_MACHINE, SubKey, ValueName,
        RRF_RT_REG_SZ, NULL, NULL, &BufSize);
    if (Status != ERROR_SUCCESS)
    {
        return Status;
    }

    try
    {
        Buf = std::make_unique<WCHAR[]>(BufSize / sizeof(WCHAR));
    }
    catch (std::bad_alloc)
    {
        return ERROR_NO_SYSTEM_RESOURCES;
    }

    Status = RegGetValueW(HKEY_LOCAL_MACHINE, SubKey, ValueName,
        RRF_RT_REG_SZ, NULL, Buf.get(), &BufSize);
    if (Status == ERROR_SUCCESS)
    {
        Value.assign(Buf.get());
    }

    return Status;
}

static DWORD FindDeviceInterface(const GUID *ClassGuid, PHANDLE Device, DWORD MemberIndex)
{
    HDEVINFO DevInfo;
    SECURITY_ATTRIBUTES SecurityAttributes;
    SP_DEVICE_INTERFACE_DATA DevIfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA DevIfaceDetail = NULL;
    ULONG Length, RequiredLength = 0;

    DevInfo = SetupDiGetClassDevs(ClassGuid, NULL, NULL,
        (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (DevInfo == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }
    SCOPE_EXIT(DevInfo, { SetupDiDestroyDeviceInfoList(DevInfo); });

    DevIfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    if (!SetupDiEnumDeviceInterfaces(DevInfo, 0,
        ClassGuid, MemberIndex, &DevIfaceData))
    {
        return GetLastError();
    }

    SetupDiGetDeviceInterfaceDetail(DevInfo, &DevIfaceData, NULL, 0,
        &RequiredLength, NULL);

    DevIfaceDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)LocalAlloc(LMEM_FIXED,
        RequiredLength);
    if (DevIfaceDetail == NULL)
    {
        return GetLastError();
    }
    SCOPE_EXIT(DevIfaceDetail, { LocalFree(DevIfaceDetail); });

    DevIfaceDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
    Length = RequiredLength;

    if (!SetupDiGetDeviceInterfaceDetail(DevInfo, &DevIfaceData,
        DevIfaceDetail, Length, &RequiredLength, NULL))
    {
        return GetLastError();
    }

    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.lpSecurityDescriptor = NULL;
    SecurityAttributes.bInheritHandle = FALSE;

    *Device = CreateFile(DevIfaceDetail->DevicePath, GENERIC_READ | GENERIC_WRITE,
        0, &SecurityAttributes, OPEN_EXISTING, 0L, NULL);
    if (*Device == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    return ERROR_SUCCESS;
}

static DWORD FindDeviceInterface(const GUID *ClassGuid, PHANDLE Device,
    std::function<BOOLEAN(HANDLE Device)> cmp_fn)
{
    for (DWORD MemberIndex = 0; ; MemberIndex++)
    {
        DWORD Error = FindDeviceInterface(ClassGuid, Device, MemberIndex);
        if (Error != ERROR_SUCCESS)
        {
            return Error;
        }

        if (cmp_fn(*Device))
        {
            return ERROR_SUCCESS;
        }
        else
        {
            CloseHandle(*Device);
        }
    }
}

static bool FileNameIgnoreCaseCompare(PCWSTR a, const char *b, uint32_t b_len)
{
    WCHAR wide_b[MAX_PATH];

    int wide_b_len = MultiByteToWideChar(CP_UTF8, 0, b, b_len, wide_b, MAX_PATH);
    if (wide_b_len == 0)
    {
        return false;
    }

    return (CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, a, -1, wide_b, wide_b_len) == CSTR_EQUAL);
}

class DeviceInterfaceNotification
{
    HCMNOTIFICATION     Handle{ nullptr };

    DeviceInterfaceNotification(const DeviceInterfaceNotification&) = delete;
    DeviceInterfaceNotification& operator=(const DeviceInterfaceNotification&) = delete;
    DeviceInterfaceNotification(DeviceInterfaceNotification&&) = delete;
    DeviceInterfaceNotification& operator=(DeviceInterfaceNotification&&) = delete;

public:
    DeviceInterfaceNotification() = default;

    DWORD Register(PCM_NOTIFY_CALLBACK pCallback, PVOID pContext, const GUID &ClassGuid)
    {
        HCMNOTIFICATION NotifyHandle = nullptr;
        CM_NOTIFY_FILTER Filter;
        CONFIGRET ConfigRet;

        ZeroMemory(&Filter, sizeof(Filter));
        Filter.cbSize = sizeof(Filter);
        Filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        Filter.u.DeviceInterface.ClassGuid = ClassGuid;

        ConfigRet = CM_Register_Notification(&Filter, pContext, pCallback, &NotifyHandle);

        if (ConfigRet == CR_SUCCESS)
        {
            Handle = NotifyHandle;
        }

        return CM_MapCrToWin32Err(ConfigRet, ERROR_NOT_SUPPORTED);
    }

    void Unregister()
    {
        CM_Unregister_Notification(Handle);
    }
};

class DeviceHandleNotification
{
    HCMNOTIFICATION     Handle{ nullptr };
    CRITICAL_SECTION    Lock;
    BOOL                UnregInProgress{ FALSE };
    PTP_WORK            UnregWork{ nullptr };

    DeviceHandleNotification(const DeviceHandleNotification&) = delete;
    DeviceHandleNotification& operator=(const DeviceHandleNotification&) = delete;
    DeviceHandleNotification(DeviceHandleNotification&&) = delete;
    DeviceHandleNotification& operator=(DeviceHandleNotification&&) = delete;

public:
    DeviceHandleNotification()
    {
        InitializeCriticalSection(&Lock);
    }

    ~DeviceHandleNotification()
    {
        if (UnregWork != nullptr)
        {
            CloseThreadpoolWork(UnregWork);
        }

        DeleteCriticalSection(&Lock);
    }

    DWORD Register(PCM_NOTIFY_CALLBACK pCallback, PVOID pContext, HANDLE DeviceHandle)
    {
        HCMNOTIFICATION NotifyHandle = nullptr;
        CM_NOTIFY_FILTER Filter;
        CONFIGRET ConfigRet;

        ZeroMemory(&Filter, sizeof(Filter));
        Filter.cbSize = sizeof(Filter);
        Filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
        Filter.u.DeviceHandle.hTarget = DeviceHandle;

        ConfigRet = CM_Register_Notification(&Filter, pContext, pCallback, &NotifyHandle);

        if (ConfigRet == CR_SUCCESS)
        {
            Handle = NotifyHandle;
            UnregInProgress = FALSE;
        }

        return CM_MapCrToWin32Err(ConfigRet, ERROR_NOT_SUPPORTED);
    }

    void Unregister()
    {
        BOOL ShouldUnregister = FALSE;

        EnterCriticalSection(&Lock);

        if (!UnregInProgress)
        {
            UnregInProgress = TRUE;
            ShouldUnregister = TRUE;
        }

        LeaveCriticalSection(&Lock);

        if (ShouldUnregister)
        {
            CM_Unregister_Notification(Handle);
        }
        else
        {
            WaitForThreadpoolWorkCallbacks(UnregWork, FALSE);
        }
    }

    // Must be used instead of Unregister() when unregistering
    // device notification callback from itself.
    void AsyncUnregister()
    {
        EnterCriticalSection(&Lock);

        if (!UnregInProgress)
        {
            UnregInProgress = TRUE;
            SubmitThreadpoolWork(UnregWork);
        }

        LeaveCriticalSection(&Lock);
    }

    bool CreateUnregWork()
    {
        auto cb = [](PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
        {
            auto Notification = static_cast<DeviceHandleNotification *>(Context);

            UNREFERENCED_PARAMETER(Instance);
            UNREFERENCED_PARAMETER(Work);

            CM_Unregister_Notification(Notification->Handle);
        };
        UnregWork = CreateThreadpoolWork(cb, this, nullptr);

        return (UnregWork != nullptr);
    }

    void WaitForUnregWork()
    {
        WaitForThreadpoolWorkCallbacks(UnregWork, FALSE);
    }
};