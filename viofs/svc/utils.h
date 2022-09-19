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
