/*
 * Provider NPI functions
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
#include "wsk-utils.h"
#include "viowsk-internal.h"
#include "wsk-completion.h"




static
void
WskCancelIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp
)
{
    PIRP CurrentIrp = NULL;
    DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p", DeviceObject, Irp);

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    CurrentIrp = InterlockedExchangePointer(Irp->Tail.Overlay.DriverContext + 1, NULL);
    if (CurrentIrp)
        IoCancelIrp(CurrentIrp);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}


static
NTSTATUS
WskGeneralIrpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PVOID          Context
)
{
    EWSKState opState;
    PVIOSOCKET_COMPLETION_CONTEXT Ctx = (PVIOSOCKET_COMPLETION_CONTEXT)Context;
    DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; Irp=0x%p; Context=0x%p", DeviceObject, Irp, Context);

    if (Ctx->MasterIrp)
    {
        InterlockedExchangePointer(Ctx->MasterIrp->Tail.Overlay.DriverContext + 1, NULL);
        if (Ctx->MasterIrp->Cancel)
            Irp->IoStatus.Status = STATUS_CANCELLED;
    }

    UNREFERENCED_PARAMETER(DeviceObject);
    opState = Ctx->State;
    if (NT_SUCCESS(Irp->IoStatus.Status)) {
        switch (opState)
        {
        case wsksSingleIOCTL:
            memcpy(Irp->UserBuffer, Irp->AssociatedIrp.SystemBuffer, Irp->IoStatus.Information);
            opState = wsksFinished;
            break;
        default:
            opState = wsksFinished;
            break;
        }
    }

    IO_STATUS_BLOCK irpStatus;

    irpStatus = Irp->IoStatus;
    VioWskIrpFree(Irp, Ctx->DeviceObject, TRUE);
    if (!NT_SUCCESS(irpStatus.Status) ||
        opState == wsksFinished) {
        if (Ctx->IoStatusBlock)
            *Ctx->IoStatusBlock = irpStatus;

        if (Ctx->BytesReturned)
            *Ctx->BytesReturned = irpStatus.Information;

        if (Ctx->Event)
            KeSetEvent(Ctx->Event, IO_NO_INCREMENT, FALSE);

        if (Ctx->MasterIrp)
        {
            if (!Ctx->UseIOSBInformation)
                Ctx->IOSBInformation = irpStatus.Information;

            VioWskIrpComplete(Ctx->Socket, Ctx->MasterIrp, irpStatus.Status, Ctx->IOSBInformation);
        }
    }

    WskCompContextDereference(Ctx);
    
    DEBUG_EXIT_FUNCTION("0x%x", STATUS_MORE_PROCESSING_REQUIRED);
    return STATUS_MORE_PROCESSING_REQUIRED;
}


static
void
WskCompContextFree(
    PVIOSOCKET_COMPLETION_CONTEXT CompContext
)
{
    DEBUG_ENTER_FUNCTION("CompContext=0x%p", CompContext);

    ExFreePoolWithTag(CompContext, VIOSOCK_WSK_MEMORY_TAG);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}


PVIOSOCKET_COMPLETION_CONTEXT
WskCompContextAlloc(
	_In_ EWSKState            State,
	_In_ PVIOWSK_SOCKET       Socket,
	_In_opt_ PIRP             MasterIrp,
    _In_opt_ PIO_STATUS_BLOCK IoStatusBlock
)
{
    PVIOWSK_REG_CONTEXT pContext = NULL;
    PWSK_REGISTRATION Registration = NULL;
    PVIOSOCKET_COMPLETION_CONTEXT Ret = NULL;
    DEBUG_ENTER_FUNCTION("State=%u; Socket=0x%p; MasterIrp=0x%p; IoStatusBlock=0x%p", State, Socket, MasterIrp, IoStatusBlock);

    Ret = ExAllocatePoolUninitialized(NonPagedPool, sizeof(*Ret), VIOSOCK_WSK_MEMORY_TAG);
    if (!Ret)
        goto Exit;

    memset(Ret, 0, sizeof(*Ret));
    InterlockedExchange(&Ret->ReferenceCount, 1);
    Ret->State = State;
    Ret->Socket = Socket;
    Ret->MasterIrp = MasterIrp;
    Ret->IoStatusBlock = IoStatusBlock;
    Registration = (PWSK_REGISTRATION)Socket->Client;
    pContext = (PVIOWSK_REG_CONTEXT)Registration->ReservedRegistrationContext;
    Ret->DeviceObject = pContext->VIOSockDevice;

Exit:
    DEBUG_EXIT_FUNCTION("0x%p", Ret);
    return Ret;
}

void
WskCompContextReference(
    _Inout_ PVIOSOCKET_COMPLETION_CONTEXT CompContext
)
{
    DEBUG_ENTER_FUNCTION("CompContext=0x%p", CompContext);

    InterlockedIncrement(&CompContext->ReferenceCount);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

void
WskCompContextDereference(
    _Inout_ PVIOSOCKET_COMPLETION_CONTEXT CompContext
)
{
    DEBUG_ENTER_FUNCTION("CompContext=0x%p", CompContext);

    if (InterlockedDecrement(&CompContext->ReferenceCount) == 0)
        WskCompContextFree(CompContext);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}


NTSTATUS
WskCompContextSendIrp(
    _Inout_ PVIOSOCKET_COMPLETION_CONTEXT CompContext,
    _In_ PIRP                             Irp
)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PVIOWSK_REG_CONTEXT pContext = NULL;
    PWSK_REGISTRATION Registration = NULL;
    DEBUG_ENTER_FUNCTION("CompContext=0x%p; Irp=0x%p", CompContext, Irp);

    Status = STATUS_SUCCESS;
    Registration = (PWSK_REGISTRATION)CompContext->Socket->Client;
    pContext = (PVIOWSK_REG_CONTEXT)Registration->ReservedRegistrationContext;
    if (_viowskDeviceObject)
    {
        Status = IoSetCompletionRoutineEx(_viowskDeviceObject, Irp, WskGeneralIrpCompletion, CompContext, TRUE, TRUE, TRUE);
        if (!NT_SUCCESS(Status))
            goto CompleteMasterIrp;
    } else IoSetCompletionRoutine(Irp, WskGeneralIrpCompletion, CompContext, TRUE, TRUE, TRUE);
   
    WskCompContextReference(CompContext);
    if (CompContext->MasterIrp)
    {
        InterlockedExchangePointer(CompContext->MasterIrp->Tail.Overlay.DriverContext + 1, Irp);
        IoSetCancelRoutine(CompContext->MasterIrp, WskCancelIrp);
    }
        
    IoCallDriver(pContext->VIOSockDevice, Irp);
    Status = STATUS_PENDING;
    Irp = NULL;
CompleteMasterIrp:
    if (Irp && CompContext->MasterIrp)
        VioWskIrpComplete(CompContext->Socket, CompContext->MasterIrp, Status, 0);

    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}
