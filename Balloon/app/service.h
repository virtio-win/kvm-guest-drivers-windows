#ifndef SERVICE_H
#define SERVICE_H

#include <windows.h>
#include <dbt.h>

#ifdef UNIVERSAL
#define NOTIFY_HANDLE HCMNOTIFICATION
#else
#define NOTIFY_HANDLE HDEVNOTIFY
#endif

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

    NOTIFY_HANDLE RegisterDeviceInterfaceNotification();
    NOTIFY_HANDLE RegisterDeviceHandleNotification(HANDLE DeviceHandle);
    BOOL UnregisterNotification(NOTIFY_HANDLE Handle);

#ifdef UNIVERSAL
    static DWORD WINAPI DeviceNotificationCallback(HCMNOTIFICATION Notify,
        PVOID Context, CM_NOTIFY_ACTION Action,
        PCM_NOTIFY_EVENT_DATA EventData, DWORD EventDataSize);
#endif

private:
    BOOL SendStatusToSCM(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint);
    void StopService();
    void terminate(DWORD error);
    void ServiceCtrlHandler(DWORD controlCode);
    void ServiceMain(DWORD argc, LPTSTR *argv);
    DWORD ServiceHandleDeviceChange(DWORD evtype);
    DWORD ServiceHandlePowerEvent(DWORD evtype, DWORD flags);

    NOTIFY_HANDLE m_hDevNotify;
    HANDLE m_evTerminate;
    BOOL   m_bRunningService;
    DWORD  m_Status;
    CDevice* m_pDev;
};

#endif
