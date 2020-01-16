/*
 * Placeholder for the provider init functions
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

INT
NtStatusToWsaError(
    NTSTATUS Status
)
{
    INT wsaError = (NT_SUCCESS(Status)) ? ERROR_SUCCESS : WSASYSNOTREADY;
    switch (Status)
    {
    case STATUS_INVALID_PARAMETER:
        wsaError = WSAEINVAL;
        break;
    case STATUS_TIMEOUT:
        wsaError = WSAETIMEDOUT;
        break;
        //     case STATUS_ACCESS_DENIED:
        //         wsaError = WSAEACCES;
        //         break;
    }

    return wsaError;
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
    UNREFERENCED_PARAMETER(addr);
    UNREFERENCED_PARAMETER(addrlen);
    UNREFERENCED_PARAMETER(lpfnCondition);
    UNREFERENCED_PARAMETER(dwCallbackData);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return INVALID_SOCKET;
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(lpsaAddress);
    UNREFERENCED_PARAMETER(dwAddressLength);
    UNREFERENCED_PARAMETER(lpProtocolInfo);
    UNREFERENCED_PARAMETER(lpszAddressString);
    UNREFERENCED_PARAMETER(lpdwAddressStringLength);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(name);
    UNREFERENCED_PARAMETER(namelen);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

int
WSPAPI
VIOSockCancelBlockingCall(
    _Out_ LPINT lpErrno
)
{
    int iRes = -1;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
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
    int iRes = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    if (!CloseHandle((HANDLE)s))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "CloseHandle failed, socket: %p\n", (PVOID)s);

        *lpErrno = WSAEINVAL;
        iRes = -1;
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(name);
    UNREFERENCED_PARAMETER(namelen);
    UNREFERENCED_PARAMETER(lpCallerData);
    UNREFERENCED_PARAMETER(lpCalleeData);
    UNREFERENCED_PARAMETER(lpSQOS);
    UNREFERENCED_PARAMETER(lpGQOS);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(dwProcessId);
    UNREFERENCED_PARAMETER(lpProtocolInfo);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(hEventObject);
    UNREFERENCED_PARAMETER(lpNetworkEvents);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

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
    int iRes = -1;

    UNREFERENCED_PARAMETER(hEventObject);
    UNREFERENCED_PARAMETER(lNetworkEvents);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

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
    int iRes = -1;

    UNREFERENCED_PARAMETER(lpOverlapped);
    UNREFERENCED_PARAMETER(lpcbTransfer);
    UNREFERENCED_PARAMETER(fWait);
    UNREFERENCED_PARAMETER(lpdwFlags);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(name);
    UNREFERENCED_PARAMETER(namelen);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(name);
    UNREFERENCED_PARAMETER(namelen);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(level);
    UNREFERENCED_PARAMETER(optname);
    UNREFERENCED_PARAMETER(optval);
    UNREFERENCED_PARAMETER(optlen);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(lpQOSName);
    UNREFERENCED_PARAMETER(lpQOS);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(dwIoControlCode);
    UNREFERENCED_PARAMETER(lpvInBuffer);
    UNREFERENCED_PARAMETER(cbInBuffer);
    UNREFERENCED_PARAMETER(lpvOutBuffer);
    UNREFERENCED_PARAMETER(cbOutBuffer);
    UNREFERENCED_PARAMETER(lpcbBytesReturned);
    UNREFERENCED_PARAMETER(lpOverlapped);
    UNREFERENCED_PARAMETER(lpCompletionRoutine);
    UNREFERENCED_PARAMETER(lpThreadId);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return INVALID_SOCKET;
}

int
WSPAPI
VIOSockListen(
    _In_ SOCKET s,
    _In_ int backlog,
    _Out_ LPINT lpErrno
)
{
    int iRes = -1;

    UNREFERENCED_PARAMETER(backlog);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

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
    int iRes = -1;

    UNREFERENCED_PARAMETER(lpBuffers);
    UNREFERENCED_PARAMETER(dwBufferCount);
    UNREFERENCED_PARAMETER(lpNumberOfBytesRecvd);
    UNREFERENCED_PARAMETER(lpFlags);
    UNREFERENCED_PARAMETER(lpOverlapped);
    UNREFERENCED_PARAMETER(lpCompletionRoutine);
    UNREFERENCED_PARAMETER(lpThreadId);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

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
    int iRes = -1;

    UNREFERENCED_PARAMETER(lpInboundDisconnectData);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(lpBuffers);
    UNREFERENCED_PARAMETER(dwBufferCount);
    UNREFERENCED_PARAMETER(lpNumberOfBytesRecvd);
    UNREFERENCED_PARAMETER(lpFlags);
    UNREFERENCED_PARAMETER(lpFrom);
    UNREFERENCED_PARAMETER(lpFromlen);
    UNREFERENCED_PARAMETER(lpOverlapped);
    UNREFERENCED_PARAMETER(lpCompletionRoutine);
    UNREFERENCED_PARAMETER(lpThreadId);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(nfds);
    UNREFERENCED_PARAMETER(readfds);
    UNREFERENCED_PARAMETER(writefds);
    UNREFERENCED_PARAMETER(exceptfds);
    UNREFERENCED_PARAMETER(timeout);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(lpBuffers);
    UNREFERENCED_PARAMETER(dwBufferCount);
    UNREFERENCED_PARAMETER(lpNumberOfBytesSent);
    UNREFERENCED_PARAMETER(dwFlags);
    UNREFERENCED_PARAMETER(lpOverlapped);
    UNREFERENCED_PARAMETER(lpCompletionRoutine);
    UNREFERENCED_PARAMETER(lpThreadId);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

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
    int iRes = -1;

    UNREFERENCED_PARAMETER(lpOutboundDisconnectData);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(lpBuffers);
    UNREFERENCED_PARAMETER(dwBufferCount);
    UNREFERENCED_PARAMETER(lpNumberOfBytesSent);
    UNREFERENCED_PARAMETER(dwFlags);
    UNREFERENCED_PARAMETER(lpTo);
    UNREFERENCED_PARAMETER(iTolen);
    UNREFERENCED_PARAMETER(lpOverlapped);
    UNREFERENCED_PARAMETER(lpCompletionRoutine);
    UNREFERENCED_PARAMETER(lpThreadId);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(level);
    UNREFERENCED_PARAMETER(optname);
    UNREFERENCED_PARAMETER(optval);
    UNREFERENCED_PARAMETER(optlen);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(how);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s, socket: %p\n", __FUNCTION__, (PVOID)s);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}

typedef struct _FILE_FULL_EA_INFORMATION {
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
} FILE_FULL_EA_INFORMATION, *PFILE_FULL_EA_INFORMATION;

HANDLE
VIOSockOpenFile(
    _In_opt_ PVIRTIO_VSOCK_PARAMS pSocketParams,
    _Out_ LPINT lpErrno
)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb = { 0 };
    UNICODE_STRING usDeviceName = RTL_CONSTANT_STRING(VIOSOCK_NAME);

    UCHAR EaBuffer[sizeof(FILE_FULL_EA_INFORMATION) + sizeof(*pSocketParams)] = { 0 };
    PFILE_FULL_EA_INFORMATION pEaBuffer = (PFILE_FULL_EA_INFORMATION)EaBuffer;

    if (pSocketParams)
    {
        pEaBuffer->EaNameLength = sizeof(FILE_FULL_EA_INFORMATION) -
            FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) - sizeof(UCHAR);
        pEaBuffer->EaValueLength = sizeof(*pSocketParams);
        memcpy(&EaBuffer[sizeof(FILE_FULL_EA_INFORMATION)], pSocketParams, sizeof(*pSocketParams));
    }
    else
        pEaBuffer = NULL;

    InitializeObjectAttributes(&oa, &usDeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = NtCreateFile(&hFile, FILE_GENERIC_READ | FILE_GENERIC_WRITE, &oa, &iosb, 0, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_NON_DIRECTORY_FILE, pEaBuffer,
        pEaBuffer ? sizeof(EaBuffer) : 0);

    if (!NT_SUCCESS(Status))
    {
        _ASSERT(hFile == INVALID_HANDLE_VALUE);
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "NtCreateFile failed: %x\n", Status);
        *lpErrno = NtStatusToWsaError(Status);
    }

    return hFile;
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

    if (af != AF_VSOCK || type != SOCK_STREAM || protocol != 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "Invalid parameters\n");
        *lpErrno = WSAEINVAL;
        return INVALID_SOCKET;
    }

    hFile = VIOSockOpenFile(&SocketParams, lpErrno);
    if (INVALID_HANDLE_VALUE == hFile)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockCreateSocket failed: %u\n", *lpErrno);
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
    int iRes = -1;

    UNREFERENCED_PARAMETER(AddressString);
    UNREFERENCED_PARAMETER(AddressFamily);
    UNREFERENCED_PARAMETER(lpProtocolInfo);
    UNREFERENCED_PARAMETER(lpAddress);
    UNREFERENCED_PARAMETER(lpAddressLength);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    *lpErrno = WSAVERNOTSUPPORTED;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "<-- %s\n", __FUNCTION__);
    return iRes;
}
