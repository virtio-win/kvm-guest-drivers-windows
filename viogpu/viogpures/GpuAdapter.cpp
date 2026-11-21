/*
 * Copyright (C) 2021-2022 Red Hat, Inc.
 * Copyright (C) 2026 Zededa, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
 *             Mikhail Malyshev <mike.malyshev@gmail.com>
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

GpuAdapter::GpuAdapter(const std::wstring LinkName)
    : m_hDC(NULL), m_hAdapter(NULL), m_Id(0), m_PathIdx(INVALID_PATH_IDX), m_PathArrayElements(0),
      m_ModeInfoArrayElements(0), m_pDisplayPathInfo(NULL), m_pDisplayModeInfo(NULL), m_Flag(None)
{
    m_DeviceName = LinkName;
    PrintMessage(L"%ws %ws\n", __FUNCTIONW__, m_DeviceName.c_str());
    Init();
};

// Takes a snapshot of the active paths and finds this display's path in it. ERROR_SUCCESS with m_PathIdx at
// INVALID_PATH_IDX when the display has no active path; another error, logged, when the snapshot failed.
LONG GpuAdapter::UpdateDisplayConfig(void)
{
    PrintMessage(L"%ws %ws\n", __FUNCTIONW__, m_DeviceName.c_str());

    UINT32 filter = QDC_ONLY_ACTIVE_PATHS;
    LONG result;
    // A topology change between the two calls makes QueryDisplayConfig ask for larger buffers.
    do
    {
        ClearDisplayConfig();
        m_PathIdx = INVALID_PATH_IDX;
        result = ::GetDisplayConfigBufferSizes(filter, &m_PathArrayElements, &m_ModeInfoArrayElements);
        if (result != ERROR_SUCCESS)
        {
            break;
        }
        m_pDisplayPathInfo = new DISPLAYCONFIG_PATH_INFO[m_PathArrayElements];
        m_pDisplayModeInfo = new DISPLAYCONFIG_MODE_INFO[m_ModeInfoArrayElements];
        ZeroMemory(m_pDisplayPathInfo, sizeof(DISPLAYCONFIG_PATH_INFO) * m_PathArrayElements);
        ZeroMemory(m_pDisplayModeInfo, sizeof(DISPLAYCONFIG_MODE_INFO) * m_ModeInfoArrayElements);
        result = ::QueryDisplayConfig(filter,
                                      &m_PathArrayElements,
                                      m_pDisplayPathInfo,
                                      &m_ModeInfoArrayElements,
                                      m_pDisplayModeInfo,
                                      NULL);
    } while (result == ERROR_INSUFFICIENT_BUFFER);
    if (result != ERROR_SUCCESS)
    {
        PrintMessage(L"%ws %ws: querying the display configuration failed with error %d\n",
                     __FUNCTIONW__,
                     m_DeviceName.c_str(),
                     result);
        ClearDisplayConfig();
        return result;
    }

    for (UINT PathIdx = 0; PathIdx < GetNumbersOfPathArrayElements(); ++PathIdx)
    {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME SourceName = {};
        SourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        SourceName.header.size = sizeof(SourceName);
        SourceName.header.adapterId = m_pDisplayPathInfo[PathIdx].sourceInfo.adapterId;
        SourceName.header.id = m_pDisplayPathInfo[PathIdx].sourceInfo.id;

        LONG nameResult = ::DisplayConfigGetDeviceInfo(&SourceName.header);
        if (nameResult != ERROR_SUCCESS)
        {
            PrintMessage(L"%ws path %d: DisplayConfigGetDeviceInfo failed with error %d\n",
                         __FUNCTIONW__,
                         PathIdx,
                         nameResult);
        }
        else if (wcscmp(m_DeviceName.c_str(), SourceName.viewGdiDeviceName) == 0)
        {
            m_PathIdx = PathIdx;
            break;
        }
    }
    return ERROR_SUCCESS;
}

void GpuAdapter::ClearDisplayConfig(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    delete[] m_pDisplayPathInfo;
    delete[] m_pDisplayModeInfo;
    m_pDisplayPathInfo = NULL;
    m_pDisplayModeInfo = NULL;
    m_PathArrayElements = 0;
    m_ModeInfoArrayElements = 0;
}

void GpuAdapter::Init()
{
    PrintMessage(L"%ws %ws\n", __FUNCTIONW__, m_DeviceName.c_str());

    m_hDC = ::CreateDC(NULL, m_DeviceName.c_str(), NULL, NULL);

    D3DKMT_OPENADAPTERFROMHDC openAdapter = {0};
    openAdapter.hDc = m_hDC;

    NTSTATUS status = D3DKMTOpenAdapterFromHdc(&openAdapter);
    if (!NT_SUCCESS(status))
    {
        PrintMessage(L"Cannot open adapter from hdc for %ws. Status = 0x%x.\n", m_DeviceName.c_str(), status);
        return;
    }

    m_hAdapter = openAdapter.hAdapter;
    if (!QueryAdapterId())
    {
        PrintMessage(L"Cannot query adapter id for %ws. Error = %d.\n", m_DeviceName.c_str(), GetLastError());
        return;
    }
    SetStatus(Active);
}

bool GpuAdapter::QueryAdapterId()
{
    PrintMessage(L"%ws %ws\n", __FUNCTIONW__, m_DeviceName.c_str());

    VIOGPU_ESCAPE data{0};
    data.DataLength = sizeof(ULONG);
    data.Type = VIOGPU_GET_DEVICE_ID;

    D3DKMT_ESCAPE escape = {0};
    escape.hAdapter = m_hAdapter;
    escape.pPrivateDriverData = &data;
    escape.PrivateDriverDataSize = sizeof(data);

    NTSTATUS status = D3DKMTEscape(&escape);
    if (!NT_SUCCESS(status))
    {
        PrintMessage(L"D3DKMTEscape failed with status = 0x%x\n", status);
        return false;
    }

    m_Id = data.Id;
    PrintMessage(L"%ws %ws is adapter %u\n", __FUNCTIONW__, m_DeviceName.c_str(), m_Id);
    return true;
}

bool GpuAdapter::GetCustomResolution(PVIOGPU_DISP_MODE pmode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hAdapter && pmode)
    {
        VIOGPU_ESCAPE data{0};
        data.DataLength = sizeof(VIOGPU_DISP_MODE);
        data.Type = VIOGPU_GET_CUSTOM_RESOLUTION;

        D3DKMT_ESCAPE escape = {0};
        escape.hAdapter = m_hAdapter;
        escape.pPrivateDriverData = &data;
        escape.PrivateDriverDataSize = sizeof(data);

        NTSTATUS status = D3DKMTEscape(&escape);
        if (NT_SUCCESS(status))
        {
            pmode->XResolution = data.Resolution.XResolution;
            pmode->YResolution = data.Resolution.YResolution;
            PrintMessage(L"%ws (%dx%d)\n", __FUNCTIONW__, pmode->XResolution, pmode->YResolution);
            return true;
        }
        PrintMessage(L"D3DKMTEscape failed with status = 0x%0X\n", status);
    }
    return false;
}

bool GpuAdapter::SetCustomResolution(USHORT Width, USHORT Height)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    VIOGPU_ESCAPE data{0};
    data.DataLength = sizeof(VIOGPU_DISP_MODE);
    data.Type = VIOGPU_SET_CUSTOM_RESOLUTION;
    data.Resolution.XResolution = Width;
    data.Resolution.YResolution = Height;

    D3DKMT_ESCAPE escape = {0};
    escape.hAdapter = m_hAdapter;
    escape.pPrivateDriverData = &data;
    escape.PrivateDriverDataSize = sizeof(data);

    NTSTATUS status = D3DKMTEscape(&escape);
    if (!NT_SUCCESS(status))
    {
        PrintMessage(L"D3DKMTEscape failed with status = 0x%0X\n", status);
        return false;
    }

    VIOGPU_DISP_MODE custom = {0};

    /*
     * We need to ask driver about resolution that was applied to make
     * force Windows use the supported resolution
     */
    if (!GetCustomResolution(&custom))
    {
        return false;
    }

    if (UpdateDisplayConfig() != ERROR_SUCCESS)
    {
        return false;
    }
    if (m_PathIdx == INVALID_PATH_IDX)
    {
        // No active path: the display is not part of the desktop. The driver keeps the size for when it is.
        PrintMessage(L"%ws %ws has no active path\n", __FUNCTIONW__, m_DeviceName.c_str());
        return true;
    }
    return SetResolution(&custom);
}

bool GpuAdapter::SetResolution(PVIOGPU_DISP_MODE mode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    // Change only this display's mode, and save it (CDS_UPDATEREGISTRY). SetDisplayConfig with a QueryDisplayConfig
    // snapshot would also apply the snapshot's topology, and switch off a monitor that Windows attached after the
    // snapshot was taken.
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = mode->XResolution;
    dm.dmPelsHeight = mode->YResolution;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    LONG result = ChangeDisplaySettingsExW(m_DeviceName.c_str(), &dm, NULL, CDS_UPDATEREGISTRY, NULL);
    PrintMessage(L"%ws %ws (%dx%d) ChangeDisplaySettingsEx = %d\n",
                 __FUNCTIONW__,
                 m_DeviceName.c_str(),
                 mode->XResolution,
                 mode->YResolution,
                 result);
    return result == DISP_CHANGE_SUCCESSFUL;
}

void GpuAdapter::Close()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hAdapter)
    {
        D3DKMT_CLOSEADAPTER close = {m_hAdapter};
        NTSTATUS status = D3DKMTCloseAdapter(&close);
        if (!NT_SUCCESS(status))
        {
            PrintMessage(L"D3DKMTCloseAdapter failed with status = 0x%x.\n", status);
        }
        m_hAdapter = NULL;
    }

    if (m_hDC != NULL)
    {
        ReleaseDC(NULL, m_hDC);
        m_hDC = NULL;
    }
}
