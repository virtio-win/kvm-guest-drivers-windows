#include "utils.h"
#include "service.h"
#include "strsafe.h"

extern LPWSTR ServiceName;
extern CService srvc;

static struct ErrEntry {
    int code;
    const char* msg;
} ErrList[] = {
    { 0,    "No error" },
    { 1055, "The service database is locked." },
    { 1056, "An instance of the service is already running." },
    { 1060, "The service does not exist as an installed service." },
    { 1061, "The service cannot accept control messages at this time." },
    { 1062, "The service has not been started." },
    { 1063, "The service process could not connect to the service controller." },
    { 1064, "An exception occurred in the service when handling the control request." },
    { 1065, "The database specified does not exist." },
    { 1066, "The service has returned a service-specific error code." },
    { 1067, "The process terminated unexpectedly." },
    { 1068, "The dependency service or group failed to start." },
    { 1069, "The service did not start due to a logon failure." },
    { 1070, "After starting, the service hung in a start-pending state." },
    { 1071, "The specified service database lock is invalid." },
    { 1072, "The service marked for deletion." },
    { 1073, "The service already exists." },
    { 1078, "The name is already in use as either a service name or a service display name." },
};

const int nErrList = sizeof(ErrList) / sizeof(ErrEntry);

void ErrorHandler(char *s, int err)
{
    printf("Failed. Error %d ", err );
    int i;
    for (i = 0; i < nErrList; ++i) {
        if (ErrList[i].code == err) {
            printf("%s\n", ErrList[i].msg);
            break;
        }
    }
    if (i == nErrList) {
        printf("unknown error\n");
    }

    FILE* pLog = fopen("libtestsvc.log","a");
    fprintf(pLog, "%s failed, error code = %d\n",s , err);
    fclose(pLog);

    ExitProcess(err);
}

void PrintMessage(char *s)
{
#ifdef DBG
    FILE* pLog = fopen("libtestsvc.log", "a");
    fprintf(pLog, "%s\n", s);
    fclose(pLog);
#endif
}

void ShowUsage()
{
    printf("\n");
    printf("USAGE:\n");
    printf("blnsvr -i\tInstall service\n");
    printf("blnsvr -u\tUninstall service\n");
    printf("blnsvr -r\tRun service\n");
    printf("blnsvr -s\tStop service\n");
    printf("blnsvr -p\tPause service\n");
    printf("blnsvr -c\tResume service\n");
    printf("blnsvr status\tCurrent status\n");
    printf("\n");
}

BOOL InstallService()
{
    SC_HANDLE newService;
    SC_HANDLE scm;
    TCHAR szBuffer[255];
    TCHAR szPath[MAX_PATH];

    GetModuleFileName( GetModuleHandle(NULL), szPath, MAX_PATH );
    if (FAILED( StringCchCopy(szBuffer, 255, TEXT("\"")))) {
        return FALSE;
    }
    if (FAILED( StringCchCat(szBuffer, 255, szPath))) {
        return FALSE;
    }
    if (FAILED( StringCchCat(szBuffer, 255, TEXT("\"")))) {
        return FALSE;
    }

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (scm == NULL) {
        ErrorHandler("OpenSCManager", GetLastError());
    }
    newService = CreateService(
                            scm,
                            ServiceName,
                            ServiceName,
                            SERVICE_ALL_ACCESS,
                            SERVICE_WIN32_OWN_PROCESS,
                            SERVICE_AUTO_START,
                            SERVICE_ERROR_NORMAL,
                            szBuffer,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL);
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
    BOOL res;
    SERVICE_STATUS status;

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
    } else if (ctrl == SERVICE_CONTROL_PAUSE) {
        printf("Service is pausing...\n");
        res = ControlService(service, SERVICE_CONTROL_PAUSE, &status);
    } else if (ctrl == SERVICE_CONTROL_CONTINUE) {
        printf("Service is resuming...\n");
        res = ControlService(service, SERVICE_CONTROL_CONTINUE, &status);
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

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        ErrorHandler("OpenSCManager", GetLastError());
    }

    service = OpenService(scm, ServiceName, SERVICE_QUERY_CONFIG);
    if (!service) {
        ErrorHandler("OpenService", GetLastError());
    }

    buffer = (LPQUERY_SERVICE_CONFIG)LocalAlloc(LPTR, 4096);
    res = QueryServiceConfig(service, buffer, 4096, &sizeNeeded);
    if (!res) {
        ErrorHandler("QueryServiceConfig", GetLastError());
    }

    printf("Service name:\t%s\n", buffer->lpDisplayName);
    printf("Service type:\t%d\n", buffer->dwServiceType);
    printf("Start type:\t%d\n",buffer->dwStartType);
    printf("Start name:\t%s\n",buffer->lpServiceStartName);
    printf("Path:\t\t%s\n",buffer->lpBinaryPathName);

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
                                NULL);
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
