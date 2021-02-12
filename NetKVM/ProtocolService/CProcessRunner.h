#pragma once

class CPipe
{
public:
    CPipe(bool ForRead = true)
    {
        SECURITY_ATTRIBUTES a = {};
        a.nLength = sizeof(a);
        a.bInheritHandle = true;
        CreatePipe(&m_hRead, &m_hWrite, &a, 0);
        SetHandleInformation(ForRead ? m_hRead : m_hWrite, HANDLE_FLAG_INHERIT, 0);
    }
    ~CPipe()
    {
        if (m_hRead)
            CloseHandle(m_hRead);
        if (m_hWrite)
            CloseHandle(m_hWrite);
    }
    HANDLE ReadHandle()
    {
        return m_hRead;
    }
    HANDLE WriteHandle()
    {
        return m_hWrite;
    }
    void CloseWrite()
    {
        if (m_hWrite)
        {
            CloseHandle(m_hWrite);
            m_hWrite = NULL;
        }
    }
protected:
    HANDLE m_hRead = NULL;
    HANDLE m_hWrite = NULL;
};

class CProcessRunner
{
public:
    // WaitTime = 0 starts the process as orphan, without waiting for termination
    // WaitTime < INFINITE gives a possibility to do some action when the
    //     process is running and decide when to kill it
    CProcessRunner(bool Redirect = true, ULONG WaitTime = INFINITE) :
        m_Redirect(Redirect),
        m_WaitTime(WaitTime)
    {
        Clean();
        if (!m_WaitTime)
        {
            m_Redirect = false;
        }
    }
    void Terminate()
    {
        if (pi.hProcess)
        {
            TerminateProcess(pi.hProcess, 0);
        }
    }
    void RunProcess(CString& CommandLine)
    {
        Clean();
        si.cb = sizeof(si);
        if (m_Redirect)
        {
            si.hStdOutput = m_StdOut.WriteHandle();
            si.hStdError = m_StdErr.WriteHandle();
            si.hStdInput = m_StdIn.ReadHandle();
            si.dwFlags |= STARTF_USESTDHANDLES;
        }

        if (CreateProcess(NULL, CommandLine.GetBuffer(), NULL, NULL, m_Redirect, CREATE_SUSPENDED, NULL, _T("."), &si, &pi))
        {
            if (m_Redirect)
            {
                m_StdOut.CloseWrite();
                m_StdErr.CloseWrite();
            }
            ResumeThread(pi.hThread);
            while (m_WaitTime && WaitForSingleObject(pi.hProcess, m_WaitTime) == WAIT_TIMEOUT)
            {
                if (ShouldTerminate())
                {
                    Terminate();
                }
            }
            Flush();
            ULONG exitCode;
            if (!GetExitCodeProcess(pi.hProcess, &exitCode))
            {
                exitCode = GetLastError();
            }
            PostProcess(exitCode);
        }
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
        Clean();
    }
    const CString& StdOutResult() const { return m_StdOutResult; }
    const CString& StdErrResult() const { return m_StdErrResult; }
protected:
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    CPipe m_StdOut;
    CPipe m_StdErr;
    CPipe m_StdIn;
    CString m_StdOutResult;
    CString m_StdErrResult;
    void Clean()
    {
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
    }
    ULONG m_WaitTime;
    bool  m_Redirect;
    virtual bool ShouldTerminate()
    {
        return false;
    }
    virtual void PostProcess(ULONG ExitCode)
    {
        UNREFERENCED_PARAMETER(ExitCode);
    }
    CString Flush(HANDLE h)
    {
        CString s;
        ULONG err;
        BYTE buffer[128];
        do
        {
            ULONG avail = 0;
            ULONG got = 0;
            bool b = PeekNamedPipe(h, buffer, sizeof(buffer), &got, &avail, NULL);
            if (!b) {
                err = GetLastError();
                Sleep(10);
            }
            if (!b || !avail)
                break;
            got = 0;
            avail = min(avail, sizeof(buffer));
            if (!ReadFile(h, buffer, avail, &got, NULL))
            {
                err = GetLastError();
                Sleep(10);
            }
            if (!got)
                break;
            for (ULONG i = 0; i < got; ++i)
            {
                s += buffer[i];
            }
        } while (true);
        return s;
    }
    void Flush()
    {
        if (!m_Redirect)
            return;
        m_StdOutResult += Flush(m_StdOut.ReadHandle());
        m_StdErrResult += Flush(m_StdErr.ReadHandle());
    }
};

class CWmicQueryRunner : public CProcessRunner
{
public:
    void Run()
    {
        CString cmd = TEXT("wmic /namespace:\\\\root\\wmi path NetKvm_Standby get /value");
        RunProcess(cmd);
    }
    CAtlArray<CString>& Devices() { return m_Devices; }
protected:
    CAtlArray<CString> m_Devices;
    void PostProcess(ULONG ExitCode) override
    {
        Log("WMIC query done, result 0x%X", ExitCode);
        if (!m_StdOutResult.IsEmpty())
        {
            CAtlArray<CString> tokens;

            Log("STDOUT:%S", m_StdOutResult.GetString());

            int position = 0;
            while (position >= 0)
            {
                CString s = m_StdOutResult.Tokenize(TEXT("\r\n"), position);
                if (s.IsEmpty())
                    continue;
                tokens.Add(s);
            }
            for (UINT i = 0; i < tokens.GetCount(); ++i)
            {
                Log("Token[%d]:%S", i, tokens[i].GetString());
            }
            for (UINT i = 2; i < tokens.GetCount(); i += 3)
            {
                if (tokens[i].Find(TEXT("value=1")) >= 0)
                {
                    CString pattern = TEXT("Name=");
                    CString s = tokens[i - 1];
                    INT n = s.Find(pattern);
                    if (n > 0)
                    {
                        s.Delete(0, n + pattern.GetLength());
                        if (!s.IsEmpty())
                        {
                            Log("Added \"%S\" for further action", s.GetString());
                            m_Devices.Add(s);
                        }
                    }
                }
            }
        }
        if (!m_StdErrResult.IsEmpty()) {
            Log("STDERR:%S", m_StdErrResult.GetString());
        }
    }
};
