#define _CRT_SECURE_NO_WARNINGS

#include "stdafx.h"

CBridge::CBridge()
{
    m_pService = NULL;
    m_hThread = NULL;
    m_evtInitialized = NULL;
    m_evtTerminate = NULL;

    m_VsockListenSocket = NULL;
    m_VsockClientSocket = NULL;
    m_TcpSocket = NULL;
    m_hThreadV2T = NULL;
    m_hThreadT2V = NULL;
}

CBridge::~CBridge()
{
    Finalize();
}

BOOL CBridge::Init(CService *Service, std::pair<UINT, UINT> portMapping)
{
    m_portMapping = portMapping;

    m_evtInitialized = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!m_evtInitialized)
    {
        return FALSE;
    }

    m_evtTerminate = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!m_evtTerminate)
    {
        return FALSE;
    }

    m_VsockListenSocket = new CSocket();
    m_VsockClientSocket = new CSocket();
    m_TcpSocket = new CSocket();

    m_pService = Service;

    return TRUE;
}

VOID CBridge::Finalize()
{
    Stop();

    if (m_VsockListenSocket)
    {
        delete m_VsockListenSocket;
        m_VsockListenSocket = nullptr;
    }

    if (m_VsockClientSocket)
    {
        delete m_VsockClientSocket;
        m_VsockClientSocket = nullptr;
    }

    if (m_TcpSocket)
    {
        delete m_TcpSocket;
        m_TcpSocket = nullptr;
    }

    if (m_evtInitialized)
    {
        CloseHandle(m_evtInitialized);
        m_evtInitialized = NULL;
    }

    if (m_evtTerminate)
    {
        CloseHandle(m_evtTerminate);
        m_evtTerminate = NULL;
    }

    m_pService = NULL;
}

DWORD WINAPI CBridge::ThreadV2T(LPVOID lpParam)
{
    CBridge *pBridge = reinterpret_cast<CBridge *>(lpParam);

    CHAR Buffer[0x1000];
    DWORD BufferLen = sizeof(Buffer);
    int recvLen, sendLen;

    while ((recvLen = recv(pBridge->m_VsockClientSocket->GetSocket(), Buffer, BufferLen, 0)) != SOCKET_ERROR)
    {
        sendLen = send(pBridge->m_TcpSocket->GetSocket(), Buffer, recvLen, 0);
        if (sendLen == SOCKET_ERROR)
        {
            break;
        }
    }

    return 0;
}

DWORD WINAPI CBridge::ThreadT2V(LPVOID lpParam)
{
    CBridge *pBridge = reinterpret_cast<CBridge *>(lpParam);

    CHAR Buffer[0x1000];
    DWORD BufferLen = sizeof(Buffer);
    int recvLen, sendLen;

    while ((recvLen = recv(pBridge->m_TcpSocket->GetSocket(), Buffer, BufferLen, 0)) != SOCKET_ERROR)
    {
        sendLen = send(pBridge->m_VsockClientSocket->GetSocket(), Buffer, recvLen, 0);
        if (sendLen == SOCKET_ERROR)
        {
            break;
        }
    }

    return 0;
}

DWORD CBridge::Run()
{
    VIRTIO_VSOCK_CONFIG vsockConfig;
    ADDRESS_FAMILY vsockAF;

    HANDLE hDevice = CreateFile(VIOSOCK_NAME,
                                GENERIC_WRITE,
                                0,
                                NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                NULL);

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        PrintMessage("Failed to create file.");
        return GetLastError();
    }

    HCMNOTIFICATION devnotify = m_pService->RegisterDeviceHandleNotification(hDevice);

    if (!devnotify)
    {
        DWORD err = GetLastError();
        PrintMessage("Failed to register handle notification.");
        CloseHandle(hDevice);
        return err;
    }

    SetEvent(m_evtInitialized);

    while (WAIT_OBJECT_0 != WaitForSingleObject(m_evtTerminate, 0))
    {
        while (!ViosockGetConfig(&vsockConfig))
        {
            PrintMessage("Failed to get VSock config");
            Sleep(1000);
        }
        while ((vsockAF = ViosockGetAF()) == AF_UNSPEC)
        {
            PrintMessage("Failed to get VSock address family");
            Sleep(1000);
        }

        m_VsockListenSocket->SetSocket(InitializeVsockListen(vsockAF, vsockConfig.guest_cid));
        if (m_VsockListenSocket->GetSocket() == INVALID_SOCKET)
        {
            continue;
        }

        m_VsockClientSocket->SetSocket(AcceptVsockConnection());
        if (m_VsockClientSocket->GetSocket() == INVALID_SOCKET)
        {
            m_VsockListenSocket->Cleanup();
            continue;
        }

        m_TcpSocket->SetSocket(InitializeAndConnectTcp());
        if (m_TcpSocket->GetSocket() == INVALID_SOCKET)
        {
            m_VsockClientSocket->Cleanup();
            m_VsockListenSocket->Cleanup();
            continue;
        }

        DWORD v2tId, t2vId;
        m_hThreadV2T = CreateThread(NULL, 0, ThreadV2T, this, 0, &v2tId);
        m_hThreadT2V = CreateThread(NULL, 0, ThreadT2V, this, 0, &t2vId);
        PrintMessage("Two V <=> T threads started");

        HANDLE hThreadArray[] = {m_hThreadV2T, m_hThreadT2V};
        WaitForMultipleObjects(2, hThreadArray, FALSE, INFINITE);
        PrintMessage("At least one V <=> T thread exited");

        // VSock does not support async operation, so V <=> T threads are
        // working with sync & blocked API.
        // Cleanup open sockets to cause recv/send fail on both threads.
        m_VsockClientSocket->Cleanup();
        m_VsockListenSocket->Cleanup();
        m_TcpSocket->Cleanup();

        WaitForMultipleObjects(2, hThreadArray, TRUE, INFINITE);
        PrintMessage("Two V <=> T thread exited\n");
    }

    m_VsockClientSocket->Cleanup();
    m_VsockListenSocket->Cleanup();
    m_TcpSocket->Cleanup();

    m_pService->UnregisterNotification(devnotify);
    CloseHandle(hDevice);

    return NO_ERROR;
}

BOOL CBridge::Start()
{
    DWORD tid, waitrc;

    if (!m_hThread)
    {
        m_hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)BridgeThread, (LPVOID)this, 0, &tid);
        if (!m_hThread)
        {
            return FALSE;
        }

        HANDLE waitfor[] = {m_evtInitialized, m_hThread};
        waitrc = WaitForMultipleObjects(sizeof(waitfor) / sizeof(waitfor[0]), waitfor, FALSE, INFINITE);
        if (waitrc != WAIT_OBJECT_0)
        {
            // the thread failed to initialize
            CloseHandle(m_hThread);
            m_hThread = NULL;
        }
    }

    return TRUE;
}

VOID CBridge::Stop()
{
    if (m_hThread)
    {
        SetEvent(m_evtTerminate);

        // VSock does not support async operation, so V <=> T threads are
        // working with sync & blocked API.
        // m_evtTerminate evant has no effect on V <=> T threads.
        // Cleanup open sockets to cause recv/send fail on both threads.
        m_VsockClientSocket->Cleanup();
        m_VsockListenSocket->Cleanup();
        m_TcpSocket->Cleanup();

        if (WaitForSingleObject(m_hThread, 1000) == WAIT_TIMEOUT)
        {
            TerminateThread(m_hThread, 0);
        }

        CloseHandle(m_hThreadT2V);
        m_hThreadT2V = NULL;
        CloseHandle(m_hThreadV2T);
        m_hThreadV2T = NULL;

        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
}

DWORD WINAPI CBridge::BridgeThread(LPDWORD lParam)
{
    CBridge *pBridge = reinterpret_cast<CBridge *>(lParam);
    return pBridge->Run();
}

SOCKET CBridge::InitializeVsockListen(ADDRESS_FAMILY af, UINT cid)
{
    SOCKADDR_VM vsockAddr = {0};
    int wsaerror = 0;

    SOCKET listenSocket = socket(af, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET)
    {
        wsaerror = WSAGetLastError();
        PrintMessage("Failed to create VSock socket");
        return INVALID_SOCKET;
    }

    vsockAddr.svm_family = af;
    vsockAddr.svm_cid = cid;
    vsockAddr.svm_port = m_portMapping.first;

    if (ERROR_SUCCESS != bind(listenSocket, (struct sockaddr *)&vsockAddr, sizeof(vsockAddr)))
    {
        wsaerror = WSAGetLastError();
        PrintMessage("Failed to bind VSock socket");
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;

        return INVALID_SOCKET;
    }

    if (ERROR_SUCCESS != listen(listenSocket, SOMAXCONN))
    {
        wsaerror = WSAGetLastError();
        PrintMessage("Failed to listen VSock socket");
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;

        return INVALID_SOCKET;
    }

    return listenSocket;
}

SOCKET CBridge::AcceptVsockConnection()
{
    int wsaerror = 0;
    SOCKADDR_VM rAddr = {0};
    int len = sizeof(rAddr);

    SOCKET vsockClient = accept(m_VsockListenSocket->GetSocket(), (struct sockaddr *)&rAddr, &len);
    if (vsockClient == INVALID_SOCKET)
    {
        wsaerror = WSAGetLastError();
        PrintMessage("Failed to accept VSock client socket");

        return INVALID_SOCKET;
    }

    return vsockClient;
}

SOCKET CBridge::InitializeAndConnectTcp()
{
    int result = 0;
    sockaddr_in localhostAddr = {0};
    int sizeofaddr = sizeof(localhostAddr);
    localhostAddr.sin_family = AF_INET;
    localhostAddr.sin_port = htons((u_short)m_portMapping.second);
    int wsaerror;

    result = InetPton(AF_INET, L"127.0.0.1", &localhostAddr.sin_addr.s_addr);
    if (result != 1)
    {
        PrintMessage("Failed to create localhost addr");
        return INVALID_SOCKET;
    }

    SOCKET tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcpSocket == INVALID_SOCKET)
    {
        wsaerror = WSAGetLastError();
        PrintMessage("Failed to create TCP IPv4 socket");
        return INVALID_SOCKET;
    }

    result = connect(tcpSocket, (sockaddr *)&localhostAddr, sizeofaddr);
    if (result == SOCKET_ERROR)
    {
        wsaerror = WSAGetLastError();
        closesocket(tcpSocket);
        PrintMessage("Failed to connect TCP IPv4 socket");
        return INVALID_SOCKET;
    }

    return tcpSocket;
}