/*
 * This file contains wrapper of netkvmco library in netkvmp.exe
 *
 * Copyright (c) 2023 Red Hat, Inc.
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

#include "NetKVMCo.h"
#include "stdafx.h"
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS // some CString constructors will be explicit
#include <atlcoll.h>
#include <atlstr.h>

#define Log(fmt, ...)                                                                                                  \
    {                                                                                                                  \
        CStringA _s_;                                                                                                  \
        _s_.Format(fmt "\n", __VA_ARGS__);                                                                             \
        OutputDebugStringA(_s_);                                                                                       \
    }

static NS_CONTEXT_ATTRIBUTES Ctx;

class CArguments : public CAtlArray<LPWSTR>
{
  public:
    CArguments(int argc, char **argv)
    {
        for (int i = 0; i < argc; ++i)
        {
            Add(argv[i]);
        }
        for (UINT i = 0; i < GetCount(); ++i)
        {
            m_Copy.Add(GetAt(i));
        }
    }
    ~CArguments()
    {
        for (UINT i = 0; i < GetCount(); ++i)
        {
            delete[] (GetAt(i));
        }
    }
    LPWSTR *GetCopy()
    {
        return m_Copy.GetData();
    }

  protected:
    void Add(const char *s)
    {
        size_t len = strlen(s) + 1;
        LPWSTR ws = new WCHAR[len];
        for (UINT i = 0; i < len; ++i)
        {
            ws[i] = s[i];
        }
        __super::Add(ws);
    }
    CAtlArray<LPWSTR> m_Copy;
    LPWSTR *GetData()
    {
        return __super::GetData();
    }
};

static bool IsEqual(const char *Str, LPCWSTR Ws)
{
    while (*Str && *Ws)
    {
        if (tolower(*Str) != tolower(*Ws))
            return false;
        Str++;
        Ws++;
    }
    return *Str == *Ws;
}

static bool FindToken(const char *Token, PCMD_ENTRY &Cmd, ULONG &Size)
{
    if (Ctx.ulNumGroups)
    {
        for (ULONG i = 0; i < Ctx.ulNumGroups; ++i)
        {
            auto &group = (*Ctx.pCmdGroups)[i];
            if (IsEqual(Token, group.pwszCmdGroupToken))
            {
                Cmd = group.pCmdGroup;
                Size = group.ulCmdGroupSize;
                return true;
            }
        }
    }
    if (Ctx.ulNumTopCmds)
    {
        for (ULONG i = 0; i < Ctx.ulNumTopCmds; ++i)
        {
            auto &cmd = (*Ctx.pTopCmds)[i];
            if (IsEqual(Token, cmd.pwszCmdToken))
            {
                Cmd = &cmd;
                Size = 1;
                return true;
            }
        }
    }
    Cmd = NULL;
    Size = 0;
    return false;
}

void PrintError(HMODULE, UINT ResourceId)
{
    CString s;
    if (s.LoadString(ResourceId))
    {
        printf("netkvmco error: %S", s.GetString());
    }
}

void PrintMessageFromModule(HMODULE, UINT ResourceId)
{
    CString s;
    if (s.LoadString(ResourceId))
    {
        printf("%S", s.GetString());
    }
}

// example: prefix='Idx'
// looks for 'Idx=' and shifts Str to remove it
static bool FindRemovePrefix(LPWSTR &Str, LPCWSTR Prefix)
{
    CString s = Str, p = Prefix;
    p += '=';
    p.MakeLower();
    s.MakeLower();
    if (!s.Find(p.GetString()))
    {
        Str += p.GetLength();
        return true;
    }
    return false;
}

DWORD PreprocessCommand(HANDLE, LPWSTR *ppwcArguments, DWORD dwCurrentIndex, DWORD dwArgCount, TAG_TYPE *pttTags,
                        DWORD dwTagCount, DWORD dwMinArgs, DWORD dwMaxArgs, DWORD *pdwTagType)
{
    Log("%s: idx %d, args %d, %d ... %d", __FUNCTION__, dwCurrentIndex, dwArgCount, dwMinArgs, dwMaxArgs);
    if ((dwArgCount - dwCurrentIndex) < dwMinArgs)
    {
        return ERROR_INVALID_PARAMETER;
    }
    else if (pttTags)
    {
        for (UINT i = dwCurrentIndex, tagIdx = 0; i < dwArgCount && tagIdx < dwTagCount; ++i, ++tagIdx)
        {
            if (FindRemovePrefix(ppwcArguments[i], pttTags[tagIdx].pwszTag))
            {
                pttTags[tagIdx].bPresent = true;
            }
            pdwTagType[tagIdx] = tagIdx;
        }
        return NO_ERROR;
    }
    return ERROR_INVALID_PARAMETER;
}

DWORD RegisterContext(IN CONST NS_CONTEXT_ATTRIBUTES *pChildContext)
{
    Ctx = *pChildContext;
    return NO_ERROR;
}

static void DoTab(int minTab, LPCWSTR Name)
{
    CString s = Name;
    if (s.GetLength() < 8)
        printf("\t");
    while (minTab-- > 0)
        printf("\t");
}

static void Usage()
{
    if (Ctx.ulNumTopCmds)
    {
        puts("Commands:");
        for (ULONG i = 0; i < Ctx.ulNumTopCmds; ++i)
        {
            auto &cmd = (*Ctx.pTopCmds)[i];
            printf("\t%S", cmd.pwszCmdToken);
            DoTab(1, cmd.pwszCmdToken);
            PrintMessageFromModule(NULL, cmd.dwShortCmdHelpToken);
        }
    }
    if (Ctx.ulNumGroups)
    {
        puts("Command groups:");
        for (ULONG i = 0; i < Ctx.ulNumGroups; ++i)
        {
            auto &group = (*Ctx.pCmdGroups)[i];
            printf("\t%S", group.pwszCmdGroupToken);
            DoTab(1, group.pwszCmdGroupToken);
            PrintMessageFromModule(NULL, group.dwShortCmdHelpToken);
        }
    }
}

// argv[0] points on command name
static int ProcessCommand(int argc, char **argv, PCMD_ENTRY Cmd)
{
    CArguments a(argc, argv);
    BOOL done = false;
    ULONG res = Cmd->pfnCmdHandler(NULL, a.GetCopy(), 1, argc, 0, NULL, &done);
    if (res)
    {
        Log("%s: returns %d", __FUNCTION__, res);
        CString help;
        (void)help.LoadString(Cmd->dwCmdHlpToken);
        printf("%S", help.GetString());
    }
    return res;
}

static void LogParameters(int argc, char **argv)
{
    CStringA s;
    while (argc)
    {
        argc--;
        s += *argv++;
        s += ' ';
    }
    Log("cmd: %s", s.GetString());
}

static int ProcessNetkvmCommand(int argc, char **argv)
{
    PCMD_ENTRY pCmd;
    ULONG size = 0;
    LogParameters(argc, argv);
    ULONG res = NetKVMNetshStartHelper(NULL, 0);
    if (res)
        return res;
    if (!argc)
    {
        // argc = 0
        Usage();
    }
    else if (IsEqual(*argv, L"dump") && Ctx.pfnDumpFn)
    {
        // argc >= 1
        argv++;
        argc--;
        CArguments a(argc, argv);
        Ctx.pfnDumpFn(NULL, a.GetCopy(), argc, NULL);
    }
    else if (FindToken(*argv, pCmd, size) && pCmd)
    {
        // argc >= 1
        if (size > 1) // this is a group ('show', for ex.)
        {
            // try to find one of group's commands
            res = ERROR_NOT_FOUND;
            // we do the loop only with argc > 1
            // otherwise - print group help
            for (ULONG i = 0; argc > 1 && i < size; ++i)
            {
                auto &cmd = pCmd[i];
                if (IsEqual(argv[1], cmd.pwszCmdToken))
                {
                    // argc > 1, for ex. 'show devices'
                    // argv[0] - group name
                    // argv[1] - command name
                    argc--;
                    argv++;
                    // now argv[0] - command name
                    res = ProcessCommand(argc, argv, &cmd);
                    break;
                }
            }
            if (res == ERROR_NOT_FOUND)
            {
                printf("%s <...>\tpossible values:\n", *argv);
                for (ULONG i = 0; i < size; ++i)
                {
                    auto &cmd = pCmd[i];
                    printf("\t%S", cmd.pwszCmdToken);
                    DoTab(1, cmd.pwszCmdToken);
                    PrintMessageFromModule(NULL, cmd.dwShortCmdHelpToken);
                    res = NO_ERROR;
                }
            }
        }
        else
        {
            res = ProcessCommand(argc, argv, pCmd);
        }
    }
    else
    {
        Usage();
    }
    NetKVMNetshStopHelper(0);
    return res;
}

int main(int argc, char **argv)
{
    return ProcessNetkvmCommand(argc - 1, argv + 1);
}
