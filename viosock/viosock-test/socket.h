#pragma once

//////////////////////////////////////////////////////////////////////////
SOCKET
WINAPI
VIOSockCreateSocket(
    _In_opt_ PVIRTIO_VSOCK_PARAMS pSocketParams
);

VOID
WINAPI
VIOSockCloseSocket(
    _In_ SOCKET hSocket
);

BOOL
WINAPI
VIOSockGetConfig(
    _In_ SOCKET hViosock,
    _Out_ PVIRTIO_VSOCK_CONFIG pConfig
);
