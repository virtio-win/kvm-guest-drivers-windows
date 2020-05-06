// viosocklib-test.c : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "installprov.h"

VOID
Usage()
{
    _tprintf(_T("Usage:\tviosocklib-test.exe /[i|d|e]\n\
\tviosocklib-test.exe /[c|l] [cid:]port filepath\n\
\n\
\t/i - install viosocklib.dll as Virtio Socket Provider (administrative rights required)\n\
\t/d - deinstall Virtio Socket Provider (administrative rights required)\n\
\t/e - enum installed protocols\n\
\n\
\t/c - perform connect-send-recv cycle\n\
\t/l - perform listen-accept-recv-send cycle\n\
\t\tport - port number\n\
\t\tfilepath - full path to file to stor recieved or send stored data\n\
"));
}

BOOL
ReadBufferFromFile(
    PTCHAR sFileName,
    PVOID *Buffer,
    PULONG BufferLen
)
{
    BOOL bRes = FALSE;
    HANDLE hFile = CreateFile(sFileName, GENERIC_READ,
        FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD dwSize = GetFileSize(hFile, NULL);
        if (dwSize)
        {
            *Buffer = malloc(dwSize);
            if (*Buffer)
            {
                if (ReadFile(hFile, *Buffer, dwSize, BufferLen, NULL))
                {
                    _tprintf(_T("read %d bytes from %s\n"),*BufferLen, sFileName);
                    bRes = TRUE;
                }
                else
                {
                    _tprintf(_T("ReadFile failed: %d\n"), GetLastError());
                    free(*Buffer);
                }
            }
            else
            {
                _tprintf(_T("malloc failed: %d\n"), GetLastError());
            }
       }
        else
        {
            _tprintf(_T("Empty file specified\n"));
        }
        CloseHandle(hFile);
    }
    else
    {
        _tprintf(_T("CreateFile failed: %d\n"), GetLastError());
    }
    return bRes;
}

BOOL
AddBufferToFile(
    PTCHAR sFileName,
    PVOID Buffer,
    ULONG BufferLen
)
{
    BOOL bRes = FALSE;
    HANDLE hFile = CreateFile(sFileName, GENERIC_WRITE,
        0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD dwSize = SetFilePointer(hFile, 0, NULL, FILE_END);

        if (dwSize != INVALID_SET_FILE_POINTER)
        {
            if (WriteFile(hFile, Buffer, BufferLen, &BufferLen, NULL))
            {
                _tprintf(_T("Add %d bytes to %s with %d offset\n"), BufferLen, sFileName, dwSize);
                bRes = TRUE;
            }
            else
            {
                _tprintf(_T("WriteFile failed: %d\n"), GetLastError());
            }
        }
        else
        {
            _tprintf(_T("SetFilePointer failed: %d\n"), GetLastError());
        }

        CloseHandle(hFile);
    }
    else
    {
        _tprintf(_T("CreateFile failed: %d\n"), GetLastError());
    }
    return bRes;
}

BOOL
Send(
    SOCKET sock,
    PCHAR Buffer,
    DWORD *BufferLen
)
{
    while (BufferLen)
    {
        int len = send(sock, (char*)Buffer, *BufferLen, 0);
        if (len == SOCKET_ERROR)
        {
            _tprintf(_T("send failed: %d\n"), WSAGetLastError());
            return FALSE;
        }
        else if (!len)
        {
            _tprintf(_T("connection closed\n"));
            return TRUE;
        }
        else
        {
            _tprintf(_T("%d bytes sent\n"), len);
        }
        *BufferLen -= len;
        Buffer += len;
    }
    return TRUE;
}

BOOL
Recv(
    SOCKET sock,
    PCHAR Buffer,
    DWORD *BufferLen
)
{
    int len = recv(sock, Buffer, *BufferLen, 0);

    if (len == SOCKET_ERROR)
    {
        _tprintf(_T("recv failed: %d\n"), WSAGetLastError());
        return FALSE;
    }
    else
    {
        _tprintf(_T("recv %d bytes\n"), len);
        if (!len)
        {
            _tprintf(_T("connection closed\n"));
        }
        else
        {
            *BufferLen = len;
        }
    }
    return TRUE;
}

int
SocketConnectTest(
    PSOCKADDR_VM    addr,
    PTCHAR          sFileName
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
        if (addr->svm_cid == VMADDR_CID_ANY)
            addr->svm_cid = VMADDR_CID_HOST;

        if (ERROR_SUCCESS == connect(sock, (struct sockaddr*)addr, sizeof(*addr)))
        {
            PVOID Buffer;
            DWORD BufferLen;

            _tprintf(_T("connect success\n"));

            if (ReadBufferFromFile(sFileName, &Buffer, &BufferLen))
            {
                if (Send(sock, Buffer, &BufferLen))
                {
                    UCHAR ackBuffer[0x400];

                    BufferLen = sizeof(ackBuffer);

                    if (Recv(sock, ackBuffer, &BufferLen) && BufferLen)
                    {
                        if (BufferLen == sizeof(ackBuffer))
                            BufferLen--;

                        ackBuffer[BufferLen] = 0;
                        printf("Ack: %s\n", ackBuffer);
                    }
                }
                free(Buffer);
            }
        }
        else
            _tprintf(_T("connect failed: %d\n"), WSAGetLastError());

        shutdown(sock, SD_BOTH);
        closesocket(sock);
    }
    else
        _tprintf(_T("socket failed: %d\n"), WSAGetLastError());

    WSACleanup();
    return 0;
}

int
SocketListenTest(
    PSOCKADDR_VM    addr,
    PTCHAR          sFileName
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

    if (addr->svm_port == VMADDR_PORT_ANY)
    {
        _tprintf(_T("Invalid port for listen\n"));
        return 2;
    }

    _tprintf(_T("socket(AF_VSOCK, SOCK_STREAM, 0)\n"));
    sock = socket(AF_VSOCK, SOCK_STREAM, 0);
    if (sock != INVALID_SOCKET)
    {
        if (ERROR_SUCCESS == bind(sock, (struct sockaddr*)addr, sizeof(*addr)))
        {
            if (ERROR_SUCCESS == listen(sock, 10))
            {
                SOCKADDR_VM rAddr = { 0 };
                int len = sizeof(rAddr);
                SOCKET  aSock;

                _tprintf(_T("listen success\n"));

                aSock = accept(sock, (struct sockaddr*)&rAddr, &len);
                if (aSock == INVALID_SOCKET)
                {
                    _tprintf(_T("accept failed: %d\n"), WSAGetLastError());
                }
                else
                {
                    BYTE Buffer[0x1000];
                    DWORD BufferLen = sizeof(Buffer);

                    _tprintf(_T("accepted from: %d:%d\n"), rAddr.svm_cid, rAddr.svm_port);

                    if (Recv(aSock, Buffer, &BufferLen) && BufferLen)
                    {
                        if (!AddBufferToFile(sFileName, Buffer, BufferLen))
                        {
                            _tprintf(_T("AddBufferToFile failed: %d\n"), WSAGetLastError());
                        }

                        BufferLen = sizeof("Recv: OK") - 1;
                        Send(aSock, "Recv: OK", &BufferLen);
                    }

                    shutdown(aSock, SD_BOTH);
                    closesocket(aSock);
                }
            }
            else
                _tprintf(_T("listen failed: %d\n"), WSAGetLastError());
        }
        else
            _tprintf(_T("bind failed: %d\n"), WSAGetLastError());

        closesocket(sock);
    }
    else
        _tprintf(_T("socket failed: %d\n"), WSAGetLastError());

    WSACleanup();
    return 0;
}

BOOL
ParseAddr(
    PTCHAR  AddrString,
    PSOCKADDR_VM addr
)
{
    addr->svm_cid = VMADDR_CID_ANY;
    addr->svm_port = VMADDR_PORT_ANY;

    if (AddrString)
    {
        PTCHAR pSep = _tcschr(AddrString, _T(':'));
        if (pSep)
        {
            *pSep = 0;
            addr->svm_cid = _tstoi(AddrString);
            if (!addr->svm_cid)
            {
                _tprintf(_T("Invalid cid: %s\n"), AddrString);
                return FALSE;
            }

            addr->svm_port = _tstoi(++pSep);
            if (!addr->svm_port)
            {
                _tprintf(_T("Invalid port: %s\n"), pSep);
                return FALSE;
            }
        }
        else
        {
            addr->svm_port = _tstoi(AddrString);
            if (!addr->svm_port)
            {
                _tprintf(_T("Invalid port: %s\n"), AddrString);
                return FALSE;
            }
        }
    }

    addr->svm_family = AF_VSOCK;
    return TRUE;
}

int __cdecl _tmain(int argc, _TCHAR* argv[])
{
    BOOL bUsage = FALSE;
    int iRes = 0;
    SOCKADDR_VM addr = { 0 };

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
        if (argc >= 4)
        {
            if (ParseAddr(argv[2], &addr))
            {
                iRes = SocketConnectTest(&addr, argv[3]);
            }
            else
                iRes = 5;
        }
        else
        {
            Usage();
        }
        break;
    case _T('l'):
        if (argc >= 4)
        {
            if (ParseAddr(argv[2], &addr))
            {
                iRes = SocketListenTest(&addr, argv[3]);
            }
            else
                iRes = 5;
        }
        else
        {
            Usage();
        }
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
