#pragma once

template <typename t> LPCSTR Name(ULONG val)
{
    LPCSTR GetName(const t&);
    t a;
    a = (t)val;
    return GetName(a);
}

typedef enum { eDummy1 = SERVICE_CONTROL_STOP } eServiceControl;

