#ifndef MEMSTAT_H
#define MEMSTAT_H

#include "Wbemidl.h"
#include "coguid.h"
#include "atlbase.h"
#include "comutil.h"
#include "public.h"

class CMemStat {
public:
    CMemStat();
    ~CMemStat();
    BOOL Init();
    BOOL GetStatus(PBALLOON_STAT pstat);
private:
    BOOL initialized;
    CComPtr< IWbemLocator > locator;
    CComPtr< IWbemServices > service;
};

#endif
