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
#include "..\inc\debug-utils.h"
#include "viowsk.h"
#include "wsk-utils.h"
#include "..\inc\vio_wsk.h"
#include "viowsk-internal.h"
#include "wsk-workitem.h"
#ifdef EVENT_TRACING
#include "wsk-workitem.tmh"
#endif

#pragma warning(disable : 4996)

static void _ProcessWorkItem(_In_ PWSK_WORKITEM WorkItem)
{
    DEBUG_ENTER_FUNCTION("WorkItem=0x%p", WorkItem);

    switch (WorkItem->Type)
    {
        case wskwitSocket:
            VioWskSocket(WorkItem->Specific.Socket.Client,
                         WorkItem->Specific.Socket.AddressFamily,
                         WorkItem->Specific.Socket.SocketType,
                         WorkItem->Specific.Socket.Protocol,
                         WorkItem->Specific.Socket.Flags,
                         WorkItem->Specific.Socket.SocketContext,
                         WorkItem->Specific.Socket.Dispatch,
                         WorkItem->Specific.Socket.OwningProcess,
                         WorkItem->Specific.Socket.OwningThread,
                         WorkItem->Specific.Socket.SecurityDescriptor,
                         WorkItem->Irp);
            break;
        case wskwitSocketAndConnect:
            VioWskSocketConnect(WorkItem->Specific.SocketConnect.Client,
                                WorkItem->Specific.SocketConnect.SocketType,
                                WorkItem->Specific.SocketConnect.Protocol,
                                WorkItem->Specific.SocketConnect.LocalAddress,
                                WorkItem->Specific.SocketConnect.RemoteAddress,
                                WorkItem->Specific.SocketConnect.Flags,
                                WorkItem->Specific.SocketConnect.SocketContext,
                                WorkItem->Specific.SocketConnect.Dispatch,
                                WorkItem->Specific.SocketConnect.OwningProcess,
                                WorkItem->Specific.SocketConnect.OwningThread,
                                WorkItem->Specific.SocketConnect.SecurityDescriptor,
                                WorkItem->Irp);
            break;
        case wskwitCloseSocket:
            VioWskCloseSocket(WorkItem->Specific.CloseSocket.Socket, WorkItem->Irp);
            break;
        case wskwitAccept:
            {
                PVIOWSK_SOCKET ListenSocket = NULL;

                ListenSocket = CONTAINING_RECORD(WorkItem->Specific.Accept.ListenSocket, VIOWSK_SOCKET, WskSocket);
                VioWskAccept(WorkItem->Specific.Accept.ListenSocket,
                             WorkItem->Specific.Accept.Flags,
                             WorkItem->Specific.Accept.AcceptSocketContext,
                             WorkItem->Specific.Accept.AcceptSocketDispatch,
                             WorkItem->Specific.Accept.LocalAddress,
                             WorkItem->Specific.Accept.RemoteAddress,
                             WorkItem->Irp);
                VioWskIrpRelease(ListenSocket, WorkItem->Irp);
            }
            break;
        default:
            ASSERT(FALSE);
            break;
    }

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

static void _IoWskRoutine(_In_ PVOID IoObject, _In_opt_ PVOID Context, _In_ PIO_WORKITEM IoWorkItem)
{
    PWSK_WORKITEM WorkItem = (PWSK_WORKITEM)Context;
    DEBUG_ENTER_FUNCTION("IoObject=0x%p; Context=0x%p; IoWorkItem=0x%p", IoObject, Context, IoWorkItem);

    __analysis_assert(WorkItem);
    UNREFERENCED_PARAMETER(IoObject);
    _ProcessWorkItem(WorkItem);
    IoUninitializeWorkItem(IoWorkItem);
    ExFreePoolWithTag(WorkItem, VIOSOCK_WSK_MEMORY_TAG);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

static void _ExWskRoutine(_In_opt_ PVOID Context)
{
    PWSK_WORKITEM WorkItem = (PWSK_WORKITEM)Context;
    DEBUG_ENTER_FUNCTION("Context=0x%p", Context);

    __analysis_assert(WorkItem);
    _ProcessWorkItem(WorkItem);
    ExFreePoolWithTag(WorkItem, VIOSOCK_WSK_MEMORY_TAG);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

PWSK_WORKITEM
WskWorkItemAlloc(_In_ EWSKWorkItemType Type, _In_ PIRP Irp)
{
    PWSK_WORKITEM Ret = NULL;
    SIZE_T TotalSize = sizeof(WSK_WORKITEM);
    BOOLEAN IoWorkItem = (_viowskDeviceObject != NULL);
    DEBUG_ENTER_FUNCTION("Type%u; Irp=0x%p", Type, Irp);

    if (IoWorkItem)
    {
        TotalSize += (IoSizeofWorkItem() - sizeof(Ret->ExWorkItem));
    }

    Ret = ExAllocatePoolWithTag(NonPagedPool, TotalSize, VIOSOCK_WSK_MEMORY_TAG);
    if (Ret)
    {
        memset(Ret, 0, TotalSize);
        Ret->Type = Type;
        Ret->IoMethod = IoWorkItem;
        Ret->Irp = Irp;
        if (Ret->IoMethod)
        {
            IoInitializeWorkItem(_viowskDeviceObject, (PIO_WORKITEM)Ret->IoWorkItem);
        }
        else
        {
            ExInitializeWorkItem(&Ret->ExWorkItem, _ExWskRoutine, Ret);
        }
    }

    DEBUG_EXIT_FUNCTION("0x%p", Ret);
    return Ret;
}

void WskWorkItemQueue(_In_ PWSK_WORKITEM WorkItem)
{
    DEBUG_ENTER_FUNCTION("WorkItem=0x%p", WorkItem);

    if (WorkItem->IoMethod)
    {
        IoQueueWorkItemEx((PIO_WORKITEM)WorkItem->IoWorkItem, _IoWskRoutine, DelayedWorkQueue, WorkItem);
    }
    else
    {
        ExQueueWorkItem(&WorkItem->ExWorkItem, DelayedWorkQueue);
    }

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

void WskWorkItemFree(_In_ PWSK_WORKITEM WorkItem)
{
    if (WorkItem->IoMethod)
    {
        IoUninitializeWorkItem((PIO_WORKITEM)WorkItem->IoWorkItem);
    }

    IoFreeIrp(WorkItem->Irp);
    ExFreePoolWithTag(WorkItem, VIOSOCK_WSK_MEMORY_TAG);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}
