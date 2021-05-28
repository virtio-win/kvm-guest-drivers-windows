#include "pch.h"
#include "strsafe.h"

extern CService srvc;

void ShowUsage()
{
    wprintf(L"\n");
    wprintf(L"USAGE:\n");
    wprintf(L"vgpusvr -i\tInstall service\n");
    wprintf(L"vgpusvr -u\tUninstall service\n");
    wprintf(L"vgpusvr -r\tRun service\n");
    wprintf(L"vgpusvr -s\tStop service\n");
    wprintf(L"vgpusvr status\tCurrent status\n");
    wprintf(L"\n");
}

BOOL InstallService()
{
    SC_HANDLE newService;
    SC_HANDLE scm;
    TCHAR szBuffer[255];
    TCHAR szPath[MAX_PATH];

    GetModuleFileName( GetModuleHandle(NULL), szPath, MAX_PATH );
    PrintMessage(L"Nodule Name %ws\n", szPath);
    if (FAILED( StringCchCopy(szBuffer, 255, TEXT("\"")))) {
        PrintMessage(L"szBuffer %ws\n", szBuffer);
        return FALSE;
    }
    if (FAILED( StringCchCat(szBuffer, 255, szPath))) {
        PrintMessage(L"szBuffer %ws\n", szBuffer);
        return FALSE;
    }
    if (FAILED( StringCchCat(szBuffer, 255, TEXT("\"")))) {
        PrintMessage(L"szBuffer %ws\n", szBuffer);
        return FALSE;
    }

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (scm == NULL) {
        ErrorHandler("OpenSCManager", GetLastError());
    }
    newService = CreateService(
                             scm,
                             ServiceName,
                             DisplayName,
                             SERVICE_ALL_ACCESS,
                             SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS,
                             SERVICE_AUTO_START,
                             SERVICE_ERROR_NORMAL,
                             szBuffer,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL
                             );
    if (!newService) {
        ErrorHandler("CreateService", GetLastError());
        return FALSE;
    } else {
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
    if (!scm) {
        ErrorHandler("OpenSCManager", GetLastError());
    }

    service = OpenService(scm, ServiceName, SERVICE_ALL_ACCESS | DELETE);
    if (!service) {
        ErrorHandler("OpenService", GetLastError());
    }

    res = QueryServiceStatus(service, &status);
    if (!res) {
        ErrorHandler("QueryServiceStatus", GetLastError());
    }

    if (status.dwCurrentState != SERVICE_STOPPED) {
        printf("Stopping service...\n");
        res = ControlService(service, SERVICE_CONTROL_STOP, &status);
        if (!res) {
            ErrorHandler("ControlService", GetLastError());
        }
        Sleep(5000);
    }

    res = DeleteService(service);
    if (res) {
        printf("Service Uninstalled\n");
    } else {
        ErrorHandler("DeleteService", GetLastError());
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

    PrintMessage(L"%ws\n", __FUNCTIONW__);

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        ErrorHandler("OpenSCManager", GetLastError());
    }

    Service = OpenService(scm, ServiceName, SERVICE_ALL_ACCESS);
    if (!Service) {
        ErrorHandler("OpenService", GetLastError());
        return FALSE;
    } else {
        StartService(Service, 0, NULL);
        srvc.GetStatus(Service);

        if (!QueryServiceStatus( Service, &ssStatus) ) {
            ErrorHandler("QueryServiceStatus", GetLastError());
        }
        dwStartTickCount = GetTickCount();
        dwOldCheckPoint = ssStatus.dwCheckPoint;

        while (ssStatus.dwCurrentState == SERVICE_START_PENDING) {
            dwWaitTime = ssStatus.dwWaitHint / 10;

            if( dwWaitTime < 1000 ) {
                dwWaitTime = 1000;
            } else if ( dwWaitTime > 10000 ) {
                dwWaitTime = 10000;
            }

            Sleep( dwWaitTime );

            if (!QueryServiceStatus(Service, &ssStatus) ) {
                ErrorHandler("QueryServiceStatus", GetLastError());
                break;
            }

            if ( ssStatus.dwCheckPoint > dwOldCheckPoint ) {
                dwStartTickCount = GetTickCount();
                dwOldCheckPoint = ssStatus.dwCheckPoint;
            } else {
                if(GetTickCount()-dwStartTickCount > ssStatus.dwWaitHint) {
                    break;
                }
            }
        }

        if (ssStatus.dwCurrentState == SERVICE_RUNNING) {
            srvc.GetStatus(Service);
            dwStatus = NO_ERROR;
        } else {
            printf("\nService not started.\n");
            printf("  Current State: %d\n", ssStatus.dwCurrentState);
            printf("  Exit Code: %d\n", ssStatus.dwWin32ExitCode);
            printf("  Service Specific Exit Code: %d\n", ssStatus.dwServiceSpecificExitCode);
            printf("  Check Point: %d\n", ssStatus.dwCheckPoint);
            printf("  Wait Hint: %d\n", ssStatus.dwWaitHint);
            dwStatus = GetLastError();
            ErrorHandler("<--> ServiceRun", dwStatus);
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

    PrintMessage(L"%ws\n", __FUNCTIONW__);

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        ErrorHandler("OpenSCManager", GetLastError());
    }

    service = OpenService(scm, ServiceName, SERVICE_ALL_ACCESS);
    if (!service) {
        ErrorHandler("OpenService", GetLastError());
    }

    if (ctrl == SERVICE_CONTROL_STOP) {
        printf("Service is stopping...\n");
        res = ControlService(service, SERVICE_CONTROL_STOP, &status);
    }

    if (!res) {
        ErrorHandler("ControlService", GetLastError());
    } else {
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

    PrintMessage(L"%ws\n", __FUNCTIONW__);

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        ErrorHandler("OpenSCManager", GetLastError());
    }

    service = OpenService(scm, ServiceName, SERVICE_QUERY_CONFIG);
    if (!service) {
        ErrorHandler("OpenService", GetLastError());
    }

    buffer = (LPQUERY_SERVICE_CONFIG)LocalAlloc(LPTR, 4096);
    if (!buffer) {
        ErrorHandler("LocalAlloc", GetLastError());
    }

    res = QueryServiceConfig(service, buffer, 4096, &sizeNeeded);
    if (!res) {
        ErrorHandler("QueryServiceConfig", GetLastError());
    }

    printf("Service name:\t%S\n", buffer->lpDisplayName);
    printf("Service type:\t%d\n", buffer->dwServiceType);
    printf("Start type:\t%d\n",buffer->dwStartType);
    printf("Start name:\t%S\n",buffer->lpServiceStartName);
    printf("Path:\t\t%S\n",buffer->lpBinaryPathName);

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

    PrintMessage(L"%ws\n", __FUNCTIONW__);

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS | GENERIC_WRITE);
    if (!scm) {
        ErrorHandler("OpenSCManager", GetLastError());
    }
    lock = LockServiceDatabase(scm);
    if (lock == 0) {
        ErrorHandler("LockServiceDatabase", GetLastError());
    }
    service = OpenService(scm, ServiceName, SERVICE_ALL_ACCESS);
    if (!service) {
        ErrorHandler("OpenService", GetLastError());
    }
    res = ChangeServiceConfig(
                             service,
                             SERVICE_NO_CHANGE,
                             SERVICE_NO_CHANGE,
                             SERVICE_NO_CHANGE,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL
                             );
    if (!res) {
        UnlockServiceDatabase(lock);
        ErrorHandler("ChangeServiceConfig", GetLastError());
    }

    res = UnlockServiceDatabase(lock);
    if (!res) {
        ErrorHandler("UnlockServiceDatabase", GetLastError());
    }
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    return TRUE;
}
