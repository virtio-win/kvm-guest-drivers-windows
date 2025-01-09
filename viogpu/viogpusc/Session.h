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
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma once
#include "pch.h"
#include <string>

class PipeServer;

class CSession
{
  public:
    CSession(ULONG Id);
    ~CSession(void);
    bool Init(void);
    void Close(void);
    void Pause(void);
    void Resume(void);

    SESSION_STATUS GetStatus(void)
    {
        return m_SessionInfo.Status;
    }
    void SetStatus(SESSION_STATUS status)
    {
        m_SessionInfo.Status = status;
    }
    HANDLE GetProcess(void)
    {
        return m_SessionInfo.hProcess;
    }
    void SetProcess(HANDLE Handle)
    {
        m_SessionInfo.hProcess = Handle;
    }
    ULONG GetId(void)
    {
        return m_SessionInfo.SessionId;
    }
    HANDLE GetCreateProcess(void)
    {
        return m_ProcessInfo.hProcess;
    }

  private:
    bool PipeServerActive(void)
    {
        return (m_PipeServer != NULL);
    }
    bool CreateProcessInSession(const std::wstring &commandLine);
    CSession(void)
    {
        ;
    }

  private:
    PipeServer *m_PipeServer;
    SESSION_INFORMATION m_SessionInfo;
    PROCESS_INFORMATION m_ProcessInfo;
    HANDLE m_hToken;
    LPVOID m_lpvEnv;
};
