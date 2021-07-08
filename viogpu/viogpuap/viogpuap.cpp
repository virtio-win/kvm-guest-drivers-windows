#include "pch.h"
#include <iostream>

int
_cdecl
wmain(
    __in              ULONG argc,
    __in_ecount(argc) PWCHAR argv[]
    )
{
    PrintMessage(L"viogpuap.exe built on %ws %ws\n", _CRT_WIDE(__DATE__) , _CRT_WIDE(__TIME__));

    PipeClient* pClient = NULL;
    if (argc == 2 && iswdigit(argv[1][0]))
    {
        std::wstring pipename = PIPE_NAME;
        pipename.append(argv[1]);
        if (pipename.length()) {
            pClient = new PipeClient(pipename);
            pClient->Init();
        }
    }

    GpuAdaptersMgr* m_pMgr;
    m_pMgr = new GpuAdaptersMgr();
    if (!m_pMgr || !m_pMgr->Init()) {
        ErrorHandler("Start GpuAdaptersMgr", GetLastError());
        return 0;
    }

    if (pClient) {
        pClient->WaitRunning();
    }
    else {
        while (getchar() != 'q');
    }

    m_pMgr->Close();
    delete m_pMgr;
    m_pMgr = NULL;

    if (pClient) {
        pClient->Close();
        delete pClient;
        pClient = NULL;
    }
    return 0;
}
