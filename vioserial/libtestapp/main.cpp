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
    UINT ports = NumPorts();
    printf ("we have %d port(s) in the system.\n", ports);
    if (ports)
    {
        PVOID port = OpenPortById(0);
        if (port)
        {
           UINT test_size = 1024*1024*2;
           PBYTE data = (PBYTE) new BYTE[test_size];
           memset (data, '0', test_size);
           WritePort(port, data, test_size);
           delete[] data;
        }
    }
    VIOSCleanup();
    return 0;
}
