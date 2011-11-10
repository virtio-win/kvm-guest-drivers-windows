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
    printf("blnsvr -n\tPort Notification Test\n");
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
        ClosePort(port);
        delete[] data;
        return;
    }
    printf ("ReadTest Error. Invalid port index.\n");
}

VOID InfoTest( )
{
    UINT nr = NumPorts();
    for (UINT i = 0; i < nr; i++)
    {
        PVOID port = OpenPortById(i);
        if (port)
        {
           printf ("Port index %d.\n", i);
           printf ("\tSymbolic name %ws\n", PortSymbolicName(i));
           ClosePort(port);
        }
    }
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
        ClosePort(port);
        delete[] data;
        return;
    }
    printf ("WriteTest Error. Invalid port index.\n");
}

BOOL NotificationTestFunction(PVOID ptr)
{
    printf ("NotificationTestFunction.\n");
    return TRUE;
}

VOID NotificationTest( UINT id)
{
    PVOID port = OpenPortById(id);
    if (port)
    {
        int test_data = 5;
        RegisterNotification(port, NotificationTestFunction, (PVOID)&test_data);
        while(getchar() != 'q');
        ClosePort(port);
        return;
    }
    printf ("NotificationTest Error. Invalid port index.\n");
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
           InfoTest();
        } else if (_tcsicmp(L"-n", argv[1]) == 0) {
           NotificationTest(0);
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
