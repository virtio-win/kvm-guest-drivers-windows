/*
 * This file contains implementation of user-mode loggin service
 *
 * Copyright (c) 2025 Red Hat, Inc.
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

#include "stdafx.h"

#define SERVICE_EXEFILE L"hcktrace.exe"

class CStringAMap : public CAtlMap<CStringA, CStringA>
{
  public:
    POSITION SetAt(LPCSTR key, LPCSTR val)
    {
        POSITION pos = __super::SetAt(key, val);
#if 0
        CStringA value;
        if (Lookup("Action"), value)
        {
            Log("%s: after set %s=%s val=%s", __FUNCTION__, key, val, value.GetString());
            CPair *p = GetAt(pos);
            Log("pair: %s=%s", p->m_key.GetString(), p->m_value.GetString());
        }
#endif
        return pos;
    }
    bool Find(LPCSTR key, CStringA &val)
    {
        POSITION pos = GetStartPosition();
        while (pos)
        {
            CPair *p = GetAt(pos);
            if (!p->m_key.CompareNoCase(key))
            {
                val = p->m_value;
                return true;
            }
            GetNext(pos);
        }
        return false;
    }
};

class CJobFile
{
  public:
    CJobFile(bool ForWrite)
    {
        bool exists = DoesFileExist(JOB_FILE);
        ULONG access, share, disp, repeat = 0, attr = 0;
        if (ForWrite)
        {
            access = GENERIC_WRITE;
            share = 0;
            disp = CREATE_NEW;
            Log("%s for write", __FUNCTION__);
        }
        else
        {
            access = GENERIC_READ;
            share = FILE_SHARE_READ;
            disp = OPEN_EXISTING;
            repeat = 20;
            attr = 0;
            m_Delete = true;
            Log("%s for read", __FUNCTION__);
        }
        while (!m_Handle)
        {
            m_Handle = CreateFile(JOB_FILE, access, share, NULL, disp, attr, NULL);
            if (m_Handle == INVALID_HANDLE_VALUE)
            {
                m_Handle = NULL;
                if (!repeat || !exists)
                {
                    break;
                }
                --repeat;
                Sleep(500);
            }
        }
        Log("ctor done = %p%s", m_Handle, exists ? " " : "(no file)");
    }
    static void WaitForDeletion()
    {
        while (DoesFileExist(JOB_FILE))
        {
            Sleep(1000);
        }
    }
    bool Add(LPCSTR Name, LPCSTR Value)
    {
        if (!m_Handle)
        {
            return false;
        }
        CStringA name(Name);
        CStringA value(Value);
        ULONG written = 0;
        name += "=";
        value += "\n";
        WriteFile(m_Handle, name.GetString(), name.GetLength(), &written, NULL);
        WriteFile(m_Handle, value.GetString(), value.GetLength(), &written, NULL);
        return true;
    }
    ~CJobFile()
    {
        Close();
    }
    bool Load(CStringAMap &Map)
    {
        if (!m_Handle)
        {
            return false;
        }
        CStringA key;
        CStringA val;
        CStringA *p = &key;
        char c;
        ULONG done = 0;
        while (ReadFile(m_Handle, &c, 1, &done, NULL) && done == 1)
        {
            switch (c)
            {
                case '=':
                    p = &val;
                    break;
                case '\n':
                    p = &key;
                    Log("%s: setting %s=%s", __FUNCTION__, key.GetString(), val.GetString());
                    Map.SetAt(key, val);
                    key.Empty();
                    val.Empty();
                    break;
                default:
                    (*p).AppendChar(c);
                    break;
            }
        }
        return true;
    }
    void Close()
    {
        if (!m_Handle)
        {
            return;
        }
        CloseHandle(m_Handle);
        m_Handle = NULL;
        Log("%s", __FUNCTION__);
        if (m_Delete)
        {
            bool b = DeleteFile(JOB_FILE);
            Log("Deleting = %d", b);
        }
    }

  private:
    HANDLE m_Handle = NULL;
    bool m_Delete = false;
};

// should always be called from the application
// instance, not from service one
static bool CopySelf(CString &BinPath)
{
    CSystemDirectory s;
    s += SERVICE_EXEFILE;
    bool done;
    int retries = 0;
    Log("%s: copy %S to %S", __FUNCTION__, CServiceImplementation::BinaryPath().GetString(), s.GetString());
    if (!s.CompareNoCase(CServiceImplementation::BinaryPath()))
    {
        return true;
    }
    do
    {
        done = CopyFile(CServiceImplementation::BinaryPath(), s, false);
        if (!done)
        {
            Log("%s: error %d", __FUNCTION__, GetLastError());
            retries++;
            Sleep(1000);
        }
        else
        {
            Log("%s: done", __FUNCTION__);
            BinPath = s;
        }
    } while (!done && retries < 5);
    return done;
}

class CLogServiceImplementation : public CServiceImplementation, public CThreadOwner
{
  public:
    CLogServiceImplementation() : CServiceImplementation(_T("hcktrace")), m_ThreadEvent(false)
    {
    }
    enum
    {
        ctlTrigger = 128
    };

  protected:
    virtual bool OnStart() override
    {
        WriteLogProc = WriteToFile;
        StartThread();
        return true;
    }
    virtual DWORD ControlHandler(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData)
    {
        DWORD res = NO_ERROR;
        switch (dwControl)
        {
            case ctlTrigger:
                ProcessTrigger();
                return res;
            default:
                break;
        }
        res = __super::ControlHandler(dwControl, dwEventType, lpEventData);
        return res;
    }
    virtual bool OnStop() override
    {
        bool res = !IsThreadRunning();
        StopThread();
        m_ThreadEvent.Set();
        // if the thread is running, it will indicate stopped
        // state when the thread is terminated
        return res;
    }
    virtual void ThreadProc()
    {
        CoInitialize(NULL);

        LoadCurrentConfiguration();

        m_Trace.RetrieveState();

        ProcessTrigger();

        do
        {
            if (ThreadState() != tsRunning)
            {
                break;
            }

            m_Trace.Process();

            // Log("%s is running", __FUNCTION__);

            if (m_ThreadEvent.Wait(3000) == ERROR_SUCCESS)
            {
                // thread work body
            }

        } while (true);

        CoUninitialize();
    }
    void ThreadTerminated(tThreadState previous)
    {
        __super::ThreadTerminated(previous);
        SetState(SERVICE_STOPPED);
    }

    bool ProcessTrigger()
    {
        CJobFile f(false);
        CStringAMap map;
        if (!f.Load(map))
        {
            Log("can't load the Job map");
            return false;
        }
        CStringA value;

        if (map.Find("writelog", value))
        {
            Log("applog: %s", value.GetString());
            return false;
        }

        // new setting
        CStringA device = Settings.m_Device, test = Settings.m_CurrentTest;
        ULONG debug = Settings.m_Debug;

        if (map.Find("device", value))
        {
            Log("found device=%s", value.GetString());
            device = value;
        }
        if (map.Find("test", value))
        {
            Log("found test=%s", value.GetString());
            test = value;
            if (!test.CompareNoCase("stop") || !test.CompareNoCase("end") || !test.CompareNoCase("done"))
            {
                test.Empty();
            }
        }
        if (map.Find("debuglevel", value))
        {
            Log("found debuglevel=%s", value.GetString());
            debug = atoi(value);
        }

        Log("Old setting: %s:%s:%d",
            Settings.m_Device.GetString(),
            Settings.m_CurrentTest.GetString(),
            Settings.m_Debug);
        Log("New setting: %s:%s:%d", device.GetString(), test.GetString(), debug);

        if (Settings.m_Device.CompareNoCase(device))
        {
            Settings.m_Device = device;
            SaveCurrentConfiguration();
            m_Trace.ConfigureRecording(device);
        }

        if (Settings.m_Debug != debug)
        {
            Settings.m_Debug = debug;
            SaveCurrentConfiguration();
            m_Trace.SetDebugLevel(debug);
        }

        if (Settings.m_CurrentTest.CompareNoCase(test))
        {
            Settings.m_CurrentTest = test;
            SaveCurrentConfiguration();
            if (test.IsEmpty())
            {
                m_Trace.StopTest();
            }
            else
            {
                m_Trace.StartTest();
            }
        }
        return true;
    }

    void LoadCurrentConfiguration()
    {
        GetString(TEXT("Device"), Settings.m_Device);
        GetString(TEXT("Test"), Settings.m_CurrentTest);
        GetValue(TEXT("Debug"), Settings.m_Debug);
        Log("%s: Dev=%s, Test=%s", __FUNCTION__, Settings.m_Device.GetString(), Settings.m_CurrentTest.GetString());
    }
    void SaveCurrentConfiguration()
    {
        SetString(TEXT("Device"), Settings.m_Device);
        SetString(TEXT("Test"), Settings.m_CurrentTest);
        SetValue(TEXT("Debug"), Settings.m_Debug);
        Log(__FUNCTION__);
    }

  public:
    void QueryTrace()
    {
        // to be called in app context
        LoadCurrentConfiguration();
        printf("Device=%s, Test=%s\n", Settings.m_Device.GetString(), Settings.m_CurrentTest.GetString());
        m_Trace.RetrieveState();
        printf("Trace: %s, status %X, current %d of %d\n",
               m_Trace.m_Active ? "active" : "inactive",
               m_Trace.m_Status,
               m_Trace.m_FileCounter,
               m_Trace.m_FileMax);
        system("logman query -ets " LOGGER_NAME);
    }

  private:
    CEvent m_ThreadEvent;

    struct
    {
        CStringA m_Device;
        CStringA m_CurrentTest;
        ULONG m_Debug = 0;
    } Settings;

    CAutoTrace m_Trace;
};

static CLogServiceImplementation DummyService;

static void Usage()
{
    puts("i(nstall)|u(ninstall)|q(uery)");
    puts("driver <driver> <path> <debuglevel>");
    puts("test <testname>");
    puts("test stop");
}

static void SendJob(LPCSTR param1, LPCSTR param2)
{
    CJobFile f(true);
    f.Add(param1, param2);

    f.Close();
    DummyService.Control(CLogServiceImplementation::ctlTrigger);
    f.WaitForDeletion();
}

static void PopulateFile(LPCTSTR Name)
{
    CString fileName = WORKING_DIR;
    fileName.AppendFormat(TEXT("\\%s.exe"), Name);
    SaveResourceAs(Name, fileName);
}

static void PopulateFiles()
{
    PopulateFile(TEXT("TRACEFMT"));
    PopulateFile(TEXT("TRACELOG"));
}

// example of driver package path:
// C:\\Users\\ADMINI\~1\\AppData\\Local\\Temp\\a99327df-7775-4017-85fe-7e2c809ca6cc
static void FixPdbPath(CStringA &Path)
{
    Path.Replace("\\\\", "\\");
    Path.Replace("\\~", "~");
}

int __cdecl main(int argc, char **argv)
{
    CStringA s;
    if (argc > 1)
    {
        s = argv[1];
        Log("First parameter: %s", s.GetString());
    }
    if (DummyService.CheckInMain(s))
    {
        return 0;
    }
    if (!s.IsEmpty())
    {
        if (!s.CompareNoCase("i") || !s.CompareNoCase("install"))
        {
            puts("installing service");
            CreateDirectory(WORKING_DIR, NULL);
            CreateDirectory(LOGS_PATH, NULL);
            CreateDirectory(ETL_PATH, NULL);

            if (DummyService.Installed())
            {
                puts("Already installed");
            }
            else
            {
                CString binPath;
                if (CopySelf(binPath))
                {
                    DummyService.Install(binPath);
                    DummyService.Start();
                    PopulateFiles();
                }
            }
        }
        else if (!s.CompareNoCase("u") || !s.CompareNoCase("uninstall"))
        {
            if (DummyService.Installed())
            {
                if (DummyService.IsRunning())
                {
                    SendJob("device", "none");
                    DummyService.Stop();
                }
                DummyService.Uninstall();
            }
            else
            {
                puts("Service is not installed");
            }
        }
        else if (!s.CompareNoCase("q") || !s.CompareNoCase("query"))
        {
            printf("Service %sinstalled\n", DummyService.Installed() ? "" : "not ");
            DummyService.QueryTrace();
        }
        else if (!s.CompareNoCase("driver") && argc > 4)
        {
            // copy PDB from the driver package
            CStringA srcPath, destPath;
            srcPath.Format("%s\\%s.pdb", argv[3], argv[2]);
            FixPdbPath(srcPath);
            SendJob("writelog", srcPath);
            destPath.Format(WORKING_DIR_A "\\%s.pdb", argv[2]);
            BOOL b = CopyFileA(srcPath, destPath, false);
            Log("(%s) copying of %s", b ? "OK" : "Failed", srcPath.GetString());
            printf("%s.pdb%scopied\n", argv[2], b ? " " : " not ");
            SendJob("device", argv[2]);
            SendJob("debuglevel", argv[4]);
        }
        else if (!s.CompareNoCase("test") && argc > 2)
        {
            SendJob("test", argv[2]);
        }
        else if (!s.CompareNoCase("x"))
        {
            PopulateFiles();
        }
        else
        {
            Usage();
        }
    }
    else
    {
        Usage();
    }
    return 0;
}
