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

#ifndef _WSK_UTILS_H
#define _WSK_UTILS_H

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER


#include "..\inc\vio_wsk.h"
#include "viowsk.h"


_Must_inspect_result_
NTSTATUS
VioWskIrpAcquire(
    _In_opt_ PVIOWSK_SOCKET Socket,
    _Inout_                 PIRP Irp
);

void
VioWskIrpRelease(
    _In_opt_ PVIOWSK_SOCKET Socket,
    _In_                    PIRP Irp
);

NTSTATUS
VioWskIrpComplete(
    _Inout_opt_ PVIOWSK_SOCKET Socket,
    _In_ PIRP                  Irp,
    _In_ NTSTATUS              Status,
    _In_ ULONG_PTR             Information
);

void
VioWskIrpFree(
    _Inout_ PIRP            Irp,
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN            Completion
);

_Must_inspect_result_
NTSTATUS
VioWskAddressPartToString(
    _In_ ULONG            Value,
    _Out_ PUNICODE_STRING String
);

_Must_inspect_result_
NTSTATUS
VioWskStringToAddressPart(
    _In_ PUNICODE_STRING String,
    _Out_ PULONG         Value
);

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
);

_Must_inspect_result_
NTSTATUS
VioWskSocketBuildIOCTL(
    _In_ PVIOWSK_SOCKET Socket,
    _In_ ULONG          ControlCode,
    _In_opt_ PVOID      InputBuffer,
    _In_ ULONG          InputBufferLength,
    _In_opt_ PVOID      OutputBuffer,
    _In_ ULONG          OutputBufferLength,
    _Out_ PIRP*         Irp
);


_Must_inspect_result_
NTSTATUS
VioWskSocketReadWrite(
    _In_ PVIOWSK_SOCKET Socket,
    const WSK_BUF      *Buffers,
    _In_ UCHAR          MajorFunction,
    _Inout_ PIRP        Irp
);


_Must_inspect_result_
NTSTATUS
VioWskSocketBuildReadWriteSingleMdl(
    _In_ PVIOWSK_SOCKET Socket,
    _In_ PMDL           Mdl,
    _In_ ULONG          Offset,
    _In_ ULONG          Length,
    _In_ UCHAR          MajorFunction,
    _Out_ PIRP*         Irp
);


NTSTATUS
WskBufferValidate(
    _In_ const WSK_BUF* Buffer,
    _Out_ PULONG FirstMdlLength,
    _Out_ PULONG LastMdlLength
);


#endif
