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

void (*WriteLogProc)(LPCSTR Data) = WriteToDebugger;

void WriteToDebugger(LPCSTR Data)
{
    OutputDebugStringA(Data);
}

void WriteToFile(LPCSTR Data)
{
    static HANDLE hLog = NULL;
    static CMutex mutex;

    CMutexProtect sync(mutex);
    if (!hLog)
    {
        hLog = CreateFile(WORKING_DIR TEXT("\\servicelog.txt"),
                          GENERIC_WRITE,
                          FILE_SHARE_READ,
                          NULL,
                          OPEN_ALWAYS,
                          0,
                          NULL);
        if (hLog == INVALID_HANDLE_VALUE)
        {
            hLog = NULL;
        }
        else
        {
            LARGE_INTEGER li;
            li.QuadPart = 0;
            SetFilePointerEx(hLog, li, NULL, FILE_END);
        }
    }

    if (!hLog)
    {
        WriteToDebugger("Can't open file\n");
        WriteToDebugger(Data);
        return;
    }
    CStringA s;
    GetLocalTimestamp(s);
    s += ": ";
    s += Data;
    ULONG written = 0;
    WriteFile(hLog, s.GetBuffer(), s.GetLength(), &written, NULL);
    FlushFileBuffers(hLog);
}

void SaveResourceAs(LPCTSTR ResourceName, LPCTSTR FileName)
{
    if (DoesFileExist(FileName))
    {
        return;
    }
    Log("[%s] searching resource %S", __FUNCTION__, ResourceName);
    HRSRC hrsrc = FindResource(NULL, ResourceName, TEXT("BINRES"));
    if (!hrsrc)
    {
        return;
    }
    Log("[%s] loading resource", __FUNCTION__);
    HGLOBAL hg = LoadResource(NULL, hrsrc);
    if (!hg)
    {
        return;
    }
    Log("[%s] locking resource", __FUNCTION__);
    PVOID p = LockResource(hg);
    if (!p)
    {
        return;
    }
    Log("[%s] getting size of resource", __FUNCTION__);
    ULONG size = SizeofResource(NULL, hrsrc);
    if (!size)
    {
        return;
    }
    Log("[%s] creating local file", __FUNCTION__);
    HANDLE h = CreateFile(FileName, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return;
    }
    WriteFile(h, p, size, &size, NULL);
    CloseHandle(h);
    Log("[%s] Done", __FUNCTION__);
}
