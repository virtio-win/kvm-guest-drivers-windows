// viosocklib-test.c : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "installprov.h"

VOID
Usage()
{
    _tprintf(_T("Usage:\tviosocklib-test.exe /[i|d|e]\n\
\tviosocklib-test.exe /[c|l] port filepath\n\
\n\
\t/i - install viosocklib.dll as Virtio Socket Provider (administrative rights required)\n\
\t/d - deinstall Virtio Socket Provider (administrative rights required)\n\
\t/e - enum installed protocols\n\
\n\
\t/c - perform connect-send-recv cycle with host\n\
\t/l - perform listen and recv data from host while connection is opened\n\
\t\tport - port number\n\
\t\tfilepath - full path to file to stor recieved or send stored data\n\
"));
}

int
SocketTest(
    PTCHAR op,
    int argc,
    _TCHAR* argv[]
)
{
    SOCKET sock = INVALID_SOCKET;
    WSADATA wsaData = { 0 };
    int iRes = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (iRes != ERROR_SUCCESS)
    {
        _tprintf(_T("WSAStartup failed: %d\n"), iRes);
        return 1;
    }

    _tprintf(_T("socket(AF_VSOCK, SOCK_STREAM, 0)\n"));
    sock = socket(AF_VSOCK, SOCK_STREAM, 0);
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
    }
    else
        _tprintf(_T("socket failed: %d\n"), WSAGetLastError());

    WSACleanup();
    return 0;
}

int __cdecl _tmain(int argc, _TCHAR* argv[])
{
    BOOL bUsage = FALSE;
    int iRes = 0;

    if (argc < 2 || argv[1][0]!=_T('/'))
    {
        Usage();
        return 1;
    }

    switch (argv[1][1])
    {
    case _T('i'):
        if (!InstallProtocol())
            iRes = 2;
        break;

    case _T('d'):
        if (!DeinstallProtocol())
            iRes = 3;
        break;

    case _T('e'):
        if (!EnumProtocols())
            iRes = 4;
        break;

    case _T('c'):
    case _T('l'):
        iRes = SocketTest(argv[1], argc - 2, &argv[2]);
        break;

    case _T('?'):
    default:
        bUsage = TRUE;
        break;
    }

    if (bUsage)
        Usage();

    return iRes;
}
