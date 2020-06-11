#pragma once

#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <string.h>
#include <tchar.h>
#include <cguid.h>
#include <WtsApi32.h>

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS      // some CString constructors will be explicit
//#include <atlbase.h>
//#include <atlcoll.h>
#include <atlstr.h>
#include <atlcoll.h>

#include "Log.h"
#include "Names.h"
#include "Service.h"
