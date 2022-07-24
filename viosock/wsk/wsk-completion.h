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

#ifndef __WSK_COMPLETION_H__
#define __WSK_COMPLETION_H__



#include "..\inc\vio_wsk.h"



typedef enum _EWSKState {
    wsksUndefined,
    wsksSingleIOCTL,
    wsksSend,
    wsksReceive,
    wsksFinished,
} EWSKState, * PEWSKState;

typedef struct _VIOSOCKET_COMPLETION_CONTEXT {
    volatile LONG ReferenceCount;
    PVIOWSK_SOCKET Socket;
    PDEVICE_OBJECT DeviceObject;
    EWSKState State;
    PIRP MasterIrp;
    PIO_STATUS_BLOCK IoStatusBlock;
    PSIZE_T BytesReturned;
    PKEVENT Event;
    ULONG_PTR IOSBInformation;
    int UseIOSBInformation : 1;
    union {
        struct {
            PMDL NextMdl;
            ULONG CurrentMdlSize;
            ULONG LastMdlSize;
        } Transfer;
    } Specific;
} VIOSOCKET_COMPLETION_CONTEXT, * PVIOSOCKET_COMPLETION_CONTEXT;



PVIOSOCKET_COMPLETION_CONTEXT
WskCompContextAlloc(
    _In_ EWSKState            State,
    _In_ PVIOWSK_SOCKET       Socket,
    _In_opt_ PIRP             MasterIrp,
    _In_opt_ PIO_STATUS_BLOCK IoStatusBlock
);

void
WskCompContextReference(
    _Inout_ PVIOSOCKET_COMPLETION_CONTEXT CompContext
);

void
WskCompContextDereference(
    _Inout_ PVIOSOCKET_COMPLETION_CONTEXT CompContext
);

NTSTATUS
WskCompContextSendIrp(
    _Inout_ PVIOSOCKET_COMPLETION_CONTEXT CompContext,
    _In_ PIRP                             Irp
);



#endif
