#include "pch.h"

//////////////////////////////////////////////////////////////////////////
#ifndef RTL_CONSTANT_STRING
#define RTL_CONSTANT_STRING(s) \
{ \
    sizeof( s ) - sizeof( (s)[0] ), \
    sizeof( s ) / sizeof( (s) ), \
    ( s ) \
}
#endif

typedef struct _FILE_FULL_EA_INFORMATION {
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
} FILE_FULL_EA_INFORMATION, *PFILE_FULL_EA_INFORMATION;

SOCKET
WINAPI
VIOSockCreateSocket(
    _In_opt_ PVIRTIO_VSOCK_PARAMS pSocketParams
)
{
    SOCKET hSocket = INVALID_SOCKET;
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

    Status = NtCreateFile((PHANDLE)&hSocket, FILE_GENERIC_READ | FILE_GENERIC_WRITE, &oa, &iosb, 0, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_NON_DIRECTORY_FILE, pEaBuffer,
        pEaBuffer ? sizeof(EaBuffer) : 0);

    if (!NT_SUCCESS(Status))
    {
        _ASSERT(hSocket == INVALID_SOCKET);
        _tprintf(L"Can't open %s, status: %x\n", VIOSOCK_NAME, Status);
        SetLastError(RtlNtStatusToDosError(Status));
    }

    return hSocket;
}

VOID
WINAPI
VIOSockCloseSocket(
    _In_ SOCKET hSocket
)
{
    CloseHandle((HANDLE)hSocket);
}

BOOL
WINAPI
VIOSockGetConfig(
    _In_ SOCKET hViosock,
    _Out_ PVIRTIO_VSOCK_CONFIG pConfig
)
{
    DWORD dwBytes;
    return DeviceIoControl((HANDLE)hViosock, IOCTL_GET_CONFIG, NULL, 0, pConfig, sizeof(*pConfig), &dwBytes, NULL);
}
