/*
 * Main library file contained exported functions
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
#include "..\inc\debug-utils.h"
#include "viowsk.h"
#include "..\inc\vio_wsk.h"
#include "viowsk-internal.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VioWskRegister)
#pragma alloc_text (PAGE, VioWskDeregister)
#endif


typedef struct _VIOSOCK_WAIT_CONTEXT {
    UNICODE_STRING SymbolicLinkName;
    KEVENT Event;
    NTSTATUS Result;
} VIOSOCK_WAIT_CONTEXT, * PVIOSOCK_WAIT_CONTEXT;


PDRIVER_OBJECT _viowskDriverObject = NULL;
PDEVICE_OBJECT _viowskDeviceObject = NULL;

static const WSK_PROVIDER_DISPATCH _providerDispatch = {
    MAKE_WSK_VERSION(VIOWSK_PROVIDER_VERSION, 0),
    0,
    VioWskSocket,
    VioWskSocketConnect,
    VioWskControlClient,
    VioWskGetAddressInfo,
    VioWskFreeAddressInfo,
    VioWskGetNameInfo,
};



static
NTSTATUS
_NotifyCallback(
    _In_ PVOID    NotificationStructure,
    _Inout_ PVOID Context
)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PVIOSOCK_WAIT_CONTEXT Ctx = (PVIOSOCK_WAIT_CONTEXT)Context;
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION NotifyInfo = (PDEVICE_INTERFACE_CHANGE_NOTIFICATION)NotificationStructure;
    DEBUG_ENTER_FUNCTION("NotificationStructure=0x%p; Context=0x%p", NotificationStructure, Context);

    Status = STATUS_SUCCESS;
    if (IsEqualGUID(&NotifyInfo->Event, &GUID_DEVICE_INTERFACE_ARRIVAL) &&
        NotifyInfo->SymbolicLinkName != NULL)
    {
        Ctx->SymbolicLinkName = *NotifyInfo->SymbolicLinkName;
        Ctx->SymbolicLinkName.MaximumLength = Ctx->SymbolicLinkName.Length;
        Ctx->SymbolicLinkName.Buffer = ExAllocatePoolUninitialized(PagedPool, Ctx->SymbolicLinkName.MaximumLength, VIOSOCK_WSK_MEMORY_TAG);
        if (Ctx->SymbolicLinkName.Buffer == NULL)
            Status = STATUS_INSUFFICIENT_RESOURCES;

        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(Ctx->SymbolicLinkName.Buffer, NotifyInfo->SymbolicLinkName->Buffer, Ctx->SymbolicLinkName.Length);
        }

        Ctx->Result = Status;
        KeSetEvent(&Ctx->Event, IO_NO_INCREMENT, FALSE);
        Status = STATUS_SUCCESS;
    }

    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}

_Must_inspect_result_
static
NTSTATUS
_VioSockDriverConnect(
    _Inout_ PVIOWSK_REG_CONTEXT WskContext,
    _In_ DWORD                  Timeout,
    _In_ PDRIVER_OBJECT         DriverObject)
{
    PVOID NotifyHandle = NULL;
    LARGE_INTEGER Timeout100ns;
    PLARGE_INTEGER pTimeout = NULL;
    VIOSOCK_WAIT_CONTEXT WaitContext;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("WskContext=0x%p; Timeout=%u; DriverObject=0x%p", WskContext, Timeout, DriverObject);

    switch (Timeout) {
    case WSK_NO_WAIT:
        Timeout100ns.QuadPart = 0;
        pTimeout = &Timeout100ns;
        break;
    case WSK_INFINITE_WAIT:
        break;
    default:
        Timeout100ns.QuadPart = Timeout;
        Timeout100ns.QuadPart *= -10000;
        pTimeout = &Timeout100ns;
        break;
    }

    KeInitializeEvent(&WaitContext.Event, NotificationEvent, FALSE);
    WaitContext.Result = STATUS_UNSUCCESSFUL;
    Status = IoRegisterPlugPlayNotification(
        EventCategoryDeviceInterfaceChange,
        PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
        (PVOID)&GUID_DEVINTERFACE_VIOSOCK,
        DriverObject,
        _NotifyCallback,
        &WaitContext,
        &NotifyHandle);

    if (!NT_SUCCESS(Status))
    {
        DEBUG_ERROR("IoRegisterPlugPlayNotification: 0x%x", Status);
        goto Exit;
    }

    Status = KeWaitForSingleObject(&WaitContext.Event, Executive, KernelMode, FALSE, pTimeout);
#if (NTDDI_VERSION >= NTDDI_WIN7)
    IoUnregisterPlugPlayNotificationEx(NotifyHandle);
#else // if (NTDDI_VERSION >= NTDDI_WIN7)
    IoUnregisterPlugPlayNotification(NotifyHandle);
#endif // if (NTDDI_VERSION < NTDDI_WIN7)
    switch (Status) {
    case STATUS_WAIT_0:
        Status = WaitContext.Result;
        if (!NT_SUCCESS(Status))
        {
            DEBUG_ERROR("The interface arrival notificaiton routine failed: 0x%x", Status);
            break;
        }

        Status = IoGetDeviceObjectPointer(&WaitContext.SymbolicLinkName, FILE_READ_ATTRIBUTES, &WskContext->VIOSockMainFileObject, &WskContext->VIOSockDevice);
        if (!NT_SUCCESS(Status))
        {
            DEBUG_ERROR("IoGetDeviceObjectPointer: 0x%x", Status);
            ExFreePoolWithTag(WaitContext.SymbolicLinkName.Buffer, VIOSOCK_WSK_MEMORY_TAG);
            break;
        }

        WskContext->VIOSockLinkName = WaitContext.SymbolicLinkName;
        break;
    case STATUS_TIMEOUT:
        Status = STATUS_DEVICE_NOT_READY;
        break;
    }

Exit:
    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}

static
VOID
_VIOSockDriverDisconnect(
    _Inout_ PVIOWSK_REG_CONTEXT RegContext)
{
    DEBUG_ENTER_FUNCTION("RegContext=0x%p", RegContext);

    ObDereferenceObject(RegContext->VIOSockMainFileObject);
    RegContext->VIOSockMainFileObject = NULL;
    ExFreePoolWithTag(RegContext->VIOSockLinkName.Buffer, VIOSOCK_WSK_MEMORY_TAG);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}



_Must_inspect_result_
NTSTATUS
VioWskRegister(
    _In_ PWSK_CLIENT_NPI    WskClientNpi,
    _Out_ PWSK_REGISTRATION WskRegistration
)
{
    PVIOWSK_REG_CONTEXT pContext;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("WskClientNpi=0x%p; WskRegistration=0x%p", WskClientNpi, WskRegistration);

    PAGED_CODE();

    pContext = ExAllocatePoolUninitialized(NonPagedPoolNx,
        sizeof(VIOWSK_REG_CONTEXT), VIOSOCK_WSK_MEMORY_TAG);

    if (!pContext)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory(pContext, sizeof(VIOWSK_REG_CONTEXT));
    Status = ExInitializeResourceLite(&pContext->NPILock);
    if (!NT_SUCCESS(Status))
        goto FreeContext;

    pContext->ClientContext = WskClientNpi->ClientContext;
    if (WskClientNpi->Dispatch)
        RtlCopyMemory(&pContext->ClientDispatch, WskClientNpi->Dispatch, sizeof(*WskClientNpi->Dispatch));

    WskRegistration->ReservedRegistrationContext = pContext;

FreeContext:
    if (!NT_SUCCESS(Status))
        ExFreePoolWithTag(pContext, VIOSOCK_WSK_MEMORY_TAG);
Exit:
    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}

VOID
VioWskDeregister(
    _In_ PWSK_REGISTRATION WskRegistration
)
{
    PVIOWSK_REG_CONTEXT pContext = NULL;
    DEBUG_ENTER_FUNCTION("WskRegistration=0x%p", WskRegistration);

    pContext = WskRegistration->ReservedRegistrationContext;
    if (pContext != NULL)
    {
        ExDeleteResourceLite(&pContext->NPILock);
        ExFreePoolWithTag(pContext, VIOSOCK_WSK_MEMORY_TAG);
        WskRegistration->ReservedRegistrationContext = NULL;
    }

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

_Must_inspect_result_
NTSTATUS
VioWskCaptureProviderNPI(
    _In_ PWSK_REGISTRATION  WskRegistration,
    _In_ ULONG              WaitTimeout,
    _Out_ PWSK_PROVIDER_NPI WskProviderNpi
)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PVIOWSK_REG_CONTEXT regContext = (PVIOWSK_REG_CONTEXT)WskRegistration->ReservedRegistrationContext;
    DEBUG_ENTER_FUNCTION("WskRegistration=0x%p; WaitTimeout=%u; WskProviderNpi=0x%p", WskRegistration, WaitTimeout, WskProviderNpi);

    if (KeGetCurrentIrql() >= APC_LEVEL)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status = STATUS_SUCCESS;
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&regContext->NPILock, TRUE);
    if (InterlockedIncrement(&regContext->NPICount) == 1)
    {
        Status = _VioSockDriverConnect(regContext, WaitTimeout, _viowskDriverObject);
        if (!NT_SUCCESS(Status))
            InterlockedDecrement(&regContext->NPICount);
	}

    ExReleaseResourceLite(&regContext->NPILock);
    KeLeaveCriticalRegion();
    if (NT_SUCCESS(Status))
    {
        WskProviderNpi->Client = (PWSK_CLIENT)WskRegistration;
        WskProviderNpi->Dispatch =  &_providerDispatch;
    }

Exit:
    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}

VOID
VioWskReleaseProviderNPI(
    _In_ PWSK_REGISTRATION WskRegistration
)
{
    PVIOWSK_REG_CONTEXT regContext = (PVIOWSK_REG_CONTEXT)WskRegistration->ReservedRegistrationContext;
    DEBUG_ENTER_FUNCTION("WskRegistration=0x%p", WskRegistration);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&regContext->NPILock, TRUE);
    if (InterlockedDecrement(&regContext->NPICount) == 0)
        _VIOSockDriverDisconnect(regContext);

    ExReleaseResourceLite(&regContext->NPILock);
    KeLeaveCriticalRegion();

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

_Must_inspect_result_
NTSTATUS
VioWskQueryProviderCharacteristics(
    _In_ PWSK_REGISTRATION              WskRegistration,
    _Out_ PWSK_PROVIDER_CHARACTERISTICS WskProviderCharacteristics
)
{
    if (!WskRegistration)
        return STATUS_INVALID_PARAMETER;

    WskProviderCharacteristics->HighestVersion = MAKE_WSK_VERSION(VIOWSK_PROVIDER_VERSION, 0);
    WskProviderCharacteristics->LowestVersion = MAKE_WSK_VERSION(VIOWSK_PROVIDER_VERSION, 0);

    return STATUS_SUCCESS;
}

_Must_inspect_result_
NTSTATUS
VioWskModuleInit(
    _In_ PDRIVER_OBJECT     DriverObject,
    _In_ PUNICODE_STRING    RegistryPath,
    _In_opt_ PDEVICE_OBJECT DeviceObject
)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("DriverObject=0x%p; RegistryPath=\"%wZ\"", DriverObject, RegistryPath);

    UNREFERENCED_PARAMETER(RegistryPath);

    Status = STATUS_SUCCESS;
    ObReferenceObject(DriverObject);
    _viowskDriverObject = DriverObject;
    if (DeviceObject) {
        ObReferenceObject(DeviceObject);
        _viowskDeviceObject = DeviceObject;
    }

    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}

VOID
VioWskModuleFinit(
    VOID
)
{
    DEBUG_ENTER_FUNCTION_NO_ARGS();

    if (_viowskDeviceObject) {
        ObDereferenceObject(_viowskDeviceObject);
        _viowskDeviceObject = NULL;
    }

    ObDereferenceObject(_viowskDriverObject);
    _viowskDriverObject = NULL;

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}
