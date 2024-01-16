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

    HCMNOTIFICATION RegisterDeviceInterfaceNotification();
    HCMNOTIFICATION RegisterDeviceHandleNotification(HANDLE DeviceHandle);
    BOOL UnregisterNotification(HCMNOTIFICATION Handle);

    static DWORD WINAPI DeviceNotificationCallback(HCMNOTIFICATION Notify,
        PVOID Context, CM_NOTIFY_ACTION Action,
        PCM_NOTIFY_EVENT_DATA EventData, DWORD EventDataSize);

private:
    BOOL SendStatusToSCM(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint);
    void StopService();
    void terminate(DWORD error);
    void ServiceCtrlHandler(DWORD controlCode);
    void ServiceMain(DWORD argc, LPTSTR *argv);
    DWORD ServiceHandleDeviceChange(DWORD evtype);
    DWORD ServiceHandlePowerEvent(DWORD evtype, DWORD flags);

    HCMNOTIFICATION m_hDevNotify;
    HANDLE m_evTerminate;
    BOOL   m_bRunningService;
    DWORD  m_Status;
    CDevice* m_pDev;
};

#endif
