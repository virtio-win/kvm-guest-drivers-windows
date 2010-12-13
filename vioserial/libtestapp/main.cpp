#include "vioser.h"
#include <stdio.h>

ULONG
_cdecl
wmain(
    __in              ULONG argc,
    __in_ecount(Argc) PWCHAR argv[]
    )
{
    VIOSStartup();
    printf ("we have %d port(s) in the system.\n", NumPorts());
    VIOSCleanup();
    return 0;
}
