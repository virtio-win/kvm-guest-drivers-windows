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

#include "precomp.h"
#include "viowsk.h"
#include "..\inc\debug-utils.h"
#include "wsk-utils.h"
#include "viowsk-internal.h"
#ifdef EVENT_TRACING
#include "socket-internal.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VioWskSocketInternal)
#pragma alloc_text(PAGE, VioWskCloseSocketInternal)
#endif

_Must_inspect_result_ static NTSTATUS _VioSocketCreate(_In_ PWSK_CLIENT Client,
                                                       _In_opt_ PVIOWSK_SOCKET ListenSocket,
                                                       _In_ ULONG SocketType,
                                                       _Out_ PHANDLE SocketFileHandle,
                                                       _Out_ PFILE_OBJECT *SocketFileObject)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PVIOWSK_REG_CONTEXT pContext = NULL;
    PWSK_REGISTRATION reg = NULL;
    HANDLE hFile = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Iosb = {0};
    VIRTIO_VSOCK_PARAMS SockParams = {0};
    UCHAR EaBuffer[sizeof(FILE_FULL_EA_INFORMATION) + sizeof(SockParams)] = {0};
    PFILE_FULL_EA_INFORMATION pEaBuffer = (PFILE_FULL_EA_INFORMATION)EaBuffer;
    DEBUG_ENTER_FUNCTION("Client=0x%p; ListenSocket=0x%p; Sockettype=%u; SocketFileHandle=0x%p; SocketFileObject=0x%p",
                         Client,
                         ListenSocket,
                         SocketType,
                         SocketFileHandle,
                         SocketFileObject);

    PAGED_CODE();
    reg = (PWSK_REGISTRATION)Client;
    pContext = (PVIOWSK_REG_CONTEXT)reg->ReservedRegistrationContext;
    RtlSecureZeroMemory(&SockParams, sizeof(SockParams));
    if (ListenSocket != NULL)
    {
        SockParams.Socket = (ULONGLONG)ListenSocket->FileHandle;
    }

    SockParams.Type = SocketType;
    pEaBuffer->EaNameLength = sizeof(FILE_FULL_EA_INFORMATION) - FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) -
                              sizeof(UCHAR);
    pEaBuffer->EaValueLength = sizeof(SockParams);
    RtlCopyMemory(&EaBuffer[sizeof(FILE_FULL_EA_INFORMATION)], &SockParams, sizeof(SockParams));

    InitializeObjectAttributes(&ObjectAttributes, &pContext->VIOSockLinkName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = ZwCreateFile(&hFile,
                          FILE_GENERIC_READ | FILE_GENERIC_WRITE,
                          &ObjectAttributes,
                          &Iosb,
                          0,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE,
                          pEaBuffer,
                          pEaBuffer ? sizeof(EaBuffer) : 0);

    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(hFile,
                                           FILE_GENERIC_READ | FILE_GENERIC_WRITE,
                                           *IoFileObjectType,
                                           KernelMode,
                                           (PVOID *)SocketFileObject,
                                           NULL);
        if (NT_SUCCESS(Status))
        {
            *SocketFileHandle = hFile;
        }
    }

    DEBUG_EXIT_FUNCTION("0x%x, *SocketFileHandle=0x%p, *SocketFileObject=0x%p",
                        Status,
                        *SocketFileHandle,
                        *SocketFileObject);
    return Status;
}

static NTSTATUS _ConfigIrpComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PKEVENT event = (PKEVENT)Context;
    DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p; Context=0x%p", DeviceObject, Irp, Context);

    KeSetEvent(event, IO_NO_INCREMENT, FALSE);
    status = STATUS_MORE_PROCESSING_REQUIRED;

    DEBUG_EXIT_FUNCTION("0x%x", status);
    return status;
}

_Must_inspect_result_ NTSTATUS VioWskSocketInternal(_In_ PWSK_CLIENT Client,
                                                    _In_opt_ PVIOWSK_SOCKET ListenSocket,
                                                    _In_ ULONG Flags,
                                                    _In_opt_ PVOID SocketContext,
                                                    _In_opt_ CONST VOID *Dispatch,
                                                    _In_opt_ PEPROCESS OwningProcess,
                                                    _In_opt_ PETHREAD OwningThread,
                                                    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
                                                    _Out_ PVIOWSK_SOCKET *pNewSocket)
{
    PWSK_CLIENT_LISTEN_DISPATCH pListenDispatch = NULL;
    PWSK_CLIENT_CONNECTION_DISPATCH pConnectionDispatch = NULL;
    PVIOWSK_SOCKET pSocket = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PVOID pProviderDispatch = NULL;
    SIZE_T ProviderDispatchSize = 0;
    VIRTIO_VSOCK_CONFIG SocketConfig;
    PIRP ConfigIrp = NULL;
    KEVENT ConfigEvent;
    DEBUG_ENTER_FUNCTION("Client=0x%p; ListenSocket=0x%p; Flags=0x%x; SocketContext=0x%p; Dispatch=0x%p; "
                         "OwningProcess=0x%p; OwningThread=0x%p; SecurityDescriptor=0x%p; pNewSocket=0x%p",
                         Client,
                         ListenSocket,
                         Flags,
                         SocketContext,
                         Dispatch,
                         OwningProcess,
                         OwningThread,
                         SecurityDescriptor,
                         pNewSocket);

    PAGED_CODE();
    UNREFERENCED_PARAMETER(OwningProcess);
    UNREFERENCED_PARAMETER(OwningThread);
    UNREFERENCED_PARAMETER(SecurityDescriptor);

    *pNewSocket = NULL;
    Status = STATUS_SUCCESS;

    switch (Flags)
    {
        case WSK_FLAG_BASIC_SOCKET:
            pProviderDispatch = &gBasicDispatch;
            ProviderDispatchSize = sizeof(gBasicDispatch);
            break;
        case WSK_FLAG_LISTEN_SOCKET:
            pListenDispatch = (PWSK_CLIENT_LISTEN_DISPATCH)Dispatch;
            pProviderDispatch = &gListenDispatch;
            ProviderDispatchSize = sizeof(gListenDispatch);
            break;
        case WSK_FLAG_CONNECTION_SOCKET:
            pConnectionDispatch = (PWSK_CLIENT_CONNECTION_DISPATCH)Dispatch;
            pProviderDispatch = &gConnectionDispatch;
            ProviderDispatchSize = sizeof(gConnectionDispatch);
            break;
        case WSK_FLAG_DATAGRAM_SOCKET:
            Status = STATUS_NOT_SUPPORTED;
            break;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        case WSK_FLAG_STREAM_SOCKET:
            if (Dispatch)
            {
                pListenDispatch = (PWSK_CLIENT_LISTEN_DISPATCH)((PWSK_CLIENT_STREAM_DISPATCH)Dispatch)->Listen;
                pConnectionDispatch = (PWSK_CLIENT_CONNECTION_DISPATCH)((PWSK_CLIENT_STREAM_DISPATCH)Dispatch)->Connect;
            }

            pProviderDispatch = &gStreamDispatch;
            ProviderDispatchSize = sizeof(gStreamDispatch);
            break;
#endif // if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    pSocket = ExAllocatePoolUninitialized(NonPagedPoolNx, sizeof(VIOWSK_SOCKET), VIOSOCK_WSK_MEMORY_TAG);
    if (!pSocket)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    memset(pSocket, 0, sizeof(VIOWSK_SOCKET));
    pSocket->Client = Client;
    pSocket->SocketContext = SocketContext;
    pSocket->Type = Flags;
    if (pListenDispatch)
    {
        RtlCopyMemory(&pSocket->ListenDispatch, pListenDispatch, sizeof(*pListenDispatch));
    }

    if (pConnectionDispatch)
    {
        RtlCopyMemory(&pSocket->ConnectionDispatch, pConnectionDispatch, sizeof(*pConnectionDispatch));
    }

    if (pProviderDispatch)
    {
        RtlCopyMemory(&pSocket->ProviderDispatch, pProviderDispatch, ProviderDispatchSize);
    }

    pSocket->WskSocket.Dispatch = &pSocket->ProviderDispatch;
    Status = _VioSocketCreate(Client, ListenSocket, SOCK_STREAM, &pSocket->FileHandle, &pSocket->FileObject);
    if (!NT_SUCCESS(Status))
    {
        goto FreeSocket;
    }

    KeInitializeEvent(&ConfigEvent, NotificationEvent, FALSE);
    IoInitializeRemoveLock(&pSocket->CloseRemoveLock, VIOSOCK_WSK_MEMORY_TAG, 0, 0x7FFFFFFF);
    ConfigIrp = IoAllocateIrp(1, FALSE);
    if (!ConfigIrp)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto CloseSocket;
    }

    IoSetCompletionRoutine(ConfigIrp, _ConfigIrpComplete, &ConfigEvent, TRUE, TRUE, TRUE);
    Status = VioWskIrpAcquire(pSocket, ConfigIrp);
    if (!NT_SUCCESS(Status))
    {
        goto FreeConfigIrp;
    }

    Status = VioWskSocketIOCTL(pSocket,
                               IOCTL_GET_CONFIG,
                               NULL,
                               0,
                               &SocketConfig,
                               sizeof(SocketConfig),
                               ConfigIrp,
                               NULL);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&ConfigEvent, Executive, KernelMode, FALSE, NULL);
        Status = ConfigIrp->IoStatus.Status;
    }

    if (NT_SUCCESS(Status) && ConfigIrp->IoStatus.Information < sizeof(SocketConfig))
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    if (!NT_SUCCESS(Status))
    {
        goto FreeConfigIrp;
    }

    pSocket->GuestId = SocketConfig.guest_cid;
    *pNewSocket = pSocket;
    pSocket = NULL;

FreeConfigIrp:
    IoFreeIrp(ConfigIrp);
CloseSocket:
    if (pSocket)
    {
        ZwClose(pSocket->FileHandle);
        ObDereferenceObject(pSocket->FileObject);
    }
FreeSocket:
    if (pSocket)
    {
        ExFreePoolWithTag(pSocket, VIOSOCK_WSK_MEMORY_TAG);
    }
Exit:
    DEBUG_EXIT_FUNCTION("0x%x, *pNewSocket=0x%p", Status, *pNewSocket);
    return Status;
}

NTSTATUS
VioWskCloseSocketInternal(_Inout_ PVIOWSK_SOCKET Socket, _In_opt_ PVOID ReleaseTag)
{
    HANDLE fileHandle = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("Socket=0x%p; ReleaseTag=0x%p", Socket, ReleaseTag);

    PAGED_CODE();
    fileHandle = InterlockedExchangePointer(&Socket->FileHandle, NULL);
    if (fileHandle)
    {
        Status = ZwClose(fileHandle);
        if (ReleaseTag)
        {
            IoReleaseRemoveLockAndWait(&Socket->CloseRemoveLock, ReleaseTag);
        }

        ObDereferenceObject(Socket->FileObject);
        ExFreePoolWithTag(Socket, VIOSOCK_WSK_MEMORY_TAG);
    }

    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}
