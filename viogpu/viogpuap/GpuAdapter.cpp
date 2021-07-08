#include "pch.h"

GpuAdapter::GpuAdapter(const std::wstring LinkName) :
    m_hThread(NULL),
    m_hStopEvent(NULL),
    m_hResolutionEvent(NULL),
    m_hDC(NULL),
    m_hAdapter(NULL),
    m_Index(-1),
    m_PathArrayElements(0),
    m_ModeInfoArrayElements(0),
    m_pDisplayPathInfo(NULL),
    m_pDisplayModeInfo(NULL),
    m_Flag(None)
{
    m_DeviceName = LinkName;
    PrintMessage(L"%ws %ws\n", __FUNCTIONW__, m_DeviceName.c_str());
    Init();
};

void GpuAdapter::UpdateDisplayConfig(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    UINT32 filter = QDC_ALL_PATHS;
    ClearDisplayConfig();

    if (FAILED(HRESULT_FROM_WIN32(::GetDisplayConfigBufferSizes(filter, &m_PathArrayElements, &m_ModeInfoArrayElements)))) {
        PrintMessage(L"GetDisplayConfigBufferSizes faled\n");
        return;
    }

    m_pDisplayPathInfo = new DISPLAYCONFIG_PATH_INFO[m_PathArrayElements];
    m_pDisplayModeInfo = new DISPLAYCONFIG_MODE_INFO[m_ModeInfoArrayElements];
    ZeroMemory(m_pDisplayPathInfo, sizeof(DISPLAYCONFIG_PATH_INFO)* m_PathArrayElements);
    ZeroMemory(m_pDisplayModeInfo, sizeof(DISPLAYCONFIG_MODE_INFO)* m_ModeInfoArrayElements);

    if (SUCCEEDED(HRESULT_FROM_WIN32(::QueryDisplayConfig(filter,
        &m_PathArrayElements,
        m_pDisplayPathInfo,
        &m_ModeInfoArrayElements,
        m_pDisplayModeInfo, NULL)))) {

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
                    m_Index = PathIdx;
                    break;
                }
            }
        }
    }
};

DISPLAYCONFIG_MODE_INFO* GpuAdapter::GetDisplayConfig(UINT index)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (index < GetNumbersOfPathArrayElements())
    {
        UINT idx = m_pDisplayPathInfo[index].sourceInfo.modeInfoIdx;
        PrintMessage(L"%ws m_Index %d idx %d active %d\n", __FUNCTIONW__, index, idx, m_pDisplayPathInfo[index].flags & DISPLAYCONFIG_PATH_ACTIVE);
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
    if (!m_hStopEvent) {
        return;
    }

    m_hDC = ::CreateDC(NULL, m_DeviceName.c_str(), NULL, NULL);

    D3DKMT_OPENADAPTERFROMHDC openAdapter = { 0 };
    openAdapter.hDc = m_hDC;

    NTSTATUS status = D3DKMTOpenAdapterFromHdc(&openAdapter);
    if (NT_SUCCESS(status))
    {
        UpdateDisplayConfig();
        m_hAdapter = openAdapter.hAdapter;
        if (QueryAdapterId()) {
            std::wstring EventName = GLOBAL_OBJECTS;
            EventName += RESOLUTION_EVENT_NAME;
            EventName += std::to_wstring(m_Index);
            m_hResolutionEvent = ::OpenEvent(EVENT_ALL_ACCESS | EVENT_MODIFY_STATE, FALSE, EventName.c_str());
            if (m_hResolutionEvent == NULL) {
                PrintMessage(L"Cannot open event %ws Error = %d.\n", EventName.c_str(), GetLastError());
                return;
            }
            m_hThread = CreateThread(
                NULL,
                0,
                (LPTHREAD_START_ROUTINE)ServiceThread,
                (LPVOID)this,
                0,
                NULL);
            if (m_hThread == NULL) {
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

    if (m_hAdapter) {
        VIOGPU_ESCAPE data{ 0 };
        data.DataLength = sizeof(ULONG);
        data.Type = VIOGPU_GET_DEVICE_ID;

        D3DKMT_ESCAPE escape = { 0 };
        escape.hAdapter = m_hAdapter;
        escape.pPrivateDriverData = &data;
        escape.PrivateDriverDataSize = sizeof(data);

        NTSTATUS status = D3DKMTEscape(&escape);
        if (!NT_SUCCESS(status))
        {
            PrintMessage(L"D3DKMTEscape failed with status = 0x%x\n", status);
        }
        else {
            m_Index = data.Id;
            return true;
        }
    }
    return false;
}

bool GpuAdapter::GetCurrentResolution(PVIOGPU_DISP_MODE mode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    DISPLAYCONFIG_MODE_INFO* pConfig = GetDisplayConfig(m_Index);
    if (pConfig) {
        mode->XResolution = (USHORT)pConfig->sourceMode.width;
        mode->YResolution = (USHORT)pConfig->sourceMode.height;
        return true;
    }
    return false;
}

bool GpuAdapter::GetCustomResolution(PVIOGPU_DISP_MODE pmode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hAdapter && pmode) {
        VIOGPU_ESCAPE data{ 0 };
        data.DataLength = sizeof(VIOGPU_DISP_MODE);
        data.Type = VIOGPU_GET_CUSTOM_RESOLUTION;

        D3DKMT_ESCAPE escape = { 0 };
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

bool GpuAdapter::SetResolution(PVIOGPU_DISP_MODE mode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    int ix = m_pDisplayPathInfo[m_Index].sourceInfo.modeInfoIdx;
    PrintMessage(L"%ws m_Index %d %d (%dx%d)\n", __FUNCTIONW__, m_Index, ix, mode->XResolution, mode->YResolution);
    m_pDisplayModeInfo[ix].sourceMode.width = mode->XResolution;
    m_pDisplayModeInfo[ix].sourceMode.height = mode->YResolution;
    SetDisplayConfig(m_PathArrayElements, m_pDisplayPathInfo, m_ModeInfoArrayElements, m_pDisplayModeInfo, SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES | SDC_SAVE_TO_DATABASE);
    return true;
}

void GpuAdapter::Run()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hThread != NULL &&
        m_hStopEvent != NULL &&
        m_hResolutionEvent != NULL) {
        const HANDLE handles[] = { m_hStopEvent , m_hResolutionEvent };
        while (1) {
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) {
                break;
            }
            VIOGPU_DISP_MODE custom = { 0 };
            UpdateDisplayConfig();
            if (GetCustomResolution(&custom)) {
                VIOGPU_DISP_MODE current = { 0 };
                GetCurrentResolution(&current);
                SetResolution(&custom);
            }
        }
    }
}

DWORD WINAPI GpuAdapter::ServiceThread(GpuAdapter* ptr)
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
        if (m_hStopEvent != NULL) {
            SetEvent(m_hStopEvent);
            if (WAIT_TIMEOUT == WaitForSingleObject(m_hThread, 1000))
            {
                PrintMessage(L"Cannot close thread after 1 sec\n");
                TerminateThread(m_hThread, 0);
            }
        }
        m_hThread = NULL;
    }

    if (m_hStopEvent) {
        CloseHandle(m_hStopEvent);
        m_hStopEvent = NULL;
    }

    if (m_hResolutionEvent) {
        CloseHandle(m_hResolutionEvent);
        m_hResolutionEvent = NULL;
    }

    if (m_hAdapter) {
        D3DKMT_CLOSEADAPTER close = { m_hAdapter };
        NTSTATUS status = D3DKMTCloseAdapter(&close);
        if (!NT_SUCCESS(status))
        {
            PrintMessage(L"D3DKMTCloseAdapter failed with status = 0x%x.\n", status);
        }
        m_hAdapter = NULL;
    }

    if (m_hDC != NULL) {
        ReleaseDC(NULL, m_hDC);
        m_hDC = NULL;
    }
}
