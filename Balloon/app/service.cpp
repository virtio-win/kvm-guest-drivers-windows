#include "stdafx.h"

CService::CService()
{
    m_hDevNotify = NULL;
    m_evTerminate = NULL;
    m_bRunningService = FALSE;
    m_StatusHandle = NULL;
    m_pDev = NULL;
    m_Status = SERVICE_STOPPED;
}

CService::~CService()
{
    m_evTerminate = NULL;
    m_bRunningService = FALSE;
    m_StatusHandle = NULL;
    m_pDev = NULL;
    m_Status = SERVICE_STOPPED;
}

DWORD __stdcall CService::HandlerExThunk(CService* service, DWORD ctlcode, DWORD evtype, PVOID evdata)
{
    switch (ctlcode) {

    case SERVICE_CONTROL_DEVICEEVENT:
    case SERVICE_CONTROL_HARDWAREPROFILECHANGE:
        return service->ServiceHandleDeviceChange(evtype, (_DEV_BROADCAST_HEADER*) evdata);

    case SERVICE_CONTROL_POWEREVENT:
        return service->ServiceHandlePowerEvent(evtype, (DWORD)((DWORD_PTR) evdata));

    default:
        service->ServiceCtrlHandler(ctlcode);
        return NO_ERROR;
    }
}

void __stdcall CService::ServiceMainThunk(CService* service, DWORD argc, TCHAR* argv[])
{
    service->ServiceMain(argc, argv);
}

BOOL CService::InitService()
{
    m_bRunningService = TRUE;
    m_Status = SERVICE_RUNNING;
    return TRUE;
}

BOOL CService::SendStatusToSCM(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint)
{
    BOOL res;
    SERVICE_STATUS serviceStatus;

    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
    serviceStatus.dwCurrentState = dwCurrentState;

    if (dwCurrentState == SERVICE_START_PENDING) {
        serviceStatus.dwControlsAccepted = 0;
    } else {
        serviceStatus.dwControlsAccepted =
                SERVICE_ACCEPT_STOP |
                SERVICE_ACCEPT_SHUTDOWN;
    }

    if (dwServiceSpecificExitCode == 0) {
        serviceStatus.dwWin32ExitCode = dwWin32ExitCode;
    } else {
        serviceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
    }

    serviceStatus.dwServiceSpecificExitCode = dwServiceSpecificExitCode;
    serviceStatus.dwCheckPoint = dwCheckPoint;
    serviceStatus.dwWaitHint = dwWaitHint;

    res = SetServiceStatus (m_StatusHandle, &serviceStatus);
    if (!res) {
        StopService();
    }

    return res;
}

void CService::StopService()
{
    if (m_bRunningService && m_pDev) {
        m_pDev->Stop();
        m_bRunningService = FALSE;
        m_Status = SERVICE_STOPPED;
    }
    SetEvent(m_evTerminate);
}

void CService::terminate(DWORD error)
{
    if (m_hDevNotify) {
        UnregisterDeviceNotification(m_hDevNotify);
        m_hDevNotify = NULL;
    }

    if (m_evTerminate) {
        CloseHandle(m_evTerminate);
        m_evTerminate = NULL;
    }

    if (m_StatusHandle) {
        SendStatusToSCM(SERVICE_STOPPED, error, 0, 0, 0);
    }

    delete m_pDev;
}

void CService::ServiceCtrlHandler(DWORD controlCode)
{
    switch(controlCode)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            m_Status = SERVICE_STOP_PENDING;
            SendStatusToSCM(
                             m_Status,
                             NO_ERROR,
                             0,
                             1,
                             5000
                             );
            StopService();
            return;

        case SERVICE_CONTROL_INTERROGATE:
            break;

        default:
            break;
    }
    SendStatusToSCM(m_Status, NO_ERROR, 0, 0, 0);
}

DWORD CService::ServiceHandleDeviceChange(DWORD evtype, _DEV_BROADCAST_HEADER* dbhdr)
{
    switch (evtype)
    {
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEQUERYREMOVEFAILED:
            m_pDev->Start();
            break;

        case DBT_DEVICEQUERYREMOVE:
        case DBT_DEVICEREMOVECOMPLETE:
            m_pDev->Stop();
            break;

        default:
            break;
    }

    return NO_ERROR;
}

DWORD CService::ServiceHandlePowerEvent(DWORD evtype, DWORD flags)
{
    PrintMessage("ServiceHandlePowerEvent");
    return NO_ERROR;
}

void CService::ServiceMain(DWORD argc, LPTSTR *argv)
{
    BOOL res;

    if (!m_StatusHandle) {
        terminate(GetLastError());
        return;
    }

    res = SendStatusToSCM(SERVICE_START_PENDING, NO_ERROR, 0 , 1, 5000);
    if (!res) {
        terminate(GetLastError());
        return;
    }

    m_pDev = new CDevice();
    if (!m_pDev || !m_pDev->Init(m_StatusHandle) || !m_pDev->Start()) {
        terminate(GetLastError());
        return;
    }

    DEV_BROADCAST_DEVICEINTERFACE filter;

    ZeroMemory(&filter, sizeof(filter));
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = GUID_DEVINTERFACE_BALLOON;

    m_hDevNotify = RegisterDeviceNotification(m_StatusHandle, &filter,
        DEVICE_NOTIFY_SERVICE_HANDLE);

    if (!m_hDevNotify) {
        terminate(GetLastError());
        return;
    }

    m_evTerminate = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_evTerminate) {
        terminate(GetLastError());
        return;
    }

    res = InitService();
    if (!res) {
        terminate(GetLastError());
        return;
    }

    res = SendStatusToSCM(SERVICE_RUNNING, NO_ERROR, 0 , 0, 0);
    if (!res) {
        terminate(GetLastError());
        return;
    }

    WaitForSingleObject(m_evTerminate, INFINITE);
    terminate(0);
}


void CService::GetStatus(SC_HANDLE service)
{
    SERVICE_STATUS status;
    DWORD CurrentState;

    QueryServiceStatus(service, &status);

    switch(status.dwCurrentState) {
        case SERVICE_RUNNING:
            CurrentState = SERVICE_RUNNING;
            printf("Service RUNNING.\n");
            break;
        case SERVICE_STOPPED:
            CurrentState = SERVICE_STOPPED;
            printf("Service STOPPED.\n");
            break;
        case SERVICE_CONTINUE_PENDING:
            CurrentState = SERVICE_CONTINUE_PENDING;
            printf("Service is resuming...\n");
            break;
        case SERVICE_START_PENDING:
            CurrentState = SERVICE_START_PENDING;
            printf("Service is starting...\n");
            break;
        case SERVICE_STOP_PENDING:
            CurrentState = SERVICE_STOP_PENDING;
            printf("Service is stopping...\n");
            break;
        default:
            break;
    }
    SendStatusToSCM(CurrentState, NO_ERROR, 0, 0, 0);
}
