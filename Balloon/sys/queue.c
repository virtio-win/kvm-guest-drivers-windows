#include "precomp.h"

EVT_WDF_IO_QUEUE_IO_WRITE BalloonIoWrite;

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, BalloonQueueInitialize)
#endif

NTSTATUS
BalloonQueueInitialize(
    WDFDEVICE Device
    )
{
    NTSTATUS               status = STATUS_SUCCESS;
    WDF_IO_QUEUE_CONFIG    queueConfig = {0};
    PDEVICE_CONTEXT        devCtx = NULL;

    PAGED_CODE();

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
                 &queueConfig,
                 WdfIoQueueDispatchSequential);

    queueConfig.EvtIoWrite  = BalloonIoWrite;

    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 WDF_NO_HANDLE
                 );

    if( !NT_SUCCESS(status) ) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfIoQueueCreate failed 0x%08x\n", status);
        return status;
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
    PDRIVER_CONTEXT        drvCtx = NULL;
    NTSTATUS               status = STATUS_SUCCESS;
    WDFDRIVER              drv;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "BalloonIoWrite Called! Queue 0x%p, Request 0x%p Length %d\n",
             Queue,Request,Length);

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

    drv = WdfGetDriver();
    drvCtx = GetDriverContext( drv );

    RtlCopyMemory(drvCtx->MemStats, buffer, sizeof(BALLOON_STAT));
    WdfRequestCompleteWithInformation(Request, status, sizeof(BALLOON_STAT));
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "WdfRequestCompleteWithInformation Called! Queue 0x%p, Request 0x%p Length %d Status 0x%08x\n",
             Queue,Request,Length, status);
}
