#ifndef SERVICE_H
#define SERVICE_H

#include <windows.h>
#include <dbt.h>

class CDevice;

class CService
{
public:
    CService();
    ~CService();
    BOOL InitService();
    void GetStatus(SC_HANDLE service);
    static DWORD __stdcall HandlerExThunk(CService* service, DWORD ctlcode, DWORD evtype, PVOID evdata);
    static void __stdcall ServiceMainThunk(CService* service, DWORD argc, TCHAR* argv[]);
    SERVICE_STATUS_HANDLE m_StatusHandle;

private:
    BOOL SendStatusToSCM(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint);
    void StopService();
    void terminate(DWORD error);
    void ServiceCtrlHandler(DWORD controlCode);
    void ServiceMain(DWORD argc, LPTSTR *argv);
    DWORD ServiceHandleDeviceChange(DWORD evtype, _DEV_BROADCAST_HEADER* dbhdr);
    DWORD ServiceHandlePowerEvent(DWORD evtype, DWORD flags);

    HDEVNOTIFY m_hDevNotify;
    HANDLE m_evTerminate;
    BOOL   m_bRunningService;
    DWORD  m_Status;
    CDevice* m_pDev;
};

#endif
