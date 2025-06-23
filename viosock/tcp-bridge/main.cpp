#include "stdafx.h"
extern "C"
{
#include "..\inc\install.h"
}

#if defined(EVENT_TRACING)
#include "main.tmh"
#endif

LPWSTR ServiceName = (LPWSTR)L"VsockTcpBridge";
LPWSTR DisplayName = (LPWSTR)L"Vsock Tcp Bridge";

CService srvc;

DWORD WINAPI HandlerEx(DWORD ctlcode, DWORD evtype, PVOID evdata, PVOID context)
{
    CService *service = static_cast<CService *>(context);

    return CService::HandlerExThunk(service, ctlcode, evtype, evdata);
}

void __stdcall ServiceMainEx(DWORD argc, TCHAR *argv[])
{
    srvc.m_StatusHandle = RegisterServiceCtrlHandlerEx(ServiceName, HandlerEx, &srvc);
    CService::ServiceMainThunk(&srvc, argc, argv);
}

SERVICE_TABLE_ENTRY serviceTableEx[] = {{ServiceName, (LPSERVICE_MAIN_FUNCTION)ServiceMainEx}, {NULL, NULL}};

int _cdecl wmain(__in ULONG argc, __in_ecount(argc) PWCHAR argv[])
{
    WPP_INIT_TRACING(ServiceName);
    int iResult = 0;

    WSADATA wsaData = {0};
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        ErrorExit("WSAStartup failed", iResult);
    }

    if (argc == 2)
    {
        if (_tcsicmp(L"-i", argv[1]) == 0)
        {
            InstallService();
        }
        else if (_tcsicmp(L"-u", argv[1]) == 0)
        {
            UninstallService();
        }
        else if (_tcsicmp(L"-r", argv[1]) == 0)
        {
            ServiceRun();
        }
        else if (_tcsicmp(L"-s", argv[1]) == 0)
        {
            ServiceControl(SERVICE_CONTROL_STOP);
        }
        else if (_tcsicmp(L"status", argv[1]) == 0)
        {
            SC_HANDLE scm, service;
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
            printf("STATUS: ");
            srvc.GetStatus(service);
        }
        else if (_tcsicmp(L"config", argv[1]) == 0)
        {
            GetConfiguration();
        }
        else if (_tcsicmp(L"help", argv[1]) == 0)
        {
            ShowUsage();
        }
        else
        {
            ShowUsage();
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s\n", __FUNCTION__);

        iResult = InstallProtocol();
        if (!iResult)
        {
            DWORD error = GetLastError();
            // WSANO_RECOVERY has several reason
            // let's hope the provider is already installed
            if (error != WSANO_RECOVERY)
            {
                ErrorExit("InstallProtocol failed", error);
            }
        }

        BOOL success;
        success = StartServiceCtrlDispatcher(serviceTableEx);
        if (!success)
        {
            ErrorExit("StartServiceCtrlDispatcher", GetLastError());
        }
    }

    WPP_CLEANUP();
    return 0;
}
