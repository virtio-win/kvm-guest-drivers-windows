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

#pragma once
class CSessionMgr;

typedef struct _SESSION_STATUS {
  ULONG ConsoleConnect : 1;
  ULONG RemoteConnect : 1;
  ULONG SessionLogon : 1;
  ULONG SessionLock : 1;
  ULONG SessionCreate : 1;
  ULONG Reserved : 27;
} SESSION_STATUS, *PSESSION_STATUS;

typedef struct _SESSION_INFORMATION {
  SESSION_STATUS Status;
  HANDLE hProcess;
  ULONG SessionId;
} SESSION_INFORMATION, *PSESSION_INFORMATION;

class CService {
public:
  CService();
  ~CService();
  BOOL InitService();
  void GetStatus(SC_HANDLE service);
  static DWORD __stdcall HandlerExThunk(CService *pService, DWORD ctlcode,
                                        DWORD evtype, PVOID evdata);
  static void __stdcall ServiceMainThunk(CService *service, DWORD argc,
                                         TCHAR *argv[]);
  SERVICE_STATUS_HANDLE m_StatusHandle;

private:
  BOOL SendStatusToSCM(DWORD dwCurrentState, DWORD dwWin32ExitCode,
                       DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint,
                       DWORD dwWaitHint);
  void StopService();
  void terminate(DWORD error);
  void ServiceCtrlHandler(DWORD controlCode);
  void ServiceMain(DWORD argc, LPTSTR *argv);
  DWORD ServiceHandleDeviceChange(DWORD evtype);
  DWORD ServiceHandlePowerEvent(DWORD evtype, DWORD flags);
  DWORD ServiceControlSessionChange(DWORD evtype, PVOID flags);
  HANDLE m_evTerminate;
  BOOL m_bRunningService;
  DWORD m_Status;
  CSessionMgr *m_SessionMgr;
};
