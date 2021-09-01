/*
 * Socket dispatch functions
 *
 * Copyright (c) 2021 Virtuozzo International GmbH
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
#include "viowsk.h"
#include "..\inc\vio_wsk.h"

NTSTATUS
WSKAPI
VioWskControlSocket(
    _In_ PWSK_SOCKET                    Socket,
    _In_ WSK_CONTROL_SOCKET_TYPE        RequestType,
    _In_ ULONG                          ControlCode,
    _In_ ULONG                          Level,
    _In_ SIZE_T                         InputSize,
    _In_reads_bytes_opt_(InputSize) PVOID    InputBuffer,
    _In_ SIZE_T                         OutputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID  OutputBuffer,
    _Out_opt_ SIZE_T                   *OutputSizeReturned,
    _Inout_opt_ PIRP                    Irp
);

NTSTATUS
WSKAPI
VioWskCloseSocket(
    _In_ PWSK_SOCKET Socket,
    _Inout_ PIRP     Irp
);

NTSTATUS
WSKAPI
VioWskBind(
    _In_ PWSK_SOCKET Socket,
    _In_ PSOCKADDR   LocalAddress,
    _Reserved_ ULONG Flags,
    _Inout_ PIRP     Irp
    );

NTSTATUS
WSKAPI
VioWskAccept(
    _In_ PWSK_SOCKET                               ListenSocket,
    _Reserved_ ULONG                               Flags,
    _In_opt_ PVOID                                 AcceptSocketContext,
    _In_opt_ CONST WSK_CLIENT_CONNECTION_DISPATCH *AcceptSocketDispatch,
    _Out_opt_ PSOCKADDR                            LocalAddress,
    _Out_opt_ PSOCKADDR                            RemoteAddress,
    _Inout_ PIRP                                   Irp
    );

NTSTATUS
WSKAPI
VioWskInspectComplete(
    _In_ PWSK_SOCKET        ListenSocket,
    _In_ PWSK_INSPECT_ID    InspectID,
    _In_ WSK_INSPECT_ACTION Action,
    _Inout_ PIRP            Irp
    );

NTSTATUS
WSKAPI
VioWskGetLocalAddress(
    _In_ PWSK_SOCKET Socket,
    _Out_ PSOCKADDR  LocalAddress,
    _Inout_ PIRP     Irp
    );

NTSTATUS
WSKAPI
VioWskConnect(
    _In_ PWSK_SOCKET Socket,
    _In_ PSOCKADDR   RemoteAddress,
    _Reserved_ ULONG Flags,
    _Inout_ PIRP     Irp
    );


NTSTATUS
WSKAPI
VioWskGetRemoteAddress(
    _In_ PWSK_SOCKET Socket,
    _Out_ PSOCKADDR  RemoteAddress,
    _Inout_ PIRP     Irp
    );

NTSTATUS
WSKAPI
VioWskSend(
    _In_ PWSK_SOCKET Socket,
    _In_ PWSK_BUF    Buffer,
    _In_ ULONG       Flags,
    _Inout_ PIRP     Irp
    );

NTSTATUS
WSKAPI
VioWskReceive(
    _In_ PWSK_SOCKET Socket,
    _In_ PWSK_BUF    Buffer,
    _In_ ULONG       Flags,
    _Inout_ PIRP     Irp
    );

NTSTATUS
WSKAPI
VioWskDisconnect(
    _In_ PWSK_SOCKET  Socket,
    _In_opt_ PWSK_BUF Buffer,
    _In_ ULONG        Flags,
    _Inout_ PIRP      Irp
    );

NTSTATUS
WSKAPI
VioWskRelease(
    _In_ PWSK_SOCKET          Socket,
    _In_ PWSK_DATA_INDICATION DataIndication
    );

NTSTATUS
WSKAPI
VioWskConnectEx(
    _In_ PWSK_SOCKET  Socket,
    _In_ PSOCKADDR    RemoteAddress,
    _In_opt_ PWSK_BUF Buffer,
    _Reserved_ ULONG  Flags,
    _Inout_ PIRP      Irp
    );

NTSTATUS
WSKAPI
VioWskSendEx(
    _In_ PWSK_SOCKET Socket,
    _In_ PWSK_BUF    Buffer,
    _In_ ULONG       Flags,
    _In_ ULONG       ControlInfoLength,
    _In_reads_bytes_opt_(ControlInfoLength) PCMSGHDR ControlInfo,
    _Inout_ PIRP     Irp
    );

NTSTATUS
WSKAPI
VioWskReceiveEx(
    _In_ PWSK_SOCKET   Socket,
    _In_ PWSK_BUF      Buffer,
    _In_ ULONG         Flags,
    _Inout_opt_ PULONG ControlInfoLength,
    _Out_writes_bytes_opt_(*ControlInfoLength) PCMSGHDR ControlInfo,
    _Reserved_ PULONG  ControlFlags,
    _Inout_ PIRP       Irp
    );

NTSTATUS
WSKAPI
VioWskListen(
    _In_ PWSK_SOCKET Socket,
    _Inout_ PIRP     Irp
    );

//////////////////////////////////////////////////////////////////////////
WSK_PROVIDER_BASIC_DISPATCH gBasicDispatch =
{
    VioWskControlSocket,
    VioWskCloseSocket
};

WSK_PROVIDER_LISTEN_DISPATCH gListenDispatch =
{
    {
        VioWskControlSocket,
        VioWskCloseSocket
    },
    VioWskBind,
    VioWskAccept,
    VioWskInspectComplete,
    VioWskGetLocalAddress
};

WSK_PROVIDER_CONNECTION_DISPATCH gConnectionDispatch =
{
    {
        VioWskControlSocket,
        VioWskCloseSocket
    },
    VioWskBind,
    VioWskConnect,
    VioWskGetLocalAddress,
    VioWskGetRemoteAddress,
    VioWskSend,
    VioWskReceive,
    VioWskDisconnect,
    VioWskRelease,
    VioWskConnectEx,
    VioWskSendEx,
    VioWskReceiveEx
};

#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
WSK_PROVIDER_STREAM_DISPATCH gStreamDispatch =
{
    {
        VioWskControlSocket,
        VioWskCloseSocket
    },
    VioWskBind,
    VioWskAccept,
    VioWskConnect,
    VioWskListen,
    VioWskSend,
    VioWskReceive,
    VioWskDisconnect,
    VioWskRelease,
    VioWskGetLocalAddress,
    VioWskGetRemoteAddress,
    VioWskConnectEx,
    VioWskSendEx,
    VioWskReceiveEx
};
#endif // if (NTDDI_VERSION >= NTDDI_WIN10_RS2)

//////////////////////////////////////////////////////////////////////////
NTSTATUS
WSKAPI
VioWskControlSocket(
    _In_ PWSK_SOCKET                    Socket,
    _In_ WSK_CONTROL_SOCKET_TYPE        RequestType,
    _In_ ULONG                          ControlCode,
    _In_ ULONG                          Level,
    _In_ SIZE_T                         InputSize,
    _In_reads_bytes_opt_(InputSize) PVOID    InputBuffer,
    _In_ SIZE_T                         OutputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID  OutputBuffer,
    _Out_opt_ SIZE_T                   *OutputSizeReturned,
    _Inout_opt_ PIRP                    Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(RequestType);
    UNREFERENCED_PARAMETER(ControlCode);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(InputSize);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(OutputSize);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputSizeReturned);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskCloseSocket(
    _In_ PWSK_SOCKET Socket,
    _Inout_ PIRP     Irp
)
{
    UNREFERENCED_PARAMETER(Socket);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskBind(
    _In_ PWSK_SOCKET Socket,
    _In_ PSOCKADDR   LocalAddress,
    _Reserved_ ULONG Flags,
    _Inout_ PIRP     Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(LocalAddress);
    UNREFERENCED_PARAMETER(Flags);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskAccept(
    _In_ PWSK_SOCKET                               ListenSocket,
    _Reserved_ ULONG                               Flags,
    _In_opt_ PVOID                                 AcceptSocketContext,
    _In_opt_ CONST WSK_CLIENT_CONNECTION_DISPATCH *AcceptSocketDispatch,
    _Out_opt_ PSOCKADDR                            LocalAddress,
    _Out_opt_ PSOCKADDR                            RemoteAddress,
    _Inout_ PIRP                                   Irp
)
{
    UNREFERENCED_PARAMETER(ListenSocket);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(AcceptSocketContext);
    UNREFERENCED_PARAMETER(AcceptSocketDispatch);
    UNREFERENCED_PARAMETER(LocalAddress);
    UNREFERENCED_PARAMETER(RemoteAddress);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskInspectComplete(
    _In_ PWSK_SOCKET        ListenSocket,
    _In_ PWSK_INSPECT_ID    InspectID,
    _In_ WSK_INSPECT_ACTION Action,
    _Inout_ PIRP            Irp
)
{
    UNREFERENCED_PARAMETER(ListenSocket);
    UNREFERENCED_PARAMETER(InspectID);
    UNREFERENCED_PARAMETER(Action);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskGetLocalAddress(
    _In_ PWSK_SOCKET Socket,
    _Out_ PSOCKADDR  LocalAddress,
    _Inout_ PIRP     Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(LocalAddress);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskConnect(
    _In_ PWSK_SOCKET Socket,
    _In_ PSOCKADDR   RemoteAddress,
    _Reserved_ ULONG Flags,
    _Inout_ PIRP     Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(RemoteAddress);
    UNREFERENCED_PARAMETER(Flags);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}


NTSTATUS
WSKAPI
VioWskGetRemoteAddress(
    _In_ PWSK_SOCKET Socket,
    _Out_ PSOCKADDR  RemoteAddress,
    _Inout_ PIRP     Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(RemoteAddress);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskSend(
    _In_ PWSK_SOCKET Socket,
    _In_ PWSK_BUF    Buffer,
    _In_ ULONG       Flags,
    _Inout_ PIRP     Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Flags);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskReceive(
    _In_ PWSK_SOCKET Socket,
    _In_ PWSK_BUF    Buffer,
    _In_ ULONG       Flags,
    _Inout_ PIRP     Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Flags);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskDisconnect(
    _In_ PWSK_SOCKET  Socket,
    _In_opt_ PWSK_BUF Buffer,
    _In_ ULONG        Flags,
    _Inout_ PIRP      Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Flags);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskRelease(
    _In_ PWSK_SOCKET          Socket,
    _In_ PWSK_DATA_INDICATION DataIndication
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(DataIndication);

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
WSKAPI
VioWskConnectEx(
    _In_ PWSK_SOCKET  Socket,
    _In_ PSOCKADDR    RemoteAddress,
    _In_opt_ PWSK_BUF Buffer,
    _Reserved_ ULONG  Flags,
    _Inout_ PIRP      Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(RemoteAddress);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Flags);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskSendEx(
    _In_ PWSK_SOCKET Socket,
    _In_ PWSK_BUF    Buffer,
    _In_ ULONG       Flags,
    _In_ ULONG       ControlInfoLength,
    _In_reads_bytes_opt_(ControlInfoLength) PCMSGHDR ControlInfo,
    _Inout_ PIRP     Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(ControlInfoLength);
    UNREFERENCED_PARAMETER(ControlInfo);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskReceiveEx(
    _In_ PWSK_SOCKET   Socket,
    _In_ PWSK_BUF      Buffer,
    _In_ ULONG         Flags,
    _Inout_opt_ PULONG ControlInfoLength,
    _Out_writes_bytes_opt_(*ControlInfoLength) PCMSGHDR ControlInfo,
    _Reserved_ PULONG  ControlFlags,
    _Inout_ PIRP       Irp
)
{
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(ControlInfoLength);
    UNREFERENCED_PARAMETER(ControlInfo);
    UNREFERENCED_PARAMETER(ControlFlags);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
WSKAPI
VioWskListen(
    _In_ PWSK_SOCKET Socket,
    _Inout_ PIRP     Irp
)
{
    UNREFERENCED_PARAMETER(Socket);

    return VioWskCompleteIrp(Irp, STATUS_NOT_IMPLEMENTED, 0);
}
