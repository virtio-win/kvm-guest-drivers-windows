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
        pSocket->IsControl = TRUE;
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

    if (pSocket->ListenSocket)
    {
        WdfObjectDereference(pSocket->ListenSocket);
        pSocket->ListenSocket = WDF_NO_HANDLE;
    }

    if (!pSocket->IsControl)
    {
        //TODO: lock collection
        WdfCollectionRemove(pContext->SocketList, FileObject);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_CREATE_CLOSE,
        "<-- %s\n", __FUNCTION__);
}
