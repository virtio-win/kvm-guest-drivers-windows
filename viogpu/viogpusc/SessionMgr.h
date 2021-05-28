#pragma once
#include <list>
#include <algorithm>
class CSession;

class CSessionMgr
{
public:
    CSessionMgr();
    ~CSessionMgr();
    bool Init();
    void Close();
    DWORD SessionChange(DWORD evtype, PVOID evdata);
    SESSION_STATUS GetSessionStatus(UINT Indx);
    void SetSessionStatus(UINT Indx, SESSION_STATUS status);
    HANDLE GetSessioinProcess(UINT Indx);
    void SetSessionProcess(UINT Indx, HANDLE Handle);
private:
    CSession* FindSession(ULONG Indx, bool bCreate = false);
    void AddSession(CSession* session);
protected:
    static DWORD WINAPI ServiceThread(CSessionMgr*);
    void Run();
private:
    std::list<CSession*> Sessions;
    typedef std::list<CSession*>::iterator Iterator;
    HANDLE m_hThread;

};

