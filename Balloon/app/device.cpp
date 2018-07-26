#include "stdafx.h"

CDevice::CDevice()
{
    m_pMemStat = NULL;
    m_pService = NULL;
    m_hThread = NULL;
    m_evtInitialized = NULL;
    m_evtTerminate = NULL;
    m_evtWrite = NULL;
}

CDevice::~CDevice()
{
    Fini();
}

BOOL CDevice::Init(CService *Service)
{
    m_pMemStat = new CMemStat();
    if (!m_pMemStat || !m_pMemStat->Init()) {
        return FALSE;
    }

    m_evtInitialized = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!m_evtInitialized) {
        return FALSE;
    }

    m_evtTerminate = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!m_evtTerminate) {
        return FALSE;
    }

    m_evtWrite = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!m_evtWrite) {
        return FALSE;
    }

    m_pService = Service;

    return TRUE;
}

VOID CDevice::Fini()
{
    Stop();

    if (m_evtWrite) {
        CloseHandle(m_evtWrite);
        m_evtWrite = NULL;
    }

    if (m_evtInitialized) {
        CloseHandle(m_evtInitialized);
        m_evtInitialized = NULL;
    }

    if (m_evtTerminate) {
        CloseHandle(m_evtTerminate);
        m_evtTerminate = NULL;
    }

    m_pService = NULL;

    delete m_pMemStat;
    m_pMemStat = NULL;
}

DWORD CDevice::Run()
{
	PWCHAR DevicePath = GetDevicePath((LPGUID)&GUID_DEVINTERFACE_BALLOON);
    if (DevicePath == NULL) {
        PrintMessage("File not found.");
        return ERROR_FILE_NOT_FOUND;
    }

    HANDLE hDevice = CreateFile(DevicePath, GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);

    free(DevicePath);

    if (hDevice == INVALID_HANDLE_VALUE) {
        PrintMessage("Failed to create file.");
        return GetLastError();
    }

    NOTIFY_HANDLE devnotify = m_pService->RegisterDeviceHandleNotification(
        hDevice);

    if (!devnotify) {
        DWORD err = GetLastError();
        PrintMessage("Failed to register handle notification.");
        CloseHandle(hDevice);
        return err;
    }

    SetEvent(m_evtInitialized);

    WriteLoop(hDevice);

    m_pService->UnregisterNotification(devnotify);
    CloseHandle(hDevice);

    return NO_ERROR;
}

VOID CDevice::WriteLoop(HANDLE hDevice)
{
    HANDLE waitfor[] = { m_evtTerminate, m_evtWrite };
    OVERLAPPED ovlp;
    DWORD timeout;
    DWORD written;
    DWORD waitrc;
    BOOL writerc;

    ZeroMemory(&ovlp, sizeof(ovlp));
    ovlp.hEvent = m_evtWrite;

    while (1) {
        // The old version of the balloon driver didn't block write requests
        // until stats were requested. So in order not to consume too much CPU
        // we keep the old 1s delay behavior and switch to infinite wait only
        // if write result is pending.
        timeout = 1000;

        if (m_pMemStat->Update()) {
            writerc = WriteFile(hDevice, m_pMemStat->GetBuffer(),
                (DWORD)m_pMemStat->GetSize(), NULL, &ovlp);
            if (!writerc && (GetLastError() == ERROR_IO_PENDING)) {
                timeout = INFINITE;
            }
        }

        waitrc = WaitForMultipleObjects(sizeof(waitfor) / sizeof(waitfor[0]),
            waitfor, FALSE, timeout);

        if (waitrc == WAIT_OBJECT_0) {
            break;
        }
        else if (waitrc == WAIT_OBJECT_0 + 1) {
            if (!GetOverlappedResult(hDevice, &ovlp, &written, FALSE) ||
                (written != m_pMemStat->GetSize()))
            {
                PrintMessage("Failed to write stats.");
            }
        }
    }
}

BOOL CDevice::Start()
{
    DWORD tid, waitrc;

    if (!m_hThread) {
        m_hThread = CreateThread(NULL, 0,
            (LPTHREAD_START_ROUTINE)DeviceThread, (LPVOID)this, 0, &tid);
        if (!m_hThread) {
            return FALSE;
        }

        HANDLE waitfor[] = { m_evtInitialized, m_hThread };
        waitrc = WaitForMultipleObjects(sizeof(waitfor) / sizeof(waitfor[0]),
            waitfor, FALSE, INFINITE);
        if (waitrc != WAIT_OBJECT_0) {
            // the thread failed to initialize
            CloseHandle(m_hThread);
            m_hThread = NULL;
        }
    }

    // keep the original behavior of reporting success
    // even if the thread failed to initialize
    return TRUE;
}

VOID CDevice::Stop()
{
    if (m_hThread) {
        SetEvent(m_evtTerminate);
        if (WaitForSingleObject(m_hThread, 1000) == WAIT_TIMEOUT) {
            TerminateThread(m_hThread, 0);
        }
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
}

DWORD WINAPI CDevice::DeviceThread(LPDWORD lParam)
{
    CDevice* pDev = reinterpret_cast<CDevice*>(lParam);
    return pDev->Run();
}

#ifndef UNIVERSAL

PTCHAR CDevice::GetDevicePath( IN  LPGUID InterfaceGuid )
{
    HDEVINFO HardwareDeviceInfo;
    SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData = NULL;
    ULONG Length, RequiredLength = 0;
    BOOL bResult;
    PTCHAR DevicePath;

    HardwareDeviceInfo = SetupDiGetClassDevs(
                             InterfaceGuid,
                             NULL,
                             NULL,
                             (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
                             );

    if (HardwareDeviceInfo == INVALID_HANDLE_VALUE) {
        PrintMessage("Cannot get class devices");
        return NULL;
    }

    DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    bResult = SetupDiEnumDeviceInterfaces(
                             HardwareDeviceInfo,
                             0,
                             InterfaceGuid,
                             0,
                             &DeviceInterfaceData
                             );

    if (bResult == FALSE) {
        PrintMessage("Cannot get enumerate device interfaces");
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        return NULL;
    }

    SetupDiGetDeviceInterfaceDetail(
                             HardwareDeviceInfo,
                             &DeviceInterfaceData,
                             NULL,
                             0,
                             &RequiredLength,
                             NULL
                             );

    DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) LocalAlloc(LMEM_FIXED, RequiredLength);

    if (DeviceInterfaceDetailData == NULL) {
        PrintMessage("Cannot allocate memory");
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        return NULL;
    }

    DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    Length = RequiredLength;

    bResult = SetupDiGetDeviceInterfaceDetail(
                             HardwareDeviceInfo,
                             &DeviceInterfaceData,
                             DeviceInterfaceDetailData,
                             Length,
                             &RequiredLength,
                             NULL
                             );

    if (bResult == FALSE) {
        PrintMessage("Cannot get device interface details");
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        LocalFree(DeviceInterfaceDetailData);
        return NULL;
    }

    DevicePath = _tcsdup(DeviceInterfaceDetailData->DevicePath);

    SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
    LocalFree(DeviceInterfaceDetailData);

    return DevicePath;
}

#else // UNIVERSAL

PTCHAR CDevice::GetDevicePath(IN LPGUID InterfaceGuid)
{
    PWSTR DeviceInterfaceList = NULL;
    ULONG DeviceInterfaceListLength = 0;
    PTCHAR DevicePath = NULL;
    CONFIGRET cr;

    do
    {
        cr = CM_Get_Device_Interface_List_Size(&DeviceInterfaceListLength,
            InterfaceGuid, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        if (cr != CR_SUCCESS)
        {
            break;
        }

        if (DeviceInterfaceList != NULL)
        {
            HeapFree(GetProcessHeap(), 0, DeviceInterfaceList);
        }

        DeviceInterfaceList = (PWSTR)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, DeviceInterfaceListLength * sizeof(WCHAR));

        if (DeviceInterfaceList == NULL)
        {
            cr = CR_OUT_OF_MEMORY;
            break;
        }

        cr = CM_Get_Device_Interface_List(InterfaceGuid, NULL,
            DeviceInterfaceList, DeviceInterfaceListLength,
            CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

    } while (cr == CR_BUFFER_SMALL);

    if (cr == CR_SUCCESS)
    {
        DevicePath = _tcsdup(DeviceInterfaceList);
    }

    if (DeviceInterfaceList != NULL)
    {
        HeapFree(GetProcessHeap(), 0, DeviceInterfaceList);
    }

    return DevicePath;
}

#endif // UNIVERSAL
