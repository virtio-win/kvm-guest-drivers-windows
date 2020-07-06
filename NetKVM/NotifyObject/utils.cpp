/*
 * Copyright (c) 2020 Red Hat, Inc.
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

#include "stdafx.h"
#include "netkvmnoif_i.c"

void Trace (LPCSTR szFormat, ...)
{
    va_list arglist;
    va_start(arglist, szFormat);
    CStringA s;
    s.FormatV(szFormat, arglist);
    s += '\n';
    OutputDebugString(s.GetBuffer());
    va_end(arglist);
}

class CSupportedAdapters : public CAtlArray<CStringW>
{
public:
    CSupportedAdapters()
    {
        HKEY hKey;
        ULONG NoDefaults = false;
        if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                           "System\\CurrentControlSet\\Control\\VioProt",
                           0, KEY_READ, &hKey)) {
            ULONG len;
            WCHAR buffer[1024] = {};
            len = sizeof(NoDefaults);
            RegGetValueW(hKey, NULL, L"NoDefaults", RRF_RT_DWORD, NULL, &NoDefaults, &len);
            len = sizeof(buffer);
            if (!RegGetValueW(hKey, NULL, L"Supported", RRF_RT_REG_MULTI_SZ, NULL, buffer, &len)) {
                for (UINT i = 0; i < len; )
                {
                    CStringW s = buffer + i;
                    if (s.IsEmpty())
                        break;
                    s.MakeLower();
                    Add(s);
                    i += s.GetLength() + 1;
                }
            }
            RegCloseKey(hKey);
        }
        if (!NoDefaults) {
            Add(L"ven_8086&dev_1515");  // Intel X540 Virtual Function
            Add(L"ven_8086&dev_10ca");  // Intel 82576 Virtual Function
            Add(L"ven_8086&dev_15a8");  // Intel Ethernet Connection X552
            Add(L"ven_8086&dev_154c");  // Intel 700 Series VF
            Add(L"ven_8086&dev_10ed");  // Intel 82599 VF
            Add(L"ven_15b3&dev_101a");  // Mellanox MT28800 Family
        }
        for (UINT i = 0; i < GetCount(); ++i)
        {
            CStringW& s = GetAt(i);
            Trace("NotifyObj: supported %S", s.GetBuffer());
        }
    }
};

static CSupportedAdapters SupportedAdapters;

bool IsSupportedSRIOVAdapter(LPCWSTR pnpId)
{
    CStringW sId = pnpId;
    sId.MakeLower();
    for (UINT i = 0; i < SupportedAdapters.GetCount(); ++i)
    {
        CStringW& s = SupportedAdapters.GetAt(i);
        if (sId.Find(s) > 0) {
            return true;
        }
    }
    return false;
}
