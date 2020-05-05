/*
 * Socket object functions
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
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
#include "viosock.h"

#if defined(EVENT_TRACING)
#include "Socket.tmh"
#endif

NTSTATUS
VIOSockBind(
    IN WDFREQUEST   Request
);
#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockCreate)
#pragma alloc_text (PAGE, VIOSockClose)

#pragma alloc_text (PAGE, VIOSockBind)

#pragma alloc_text (PAGE, VIOSockDeviceControl)
#endif

//////////////////////////////////////////////////////////////////////////
NTSTATUS
VIOSockBoundListInit(
    IN WDFDEVICE hDevice
)
{
    NTSTATUS                status;
    PDEVICE_CONTEXT         pContext = GetDeviceContext(hDevice);
    WDF_OBJECT_ATTRIBUTES   collectionAttributes;

    // Create collection for socket objects
    WDF_OBJECT_ATTRIBUTES_INIT(&collectionAttributes);
    collectionAttributes.ParentObject = hDevice;

    status = WdfCollectionCreate(&collectionAttributes, &pContext->BoundList);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfCollectionCreate failed - 0x%x\n", status);
    }
    else
    {
        WDF_OBJECT_ATTRIBUTES   lockAttributes;

        WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
        lockAttributes.ParentObject = hDevice;
        status = WdfSpinLockCreate(&lockAttributes, &pContext->BoundLock);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfSpinLockCreate failed - 0x%x\n", status);
        }
    }
    return status;
}

NTSTATUS
VIOSockBoundAdd(
    IN PSOCKET_CONTEXT pSocket,
    IN ULONG32         svm_port
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);
    static ULONG32  uSvmPort;
    ULONG           uSeed = (ULONG)(ULONG_PTR)pSocket;
    NTSTATUS        status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    if (svm_port == VMADDR_PORT_ANY)
    {
        BOOLEAN bFound = FALSE;
        ULONG i;

        for (i = 0; i < MAX_PORT_RETRIES; ++i)
        {
            if (!uSvmPort)
                uSvmPort = RtlRandomEx((PULONG)&uSeed);

            if (uSvmPort == VMADDR_PORT_ANY)
            {
                uSvmPort = 0;
                continue;
            }

            if (uSvmPort <= LAST_RESERVED_PORT)
                uSvmPort += LAST_RESERVED_PORT + 1;

            svm_port = uSvmPort++;

            if (!VIOSockBoundFindByPort(pContext, svm_port))
            {
                bFound = TRUE;
                break;
            }
        }

        if (!bFound)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "No ports available\n");
            return STATUS_NOT_FOUND;
        }
    }
    else
    {
        if (VIOSockBoundFindByPort(pContext, svm_port))
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_SOCKET, "Local port %u is already in use\n", svm_port);
            return STATUS_ADDRESS_ALREADY_ASSOCIATED;
        }
    }

    pSocket->src_port = svm_port;

    WdfSpinLockAcquire(pContext->BoundLock);
    status = WdfCollectionAdd(pContext->BoundList, pSocket->ThisSocket);
    if (NT_SUCCESS(status))
        VIOSockSetFlag(pSocket, SOCK_BOUND);
    WdfSpinLockRelease(pContext->BoundLock);
    return status;
}

__inline
VOID
VIOSockBoundRemove(
    IN PSOCKET_CONTEXT pSocket
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(pSocket->ThisSocket));

    WdfSpinLockAcquire(pContext->BoundLock);
    if (VIOSockIsFlag(pSocket, SOCK_BOUND))
    {
        WdfCollectionRemove(pContext->BoundList, pSocket->ThisSocket);
        VIOSockResetFlag(pSocket, SOCK_BOUND);
    }
    WdfSpinLockRelease(pContext->BoundLock);
}

typedef
BOOLEAN
(*PSOCKET_CALLBACK)(
    IN PSOCKET_CONTEXT pSocket,
    IN PVOID pCallbackContext
    );

PSOCKET_CONTEXT
VIOSockBoundEnum(
    IN PDEVICE_CONTEXT  pContext,
    IN PSOCKET_CALLBACK pEnumCallback,
    IN PVOID            pCallbackContext
)
{
    PSOCKET_CONTEXT pSocket = NULL;
    ULONG i, ItemCount;

    WdfSpinLockAcquire(pContext->BoundLock);
    ItemCount = WdfCollectionGetCount(pContext->BoundList);
    for (i = 0; i < ItemCount; ++i)
    {
        PSOCKET_CONTEXT pCurrentSocket = GetSocketContext(WdfCollectionGetItem(pContext->BoundList, i));

        if (pEnumCallback(pCurrentSocket, pCallbackContext))
        {
            pSocket = pCurrentSocket;
            break;
        }
    }
    WdfSpinLockRelease(pContext->BoundLock);

    return pSocket;
}

static
BOOLEAN
VIOSockBoundFindByPortCallback(
    IN PSOCKET_CONTEXT  pSocket,
    IN PVOID            pCallbackContext
)
{
    return pSocket->src_port == (ULONG32)(ULONG_PTR)pCallbackContext;
}

PSOCKET_CONTEXT
VIOSockBoundFindByPort(
    IN PDEVICE_CONTEXT pContext,
    IN ULONG32         ulSrcPort
)
{
    return VIOSockBoundEnum(pContext, VIOSockBoundFindByPortCallback, (PVOID)ulSrcPort);
}

VOID
VIOSockCreate(
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfDevice);
    PSOCKET_CONTEXT pSocket;
    NTSTATUS                status = STATUS_SUCCESS;
    WDF_REQUEST_PARAMETERS parameters;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "%s\n", __FUNCTION__);

    ASSERT(FileObject);
    if (WDF_NO_HANDLE == FileObject)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE,"NULL FileObject\n");
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    pSocket = GetSocketContext(FileObject);
    pSocket->ThisSocket = FileObject;

    WDF_REQUEST_PARAMETERS_INIT(&parameters);

    //check EA presents
    WdfRequestGetParameters(Request, &parameters);

    if (parameters.Parameters.Create.EaLength)
    {
        PFILE_FULL_EA_INFORMATION EaBuffer=
            (PFILE_FULL_EA_INFORMATION)WdfRequestWdmGetIrp(Request)->AssociatedIrp.SystemBuffer;

        ASSERT(EaBuffer);

        if (EaBuffer->EaValueLength >= sizeof(VIRTIO_VSOCK_PARAMS))
        {
            HANDLE hListenSocket;
            PVIRTIO_VSOCK_PARAMS pParams = (PVIRTIO_VSOCK_PARAMS)((PCHAR)EaBuffer +
                (FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + 1 + EaBuffer->EaNameLength));

            //validate EA
#ifdef _WIN64
            if (WdfRequestIsFrom32BitProcess(Request))
            {
                hListenSocket = Handle32ToHandle((void * POINTER_32)(ULONG)pParams->Socket);
            }
            else
#endif //_WIN64
            {
                hListenSocket = (HANDLE)pParams->Socket;
            }

            //find listen socket
            if (hListenSocket)
            {
                PFILE_OBJECT pFileObj;

                status = ObReferenceObjectByHandle(hListenSocket, STANDARD_RIGHTS_REQUIRED, *IoFileObjectType,
                    KernelMode, (PVOID)&pFileObj, NULL);

                if (NT_SUCCESS(status))
                {
                    //TODO: lock collection
                    ULONG i, ItemCount = WdfCollectionGetCount(pContext->SocketList);
                    for (i = 0; i < ItemCount; ++i)
                    {
                        WDFFILEOBJECT CurrentFile = WdfCollectionGetItem(pContext->SocketList, i);

                        ASSERT(CurrentFile);
                        if (WdfFileObjectWdmGetFileObject(CurrentFile) == pFileObj)
                        {
                            //TODO: Check socket state
                            WdfObjectReference(CurrentFile);
                            pSocket->ListenSocket = CurrentFile;
                        }
                    }

                    ObDereferenceObject(pFileObj);

                    if (!pSocket->ListenSocket)
                    {
                        TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "Listen socket not found\n");
                        status = STATUS_INVALID_DEVICE_STATE;
                    }
                }
                else
                {
                    TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "ObReferenceObjectByHandle failed: %x\n", status);
                    status = STATUS_INVALID_PARAMETER;
                }
            }

            //TODO: lock collection
            if (NT_SUCCESS(status))
                status = WdfCollectionAdd(pContext->SocketList, FileObject);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_CREATE_CLOSE, "Invalid EA length\n");
            status = STATUS_INVALID_PARAMETER;
        }
    }
    else
    {
        //Create socket for config retrieving only
        VIOSockSetFlag(pSocket, SOCK_CONTROL);
    }

    WdfRequestComplete(Request, status);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "<-- %s\n", __FUNCTION__);
}

VOID
VIOSockClose(
    IN WDFFILEOBJECT FileObject
)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(FileObject));
    PSOCKET_CONTEXT pSocket = GetSocketContext(FileObject);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "--> %s\n", __FUNCTION__);

    if (VIOSockIsFlag(pSocket, SOCK_CONTROL))
        return;

    if (pSocket->ListenSocket)
    {
        WdfObjectDereference(pSocket->ListenSocket);
        pSocket->ListenSocket = WDF_NO_HANDLE;
    }

        //TODO: lock collection
        WdfCollectionRemove(pContext->SocketList, FileObject);


    VIOSockBoundRemove(pSocket);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "<-- %s\n", __FUNCTION__);
}

__inline
BOOLEAN
VIOSockIsGuestAddrValid(
    IN PSOCKADDR_VM    pAddr
)
{
    return (pAddr->svm_family == AF_VSOCK) &&
        (pAddr->svm_cid != VMADDR_CID_HYPERVISOR) &&
        (pAddr->svm_cid != VMADDR_CID_RESERVED) &&
        (pAddr->svm_cid != VMADDR_CID_HOST);
}

static
NTSTATUS
VIOSockBind(
    IN WDFREQUEST   Request
)
{
    PSOCKET_CONTEXT pSocket = GetSocketContextFromRequest(Request);
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PSOCKADDR_VM    pAddr;
    SIZE_T          stAddrLen;
    NTSTATUS        status;
    static ULONG32  uSvmPort;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pAddr), &pAddr, &stAddrLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stAddrLen >= sizeof(*pAddr));

    if (!VIOSockIsGuestAddrValid(pAddr))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid addr to bind, cid: %u, port: %u\n", pAddr->svm_cid, pAddr->svm_port);
        return STATUS_INVALID_PARAMETER;
    }

    if (pAddr->svm_cid != (ULONG32)pContext->Config.guest_cid &&
        pAddr->svm_cid != VMADDR_CID_ANY)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid cid: %u\n", pAddr->svm_cid);
        return STATUS_INVALID_PARAMETER;

    }

    status = VIOSockBoundAdd(pSocket, pAddr->svm_port);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SOCKET, "VIOSockBoundAdd failed: 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    return status;
}

NTSTATUS
VIOSockDeviceControl(
    IN WDFREQUEST Request,
    IN ULONG      IoControlCode,
    IN OUT size_t *pLength
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);

    *pLength = 0;

    switch (IoControlCode)
    {
    case IOCTL_SOCKET_BIND:
        status = VIOSockBind(Request);
        break;

    default:
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "Invalid socket ioctl\n");
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s\n", __FUNCTION__);
    return status;
}