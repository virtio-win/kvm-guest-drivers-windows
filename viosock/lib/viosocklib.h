/*
 * Viosocklib main include file
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

#ifndef _VIOSOCKLIB_H
#define _VIOSOCKLIB_H

#pragma once

#ifndef RTL_CONSTANT_STRING
#define RTL_CONSTANT_STRING(s) \
{ \
    sizeof( s ) - sizeof( (s)[0] ), \
    sizeof( s ) / sizeof( (s) ), \
    ( s ) \
}
#endif

HANDLE
VIOSockCreateFile(
    _In_opt_ PVIRTIO_VSOCK_PARAMS pSocketParams,
    _Out_ LPINT lpErrno
);

BOOL
VIOSockDeviceControl(
    _In_ SOCKET s,
    _In_ DWORD dwIoControlCode,
    _In_reads_bytes_opt_(nInBufferSize) LPVOID lpInBuffer,
    _In_ DWORD nInBufferSize,
    _Out_writes_bytes_to_opt_(nOutBufferSize, *lpBytesReturned) LPVOID lpOutBuffer,
    _In_ DWORD nOutBufferSize,
    _Out_opt_ LPDWORD lpBytesReturned,
    _Out_ LPINT lpErrno
);

BOOL
VIOSockWriteFile(
    _In_ SOCKET s,
    _In_reads_bytes_opt_(nNumberOfBytesToWrite) LPVOID lpBuffer,
    _In_ DWORD nNumberOfBytesToWrite,
    _Out_opt_ LPDWORD lpNumberOfBytesWritten,
    _Out_ LPINT lpErrno
);

BOOL
VIOSockReadFile(
    _In_ SOCKET s,
    _Out_writes_bytes_to_opt_(nNumberOfBytesToRead, *lpNumberOfBytesRead) __out_data_source(FILE) LPVOID lpBuffer,
    _In_ DWORD nNumberOfBytesToRead,
    _Out_opt_ LPDWORD lpNumberOfBytesRead,
    _Out_ LPINT lpErrno
);

INT
NtStatusToWsaError(
    NTSTATUS Status
);

//////////////////////////////////////////////////////////////////////////
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
);

INT
WSPAPI
VIOSockAddressToString(
    _In_reads_bytes_(dwAddressLength) LPSOCKADDR lpsaAddress,
    _In_ DWORD dwAddressLength,
    _In_opt_ LPWSAPROTOCOL_INFOW lpProtocolInfo,
    _Out_writes_to_(*lpdwAddressStringLength, *lpdwAddressStringLength) LPWSTR lpszAddressString,
    _Inout_ LPDWORD lpdwAddressStringLength,
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockAsyncSelect(
    _In_ SOCKET s,
    _In_ HWND hWnd,
    _In_ unsigned int wMsg,
    _In_ long lEvent,
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockBind(
    _In_ SOCKET s,
    _In_reads_bytes_(namelen) const struct sockaddr FAR * name,
    _In_ int namelen,
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockCancelBlockingCall(
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockCleanup(
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockCloseSocket(
    _In_ SOCKET s,
    _Out_ LPINT lpErrno
);

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
);

int
WSPAPI
VIOSockDuplicateSocket(
    _In_ SOCKET s,
    _In_ DWORD dwProcessId,
    _Out_ LPWSAPROTOCOL_INFOW lpProtocolInfo,
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockEnumNetworkEvents(
    _In_ SOCKET s,
    _In_ WSAEVENT hEventObject,
    _Out_ LPWSANETWORKEVENTS lpNetworkEvents,
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockEventSelect(
    _In_ SOCKET s,
    _In_opt_ WSAEVENT hEventObject,
    _In_ long lNetworkEvents,
    _Out_ LPINT lpErrno
);

BOOL
WSPAPI
VIOSockGetOverlappedResult(
    _In_ SOCKET s,
    _In_ LPWSAOVERLAPPED lpOverlapped,
    _Out_ LPDWORD lpcbTransfer,
    _In_ BOOL fWait,
    _Out_ LPDWORD lpdwFlags,
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockGetPeerName(
    _In_ SOCKET s,
    _Out_writes_bytes_to_(*namelen, *namelen) struct sockaddr FAR * name,
    _Inout_ LPINT namelen,
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockGetSockName(
    _In_ SOCKET s,
    _Out_writes_bytes_to_(*namelen, *namelen) struct sockaddr FAR * name,
    _Inout_ LPINT namelen,
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockGetSockOpt(
    _In_ SOCKET s,
    _In_ int level,
    _In_ int optname,
    _Out_writes_bytes_(*optlen) char FAR * optval,
    _Inout_ LPINT optlen,
    _Out_ LPINT lpErrno
);

BOOL
WSPAPI
VIOSockGetQOSByName(
    _In_ SOCKET s,
    _In_ LPWSABUF lpQOSName,
    _Out_ LPQOS lpQOS,
    _Out_ LPINT lpErrno
);

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
);

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
);

int
WSPAPI
VIOSockListen(
    _In_ SOCKET s,
    _In_ int backlog,
    _Out_ LPINT lpErrno
);

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
);

int
WSPAPI
VIOSockRecvDisconnect(
    _In_ SOCKET s,
    _In_opt_ LPWSABUF lpInboundDisconnectData,
    _Out_ LPINT lpErrno
);

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
);

int
WSPAPI
VIOSockSelect(
    _In_ int nfds,
    _Inout_opt_ fd_set FAR * readfds,
    _Inout_opt_ fd_set FAR * writefds,
    _Inout_opt_ fd_set FAR * exceptfds,
    _In_opt_ const struct timeval FAR * timeout,
    _Out_ LPINT lpErrno
);

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
);

int
WSPAPI
VIOSockSendDisconnect(
    _In_ SOCKET s,
    _In_opt_ LPWSABUF lpOutboundDisconnectData,
    _Out_ LPINT lpErrno
);

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
);

int
WSPAPI
VIOSockSetSockOpt(
    _In_ SOCKET s,
    _In_ int level,
    _In_ int optname,
    _In_reads_bytes_opt_(optlen) const char FAR * optval,
    _In_ int optlen,
    _Out_ LPINT lpErrno
);

int
WSPAPI
VIOSockShutdown(
    _In_ SOCKET s,
    _In_ int how,
    _Out_ LPINT lpErrno
);

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
);

INT
WSPAPI
VIOSockStringToAddress(
    _In_ LPWSTR AddressString,
    _In_ INT AddressFamily,
    _In_opt_ LPWSAPROTOCOL_INFOW lpProtocolInfo,
    _Out_writes_bytes_to_(*lpAddressLength, *lpAddressLength) LPSOCKADDR lpAddress,
    _Inout_ LPINT lpAddressLength,
    _Out_ LPINT lpErrno
);


#endif //_VIOSOCKLIB_H
