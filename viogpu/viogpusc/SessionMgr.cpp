#include "pch.h"
#include "SessionMgr.h"
#include "Session.h"

CSessionMgr::CSessionMgr()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
}

CSessionMgr::~CSessionMgr()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);
}

DWORD WINAPI CSessionMgr::ServiceThread(CSessionMgr* prt)
{
    prt->Run();
    return 0;
}

void CSessionMgr::Run()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    ULONG  id = WTSGetActiveConsoleSessionId();
    if (id == 0xFFFFFFFF) {
        PrintMessage(L"WTSGetActiveConsoleSessionId failed\n");
        return;
    }

    CSession* pSession = FindSession(id, true);
    if (!pSession) {
        PrintMessage(L"Cannot create new session %d\n", id);
        return;
    }

    if (!pSession->Init()) {
        PrintMessage(L"Cannot init the session %d\n", id);
        return;
    }

    SESSION_STATUS CurrentStatus = pSession->GetStatus();
    CurrentStatus.SessionLogon = 1;
    CurrentStatus.ConsoleConnect = 1;
    pSession->SetStatus(CurrentStatus);
}

bool CSessionMgr::Init()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    m_hThread = CreateThread(
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)ServiceThread,
        (LPVOID)this,
        0,
        NULL);

    if (m_hThread == NULL)
    {
        PrintMessage(L"Cannot create thread Error = %d.\n", GetLastError());
        return false;
    }

    return true;
}

void CSessionMgr::Close()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hThread) {
        TerminateThread(m_hThread, 0);
        m_hThread = NULL;
    }

    for (Iterator it = Sessions.begin(); it != Sessions.end(); it++)
    {
        delete *it;
    }

    Sessions.clear();
}

SESSION_STATUS CSessionMgr::GetSessionStatus(UINT Indx)
{
    PrintMessage(L"%ws Index %d\n", __FUNCTIONW__, Indx);

    CSession* ptr = FindSession(Indx);
    if (ptr) {
        return (ptr)->GetStatus();
    }

    SESSION_STATUS status = { 0 };
    return status;
}

void CSessionMgr::SetSessionStatus(UINT Indx, SESSION_STATUS status)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    CSession* ptr = FindSession(Indx);
    if (ptr) {
        return (ptr)->SetStatus(status);
    }
}

HANDLE CSessionMgr::GetSessioinProcess(UINT Indx)
{ 
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    CSession* ptr = FindSession(Indx);
    if (ptr) {
        return (ptr)->GetProcess();
    }

    return (HANDLE)NULL;
}

void CSessionMgr::SetSessionProcess(UINT Indx, HANDLE Handle)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    CSession* ptr = FindSession(Indx);
    if (ptr) {
        return (ptr)->SetProcess(Handle);
    }
}

DWORD CSessionMgr::SessionChange(DWORD evtype, PVOID evdata)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    PWTSSESSION_NOTIFICATION pWTSSESSION_NOTIFICATION = (PWTSSESSION_NOTIFICATION)evdata;
    if (!pWTSSESSION_NOTIFICATION) {
        return ERROR_INVALID_FUNCTION;
    }

    ULONG SessionId = pWTSSESSION_NOTIFICATION->dwSessionId;
    if (SessionId == 0 || SessionId == 0xFFFFFFFF) {
        return ERROR_INVALID_FUNCTION;
    }
    CSession* pSession = FindSession(SessionId, true);
    if (!pSession) {
        return ERROR_INVALID_FUNCTION;
    }

    if (!pSession->Init()) {
        PrintMessage(L"Cannot init the session %d\n", SessionId);
        return ERROR_INVALID_FUNCTION;
    }

    PrintMessage(L"%ws evtype = %u\n", __FUNCTIONW__, evtype);

    SESSION_STATUS CurrentStatus = pSession->GetStatus();

    switch (evtype)
    {
    case WTS_CONSOLE_CONNECT:
        if (CurrentStatus.ConsoleConnect == 0) {
            CurrentStatus.ConsoleConnect = 1;
            pSession->SetStatus(CurrentStatus);
        }
        break;
    case WTS_CONSOLE_DISCONNECT:
        CurrentStatus.ConsoleConnect = 0;
        pSession->SetStatus(CurrentStatus);
        break;
    case WTS_SESSION_LOGON:
        CurrentStatus.SessionLogon = 1;
        pSession->SetStatus(CurrentStatus);
        break;
    case WTS_SESSION_UNLOCK:
        CurrentStatus.SessionLock = 0;
        pSession->SetStatus(CurrentStatus);
        break;
    case WTS_SESSION_LOCK:
        CurrentStatus.SessionLock = 1;
        pSession->SetStatus(CurrentStatus);
        break;
    case WTS_SESSION_LOGOFF:
        CurrentStatus.ConsoleConnect = 0;
        CurrentStatus.SessionLogon = 0;
        CurrentStatus.SessionLock = 0;
        pSession->SetStatus(CurrentStatus);
        pSession->SetProcess(NULL);
        break;
    default:
        break;
    }

    return ERROR_SUCCESS;
}

CSession* CSessionMgr::FindSession(ULONG Indx, bool bCreate)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    for (Iterator it = Sessions.begin(); it != Sessions.end(); it++)
    {
        if ((*it)->GetId() == Indx) {
            return (*it);
        }
    }

    if (bCreate) {
        CSession* newSession = new CSession(Indx);
        AddSession(newSession);
        return newSession;
    }

    return NULL;
}

void CSessionMgr::AddSession(CSession* session)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    Sessions.push_back(session);
}
