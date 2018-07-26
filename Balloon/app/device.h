#ifndef DEVICE_H
#define DEVICE_H

#include <wtypes.h>

class CMemStat;
class CService;

class CDevice {
public:
    CDevice();
    ~CDevice();
    BOOL Init(CService *Service);
    VOID Fini();
    BOOL Start();
    VOID Stop();

protected:
    PTCHAR  GetDevicePath(IN LPGUID InterfaceGuid);
    DWORD   Run();
private:
    static DWORD WINAPI DeviceThread(LPDWORD lParam);
    VOID WriteLoop(HANDLE hDevice);
    CMemStat* m_pMemStat;
    CService *m_pService;
    HANDLE m_hThread;
    HANDLE m_evtInitialized;
    HANDLE m_evtTerminate;
    HANDLE m_evtWrite;
};

#endif
