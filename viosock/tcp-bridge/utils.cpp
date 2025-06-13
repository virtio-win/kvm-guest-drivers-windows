#include "stdafx.h"

#if defined(EVENT_TRACING)
#include "utils.tmh"
#endif

extern LPWSTR ServiceName;
extern LPWSTR DisplayName;

extern CService srvc;

void ErrorExit(const char *s, int err)
{
    LPSTR lpMsgBuf;
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL,
                       err,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       (LPSTR)&lpMsgBuf,
                       0,
                       NULL) > 0)
    {
        TraceEvents(TRACE_LEVEL_FATAL, DBG_INIT, "%s failed. Error %d (%s)\n", s, err, lpMsgBuf);
        LocalFree(lpMsgBuf);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_FATAL, DBG_INIT, "%s failed. Error %d (unknown error)\n", s, err);
    }

    WPP_CLEANUP();
    ExitProcess(err);
}

void ShowUsage()
{
    printf("\n");
    printf("USAGE:\n");
    printf("vstbridge -i\tInstall service\n");
    printf("vstbridge -u\tUninstall service\n");
    printf("vstbridge -r\tRun service\n");
    printf("vstbridge -s\tStop service\n");
    printf("vstbridge status\tCurrent status\n");
    printf("\n");
}

BOOL InstallService()
{
    SC_HANDLE newService;
    SC_HANDLE scm;
    TCHAR szBuffer[255];
    TCHAR szDependencies[255] = {0};
    TCHAR szPath[MAX_PATH];

    GetModuleFileName(GetModuleHandle(NULL), szPath, MAX_PATH);
    if (FAILED(StringCchCopy(szBuffer, 255, TEXT("\""))))
    {
        return FALSE;
    }
    if (FAILED(StringCchCat(szBuffer, 255, szPath)))
    {
        return FALSE;
    }
    if (FAILED(StringCchCat(szBuffer, 255, TEXT("\""))))
    {
        return FALSE;
    }

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (scm == NULL)
    {
        ErrorExit("OpenSCManager", GetLastError());
    }
    newService = CreateService(scm,
                               ServiceName,
                               DisplayName,
                               SERVICE_ALL_ACCESS,
                               SERVICE_WIN32_OWN_PROCESS,
                               SERVICE_AUTO_START,
                               SERVICE_ERROR_NORMAL,
                               szBuffer,
                               NULL,
                               NULL,
                               szDependencies,
                               NULL,
                               NULL);
    if (!newService)
    {
        ErrorExit("CreateService", GetLastError());
        return FALSE;
    }
    else
    {
        printf("Service Installed\n");
        ServiceRun();
    }

    CloseServiceHandle(newService);
    CloseServiceHandle(scm);

    return TRUE;
}

BOOL UninstallService()
{
    SC_HANDLE service;
    SC_HANDLE scm;
    BOOL res;
    SERVICE_STATUS status;

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        ErrorExit("OpenSCManager", GetLastError());
    }

    service = OpenService(scm, ServiceName, SERVICE_ALL_ACCESS | DELETE);
    if (!service)
    {
        ErrorExit("OpenService", GetLastError());
    }

    res = QueryServiceStatus(service, &status);
    if (!res)
    {
        ErrorExit("QueryServiceStatus", GetLastError());
    }

    if (status.dwCurrentState != SERVICE_STOPPED)
    {
        printf("Stopping service...\n");
        res = ControlService(service, SERVICE_CONTROL_STOP, &status);
        if (!res)
        {
            ErrorExit("ControlService", GetLastError());
        }
        Sleep(5000);
    }

    res = DeleteService(service);
    if (res)
    {
        printf("Service Uninstalled\n");
    }
    else
    {
        ErrorExit("DeleteService", GetLastError());
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    return TRUE;
}

BOOL ServiceRun()
{
    SC_HANDLE scm, Service;
    SERVICE_STATUS ssStatus;
    DWORD dwOldCheckPoint;
    DWORD dwStartTickCount;
    DWORD dwWaitTime;
    DWORD dwStatus;

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        ErrorExit("OpenSCManager", GetLastError());
    }

    Service = OpenService(scm, ServiceName, SERVICE_ALL_ACCESS);
    if (!Service)
    {
        ErrorExit("OpenService", GetLastError());
        return FALSE;
    }
    else
    {
        StartService(Service, 0, NULL);
        srvc.GetStatus(Service);

        if (!QueryServiceStatus(Service, &ssStatus))
        {
            ErrorExit("QueryServiceStatus", GetLastError());
        }
        dwStartTickCount = GetTickCount();
        dwOldCheckPoint = ssStatus.dwCheckPoint;

        while (ssStatus.dwCurrentState == SERVICE_START_PENDING)
        {
            dwWaitTime = ssStatus.dwWaitHint / 10;

            if (dwWaitTime < 1000)
            {
                dwWaitTime = 1000;
            }
            else if (dwWaitTime > 10000)
            {
                dwWaitTime = 10000;
            }

            Sleep(dwWaitTime);

            if (!QueryServiceStatus(Service, &ssStatus))
            {
                break;
            }

            if (ssStatus.dwCheckPoint > dwOldCheckPoint)
            {
                dwStartTickCount = GetTickCount();
                dwOldCheckPoint = ssStatus.dwCheckPoint;
            }
            else
            {
                if (GetTickCount() - dwStartTickCount > ssStatus.dwWaitHint)
                {
                    break;
                }
            }
        }

        if (ssStatus.dwCurrentState == SERVICE_RUNNING)
        {
            srvc.GetStatus(Service);
            dwStatus = NO_ERROR;
        }
        else
        {

            printf("\nService not started.\n");
            printf("  Current State: %d\n", ssStatus.dwCurrentState);
            printf("  Exit Code: %d\n", ssStatus.dwWin32ExitCode);
            printf("  Service Specific Exit Code: %d\n", ssStatus.dwServiceSpecificExitCode);
            printf("  Check Point: %d\n", ssStatus.dwCheckPoint);
            printf("  Wait Hint: %d\n", ssStatus.dwWaitHint);
            dwStatus = GetLastError();
        }
    }

    CloseServiceHandle(scm);
    CloseServiceHandle(Service);
    return TRUE;
}

BOOL ServiceControl(int ctrl)
{
    SC_HANDLE service;
    SC_HANDLE scm;
    BOOL res = TRUE;
    SERVICE_STATUS status;

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        ErrorExit("OpenSCManager", GetLastError());
    }

    service = OpenService(scm, ServiceName, SERVICE_ALL_ACCESS);
    if (!service)
    {
        ErrorExit("OpenService", GetLastError());
    }

    if (ctrl == SERVICE_CONTROL_STOP)
    {
        printf("Service is stopping...\n");
        res = ControlService(service, SERVICE_CONTROL_STOP, &status);
    }

    if (!res)
    {
        ErrorExit("ControlService", GetLastError());
    }
    else
    {
        srvc.GetStatus(service);
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    return TRUE;
}

BOOL GetConfiguration()
{
    SC_HANDLE service;
    SC_HANDLE scm;
    BOOL res;
    LPQUERY_SERVICE_CONFIG buffer;
    DWORD sizeNeeded;

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        ErrorExit("OpenSCManager", GetLastError());
    }

    service = OpenService(scm, ServiceName, SERVICE_QUERY_CONFIG);
    if (!service)
    {
        ErrorExit("OpenService", GetLastError());
    }

    buffer = (LPQUERY_SERVICE_CONFIG)LocalAlloc(LPTR, 4096);
    if (!buffer)
    {
        ErrorExit("LocalAlloc", GetLastError());
    }
    res = QueryServiceConfig(service, buffer, 4096, &sizeNeeded);
    if (!res)
    {
        ErrorExit("QueryServiceConfig", GetLastError());
    }

    printf("Service name:\t%S\n", buffer->lpDisplayName);
    printf("Service type:\t%d\n", buffer->dwServiceType);
    printf("Start type:\t%d\n", buffer->dwStartType);
    printf("Start name:\t%S\n", buffer->lpServiceStartName);
    printf("Path:\t\t%S\n", buffer->lpBinaryPathName);

    LocalFree(buffer);

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return TRUE;
}

BOOL ChangeConfig()
{
    SC_HANDLE service;
    SC_HANDLE scm;
    BOOL res;
    SC_LOCK lock;

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS | GENERIC_WRITE);
    if (!scm)
    {
        ErrorExit("OpenSCManager", GetLastError());
    }
    lock = LockServiceDatabase(scm);
    if (lock == 0)
    {
        ErrorExit("LockServiceDatabase", GetLastError());
    }
    service = OpenService(scm, ServiceName, SERVICE_ALL_ACCESS);
    if (!service)
    {
        ErrorExit("OpenService", GetLastError());
    }
    res = ChangeServiceConfig(service,
                              SERVICE_NO_CHANGE,
                              SERVICE_NO_CHANGE,
                              SERVICE_NO_CHANGE,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              NULL);
    if (!res)
    {
        UnlockServiceDatabase(lock);
        ErrorExit("ChangeServiceConfig", GetLastError());
    }

    res = UnlockServiceDatabase(lock);
    if (!res)
    {
        ErrorExit("UnlockServiceDatabase", GetLastError());
    }
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return TRUE;
}
