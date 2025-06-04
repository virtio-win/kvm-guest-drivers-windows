#pragma once

#include "stdafx.h"

CSocket::CSocket()
{
    m_Socket = INVALID_SOCKET;
    m_Mutex = CreateMutex(NULL, FALSE, NULL);
}

CSocket::~CSocket()
{
    Cleanup();
    CloseHandle(m_Mutex);
}

void CSocket::SetSocket(SOCKET s)
{
    m_Socket = s;
}

SOCKET CSocket::GetSocket()
{
    return m_Socket;
}

void CSocket::Cleanup()
{
    DWORD dwWaitResult = WaitForSingleObject(m_Mutex, INFINITE);
    if (dwWaitResult != WAIT_OBJECT_0)
    {
        return;
    }

    if (m_Socket == INVALID_SOCKET)
    {
        ReleaseMutex(m_Mutex);
        return;
    }

    shutdown(m_Socket, SD_BOTH);
    closesocket(m_Socket);
    m_Socket = INVALID_SOCKET;
    ReleaseMutex(m_Mutex);
}
