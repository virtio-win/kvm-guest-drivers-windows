#pragma once

#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <string.h>
#include <tchar.h>
#include <cguid.h>
#include <WtsApi32.h>
#include <winioctl.h>
#include <netcfgx.h>
#include <cfgmgr32.h>
#include <ndisguid.h>
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <devguid.h>
#include <setupapi.h>

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS      // some CString constructors will be explicit
//#include <atlbase.h>
//#include <atlcoll.h>
#include <atlstr.h>
#include <atlcoll.h>

#include "Log.h"
#include "Names.h"
#include "..\Common\netkvmd.h"
#include "Service.h"
#include "CProcessRunner.h"

void ProcessProtocolUninstall();
