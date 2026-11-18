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

// Every adapter's thread applies a whole-system display configuration. Taking the snapshot and applying it
// under one lock keeps one thread from applying a snapshot that predates another thread's change, which
// would undo that change. A mutex rather than an SRW lock: Close terminates a thread that does not stop in
// time, and the system releases the mutex of a thread that ended while it held it, so the others go on.
static HANDLE DisplayConfigMutex = CreateMutex(NULL, FALSE, NULL);

static void LockDisplayConfig(void)
{
    if (DisplayConfigMutex != NULL)
    {
        // WAIT_ABANDONED, when the previous owner was terminated, gives the lock all the same.
        WaitForSingleObject(DisplayConfigMutex, INFINITE);
    }
}

static void UnlockDisplayConfig(void)
{
    if (DisplayConfigMutex != NULL)
    {
        ReleaseMutex(DisplayConfigMutex);
    }
}

GpuAdapter::GpuAdapter(const std::wstring LinkName)
    : m_hThread(NULL), m_hStopEvent(NULL), m_hResolutionEvent(NULL), m_hDC(NULL), m_hAdapter(NULL), m_Id(0),
      m_PathIdx(INVALID_PATH_IDX), m_PathArrayElements(0), m_ModeInfoArrayElements(0), m_pDisplayPathInfo(NULL),
      m_pDisplayModeInfo(NULL), m_Flag(None)
{
    m_DeviceName = LinkName;
    PrintMessage(L"%ws %ws\n", __FUNCTIONW__, m_DeviceName.c_str());
    Init();
};

void GpuAdapter::UpdateDisplayConfig(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    UINT32 filter = QDC_ONLY_ACTIVE_PATHS;
    ClearDisplayConfig();
    m_PathIdx = INVALID_PATH_IDX;

    if (FAILED(HRESULT_FROM_WIN32(::GetDisplayConfigBufferSizes(filter,
                                                                &m_PathArrayElements,
                                                                &m_ModeInfoArrayElements))))
    {
        PrintMessage(L"GetDisplayConfigBufferSizes faled\n");
        return;
    }

    m_pDisplayPathInfo = new DISPLAYCONFIG_PATH_INFO[m_PathArrayElements];
    m_pDisplayModeInfo = new DISPLAYCONFIG_MODE_INFO[m_ModeInfoArrayElements];
    ZeroMemory(m_pDisplayPathInfo, sizeof(DISPLAYCONFIG_PATH_INFO) * m_PathArrayElements);
    ZeroMemory(m_pDisplayModeInfo, sizeof(DISPLAYCONFIG_MODE_INFO) * m_ModeInfoArrayElements);

    if (SUCCEEDED(HRESULT_FROM_WIN32(::QueryDisplayConfig(filter,
                                                          &m_PathArrayElements,
                                                          m_pDisplayPathInfo,
                                                          &m_ModeInfoArrayElements,
                                                          m_pDisplayModeInfo,
                                                          NULL))))
    {

        for (UINT PathIdx = 0; PathIdx < GetNumbersOfPathArrayElements(); ++PathIdx)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME SourceName = {};
            SourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            SourceName.header.size = sizeof(SourceName);
            SourceName.header.adapterId = m_pDisplayPathInfo[PathIdx].sourceInfo.adapterId;
            SourceName.header.id = m_pDisplayPathInfo[PathIdx].sourceInfo.id;

            if (SUCCEEDED(HRESULT_FROM_WIN32(::DisplayConfigGetDeviceInfo(&SourceName.header))))
            {
                if (wcscmp(m_DeviceName.c_str(), SourceName.viewGdiDeviceName) == 0)
                {
                    m_PathIdx = PathIdx;
                    break;
                }
            }
        }
    }
};

DISPLAYCONFIG_MODE_INFO *GpuAdapter::GetDisplayConfig(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_PathIdx < GetNumbersOfPathArrayElements())
    {
        UINT idx = m_pDisplayPathInfo[m_PathIdx].sourceInfo.modeInfoIdx;
        PrintMessage(L"%ws path %d mode %d\n", __FUNCTIONW__, m_PathIdx, idx);
        if (idx < GetNumbersOfModeInfoArrayElements())
        {
            return &m_pDisplayModeInfo[idx];
        }
    }
    return NULL;
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
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    m_hStopEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_hStopEvent)
    {
        return;
    }

    m_hDC = ::CreateDC(NULL, m_DeviceName.c_str(), NULL, NULL);

    D3DKMT_OPENADAPTERFROMHDC openAdapter = {0};
    openAdapter.hDc = m_hDC;

    NTSTATUS status = D3DKMTOpenAdapterFromHdc(&openAdapter);
    if (NT_SUCCESS(status))
    {
        UpdateDisplayConfig();
        m_hAdapter = openAdapter.hAdapter;
        if (QueryAdapterId())
        {
            std::wstring EventName = GLOBAL_OBJECTS;
            EventName += RESOLUTION_EVENT_NAME;
            EventName += std::to_wstring(m_Id);
            m_hResolutionEvent = ::OpenEvent(EVENT_ALL_ACCESS | EVENT_MODIFY_STATE, FALSE, EventName.c_str());
            if (m_hResolutionEvent == NULL)
            {
                PrintMessage(L"Cannot open event %ws Error = %d.\n", EventName.c_str(), GetLastError());
                return;
            }
            m_hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ServiceThread, (LPVOID)this, 0, NULL);
            if (m_hThread == NULL)
            {
                PrintMessage(L"Cannot create thread Error = %d.\n", GetLastError());
                return;
            }
        }
    }
    SetStatus(Active);
}

bool GpuAdapter::QueryAdapterId()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hAdapter)
    {
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
        }
        else
        {
            m_Id = data.Id;
            return true;
        }
    }
    return false;
}

bool GpuAdapter::GetCurrentResolution(PVIOGPU_DISP_MODE mode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    DISPLAYCONFIG_MODE_INFO *pConfig = GetDisplayConfig();
    if (pConfig)
    {
        mode->XResolution = (USHORT)pConfig->sourceMode.width;
        mode->YResolution = (USHORT)pConfig->sourceMode.height;
        return true;
    }
    return false;
}

bool GpuAdapter::GetCustomResolution(PVIOGPU_DISP_MODE pmode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hAdapter && pmode && m_PathIdx < GetNumbersOfPathArrayElements())
    {
        // An adapter can drive several displays, so ask for the size of this display's source.
        UINT32 SourceId = m_pDisplayPathInfo[m_PathIdx].sourceInfo.id;
        VIOGPU_ESCAPE data{0};
        data.DataLength = sizeof(VIOGPU_SOURCE_MODE);
        data.Type = VIOGPU_GET_SOURCE_RESOLUTION;
        data.SourceMode.SourceId = SourceId;

        D3DKMT_ESCAPE escape = {0};
        escape.hAdapter = m_hAdapter;
        escape.pPrivateDriverData = &data;
        escape.PrivateDriverDataSize = sizeof(data);

        NTSTATUS status = D3DKMTEscape(&escape);
        if (NT_SUCCESS(status))
        {
            pmode->XResolution = data.SourceMode.Resolution.XResolution;
            pmode->YResolution = data.SourceMode.Resolution.YResolution;
            PrintMessage(L"%ws source %d (%dx%d)\n", __FUNCTIONW__, SourceId, pmode->XResolution, pmode->YResolution);
            return true;
        }
        PrintMessage(L"D3DKMTEscape source %d failed with status = 0x%0X\n", SourceId, status);
        if (SourceId != 0)
        {
            return false;
        }

        // A driver without VIOGPU_GET_SOURCE_RESOLUTION has a single source, and this is its size.
        data = {0};
        data.DataLength = sizeof(VIOGPU_DISP_MODE);
        data.Type = VIOGPU_GET_CUSTOM_RESOLUTION;
        status = D3DKMTEscape(&escape);
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

bool GpuAdapter::SetResolution(PVIOGPU_DISP_MODE mode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    DISPLAYCONFIG_MODE_INFO *pConfig = GetDisplayConfig();
    if (pConfig == NULL)
    {
        return false;
    }
    pConfig->sourceMode.width = mode->XResolution;
    pConfig->sourceMode.height = mode->YResolution;
    LONG result = SetDisplayConfig(m_PathArrayElements,
                                   m_pDisplayPathInfo,
                                   m_ModeInfoArrayElements,
                                   m_pDisplayModeInfo,
                                   SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES | SDC_SAVE_TO_DATABASE);
    PrintMessage(L"%ws %ws (%dx%d) SetDisplayConfig = %d\n",
                 __FUNCTIONW__,
                 m_DeviceName.c_str(),
                 mode->XResolution,
                 mode->YResolution,
                 result);
    return result == ERROR_SUCCESS;
}

void GpuAdapter::SyncResolution(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    VIOGPU_DISP_MODE custom = {0};
    VIOGPU_DISP_MODE current = {0};

    LockDisplayConfig();
    UpdateDisplayConfig();
    if (m_PathIdx == INVALID_PATH_IDX)
    {
        // No active path: the display is not part of the desktop, so there is no mode to set.
        PrintMessage(L"%ws %ws is not active\n", __FUNCTIONW__, m_DeviceName.c_str());
    }
    else if (GetCustomResolution(&custom) && GetCurrentResolution(&current) &&
             (custom.XResolution != current.XResolution || custom.YResolution != current.YResolution))
    {
        SetResolution(&custom);
    }
    UnlockDisplayConfig();
}

void GpuAdapter::Run()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hThread != NULL && m_hStopEvent != NULL && m_hResolutionEvent != NULL)
    {
        SyncResolution();

        const HANDLE handles[] = {m_hStopEvent, m_hResolutionEvent};
        while (1)
        {
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0)
            {
                break;
            }
            SyncResolution();
        }
    }
}

DWORD WINAPI GpuAdapter::ServiceThread(GpuAdapter *ptr)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    ptr->Run();
    return 0;
}

void GpuAdapter::Close()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hThread != NULL)
    {
        if (m_hStopEvent != NULL)
        {
            SetEvent(m_hStopEvent);
            if (WAIT_TIMEOUT == WaitForSingleObject(m_hThread, 1000))
            {
                PrintMessage(L"Cannot close thread after 1 sec\n");
                TerminateThread(m_hThread, 0);
            }
        }
        m_hThread = NULL;
    }

    if (m_hStopEvent)
    {
        CloseHandle(m_hStopEvent);
        m_hStopEvent = NULL;
    }

    if (m_hResolutionEvent)
    {
        CloseHandle(m_hResolutionEvent);
        m_hResolutionEvent = NULL;
    }

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
