#include "vioser.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "service.h"

#include "tchar.h"

LPWSTR ServiceName = L"VIOSLSVC";
CService srvc;

void __stdcall HandlerEx(DWORD ctlcode, DWORD evtype, PVOID evdata, PVOID context)
{
    CService::HandlerExThunk((CService*) context, ctlcode, evtype, evdata);
}

void __stdcall ServiceMainEx(DWORD argc, TCHAR* argv[])
{
    srvc.m_StatusHandle = RegisterServiceCtrlHandlerEx(argv[0], (LPHANDLER_FUNCTION_EX) HandlerEx, (PVOID) &srvc);
    CService::ServiceMainThunk(&srvc, argc, argv);
}

SERVICE_TABLE_ENTRY serviceTableEx[] =
{
    { ServiceName, (LPSERVICE_MAIN_FUNCTION) ServiceMainEx},
    { NULL, NULL}
};


ULONG
_cdecl
wmain(
    __in              ULONG argc,
    __in_ecount(Argc) PWCHAR argv[]
    )
{
    if(argc == 2)
    {
        if (_tcsicmp(L"-i", argv[1]) == 0) {
           InstallService();
        } else if (_tcsicmp(L"-u", argv[1]) == 0) {
           UninstallService();
        } else if (_tcsicmp(L"-r", argv[1]) == 0) {
           ServiceRun();
        } else if (_tcsicmp(L"-s", argv[1]) == 0) {
           ServiceControl(SERVICE_CONTROL_STOP);
        } else if (_tcsicmp(L"-p", argv[1]) == 0) {
           ServiceControl(SERVICE_CONTROL_PAUSE);
        } else if (_tcsicmp(L"-c", argv[1]) == 0) {
           ServiceControl(SERVICE_CONTROL_CONTINUE);
        } else if (_tcsicmp(L"status", argv[1]) == 0) {
           SC_HANDLE scm, service;
           scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
           if (!scm) {
              ErrorHandler("OpenSCManager", GetLastError());
           }
           service = OpenService(scm, ServiceName, SERVICE_ALL_ACCESS);
           if (!service) {
              ErrorHandler("OpenService", GetLastError());
           }
           printf("STATUS: ");
           srvc.GetStatus(service);
        } else if (_tcsicmp(L"config", argv[1]) == 0) {
           GetConfiguration();
        } else if (_tcsicmp(L"help", argv[1]) == 0) {
           ShowUsage();
        } else {
           ShowUsage();
        }
    } else {
        BOOL success;
        success = StartServiceCtrlDispatcher(serviceTableEx);
        if (!success) {
           ErrorHandler("StartServiceCtrlDispatcher",GetLastError());
        }
    }
    return 0;
}
