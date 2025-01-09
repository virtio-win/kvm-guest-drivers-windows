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

#include "pch.h"
#include <iostream>

int _cdecl wmain(__in ULONG argc, __in_ecount(argc) PWCHAR argv[])
{
    PrintMessage(L"viogpuap.exe built on %ws %ws\n", _CRT_WIDE(__DATE__), _CRT_WIDE(__TIME__));

    PipeClient *pClient = NULL;
    if (argc == 2 && iswdigit(argv[1][0]))
    {
        std::wstring pipename = PIPE_NAME;
        pipename.append(argv[1]);
        if (pipename.length())
        {
            pClient = new PipeClient(pipename);
            pClient->Init();
        }
    }

    GpuAdaptersMgr *m_pMgr;
    m_pMgr = new GpuAdaptersMgr();
    if (!m_pMgr || !m_pMgr->Init())
    {
        ErrorHandler("Start GpuAdaptersMgr", GetLastError());
        return 0;
    }

    if (pClient)
    {
        pClient->WaitRunning();
    }
    else
    {
        while (getchar() != 'q')
            ;
    }

    m_pMgr->Close();
    delete m_pMgr;
    m_pMgr = NULL;

    if (pClient)
    {
        pClient->Close();
        delete pClient;
        pClient = NULL;
    }
    return 0;
}
