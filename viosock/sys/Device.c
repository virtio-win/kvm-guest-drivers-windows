/*
 * Placeholder for the device related functions
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
#include "Device.tmh"
#endif

EVT_WDF_DEVICE_PREPARE_HARDWARE VIOSockEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VIOSockEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY VIOSockEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT VIOSockEvtDeviceD0Exit;
EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED VIOSockEvtDeviceD0EntryPostInterruptsEnabled;
EVT_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND VIOSockEvtDeviceSelfManagedIoSuspend;
EVT_WDF_DEVICE_SELF_MANAGED_IO_RESTART VIOSockEvtDeviceSelfManagedIoRestart;

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VIOSockEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_DEFAULT VIOSockEvtIoCreate;
EVT_WDF_IO_IN_CALLER_CONTEXT VIOSockEvtIoInCallerContext;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VIOSockSgCtxCleanup;
EVT_WDF_REQUEST_CANCEL VIOSockSelectCancel;
EVT_WDF_WORKITEM VIOSockSelectWorkitem;
EVT_WDF_TIMER VIOSockSelectTimerFunc;

VOID VIOSockQueuesCleanup(IN WDFDEVICE hDevice);

NTSTATUS
VIOSockQueuesInit(IN WDFDEVICE hDevice);

NTSTATUS
VIOSockDeviceGetConfig(IN WDFREQUEST Request, OUT size_t *pLength);

NTSTATUS
VIOSockDeviceGetAf(IN WDFREQUEST Request, OUT size_t *pLength);

typedef struct _VIOSOCK_SELECT_HANDLE
{
    ULONGLONG hSocket;
    WDFFILEOBJECT Socket;
} VIOSOCK_SELECT_HANDLE, *PVIOSOCK_SELECT_HANDLE;

typedef struct _VIOSOCK_POLL_HANDLE
{
    ULONGLONG hSocket;    // user-mode handle (echoed back to user buffer)
    WDFFILEOBJECT Socket; // WDF_NO_HANDLE => bad fd; Revents pre-tagged with POLLNVAL or 0
    USHORT Events;        // requested mask snapshot
    USHORT Revents;       // current output (sticky for POLLNVAL)
} VIOSOCK_POLL_HANDLE, *PVIOSOCK_POLL_HANDLE;

typedef enum _VIOSOCK_PKT_TYPE
{
    VIOSOCK_PKT_TYPE_SELECT = 0,
    VIOSOCK_PKT_TYPE_POLL = 1,
} VIOSOCK_PKT_TYPE;

typedef struct _VIOSOCK_SELECT_PKT
{
    LIST_ENTRY ListEntry;
    VIOSOCK_TIMER_ENTRY TimerEntry;
    NTSTATUS Status;
    VIOSOCK_PKT_TYPE Type;
    union {
        // Select-arm (Type == VIOSOCK_PKT_TYPE_SELECT)
        struct
        {
            PVIRTIO_VSOCK_SELECT pSelect;
            ULONG FdCount[FDSET_MAX];
            VIOSOCK_SELECT_HANDLE Fds[FD_SETSIZE];
        };
        // Poll-arm (Type == VIOSOCK_PKT_TYPE_POLL)
        struct
        {
            PVIOSOCK_POLL_DATA pPollData; // points into the request output buffer
            ULONG PollFdCount;
            BOOLEAN Is32BitRequest;
            VIOSOCK_POLL_HANDLE PollFds[FD_SETSIZE];
        };
    };
} VIOSOCK_SELECT_PKT, *PVIOSOCK_SELECT_PKT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIOSOCK_SELECT_PKT, GetSelectContext);

#define VIOSockSelectGetFdsReadSet(p)  ((p)->Fds)
#define VIOSockSelectGetFdsWriteSet(p) ((p)->Fds + (p)->FdCount[FDSET_READ])
#define VIOSockSelectGetFdsExcptSet(p) ((p)->Fds + (p)->FdCount[FDSET_READ] + (p)->FdCount[FDSET_WRITE])

NTSTATUS
VIOSockSelectInit(IN PDEVICE_CONTEXT pContext);

VOID VIOSockSelectCleanupFds(IN PVIOSOCK_SELECT_PKT pPkt,
                             IN VIRTIO_VSOCK_FDSET_TYPE iFdSet,
                             IN PVIOSOCK_SELECT_HANDLE pHandleSet);

VOID VIOSockPollCleanupPkt(IN PVIOSOCK_SELECT_PKT pPkt);

BOOLEAN
VIOSockSelectCheckPkt(IN PVIOSOCK_SELECT_PKT pPkt);

BOOLEAN
VIOSockPollCheckPkt(IN PVIOSOCK_SELECT_PKT pPkt);

USHORT VIOSockMapEventsToPollRevents(IN PSOCKET_CONTEXT pSocket, IN USHORT eventsRequested);

BOOLEAN
VIOSockSelectCopyFds(IN PDEVICE_CONTEXT pContext,
                     IN BOOLEAN bIs32BitProcess,
                     IN KPROCESSOR_MODE AccessMode,
                     IN PVIRTIO_VSOCK_SELECT pSelect,
                     IN PVIOSOCK_SELECT_PKT pPkt,
                     IN VIRTIO_VSOCK_FDSET_TYPE iFdSet,
                     IN PVIOSOCK_SELECT_HANDLE pHandleSet);

NTSTATUS VIOSockPollCopyFds(IN PDEVICE_CONTEXT pContext,
                            IN BOOLEAN bIs32BitProcess,
                            IN KPROCESSOR_MODE AccessMode,
                            IN PVIOSOCK_POLL_DATA pPollData,
                            IN PVIOSOCK_SELECT_PKT pPkt);

NTSTATUS
VIOSockSelect(IN WDFREQUEST Request, IN OUT size_t *pLength);

NTSTATUS
VIOSockPoll(IN WDFREQUEST Request,
            IN BOOLEAN bIs32BitProcess,
            IN PVIOSOCK_POLL_DATA pPollDataIn,
            IN size_t stPollDataInLen,
            IN PVIOSOCK_POLL_DATA pPollDataOut,
            IN size_t stPollDataOutLen,
            IN OUT size_t *pLength);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VIOSockEvtDeviceAdd)
#pragma alloc_text(PAGE, VIOSockQueuesCleanup)

#pragma alloc_text(PAGE, VIOSockEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, VIOSockEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, VIOSockEvtDeviceD0Exit)
#pragma alloc_text(PAGE, VIOSockEvtDeviceSelfManagedIoSuspend)

#pragma alloc_text(PAGE, VIOSockDeviceGetConfig)
#pragma alloc_text(PAGE, VIOSockDeviceGetAf)
#pragma alloc_text(PAGE, VIOSockEvtIoDeviceControl)
#pragma alloc_text(PAGE, VIOSockEvtIoCreate)
#pragma alloc_text(PAGE, VIOSockSelectInit)
#pragma alloc_text(PAGE, VIOSockSelectCleanupFds)
#pragma alloc_text(PAGE, VIOSockPollCleanupPkt)
#pragma alloc_text(PAGE, VIOSockSelectCheckPkt)
#pragma alloc_text(PAGE, VIOSockPollCheckPkt)
#pragma alloc_text(PAGE, VIOSockMapEventsToPollRevents)
#pragma alloc_text(PAGE, VIOSockSelectWorkitem)
#pragma alloc_text(PAGE, VIOSockSelectCopyFds)
#pragma alloc_text(PAGE, VIOSockPollCopyFds)
#pragma alloc_text(PAGE, VIOSockSelect)
#pragma alloc_text(PAGE, VIOSockPoll)
#endif

static VOID VIOSockQueuesCleanup(IN WDFDEVICE hDevice)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    NTSTATUS status = STATUS_SUCCESS;

    ULONG uBufferSize;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    if (pContext->RxVq)
    {
        VIOSockRxVqCleanup(pContext);
    }

    if (pContext->TxVq)
    {
        VIOSockTxVqCleanup(pContext);
    }

    if (pContext->EvtVq)
    {
        VIOSockEvtVqCleanup(pContext);
    }

    VirtIOWdfDestroyQueues(&pContext->VDevice);
}

static NTSTATUS VIOSockQueuesInit(IN WDFDEVICE hDevice)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(hDevice);
    NTSTATUS status = STATUS_SUCCESS;
    VIRTIO_WDF_QUEUE_PARAM params[VIOSOCK_VQ_MAX];
    PVIOSOCK_VQ vqs[VIOSOCK_VQ_MAX];

    ULONG uBufferSize;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    // rx
    params[VIOSOCK_VQ_RX].Interrupt = pContext->WdfInterrupt;
    // tx
    params[VIOSOCK_VQ_TX].Interrupt = pContext->WdfInterrupt;
    // event
    params[VIOSOCK_VQ_EVT].Interrupt = pContext->WdfInterrupt;

    status = VirtIOWdfInitQueues(&pContext->VDevice, VIOSOCK_VQ_MAX, vqs, params);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfInitQueues failed: 0x%x\n", status);
        return status;
    }

    pContext->RxVq = vqs[VIOSOCK_VQ_RX];
    status = VIOSockRxVqInit(pContext);
    if (NT_SUCCESS(status))
    {
        pContext->TxVq = vqs[VIOSOCK_VQ_TX];
        status = VIOSockTxVqInit(pContext);
        if (NT_SUCCESS(status))
        {
            pContext->EvtVq = vqs[VIOSOCK_VQ_EVT];
            status = VIOSockEvtVqInit(pContext);
            if (!NT_SUCCESS(status))
            {
                pContext->EvtVq = NULL;
            }
        }
        else
        {
            pContext->TxVq = NULL;
        }
    }
    else
    {
        pContext->RxVq = NULL;
    }

    if (!NT_SUCCESS(status))
    {
        VIOSockQueuesCleanup(hDevice);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);

    return status;
}

//////////////////////////////////////////////////////////////////////////
// Scatter-gather chain support for IOCTL_SOCKET_RECV_EX

// EvtCleanupCallback of the request context: WDF runs it on request teardown after
// any completion path, so error paths in VIOSockSgPrepare just return and let the
// callback unlock whatever got locked. Attached only to contexts whose chain we
// built ourselves — never to ones wrapping a borrowed MDL. May run at DISPATCH_LEVEL.
static VOID VIOSockSgCtxCleanup(IN WDFOBJECT Object)
{
    PVIOSOCK_SG_CTX pCtx = GetRequestSgContext(Object);
    PMDL pMdl;

    // MmUnlockPages also releases the system mapping
    for (pMdl = pCtx->MdlChain; pMdl != NULL;)
    {
        PMDL pNext = pMdl->Next;

        MmUnlockPages(pMdl);
        IoFreeMdl(pMdl);
        pMdl = pNext;
    }
    pCtx->MdlChain = NULL;
}

// Build a locked MDL for uLen bytes of one user WSABUF (uLen can be less than the
// buffer's own length — see the cap in VIOSockSgPrepare); runs in the requestor
// process context. A buffer that is not valid user memory is reported as
// STATUS_ACCESS_VIOLATION, which maps to the WSAEFAULT that Winsock documents for
// a buffer "not completely contained in a valid part of the user address space".
static NTSTATUS VIOSockSgPrepareBuffer(IN PVIRTIO_VSOCK_WSABUF pUserBuf,
                                       IN ULONG uLen,
                                       IN BOOLEAN bIs32BitProcess,
                                       IN LOCK_OPERATION LockOp,
                                       OUT PMDL *ppMdl)
{
    NTSTATUS status;
    PMDL pMdl;
    PVOID pBuffer;

    if (!pUserBuf->lpBuffer)
    {
        return STATUS_ACCESS_VIOLATION;
    }

    if (bIs32BitProcess)
    {
        // Refuse rather than truncate: a caller whose pointer does not fit its
        // own address space must not have us lock some other address instead
        if (pUserBuf->lpBuffer > MAXULONG)
        {
            return STATUS_ACCESS_VIOLATION;
        }

        pBuffer = Ptr32ToPtr((VOID * POINTER_32)(ULONG)pUserBuf->lpBuffer);
    }
    else
    {
        pBuffer = (PVOID)(ULONG_PTR)pUserBuf->lpBuffer;
    }

    // The range must be checked before an MDL is built for it: IoAllocateMdl
    // derives the page count from base + length, so a range that wraps yields an
    // MDL whose page array is shorter than its ByteCount claims, and the lock
    // below would then write PFNs past the end of that allocation. Everything
    // else about the range (that it is user memory and accessible in the
    // requested direction) is enforced by MmProbeAndLockPages.
    if ((ULONG_PTR)pBuffer + uLen < (ULONG_PTR)pBuffer)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Buffer range wraps: %p + 0x%x\n", pBuffer, uLen);
        return STATUS_ACCESS_VIOLATION;
    }

    pMdl = IoAllocateMdl(pBuffer, uLen, FALSE, FALSE, NULL);
    if (!pMdl)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    __try
    {
        MmProbeAndLockPages(pMdl, UserMode, LockOp);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = GetExceptionCode();
        IoFreeMdl(pMdl);
        return status;
    }

    *ppMdl = pMdl;
    return STATUS_SUCCESS;
}

// Called from EvtIoInCallerContext: the WSABUF pointers embedded in the params
// are meaningful only in the requestor process, so the array must be captured
// and its data buffers locked before the request is allowed into a queue (which
// may deliver it in arbitrary context). LockOp gives the data direction, so the
// same routine serves a future send-side chain as well.
NTSTATUS
VIOSockSgPrepare(IN WDFREQUEST Request, IN LOCK_OPERATION LockOp)
{
    NTSTATUS status;
    PVIRTIO_VSOCK_SEND_RECV_PARAMS pParams;
    WDF_REQUEST_PARAMETERS Parameters;
    SIZE_T stParamsSize;
    PVIOSOCK_SG_CTX pCtx;
    WDF_OBJECT_ATTRIBUTES attributes;
    PVIRTIO_VSOCK_WSABUF pUserBufs;
    PMDL *ppNext;
    ULONGLONG uTotalLen = 0;
    ULONG i;
    BOOLEAN bIs32BitProcess = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pParams), (PVOID *)&pParams, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    if (!pParams->dwBufferCount || pParams->dwBufferCount > VIOSOCK_SG_MAX_BUFFERS)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid params: count %u\n", pParams->dwBufferCount);
        return STATUS_INVALID_PARAMETER;
    }

    stParamsSize = FIELD_OFFSET(VIRTIO_VSOCK_SEND_RECV_PARAMS, Buffers) +
                   sizeof(VIRTIO_VSOCK_WSABUF) * pParams->dwBufferCount;
    status = WdfRequestRetrieveInputBuffer(Request, stParamsSize, (PVOID *)&pParams, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // The transferred byte count is reported via IRP.Information. With an output
    // buffer present the io-manager would treat Information as the copy-back size
    // and overrun the (smaller) user buffer — an _EX request must not carry one.
    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);
    if (Parameters.Parameters.DeviceIoControl.OutputBufferLength != 0)
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_IOCTLS,
                    "Unexpected output buffer, length %Iu\n",
                    Parameters.Parameters.DeviceIoControl.OutputBufferLength);
        return STATUS_INVALID_PARAMETER;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VIOSOCK_SG_CTX);
    attributes.EvtCleanupCallback = VIOSockSgCtxCleanup;
    status = WdfObjectAllocateContext(Request, &attributes, (PVOID *)&pCtx);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return status;
    }
    RtlZeroMemory(pCtx, sizeof(*pCtx));
    ppNext = &pCtx->MdlChain;

    pUserBufs = (PVIRTIO_VSOCK_WSABUF)pParams->Buffers;

#ifdef _WIN64
    bIs32BitProcess = WdfRequestIsFrom32BitProcess(Request);
#endif //_WIN64

    for (i = 0; i < pParams->dwBufferCount; ++i)
    {
        PMDL pMdl;
        ULONG uLen = pUserBufs[i].dwLen;

        // A zero-length WSABUF is legal — the buffers are packed with no hole for
        // it — so it simply contributes nothing and needs no MDL
        if (!uLen)
        {
            continue;
        }

        // Bound how much memory one request can pin. A byte-stream receive may
        // legally return less than asked for, so the excess is dropped rather
        // than failing the call — except under MSG_WAITALL, whose contract is to
        // complete only once the caller's buffer is full, which we then cannot
        // honour. The sum cannot overflow: at most VIOSOCK_SG_MAX_BUFFERS entries
        // of at most MAXULONG each, accumulated in a ULONGLONG.
        if (uTotalLen + uLen > VIOSOCK_SG_MAX_TOTAL_LEN)
        {
            if (pParams->dwFlags & MSG_WAITALL)
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "MSG_WAITALL over the buffer cap\n");
                return STATUS_NOT_SUPPORTED;
            }

            uLen = (ULONG)(VIOSOCK_SG_MAX_TOTAL_LEN - uTotalLen);
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTLS, "Buffer list capped at wsabuf[%u]\n", i);
        }

        status = VIOSockSgPrepareBuffer(&pUserBufs[i], uLen, bIs32BitProcess, LockOp, &pMdl);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "Failed to prepare wsabuf[%u]: 0x%x\n", i, status);
            return status;
        }

        uTotalLen += uLen;

        // Link only after a successful lock: cleanup unconditionally unlocks
        // every chain member
        *ppNext = pMdl;
        ppNext = &pMdl->Next;

        if (uTotalLen == VIOSOCK_SG_MAX_TOTAL_LEN)
        {
            break;
        }
    }

    // Every buffer was zero-length. Completing such a request would report zero
    // bytes received, which on a byte stream means the peer closed the connection
    // — so refuse it instead of manufacturing a false end of stream.
    if (!uTotalLen)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "No buffer space in the list\n");
        return STATUS_INVALID_PARAMETER;
    }

    // Fits a ULONG by the cap above, so it also fits the transferred-bytes counter
    // reported through IRP.Information
    pCtx->TotalLen = (ULONG)uTotalLen;

    TraceEvents(TRACE_LEVEL_VERBOSE,
                DBG_IOCTLS,
                "<-- %s, locked %u buffers, %llu bytes\n",
                __FUNCTION__,
                pParams->dwBufferCount,
                uTotalLen);
    return STATUS_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
// Runs in the requestor thread before the request is queued — the only place
// where the user-mode pointers embedded in IOCTL input (the WSABUF array of
// IOCTL_SOCKET_RECV_EX / IOCTL_SOCKET_SEND_EX) may be probed, captured and
// locked. LockOp direction: recv writes into the user buffers, send reads them.
VOID VIOSockEvtIoInCallerContext(IN WDFDEVICE Device, IN WDFREQUEST Request)
{
    WDF_REQUEST_PARAMETERS Parameters;
    NTSTATUS status = STATUS_SUCCESS;

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);

    if (Parameters.Type == WdfRequestTypeDeviceControl)
    {
        switch (Parameters.Parameters.DeviceIoControl.IoControlCode)
        {
            case IOCTL_SOCKET_RECV_EX:
                status = VIOSockSgPrepare(Request, IoWriteAccess);
                break;

            case IOCTL_SOCKET_SEND_EX:
                status = VIOSockSgPrepare(Request, IoReadAccess);
                break;
        }

        if (!NT_SUCCESS(status))
        {
            WdfRequestComplete(Request, status);
            return;
        }
    }

    status = WdfDeviceEnqueueRequest(Device, Request);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfDeviceEnqueueRequest failed: 0x%x\n", status);
        WdfRequestComplete(Request, status);
    }
}

//////////////////////////////////////////////////////////////////////////
NTSTATUS
VIOSockEvtDeviceAdd(IN WDFDRIVER Driver, IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDFDEVICE hDevice;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
    PDEVICE_CONTEXT pContext = NULL;
    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_WORKITEM_CONFIG wrkConfig;

    DECLARE_CONST_UNICODE_STRING(usDeviceName, VIOSOCK_DEVICE_NAME);
    DECLARE_CONST_UNICODE_STRING(usDosDeviceName, VIOSOCK_SYMLINK_NAME);

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    // Configure Pnp/power callbacks
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDevicePrepareHardware = VIOSockEvtDevicePrepareHardware;
    PnpPowerCallbacks.EvtDeviceReleaseHardware = VIOSockEvtDeviceReleaseHardware;
    PnpPowerCallbacks.EvtDeviceD0Entry = VIOSockEvtDeviceD0Entry;
    PnpPowerCallbacks.EvtDeviceD0Exit = VIOSockEvtDeviceD0Exit;
    PnpPowerCallbacks.EvtDeviceD0EntryPostInterruptsEnabled = VIOSockEvtDeviceD0EntryPostInterruptsEnabled;
    PnpPowerCallbacks.EvtDeviceSelfManagedIoSuspend = VIOSockEvtDeviceSelfManagedIoSuspend;
    PnpPowerCallbacks.EvtDeviceSelfManagedIoRestart = VIOSockEvtDeviceSelfManagedIoRestart;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

    // Set DirectIO mode
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);
    // Valid file type is required for _open_osfhandle
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_NAMED_PIPE);

    // Capture/lock user SG buffers while still in the requestor context
    WdfDeviceInitSetIoInCallerContextCallback(DeviceInit, VIOSockEvtIoInCallerContext);

    // Set device name (for kernel mode clients)
    status = WdfDeviceInitAssignName(DeviceInit, &usDeviceName);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceInitAssignName failed - 0x%x\n", status);
        return status;
    }

    // Set device access (for user mode clients)
    status = WdfDeviceInitAssignSDDLString(DeviceInit, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceInitAssignSDDLString failed - 0x%x\n", status);
        return status;
    }

    // Configure file object callbacks.
    // Create is not handled here: it is routed to a dedicated I/O queue below (see
    // WdfDeviceConfigureRequestDispatching for WdfRequestTypeCreate). This is required
    // so that accept() - which is dispatched as IRP_MJ_CREATE - can be parked on the
    // listen socket's AcceptQueue via WdfRequestForwardToIoQueue (which is only
    // legal for requests obtained from an I/O queue).
    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig,
                               WDF_NO_EVENT_CALLBACK, // Create (routed to the create queue)
                               WDF_NO_EVENT_CALLBACK, // Close
                               VIOSockCleanup);
    fileConfig.FileObjectClass = WdfFileObjectWdfCanUseFsContext;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SOCKET_CONTEXT);
    Attributes.EvtCleanupCallback = VIOSockObjectCleanup;

    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &Attributes);

    // Create device
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &Attributes, &hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceCreate failed - 0x%x\n", status);
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(hDevice, &GUID_DEVINTERFACE_VIOSOCK, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceCreateDeviceInterface failed - 0x%x\n", status);
        return status;
    }

    status = WdfDeviceCreateSymbolicLink(hDevice, &usDosDeviceName);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDeviceCreateSymbolicLink failed - 0x%x\n", status);
        return status;
    }

    status = VIOSockBoundListInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSockBoundListInit failed - 0x%x\n", status);
        return status;
    }

    status = VIOSockConnectedListInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSockConnectedListInit failed - 0x%x\n", status);
        return status;
    }

    pContext = GetDeviceContext(hDevice);

    pContext->ThisDevice = hDevice;

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = hDevice;

    status = WdfLookasideListCreate(&Attributes,
                                    sizeof(VIOSOCK_ACCEPT_ENTRY),
                                    NonPagedPoolNx,
                                    &Attributes,
                                    VIOSOCK_DRIVER_MEMORY_TAG,
                                    &pContext->AcceptMemoryList);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfLookasideListCreate failed: 0x%x\n", status);
        return status;
    }

    // Create sequential queue for IoCtl requests.
    // Power-managed: IOCTL_SELECT / WSAPoll are held driver-owned (cancelable) for an
    // indeterminate time on this queue. Rather than hang the D0->Dx transition, they are
    // drained in EvtDeviceSelfManagedIoSuspend (VIOSockSelectIoSuspend), which runs before
    // WDF stops the power-managed queues - so no EvtIoStop is needed here.
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = VIOSockEvtIoDeviceControl;
    queueConfig.AllowZeroLengthRequests = WdfFalse;
    queueConfig.PowerManaged = WdfTrue;

    status = WdfIoQueueCreate(hDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &pContext->IoCtlQueue);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfIoQueueCreate failed (IoCtrl Queue): 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(hDevice, pContext->IoCtlQueue, WdfRequestTypeDeviceControl);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_INIT,
                    "WdfDeviceConfigureRequestDispatching failed (IoCtrl Queue): 0x%x\n",
                    status);
        return status;
    }

    // Create parallel queue for Create (IRP_MJ_CREATE) requests.
    // Not power-managed: a create is a short setup operation that should complete
    // regardless of power state. The long-lived accept() park happens on the listen
    // socket's power-managed AcceptQueue, to which VIOSockAcceptForward forwards the
    // create request (legal because the request is now obtained from this queue).
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDefault = VIOSockEvtIoCreate;
    queueConfig.PowerManaged = WdfFalse;

    status = WdfIoQueueCreate(hDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &pContext->CreateQueue);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfIoQueueCreate failed (Create Queue): 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(hDevice, pContext->CreateQueue, WdfRequestTypeCreate);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_INIT,
                    "WdfDeviceConfigureRequestDispatching failed (Create Queue): 0x%x\n",
                    status);
        return status;
    }

    // Manual queue holding pended SIO_ADDRESS_LIST_CHANGE requests. They are completed
    // when the guest CID changes on a transport reset (VIOSockEvtVqProcess ->
    // VIOSockAddressListChange). Framework-owned while parked, so - like the accept
    // queue - it does not hold the D0->Dx transition (no EvtIoStop needed).
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    // No EvtIoCanceledOnQueue: while a request sits in a queue the framework cancels
    // it on its own (CancelIoEx / socket close / process exit) and completes it with
    // STATUS_CANCELLED. That callback is only for per-request cleanup, and a parked
    // notification holds no resources.
    queueConfig.PowerManaged = WdfTrue;

    __analysis_assume(queueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(hDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &pContext->AddrChangeQueue);
    __analysis_assume(queueConfig.EvtIoStop == 0);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfIoQueueCreate failed (AddrChange Queue): 0x%x\n", status);
        return status;
    }

    // Create parallel queue for Write requests
    status = VIOSockWriteQueueInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSockWriteQueueInit failed (Write Queue): 0x%x\n", status);
        return status;
    }

    // Create parallel queue for Read requests
    status = VIOSockReadQueueInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSockReadQueueInit failed (Read Queue): 0x%x\n", status);
        return status;
    }

    status = VIOSockSelectInit(pContext);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSockSelectInit failed: 0x%x\n", status);
        return status;
    }

    status = VIOSockInterruptInit(hDevice);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VIOSockInterruptInit failed - 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

static NTSTATUS VIOSockEvtDevicePrepareHardware(IN WDFDEVICE Device,
                                                IN WDFCMRESLIST ResourcesRaw,
                                                IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;
    UINT nr_ports;
    u64 u64HostFeatures;
    u64 u64GuestFeatures = 0;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    status = VirtIOWdfInitialize(&pContext->VDevice, Device, ResourcesTranslated, NULL, VIOSOCK_DRIVER_MEMORY_TAG);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfInitialize failed with 0x%x\n", status);
        return status;
    }

    u64HostFeatures = VirtIOWdfGetDeviceFeatures(&pContext->VDevice);

    if (virtio_is_feature_enabled(u64HostFeatures, VIRTIO_RING_F_INDIRECT_DESC))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Enable indirect feature.\n");

        virtio_feature_enable(u64GuestFeatures, VIRTIO_RING_F_INDIRECT_DESC);
    }

    if (virtio_is_feature_enabled(u64HostFeatures, VIRTIO_RING_F_EVENT_IDX))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Enable event index feature.\n");

        virtio_feature_enable(u64GuestFeatures, VIRTIO_RING_F_EVENT_IDX);
    }

    status = VirtIOWdfSetDriverFeatures(&pContext->VDevice, u64GuestFeatures, 0);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "VirtIOWdfSetDriverFeatures failed: 0x%x\n", status);
        VirtIOWdfSetDriverFailed(&pContext->VDevice);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

static NTSTATUS VIOSockEvtDeviceReleaseHardware(IN WDFDEVICE Device, IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    VirtIOWdfShutdown(&pContext->VDevice);

    return STATUS_SUCCESS;
}

static NTSTATUS VIOSockEvtDeviceD0Entry(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE PreviousState)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    status = VIOSockQueuesInit(Device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "VIOSockQueuesInit failed: 0x%x\n", status);
        return status;
    }

    VirtIOWdfDeviceGet(&pContext->VDevice, 0, &pContext->Config, sizeof(pContext->Config));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "guest_cid %lld\n", pContext->Config.guest_cid);

    return status;
}

static NTSTATUS VIOSockEvtDeviceD0Exit(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE TargetState)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s TargetState: %d\n", __FUNCTION__, TargetState);

    VIOSockQueuesCleanup(Device);

    return STATUS_SUCCESS;
}

static NTSTATUS VIOSockEvtDeviceD0EntryPostInterruptsEnabled(IN WDFDEVICE WdfDevice,
                                                             IN WDF_POWER_DEVICE_STATE PreviousState)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfDevice);
    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Setting VIRTIO_CONFIG_S_DRIVER_OK flag\n");
    VirtIOWdfSetDriverOK(&pContext->VDevice);

    ASSERT(pContext->RxVq && pContext->EvtVq);

    return STATUS_SUCCESS;
}

//
// Drain pending select()/poll() before the device leaves D0.
//
// select()/poll() are held driver-owned (cancelable, parked in SelectList) for an
// indeterminate time on the power-managed IoCtl queue. When the device leaves D0 WDF
// stops that queue and waits for its driver-owned requests to drain; with nothing to
// release them the power watchdog bugchecks 0x9F DRIVER_POWER_STATE_FAILURE.
//
// EvtDeviceSelfManagedIoSuspend runs *before* WDF stops the power-managed queues, so we
// complete the pended requests here: set PowerSuspended (checked by the select workitem
// and by new VIOSockSelect/VIOSockPoll calls), kick the workitem to complete the parked
// requests, then flush it so nothing is left driver-owned when WDF stops the queue.
//
static VOID VIOSockSelectIoSuspend(IN PDEVICE_CONTEXT pContext)
{
    WdfWaitLockAcquire(pContext->SelectLock, NULL);
    pContext->PowerSuspended = TRUE;
    WdfWaitLockRelease(pContext->SelectLock);

    VIOSockSelectEnqueueWorkItem(pContext);
    WdfWorkItemFlush(pContext->SelectWorkitem);
}

static VOID VIOSockSelectIoRestart(IN PDEVICE_CONTEXT pContext)
{
    WdfWaitLockAcquire(pContext->SelectLock, NULL);
    pContext->PowerSuspended = FALSE;
    WdfWaitLockRelease(pContext->SelectLock);
}

NTSTATUS
VIOSockEvtDeviceSelfManagedIoSuspend(IN WDFDEVICE Device)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    PAGED_CODE();

    VIOSockWriteIoSuspend(pContext);
    VIOSockSelectIoSuspend(pContext);

    return STATUS_SUCCESS;
}

NTSTATUS
VIOSockEvtDeviceSelfManagedIoRestart(IN WDFDEVICE Device)
{

    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    VIOSockWriteIoRestart(pContext);
    VIOSockSelectIoRestart(pContext);

    return STATUS_SUCCESS;
}

static NTSTATUS VIOSockDeviceGetConfig(IN WDFREQUEST Request, OUT size_t *pLength)
{
    PVIRTIO_VSOCK_CONFIG pConfig = NULL;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VIRTIO_VSOCK_CONFIG), (PVOID *)&pConfig, pLength);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveOutputBuffer above
    _Analysis_assume_(*pLength >= sizeof(VIRTIO_VSOCK_CONFIG));

    *pConfig = GetDeviceContextFromRequest(Request)->Config;
    *pLength = sizeof(*pConfig);

    return STATUS_SUCCESS;
}

static NTSTATUS VIOSockDeviceGetAf(IN WDFREQUEST Request, OUT size_t *pLength)
{
    PULONG pulAF = NULL;
    NTSTATUS status;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pulAF), (PVOID *)&pulAF, pLength);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS, "WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveOutputBuffer above
    _Analysis_assume_(*pLength >= sizeof(*pulAF));

    *pulAF = AF_VSOCK;
    *pLength = sizeof(*pulAF);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s, AF: %d\n", __FUNCTION__, *pulAF);

    return STATUS_SUCCESS;
}

static VOID VIOSockEvtIoDeviceControl(IN WDFQUEUE Queue,
                                      IN WDFREQUEST Request,
                                      IN size_t OutputBufferLength,
                                      IN size_t InputBufferLength,
                                      IN ULONG IoControlCode)
{
    size_t Length = 0;
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %s\n", __FUNCTION__);

    switch (IoControlCode)
    {
        case IOCTL_GET_CONFIG:
            status = VIOSockDeviceGetConfig(Request, &Length);
            break;

        case IOCTL_SELECT:
            status = VIOSockSelect(Request, &Length);
            break;

        case IOCTL_GET_AF:
            status = VIOSockDeviceGetAf(Request, &Length);
            break;

        default:
            if (IsControlRequest(Request))
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_IOCTLS, "Invalid socket type\n");
                status = STATUS_NOT_SOCKET;
            }
            else
            {
                status = VIOSockDeviceControl(Request, IoControlCode, &Length);
            }
    }

    if (status != STATUS_PENDING)
    {
        WdfRequestCompleteWithInformation(Request, status, Length);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);
}

static VOID VIOSockEvtIoCreate(IN WDFQUEUE Queue, IN WDFREQUEST Request)
{
    WDFDEVICE WdfDevice = WdfIoQueueGetDevice(Queue);
    NTSTATUS status;

    PAGED_CODE();

    status = VIOSockCreate(WdfDevice, Request);

    if (status != STATUS_PENDING)
    {
        WdfRequestComplete(Request, status);
    }
}

//////////////////////////////////////////////////////////////////////////
static NTSTATUS VIOSockSelectInit(IN PDEVICE_CONTEXT pContext)
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_WORKITEM_CONFIG wrkConfig;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    InitializeListHead(&pContext->SelectList);
    pContext->SelectInProgress = 0;

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = pContext->ThisDevice;

    status = WdfWaitLockCreate(&Attributes, &pContext->SelectLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfWaitLockCreate failed (Select): 0x%x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = pContext->ThisDevice;

    WDF_WORKITEM_CONFIG_INIT(&wrkConfig, VIOSockSelectWorkitem);
    status = WdfWorkItemCreate(&wrkConfig, &Attributes, &pContext->SelectWorkitem);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfWorkItemCreate failed (Select): 0x%x\n", status);
        return status;
    }

    VIOSockTimerCreate(&pContext->SelectTimer, pContext->ThisDevice, VIOSockSelectTimerFunc);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "VIOSockTimerCreate failed (Select): 0x%x\n", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "<-- %s\n", __FUNCTION__);

    return status;
}

static VOID VIOSockSelectCleanupFds(IN PVIOSOCK_SELECT_PKT pPkt,
                                    IN VIRTIO_VSOCK_FDSET_TYPE iFdSet,
                                    IN PVIOSOCK_SELECT_HANDLE pHandleSet)
{
    ULONG i;

    PAGED_CODE();

    for (i = 0; i < pPkt->FdCount[iFdSet]; ++i)
    {
        ASSERT(pHandleSet[i].Socket);

        InterlockedDecrement(&GetSocketContext(pHandleSet[i].Socket)->SelectRefs[iFdSet]); // dereference socket
        WdfObjectDereference(pHandleSet[i].Socket);
        pHandleSet[i].Socket = NULL;
    }

    pPkt->FdCount[iFdSet] = 0;
}

__inline VOID VIOSockSelectCleanupPkt(IN PVIOSOCK_SELECT_PKT pPkt)
{
    // VIOSockSelectGetFds*Set() derives its offset from pPkt->FdCount, and
    // VIOSockSelectCleanupFds() zeroes pPkt->FdCount[iFdSet] before returning.
    // Recomputing the WRITE/EXCPT pointers between calls would collapse the
    // offsets and the next cleanup would walk over the slots just cleared.
    // Snapshot all three pointers up front.
    PVIOSOCK_SELECT_HANDLE pReadSet = VIOSockSelectGetFdsReadSet(pPkt);
    PVIOSOCK_SELECT_HANDLE pWriteSet = VIOSockSelectGetFdsWriteSet(pPkt);
    PVIOSOCK_SELECT_HANDLE pExcptSet = VIOSockSelectGetFdsExcptSet(pPkt);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s, packet status: 0x%08x\n", __FUNCTION__, pPkt->Status);

    VIOSockSelectCleanupFds(pPkt, FDSET_READ, pReadSet);
    VIOSockSelectCleanupFds(pPkt, FDSET_WRITE, pWriteSet);
    VIOSockSelectCleanupFds(pPkt, FDSET_EXCPT, pExcptSet);
}

static VOID VIOSockPollCleanupPkt(IN PVIOSOCK_SELECT_PKT pPkt)
{
    ULONG i;

    PAGED_CODE();

    for (i = 0; i < pPkt->PollFdCount; ++i)
    {
        PVIOSOCK_POLL_HANDLE pPollFd = &pPkt->PollFds[i];

        if (pPollFd->Socket != WDF_NO_HANDLE)
        {
            PSOCKET_CONTEXT pSocket = GetSocketContext(pPollFd->Socket);

            if (pPollFd->Events & (POLLRDNORM | POLLHUP | POLLERR))
            {
                InterlockedDecrement(&pSocket->SelectRefs[FDSET_READ]);
            }
            if (pPollFd->Events & POLLWRNORM)
            {
                InterlockedDecrement(&pSocket->SelectRefs[FDSET_WRITE]);
            }
            InterlockedDecrement(&pSocket->SelectRefs[FDSET_EXCPT]);

            WdfObjectDereference(pPollFd->Socket);
            pPollFd->Socket = WDF_NO_HANDLE;
        }
    }

    pPkt->PollFdCount = 0;
}

static VOID VIOSockSelectTimerFunc(IN WDFTIMER Timer)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfTimerGetParentObject(Timer));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    VIOSockSelectEnqueueWorkItem(pContext);
}

//
// select / WSAPoll readiness is a pure level query, computed on demand from the
// socket's durable state rather than from the consumable Events record. This keeps
// it independent of the WSAEventSelect path: WSAEnumNetworkEvents zeroes Events,
// but these predicates read PeerShutdown / EventsStatus / counters / state, which
// it does not touch. Lockless by design — a one-tick-stale snapshot is re-checked
// on the next workitem pass; natural-sized aligned reads on x64 don't tear.
//
__inline BOOLEAN VIOSockSelectPeerClosed(IN PSOCKET_CONTEXT pSocket)
{
    // VIOSockDoClose forces PeerShutdown to the full mask on any close (graceful
    // or RST), so the peer send-shutdown bit captures every read-side close.
    return !!(pSocket->PeerShutdown & VIRTIO_VSOCK_SHUTDOWN_SEND);
}

__inline BOOLEAN VIOSockSelectReadable(IN PSOCKET_CONTEXT pSocket)
{
    return VIOSockRxReadReady(pSocket) || !!pSocket->AcceptPended || VIOSockSelectPeerClosed(pSocket);
}

__inline BOOLEAN VIOSockSelectWritable(IN PSOCKET_CONTEXT pSocket)
{
    LONG lSpace = (LONG)pSocket->peer_buf_alloc - (pSocket->tx_cnt - pSocket->peer_fwd_cnt);
    return VIOSockStateGet(pSocket) == VIOSOCK_STATE_CONNECTED && lSpace > 0;
}

__inline BOOLEAN VIOSockSelectConnectFailed(IN PSOCKET_CONTEXT pSocket)
{
    // Connect failure latches as state==CLOSE with a non-success FD_CONNECT status;
    // a fresh socket keeps the initial STATUS_SUCCESS, so it is not misreported.
    return VIOSockStateGet(pSocket) == VIOSOCK_STATE_CLOSE && !NT_SUCCESS(pSocket->EventsStatus[FD_CONNECT_BIT]);
}

static USHORT VIOSockMapEventsToPollRevents(IN PSOCKET_CONTEXT pSocket, IN USHORT eventsRequested)
{
    USHORT revents = 0;

    PAGED_CODE();

    // Read side: readable (>= SO_RCVLOWAT bytes buffered) or a pending accept
    if (VIOSockRxReadReady(pSocket) || pSocket->AcceptPended)
    {
        revents |= POLLRDNORM;
    }
    // Read side: peer-closed (next recv unblocks: data, EOF, or reset)
    if (VIOSockSelectPeerClosed(pSocket))
    {
        revents |= POLLRDNORM;
        revents |= NT_SUCCESS(pSocket->EventsStatus[FD_CLOSE_BIT]) ? POLLHUP : POLLERR;
    }

    // Write side: connected with TX credit available
    if (VIOSockSelectWritable(pSocket))
    {
        revents |= POLLWRNORM;
    }
    // Failed outbound connect
    if (VIOSockSelectConnectFailed(pSocket))
    {
        revents |= POLLERR;
    }

    if (!revents && VIOSockIsFlag(pSocket, SOCK_CLEANUP))
    {
        revents = POLLNVAL;
    }

    // MSDN: revents filtered to events_requested for the in/out bits;
    // POLLERR/POLLHUP/POLLNVAL are output-only and reported unconditionally
    return (USHORT)(revents & (eventsRequested | POLLERR | POLLHUP | POLLNVAL));
}

static BOOLEAN VIOSockSelectCheckPkt(IN PVIOSOCK_SELECT_PKT pPkt)
{
    ULONG i, uTotalFds = 0, uClosedFds = 0;
    PVIOSOCK_SELECT_HANDLE pHandleSet;
    PVIRTIO_VSOCK_FD_SET pFds;
    BOOLEAN bRes = FALSE;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    pFds = &pPkt->pSelect->Fdss[FDSET_READ];
    pHandleSet = VIOSockSelectGetFdsReadSet(pPkt);

    pFds->fd_count = 0;
    for (i = 0; i < pPkt->FdCount[FDSET_READ]; ++i)
    {
        PSOCKET_CONTEXT pSocket = GetSocketContext(pHandleSet[i].Socket);
        if (VIOSockSelectReadable(pSocket))
        {
            pFds->fd_array[pFds->fd_count++] = pHandleSet[i].hSocket;
        }
        else if (VIOSockIsFlag(pSocket, SOCK_CLEANUP))
        {
            ++uClosedFds;
        }
    }
    uTotalFds += pFds->fd_count;

    pFds = &pPkt->pSelect->Fdss[FDSET_WRITE];
    pHandleSet = VIOSockSelectGetFdsWriteSet(pPkt);

    pFds->fd_count = 0;
    for (i = 0; i < pPkt->FdCount[FDSET_WRITE]; ++i)
    {
        PSOCKET_CONTEXT pSocket = GetSocketContext(pHandleSet[i].Socket);
        if (VIOSockSelectWritable(pSocket))
        {
            pFds->fd_array[pFds->fd_count++] = pHandleSet[i].hSocket;
        }
        else if (VIOSockIsFlag(pSocket, SOCK_CLEANUP))
        {
            ++uClosedFds;
        }
    }
    uTotalFds += pFds->fd_count;

    pFds = &pPkt->pSelect->Fdss[FDSET_EXCPT];
    pHandleSet = VIOSockSelectGetFdsExcptSet(pPkt);

    pFds->fd_count = 0;
    for (i = 0; i < pPkt->FdCount[FDSET_EXCPT]; ++i)
    {
        PSOCKET_CONTEXT pSocket = GetSocketContext(pHandleSet[i].Socket);
        if (VIOSockSelectConnectFailed(pSocket) || VIOSockIsFlag(pSocket, SOCK_CLEANUP))
        {
            pFds->fd_array[pFds->fd_count++] = pHandleSet[i].hSocket;
        }
    }
    uTotalFds += pFds->fd_count;

    if (uTotalFds)
    {
        pPkt->Status = STATUS_SUCCESS;
        bRes = TRUE;
    }
    else if (uClosedFds == pPkt->FdCount[FDSET_READ] + pPkt->FdCount[FDSET_WRITE] + pPkt->FdCount[FDSET_EXCPT])
    {
        pPkt->Status = STATUS_INVALID_HANDLE;
        bRes = TRUE;
    }

    return bRes;
}

__inline ULONG_PTR VIOSockPollGetHeaderSize(IN BOOLEAN bIs32BitRequest)
{
    return bIs32BitRequest ? FIELD_OFFSET(VIOSOCK_POLL_DATA32, FdArray) : FIELD_OFFSET(VIOSOCK_POLL_DATA, FdArray);
}

__inline ULONG_PTR VIOSockPollGetFdSize(IN BOOLEAN bIs32BitRequest)
{
    return bIs32BitRequest ? sizeof(VIOSOCK_POLL_FD32) : sizeof(VIOSOCK_POLL_FD);
}

__inline ULONG_PTR VIOSockPollGetSize(IN ULONG FdCount, IN BOOLEAN bIs32BitRequest)
{
    return VIOSockPollGetHeaderSize(bIs32BitRequest) + FdCount * VIOSockPollGetFdSize(bIs32BitRequest);
}

__inline ULONGLONG VIOSockPollGetSocketHandle(IN PVIOSOCK_POLL_DATA pData, IN BOOLEAN bIs32BitRequest, IN ULONG i)
{
    if (bIs32BitRequest)
    {
        PVIOSOCK_POLL_DATA32 pPollData = (PVIOSOCK_POLL_DATA32)pData;
        if (pPollData->FdArray[i].Fd == INVALID_HANDLE_32)
        {
            return INVALID_HANDLE;
        }
        return (ULONGLONG)pPollData->FdArray[i].Fd;
    }
    return pData->FdArray[i].Fd;
}

__inline SHORT VIOSockPollGetEvents(IN PVIOSOCK_POLL_DATA pData, IN BOOLEAN bIs32BitRequest, IN ULONG i)
{
    return bIs32BitRequest ? ((PVIOSOCK_POLL_DATA32)pData)->FdArray[i].Events : pData->FdArray[i].Events;
}

__inline VOID VIOSockPollSetRevents(IN PVIOSOCK_POLL_DATA pData,
                                    IN BOOLEAN bIs32BitRequest,
                                    IN ULONG i,
                                    IN SHORT revents)
{
    if (bIs32BitRequest)
    {
        ((PVIOSOCK_POLL_DATA32)pData)->FdArray[i].Revents = revents;
    }
    else
    {
        pData->FdArray[i].Revents = revents;
    }
}

static BOOLEAN VIOSockPollCheckPkt(IN PVIOSOCK_SELECT_PKT pPkt)
{
    ULONG i, count = 0;
    PVIOSOCK_POLL_DATA pPollData = pPkt->pPollData;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s, fds=%u\n", __FUNCTION__, pPkt->PollFdCount);

    for (i = 0; i < pPkt->PollFdCount; ++i)
    {
        PVIOSOCK_POLL_HANDLE pPollFd = &pPkt->PollFds[i];

        if (pPollFd->Socket != WDF_NO_HANDLE)
        {
            pPollFd->Revents = VIOSockMapEventsToPollRevents(GetSocketContext(pPollFd->Socket), pPollFd->Events);
        }
        if (pPollFd->Revents)
        {
            ++count;
        }

        VIOSockPollSetRevents(pPollData, pPkt->Is32BitRequest, i, (SHORT)pPollFd->Revents);
    }

    pPollData->Result = (int)count;
    if (count)
    {
        // Match VIOSockSelectCheckPkt's contract: set pkt status here so the
        // workitem can assert pPkt->Status != STATUS_PENDING on TRUE return.
        pPkt->Status = STATUS_SUCCESS;
        return TRUE;
    }

    return FALSE;
}

static VOID VIOSockSelectCancel(IN WDFREQUEST Request)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    VIOSockSelectEnqueueWorkItem(pContext);
}

static VOID VIOSockSelectWorkitem(IN WDFWORKITEM Workitem)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfWorkItemGetParentObject(Workitem));

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    do
    {
        LIST_ENTRY CompletionList;
        WDFREQUEST Request;
        LONGLONG MinRemainingTicks = MAXLONGLONG;
        PLIST_ENTRY CurrentItem;
        BOOLEAN bRemove;

        InterlockedExchange(&pContext->SelectInProgress, 1);

        InitializeListHead(&CompletionList);

        WdfWaitLockAcquire(pContext->SelectLock, NULL);

        for (CurrentItem = pContext->SelectList.Flink; CurrentItem != &pContext->SelectList;
             CurrentItem = CurrentItem->Flink)
        {
            PVIOSOCK_SELECT_PKT pPkt = CONTAINING_RECORD(CurrentItem, VIOSOCK_SELECT_PKT, ListEntry);
            WDFREQUEST Request = WdfObjectContextGetObject(pPkt);
            NTSTATUS status = WdfRequestUnmarkCancelable(Request);

            ASSERT(NT_SUCCESS(status) || status == STATUS_CANCELLED);

            bRemove = FALSE;

            if (status == STATUS_CANCELLED)
            {
                bRemove = TRUE;
                pPkt->Status = STATUS_CANCELLED;
            }
            else if (pContext->PowerSuspended)
            {
                // Device is leaving D0: complete the pended select/poll so WDF's stop of
                // the power-managed IoCtl queue has nothing driver-owned to wait on (0x9F).
                // STATUS_UNSUCCESSFUL maps to WSAENETDOWN in the user-mode provider.
                bRemove = TRUE;
                pPkt->Status = STATUS_UNSUCCESSFUL;
            }
            else if ((pPkt->Type == VIOSOCK_PKT_TYPE_SELECT) ? VIOSockSelectCheckPkt(pPkt) : VIOSockPollCheckPkt(pPkt))
            {
                // pPkt->Status is already set by VIOSockSelectCheckPkt
                ASSERT(pPkt->Status != STATUS_PENDING);
                bRemove = TRUE;
            }
            else if (VIOSockTimerEntryIsSet(&pPkt->TimerEntry))
            {
                LONGLONG llRemainingTicks = VIOSockTimerEntryRemainingTicks(&pPkt->TimerEntry);

                if (llRemainingTicks <= VIOSOCK_TIMER_TOLERANCE_TICKS)
                {
                    bRemove = TRUE;
                    // It is a success value, just return empty fd sets
                    pPkt->Status = STATUS_TIMEOUT;
                }
                else if (llRemainingTicks < MinRemainingTicks)
                {
                    MinRemainingTicks = llRemainingTicks;
                }
            }

            if (!bRemove)
            {
                status = WdfRequestMarkCancelableEx(Request, VIOSockSelectCancel);

                ASSERT(NT_SUCCESS(status) || status == STATUS_CANCELLED);

                if (status == STATUS_CANCELLED)
                {
                    bRemove = TRUE;
                    pPkt->Status = STATUS_CANCELLED;
                }
            }

            if (bRemove)
            {
                CurrentItem = pPkt->ListEntry.Blink;
                RemoveEntryList(&pPkt->ListEntry);
                InsertTailList(&CompletionList, &pPkt->ListEntry);
                if (VIOSockTimerEntryIsSet(&pPkt->TimerEntry))
                {
                    VIOSockTimerDeref(&pContext->SelectTimer, TRUE);
                }
            }
        }

        if (MinRemainingTicks != MAXLONGLONG)
        {
            VIOSockTimerSetDeadline(&pContext->SelectTimer, VIOSockTimerTicksToDeadline(MinRemainingTicks));
        }

        WdfWaitLockRelease(pContext->SelectLock);

        while (!IsListEmpty(&CompletionList))
        {
            ULONG_PTR infoSize = 0;
            PVIOSOCK_SELECT_PKT pPkt = CONTAINING_RECORD(RemoveHeadList(&CompletionList),
                                                         VIOSOCK_SELECT_PKT,
                                                         ListEntry);

            if (pPkt->Type == VIOSOCK_PKT_TYPE_SELECT)
            {
                infoSize = sizeof(VIRTIO_VSOCK_SELECT);
                VIOSockSelectCleanupPkt(pPkt);
            }
            else
            {
                infoSize = VIOSockPollGetSize(pPkt->PollFdCount, pPkt->Is32BitRequest);
                VIOSockPollCleanupPkt(pPkt);
            }

            // NT_SUCCESS — not just STATUS_SUCCESS — must propagate infoSize:
            // STATUS_TIMEOUT is success-class but != STATUS_SUCCESS, and the
            // user-mode lib's WSPSelect treats dwBytesReturned != sizeof(...)
            // as an error (WSAEINVAL).
            WdfRequestCompleteWithInformation(WdfObjectContextGetObject(pPkt),
                                              pPkt->Status,
                                              NT_SUCCESS(pPkt->Status) ? infoSize : 0);
        }

    } while (InterlockedCompareExchange(&pContext->SelectInProgress, 0, 1) != 1);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "<-- %s\n", __FUNCTION__);
}

_IRQL_requires_max_(DISPATCH_LEVEL) VOID VIOSockSelectEnqueueWorkItem(IN PDEVICE_CONTEXT pContext)
{
    if (InterlockedIncrement(&pContext->SelectInProgress) == 1)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "Enqueue workitem\n");
        WdfWorkItemEnqueue(pContext->SelectWorkitem);
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL) VOID VIOSockSelectRun(IN PSOCKET_CONTEXT pSocket)
{
    BOOLEAN bRun = FALSE;
    PDEVICE_CONTEXT pContext = GetDeviceContextFromSocket(pSocket);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    // Enqueue the workitem only for fdsets that actually have a pending select/poll
    // and whose readiness condition currently holds. Same durable predicates the
    // workitem evaluates (VIOSockSelectCheckPkt / VIOSockMapEventsToPollRevents), so
    // this no longer reads the consumable Events record.
    if (pSocket->SelectRefs[FDSET_READ])
    {
        bRun |= VIOSockSelectReadable(pSocket);
    }
    if (pSocket->SelectRefs[FDSET_WRITE])
    {
        bRun |= VIOSockSelectWritable(pSocket);
    }
    if (pSocket->SelectRefs[FDSET_EXCPT])
    {
        bRun |= VIOSockSelectConnectFailed(pSocket);
    }

    if (bRun)
    {
        VIOSockSelectEnqueueWorkItem(pContext);
    }
}

static BOOLEAN VIOSockSelectCopyFds(IN PDEVICE_CONTEXT pContext,
                                    IN BOOLEAN bIs32BitProcess,
                                    IN KPROCESSOR_MODE AccessMode,
                                    IN PVIRTIO_VSOCK_SELECT pSelect,
                                    IN PVIOSOCK_SELECT_PKT pPkt,
                                    IN VIRTIO_VSOCK_FDSET_TYPE iFdSet,
                                    IN PVIOSOCK_SELECT_HANDLE pHandleSet)
{
    ULONG i;

    PAGED_CODE();

    pPkt->FdCount[iFdSet] = 0;
    for (i = 0; i < pSelect->Fdss[iFdSet].fd_count; ++i)
    {
        ULONGLONG hSocket = pSelect->Fdss[iFdSet].fd_array[i];
        if (hSocket)
        {
            WDFFILEOBJECT Socket = VIOSockGetSocketFromHandle(pContext, hSocket, bIs32BitProcess, AccessMode);
            if (Socket != WDF_NO_HANDLE)
            {
                PSOCKET_CONTEXT pSocket = GetSocketContext(Socket);

                if (VIOSockIsFlag(pSocket, SOCK_CLEANUP))
                {
                    WdfObjectDereference(pSocket->ThisSocket);
                    break;
                }
                pHandleSet[i].hSocket = hSocket;
                pHandleSet[i].Socket = Socket;
                InterlockedIncrement(&pSocket->SelectRefs[iFdSet]); // reference socket
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
    }

    pPkt->FdCount[iFdSet] = i;

    return i == pSelect->Fdss[iFdSet].fd_count;
}

static VOID VIOSockSelectClearFdss(IN PVIRTIO_VSOCK_SELECT pSelect)
{
    VIRTIO_VSOCK_FDSET_TYPE i;
    for (i = 0; i < FDSET_MAX; i++)
    {
        PVIRTIO_VSOCK_FD_SET pFds = &pSelect->Fdss[i];
        RtlZeroMemory(pFds->fd_array, pFds->fd_count * sizeof(pFds->fd_array[0]));
        pFds->fd_count = 0;
    }
}

static NTSTATUS VIOSockPollCopyFds(IN PDEVICE_CONTEXT pContext,
                                   IN BOOLEAN bIs32BitProcess,
                                   IN KPROCESSOR_MODE AccessMode,
                                   IN PVIOSOCK_POLL_DATA pPollData,
                                   IN PVIOSOCK_SELECT_PKT pPkt)
{
    ULONG i;
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();

    pPkt->pPollData = pPollData;
    pPkt->Is32BitRequest = bIs32BitProcess;

    for (i = 0; i < pPollData->Fds; ++i)
    {
        ULONGLONG hSocket = VIOSockPollGetSocketHandle(pPollData, bIs32BitProcess, i);
        USHORT events = (USHORT)VIOSockPollGetEvents(pPollData, bIs32BitProcess, i);
        WDFFILEOBJECT Socket = WDF_NO_HANDLE;
        USHORT initialRevents = POLLNVAL;

        if (!events || (events & ~(POLLIN | POLLOUT | POLLHUP | POLLERR)))
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (hSocket != 0 && hSocket != INVALID_HANDLE)
        {
            Socket = VIOSockGetSocketFromHandle(pContext, hSocket, bIs32BitProcess, AccessMode);
            if (Socket != WDF_NO_HANDLE)
            {
                PSOCKET_CONTEXT pSocket = GetSocketContext(Socket);

                // FDSET_READ predicate in VIOSockSelectRun fires on
                // FD_ACCEPT | FD_READ | FD_CLOSE — i.e. it carries the wake for
                // POLLHUP / POLLERR (peer close, RST) as well as POLLRDNORM.
                if (events & (POLLRDNORM | POLLHUP | POLLERR))
                {
                    InterlockedIncrement(&pSocket->SelectRefs[FDSET_READ]);
                }
                if (events & POLLWRNORM)
                {
                    InterlockedIncrement(&pSocket->SelectRefs[FDSET_WRITE]);
                }
                // FDSET_EXCPT predicate fires on FD_CONNECT-failure; that's how
                // a failed outbound connect reaches POLLERR. Always-bumped.
                InterlockedIncrement(&pSocket->SelectRefs[FDSET_EXCPT]);
                initialRevents = 0;
            }
        }

        pPkt->PollFds[i].hSocket = hSocket;
        pPkt->PollFds[i].Socket = Socket;
        pPkt->PollFds[i].Events = events;
        pPkt->PollFds[i].Revents = initialRevents;
    }

    pPkt->PollFdCount = i;

    if (!NT_SUCCESS(status))
    {
        VIOSockPollCleanupPkt(pPkt);
    }
    return status;
}

static NTSTATUS VIOSockSelect(IN WDFREQUEST Request, IN OUT size_t *pLength)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    PVIRTIO_VSOCK_SELECT pSelect;
    SIZE_T stSelectLen;
    NTSTATUS status;
    BOOLEAN bIs32BitProcess = FALSE;
    KPROCESSOR_MODE AccessMode;
    WDF_OBJECT_ATTRIBUTES Attributes;
    PVIOSOCK_SELECT_PKT pPkt = NULL;
    ULONG fdRead, fdWrite, fdExcpt, uTotalFdCount;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    *pLength = 0;

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(*pSelect), &pSelect, &stSelectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfRequestRetrieveInputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stSelectLen >= sizeof(*pSelect));

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*pSelect), &pSelect, &stSelectLen);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status);
        return status;
    }

    // minimum length guaranteed by WdfRequestRetrieveInputBuffer above
    _Analysis_assume_(stSelectLen >= sizeof(*pSelect));

    fdRead = pSelect->Fdss[FDSET_READ].fd_count;
    fdWrite = pSelect->Fdss[FDSET_WRITE].fd_count;
    fdExcpt = pSelect->Fdss[FDSET_EXCPT].fd_count;

    // Validate each fd_count before summing to prevent integer overflow
    // when these untrusted values are attacker-controlled.
    if (fdRead > FD_SETSIZE || fdWrite > FD_SETSIZE || fdExcpt > FD_SETSIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    uTotalFdCount = fdRead + fdWrite + fdExcpt;
    if (uTotalFdCount == 0 || uTotalFdCount > FD_SETSIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pSelect->Timeout < 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

#ifdef _WIN64
    bIs32BitProcess = WdfRequestIsFrom32BitProcess(Request);
#endif //_WIN64

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, VIOSOCK_SELECT_PKT);

    status = WdfObjectAllocateContext(Request, &Attributes, &pPkt);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return status;
    }

    pPkt->Type = VIOSOCK_PKT_TYPE_SELECT;
    VIOSockTimerEntryInit(&pPkt->TimerEntry);
    pPkt->pSelect = pSelect;

    // Requestor mode drives the ObReferenceObjectByHandle domain: user IOCTL →
    // per-process user handle table, driver-issued Request → kernel table.
    AccessMode = WdfRequestGetRequestorMode(Request);
    if (!VIOSockSelectCopyFds(pContext,
                              bIs32BitProcess,
                              AccessMode,
                              pSelect,
                              pPkt,
                              FDSET_READ,
                              VIOSockSelectGetFdsReadSet(pPkt)) ||
        !VIOSockSelectCopyFds(pContext,
                              bIs32BitProcess,
                              AccessMode,
                              pSelect,
                              pPkt,
                              FDSET_WRITE,
                              VIOSockSelectGetFdsWriteSet(pPkt)) ||
        !VIOSockSelectCopyFds(pContext,
                              bIs32BitProcess,
                              AccessMode,
                              pSelect,
                              pPkt,
                              FDSET_EXCPT,
                              VIOSockSelectGetFdsExcptSet(pPkt)))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_SELECT, "VIOSockSelectCopyFds failed\n");
        status = STATUS_INVALID_HANDLE;
    }

    if (NT_SUCCESS(status))
    {
        VIOSockSelectClearFdss(pSelect);

        WdfWaitLockAcquire(pContext->SelectLock, NULL);

        if (pContext->PowerSuspended)
        {
            // Device is leaving D0 - fail the select rather than pend it (-> WSAENETDOWN).
            status = STATUS_UNSUCCESSFUL;
        }
        else if (VIOSockSelectCheckPkt(pPkt))
        {
            status = pPkt->Status;
        }
        else if (!pSelect->Timeout)
        {
            // zero timeout specified, return immediately with zero fd sets
            status = STATUS_TIMEOUT;
        }
        else
        {
            status = STATUS_PENDING;
        }

        if (status == STATUS_PENDING)
        {
            status = WdfRequestMarkCancelableEx(Request, VIOSockSelectCancel);

            ASSERT(NT_SUCCESS(status) || status == STATUS_CANCELLED);

            if (NT_SUCCESS(status))
            {
                pPkt->Status = status = STATUS_PENDING;

                if (pSelect->Timeout != VSOCK_TIMEOUT_INFINITE)
                {
                    VIOSockTimerStartTimeout(&pContext->SelectTimer, &pPkt->TimerEntry, pSelect->Timeout);
                }

                InsertTailList(&pContext->SelectList, &pPkt->ListEntry);
            }
        }
        else if (NT_SUCCESS(status))
        {
            *pLength = sizeof(*pSelect);
        }

        WdfWaitLockRelease(pContext->SelectLock);
    }

    if (status != STATUS_PENDING)
    {
        VIOSockSelectCleanupPkt(pPkt);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "<-- %s\n", __FUNCTION__);

    return status;
}

NTSTATUS
VIOSockPoll(IN WDFREQUEST Request,
            IN BOOLEAN bIs32BitProcess,
            IN PVIOSOCK_POLL_DATA pPollDataIn,
            IN size_t stPollDataInLen,
            IN PVIOSOCK_POLL_DATA pPollDataOut,
            IN size_t stPollDataOutLen,
            IN OUT size_t *pLength)
{
    PDEVICE_CONTEXT pContext = GetDeviceContextFromRequest(Request);
    WDF_OBJECT_ATTRIBUTES Attributes;
    PVIOSOCK_SELECT_PKT pPkt = NULL;
    NTSTATUS status;
    ULONG_PTR cbExpected;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "--> %s\n", __FUNCTION__);

    *pLength = 0;

    if (!pPollDataIn || !pPollDataOut || stPollDataInLen < VIOSockPollGetHeaderSize(bIs32BitProcess) ||
        stPollDataOutLen < VIOSockPollGetHeaderSize(bIs32BitProcess))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_SELECT,
                    "SIO_EXT_POLL: invalid parameters (in=%Iu, out=%Iu)\n",
                    stPollDataInLen,
                    stPollDataOutLen);
        return STATUS_INVALID_PARAMETER;
    }

    cbExpected = VIOSockPollGetSize(pPollDataIn->Fds, bIs32BitProcess);
    if (!pPollDataIn->Fds || pPollDataIn->Fds > FD_SETSIZE || cbExpected > stPollDataInLen ||
        cbExpected > stPollDataOutLen)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_SELECT,
                    "SIO_EXT_POLL: invalid parameters (fds=%u, in=%Iu, out=%Iu)\n",
                    pPollDataIn->Fds,
                    stPollDataInLen,
                    stPollDataOutLen);
        return STATUS_INVALID_PARAMETER;
    }

    // Copy WSAPOLLDATA from the locked user input buffer into the
    // METHOD_BUFFERED system buffer (pPollDataOut). VIOSockPoll
    // operates on lpvOutBuffer, which will be copied back to the
    // user output buffer on request completion.
    RtlMoveMemory(pPollDataOut, pPollDataIn, cbExpected);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, VIOSOCK_SELECT_PKT);

    status = WdfObjectAllocateContext(Request, &Attributes, &pPkt);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_SELECT, "WdfObjectAllocateContext failed: 0x%x\n", status);
        return status;
    }

    pPkt->Type = VIOSOCK_PKT_TYPE_POLL;
    VIOSockTimerEntryInit(&pPkt->TimerEntry);

    status = VIOSockPollCopyFds(pContext, bIs32BitProcess, WdfRequestGetRequestorMode(Request), pPollDataOut, pPkt);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    WdfWaitLockAcquire(pContext->SelectLock, NULL);

    if (pContext->PowerSuspended)
    {
        // Device is leaving D0 - fail the poll rather than pend it (-> WSAENETDOWN).
        status = STATUS_UNSUCCESSFUL;
    }
    else if (VIOSockPollCheckPkt(pPkt))
    {
        // At least one fd is immediately ready (real ready or POLLNVAL-tagged).
        status = STATUS_SUCCESS;
    }
    else if (pPollDataOut->Timeout == 0)
    {
        // Non-blocking poll: return immediately. Result is already 0 from CheckPollPkt.
        status = STATUS_SUCCESS;
    }
    else
    {
        status = STATUS_PENDING;
    }

    if (status == STATUS_PENDING)
    {
        status = WdfRequestMarkCancelableEx(Request, VIOSockSelectCancel);

        ASSERT(NT_SUCCESS(status) || status == STATUS_CANCELLED);

        if (NT_SUCCESS(status))
        {
            pPkt->Status = status = STATUS_PENDING;

            // Timeout > 0: start deadline timer; -1 (infinite) leaves timer unarmed.
            if (pPollDataOut->Timeout > 0)
            {
                VIOSockTimerStartTimeout(&pContext->SelectTimer,
                                         &pPkt->TimerEntry,
                                         MSEC_TO_NANO(pPollDataOut->Timeout));
            }

            InsertTailList(&pContext->SelectList, &pPkt->ListEntry);
        }
    }
    else
    {
        *pLength = cbExpected;
    }

    WdfWaitLockRelease(pContext->SelectLock);

    if (status != STATUS_PENDING)
    {
        VIOSockPollCleanupPkt(pPkt);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SELECT, "<-- %s, status: 0x%08x\n", __FUNCTION__, status);

    return status;
}
