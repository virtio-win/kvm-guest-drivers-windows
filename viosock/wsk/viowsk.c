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
#include "viowsk.h"
#include "..\inc\vio_wsk.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VioWskRegister)
#pragma alloc_text (PAGE, VioWskDeregister)
#endif

_Must_inspect_result_
NTSTATUS
VioWskRegister(
    _In_ PWSK_CLIENT_NPI    WskClientNpi,
    _Out_ PWSK_REGISTRATION WskRegistration
)
{
    PVIOWSK_REG_CONTEXT pContext;

    PAGED_CODE();

    if (!WskClientNpi || !WskRegistration)
        return STATUS_INVALID_PARAMETER;

    pContext = ExAllocatePoolWithTag(NonPagedPoolNx,
        sizeof(VIOWSK_REG_CONTEXT), VIOSOCK_WSK_MEMORY_TAG);

    if (!pContext)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(pContext, sizeof(VIOWSK_REG_CONTEXT));

    pContext->ClientContext = WskClientNpi->ClientContext;
    if (WskClientNpi->Dispatch)
        RtlCopyMemory(&pContext->ClientDispatch, WskClientNpi->Dispatch, sizeof(*WskClientNpi->Dispatch));

    WskRegistration->ReservedRegistrationContext = pContext;

    return STATUS_SUCCESS;
}

VOID
VioWskDeregister(
    _In_ PWSK_REGISTRATION WskRegistration
)
{
    if (!WskRegistration)
        return;

    if (WskRegistration->ReservedRegistrationContext)
    {
        ExFreePool(WskRegistration->ReservedRegistrationContext);
        WskRegistration->ReservedRegistrationContext = NULL;
    }
}

_Must_inspect_result_
NTSTATUS
VioWskCaptureProviderNPI(
    _In_ PWSK_REGISTRATION  WskRegistration,
    _In_ ULONG              WaitTimeout,
    _Out_ PWSK_PROVIDER_NPI WskProviderNpi
)
{
    WSK_PROVIDER_DISPATCH *Dispatch;

    UNREFERENCED_PARAMETER(WaitTimeout);

    if (!WskProviderNpi || !WskProviderNpi->Dispatch || !WskRegistration)
        return STATUS_INVALID_PARAMETER;

    Dispatch = (WSK_PROVIDER_DISPATCH *)WskProviderNpi->Dispatch;

    //TODO: open viosock and handle WaitTimeout
    WskProviderNpi->Client = (PWSK_CLIENT)WskRegistration;

    Dispatch->Version = MAKE_WSK_VERSION(VIOWSK_PROVIDER_VERSION, 0);
    Dispatch->Reserved = 0;

    Dispatch->WskSocket = VioWskSocket;
    Dispatch->WskSocketConnect = VioWskSocketConnect;
    Dispatch->WskControlClient = VioWskControlClient;
    Dispatch->WskGetAddressInfo = VioWskGetAddressInfo;
    Dispatch->WskFreeAddressInfo = VioWskFreeAddressInfo;
    Dispatch->WskGetNameInfo = VioWskGetNameInfo;

    return STATUS_SUCCESS;
}

VOID
VioWskReleaseProviderNPI(
    _In_ PWSK_REGISTRATION WskRegistration
)
{
    UNREFERENCED_PARAMETER(WskRegistration);
    //TODO: close viosock
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

