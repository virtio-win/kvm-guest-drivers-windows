/*
 * Placeholder for the provider functions
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "precomp.h"
#include "viosocklib.h"

#if defined(EVENT_TRACING)
#include "viosocklib.tmh"
#endif

volatile LONG g_StartupRef = 0;
WSAPROTOCOL_INFOW g_ProtocolInfo;
WSPUPCALLTABLE g_UpcallTable;

#define VIOSOCK_PROTOCOL L"Virtio Vsock Protocol"

C_ASSERT(sizeof(VIOSOCK_PROTOCOL) / sizeof(VIOSOCK_PROTOCOL[0]) <= 0xFF);

_Must_inspect_result_
int
WSPAPI
WSPStartup(
    _In_ WORD wVersionRequested,
    _In_ LPWSPDATA lpWSPData,
    _In_ LPWSAPROTOCOL_INFOW lpProtocolInfo,
    _In_ WSPUPCALLTABLE UpcallTable,
    _Out_ LPWSPPROC_TABLE lpProcTable
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    /* Make sure that the version requested is >= 2.2.  */
    if ((LOBYTE(wVersionRequested) < 2) ||
        ((LOBYTE(wVersionRequested) == 2) &&
        (HIBYTE(wVersionRequested) < 2))) {
        return WSAVERNOTSUPPORTED;
    }

    /* Since we only support 2.2, set both wVersion and  */
    /* wHighVersion to 2.2.                              */

    lpWSPData->wVersion = MAKEWORD(2, 2);
    lpWSPData->wHighVersion = MAKEWORD(2, 2);

    _ASSERT(g_StartupRef >= 0);
    if (InterlockedIncrement(&g_StartupRef) == 1)
    {
        memcpy(&g_ProtocolInfo, lpProtocolInfo, sizeof(g_ProtocolInfo));
        g_UpcallTable = UpcallTable;
    }

    StringCbCopy(lpWSPData->szDescription,
        sizeof(lpWSPData->szDescription) - sizeof(lpWSPData->szDescription[0]),
        VIOSOCK_PROTOCOL);

    lpProcTable->lpWSPAccept = VIOSockAccept;
    lpProcTable->lpWSPAddressToString = VIOSockAddressToString;
    lpProcTable->lpWSPAsyncSelect = VIOSockAsyncSelect;
    lpProcTable->lpWSPBind = VIOSockBind;
    lpProcTable->lpWSPCancelBlockingCall = VIOSockCancelBlockingCall;
    lpProcTable->lpWSPCleanup = VIOSockCleanup;
    lpProcTable->lpWSPCloseSocket = VIOSockCloseSocket;
    lpProcTable->lpWSPConnect = VIOSockConnect;
    lpProcTable->lpWSPDuplicateSocket = VIOSockDuplicateSocket;
    lpProcTable->lpWSPEnumNetworkEvents = VIOSockEnumNetworkEvents;
    lpProcTable->lpWSPEventSelect = VIOSockEventSelect;
    lpProcTable->lpWSPGetOverlappedResult = VIOSockGetOverlappedResult;
    lpProcTable->lpWSPGetPeerName = VIOSockGetPeerName;
    lpProcTable->lpWSPGetSockName = VIOSockGetSockName;
    lpProcTable->lpWSPGetSockOpt = VIOSockGetSockOpt;
    lpProcTable->lpWSPGetQOSByName = VIOSockGetQOSByName;
    lpProcTable->lpWSPIoctl = VIOSockIoctl;
    lpProcTable->lpWSPJoinLeaf = VIOSockJoinLeaf;
    lpProcTable->lpWSPListen = VIOSockListen;
    lpProcTable->lpWSPRecv = VIOSockRecv;
    lpProcTable->lpWSPRecvDisconnect = VIOSockRecvDisconnect;
    lpProcTable->lpWSPRecvFrom = VIOSockRecvFrom;
    lpProcTable->lpWSPSelect = VIOSockSelect;
    lpProcTable->lpWSPSend = VIOSockSend;
    lpProcTable->lpWSPSendDisconnect = VIOSockSendDisconnect;
    lpProcTable->lpWSPSendTo = VIOSockSendTo;
    lpProcTable->lpWSPSetSockOpt = VIOSockSetSockOpt;
    lpProcTable->lpWSPShutdown = VIOSockShutdown;
    lpProcTable->lpWSPSocket = VIOSockSocket;
    lpProcTable->lpWSPStringToAddress = VIOSockStringToAddress;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);

    return ERROR_SUCCESS;
}

_Must_inspect_result_
SOCKET
WSPAPI
VIOSockAccept(
    _In_ SOCKET s,
    _Out_writes_bytes_to_opt_(*addrlen, *addrlen) struct sockaddr FAR * addr,
    _Inout_opt_ LPINT addrlen,
    _In_opt_ LPCONDITIONPROC lpfnCondition,
    _In_opt_ DWORD_PTR dwCallbackData,
    _Out_ LPINT lpErrno
)
{
    HANDLE hFile;
    VIRTIO_VSOCK_PARAMS SocketParams = { 0 };

    UNREFERENCED_PARAMETER(lpfnCondition);
    UNREFERENCED_PARAMETER(dwCallbackData);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (s == 0 || s == INVALID_SOCKET)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid listen socket\n");
        *lpErrno = WSAEINVAL;
        return INVALID_SOCKET;
    }

    SocketParams.Socket = s;

    hFile = VIOSockCreateFile(&SocketParams, lpErrno);
    if (INVALID_HANDLE_VALUE == hFile)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockOpenFile failed: %u\n", *lpErrno);
        return INVALID_SOCKET;
    }

    s = g_UpcallTable.lpWPUModifyIFSHandle(g_ProtocolInfo.dwCatalogEntryId, (SOCKET)hFile, lpErrno);
    if (INVALID_SOCKET == s)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "g_UpcallTable.lpWPUModifyIFSHandle failed: %u\n", *lpErrno);
        CloseHandle(hFile);
    }
    else
    {
        VIOSockGetPeerName(s, addr, addrlen, lpErrno);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return s;
}

INT
WSPAPI
VIOSockAddressToString(
    _In_reads_bytes_(dwAddressLength) LPSOCKADDR lpsaAddress,
    _In_ DWORD dwAddressLength,
    _In_opt_ LPWSAPROTOCOL_INFOW lpProtocolInfo,
    _Out_writes_to_(*lpdwAddressStringLength, *lpdwAddressStringLength) LPWSTR lpszAddressString,
    _Inout_ LPDWORD lpdwAddressStringLength,
    _Out_ LPINT lpErrno
)
{
    UNREFERENCED_PARAMETER(lpsaAddress);
    UNREFERENCED_PARAMETER(dwAddressLength);
    UNREFERENCED_PARAMETER(lpProtocolInfo);
    UNREFERENCED_PARAMETER(lpszAddressString);
    UNREFERENCED_PARAMETER(lpdwAddressStringLength);
    UNREFERENCED_PARAMETER(lpErrno);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    return ERROR_SUCCESS;
}

int
WSPAPI
VIOSockAsyncSelect(
    _In_ SOCKET s,
    _In_ HWND hWnd,
    _In_ unsigned int wMsg,
    _In_ long lEvent,
    _Out_ LPINT lpErrno
)
{
    int iRes = -1;

    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(wMsg);
    UNREFERENCED_PARAMETER(lEvent);
    UNREFERENCED_PARAMETER(lpErrno);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    return iRes;
}

int
WSPAPI
VIOSockBind(
    _In_ SOCKET s,
    _In_reads_bytes_(namelen) const struct sockaddr FAR * name,
    _In_ int namelen,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (namelen < sizeof(SOCKADDR_VM))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid namelen\n");
        *lpErrno = WSAEFAULT;
        return SOCKET_ERROR;
    }

    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_BIND, (PVOID)name, (DWORD)namelen, NULL, 0, NULL, lpErrno))
    {
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockCancelBlockingCall(
    _Out_ LPINT lpErrno
)
{
    UNREFERENCED_PARAMETER(lpErrno);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    return ERROR_SUCCESS;
}

int
WSPAPI
VIOSockCleanup(
    _Out_ LPINT lpErrno
)
{
    int iRes = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if (InterlockedDecrement(&g_StartupRef) < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Invalid g_StartupRef value: %d\n", g_StartupRef);

        *lpErrno = WSANOTINITIALISED;
        iRes = -1;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockCloseSocket(
    _In_ SOCKET s,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (!CloseHandle((HANDLE)s))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "CloseHandle failed, socket: %p\n", (PVOID)s);

        *lpErrno = WSAEINVAL;
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockConnect(
    _In_ SOCKET s,
    _In_reads_bytes_(namelen) const struct sockaddr FAR * name,
    _In_ int namelen,
    _In_opt_ LPWSABUF lpCallerData,
    _Out_opt_ LPWSABUF lpCalleeData,
    _In_opt_ LPQOS lpSQOS,
    _In_opt_ LPQOS lpGQOS,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;

    UNREFERENCED_PARAMETER(lpCallerData);
    UNREFERENCED_PARAMETER(lpCalleeData);
    UNREFERENCED_PARAMETER(lpSQOS);
    UNREFERENCED_PARAMETER(lpGQOS);


    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (namelen < sizeof(SOCKADDR_VM))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid namelen\n");
        *lpErrno = WSAEFAULT;
        return SOCKET_ERROR;
    }

    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_CONNECT, (PVOID)name, (DWORD)namelen, NULL, 0, NULL, lpErrno))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockDuplicateSocket(
    _In_ SOCKET s,
    _In_ DWORD dwProcessId,
    _Out_ LPWSAPROTOCOL_INFOW lpProtocolInfo,
    _Out_ LPINT lpErrno
)
{
    UNREFERENCED_PARAMETER(dwProcessId);
    UNREFERENCED_PARAMETER(lpProtocolInfo);
    UNREFERENCED_PARAMETER(lpErrno);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    return ERROR_SUCCESS;
}

int
WSPAPI
VIOSockEnumNetworkEvents(
    _In_ SOCKET s,
    _In_ WSAEVENT hEventObject,
    _Out_ LPWSANETWORKEVENTS lpNetworkEvents,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;
    DWORD dwBytesReturned;
    ULONGLONG ulEvent = (ULONG_PTR)hEventObject;
    VIRTIO_VSOCK_NETWORK_EVENTS NetEvents = { 0 };

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    
    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_ENUM_NET_EVENTS,
        (PVOID)&ulEvent, (DWORD)sizeof(ulEvent),
        &NetEvents, sizeof(NetEvents), &dwBytesReturned, lpErrno))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }
    else if (dwBytesReturned != sizeof(NetEvents))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid output len: %d\n", dwBytesReturned);
        *lpErrno = WSAEINVAL;
        iRes = SOCKET_ERROR;
    }
    else
    {
        DWORD i;
        lpNetworkEvents->lNetworkEvents = NetEvents.NetworkEvents;
        for (i = 0; i < FD_MAX_EVENTS; ++i)
        {
            if (NetEvents.NetworkEvents & 1)
                lpNetworkEvents->iErrorCode[i] = NtStatusToWsaError(NetEvents.Status[i]);
            else
                lpNetworkEvents->iErrorCode[i] = ERROR_SUCCESS;

            NetEvents.NetworkEvents >>= 1;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockEventSelect(
    _In_ SOCKET s,
    _In_opt_ WSAEVENT hEventObject,
    _In_ long lNetworkEvents,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;
    VIRTIO_VSOCK_EVENT_SELECT EventSelect;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    EventSelect.hEventObject = (ULONG_PTR)hEventObject;
    EventSelect.lNetworkEvents = lNetworkEvents;

    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_EVENT_SELECT,
        &EventSelect, (DWORD)sizeof(EventSelect),
        NULL, 0, NULL, lpErrno))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

BOOL
WSPAPI
VIOSockGetOverlappedResult(
    _In_ SOCKET s,
    _In_ LPWSAOVERLAPPED lpOverlapped,
    _Out_ LPDWORD lpcbTransfer,
    _In_ BOOL fWait,
    _Out_ LPDWORD lpdwFlags,
    _Out_ LPINT lpErrno
)
{
    UNREFERENCED_PARAMETER(lpOverlapped);
    UNREFERENCED_PARAMETER(lpcbTransfer);
    UNREFERENCED_PARAMETER(fWait);
    UNREFERENCED_PARAMETER(lpdwFlags);
    UNREFERENCED_PARAMETER(lpErrno);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    return ERROR_SUCCESS;
}

int
WSPAPI
VIOSockGetPeerName(
    _In_ SOCKET s,
    _Out_writes_bytes_to_(*namelen, *namelen) struct sockaddr FAR * name,
    _Inout_ LPINT namelen,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (*namelen < sizeof(SOCKADDR_VM))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid namelen\n");
        *lpErrno = WSAEINVAL;
        return SOCKET_ERROR;
    }

    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_GET_PEER_NAME,
        NULL, 0, (PVOID)name, *namelen, (LPDWORD)namelen, lpErrno))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockGetSockName(
    _In_ SOCKET s,
    _Out_writes_bytes_to_(*namelen, *namelen) struct sockaddr FAR * name,
    _Inout_ LPINT namelen,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (*namelen < sizeof(SOCKADDR_VM))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "Invalid namelen\n");
        *lpErrno = WSAEINVAL;
        return SOCKET_ERROR;
    }

    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_GET_SOCK_NAME,
        NULL, 0, (PVOID)name, *namelen, (LPDWORD)namelen, lpErrno))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockGetSockOpt(
    _In_ SOCKET s,
    _In_ int level,
    _In_ int optname,
    _Out_writes_bytes_(*optlen) char FAR * optval,
    _Inout_ LPINT optlen,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;
    VIRTIO_VSOCK_OPT Opt = { 0 };
    DWORD dwBytesReturned;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (!optlen)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid optlen\n");
        *lpErrno = WSAEINVAL;
        return SOCKET_ERROR;
    }

    Opt.level = level;
    Opt.optname = optname;
    Opt.optval = (ULONGLONG)optval;
    Opt.optlen = *optlen;

    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_GET_SOCK_OPT,
        &Opt, sizeof(Opt), &Opt, sizeof(Opt), &dwBytesReturned, lpErrno))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }
    else if (dwBytesReturned == sizeof(Opt))
    {
        *optlen = Opt.optlen;
    }
    else
    {
        *lpErrno = WSAEINVAL;
        return SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

BOOL
WSPAPI
VIOSockGetQOSByName(
    _In_ SOCKET s,
    _In_ LPWSABUF lpQOSName,
    _Out_ LPQOS lpQOS,
    _Out_ LPINT lpErrno
)
{
    UNREFERENCED_PARAMETER(s);
    UNREFERENCED_PARAMETER(lpQOSName);
    UNREFERENCED_PARAMETER(lpQOS);
    UNREFERENCED_PARAMETER(lpErrno);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    return ERROR_SUCCESS;
}

int
WSPAPI
VIOSockIoctl(
    _In_ SOCKET s,
    _In_ DWORD dwIoControlCode,
    _In_reads_bytes_opt_(cbInBuffer) LPVOID lpvInBuffer,
    _In_ DWORD cbInBuffer,
    _Out_writes_bytes_to_opt_(cbOutBuffer, *lpcbBytesReturned) LPVOID lpvOutBuffer,
    _In_ DWORD cbOutBuffer,
    _Out_ LPDWORD lpcbBytesReturned,
    _Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
    _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
    _In_opt_ LPWSATHREADID lpThreadId,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;
    VIRTIO_VSOCK_IOCTL_IN InParams;

    UNREFERENCED_PARAMETER(lpThreadId);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (lpOverlapped || lpCompletionRoutine)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Overlapped sockets not supported\n");
        *lpErrno = WSAEOPNOTSUPP;
        return SOCKET_ERROR;
    }

    if (dwIoControlCode == SIO_BSP_HANDLE ||
//        dwIoControlCode == SIO_BSP_HANDLE_POLL ||
        dwIoControlCode == SIO_BSP_HANDLE_SELECT)
    {
        if (lpvOutBuffer&&cbOutBuffer >= sizeof(s))
        {
            *(SOCKET*)lpvOutBuffer = s;
            if (lpcbBytesReturned)
                *lpcbBytesReturned = sizeof(s);
            return ERROR_SUCCESS;
        }
        else
        {
            *lpErrno = WSAEFAULT;
            return SOCKET_ERROR;
        }
    }


    InParams.dwIoControlCode = dwIoControlCode;
    InParams.lpvInBuffer = (ULONGLONG)lpvInBuffer;
    InParams.cbInBuffer = cbInBuffer;

    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_IOCTL,
        &InParams, sizeof(InParams), lpvOutBuffer, cbOutBuffer, lpcbBytesReturned, lpErrno))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

SOCKET
WSPAPI
VIOSockJoinLeaf(
    _In_ SOCKET s,
    _In_reads_bytes_(namelen) const struct sockaddr FAR * name,
    _In_ int namelen,
    _In_opt_ LPWSABUF lpCallerData,
    _Out_opt_ LPWSABUF lpCalleeData,
    _In_opt_ LPQOS lpSQOS,
    _In_opt_ LPQOS lpGQOS,
    _In_ DWORD dwFlags,
    _Out_ LPINT lpErrno
)
{
    UNREFERENCED_PARAMETER(name);
    UNREFERENCED_PARAMETER(namelen);
    UNREFERENCED_PARAMETER(lpCallerData);
    UNREFERENCED_PARAMETER(lpCalleeData);
    UNREFERENCED_PARAMETER(lpSQOS);
    UNREFERENCED_PARAMETER(lpGQOS);
    UNREFERENCED_PARAMETER(dwFlags);
    UNREFERENCED_PARAMETER(lpErrno);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    return ERROR_SUCCESS;
}

int
WSPAPI
VIOSockListen(
    _In_ SOCKET s,
    _In_ int backlog,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_LISTEN,
        &backlog, (DWORD)sizeof(backlog), NULL, 0, NULL, lpErrno))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockRecv(
    _In_ SOCKET s,
    _In_reads_(dwBufferCount) LPWSABUF lpBuffers,
    _In_ DWORD dwBufferCount,
    _Out_opt_ LPDWORD lpNumberOfBytesRecvd,
    _Inout_ LPDWORD lpFlags,
    _Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
    _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
    _In_opt_ LPWSATHREADID lpThreadId,
    _In_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;
    DWORD i;

    UNREFERENCED_PARAMETER(lpFlags);
    UNREFERENCED_PARAMETER(lpThreadId);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (lpOverlapped || lpCompletionRoutine)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Overlapped sockets not supported\n");
        *lpErrno = WSAEOPNOTSUPP;
        return SOCKET_ERROR;
    }

    *lpNumberOfBytesRecvd = 0;

    if (!dwBufferCount)
        return ERROR_SUCCESS;

    for (i = 0; i < dwBufferCount; ++i)
    {
        DWORD dwNumberOfBytesRead;
        if (*lpFlags)
        {
            VIRTIO_VSOCK_READ_PARAMS ReadParams;
            ReadParams.Flags = *lpFlags;

            if (!VIOSockDeviceControl(s, IOCTL_SOCKET_READ,
                &ReadParams, (DWORD)sizeof(ReadParams),
                lpBuffers[i].buf, lpBuffers[i].len, &dwNumberOfBytesRead, lpErrno))
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
                iRes = SOCKET_ERROR;
            }
        }
        else if (!VIOSockReadFile(s, lpBuffers[i].buf, lpBuffers[i].len, &dwNumberOfBytesRead, lpErrno))
        {
            iRes = SOCKET_ERROR;
            break;
        }

        *lpNumberOfBytesRecvd += dwNumberOfBytesRead;
        if (dwNumberOfBytesRead != lpBuffers[i].len)
        {
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockRecvDisconnect(
    _In_ SOCKET s,
    _In_opt_ LPWSABUF lpInboundDisconnectData,
    _Out_ LPINT lpErrno
)
{
    UNREFERENCED_PARAMETER(lpInboundDisconnectData);
    UNREFERENCED_PARAMETER(lpErrno);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    return ERROR_SUCCESS;
}

int
WSPAPI
VIOSockRecvFrom(
    _In_ SOCKET s,
    _In_reads_(dwBufferCount) LPWSABUF lpBuffers,
    _In_ DWORD dwBufferCount,
    _Out_opt_ LPDWORD lpNumberOfBytesRecvd,
    _Inout_ LPDWORD lpFlags,
    _Out_writes_bytes_to_opt_(*lpFromlen, *lpFromlen) struct sockaddr FAR * lpFrom,
    _Inout_opt_ LPINT lpFromlen,
    _Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
    _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
    _In_opt_ LPWSATHREADID lpThreadId,
    _Out_ LPINT lpErrno
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (VIOSockGetPeerName(s, lpFrom, lpFromlen, lpErrno) == SOCKET_ERROR)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "VIOSockGetPeerName failed: %u\n", *lpErrno);
        return SOCKET_ERROR;
    }

    return VIOSockRecv(s, lpBuffers, dwBufferCount,
        lpNumberOfBytesRecvd, lpFlags, lpOverlapped,
        lpCompletionRoutine, lpThreadId, lpErrno);
}

static
int
CopyFromFdSet(
    _Out_ VIRTIO_VSOCK_FD_SET *Dst,
    _In_ fd_set *Src
)
{
    UINT i;

    if (!Src)
        return 0;

    if (Src->fd_count > FD_SETSIZE)
        return SOCKET_ERROR;

    Dst->fd_count = Src->fd_count;
    for (i = 0; i < Dst->fd_count; ++i)
    {
        Dst->fd_array[i] = (ULONGLONG)(ULONG_PTR)Src->fd_array[i];
    }

    return Dst->fd_count;
}

static
int
CopyToFdSet(
    _Out_ fd_set *Dst,
    _In_ VIRTIO_VSOCK_FD_SET *Src
)
{
    UINT i;

    _ASSERT(Src->fd_count <= FD_SETSIZE && Src->fd_count <= Dst->fd_count);

    Dst->fd_count = Src->fd_count;
    for (i = 0; i < Dst->fd_count; ++i)
    {
        Dst->fd_array[i] = (SOCKET)(ULONG_PTR)Src->fd_array[i];
    }

    return Dst->fd_count;
}

int
WSPAPI
VIOSockSelect(
    _In_ int nfds,
    _Inout_opt_ fd_set FAR * readfds,
    _Inout_opt_ fd_set FAR * writefds,
    _Inout_opt_ fd_set FAR * exceptfds,
    _In_opt_ const struct timeval FAR * timeout,
    _Out_ LPINT lpErrno
)
{
    int iRes = 0, iReadCount, iWriteCount, iExceptCount;
    VIRTIO_VSOCK_SELECT Select = { 0 };
    HANDLE hFile;
    DWORD dwBytesReturned;

    UNREFERENCED_PARAMETER(nfds);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    iReadCount = CopyFromFdSet(&Select.ReadFds, readfds);
    if (iReadCount == SOCKET_ERROR)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid readfds parameter\n");
        *lpErrno = WSAEINVAL;
        return SOCKET_ERROR;
    }

    iWriteCount = CopyFromFdSet(&Select.WriteFds, writefds);
    if (iWriteCount == SOCKET_ERROR)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid writefds parameter\n");
        *lpErrno = WSAEINVAL;
        return SOCKET_ERROR;
    }

    iExceptCount = CopyFromFdSet(&Select.ExceptFds, exceptfds);
    if (iExceptCount == SOCKET_ERROR)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid exceptfds parameter\n");
        *lpErrno = WSAEINVAL;
        return SOCKET_ERROR;
    }

    if ((iReadCount + iWriteCount + iExceptCount) > FD_SETSIZE)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Input set is too large\n");
        *lpErrno = WSAEINVAL;
        return SOCKET_ERROR;
    }

    if (timeout)
    {
        //timeout in 100-ns intervals
        Select.Timeout.QuadPart = -(SEC_TO_NANO((LONGLONG)timeout->tv_sec) + USEC_TO_NANO(timeout->tv_usec));
    }

    hFile = VIOSockCreateFile(NULL, lpErrno);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        if (!VIOSockDeviceControl((SOCKET)hFile, IOCTL_SELECT,
            &Select, sizeof(Select), &Select, sizeof(Select), &dwBytesReturned, lpErrno))
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
            iRes = SOCKET_ERROR;
        }
        else if (dwBytesReturned == sizeof(Select))
        {
            iRes = CopyToFdSet(readfds, &Select.ReadFds) +
                CopyToFdSet(writefds, &Select.WriteFds) +
                CopyToFdSet(exceptfds, &Select.ExceptFds);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid output len: %u\n", dwBytesReturned);
            *lpErrno = WSAEINVAL;
            iRes = SOCKET_ERROR;
        }

        CloseHandle(hFile);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockCreateFile failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockSend(
    _In_ SOCKET s,
    _In_reads_(dwBufferCount) LPWSABUF lpBuffers,
    _In_ DWORD dwBufferCount,
    _Out_opt_ LPDWORD lpNumberOfBytesSent,
    _In_ DWORD dwFlags,
    _Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
    _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
    _In_opt_ LPWSATHREADID lpThreadId,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;
    DWORD i;

    UNREFERENCED_PARAMETER(dwFlags);
    UNREFERENCED_PARAMETER(lpThreadId);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (lpOverlapped || lpCompletionRoutine)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Overlapped sockets not supported\n");
        *lpErrno = WSAEOPNOTSUPP;
        return SOCKET_ERROR;
    }

    *lpNumberOfBytesSent = 0;

    for (i = 0; i < dwBufferCount; ++i)
    {
        DWORD dwNumberOfBytesWritten;

        if (!VIOSockWriteFile(s, lpBuffers[i].buf, lpBuffers[i].len, &dwNumberOfBytesWritten, lpErrno))
        {
            iRes = SOCKET_ERROR;
            break;
        }

        *lpNumberOfBytesSent += dwNumberOfBytesWritten;
        if (dwNumberOfBytesWritten != lpBuffers[i].len)
        {
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockSendDisconnect(
    _In_ SOCKET s,
    _In_opt_ LPWSABUF lpOutboundDisconnectData,
    _Out_ LPINT lpErrno
)
{
    UNREFERENCED_PARAMETER(lpOutboundDisconnectData);
    UNREFERENCED_PARAMETER(lpErrno);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    return ERROR_SUCCESS;
}

int
WSPAPI
VIOSockSendTo(
    _In_ SOCKET s,
    _In_reads_(dwBufferCount) LPWSABUF lpBuffers,
    _In_ DWORD dwBufferCount,
    _Out_opt_ LPDWORD lpNumberOfBytesSent,
    _In_ DWORD dwFlags,
    _In_reads_bytes_opt_(iTolen) const struct sockaddr FAR * lpTo,
    _In_ int iTolen,
    _Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
    _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
    _In_opt_ LPWSATHREADID lpThreadId,
    _Out_ LPINT lpErrno
)
{
    UNREFERENCED_PARAMETER(lpTo);
    UNREFERENCED_PARAMETER(iTolen);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    return VIOSockSend(s, lpBuffers, dwBufferCount,
        lpNumberOfBytesSent, dwFlags, lpOverlapped,
        lpCompletionRoutine, lpThreadId, lpErrno);
}

int
WSPAPI
VIOSockSetSockOpt(
    _In_ SOCKET s,
    _In_ int level,
    _In_ int optname,
    _In_reads_bytes_opt_(optlen) const char FAR * optval,
    _In_ int optlen,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;
    VIRTIO_VSOCK_OPT Opt = { 0 };

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (!optlen)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid optlen\n");
        *lpErrno = WSAEINVAL;
        return SOCKET_ERROR;
    }

    Opt.level = level;
    Opt.optname = optname;
    Opt.optval = (ULONGLONG)optval;
    Opt.optlen = optlen;

    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_SET_SOCK_OPT,
        &Opt, sizeof(Opt), NULL, 0, NULL, lpErrno))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockShutdown(
    _In_ SOCKET s,
    _In_ int how,
    _Out_ LPINT lpErrno
)
{
    int iRes = ERROR_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (!VIOSockDeviceControl(s, IOCTL_SOCKET_SHUTDOWN,
        &how, (DWORD)sizeof(how), NULL, 0, NULL, lpErrno))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "VIOSockDeviceControl failed: %d\n", *lpErrno);
        iRes = SOCKET_ERROR;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

_Must_inspect_result_
SOCKET
WSPAPI
VIOSockSocket(
    _In_ int af,
    _In_ int type,
    _In_ int protocol,
    _In_opt_ LPWSAPROTOCOL_INFOW lpProtocolInfo,
    _In_ GROUP g,
    _In_ DWORD dwFlags,
    _Out_ LPINT lpErrno
)
{
    SOCKET s;
    HANDLE hFile;
    VIRTIO_VSOCK_PARAMS SocketParams = { 0 };

    UNREFERENCED_PARAMETER(lpProtocolInfo);
    UNREFERENCED_PARAMETER(g);
    UNREFERENCED_PARAMETER(dwFlags);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (af != AF_VSOCK || protocol != 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid parameters\n");
        *lpErrno = WSAEINVAL;
        return INVALID_SOCKET;
    }

    if (type == SOCK_STREAM)
    {
        SocketParams.Type = VIRTIO_VSOCK_TYPE_STREAM;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Unsupported socket type\n");
        *lpErrno = WSAEINVAL;
        return INVALID_SOCKET;
    }

    hFile = VIOSockCreateFile(&SocketParams, lpErrno);
    if (INVALID_HANDLE_VALUE == hFile)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockCreateFile failed: %u\n", *lpErrno);
        return INVALID_SOCKET;
    }

    s = g_UpcallTable.lpWPUModifyIFSHandle(g_ProtocolInfo.dwCatalogEntryId, (SOCKET)hFile, lpErrno);
    if (INVALID_SOCKET == s)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "g_UpcallTable.lpWPUModifyIFSHandle failed: %u\n", *lpErrno);
        CloseHandle(hFile);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return s;
}

INT
WSPAPI
VIOSockStringToAddress(
    _In_ LPWSTR AddressString,
    _In_ INT AddressFamily,
    _In_opt_ LPWSAPROTOCOL_INFOW lpProtocolInfo,
    _Out_writes_bytes_to_(*lpAddressLength, *lpAddressLength) LPSOCKADDR lpAddress,
    _Inout_ LPINT lpAddressLength,
    _Out_ LPINT lpErrno
)
{
    UNREFERENCED_PARAMETER(AddressString);
    UNREFERENCED_PARAMETER(AddressFamily);
    UNREFERENCED_PARAMETER(lpProtocolInfo);
    UNREFERENCED_PARAMETER(lpAddress);
    UNREFERENCED_PARAMETER(lpAddressLength);
    UNREFERENCED_PARAMETER(lpErrno);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    return ERROR_SUCCESS;
}
