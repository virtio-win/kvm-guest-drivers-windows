// viogpusc.cpp
//

#include <windows.h>
#include "pch.h"
#include "Service.h"
#include <stdio.h>

CService srvc;

DWORD WINAPI HandlerEx(DWORD ctlcode, DWORD evtype, PVOID evdata, PVOID context)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
    CService *service = static_cast<CService*>(context);

    return CService::HandlerExThunk(service, ctlcode, evtype, evdata);
}

void __stdcall ServiceMainEx(DWORD argc, TCHAR* argv[])
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
    srvc.m_StatusHandle = RegisterServiceCtrlHandlerEx(ServiceName, HandlerEx,
        &srvc);
    CService::ServiceMainThunk(&srvc, argc, argv);
}

SERVICE_TABLE_ENTRY serviceTableEx[] =
{
    { (LPWSTR) ServiceName, (LPSERVICE_MAIN_FUNCTION) ServiceMainEx},
    { NULL, NULL}
};


int
_cdecl
wmain(
    __in              ULONG argc,
    __in_ecount(argc) PWCHAR argv[]
    )
{
    if(argc == 2)
    {
        if (_wcsicmp(L"-i", argv[1]) == 0) {
           InstallService();
        } else if (_wcsicmp(L"-u", argv[1]) == 0) {
           UninstallService();
        } else if (_wcsicmp(L"-r", argv[1]) == 0) {
           ServiceRun();
        } else if (_wcsicmp(L"-s", argv[1]) == 0) {
           ServiceControl(SERVICE_CONTROL_STOP);
        } else if (_wcsicmp(L"status", argv[1]) == 0) {
           SC_HANDLE scm, service;
           scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
           if (!scm) {
              ErrorHandler("OpenSCManager", GetLastError());
           }
           service = OpenService(scm, ServiceName, SERVICE_ALL_ACCESS);
           if (!service) {
              ErrorHandler("OpenService", GetLastError());
           }
           wprintf(L"STATUS: ");
           srvc.GetStatus(service);
        } else if (_wcsicmp(L"config", argv[1]) == 0) {
           GetConfiguration();
        } else if (_wcsicmp(L"help", argv[1]) == 0) {
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
