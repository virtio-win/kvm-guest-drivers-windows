// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <dbt.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <strsafe.h>

#ifdef UNIVERSAL
#include <cfgmgr32.h>
#endif // UNIVERSAL

#include "targetver.h"
#include "utils.h"
#include "service.h"
#include "device.h"
#include "memstat.h"
#include "public.h"
