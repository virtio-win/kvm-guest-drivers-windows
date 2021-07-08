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

    SESSION_STATUS GetStatus(void) { return m_SessionInfo.Status; }
    void SetStatus(SESSION_STATUS status) { m_SessionInfo.Status = status; }
    HANDLE GetProcess(void) { return m_SessionInfo.hProcess; }
    void SetProcess(HANDLE Handle) { m_SessionInfo.hProcess = Handle; }
    ULONG GetId(void) { return m_SessionInfo.SessionId; }
private:
    bool PipeServerActive(void) { return (m_PipeServer != NULL); }
    bool CreateProcessInSession(const std::wstring & commandLine);
    CSession(void) { ; }
private:
    PipeServer* m_PipeServer;
    SESSION_INFORMATION m_SessionInfo;
    PROCESS_INFORMATION m_ProcessInfo;
};

