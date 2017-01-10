// NetKVMDumpParser.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: main.cpp
 *
 * Contains entry point to console application
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "NetKVMDumpParser.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// The one and only application object

CWinApp theApp;

using namespace std;

int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
    int nRetCode = 0;

    // initialize MFC and print and error on failure
    if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0))
    {
        // TODO: change error code to suit your needs
        _tprintf(_T("Fatal Error: MFC initialization failed\n"));
        nRetCode = 1;
    }
    else
    {
        nRetCode = ParseDumpFile(argc, argv);
    }

    return nRetCode;
}
