#ifndef BRIDGE_H
#define BRIDGE_H

#include <wtypes.h>
#include <winsock2.h>
#include <utility>

class CService;
class CSocket;

class CBridge
{
  public:
    CBridge();
    ~CBridge();
    BOOL Init(CService *Service, std::pair<UINT, UINT> portMapping);
    VOID Finalize();
    BOOL Start();
    VOID Stop();

  protected:
    DWORD Run();

  private:
    static DWORD WINAPI BridgeThread(LPDWORD lParam);
    static DWORD WINAPI ThreadV2T(LPVOID lpParam);
    static DWORD WINAPI ThreadT2V(LPVOID lpParam);
    SOCKET InitializeVsockListen(ADDRESS_FAMILY af, UINT cid);
    SOCKET AcceptVsockConnection();
    SOCKET InitializeAndConnectTcp();

    CService *m_pService;
    HANDLE m_hThread;
    HANDLE m_evtInitialized;
    HANDLE m_evtTerminate;

    std::pair<UINT, UINT> m_portMapping;
    CSocket *m_VsockListenSocket;
    CSocket *m_VsockClientSocket;
    CSocket *m_TcpSocket;
    HANDLE m_hThreadV2T;
    HANDLE m_hThreadT2V;
};

#endif
