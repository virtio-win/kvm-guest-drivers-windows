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

NTSTATUS
NTAPI
NtWriteFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_opt_ PULONG Key
);

NTSTATUS
NTAPI
NtReadFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_opt_ PULONG Key
);


#define STATUS_SUCCESS                   ((NTSTATUS)0x00000000L)
#define STATUS_CANT_WAIT                 ((NTSTATUS)0xC00000D8L)
#define STATUS_CONNECTION_RESET          ((NTSTATUS)0xC000020DL)
#define STATUS_UNSUCCESSFUL              ((NTSTATUS)0xC0000001L)

INT
NtStatusToWsaError(
    _In_ NTSTATUS Status
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
    case STATUS_CANT_WAIT:
        wsaError = WSAEWOULDBLOCK;
        break;
    case STATUS_CONNECTION_RESET:
        wsaError = WSAECONNRESET;
        break;
        //     case STATUS_ACCESS_DENIED:
        //         wsaError = WSAEACCES;
        //         break;
    }

    return wsaError;
}

typedef struct _FILE_FULL_EA_INFORMATION {
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
} FILE_FULL_EA_INFORMATION, *PFILE_FULL_EA_INFORMATION;

HANDLE
VIOSockCreateFile(
    _In_opt_ PVIRTIO_VSOCK_PARAMS pSocketParams,
    _Out_ LPINT lpErrno
)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    NTSTATUS status;
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

    status = NtCreateFile(&hFile, FILE_GENERIC_READ | FILE_GENERIC_WRITE, &oa, &iosb, 0, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_NON_DIRECTORY_FILE, pEaBuffer,
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
)
{
    BOOL bRes = TRUE;
    NTSTATUS status;
    IO_STATUS_BLOCK iosb = { 0 };

    if(lpBytesReturned)
        *lpBytesReturned = 0;

    status = NtDeviceIoControlFile((HANDLE)s, NULL, NULL, NULL, &iosb,
        dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize);

    if (status == STATUS_PENDING)
    {
        WaitForSingleObject((HANDLE)s, INFINITE);
        status = iosb.Status;
    }

    if (NT_SUCCESS(status))
    {
        if (lpBytesReturned)
            *lpBytesReturned = (ULONG)iosb.Information;
    }
    else
    {
        *lpErrno = NtStatusToWsaError(status);
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SOCKET, "NtDeviceIoControlFile failed: %d\n", *lpErrno);
        bRes = FALSE;
    }

    return bRes;
}

BOOL
VIOSockWriteFile(
    _In_ SOCKET s,
    _In_reads_bytes_opt_(nNumberOfBytesToWrite) LPVOID lpBuffer,
    _In_ DWORD nNumberOfBytesToWrite,
    _Out_opt_ LPDWORD lpNumberOfBytesWritten,
    _Out_ LPINT lpErrno
)
{
    BOOL bRes = TRUE;
    NTSTATUS status;
    IO_STATUS_BLOCK iosb = { 0 };
    LARGE_INTEGER liBytesOffset = { 0 };

    if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = 0;

    status = NtWriteFile((HANDLE)s, NULL, NULL, NULL,
        &iosb, lpBuffer, nNumberOfBytesToWrite,
        &liBytesOffset, NULL);

    if (status == STATUS_PENDING)
    {
        WaitForSingleObject((HANDLE)s, INFINITE);
        status = iosb.Status;
    }

    if (NT_SUCCESS(status))
    {
        if(lpNumberOfBytesWritten)
            *lpNumberOfBytesWritten = (DWORD)iosb.Information;

    }
    else
    {
        *lpErrno = NtStatusToWsaError(status);
        bRes = FALSE;
    }

    return bRes;
}

BOOL
VIOSockReadFile(
    _In_ SOCKET s,
    _Out_writes_bytes_to_opt_(nNumberOfBytesToRead, *lpNumberOfBytesRead) __out_data_source(FILE) LPVOID lpBuffer,
    _In_ DWORD nNumberOfBytesToRead,
    _Out_opt_ LPDWORD lpNumberOfBytesRead,
    _Out_ LPINT lpErrno
)
{
    BOOL bRes = TRUE;
    NTSTATUS status;
    IO_STATUS_BLOCK iosb = { 0 };
    LARGE_INTEGER liBytesOffset = { 0 };

    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = 0;

    status = NtReadFile((HANDLE)s, NULL, NULL, NULL,
        &iosb, lpBuffer, nNumberOfBytesToRead,
        &liBytesOffset, NULL);

    if (status == STATUS_PENDING)
    {
        WaitForSingleObject((HANDLE)s, INFINITE);
        status = iosb.Status;
    }

    if (NT_SUCCESS(status))
    {
        if (lpNumberOfBytesRead)
            *lpNumberOfBytesRead = (DWORD)iosb.Information;

    }
    else
    {
        *lpErrno = NtStatusToWsaError(status);
        bRes = FALSE;
    }

    return bRes;
}
