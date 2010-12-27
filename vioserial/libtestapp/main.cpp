#include "vioser.h"
#include <stdio.h>
#include "tchar.h"

void ShowUsage()
{
    printf("\n");
    printf("USAGE:\n");
    printf("blnsvr -r\tRead Test\n");
    printf("blnsvr -w\tWrite Test\n");
    printf("blnsvr -i\tPort Status\n");
    printf("\n");
}


VOID ReadTest( UINT id)
{
    PVOID port = OpenPortById(0);
    if (port)
    {
        ULONG test_size = 1024*1024*2;
        PBYTE data = (PBYTE) new BYTE[test_size];
        memset (data, '\0', test_size);
        if (!ReadPort(port, data, &test_size))
        {
           printf ("ReadTest Error.\n");
        }
        else if (test_size == 0)
        {
           printf ("No data!\n");
        }
        else
        {
           printf ("%s\n", data);
        }
        delete[] data;
        return;
    }
    printf ("ReadTest Error. Invalid port index.\n");
}

VOID InfoTest( UINT id)
{
    PVOID port = OpenPortById(0);
    if (port)
    {
        //
        return;
    }
    printf ("InfoTest Error. Invalid port index.\n");
}

VOID WriteTest( UINT id)
{
    PVOID port = OpenPortById(0);
    if (port)
    {
        ULONG test_size = 1024*1024*2;
        PBYTE data = (PBYTE) new BYTE[test_size];
        memset (data, '0', test_size);
        if (!WritePort(port, data, test_size))
        {
           printf ("WriteTest Error.\n");
        }
        delete[] data;
        return;
    }
    printf ("WriteTest Error. Invalid port index.\n");
}


ULONG
_cdecl
wmain(
    __in              ULONG argc,
    __in_ecount(Argc) PWCHAR argv[]
    )
{
    VIOSStartup();
    UINT ports = NumPorts();
    if(argc == 2 && ports > 0)
    {
        if (_tcsicmp(L"-r", argv[1]) == 0) {
           ReadTest(0);
        } else if (_tcsicmp(L"-w", argv[1]) == 0) {
           WriteTest(0);
        } else if (_tcsicmp(L"-i", argv[1]) == 0) {
           InfoTest(0);
        } else if (_tcsicmp(L"help", argv[1]) == 0) {
           ShowUsage();
        } else {
           ShowUsage();
        }
    } else {
      printf ("we have %d port(s) in the system.\n", ports);
    }

    VIOSCleanup();
    return 0;
}
