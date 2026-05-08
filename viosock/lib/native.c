/*
 * Native call wrappers
 *
 * Copyright (c) 2020 Virtuozzo International GmbH
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
#include "native.tmh"
#endif

/*
 * We use Native API functions here because they expose parameters that have
 * no Win32 equivalent: NtCreateFile allows passing extended attributes (EA)
 * with socket parameters at file creation time, and NtReadFile/NtWriteFile
 * allow specifying a ByteOffset required for socket I/O on the underlying
 * device. NtDeviceIoControlFile returns NTSTATUS directly, which we map to
 * WSA error codes via NtStatusToWsaError.
 */

NTSTATUS
NTAPI
NtWriteFile(_In_ HANDLE FileHandle,
            _In_opt_ HANDLE Event,
            _In_opt_ PIO_APC_ROUTINE ApcRoutine,
            _In_opt_ PVOID ApcContext,
            _Out_ PIO_STATUS_BLOCK IoStatusBlock,
            _In_reads_bytes_(Length) PVOID Buffer,
            _In_ ULONG Length,
            _In_opt_ PLARGE_INTEGER ByteOffset,
            _In_opt_ PULONG Key);

NTSTATUS
NTAPI
NtReadFile(_In_ HANDLE FileHandle,
           _In_opt_ HANDLE Event,
           _In_opt_ PIO_APC_ROUTINE ApcRoutine,
           _In_opt_ PVOID ApcContext,
           _Out_ PIO_STATUS_BLOCK IoStatusBlock,
           _Out_writes_bytes_(Length) PVOID Buffer,
           _In_ ULONG Length,
           _In_opt_ PLARGE_INTEGER ByteOffset,
           _In_opt_ PULONG Key);

#define STATUS_SUCCESS                    ((NTSTATUS)0x00000000L)
#define STATUS_UNSUCCESSFUL               ((NTSTATUS)0xC0000001L)
#define STATUS_ACCESS_DENIED              ((NTSTATUS)0xC0000022L)
#define STATUS_INSUFFICIENT_RESOURCES     ((NTSTATUS)0xC000009AL)
#define STATUS_NOT_SUPPORTED              ((NTSTATUS)0xC00000BBL)
#define STATUS_CANT_WAIT                  ((NTSTATUS)0xC00000D8L)
#define STATUS_INVALID_ADDRESS_COMPONENT  ((NTSTATUS)0xC0000207L)
#define STATUS_ADDRESS_ALREADY_ASSOCIATED ((NTSTATUS)0xC0000238L)
#define STATUS_INVALID_CONNECTION         ((NTSTATUS)0xC0000140L)
#define STATUS_CONNECTION_ACTIVE          ((NTSTATUS)0xC000023BL)
#define STATUS_CONNECTION_REFUSED         ((NTSTATUS)0xC0000236L)
#define STATUS_CONNECTION_RESET           ((NTSTATUS)0xC000020DL)
#define STATUS_LOCAL_DISCONNECT           ((NTSTATUS)0xC000013BL)
#define STATUS_HOST_UNREACHABLE           ((NTSTATUS)0xC000023DL)
#define STATUS_IO_TIMEOUT                 ((NTSTATUS)0xC00000B5L)
#define STATUS_INVALID_DEVICE_STATE       ((NTSTATUS)0xC0000184L)

INT NtStatusToWsaError(_In_ NTSTATUS Status)
{
    INT wsaError = (NT_SUCCESS(Status)) ? ERROR_SUCCESS : ERROR_INTERNAL_ERROR;
    switch (Status)
    {
        case STATUS_INVALID_DEVICE_STATE:
        case STATUS_INVALID_PARAMETER:
            wsaError = WSAEINVAL;
            break;
        case STATUS_IO_TIMEOUT:
            wsaError = WSAETIMEDOUT;
            break;
        case STATUS_CANT_WAIT:
            wsaError = WSAEWOULDBLOCK;
            break;
        case STATUS_CONNECTION_RESET:
            wsaError = WSAECONNRESET;
            break;
        case STATUS_INVALID_CONNECTION:
            wsaError = WSAENOTCONN;
            break;
        case STATUS_ACCESS_DENIED:
            wsaError = WSAEACCES;
            break;
        case STATUS_NOT_SUPPORTED:
            wsaError = WSAEPROTONOSUPPORT;
            break;
        case STATUS_NOT_SOCKET:
            wsaError = WSAENOTSOCK;
            break;
        case STATUS_ADDRESS_ALREADY_ASSOCIATED:
            wsaError = WSAEADDRINUSE;
            break;
        case STATUS_INVALID_ADDRESS_COMPONENT:
            wsaError = WSAEADDRNOTAVAIL;
            break;
        case STATUS_CONNECTION_ACTIVE:
            wsaError = WSAEISCONN;
            break;
        case STATUS_CONNECTION_ESTABLISHING:
            wsaError = WSAEALREADY;
            break;
        case STATUS_LOCAL_DISCONNECT:
            wsaError = WSAESHUTDOWN;
            break;
        case STATUS_INSUFFICIENT_RESOURCES:
            wsaError = WSAENOBUFS;
            break;
        case STATUS_CONNECTION_REFUSED:
            wsaError = WSAECONNREFUSED;
            break;
        case STATUS_HOST_UNREACHABLE:
            wsaError = WSAEHOSTUNREACH;
            break;

        // TODO: remove default later
        default:
            wsaError = Status;
    }

    return wsaError;
}

typedef struct _FILE_FULL_EA_INFORMATION
{
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
} FILE_FULL_EA_INFORMATION, *PFILE_FULL_EA_INFORMATION;

_Must_inspect_result_ HANDLE VIOSockCreateFile(_In_opt_ PVIRTIO_VSOCK_PARAMS pSocketParams, _Out_ LPINT lpErrno)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    NTSTATUS status;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb = {0};
    UNICODE_STRING usDeviceName = RTL_CONSTANT_STRING(VIOSOCK_NAME);

    UCHAR EaBuffer[sizeof(FILE_FULL_EA_INFORMATION) + sizeof(*pSocketParams)] = {0};
    PFILE_FULL_EA_INFORMATION pEaBuffer = (PFILE_FULL_EA_INFORMATION)EaBuffer;

    *lpErrno = ERROR_SUCCESS;

    if (pSocketParams)
    {
        pEaBuffer->EaNameLength = sizeof(FILE_FULL_EA_INFORMATION) - FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) -
                                  sizeof(UCHAR);
        pEaBuffer->EaValueLength = sizeof(*pSocketParams);
        memcpy(&EaBuffer[sizeof(FILE_FULL_EA_INFORMATION)], pSocketParams, sizeof(*pSocketParams));
    }
    else
    {
        pEaBuffer = NULL;
    }

    InitializeObjectAttributes(&oa, &usDeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = NtCreateFile(&hFile,
                          FILE_GENERIC_READ | FILE_GENERIC_WRITE,
                          &oa,
                          &iosb,
                          0,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE,
                          pEaBuffer,
                          pEaBuffer ? sizeof(EaBuffer) : 0);

    if (!NT_SUCCESS(status))
    {
        _ASSERT(hFile == INVALID_HANDLE_VALUE);
        hFile = INVALID_HANDLE_VALUE;
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "NtCreateFile failed: %x\n", status);
        *lpErrno = NtStatusToWsaError(status);
    }

    return hFile;
}

typedef struct _VIOSOCK_APC_CONTEXT
{
    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine;
    LPWSAOVERLAPPED lpOverlapped;
    IO_STATUS_BLOCK iosb;
    DWORD dwFlags;
} VIOSOCK_APC_CONTEXT, *PVIOSOCK_APC_CONTEXT;

static VOID NTAPI VIOSockApcRoutine(_In_ PVOID ApcContext, _In_ PIO_STATUS_BLOCK IoStatusBlock, _In_ ULONG Reserved)
{
    PVIOSOCK_APC_CONTEXT pContext = (PVIOSOCK_APC_CONTEXT)ApcContext;
    DWORD dwError;
    DWORD cbTransferred;

    UNREFERENCED_PARAMETER(Reserved);

    if (!pContext)
    {
        return;
    }

    if (NT_SUCCESS(IoStatusBlock->Status))
    {
        dwError = ERROR_SUCCESS;
        cbTransferred = (DWORD)IoStatusBlock->Information;
    }
    else
    {
        dwError = NtStatusToWsaError(IoStatusBlock->Status);
        cbTransferred = 0;
    }

    if (pContext->lpCompletionRoutine)
    {
        pContext->lpCompletionRoutine(dwError, cbTransferred, pContext->lpOverlapped, pContext->dwFlags);
    }

    HeapFree(GetProcessHeap(), 0, pContext);
}

typedef struct _VIOSOCK_OVERLAPPED_CONTEXT
{
    HANDLE hEvent;
    PVIOSOCK_APC_CONTEXT ApcContext;
    PIO_APC_ROUTINE ApcRoutine;
    PIO_STATUS_BLOCK pIosb;
    IO_STATUS_BLOCK iosb;
    BOOL bInternalEvent;
} VIOSOCK_OVERLAPPED_CONTEXT, *PVIOSOCK_OVERLAPPED_CONTEXT;

static INT VIOSockInitOverlappedContext(_Out_ PVIOSOCK_OVERLAPPED_CONTEXT pContext,
                                        _In_opt_ LPWSAOVERLAPPED lpOverlapped,
                                        _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
                                        _In_opt_ DWORD dwFlags)
{
    memset(pContext, 0, sizeof(VIOSOCK_OVERLAPPED_CONTEXT));

    if (lpCompletionRoutine)
    {
        pContext->ApcContext = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(VIOSOCK_APC_CONTEXT));
        if (!pContext->ApcContext)
        {
            return WSA_NOT_ENOUGH_MEMORY;
        }
        pContext->ApcRoutine = VIOSockApcRoutine;
        pContext->ApcContext->lpCompletionRoutine = lpCompletionRoutine;
        pContext->ApcContext->lpOverlapped = lpOverlapped;
        pContext->ApcContext->dwFlags = dwFlags;
        pContext->pIosb = lpOverlapped ? (PIO_STATUS_BLOCK)lpOverlapped : &pContext->ApcContext->iosb;
    }
    else if (lpOverlapped)
    {
        pContext->pIosb = (PIO_STATUS_BLOCK)lpOverlapped;
        pContext->hEvent = lpOverlapped->hEvent;
    }
    else // sync call
    {
        pContext->pIosb = &pContext->iosb;
        pContext->hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!pContext->hEvent)
        {
            return WSA_NOT_ENOUGH_MEMORY;
        }
        pContext->bInternalEvent = TRUE;
    }

    return ERROR_SUCCESS;
}

static INT VIOSockCompleteOverlappedContext(_In_ PVIOSOCK_OVERLAPPED_CONTEXT pContext,
                                            _In_ NTSTATUS Status,
                                            _Out_opt_ LPDWORD lpBytesReturned)
{
    INT iRes = ERROR_SUCCESS;
    if (Status == STATUS_PENDING)
    {
        if (pContext->hEvent && pContext->bInternalEvent)
        {
            // sync call
            WaitForSingleObject(pContext->hEvent, INFINITE);
            Status = pContext->pIosb->Status;
        }
        else
        {
            return WSA_IO_PENDING;
        }
    }

    if (pContext->hEvent && pContext->bInternalEvent)
    {
        CloseHandle(pContext->hEvent);
    }

    if (NT_SUCCESS(Status))
    {
        if (lpBytesReturned)
        {
            *lpBytesReturned = (ULONG)pContext->pIosb->Information;
        }
    }
    else
    {
        if (pContext->ApcContext)
        {
            HeapFree(GetProcessHeap(), 0, pContext->ApcContext);
        }

        iRes = NtStatusToWsaError(Status);
    }

    return iRes;
}

_Must_inspect_result_ BOOL VIOSockDeviceControlEx(_In_ SOCKET s,
                                                  _In_ DWORD dwIoControlCode,
                                                  _In_reads_bytes_opt_(nInBufferSize) LPVOID lpInBuffer,
                                                  _In_ DWORD nInBufferSize,
                                                  _Out_writes_bytes_to_opt_(nOutBufferSize,
                                                                            *lpBytesReturned) LPVOID lpOutBuffer,
                                                  _In_ DWORD nOutBufferSize,
                                                  _Out_opt_ LPDWORD lpBytesReturned,
                                                  _Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
                                                  _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
                                                  _Out_ LPINT lpErrno)
{
    BOOL bRes = TRUE;
    NTSTATUS status;
    VIOSOCK_OVERLAPPED_CONTEXT OverlappedContext;

    *lpErrno = ERROR_SUCCESS;

    if (lpBytesReturned)
    {
        *lpBytesReturned = 0;
    }

    *lpErrno = VIOSockInitOverlappedContext(&OverlappedContext, lpOverlapped, lpCompletionRoutine, 0);
    if (*lpErrno != ERROR_SUCCESS)
    {
        return FALSE;
    }

    status = NtDeviceIoControlFile((HANDLE)s,
                                   OverlappedContext.hEvent,
                                   OverlappedContext.ApcRoutine,
                                   OverlappedContext.ApcContext,
                                   OverlappedContext.pIosb,
                                   dwIoControlCode,
                                   lpInBuffer,
                                   nInBufferSize,
                                   lpOutBuffer,
                                   nOutBufferSize);

    *lpErrno = VIOSockCompleteOverlappedContext(&OverlappedContext, status, lpBytesReturned);

    if (*lpErrno != ERROR_SUCCESS)
    {
        if (*lpErrno != WSA_IO_PENDING)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "NtDeviceIoControlFile failed: %d\n", *lpErrno);
        }
        bRes = FALSE;
    }

    return bRes;
}

_Must_inspect_result_ BOOL VIOSockWriteFile(_In_ SOCKET s,
                                            _In_reads_bytes_(nNumberOfBytesToWrite) LPVOID lpBuffer,
                                            _In_ DWORD nNumberOfBytesToWrite,
                                            _Out_opt_ LPDWORD lpNumberOfBytesWritten,
                                            _Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
                                            _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
                                            _Out_ LPINT lpErrno)
{
    BOOL bRes = TRUE;
    NTSTATUS status;
    LARGE_INTEGER liBytesOffset = {0};
    VIOSOCK_OVERLAPPED_CONTEXT OverlappedContext;

    *lpErrno = ERROR_SUCCESS;

    if (lpNumberOfBytesWritten)
    {
        *lpNumberOfBytesWritten = 0;
    }

    *lpErrno = VIOSockInitOverlappedContext(&OverlappedContext, lpOverlapped, lpCompletionRoutine, 0);
    if (*lpErrno != ERROR_SUCCESS)
    {
        return FALSE;
    }

    status = NtWriteFile((HANDLE)s,
                         OverlappedContext.hEvent,
                         OverlappedContext.ApcRoutine,
                         OverlappedContext.ApcContext,
                         OverlappedContext.pIosb,
                         lpBuffer,
                         nNumberOfBytesToWrite,
                         &liBytesOffset,
                         NULL);

    *lpErrno = VIOSockCompleteOverlappedContext(&OverlappedContext, status, lpNumberOfBytesWritten);

    if (*lpErrno != ERROR_SUCCESS)
    {
        if (*lpErrno != WSA_IO_PENDING)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "NtWriteFile failed: %d\n", *lpErrno);
        }
        bRes = FALSE;
    }

    return bRes;
}

_Must_inspect_result_ BOOL VIOSockReadFile(_In_ SOCKET s,
                                           _Out_writes_bytes_(nNumberOfBytesToRead) LPVOID lpBuffer,
                                           _In_ DWORD nNumberOfBytesToRead,
                                           _Out_opt_ LPDWORD lpNumberOfBytesRead,
                                           _Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
                                           _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
                                           _Out_ LPINT lpErrno)
{
    BOOL bRes = TRUE;
    NTSTATUS status;
    LARGE_INTEGER liBytesOffset = {0};
    VIOSOCK_OVERLAPPED_CONTEXT OverlappedContext;

    *lpErrno = ERROR_SUCCESS;

    if (lpNumberOfBytesRead)
    {
        *lpNumberOfBytesRead = 0;
    }

    *lpErrno = VIOSockInitOverlappedContext(&OverlappedContext, lpOverlapped, lpCompletionRoutine, 0);
    if (*lpErrno != ERROR_SUCCESS)
    {
        return FALSE;
    }

    status = NtReadFile((HANDLE)s,
                        OverlappedContext.hEvent,
                        OverlappedContext.ApcRoutine,
                        OverlappedContext.ApcContext,
                        OverlappedContext.pIosb,
                        lpBuffer,
                        nNumberOfBytesToRead,
                        &liBytesOffset,
                        NULL);

    *lpErrno = VIOSockCompleteOverlappedContext(&OverlappedContext, status, lpNumberOfBytesRead);

    if (*lpErrno != ERROR_SUCCESS)
    {
        if (*lpErrno != WSA_IO_PENDING)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "NtReadFile failed: %d\n", *lpErrno);
        }
        bRes = FALSE;
    }

    return bRes;
}
