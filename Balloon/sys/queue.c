#include "precomp.h"

#ifdef USE_BALLOON_SERVICE

EVT_WDF_IO_QUEUE_IO_WRITE BalloonIoWrite;
EVT_WDF_IO_QUEUE_IO_STOP BalloonIoStop;
EVT_WDF_REQUEST_CANCEL BalloonEvtRequestCancel;

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, BalloonQueueInitialize)
#endif

NTSTATUS
BalloonQueueInitialize(
    IN WDFDEVICE Device
    )
{
    NTSTATUS               status = STATUS_SUCCESS;
    WDF_IO_QUEUE_CONFIG    queueConfig = {0};

    PAGED_CODE();

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig,
        WdfIoQueueDispatchSequential);
    queueConfig.EvtIoWrite = BalloonIoWrite;
    queueConfig.EvtIoStop = BalloonIoStop;

    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 WDF_NO_HANDLE
                 );

    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed 0x%08x\n", status);
    }
    return status;
}

VOID
BalloonIoWrite(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     Length
    )
{
    PVOID                  buffer = NULL;
    size_t                 buffSize;
    PDEVICE_CONTEXT        devCtx = NULL;
    NTSTATUS               status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "BalloonIoWrite Called! Queue 0x%p, Request 0x%p Length %d\n",
             Queue,Request,Length);

    devCtx = GetDeviceContext(WdfIoQueueGetDevice(Queue));

    if( Length < sizeof(BALLOON_STAT)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "BalloonIoWrite Buffer Length too small %d, expected is %d\n",
                 Length, sizeof(BALLOON_STAT));
        WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        return;
    }

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(BALLOON_STAT), &buffer, &buffSize);
    if( !NT_SUCCESS(status) || (buffer == NULL)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "BalloonIoWrite Could not get request memory buffer 0x%08x\n",
                 status);
        WdfRequestCompleteWithInformation(Request, status, 0L);
        return;
    }

    if (devCtx->HandleWriteRequest)
    {
        Length = min(buffSize, sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR);
#if 0
        {
            size_t i;
            PBALLOON_STAT stat = (PBALLOON_STAT)buffer;
            for (i = 0; i < Length / sizeof(BALLOON_STAT); ++i)
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS,
                    "tag = %d, val = %08I64X\n\n", stat[i].tag, stat[i].val);
            }
        }
#endif
        RtlCopyMemory(devCtx->MemStats, buffer, Length);
        WdfRequestCompleteWithInformation(Request, status, Length);
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "WdfRequestCompleteWithInformation Called! Queue 0x%p, Request 0x%p Length %d Status 0x%08x\n",
            Queue, Request, Length, status);

        devCtx->HandleWriteRequest = FALSE;
        BalloonMemStats(WdfIoQueueGetDevice(Queue));
    }
    else
    {
        status = WdfRequestMarkCancelableEx(Request, BalloonEvtRequestCancel);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_READ,
                "WdfRequestMarkCancelableEx failed: 0x%08x\n", status);
            WdfRequestCompleteWithInformation(Request, status, 0L);
            return;
        }

        devCtx->PendingWriteRequest = Request;
    }
}

VOID BalloonIoStop(IN WDFQUEUE Queue,
                   IN WDFREQUEST Request,
                   IN ULONG ActionFlags)
{
    UNREFERENCED_PARAMETER(Queue);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
        "--> %!FUNC! Request: %p", Request);

    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        WdfRequestStopAcknowledge(Request, FALSE);
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {
        if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            WdfRequestComplete(Request, STATUS_CANCELLED);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- %!FUNC!");
}

VOID BalloonEvtRequestCancel(IN WDFREQUEST Request)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfIoQueueGetDevice(
        WdfRequestGetIoQueue(Request)));

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ,
        "--> BalloonEvtRequestCancel Cancelled Request: %p\n", Request);

    NT_ASSERT(devCtx->PendingWriteRequest == Request);
    devCtx->PendingWriteRequest = NULL;
    WdfRequestComplete(Request, STATUS_CANCELLED);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- BalloonEvtRequestCancel\n");
}

#endif // USE_BALLOON_SERVICE
