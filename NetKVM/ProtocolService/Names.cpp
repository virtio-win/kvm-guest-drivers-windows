#include "stdafx.h"

typedef struct _tNamedEntry
{
    ULONG value;
    LPCSTR name;
}tNamedEntry;

#define MAKE_ENTRY(e) { e, #e},
#define GET_NAME(table, val) GetName(table, ELEMENTS_IN(table), val)

static LPCSTR GetName(const tNamedEntry* table, UINT size, ULONG val)
{
    for (UINT i = 0; i < size; ++i)
    {
        if (table[i].value == val) return table[i].name;
    }
    return "Unknown";
}

LPCSTR GetName(const eServiceControl& val)
{
    static tNamedEntry names[] = {
        MAKE_ENTRY(SERVICE_CONTROL_STOP)
        MAKE_ENTRY(SERVICE_CONTROL_PAUSE)
        MAKE_ENTRY(SERVICE_CONTROL_CONTINUE)
        MAKE_ENTRY(SERVICE_CONTROL_INTERROGATE)
        MAKE_ENTRY(SERVICE_CONTROL_SHUTDOWN)
        MAKE_ENTRY(SERVICE_CONTROL_PARAMCHANGE)
        MAKE_ENTRY(SERVICE_CONTROL_NETBINDADD)
        MAKE_ENTRY(SERVICE_CONTROL_NETBINDREMOVE)
        MAKE_ENTRY(SERVICE_CONTROL_NETBINDENABLE)
        MAKE_ENTRY(SERVICE_CONTROL_NETBINDDISABLE)
        MAKE_ENTRY(SERVICE_CONTROL_DEVICEEVENT)
        MAKE_ENTRY(SERVICE_CONTROL_HARDWAREPROFILECHANGE)
        MAKE_ENTRY(SERVICE_CONTROL_POWEREVENT)
        MAKE_ENTRY(SERVICE_CONTROL_SESSIONCHANGE)
        MAKE_ENTRY(SERVICE_CONTROL_PRESHUTDOWN)
        MAKE_ENTRY(SERVICE_CONTROL_TIMECHANGE)
        MAKE_ENTRY(SERVICE_CONTROL_TRIGGEREVENT)
    };
    return GET_NAME(names, val);
}
