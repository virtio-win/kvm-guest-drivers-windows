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
Dump(
   PBALLOON_STAT Buffer,
   ULONG   Length
   )
{
#ifdef DBG
    ULONG i;
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "\n");
    for (i = 0; i < Length/sizeof(BALLOON_STAT); ++i) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "[%d] = %08I64X\n", Buffer[i].tag, Buffer[i].val);
    }
#endif
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
    PDEVICE_CONTEXT        devCtx = GetDeviceContext(WdfIoQueueGetDevice( Queue ));
    PDRIVER_CONTEXT        drvCtx = GetDriverContext(WdfGetDriver());
    NTSTATUS               status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "BalloonIoWrite Called! Queue 0x%p, Request 0x%p Length %d\n",
             Queue,Request,Length);

    if( Length < sizeof(BALLOON_STAT)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "BalloonIoWrite Buffer Length to small %d, expected is %d\n",
                 Length, sizeof(BALLOON_STAT));
        WdfRequestCompleteWithInformation(Request, STATUS_BUFFER_OVERFLOW, 0L);
        return;
    }

    status = WdfRequestRetrieveInputBuffer(Request, 0, &buffer, &buffSize);
    if( !NT_SUCCESS(status) || (buffer == NULL)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "BalloonIoWrite Could not get request memory buffer 0x%08x\n",
                 status);
        WdfVerifierDbgBreakPoint();
        WdfRequestComplete(Request, status);
        return;
    }
    ASSERT (buffSize == Length);
    RtlCopyMemory(drvCtx->MemStats, buffer, Length);
    Dump(buffer, Length);
    Dump(drvCtx->MemStats, Length);
    WdfRequestCompleteWithInformation(Request, status, Length);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "WdfRequestCompleteWithInformation Called! Queue 0x%p, Request 0x%p Length %d Status 0x%08x\n",
             Queue,Request,Length, status);
}
