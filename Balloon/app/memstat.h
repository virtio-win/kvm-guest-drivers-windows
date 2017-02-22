#ifndef MEMSTAT_H
#define MEMSTAT_H

#include "Wbemidl.h"
#include "atlbase.h"
#include "comdef.h"
#include "comutil.h"
#include "public.h"

class CMemStat {
public:
    CMemStat();
    ~CMemStat();
    BOOL Init();
    BOOL Update();

    PVOID GetBuffer() {
        return m_Stats;
    }

    size_t GetSize() {
        return sizeof(m_Stats);
    }

private:
    BOOL initialized;
    CComPtr< IWbemLocator > locator;
    CComPtr< IWbemServices > service;
    BALLOON_STAT m_Stats[VIRTIO_BALLOON_S_NR];
};

#endif
