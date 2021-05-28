#include "pch.h"
#include "Service.h"
#include <stdio.h>
#include "SessionMgr.h"

CService::CService()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    m_evTerminate = NULL;
    m_bRunningService = FALSE;
    m_StatusHandle = NULL;
    m_Status = SERVICE_STOPPED;
    m_SessionMgr = NULL;
}

CService::~CService()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    m_evTerminate = NULL;
    m_bRunningService = FALSE;
    m_StatusHandle = NULL;
    m_Status = SERVICE_STOPPED;
    delete m_SessionMgr;
    m_SessionMgr = NULL;
}

DWORD __stdcall CService::HandlerExThunk(CService* pService, DWORD ctlcode, DWORD evtype, PVOID evdata)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    switch (ctlcode) {

    case SERVICE_CONTROL_SESSIONCHANGE:
        return pService->ServiceControlSessionChange(evtype, evdata);

    case SERVICE_CONTROL_DEVICEEVENT:
    case SERVICE_CONTROL_HARDWAREPROFILECHANGE:
        return pService->ServiceHandleDeviceChange(evtype);

    case SERVICE_CONTROL_POWEREVENT:
        return pService->ServiceHandlePowerEvent(evtype, (DWORD)((DWORD_PTR) evdata));

    default:
        pService->ServiceCtrlHandler(ctlcode);
    }
    return NO_ERROR;
}

void __stdcall CService::ServiceMainThunk(CService* service, DWORD argc, TCHAR* argv[])
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    service->ServiceMain(argc, argv);
}

BOOL CService::InitService()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    m_bRunningService = TRUE;
    m_Status = SERVICE_RUNNING;
    return TRUE;
}

BOOL CService::SendStatusToSCM(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint)
{
    BOOL res;
    SERVICE_STATUS serviceStatus;

    PrintMessage(L"%ws\n", __FUNCTIONW__);

    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
    serviceStatus.dwCurrentState = dwCurrentState;

    if (dwCurrentState == SERVICE_START_PENDING) {
        serviceStatus.dwControlsAccepted = 0;
    } else {
        serviceStatus.dwControlsAccepted =
                SERVICE_ACCEPT_STOP |
                SERVICE_ACCEPT_SHUTDOWN |
                SERVICE_ACCEPT_SESSIONCHANGE;
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
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_bRunningService) {
        m_bRunningService = FALSE;
        m_Status = SERVICE_STOPPED;
    }
    SetEvent(m_evTerminate);
}

void CService::terminate(DWORD error)
{
    PrintMessage(L"%ws = %d\n", __FUNCTIONW__, error);

    if (m_evTerminate) {
        CloseHandle(m_evTerminate);
        m_evTerminate = NULL;
    }

    if (m_StatusHandle) {
        SendStatusToSCM(SERVICE_STOPPED, error, 0, 0, 0);
    }

    m_SessionMgr->Close();
}

void CService::ServiceCtrlHandler(DWORD controlCode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

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

DWORD CService::ServiceHandleDeviceChange(DWORD evtype)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    return NO_ERROR;
}

DWORD CService::ServiceHandlePowerEvent(DWORD evtype, DWORD flags)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    return NO_ERROR;
}

DWORD CService::ServiceControlSessionChange(DWORD evtype, PVOID flags)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_SessionMgr) {
        return m_SessionMgr->SessionChange(evtype, flags);
    }

    return NO_ERROR;
}

void CService::ServiceMain(DWORD argc, LPTSTR *argv)
{
    BOOL res;

    PrintMessage(L"%ws built on %ws %ws\n", __FUNCTIONW__, _CRT_WIDE(__DATE__) , _CRT_WIDE(__TIME__));

    if (!m_StatusHandle) {
        PrintMessage(L"-->ServiceMain m_StatusHandle\n");
        terminate(GetLastError());
        return;
    }

    res = SendStatusToSCM(SERVICE_START_PENDING, NO_ERROR, 0 , 1, 5000);
    if (!res) {
        PrintMessage(L"-->ServiceMain res\n");
        terminate(GetLastError());
        return;
    }

    m_evTerminate = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_evTerminate) {
        PrintMessage(L"-->ServiceMain m_evTerminate\n");
        terminate(GetLastError());
        return;
    }

    m_SessionMgr = new CSessionMgr();
    if (!m_SessionMgr || !m_SessionMgr->Init()) {
        PrintMessage(L"-->ServiceMain m_SessionMgr Init\n");
        terminate(GetLastError());
        return;
    }

    res = InitService();
    if (!res) {
        PrintMessage(L"-->ServiceMain InitService\n");
        terminate(GetLastError());
        return;
    }

    res = SendStatusToSCM(SERVICE_RUNNING, NO_ERROR, 0 , 0, 0);
    if (!res) {
        PrintMessage(L"-->ServiceMain SendStatusToSCM\n");
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

    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (!QueryServiceStatus(service, &status)) {
        printf("Failed to get service status.\n");
        return;
    }

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
            return;
    }
    SendStatusToSCM(CurrentState, NO_ERROR, 0, 0, 0);
}
