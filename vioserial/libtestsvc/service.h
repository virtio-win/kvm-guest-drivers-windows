#ifndef SERVICE_H
#define SERVICE_H

#include <windows.h>
#include <dbt.h>


class CService
{
public:
    CService();
    ~CService();
    BOOL InitService();
    void GetStatus(SC_HANDLE service);
    static void __stdcall HandlerThunk(CService* service, DWORD ctlcode);
    static DWORD __stdcall HandlerExThunk(CService* service, DWORD ctlcode, DWORD evtype, PVOID evdata);
    static void __stdcall ServiceMainThunk(CService* service, DWORD argc, TCHAR* argv[]);
    SERVICE_STATUS_HANDLE m_StatusHandle;

private:
    static DWORD WINAPI ServiceThread( LPDWORD lParam);
    void Run();
    BOOL SendStatusToSCM(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint);
    void ResumeService();
    void PauseService();
    void StopService();
    void terminate(DWORD error);
    void ServiceCtrlHandler(DWORD controlCode);
    void ServiceMain(DWORD argc, LPTSTR *argv);
    DWORD ServiceHandleDeviceChange(DWORD evtype, _DEV_BROADCAST_HEADER* dbhdr);
    DWORD ServiceHandlePowerEvent(DWORD evtype, DWORD flags);

    HANDLE m_evTerminate;
    HANDLE m_evWakeUp;
    HANDLE m_thHandle;
    BOOL   m_bPauseService;
    BOOL   m_bRunningService;
    CRITICAL_SECTION m_scWrite;
};

#endif
