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

#ifndef __VIOWSK_INTERNAL_H__
#define __VIOWSK_INTERNAL_H__



extern PDRIVER_OBJECT _viowskDriverObject;
extern PDEVICE_OBJECT _viowskDeviceObject;


_Must_inspect_result_
NTSTATUS
VioWskSocketInternal(
    _In_ PWSK_CLIENT              Client,
    _In_opt_ PVIOWSK_SOCKET       ListenSocket,
    _In_ ULONG                    Flags,
    _In_opt_ PVOID                SocketContext,
    _In_opt_ CONST VOID* Dispatch,
    _In_opt_ PEPROCESS            OwningProcess,
    _In_opt_ PETHREAD             OwningThread,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Out_ PVIOWSK_SOCKET* pNewSocket
);

NTSTATUS
VioWskCloseSocketInternal(
    _Inout_ PVIOWSK_SOCKET Socket,
    _In_opt_ PVOID         ReleleaseTag
);

NTSTATUS
WSKAPI
VioWskCloseSocket(
    _In_ PWSK_SOCKET Socket,
    _Inout_ PIRP     Irp
);

NTSTATUS
WSKAPI
VioWskAccept(
    _In_ PWSK_SOCKET                               ListenSocket,
    _Reserved_ ULONG                               Flags,
    _In_opt_ PVOID                                 AcceptSocketContext,
    _In_opt_ CONST WSK_CLIENT_CONNECTION_DISPATCH* AcceptSocketDispatch,
    _Out_opt_ PSOCKADDR                            LocalAddress,
    _Out_opt_ PSOCKADDR                            RemoteAddress,
    _Inout_ PIRP                                   Irp
);



#endif
