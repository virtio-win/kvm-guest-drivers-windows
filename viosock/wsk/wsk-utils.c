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
#include "wsk-utils.h"

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
    DEBUG_ENTER_FUNCTION("Socket=0x%p; Irp=0x%p; Status=0x%x; Information=%zu", Socket, Irp, Status, Information);

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
