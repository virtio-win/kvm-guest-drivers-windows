/*
 * Exports definition for virtio socket WSK interface
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

#ifndef _VIOWSK_H
#define _VIOWSK_H

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER

#define VIOSOCK_WSK_MEMORY_TAG (ULONG)'kswV'

typedef struct _VIOWSK_REG_CONTEXT
{
    PVOID ClientContext;
    WSK_CLIENT_DISPATCH ClientDispatch;
}VIOWSK_REG_CONTEXT,*PVIOWSK_REG_CONTEXT;

#define VIOWSK_PROVIDER_VERSION 1

_At_((void*)Irp->IoStatus.Information, __drv_allocatesMem(Mem))
NTSTATUS
WSKAPI
VioWskSocket(
    _In_ PWSK_CLIENT              Client,
    _In_ ADDRESS_FAMILY           AddressFamily,
    _In_ USHORT                   SocketType,
    _In_ ULONG                    Protocol,
    _In_ ULONG                    Flags,
    _In_opt_ PVOID                SocketContext,
    _In_opt_ CONST VOID          *Dispatch,
    _In_opt_ PEPROCESS            OwningProcess,
    _In_opt_ PETHREAD             OwningThread,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PIRP                  Irp
);

_At_(Irp->IoStatus.Information, __drv_allocatesMem(Mem))
NTSTATUS
WSKAPI
VioWskSocketConnect(
    _In_ PWSK_CLIENT                               Client,
    _In_ USHORT                                    SocketType,
    _In_ ULONG                                     Protocol,
    _In_ PSOCKADDR                                 LocalAddress,
    _In_ PSOCKADDR                                 RemoteAddress,
    _Reserved_ ULONG                               Flags,
    _In_opt_ PVOID                                 SocketContext,
    _In_opt_ CONST WSK_CLIENT_CONNECTION_DISPATCH *Dispatch,
    _In_opt_ PEPROCESS                             OwningProcess,
    _In_opt_ PETHREAD                              OwningThread,
    _In_opt_ PSECURITY_DESCRIPTOR                  SecurityDescriptor,
    _Inout_ PIRP                                   Irp
    );

NTSTATUS
WSKAPI
VioWskControlClient(
    _In_ PWSK_CLIENT                    Client,
    _In_ ULONG                          ControlCode,
    _In_ SIZE_T                         InputSize,
    _In_reads_bytes_opt_(InputSize) PVOID    InputBuffer,
    _In_ SIZE_T                         OutputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID  OutputBuffer,
    _Out_opt_ SIZE_T                   *OutputSizeReturned,
    _Inout_opt_ PIRP                    Irp
);

_At_(*Result, __drv_allocatesMem(Mem))
NTSTATUS
WSKAPI
VioWskGetAddressInfo(
    _In_ PWSK_CLIENT          Client,
    _In_opt_ PUNICODE_STRING  NodeName,
    _In_opt_ PUNICODE_STRING  ServiceName,
    _In_opt_ ULONG            NameSpace,
    _In_opt_ GUID            *Provider,
    _In_opt_ PADDRINFOEXW     Hints,
    _Outptr_ PADDRINFOEXW *Result,
    _In_opt_ PEPROCESS        OwningProcess,
    _In_opt_ PETHREAD         OwningThread,
    _Inout_ PIRP              Irp
);

_At_(AddrInfo, __drv_freesMem(Mem))
VOID
WSKAPI
VioWskFreeAddressInfo(
    _In_ PWSK_CLIENT  Client,
    _In_ PADDRINFOEXW AddrInfo
);

NTSTATUS
WSKAPI
VioWskGetNameInfo(
    _In_ PWSK_CLIENT          Client,
    _In_ PSOCKADDR            SockAddr,
    _In_ ULONG                SockAddrLength,
    _Out_opt_ PUNICODE_STRING NodeName,
    _Out_opt_ PUNICODE_STRING ServiceName,
    _In_ ULONG                Flags,
    _In_opt_ PEPROCESS        OwningProcess,
    _In_opt_ PETHREAD         OwningThread,
    _Inout_ PIRP              Irp
);

__inline
NTSTATUS
VioWskCompleteIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information
)
{
    Irp->IoStatus.Information = Information;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

typedef struct _VIOWSK_SOCKET
{
    WSK_SOCKET WskSocket;
    PVOID SocketContext;
    ULONG Type;
    WSK_CLIENT_LISTEN_DISPATCH ListenDispatch;
    WSK_CLIENT_CONNECTION_DISPATCH ConnectionDispatch;
    union
    {
        WSK_PROVIDER_BASIC_DISPATCH ProviderBasicDispatch;
        WSK_PROVIDER_LISTEN_DISPATCH ProviderListenDispatch;
        WSK_PROVIDER_CONNECTION_DISPATCH ProviderConnectionDispatch;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        WSK_PROVIDER_STREAM_DISPATCH ProviderStreamDispatch;
#endif // if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    }ProviderDispatch;
}VIOWSK_SOCKET,*PVIOWSK_SOCKET;

extern WSK_PROVIDER_BASIC_DISPATCH gBasicDispatch;
extern WSK_PROVIDER_LISTEN_DISPATCH gListenDispatch;
extern WSK_PROVIDER_CONNECTION_DISPATCH gConnectionDispatch;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
extern WSK_PROVIDER_STREAM_DISPATCH gStreamDispatch;
#endif // if (NTDDI_VERSION >= NTDDI_WIN10_RS2)

#endif //_VIOWSK_H
