// DummyService.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

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
