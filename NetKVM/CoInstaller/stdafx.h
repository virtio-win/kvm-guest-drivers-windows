// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "resource.h"
#include "targetver.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#endif

// Windows Header Files:
#pragma warning(push, 3)
#include "netkvmco.h"
#include <Cfgmgr32.h>
#include <setupapi.h>
#include <stdio.h>
#include <strsafe.h>
#include <tchar.h>
#include <windows.h>
#include <winscard.h>
#pragma warning(pop)

#ifdef __cplusplus
#include "Exception.h"
#include "tstrings.h"

#pragma warning(push, 3)
#pragma warning(disable : 4995) // name was marked as #pragma deprecated
#include <algorithm>
#include <vector>
#pragma warning(pop)

using namespace std;
#endif
#pragma warning(disable : 4512) // assignment operator could not be generated

#ifdef _DEBUG
#define NETCO_DEBUG_PRINT(x)                                                                                           \
    {                                                                                                                  \
        tcout << TEXT("NETKVM: ") << x << endl;                                                                        \
    }
#else
#define NETCO_DEBUG_PRINT(x)                                                                                           \
    {                                                                                                                  \
        wstringbuf sb;                                                                                                 \
        wostream s(&sb);                                                                                               \
        s << TEXT("NETKVM: ") << x << endl;                                                                            \
        OutputDebugString(sb.str().c_str());                                                                           \
    }
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

// TODO: reference additional headers your program requires here
