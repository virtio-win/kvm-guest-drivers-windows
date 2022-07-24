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

#ifndef __WSK_WORKITEM_H__
#define __WSK_WORKITEM_H__


#include <ntifs.h>
#include "viowsk.h"



typedef enum _EWSKWorkItemType {
    wskwitUndefined,
    wskwitSocket,
    wskwitSocketAndConnect,
    wskwitCloseSocket,
    wskwitAccept,
    wskwitMax,
} EWSKWorkItemType, *PEWSKWorkItemType;

typedef struct _WSK_WORKITEM {
    EWSKWorkItemType Type;
    BOOLEAN IoMethod;
    PIRP Irp;
    union {
        struct {
            PWSK_CLIENT Client;
            ADDRESS_FAMILY AddressFamily;
            USHORT SocketType;
            ULONG Protocol;
            ULONG Flags;
            PVOID SocketContext;
            const VOID *Dispatch;
            PEPROCESS OwningProcess;
            PETHREAD OwningThread;
            PSECURITY_DESCRIPTOR SecurityDescriptor;
        } Socket;
        struct {
            PWSK_CLIENT Client;
            USHORT SocketType;
            ULONG Protocol;
            PSOCKADDR LocalAddress;
            PSOCKADDR RemoteAddress;
            ULONG Flags;
            PVOID SocketContext;
            const WSK_CLIENT_CONNECTION_DISPATCH *Dispatch;
            PEPROCESS OwningProcess;
            PETHREAD OwningThread;
            PSECURITY_DESCRIPTOR SecurityDescriptor;
        } SocketConnect;
        struct {
            PWSK_SOCKET Socket;
        } CloseSocket;
        struct {
            PWSK_SOCKET ListenSocket;
            ULONG Flags;
            PVOID AcceptSocketContext;
            CONST WSK_CLIENT_CONNECTION_DISPATCH *AcceptSocketDispatch;
            PSOCKADDR LocalAddress;
            PSOCKADDR RemoteAddress;
        } Accept;
    } Specific;
    union {
        WORK_QUEUE_ITEM ExWorkItem;
        unsigned char IoWorkItem[1]; 
    };
} WSK_WORKITEM, *PWSK_WORKITEM;


PWSK_WORKITEM
WskWorkItemAlloc(
    _In_ EWSKWorkItemType Type,
    _In_ PIRP             Irp
);

void
WskWorkItemQueue(
    _In_ PWSK_WORKITEM WorkItem
);



#endif
