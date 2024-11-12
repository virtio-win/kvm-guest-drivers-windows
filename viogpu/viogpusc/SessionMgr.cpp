/*
 * Copyright (C) 2021-2022 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. THIS SOFTWARE IS PROVIDED
 * BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "SessionMgr.h"
#include "Session.h"
#include "pch.h"

CSessionMgr::CSessionMgr() { PrintMessage(L"%ws\n", __FUNCTIONW__); }

CSessionMgr::~CSessionMgr() { PrintMessage(L"%ws\n", __FUNCTIONW__); }

DWORD WINAPI CSessionMgr::ServiceThread(CSessionMgr *prt) {
  HANDLE hProcessHandle;
  DWORD ExitCode;

  prt->Run();
  for (Iterator it = prt->Sessions.begin(); it != prt->Sessions.end(); it++) {
    hProcessHandle = prt->GetSessioinCreateProcess((*it)->GetId());
    WaitForSingleObject(hProcessHandle, INFINITE);
    GetExitCodeProcess(hProcessHandle, &ExitCode);
    PrintMessage(L"Process finished with code 0x%x\n", ExitCode);
  }

  return 0;
}

void CSessionMgr::Run() {
  PrintMessage(L"%ws\n", __FUNCTIONW__);

  ULONG id = WTSGetActiveConsoleSessionId();
  if (id == 0xFFFFFFFF) {
    PrintMessage(L"WTSGetActiveConsoleSessionId failed\n");
    return;
  }

  CSession *pSession = FindSession(id, true);
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

bool CSessionMgr::Init() {
  PrintMessage(L"%ws\n", __FUNCTIONW__);

  m_hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ServiceThread,
                           (LPVOID)this, 0, NULL);

  if (m_hThread == NULL) {
    PrintMessage(L"Cannot create thread Error = %d.\n", GetLastError());
    return false;
  }

  return true;
}

void CSessionMgr::Close() {
  PrintMessage(L"%ws\n", __FUNCTIONW__);

  if (m_hThread) {
    TerminateThread(m_hThread, 0);
    m_hThread = NULL;
  }

  for (Iterator it = Sessions.begin(); it != Sessions.end(); it++) {
    (*it)->Close();
    delete *it;
  }

  Sessions.clear();
}

SESSION_STATUS CSessionMgr::GetSessionStatus(UINT Indx) {
  PrintMessage(L"%ws Index %d\n", __FUNCTIONW__, Indx);

  CSession *ptr = FindSession(Indx);
  if (ptr) {
    return (ptr)->GetStatus();
  }

  SESSION_STATUS status = {0};
  return status;
}

void CSessionMgr::SetSessionStatus(UINT Indx, SESSION_STATUS status) {
  PrintMessage(L"%ws\n", __FUNCTIONW__);

  CSession *ptr = FindSession(Indx);
  if (ptr) {
    return (ptr)->SetStatus(status);
  }
}

HANDLE CSessionMgr::GetSessioinCreateProcess(UINT Indx) {
  PrintMessage(L"%ws\n", __FUNCTIONW__);

  CSession *ptr = FindSession(Indx);
  if (ptr) {
    return (ptr)->GetCreateProcess();
  }

  return (HANDLE)NULL;
}

HANDLE CSessionMgr::GetSessioinProcess(UINT Indx) {
  PrintMessage(L"%ws\n", __FUNCTIONW__);

  CSession *ptr = FindSession(Indx);
  if (ptr) {
    return (ptr)->GetProcess();
  }

  return (HANDLE)NULL;
}

void CSessionMgr::SetSessionProcess(UINT Indx, HANDLE Handle) {
  PrintMessage(L"%ws\n", __FUNCTIONW__);

  CSession *ptr = FindSession(Indx);
  if (ptr) {
    return (ptr)->SetProcess(Handle);
  }
}

DWORD CSessionMgr::SessionChange(DWORD evtype, PVOID evdata) {
  PrintMessage(L"%ws\n", __FUNCTIONW__);

  PWTSSESSION_NOTIFICATION pWTSSESSION_NOTIFICATION =
      (PWTSSESSION_NOTIFICATION)evdata;
  if (!pWTSSESSION_NOTIFICATION) {
    return ERROR_INVALID_FUNCTION;
  }

  ULONG SessionId = pWTSSESSION_NOTIFICATION->dwSessionId;
  if (SessionId == 0 || SessionId == 0xFFFFFFFF) {
    return ERROR_INVALID_FUNCTION;
  }
  CSession *pSession = FindSession(SessionId, true);
  if (!pSession) {
    return ERROR_INVALID_FUNCTION;
  }

  if (!pSession->Init()) {
    PrintMessage(L"Cannot init the session %d\n", SessionId);
    return ERROR_INVALID_FUNCTION;
  }

  PrintMessage(L"%ws evtype = %u\n", __FUNCTIONW__, evtype);

  SESSION_STATUS CurrentStatus = pSession->GetStatus();

  switch (evtype) {
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

CSession *CSessionMgr::FindSession(ULONG Indx, bool bCreate) {
  PrintMessage(L"%ws\n", __FUNCTIONW__);

  for (Iterator it = Sessions.begin(); it != Sessions.end(); it++) {
    if ((*it)->GetId() == Indx) {
      return (*it);
    }
  }

  if (bCreate) {
    CSession *newSession = new CSession(Indx);
    AddSession(newSession);
    return newSession;
  }

  return NULL;
}

void CSessionMgr::AddSession(CSession *session) {
  PrintMessage(L"%ws\n", __FUNCTIONW__);

  Sessions.push_back(session);
}
