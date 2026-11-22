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

// One display's sync at a time: a thread reads its display's path and mode, and sets the mode, while no other thread
// of this process changes a mode. A mutex rather than an SRW lock: Close terminates a thread that does not stop in
// time, and the mutex of a thread that ended while it held it is abandoned, so the next wait takes it and the other
// threads do not block on it for good.
static HANDLE DisplayConfigMutex = CreateMutex(NULL, FALSE, NULL);

// Takes the lock, or returns false when hStop is set first or the wait fails. Without the mutex, which CreateMutex
// failed to make, the syncs go on unserialized.
static bool LockDisplayConfig(HANDLE hStop)
{
    if (DisplayConfigMutex == NULL)
    {
        PrintMessage(L"%ws no lock: CreateMutex failed\n", __FUNCTIONW__);
        return true;
    }
    const HANDLE handles[] = {hStop, DisplayConfigMutex};
    // WAIT_ABANDONED, when the previous owner was terminated, gives the lock all the same.
    DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (wait == WAIT_OBJECT_0 + 1 || wait == WAIT_ABANDONED_0 + 1)
    {
        return true;
    }
    if (wait == WAIT_FAILED)
    {
        PrintMessage(L"%ws WaitForMultipleObjects failed with error %d\n", __FUNCTIONW__, GetLastError());
    }
    return false;
}

static void UnlockDisplayConfig(void)
{
    if (DisplayConfigMutex != NULL)
    {
        ReleaseMutex(DisplayConfigMutex);
    }
}

// What a driver without VIOGPU_GET_SOURCE_RESOLUTION answers it with, and what the driver answers while a source drives
// no connected display (ntstatus.h collides with winnt.h).
static const NTSTATUS StatusInvalidParameter = (NTSTATUS)0xC000000DL;
static const NTSTATUS StatusDeviceNotConnected = (NTSTATUS)0xC000009DL;

GpuAdapter::GpuAdapter(const std::wstring LinkName)
    : m_hThread(NULL), m_hStopEvent(NULL), m_hResolutionEvent(NULL), m_DeviceEventOnly(false), m_Escape(EscapeUnknown),
      m_SyncProblem(L""), m_SyncStatus(0), m_hDC(NULL), m_hAdapter(NULL), m_Id(0), m_PathIdx(INVALID_PATH_IDX),
      m_PathArrayElements(0), m_ModeInfoArrayElements(0), m_pDisplayPathInfo(NULL), m_pDisplayModeInfo(NULL),
      m_Flag(None)
{
    m_DeviceName = LinkName;
    PrintMessage(L"%ws %ws\n", __FUNCTIONW__, m_DeviceName.c_str());
    Init();
};

// Takes a snapshot of the active paths and finds this display's path in it. ERROR_SUCCESS with m_PathIdx at
// INVALID_PATH_IDX when the display has no active path; another error, logged, when the snapshot failed.
LONG GpuAdapter::UpdateDisplayConfig(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

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
    if (m_hDC == NULL)
    {
        PrintMessage(L"%ws %ws: CreateDC failed, Error = %d: set up again when found next\n",
                     __FUNCTIONW__,
                     m_DeviceName.c_str(),
                     GetLastError());
    }

    D3DKMT_OPENADAPTERFROMHDC openAdapter = {0};
    openAdapter.hDc = m_hDC;

    NTSTATUS status = D3DKMTOpenAdapterFromHdc(&openAdapter);
    if (!NT_SUCCESS(status))
    {
        PrintMessage(L"%ws %ws: D3DKMTOpenAdapterFromHdc failed with status 0x%08X: set up again when found next\n",
                     __FUNCTIONW__,
                     m_DeviceName.c_str(),
                     status);
    }
    else
    {
        m_hAdapter = openAdapter.hAdapter;
        if (QueryAdapterId())
        {
            std::wstring EventName = GLOBAL_OBJECTS;
            EventName += RESOLUTION_EVENT_NAME;
            EventName += std::to_wstring(m_Id);
            // The source's own event stays set until Run waits on it. A driver without one has only the device's
            // event, which it pulses: a change made while Run is busy is then missed.
            std::wstring SourceEventName = EventName + L"_" + std::to_wstring(openAdapter.VidPnSourceId);
            m_hResolutionEvent = ::OpenEvent(SYNCHRONIZE, FALSE, SourceEventName.c_str());
            if (m_hResolutionEvent == NULL)
            {
                PrintMessage(L"Cannot open event %ws Error = %d: waiting on %ws, which can miss a change made while "
                             L"this display syncs\n",
                             SourceEventName.c_str(),
                             GetLastError(),
                             EventName.c_str());
                m_DeviceEventOnly = true;
                m_hResolutionEvent = ::OpenEvent(EVENT_ALL_ACCESS | EVENT_MODIFY_STATE, FALSE, EventName.c_str());
            }
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

// Asks the driver for this display's size, its generation and the scanout they are for (VIOGPU_GET_SOURCE_RESOLUTION),
// or, from a driver without that escape, for the size of its one source (VIOGPU_GET_CUSTOM_RESOLUTION; generation 0,
// so every size is applied). Returns the driver's status: STATUS_DEVICE_NOT_CONNECTED while the source drives no
// connected display. Needs this display's path in the snapshot.
NTSTATUS GpuAdapter::GetCustomResolution(PVIOGPU_SOURCE_MODE pAnswer)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    const DISPLAYCONFIG_PATH_INFO &Path = m_pDisplayPathInfo[m_PathIdx];
    VIOGPU_ESCAPE data{0};
    D3DKMT_ESCAPE escape = {0};
    escape.hAdapter = m_hAdapter;
    escape.pPrivateDriverData = &data;
    escape.PrivateDriverDataSize = sizeof(data);
    NTSTATUS status;

    if (m_Escape != EscapeLegacy)
    {
        data.DataLength = sizeof(VIOGPU_SOURCE_MODE);
        data.Type = VIOGPU_GET_SOURCE_RESOLUTION;
        data.SourceMode.SourceId = Path.sourceInfo.id;
        data.SourceMode.Layout = CurrentLayoutKey();
        status = D3DKMTEscape(&escape);
        if (NT_SUCCESS(status))
        {
            if (m_Escape == EscapeUnknown && m_DeviceEventOnly)
            {
                PrintMessage(L"%ws %ws: the driver has per-source events, but this display waits on the device's: a "
                             L"change made while it syncs can be missed\n",
                             __FUNCTIONW__,
                             m_DeviceName.c_str());
            }
            m_Escape = EscapeSource;
            *pAnswer = data.SourceMode;
            PrintMessage(L"%ws source %u: scanout %u (%ux%u) generation %u, %u applied in this layout\n",
                         __FUNCTIONW__,
                         pAnswer->SourceId,
                         pAnswer->ScanoutId,
                         pAnswer->Resolution.XResolution,
                         pAnswer->Resolution.YResolution,
                         pAnswer->Generation,
                         pAnswer->Applied);
            return status;
        }
        PrintMessage(L"%ws source %u: VIOGPU_GET_SOURCE_RESOLUTION failed with status 0x%08X\n",
                     __FUNCTIONW__,
                     Path.sourceInfo.id,
                     status);
        // Only a driver without the escape lacks source 0, and a driver that answered it once has it: any other
        // failure is the answer for this display, which the old escape would give another display's size for.
        if (status != StatusInvalidParameter || Path.sourceInfo.id != 0 || m_Escape == EscapeSource)
        {
            return status;
        }
        m_Escape = EscapeLegacy;
        PrintMessage(L"%ws: a driver without VIOGPU_GET_SOURCE_RESOLUTION: VIOGPU_GET_CUSTOM_RESOLUTION, every size "
                     L"applied\n",
                     __FUNCTIONW__);
    }

    data = {0};
    data.DataLength = sizeof(VIOGPU_DISP_MODE);
    data.Type = VIOGPU_GET_CUSTOM_RESOLUTION;
    status = D3DKMTEscape(&escape);
    if (NT_SUCCESS(status))
    {
        pAnswer->SourceId = Path.sourceInfo.id;
        pAnswer->Resolution = data.Resolution;
        pAnswer->Generation = 0;
        pAnswer->ScanoutId = Path.targetInfo.id;
        pAnswer->Layout = 0;
        pAnswer->Applied = 0;
        PrintMessage(L"%ws (%ux%u)\n", __FUNCTIONW__, pAnswer->Resolution.XResolution, pAnswer->Resolution.YResolution);
        return status;
    }
    PrintMessage(L"%ws: VIOGPU_GET_CUSTOM_RESOLUTION failed with status 0x%08X\n", __FUNCTIONW__, status);
    return status;
}

// The layout of the last snapshot, in one order whatever order Windows listed it in: every active path, by its adapter,
// source and target.
std::wstring GpuAdapter::CurrentLayout(void)
{
    std::vector<std::wstring> Paths;
    for (UINT PathIdx = 0; PathIdx < GetNumbersOfPathArrayElements(); ++PathIdx)
    {
        const DISPLAYCONFIG_PATH_INFO &Path = m_pDisplayPathInfo[PathIdx];
        wchar_t Entry[64];
        swprintf_s(Entry,
                   L"%08lx%08lx:%u:%u;",
                   Path.sourceInfo.adapterId.HighPart,
                   Path.sourceInfo.adapterId.LowPart,
                   Path.sourceInfo.id,
                   Path.targetInfo.id);
        Paths.push_back(Entry);
    }
    std::sort(Paths.begin(), Paths.end());
    std::wstring Layout;
    for (const std::wstring &Entry : Paths)
    {
        Layout += Entry;
    }
    return Layout;
}

// The key the driver keeps the applied generation under for the current layout (VIOGPU_SOURCE_MODE.Layout): a 64-bit
// FNV-1a hash of CurrentLayout, never 0.
ULONGLONG GpuAdapter::CurrentLayoutKey(void)
{
    ULONGLONG Key = 0xcbf29ce484222325ULL;
    for (wchar_t c : CurrentLayout())
    {
        Key = (Key ^ (ULONGLONG)c) * 0x100000001b3ULL;
    }
    return Key != 0 ? Key : 1;
}

// Tells the driver that the display took the size of the answer's generation in this layout, so that a helper started
// again leaves the size the user chooses next alone (VIOGPU_SET_SOURCE_APPLIED).
void GpuAdapter::SetAppliedGeneration(const VIOGPU_SOURCE_MODE &Answer)
{
    VIOGPU_ESCAPE data{0};
    D3DKMT_ESCAPE escape = {0};
    escape.hAdapter = m_hAdapter;
    escape.pPrivateDriverData = &data;
    escape.PrivateDriverDataSize = sizeof(data);
    data.DataLength = sizeof(VIOGPU_SOURCE_MODE);
    data.Type = VIOGPU_SET_SOURCE_APPLIED;
    data.SourceMode = Answer;
    NTSTATUS status = D3DKMTEscape(&escape);
    if (!NT_SUCCESS(status))
    {
        PrintMessage(L"%ws source %u: VIOGPU_SET_SOURCE_APPLIED failed with status 0x%08X: a helper started again "
                     L"applies generation %u once more\n",
                     __FUNCTIONW__,
                     Answer.SourceId,
                     status,
                     Answer.Generation);
    }
}

// Sets this display's size, and saves it (CDS_UPDATEREGISTRY). Returns ChangeDisplaySettingsEx's result.
LONG GpuAdapter::SetResolution(PVIOGPU_DISP_MODE mode)
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
    return result;
}

// Gives the display the size the host asks for, unless its generation is the one last applied to the display in this
// layout. Waiting when the display has no active path yet, as right after a replug, when the driver has no size for
// it, when the driver answered for another scanout than the path's (a commit is moving the source), or when the
// display configuration could not be read; SetFailed when the mode change failed; Failed when the driver refused
// otherwise. The generation is recorded only once the display has the size.
GpuAdapter::SyncResult GpuAdapter::SyncResolution(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    VIOGPU_SOURCE_MODE answer = {0};
    VIOGPU_DISP_MODE current = {0};
    SyncResult Result = Failed;

    if (!LockDisplayConfig(m_hStopEvent))
    {
        // Stopping; or the wait failed (logged), and the display is tried again.
        m_SyncProblem = L"the display configuration lock could not be taken";
        m_SyncStatus = 0;
        return WaitForSingleObject(m_hStopEvent, 0) == WAIT_OBJECT_0 ? Synced : Waiting;
    }
    m_SyncStatus = UpdateDisplayConfig();
    if (m_SyncStatus != ERROR_SUCCESS)
    {
        m_SyncProblem = L"the display configuration could not be read";
        Result = Waiting;
    }
    else if (m_PathIdx == INVALID_PATH_IDX)
    {
        // No active path: the display is not part of the desktop, so there is no mode to set.
        PrintMessage(L"%ws %ws has no active path\n", __FUNCTIONW__, m_DeviceName.c_str());
        m_SyncProblem = L"no active path";
        Result = Waiting;
    }
    else if (!NT_SUCCESS(m_SyncStatus = GetCustomResolution(&answer)))
    {
        // The driver signals the source when it drives a connected display again; any other failure stays.
        m_SyncProblem = L"the driver gave no size";
        Result = m_SyncStatus == StatusDeviceNotConnected ? Waiting : Failed;
    }
    else if (answer.Generation != 0 && answer.ScanoutId != m_pDisplayPathInfo[m_PathIdx].targetInfo.id)
    {
        // The snapshot and the driver disagree on the source's scanout: a commit is moving it, and the driver
        // signals the source once it has.
        PrintMessage(L"%ws %ws: the driver answered for scanout %u, the path's target is %u\n",
                     __FUNCTIONW__,
                     m_DeviceName.c_str(),
                     answer.ScanoutId,
                     m_pDisplayPathInfo[m_PathIdx].targetInfo.id);
        m_SyncProblem = L"the driver answered for another scanout";
        Result = Waiting;
    }
    else
    {
        if (answer.Generation != 0 && answer.Generation == answer.Applied)
        {
            // Nothing new for this display in this layout since the size last applied: the size it has stays.
            PrintMessage(L"%ws %ws: generation %u applied before in this layout: the display keeps its size\n",
                         __FUNCTIONW__,
                         m_DeviceName.c_str(),
                         answer.Generation);
            Result = Synced;
        }
        else if (!GetCurrentResolution(&current))
        {
            m_SyncProblem = L"the display has no current mode";
            m_SyncStatus = 0;
            Result = Waiting;
        }
        else
        {
            bool already = answer.Resolution.XResolution == current.XResolution &&
                           answer.Resolution.YResolution == current.YResolution;
            m_SyncStatus = already ? DISP_CHANGE_SUCCESSFUL : SetResolution(&answer.Resolution);
            if (m_SyncStatus == DISP_CHANGE_SUCCESSFUL)
            {
                PrintMessage(L"%ws %ws: %ux%u generation %u %ws\n",
                             __FUNCTIONW__,
                             m_DeviceName.c_str(),
                             answer.Resolution.XResolution,
                             answer.Resolution.YResolution,
                             answer.Generation,
                             already ? L"was the display's size already" : L"applied");
                if (answer.Generation != 0)
                {
                    SetAppliedGeneration(answer);
                }
                Result = Synced;
            }
            else
            {
                m_SyncProblem = L"the mode change failed";
                Result = SetFailed;
            }
        }
    }
    UnlockDisplayConfig();
    return Result;
}

void GpuAdapter::Run()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    // A display waiting for a path or a size is tried again for a while: after a replug Windows sets its path up only
    // some time after the driver reported the new size. A failed mode change is tried a few times.
    const DWORD RetryMs = 500;
    const UINT MaxWaiting = 20;
    const UINT MaxSetFailed = 3;

    // Init starts this thread once the events are open; m_hThread may not be set yet.
    if (m_hStopEvent != NULL && m_hResolutionEvent != NULL)
    {
        UINT Retries = 0;
        SyncResult Result = SyncResolution();

        const HANDLE handles[] = {m_hStopEvent, m_hResolutionEvent};
        while (1)
        {
            UINT MaxRetries = Result == Waiting ? MaxWaiting : Result == SetFailed ? MaxSetFailed : 0;
            DWORD Timeout = INFINITE;
            if (Result != Synced && Retries < MaxRetries)
            {
                Timeout = RetryMs;
            }
            else if (Result != Synced)
            {
                PrintMessage(L"%ws %ws not synced after %u tries (%ws, 0x%08X), until the next change\n",
                             __FUNCTIONW__,
                             m_DeviceName.c_str(),
                             Retries + 1,
                             m_SyncProblem,
                             m_SyncStatus);
            }
            DWORD Wait = WaitForMultipleObjects(2, handles, FALSE, Timeout);
            if (Wait == WAIT_OBJECT_0)
            {
                break;
            }
            if (Wait == WAIT_FAILED)
            {
                PrintMessage(L"%ws WaitForMultipleObjects failed with error %d\n", __FUNCTIONW__, GetLastError());
                break;
            }
            Retries = (Wait == WAIT_TIMEOUT) ? Retries + 1 : 0;
            Result = SyncResolution();
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
        CloseHandle(m_hThread);
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
