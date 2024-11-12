#pragma once

#include <ShlObj.h>
#include <WinSock2.h>
#include <Windows.h>
#include <WtsApi32.h>
#include <cfgmgr32.h>
#include <cguid.h>
#include <devguid.h>
#include <iphlpapi.h>
#include <ndisguid.h>
#include <netcfgx.h>
#include <process.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <winioctl.h>
#include <ws2ipdef.h>

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS // some CString constructors will be explicit
// #include <atlbase.h>
// #include <atlcoll.h>
#include <atlcoll.h>
#include <atlstr.h>

#include "..\Common\netkvmd.h"
#include "CProcessRunner.h"
#include "Log.h"
#include "Names.h"
#include "Service.h"

void ProcessProtocolUninstall();
