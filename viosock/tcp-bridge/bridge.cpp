#define _CRT_SECURE_NO_WARNINGS

#include "stdafx.h"

#if defined(EVENT_TRACING)
#include "bridge.tmh"
#endif

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
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_THREAD_VT, "Thread V -> T started\n");
    CBridge *pBridge = reinterpret_cast<CBridge *>(lpParam);

    CHAR Buffer[0x1000];
    DWORD BufferLen = sizeof(Buffer);
    int recvLen, sendLen;

    while ((recvLen = recv(pBridge->m_VsockClientSocket->GetSocket(), Buffer, BufferLen, 0)) != SOCKET_ERROR)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_THREAD_VT_TRANSFER, "V -> T: recv %d bytes\n", recvLen);
        sendLen = send(pBridge->m_TcpSocket->GetSocket(), Buffer, recvLen, 0);
        if (sendLen == SOCKET_ERROR)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_THREAD_VT_TRANSFER, "V -> T: send failed - 0x%x\n", WSAGetLastError());
            break;
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_THREAD_VT_TRANSFER, "V -> T: %d bytes sent\n", sendLen);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_THREAD_VT, "Thread V -> T exited\n");
    return 0;
}

DWORD WINAPI CBridge::ThreadT2V(LPVOID lpParam)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_THREAD_VT, "Thread T -> V started\n");
    CBridge *pBridge = reinterpret_cast<CBridge *>(lpParam);

    CHAR Buffer[0x1000];
    DWORD BufferLen = sizeof(Buffer);
    int recvLen, sendLen;

    while ((recvLen = recv(pBridge->m_TcpSocket->GetSocket(), Buffer, BufferLen, 0)) != SOCKET_ERROR)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_THREAD_VT_TRANSFER, "T -> V: recv %d bytes\n", recvLen);
        sendLen = send(pBridge->m_VsockClientSocket->GetSocket(), Buffer, recvLen, 0);
        if (sendLen == SOCKET_ERROR)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_THREAD_VT_TRANSFER, "T -> V: send failed - 0x%x\n", WSAGetLastError());
            break;
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_THREAD_VT_TRANSFER, "T -> V: %d bytes sent\n", sendLen);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_THREAD_VT, "Thread T -> V exited\n");
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
        DWORD err = GetLastError();
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Failed to create VIOSOCK file notification - 0x%x\n", err);
        return err;
    }

    HCMNOTIFICATION devnotify = m_pService->RegisterDeviceHandleNotification(hDevice);

    if (!devnotify)
    {
        DWORD err = GetLastError();
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Failed to register handle notification - 0x%x\n", err);

        CloseHandle(hDevice);
        return err;
    }

    SetEvent(m_evtInitialized);

    while (WAIT_OBJECT_0 != WaitForSingleObject(m_evtTerminate, 0))
    {
        while (!ViosockGetConfig(&vsockConfig))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Failed to get VSock config\n");
            Sleep(1000);
        }
        while ((vsockAF = ViosockGetAF()) == AF_UNSPEC)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Failed to get VSock address family\n");
            Sleep(1000);
        }

        m_VsockListenSocket->SetSocket(InitializeVsockListen(vsockAF, vsockConfig.guest_cid));
        if (m_VsockListenSocket->GetSocket() == INVALID_SOCKET)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "InitializeVsockListen failed - 0x%x\n", WSAGetLastError());
            continue;
        }

        m_VsockClientSocket->SetSocket(AcceptVsockConnection());
        if (m_VsockClientSocket->GetSocket() == INVALID_SOCKET)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "AcceptVsockConnection failed - 0x%x\n", WSAGetLastError());
            m_VsockListenSocket->Cleanup();
            continue;
        }

        m_TcpSocket->SetSocket(InitializeAndConnectTcp());
        if (m_TcpSocket->GetSocket() == INVALID_SOCKET)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "InitializeAndConnectTcp failed - 0x%x\n", WSAGetLastError());
            m_VsockClientSocket->Cleanup();
            m_VsockListenSocket->Cleanup();
            continue;
        }

        DWORD v2tId, t2vId;
        m_hThreadV2T = CreateThread(NULL, 0, ThreadV2T, this, 0, &v2tId);
        m_hThreadT2V = CreateThread(NULL, 0, ThreadT2V, this, 0, &t2vId);
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_THREAD_VT,
                    "Two V <=> T threads created: V2T Id = %d, T2V Id = %d\n",
                    v2tId,
                    t2vId);

        HANDLE hThreadArray[] = {m_hThreadV2T, m_hThreadT2V};
        WaitForMultipleObjects(2, hThreadArray, FALSE, INFINITE);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_THREAD_VT, "At least one V <=> T thread exited\n");

        // VSock does not support async operation, so V <=> T threads are
        // working with sync & blocked API.
        // Cleanup open sockets to cause recv/send fail on both threads.
        m_VsockClientSocket->Cleanup();
        m_VsockListenSocket->Cleanup();
        m_TcpSocket->Cleanup();

        WaitForMultipleObjects(2, hThreadArray, TRUE, INFINITE);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_THREAD_VT, "Two V <=> T thread exited\n");
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
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_INIT,
                    "Failed to create VSock socket - 0x%x, af = %d\n",
                    WSAGetLastError(),
                    af);
        return INVALID_SOCKET;
    }

    vsockAddr.svm_family = af;
    vsockAddr.svm_cid = cid;
    vsockAddr.svm_port = m_portMapping.first;

    if (ERROR_SUCCESS != bind(listenSocket, (struct sockaddr *)&vsockAddr, sizeof(vsockAddr)))
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_INIT,
                    "Failed to bind VSock socket - 0x%x, af = %d, cid = %d, port = %d\n",
                    WSAGetLastError(),
                    af,
                    cid,
                    m_portMapping.first);

        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;

        return INVALID_SOCKET;
    }

    if (ERROR_SUCCESS != listen(listenSocket, SOMAXCONN))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Failed to listen VSock socket - 0x%x\n", WSAGetLastError());

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
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Failed to accept VSock client socket - 0x%x\n", WSAGetLastError());
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
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Failed to create localhost addr - 0x%x\n", WSAGetLastError());

        return INVALID_SOCKET;
    }

    SOCKET tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcpSocket == INVALID_SOCKET)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Failed to create TCP IPv4 socket - 0x%x\n", WSAGetLastError());
        return INVALID_SOCKET;
    }

    result = connect(tcpSocket, (sockaddr *)&localhostAddr, sizeofaddr);
    if (result == SOCKET_ERROR)
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_INIT,
                    "Failed to connect TCP IPv4 socket - 0x%x, port = %d\n",
                    WSAGetLastError(),
                    m_portMapping.second);

        closesocket(tcpSocket);
        return INVALID_SOCKET;
    }

    return tcpSocket;
}