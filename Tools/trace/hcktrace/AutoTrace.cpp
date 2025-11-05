/*
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

// MSFT documentation
// https://learn.microsoft.com/en-us/windows/win32/etw/configuring-and-starting-an-autologger-session

class CExtRegKey : public CRegKey
{
  public:
    bool GetString(LPCTSTR Name, CString &Value)
    {
        TCHAR buffer[1024];
        ULONG len = ARRAYSIZE(buffer);
        if (QueryStringValue(Name, buffer, &len) == ERROR_SUCCESS)
        {
            buffer[len] = 0;
            Value = buffer;
            return true;
        }
        return false;
    }
};

class CAutoLoggerRegKey : public CExtRegKey
{
  public:
    CAutoLoggerRegKey()
    {
        Create(HKEY_LOCAL_MACHINE, m_KeyPath);
    }
    void Delete()
    {
        RegDeleteTree(HKEY_LOCAL_MACHINE, m_KeyPath);
    }

  protected:
    LPCTSTR m_KeyPath = TEXT("system\\currentcontrolset\\control\\wmi\\autologger\\") LOGGER_LNAME;
};

void CAutoTrace::ConfigureRecording(LPCSTR Device)
{
    // TODO: currently netkvm only
    CStringA device = Device;
    if (device.CompareNoCase("netkvm"))
    {
        m_Active = 0;
        CAutoLoggerRegKey key;
        key.Delete();
        return;
    }

    CAutoLoggerRegKey key;
    m_Active = true;
    key.SetStringValue(TEXT("Guid"), TEXT("{A48B597A-9D86-4277-9455-802691735BB0}"));
    key.SetDWORDValue(TEXT("Start"), m_Active);
    key.SetDWORDValue(TEXT("FileMax"), m_FileMax);
    // EVENT_TRACE_REAL_TIME_MODE does not write log file, just delivers event to consumers if any
    // EVENT_TRACE_BUFFERING_MODE does not write log file, just buffers the data
    key.SetDWORDValue(TEXT("LogFileMode"), EVENT_TRACE_FILE_MODE_SEQUENTIAL);
    // unlimited file size
    key.SetStringValue(TEXT("FileName"), ETL_FILE);
    key.SetDWORDValue(TEXT("MaxFileSize"), 0);
    key.SetDWORDValue(TEXT("FlushTimer"), 2);  // sec
    key.SetDWORDValue(TEXT("BufferSize"), 16); // kb
    key.SetDWORDValue(TEXT("MaximumBuffers"), 256);
    key.SetDWORDValue(TEXT("MinimumBuffers"), 64);

    CExtRegKey subkey;

    subkey.Create(key, TEXT("{5666D67E-281E-43ED-8B8D-4347080198AA}"));
    subkey.SetDWORDValue(TEXT("Enabled"), 1);
    subkey.SetDWORDValue(TEXT("EnableFlags"), 0x7fffffff);
    subkey.SetDWORDValue(TEXT("EnableLevel"), m_DebugLevel);
    subkey.SetStringValue(TEXT("Description"), TEXT("netkvm.trace"));

#if TODO_NDIS_LOG
    subkey.Create(key, TEXT("{CDEAD503-17F5-4A3E-B7AE-DF8CC2902EB9}"));
    subkey.SetDWORDValue(TEXT("Enabled"), 1);
    subkey.SetDWORDValue(TEXT("EnableFlags"), 0x7fffffff);
    subkey.SetDWORDValue(TEXT("EnableLevel"), 7);
    subkey.SetStringValue(TEXT("Description"), TEXT("NDIS"));
#endif
}

void CAutoTrace::SetDebugLevel(ULONG level)
{
    if (!m_Active || level < 4)
    {
        return;
    }
    CAutoLoggerRegKey key;
    CExtRegKey subkey;
    subkey.Open(key, TEXT("{5666D67E-281E-43ED-8B8D-4347080198AA}"));
    subkey.SetDWORDValue(TEXT("EnableLevel"), level);
    m_DebugLevel = level;
}

static bool GetNextFreeFileName(CString &Name, LPCTSTR Format)
{
    ULONG i = 1;
    while (i < 10000)
    {
        Name.Format(Format, i);
        if (!DoesFileExist(Name))
        {
            return true;
        }
        ++i;
    }
    return false;
}

// copy data from current pos of the large file to the small one
// returns true if everything is OK (i.e. there is more data in From)
static bool CopyData(HANDLE From, HANDLE To)
{
    static UCHAR buffer[0x10000];
    ULONG total = 0;
    bool bContinue = true;
    while (bContinue)
    {
        ULONG done = 0;
        if (!ReadFile(From, buffer, sizeof(buffer), &done, NULL) && !done)
        {
            Log("Can't read large file, total copied %d", total);
            return false;
        }
        bContinue = bContinue && done == sizeof(buffer);
        if (!WriteFile(To, buffer, done, &done, NULL))
        {
            Log("Can't write small file, total copied %d, error %d", total, GetLastError());
            return false;
        }
        total += done;
        if (total >= MAX_LOG_SIZE)
        {
            break;
        }
    }
    return bContinue;
}

static void SplitTextFile(const CString &LogName)
{
    LARGE_INTEGER offset;
    offset.QuadPart = MAX_LOG_SIZE;
    CString nextSmallFile;
    HANDLE hLarge = CreateFile(LogName, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hLarge == INVALID_HANDLE_VALUE)
    {
        Log("Can't open %S for split", LogName.GetString());
        return;
    }
    Log("Splitting file %S", LogName.GetString());
    bool needSplit = true;
    SetFilePointerEx(hLarge, offset, &offset, FILE_BEGIN);
    // copy the file content from here to smaller ones
    while (needSplit)
    {
        if (!GetNextFreeFileName(nextSmallFile, LOGS_PATH TEXT("%04d.log")))
        {
            Log("Can't get next text log name for smaller file");
            break;
        }
        HANDLE hSmall = CreateFile(nextSmallFile, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        if (hSmall == INVALID_HANDLE_VALUE)
        {
            Log("Can't create small file %S", nextSmallFile.GetString());
            break;
        }
        needSplit = CopyData(hLarge, hSmall);
        CloseHandle(hSmall);

        LogFileSize("Written small file ", nextSmallFile);
    }

    Log("Truncating large file size to %lld", offset.QuadPart);
    SetFilePointerEx(hLarge, offset, &offset, FILE_BEGIN);
    SetEndOfFile(hLarge);
    CloseHandle(hLarge);
}

static void ConvertBinaryToText(LPCTSTR binPath)
{
    LPCTSTR traceFmt = WORKING_DIR TEXT("\\tracefmt.exe");
    LPCTSTR pdb = WORKING_DIR TEXT("\\netkvm.pdb");
    CString nextTextLog;
    if (!GetNextFreeFileName(nextTextLog, LOGS_PATH TEXT("%04d.log")))
    {
        Log("Can't get next text log name");
        return;
    }
    CProcessRunner converter;
    CString cmdLine;
    cmdLine.Format(TEXT("%s -nosummary %s -pdb %s -o %s"), traceFmt, binPath, pdb, nextTextLog.GetString());
    converter.RunProcess(cmdLine);
    if (DoesFileExist(nextTextLog))
    {
        ULONG64 size = LogFileSize("resulting text ", nextTextLog);
        if (size > MAX_LOG_SIZE)
        {
            SplitTextFile(nextTextLog);
        }
    }
    else
    {
        Log("File %S does not exist after conversion", nextTextLog.GetString());
    }
}

static void EnsureMovable(const CString &Path)
{
    ULONG tries = 0, sleep = 10000;
    while (tries++ < 5)
    {
        HANDLE h = CreateFile(Path, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            CloseHandle(h);
            break;
        }
        Log("File is busy...");
        Sleep(sleep);
    }
}

void CAutoTrace::ProcessEtlFile(ULONG i, bool Move)
{
    CString srcPath, destPath, fileSuffix;
    srcPath = ETL_FILE;
    destPath = LOGS_PATH;
    fileSuffix.Format(TEXT(".%03d"), i);
    srcPath += fileSuffix;
    destPath += ETL_FILENAME;
    destPath += fileSuffix;
    if (!DoesFileExist(srcPath))
    {
        return;
    }
    LogFileSize(__FUNCTION__ ": ", srcPath.GetString());
    if (Move && DoesFileExist(destPath))
    {
        Log("Due to some reason the file exists: %S", destPath.GetString());
        BOOL b = DeleteFile(destPath);
        Log("deletion = %d", b);
        return;
    }
    BOOL res;
    if (Move)
    {
        EnsureMovable(srcPath);
        res = MoveFile(srcPath, destPath);
    }
    else
    {
        res = CopyFile(srcPath, destPath, true);
    }
    Log("(%s) %s %S to %S", Move ? "moving" : "copy", res ? "OK" : "Failed", srcPath.GetString(), destPath.GetString());
    if (!res)
    {
        return;
    }
    if (Move && DoesFileExist(srcPath))
    {
        Log("Due to some reason the file exists: %S", srcPath.GetString());
    }
    ConvertBinaryToText(destPath);
    DeleteFile(destPath);
}

void CAutoTrace::StopTest()
{
    CMutexProtect sync(m_Mutex);
    // stop current logging process
    StopLogging();
    // move the log file aside and parse to logging directory
    ProcessEtlFile(m_FileCounter, true);
    // zip Log directory
    CProcessRunner zipper(false);
    CString cmd, nextzip;
    CreateDirectory(ZIP_PATH, NULL);
    GetNextFreeFileName(nextzip, ZIP_PATH TEXT("test%04d.zip"));
    Log("making archive %S", nextzip.GetString());
    cmd.Format(TEXT("powershell Compress-Archive ") LOGS_PATH TEXT("*.log "));
    cmd += nextzip;
    zipper.RunProcess(cmd);
    // delete the content of Log directory
    CFileFinder logs(LOGS_PATH TEXT("*.*"));
    logs.Process([](LPCTSTR name) {
        CString fname = LOGS_PATH;
        fname += name;
        Log("deleting file %S", name);
        DeleteFile(fname);
        return true;
    });
    if (DoesFileExist(nextzip))
    {
        LogFileSize("resulting zip ", nextzip);
    }
    else
    {
        Log("File %S does not exist after conversion", nextzip.GetString());
    }
    RestartLogging();
}

void CAutoTrace::RetrieveState()
{
    CAutoLoggerRegKey key;
    key.QueryDWORDValue(TEXT("Start"), m_Active);
    key.QueryDWORDValue(TEXT("FileCounter"), m_FileCounter);
    key.QueryDWORDValue(TEXT("Status"), m_Status);
    Log("Autotrace: start %d, file %d, status %X", m_Active, m_FileCounter, m_Status);
    if (m_Active)
    {
        ULONG i = 0;
        while (true)
        {
            TCHAR buffer[1024];
            ULONG len = ARRAYSIZE(buffer);
            if (key.EnumKey(i, buffer, &len) != ERROR_SUCCESS)
            {
                break;
            }
            buffer[len] = 0;
            CExtRegKey subkey;
            if (subkey.Open(key, buffer) == ERROR_SUCCESS)
            {
                CAutoTraceProvider provider;
                provider.ProviderGuid = buffer;
                subkey.QueryDWORDValue(TEXT("Enabled"), provider.m_Enabled);
                subkey.QueryDWORDValue(TEXT("EnableLevel"), provider.m_Level);
                subkey.GetString(TEXT("Description"), provider.m_Description);
                m_Providers.Add(provider);
                Log("Found provider %S, enabled %d", provider.m_Description.GetString(), provider.m_Enabled);
            }
            i++;
        }
    }
}

void CAutoTrace::Process()
{
    CMutexProtect sync(m_Mutex);

    // Parse finished ETL files to Logs directory
    // start from m_FileCounter + 1 to m_FileMax incl
    // then from 1 to m_FileCounter not incl
    // file m_FileCounter is current one, do not touch it
    for (ULONG i = m_FileCounter + 1; i <= m_FileMax; ++i)
    {
        ProcessEtlFile(i, true);
    }
    for (ULONG i = 1; i < m_FileCounter; ++i)
    {
        ProcessEtlFile(i, true);
    }
}

void CAutoTrace::RestartLogging()
{
    CProcessRunner restart(false);
    CString cmd = WORKING_DIR TEXT("\\tracelog.exe -startautologger ") LOGGER_LNAME;
    restart.RunProcess(cmd);
    CAutoLoggerRegKey key;
    key.SetDWORDValue(TEXT("FileCounter"), m_FileCounter);
}

void CAutoTrace::StopLogging()
{
    CString cmd = TEXT("logman stop -ets ") LOGGER_LNAME;
    CProcessRunner stop(false);
    stop.RunProcess(cmd);
}

void CAutoTrace::StartTest()
{
    if (m_FileCounter || !m_Active)
    {
        return;
    }
    CAutoLoggerRegKey key;
    m_FileCounter = 1;
    key.SetDWORDValue(TEXT("FileCounter"), m_FileCounter);
    RestartLogging();
}
