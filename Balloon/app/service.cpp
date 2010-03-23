#include "service.h"

CService::CService()
{
    m_evTerminate = NULL;
    m_thHandle = NULL;
    m_bPauseService = FALSE;
    m_bRunningService = FALSE;
    m_StatusHandle = NULL;
    m_pMemStat = NULL;
    m_pDev = NULL;
}

CService::~CService()
{
    m_evTerminate = NULL;
    m_thHandle = NULL;
    m_bPauseService = FALSE;
    m_bRunningService = FALSE;
    m_StatusHandle = NULL;
    m_pMemStat = NULL;
    m_pDev = NULL;
}

void __stdcall CService::HandlerThunk(CService* service, DWORD ctlcode)
{
    service->ServiceCtrlHandler(ctlcode);
}

void __stdcall CService::ServiceMainThunk(CService* service, DWORD argc, TCHAR* argv[])
{
    service->ServiceMain(argc, argv);
}

BOOL CService::InitService()
{
    DWORD id;

    m_thHandle = CreateThread(
                              NULL,
                              0,
                              (LPTHREAD_START_ROUTINE) ServiceThread,
                              (LPVOID)this,
                              0,
                              &id);

    if (m_thHandle == NULL) {
        return FALSE;
    }
    m_bRunningService = TRUE;
    return TRUE;
}

DWORD WINAPI CService::ServiceThread(LPDWORD lParam)
{
    CService* service = (CService*)lParam;
    service->Run(); 
    return 0;
}

void CService::Run()
{
    HANDLE ev = CreateEvent(NULL, TRUE, FALSE, NULL);
    BALLOON_STAT stat[VIRTIO_BALLOON_S_NR];
    DWORD res;
    while (1) {
        if (m_pMemStat->GetStatus(stat)) {
           if (!m_pDev->Write(stat, VIRTIO_BALLOON_S_NR, ev) &&
              (GetLastError() == ERROR_IO_PENDING)) {
              res = WaitForSingleObject(ev, 1000);
           }
        }
    }
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
                SERVICE_ACCEPT_PAUSE_CONTINUE |
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

void CService::ResumeService()
{
    m_bPauseService = FALSE;
    ResumeThread(m_thHandle);
}

void CService::PauseService()
{
    m_bPauseService = TRUE;
    SuspendThread(m_thHandle);
}

void CService::StopService()
{
    m_bRunningService = FALSE;
    SetEvent(m_evTerminate);
}

void CService::terminate(DWORD error)
{
    if (m_evTerminate) {
        CloseHandle(m_evTerminate);
    }

    if (m_StatusHandle) {
        SendStatusToSCM(SERVICE_STOPPED, error, 0, 0, 0);
    }

    if (m_thHandle) {
        CloseHandle(m_thHandle);
    }

    delete m_pMemStat;
    delete m_pDev;
}


void CService::ServiceCtrlHandler(DWORD controlCode)
{
    DWORD currentState = 0;

    switch(controlCode)
    {
        case SERVICE_CONTROL_STOP:
            currentState = SERVICE_STOP_PENDING;
            SendStatusToSCM(SERVICE_STOP_PENDING,
                            NO_ERROR,
                            0,
                            1,
                            5000);
            StopService();
            return;

        case SERVICE_CONTROL_PAUSE:
            if (m_bRunningService && !m_bPauseService) {
                SendStatusToSCM(SERVICE_PAUSE_PENDING,
                                NO_ERROR,
                                0,
                                1,
                                1000);
				
                PauseService();
                currentState = SERVICE_PAUSED;
            }
            break;
			
        case SERVICE_CONTROL_CONTINUE:
            if (m_bRunningService && m_bPauseService) {
                SendStatusToSCM(SERVICE_CONTINUE_PENDING,
                                NO_ERROR,
                                0,
                                1,
                                1000);
				
                ResumeService();
                currentState = SERVICE_RUNNING;
            }
            break;

        case SERVICE_CONTROL_INTERROGATE:
            break;
			
        case SERVICE_CONTROL_SHUTDOWN:
            return;

        default:
            break;
    }
    SendStatusToSCM(currentState, NO_ERROR, 0, 0, 0);
}

void CService::ServiceMain(DWORD argc, LPTSTR *argv)
{
    BOOL res;

    if (!m_StatusHandle) {
        terminate(GetLastError());
        return;
    }

    m_pMemStat = new CMemStat;
    if(!m_pMemStat || !m_pMemStat->Init()) {
        terminate(GetLastError());
        return;
    }

    m_pDev = new CDevice;
    if(!m_pDev || !m_pDev->Init()) {
        terminate(GetLastError());
        return;
    }

    res = SendStatusToSCM(SERVICE_START_PENDING, NO_ERROR, 0 , 1, 5000);
    if (!res) { 
        terminate(GetLastError());
        return;
    }

    m_evTerminate = CreateEvent (0, TRUE, FALSE, 0);
    if (!m_evTerminate) {
        terminate(GetLastError());
        return;
    }

    res = SendStatusToSCM(SERVICE_START_PENDING, NO_ERROR, 0 , 2, 1000);
    if (!res) { 
        terminate(GetLastError());
        return;
    }

    res = SendStatusToSCM(SERVICE_START_PENDING, NO_ERROR, 0 , 3, 5000);
    if (!res) { 
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
        case SERVICE_PAUSED:
            CurrentState = SERVICE_PAUSED;
            printf("Service PAUSED.\n");
            break;
        case SERVICE_CONTINUE_PENDING:
            CurrentState = SERVICE_CONTINUE_PENDING;
            printf("Service is resuming...\n");
            break;
        case SERVICE_PAUSE_PENDING:
            CurrentState = SERVICE_PAUSE_PENDING;
            printf("Service is pausing...\n");
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
