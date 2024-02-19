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
#include "viowsk.h"
#include "..\inc\debug-utils.h"
#include "..\inc\vio_wsk.h"
#include "viowsk-internal.h"
#include "wsk-completion.h"
#include "wsk-utils.h"
#ifdef EVENT_TRACING
#include "wsk-utils.tmh"
#endif

#ifdef ALLOC_PRAGMA
#endif




_Must_inspect_result_
NTSTATUS
VioWskIrpAcquire(
    _In_opt_ PVIOWSK_SOCKET Socket,
	_Inout_ PIRP            Irp
)
{
    LONG reqCount = 0;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("Socket=0x%p; Irp=0x%p", Socket, Irp);

    Status = STATUS_SUCCESS;

    if (Socket)
    {
        Status = IoAcquireRemoveLock(&Socket->CloseRemoveLock, Irp);
        if (NT_SUCCESS(Status))
            reqCount = InterlockedIncrement(&Socket->RequestCount);

        if (!NT_SUCCESS(Status))
            Status = STATUS_FILE_FORCED_CLOSED;
    }

    DEBUG_EXIT_FUNCTION("0x%x, reqCount=%i", Status, reqCount);
    return Status;
}


void
VioWskIrpRelease(
    _In_opt_ PVIOWSK_SOCKET Socket,
	_In_ PIRP               Irp
)
{
    LONG reqCount = 0;
    DEBUG_ENTER_FUNCTION("Socket=0x%p; Irp=0x%p", Socket, Irp);

    if (Socket)
    {
        IoReleaseRemoveLock(&Socket->CloseRemoveLock, Irp);
        reqCount = InterlockedDecrement(&Socket->RequestCount);
    }

    DEBUG_EXIT_FUNCTION("void, reqCount=%i", reqCount);
    return;
}



NTSTATUS
VioWskIrpComplete(
	_Inout_opt_ PVIOWSK_SOCKET Socket,
	_In_ PIRP                  Irp,
	_In_ NTSTATUS              Status,
	_In_ ULONG_PTR             Information
)
{
    DEBUG_ENTER_FUNCTION("Socket=0x%p; Irp=0x%p; Status=0x%x; Information=%Iu", Socket, Irp, Status, (ULONG64)Information);

    IoSetCancelRoutine(Irp, NULL);
    if (Socket)
        VioWskIrpRelease(Socket, Irp);

    Irp->IoStatus.Information = Information;
    Irp->IoStatus.Status = Status;
    IoSetNextIrpStackLocation(Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}


void
VioWskIrpFree(
    _Inout_ PIRP            Irp,
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN            Completion
)
{
    DEBUG_ENTER_FUNCTION("Irp=0x%p; DeviceObject=0x%p; Completion=%u", Irp, DeviceObject, Completion);

    if (Irp->MdlAddress)
    {
        MmPrepareMdlForReuse(Irp->MdlAddress);
        IoFreeMdl(Irp->MdlAddress);
        Irp->MdlAddress = NULL;
    }

    if ((Irp->Flags & IRP_BUFFERED_IO) != 0 &&
        (Irp->Flags & IRP_DEALLOCATE_BUFFER) != 0)
    {
        Irp->Flags &= ~(IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER);
        ExFreePoolWithTag(Irp->AssociatedIrp.SystemBuffer, VIOSOCK_WSK_MEMORY_TAG);
        Irp->AssociatedIrp.SystemBuffer = NULL;
    }

    IoFreeIrp(Irp);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}


_Must_inspect_result_
NTSTATUS
VioWskAddressPartToString(
    _In_ ULONG            Value,
    _Out_ PUNICODE_STRING String
)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("Value=%u; String=0x%p", Value, String);

    String->Length = 0;
    Status = RtlIntegerToUnicodeString(Value, 0, String);
    if (!NT_SUCCESS(Status))
        goto Exit;

    if (String->Length == String->MaximumLength) {
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    String->Buffer[String->Length / sizeof(wchar_t)] = L'\0';

Exit:
    DEBUG_EXIT_FUNCTION("0x%x, *String=\"%wZ\"", Status, String);
    return Status;
}

_Must_inspect_result_
NTSTATUS
VioWskStringToAddressPart(
    _In_ PUNICODE_STRING String,
    _Out_ PULONG         Value
)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("String=\"%wZ\"; Value=0x%p", String, Value);

    *Value = 0;
    if (String->Length > 0) {
        if (String->Buffer[0] < L'0' || String->Buffer[0] > '9') {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        Status = RtlUnicodeStringToInteger(String, 0, Value);
        if (!NT_SUCCESS(Status))
            goto Exit;
    }

Exit:
    DEBUG_EXIT_FUNCTION("0x%x, *Value=%u", Status, *Value);
    return Status;
}



_Must_inspect_result_
NTSTATUS
VioWskSocketIOCTL(
    _In_ PVIOWSK_SOCKET        Socket,
    _In_ ULONG                 ControlCode,
    _In_opt_ PVOID             InputBuffer,
    _In_ ULONG                 InputBufferLength,
    _Out_opt_ PVOID            OutputBuffer,
    _In_ ULONG                 OutputBufferLength,
    _Inout_opt_ PIRP           Irp,
    _Out_opt_ PIO_STATUS_BLOCK IoStatusBlock
)
{
	PIRP IOCTLIrp = NULL;
	PVIOWSK_REG_CONTEXT pContext = NULL;
	PWSK_REGISTRATION Registraction = NULL;
	NTSTATUS Status = STATUS_UNSUCCESSFUL;
	PVIOSOCKET_COMPLETION_CONTEXT CompContext = NULL;
    DEBUG_ENTER_FUNCTION("Socket=0x%p; ControlCode=0x%x; InputBuffer=0x%p; InputBufferLength=%u; OutputBuffer=0x%p; OutputBufferLength=%u; Irp=0x%p; IoStatusBLock=0x%p", Socket, ControlCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength, Irp, IoStatusBlock);

    Registraction = (PWSK_REGISTRATION)Socket->Client;
    pContext = (PVIOWSK_REG_CONTEXT)Registraction->ReservedRegistrationContext;
    Status = VioWskSocketBuildIOCTL(Socket, ControlCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength, &IOCTLIrp);
    if (!NT_SUCCESS(Status))
        goto Complete;

    CompContext = WskCompContextAlloc(wsksSingleIOCTL, Socket, Irp, IoStatusBlock);
    if (CompContext == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto FreeIOCTL;
	}

	Status = WskCompContextSendIrp(CompContext, IOCTLIrp);
    WskCompContextDereference(CompContext);
    if (NT_SUCCESS(Status))
        IOCTLIrp = NULL;

    Irp = NULL;

FreeIOCTL:
    if (IOCTLIrp)
        VioWskIrpFree(IOCTLIrp, NULL, FALSE);
Complete:
    if (Irp)
        VioWskIrpComplete(Socket, Irp, Status, 0);

	DEBUG_EXIT_FUNCTION("0x%x", Status);
	return Status;
}


_Must_inspect_result_
NTSTATUS
VioWskSocketBuildIOCTL(
    _In_ PVIOWSK_SOCKET  Socket,
    _In_ ULONG           ControlCode,
    _In_opt_ PVOID       InputBuffer,
    _In_ ULONG           InputBufferLength,
    _In_opt_ PVOID       OutputBuffer,
    _In_ ULONG           OutputBufferLength,
    _Out_ PIRP          *Irp
)
{
    PIRP IOCTLIrp = NULL;
    ULONG method = 0;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PIO_STACK_LOCATION IrpStack = NULL;
    PVIOWSK_REG_CONTEXT pContext = NULL;
    PWSK_REGISTRATION Registraction = NULL;
    DEBUG_ENTER_FUNCTION("Socket=0x%p; ControlCode=0x%x; InputBuffer=0x%p; InputBufferLength=%u; OutputBuffer=0x%p; OutputBufferLength=%u; Irp=0x%p", Socket, ControlCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength, Irp);

    Status = STATUS_SUCCESS;
    Registraction = (PWSK_REGISTRATION)Socket->Client;
    pContext = (PVIOWSK_REG_CONTEXT)Registraction->ReservedRegistrationContext;
    IOCTLIrp = IoAllocateIrp(pContext->VIOSockDevice->StackSize, FALSE);
    if (!IOCTLIrp)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    IOCTLIrp->Tail.Overlay.Thread = PsGetCurrentThread();
    IrpStack = IoGetNextIrpStackLocation(IOCTLIrp);
    IrpStack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    IrpStack->DeviceObject = pContext->VIOSockDevice;
    IrpStack->FileObject = Socket->FileObject;
    IrpStack->Parameters.DeviceIoControl.InputBufferLength = InputBufferLength;
    IrpStack->Parameters.DeviceIoControl.OutputBufferLength = OutputBufferLength;
    IrpStack->Parameters.DeviceIoControl.IoControlCode = ControlCode;
    method = ControlCode & 3;
    switch (method)
    {
    case METHOD_BUFFERED:
        if (InputBufferLength != 0 || OutputBufferLength != 0)
        {
            IOCTLIrp->AssociatedIrp.SystemBuffer = ExAllocatePoolUninitialized(
                NonPagedPool, 
                InputBufferLength > OutputBufferLength ? InputBufferLength : OutputBufferLength,
                VIOSOCK_WSK_MEMORY_TAG);

            if (IOCTLIrp->AssociatedIrp.SystemBuffer)
            {
                if (InputBuffer)
                    memcpy(IOCTLIrp->AssociatedIrp.SystemBuffer, InputBuffer, InputBufferLength);

                IOCTLIrp->Flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER;
                IOCTLIrp->UserBuffer = OutputBuffer;
                if (OutputBuffer)
                    IOCTLIrp->Flags |= IRP_INPUT_OPERATION;
            }
            else Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else {
            IOCTLIrp->Flags = 0;
            IOCTLIrp->UserBuffer = NULL;
        }
        break;
    case METHOD_IN_DIRECT:
    case METHOD_OUT_DIRECT:
        if (InputBuffer)
        {
            IOCTLIrp->AssociatedIrp.SystemBuffer = ExAllocatePoolUninitialized(
                NonPagedPool,
                InputBufferLength,
                VIOSOCK_WSK_MEMORY_TAG);

            if (IOCTLIrp->AssociatedIrp.SystemBuffer)
            {
                memcpy(IOCTLIrp->AssociatedIrp.SystemBuffer, InputBuffer, InputBufferLength);
                IOCTLIrp->Flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER;
            }
            else Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else IOCTLIrp->Flags = 0;

        if (NT_SUCCESS(Status) && OutputBuffer) {
            IOCTLIrp->MdlAddress = IoAllocateMdl(
                OutputBuffer,
                OutputBufferLength,
                FALSE,
                FALSE,
                NULL);

            if (!IOCTLIrp->MdlAddress)
            {
                if (InputBuffer)
                    ExFreePoolWithTag(IOCTLIrp->AssociatedIrp.SystemBuffer, VIOSOCK_WSK_MEMORY_TAG);

                Status = STATUS_INSUFFICIENT_RESOURCES;
            }

            if (NT_SUCCESS(Status)) {
                __try
                {
                    MmProbeAndLockPages(IOCTLIrp->MdlAddress, KernelMode, (LOCK_OPERATION)((method == METHOD_IN_DIRECT) ? IoReadAccess : IoWriteAccess));
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    if (IOCTLIrp->MdlAddress)
                        IoFreeMdl(IOCTLIrp->MdlAddress);

                    if (InputBuffer)
                        ExFreePoolWithTag(IOCTLIrp->AssociatedIrp.SystemBuffer, VIOSOCK_WSK_MEMORY_TAG);

                    Status = GetExceptionCode();
                }
            }
        }
        break;
    case METHOD_NEITHER:
        IOCTLIrp->UserBuffer = OutputBuffer;
        IrpStack->Parameters.DeviceIoControl.Type3InputBuffer = InputBuffer;
        break;
	}

    if (NT_SUCCESS(Status)) {
        *Irp = IOCTLIrp;
        IOCTLIrp = NULL;
    }

    if (IOCTLIrp)
        VioWskIrpFree(IOCTLIrp, NULL, FALSE);
Exit:
	DEBUG_EXIT_FUNCTION("0x%x, *Irp=0x%p", Status, *Irp);
	return Status;
}


_Must_inspect_result_
NTSTATUS
VioWskSocketReadWrite(
    _In_ PVIOWSK_SOCKET Socket,
    const WSK_BUF      *Buffers,
    _In_ UCHAR          MajorFunction,
    _Inout_ PIRP        Irp
)
{
    PIRP OpIrp = NULL;
    ULONG firstMdlLength = 0;
    ULONG lastMdlLength = 0;
    EWSKState state = wsksUndefined;
    PVIOWSK_REG_CONTEXT pContext = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PWSK_REGISTRATION Registraction = NULL;
    PVIOSOCKET_COMPLETION_CONTEXT CompContext = NULL;
	DEBUG_ENTER_FUNCTION("Sockect=0x%p; Buffers=0x%p; MajorFunction=%u; Irp=0x%p", Socket, Buffers, MajorFunction, Irp);

    Registraction = (PWSK_REGISTRATION)Socket->Client;
    pContext = (PVIOWSK_REG_CONTEXT)Registraction->ReservedRegistrationContext;
    Status = WskBufferValidate(Buffers, &firstMdlLength, &lastMdlLength);
    if (!NT_SUCCESS(Status))
        goto CompleteParentIrp;

    switch (MajorFunction)
    {
    case IRP_MJ_READ:
        state = wsksReceive;
        break;
    case IRP_MJ_WRITE:
        state = wsksSend;
        break;
    default:
        Status = STATUS_INVALID_PARAMETER_3;
        goto CompleteParentIrp;
        break;
    }

    Status = VioWskSocketBuildReadWriteSingleMdl(Socket, Buffers->Mdl, Buffers->Offset, firstMdlLength, MajorFunction, &OpIrp);
    if (!NT_SUCCESS(Status))
        goto CompleteParentIrp;

    CompContext = WskCompContextAlloc(state, Socket, Irp, NULL);
    if (!CompContext)
        goto FreeOpIrp;

    CompContext->Specific.Transfer.CurrentMdlSize = firstMdlLength;
    CompContext->Specific.Transfer.LastMdlSize = lastMdlLength;
    CompContext->Specific.Transfer.NextMdl = Buffers->Mdl->Next;
    Status = WskCompContextSendIrp(CompContext, OpIrp);
    WskCompContextDereference(CompContext);
    if (NT_SUCCESS(Status))
        OpIrp = NULL;
        
    Irp = NULL;

FreeOpIrp:
    if (OpIrp)
        VioWskIrpFree(OpIrp, NULL, FALSE);
CompleteParentIrp:
    if (Irp)
        VioWskIrpComplete(Socket, Irp, Status, 0);

    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}


_Must_inspect_result_
NTSTATUS
VioWskSocketBuildReadWriteSingleMdl(
    _In_ PVIOWSK_SOCKET Socket,
    _In_ PMDL           Mdl,
    _In_ ULONG          Offset,
    _In_ ULONG          Length,
    _In_ UCHAR          MajorFunction,
    _Out_ PIRP         *Irp
)
{
    PIRP OpIrp = NULL;
    PMDL OpMdl = NULL;
    PVOID mdlBuffer = NULL;
    PIO_STACK_LOCATION IrpStack = NULL;
    PVIOWSK_REG_CONTEXT pContext = NULL;
    PWSK_REGISTRATION Registraction = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("Socket=0x%p; Mdl=0x%p; MajorFunction=%u; Offset=%u; Length=%u; Irp=0x%p", Socket, Mdl, Offset, Length, MajorFunction, Irp);

    Registraction = (PWSK_REGISTRATION)Socket->Client;
    pContext = (PVIOWSK_REG_CONTEXT)Registraction->ReservedRegistrationContext;
    mdlBuffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
    if (!mdlBuffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    OpIrp = IoAllocateIrp(pContext->VIOSockDevice->StackSize, FALSE);
    if (!OpIrp)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    OpMdl = IoAllocateMdl((unsigned char *)mdlBuffer + Offset, Length, FALSE, FALSE, NULL);
    if (!OpMdl)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto FreeIrp;
    }

    IoBuildPartialMdl(Mdl, OpMdl, (unsigned char *)mdlBuffer + Offset, Length);
    OpIrp->MdlAddress = OpMdl;
    OpIrp->UserIosb = NULL;
    OpIrp->Tail.Overlay.Thread = PsGetCurrentThread();
    IrpStack = IoGetNextIrpStackLocation(OpIrp);
    IrpStack->MajorFunction = MajorFunction;
    IrpStack->DeviceObject = pContext->VIOSockDevice;
    IrpStack->FileObject = Socket->FileObject;
    switch (MajorFunction)
    {
        case IRP_MJ_READ:
            IrpStack->Parameters.Read.Flags = 0;
            IrpStack->Parameters.Read.ByteOffset.QuadPart = 0;
            IrpStack->Parameters.Read.Key = 0;
            IrpStack->Parameters.Read.Length = Length;
            break;
        case IRP_MJ_WRITE:
            IrpStack->Parameters.Write.Flags = 0;
            IrpStack->Parameters.Write.ByteOffset.QuadPart = 0;
            IrpStack->Parameters.Write.Key = 0;
            IrpStack->Parameters.Write.Length = Length;
            break;
        default:
            ASSERT(FALSE);
            break;
    }

    *Irp = OpIrp;
    OpIrp = NULL;
    Status = STATUS_SUCCESS;

FreeIrp:
    if (OpIrp)
        VioWskIrpFree(OpIrp, NULL, FALSE);
Exit:
    DEBUG_EXIT_FUNCTION("0x%x, *Irp=0x%p", Status, *Irp);
    return Status;
}


NTSTATUS
WskBufferValidate(
    _In_ const WSK_BUF* Buffer,
    _Out_ PULONG FirstMdlLength,
    _Out_ PULONG LastMdlLength
)
{
    PMDL mdl = NULL;
    ULONG offset = 0;
    SIZE_T length = 0;
    ULONG mdlLength = 0;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("Buffer=0x%p; FirstMdlLength=0x%p; LastMdlLength=0x%p", Buffer, FirstMdlLength, LastMdlLength);

    *FirstMdlLength = 0;
    *LastMdlLength = 0;
    status = STATUS_SUCCESS;
    length = Buffer->Length;
    offset = Buffer->Offset;
    mdl = Buffer->Mdl;
    if (mdl != NULL)
    {
        mdlLength = MmGetMdlByteCount(mdl);
        if (offset <= mdlLength)
        {
            while (TRUE)
            {
                ULONG effectiveLength = mdlLength - offset;

                if (length < effectiveLength)
                    effectiveLength = (ULONG)length;

                if (mdl == Buffer->Mdl)
                    *FirstMdlLength = effectiveLength;
                    
                mdl = mdl->Next;
                length -= effectiveLength;
                if (length == 0 || mdl == NULL)
                {
                    *LastMdlLength = effectiveLength;
                    break;
                }

                mdlLength = MmGetMdlByteCount(mdl);
                offset = 0;
            }
        }
        else status = STATUS_INVALID_PARAMETER;
    }
    else if (length != 0)
        status = STATUS_INVALID_PARAMETER;

    DEBUG_EXIT_FUNCTION("0x%x, *FirstMdlLength=%u, *LastMdlLength=%u", status, *FirstMdlLength, *LastMdlLength);
    return status;
}

