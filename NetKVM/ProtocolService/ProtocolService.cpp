/*
 * This file contains implementation of minimal user-mode service
 *
 * Copyright (c) 2020 Red Hat, Inc.
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

class CProtocolServiceImplementation : public CServiceImplementation
{
public:
    CProtocolServiceImplementation() : CServiceImplementation(_T("netkvmp")) {}
protected:
#if 0
    virtual bool OnStart() override
    {
        return true;
    }
#endif
    virtual DWORD ControlHandler(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData)
    {
        DWORD res = NO_ERROR;
        switch (dwControl)
        {
            case 0xffffffff:
            default:
                res = __super::ControlHandler(dwControl, dwEventType, lpEventData);
                break;
        }
        return res;
    }
#if 0
    virtual bool OnStop() override
    {
        return true;
    }
#endif
};

static CProtocolServiceImplementation DummyService;

int __cdecl main(int argc, char **argv)
{
    if (CServiceImplementation::CheckInMain())
    {
        return 0;
    }
    if (argc > 1)
    {
        CStringA s = argv[1];
        if (!s.CompareNoCase("i"))
        {
            if (!DummyService.Installed())
            {
                DummyService.Install();
            }
            else
            {
                puts("Already installed");
            }
        }
        if (!s.CompareNoCase("u"))
        {
            if (DummyService.Installed())
            {
                DummyService.Uninstall();
            }
            else
            {
                puts("Not installed");
            }
        }
        if (!s.CompareNoCase("q"))
        {
            puts(DummyService.Installed() ? "installed" : "not installed");
        }
    }
    else
    {
        puts("i(nstall)|u(ninstall)|q(uery)");
    }
    return 0;
}
