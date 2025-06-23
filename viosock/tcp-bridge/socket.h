#pragma once

#include <wtypes.h>
#include <winsock2.h>

class CSocket
{
  public:
    CSocket();
    ~CSocket();

    void SetSocket(SOCKET s);
    SOCKET GetSocket();

    void Cleanup();

  private:
    SOCKET m_Socket;
    HANDLE m_Mutex;
};