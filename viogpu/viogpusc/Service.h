#pragma once
class CSessionMgr;

typedef struct _SESSION_STATUS
{
    ULONG ConsoleConnect : 1;
    ULONG RemoteConnect : 1;
    ULONG SessionLogon : 1;
    ULONG SessionLock : 1;
    ULONG SessionCreate : 1;
    ULONG Reserved : 27;
} SESSION_STATUS, *PSESSION_STATUS;

typedef struct _SESSION_INFORMATION
{
    SESSION_STATUS Status;
    HANDLE hProcess;
    ULONG SessionId;
} SESSION_INFORMATION, *PSESSION_INFORMATION;


class CService
{
public:
    CService();
    ~CService();
    BOOL InitService();
    void GetStatus(SC_HANDLE service);
    static DWORD __stdcall HandlerExThunk(CService* pService, DWORD ctlcode, DWORD evtype, PVOID evdata);
    static void __stdcall ServiceMainThunk(CService* service, DWORD argc, TCHAR* argv[]);
    SERVICE_STATUS_HANDLE m_StatusHandle;
private:
    BOOL SendStatusToSCM(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint);
    void StopService();
    void terminate(DWORD error);
    void ServiceCtrlHandler(DWORD controlCode);
    void ServiceMain(DWORD argc, LPTSTR *argv);
    DWORD ServiceHandleDeviceChange(DWORD evtype);
    DWORD ServiceHandlePowerEvent(DWORD evtype, DWORD flags);
    DWORD ServiceControlSessionChange(DWORD evtype, PVOID flags);
    HANDLE m_evTerminate;
    BOOL   m_bRunningService;
    DWORD  m_Status;
    CSessionMgr* m_SessionMgr;
};

