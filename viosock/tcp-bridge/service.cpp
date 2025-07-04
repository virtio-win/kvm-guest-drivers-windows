#include "stdafx.h"

#if defined(EVENT_TRACING)
#include "service.tmh"
#endif

CService::CService()
{
    m_hDevNotify = NULL;
    m_evTerminate = NULL;
    m_bRunningService = FALSE;
    m_StatusHandle = NULL;
    m_pBridge = NULL;
    m_Status = SERVICE_STOPPED;
}

CService::~CService()
{
    m_evTerminate = NULL;
    m_bRunningService = FALSE;
    m_StatusHandle = NULL;
    m_pBridge = NULL;
    m_Status = SERVICE_STOPPED;
}

DWORD __stdcall CService::HandlerExThunk(CService *service, DWORD ctlcode, DWORD evtype, PVOID evdata)
{
    switch (ctlcode)
    {
        case SERVICE_CONTROL_DEVICEEVENT:
        case SERVICE_CONTROL_HARDWAREPROFILECHANGE:
            return service->ServiceHandleDeviceChange(evtype);

        case SERVICE_CONTROL_POWEREVENT:
            return service->ServiceHandlePowerEvent(evtype, (DWORD)((DWORD_PTR)evdata));

        default:
            service->ServiceCtrlHandler(ctlcode);
            return NO_ERROR;
    }
}

void __stdcall CService::ServiceMainThunk(CService *service, DWORD argc, TCHAR *argv[])
{
    service->ServiceMain(argc, argv);
}

BOOL CService::InitService()
{
    m_bRunningService = TRUE;
    m_Status = SERVICE_RUNNING;
    return TRUE;
}

BOOL CService::SendStatusToSCM(DWORD dwCurrentState,
                               DWORD dwWin32ExitCode,
                               DWORD dwServiceSpecificExitCode,
                               DWORD dwCheckPoint,
                               DWORD dwWaitHint)
{
    BOOL res;
    SERVICE_STATUS serviceStatus;

    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
    serviceStatus.dwCurrentState = dwCurrentState;

    if (dwCurrentState == SERVICE_START_PENDING)
    {
        serviceStatus.dwControlsAccepted = 0;
    }
    else
    {
        serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    if (dwServiceSpecificExitCode == 0)
    {
        serviceStatus.dwWin32ExitCode = dwWin32ExitCode;
    }
    else
    {
        serviceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
    }

    serviceStatus.dwServiceSpecificExitCode = dwServiceSpecificExitCode;
    serviceStatus.dwCheckPoint = dwCheckPoint;
    serviceStatus.dwWaitHint = dwWaitHint;

    res = SetServiceStatus(m_StatusHandle, &serviceStatus);
    if (!res)
    {
        StopService();
    }

    return res;
}

void CService::StopService()
{
    if (m_bRunningService && m_pBridge)
    {
        m_pBridge->Stop();
        m_bRunningService = FALSE;
        m_Status = SERVICE_STOPPED;
    }
    SetEvent(m_evTerminate);
}

void CService::terminate(DWORD error)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s error = %d\n", __FUNCTION__, error);
    UnregisterNotification(m_hDevNotify);

    if (m_evTerminate)
    {
        CloseHandle(m_evTerminate);
        m_evTerminate = NULL;
    }

    if (m_StatusHandle)
    {
        SendStatusToSCM(SERVICE_STOPPED, error, 0, 0, 0);
    }

    delete m_pBridge;
}

void CService::ServiceCtrlHandler(DWORD controlCode)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s controlCode = %d\n", __FUNCTION__, controlCode);

    switch (controlCode)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            m_Status = SERVICE_STOP_PENDING;
            SendStatusToSCM(m_Status, NO_ERROR, 0, 1, 5000);
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
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s evtype = %d\n", __FUNCTION__, evtype);

    switch (evtype)
    {
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEQUERYREMOVEFAILED:
            m_pBridge->Start();
            break;

        case DBT_DEVICEQUERYREMOVE:
        case DBT_DEVICEREMOVECOMPLETE:
            m_pBridge->Stop();
            break;

        default:
            break;
    }

    return NO_ERROR;
}

DWORD CService::ServiceHandlePowerEvent(DWORD evtype, DWORD flags)
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s evtype = %d, flags = %d\n", __FUNCTION__, evtype, flags);
    return NO_ERROR;
}

void CService::ServiceMain(DWORD argc, LPTSTR *argv)
{
    BOOL res;

    if (!m_StatusHandle)
    {
        terminate(GetLastError());
        return;
    }

    res = SendStatusToSCM(SERVICE_START_PENDING, NO_ERROR, 0, 1, 5000);
    if (!res)
    {
        terminate(GetLastError());
        return;
    }

    m_pBridge = new CBridge();
    // Lets hardcode port 22 for now
    if (!m_pBridge || !m_pBridge->Init(this, std::make_pair(22, 22)) || !m_pBridge->Start())
    {
        terminate(GetLastError());
        return;
    }

    m_hDevNotify = RegisterDeviceInterfaceNotification();
    if (m_hDevNotify == NULL)
    {
        terminate(GetLastError());
        return;
    }

    m_evTerminate = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_evTerminate)
    {
        terminate(GetLastError());
        return;
    }

    res = InitService();
    if (!res)
    {
        terminate(GetLastError());
        return;
    }

    res = SendStatusToSCM(SERVICE_RUNNING, NO_ERROR, 0, 0, 0);
    if (!res)
    {
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

    if (!QueryServiceStatus(service, &status))
    {
        printf("Failed to get service status.\n");
        return;
    }

    switch (status.dwCurrentState)
    {
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

DWORD WINAPI CService::DeviceNotificationCallback(HCMNOTIFICATION Notify,
                                                  PVOID Context,
                                                  CM_NOTIFY_ACTION Action,
                                                  PCM_NOTIFY_EVENT_DATA EventData,
                                                  DWORD EventDataSize)
{
    CService *pThis = reinterpret_cast<CService *>(Context);
    DWORD event = 0;

    switch (Action)
    {
        case CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL:
            event = DBT_DEVICEARRIVAL;
            break;

        case CM_NOTIFY_ACTION_DEVICEQUERYREMOVE:
            event = DBT_DEVICEQUERYREMOVE;
            break;

        case CM_NOTIFY_ACTION_DEVICEQUERYREMOVEFAILED:
            event = DBT_DEVICEQUERYREMOVEFAILED;
            break;

        case CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE:
            event = DBT_DEVICEREMOVECOMPLETE;
            break;

        default:
            break;
    }

    if (event > 0)
    {
        pThis->ServiceHandleDeviceChange(event);
    }

    return ERROR_SUCCESS;
}

VOID WINAPI UnregisterNotificationWork(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work)
{
    HCMNOTIFICATION Handle = static_cast<HCMNOTIFICATION>(Context);

    CM_Unregister_Notification(Handle);
    CloseThreadpoolWork(Work);
}

HCMNOTIFICATION CService::RegisterDeviceInterfaceNotification()
{
    HCMNOTIFICATION handle = NULL;

    CM_NOTIFY_FILTER filter;
    CONFIGRET cr;

    ::ZeroMemory(&filter, sizeof(filter));
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_VIOSOCK;

    cr = CM_Register_Notification(&filter, this, CService::DeviceNotificationCallback, &handle);

    if (cr != CR_SUCCESS)
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_PNP,
                    "CM_Register_Notification for interface failed, error = %d\n",
                    (DWORD)cr);

        SetLastError(CM_MapCrToWin32Err(cr, ERROR_NOT_SUPPORTED));
    }

    return handle;
}

HCMNOTIFICATION CService::RegisterDeviceHandleNotification(HANDLE DeviceHandle)
{
    HCMNOTIFICATION handle = NULL;

    CM_NOTIFY_FILTER filter;
    CONFIGRET cr;

    ::ZeroMemory(&filter, sizeof(filter));
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
    filter.u.DeviceHandle.hTarget = DeviceHandle;

    cr = CM_Register_Notification(&filter, this, CService::DeviceNotificationCallback, &handle);

    if (cr != CR_SUCCESS)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "CM_Register_Notification for device failed, error = %d\n", (DWORD)cr);

        SetLastError(CM_MapCrToWin32Err(cr, ERROR_NOT_SUPPORTED));
    }

    return handle;
}

BOOL CService::UnregisterNotification(HCMNOTIFICATION Handle)
{
    BOOL ret = FALSE;

    if (Handle)
    {
        PTP_WORK work;

        work = CreateThreadpoolWork(UnregisterNotificationWork, Handle, NULL);
        if (work != NULL)
        {
            SubmitThreadpoolWork(work);
        }

        ret = TRUE;
    }

    return ret;
}
