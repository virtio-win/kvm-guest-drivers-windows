/*
 * Copyright (C) 2021-2022 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
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

#include "pch.h"

void GpuAdaptersMgr::FindAdapters()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    std::wstring id = TEXT("PCI\\VEN_1AF4&DEV_1050");
    DISPLAY_DEVICE adapter = {sizeof(DISPLAY_DEVICE)};
    DWORD adapterIndex = 0;
    while (FindDisplayDevice(&adapter, id, &adapterIndex))
    {
        if (adapter.StateFlags & DISPLAY_DEVICE_ACTIVE)
        {
            AddAdapter(adapter.DeviceName);
        }
    }
}

void GpuAdaptersMgr::SetCustomResolution(USHORT Width, USHORT Height)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    for (Iterator it = Adapters.begin(); it != Adapters.end(); it++)
    {
        (*it)->SetCustomResolution(Width, Height);
    }
}

BOOL GpuAdaptersMgr::GetDisplayDevice(LPCTSTR lpDevice, DWORD iDevNum, PDISPLAY_DEVICE lpDisplayDevice, DWORD dwFlags)
{
    PrintMessage(L"%ws iDevNum = %d\n", __FUNCTIONW__, iDevNum);

    return ::EnumDisplayDevices(lpDevice, iDevNum, lpDisplayDevice, dwFlags);
};

BOOL GpuAdaptersMgr::FindDisplayDevice(PDISPLAY_DEVICE lpDisplayDevice, std::wstring &name, PDWORD adapterIndex)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    DWORD index = *adapterIndex;
    (*adapterIndex)++;

    if (GetDisplayDevice(NULL, index, lpDisplayDevice, 0))
    {
        if (!name.empty() && !_wcsnicmp(lpDisplayDevice->DeviceID, name.c_str(), name.size()))
        {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL GpuAdaptersMgr::Init()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
    FindAdapters();

    return TRUE;
}

void GpuAdaptersMgr::Close()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    RemoveAllAdapters();
}

void GpuAdaptersMgr::AddAdapter(const wchar_t *name)
{
    PrintMessage(L"Add Adapter %ws\n", name);

    for (Iterator it = Adapters.begin(); it != Adapters.end(); it++)
    {
        if (_wcsnicmp((*it)->m_DeviceName.c_str(), name, (*it)->m_DeviceName.size()) == 0)
        {
            (*it)->SetStatus(Active);
            return;
        }
    }

    Adapters.push_back(new GpuAdapter(name));
}

void GpuAdaptersMgr::RemoveAllAdapters()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    for (Iterator it = Adapters.begin(); it != Adapters.end(); it++)
    {
        delete *it;
    }

    Adapters.clear();
}
