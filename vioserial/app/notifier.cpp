#include "notifier.h"
#include "assert.h"

CNotifier::CNotifier()
{
    m_hThread  = INVALID_HANDLE_VALUE;
    m_hEvent   = INVALID_HANDLE_VALUE;
    m_bRunning = TRUE;
}

CNotifier::~CNotifier()
{
    if (m_hThread != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hThread);
        m_hThread = INVALID_HANDLE_VALUE;
    }
    if (m_hEvent != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hEvent);
        m_hEvent = INVALID_HANDLE_VALUE;
    }
}

BOOL CNotifier::Init()
{
    DWORD id;

    m_hThread = CreateThread(
                              NULL,
                              0,
                              (LPTHREAD_START_ROUTINE) ServiceThread,
                              (LPVOID)this,
                              0,
                              &id);

    if (m_hThread == NULL) {
        printf("Cannot create thread.\n");
        return FALSE;
    }
    m_bRunning = TRUE;
    return TRUE;
}

BOOL CNotifier::Stop()
{
    BOOL res = FALSE;
    return res;
}


DWORD WINAPI CNotifier::ServiceThread(CNotifier* ptr)
{
    ptr->Run();
    return 0;
}

void CNotifier::Run()
{
}
