// ConsoleSim.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "ConsoleSim.h"
#include "testcommands.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// The one and only application object

CWinApp theApp;

using namespace std;

static FILE *f;

EXTERN_C void DoPrint(const char *format, ...)
{
    va_list list;
    va_start(list, format);
    vprintf(format, list);
    if (!f) f = fopen("test.log", "w+t");
    if (f) vfprintf(f, format, list);
    if (f) fflush(f);
}

void OnScriptEvent(PVOID ref, tScriptEvent evt, const char *format, ...)
{
    switch(evt)
    {
    case escriptEvtPreprocessOK:
        break;
    case escriptEvtPreprocessFail:
        break;
    case escriptEvtPreprocessStep:
        break;
    case escriptEvtProcessStep:
        break;
    case escriptEvtProcessOK:
        break;
    case escriptEvtProcessFail:
        break;
    default:
        break;
    }
}


int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
    int nRetCode = 0;

    HMODULE hModule = ::GetModuleHandle(NULL);

    if (hModule != NULL)
    {
        // initialize MFC and print and error on failure
        if (!AfxWinInit(hModule, NULL, ::GetCommandLine(), 0))
        {
            // TODO: change error code to suit your needs
            _tprintf(_T("Fatal Error: MFC initialization failed\n"));
            nRetCode = 1;
        }
        else
        {
            // TODO: code your application's behavior here.
            if (argc > 1)
            {
                RunScript(argv[1], NULL, OnScriptEvent);
            }
            else
            {
                RunScript("test.txt", NULL, OnScriptEvent);
            }
        }
    }
    else
    {
        // TODO: change error code to suit your needs
        _tprintf(_T("Fatal Error: GetModuleHandle failed\n"));
        nRetCode = 1;
    }

    return nRetCode;
}


