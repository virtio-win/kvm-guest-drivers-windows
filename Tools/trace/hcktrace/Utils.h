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

#pragma once

class CSystemDirectory : public CString
{
  public:
    CSystemDirectory()
    {
        WCHAR *p = new WCHAR[MAX_PATH];
        if (p)
        {
            if (GetSystemDirectory(p, MAX_PATH))
            {
                Append(p);
                Append(L"\\");
            }
            delete[] p;
        }
    }
};

class CEvent
{
  public:
    CEvent(bool Manual)
    {
        m_Handle = CreateEvent(NULL, Manual, false, NULL);
    }
    ~CEvent()
    {
        if (m_Handle)
        {
            CloseHandle(m_Handle);
        }
    }
    ULONG Wait(ULONG Millies = INFINITE)
    {
        return WaitForSingleObject(m_Handle, Millies);
    }
    void Set()
    {
        SetEvent(m_Handle);
    }

  private:
    HANDLE m_Handle;
};

class CMutex
{
  public:
    CMutex()
    {
        m_Handle = CreateMutex(NULL, false, NULL);
    }
    ~CMutex()
    {
        if (m_Handle)
        {
            CloseHandle(m_Handle);
        }
    }
    ULONG Wait(ULONG Millies = INFINITE)
    {
        return WaitForSingleObject(m_Handle, Millies);
    }
    void Release()
    {
        ReleaseMutex(m_Handle);
    }

  private:
    HANDLE m_Handle;
};

class CMutexProtect
{
  public:
    CMutexProtect(CMutex &Mutex) : m_Mutex(Mutex)
    {
        Mutex.Wait();
    }
    ~CMutexProtect()
    {
        m_Mutex.Release();
    }

  private:
    CMutex &m_Mutex;
};

class CFileFinder
{
  public:
    CFileFinder(LPCTSTR WildCard) : m_WildCard(WildCard){};
    template <typename T> bool Process(T Functor)
    {
        HANDLE h = FindFirstFile(m_WildCard, &m_fd);
        if (h == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        while (Functor(m_fd.cFileName) && FindNextFile(h, &m_fd))
        {
        }
        FindClose(h);
        return true;
    }

  private:
    WIN32_FIND_DATA m_fd = {};
    const CString m_WildCard;
};

static FORCEINLINE bool DoesFileExist(LPCTSTR NameOrWildcard)
{
    CFileFinder ff(NameOrWildcard);
    return ff.Process([](...) { return false; });
}

static FORCEINLINE ULONG64 LogFileSize(LPCSTR Prefix, LPCTSTR FileName)
{
    struct _stat64 stat = {};
    _wstat64(FileName, &stat);
    CStringA sNum, s = Prefix;
    sNum.Format("%lld", stat.st_size);
    int insertPos = sNum.GetLength() - 3;
    while (insertPos > 0)
    {
        sNum.Insert(insertPos, ',');
        insertPos -= 3;
    }
    s.AppendFormat("%S, size %s", FileName, sNum.GetString());
    Log("%s", s.GetString());
    return stat.st_size;
}

static FORCEINLINE void GetLocalTimestamp(CStringA &Dest)
{
    __time64_t long_time;
    _time64(&long_time);
    tm newtime;
    _localtime64_s(&newtime, &long_time);
    char timestamp[128];
    strftime(timestamp, sizeof(timestamp), "[%D %T]", &newtime);
    Dest = timestamp;
}

void SaveResourceAs(LPCTSTR FileName, LPCTSTR ResourceName);