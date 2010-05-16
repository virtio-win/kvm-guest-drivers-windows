#include "precomp.h"

EVT_WDF_IO_QUEUE_IO_WRITE BalloonIoWrite;
EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE BalloonEvtIoCanceledOnQueue;

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, BalloonQueueInitialize)
#endif

VOID
BalloonEvtIoCanceledOnQueue(
    IN WDFQUEUE  Queue,
    IN WDFREQUEST  Request
    )
{
    UNREFERENCED_PARAMETER(Queue);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);
    WdfRequestComplete(Request, STATUS_CANCELLED);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}

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
    queueConfig.EvtIoCanceledOnQueue = BalloonEvtIoCanceledOnQueue;

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

    devCtx = GetDeviceContext(Device);

    WDF_IO_QUEUE_CONFIG_INIT(
                 &queueConfig,
                 WdfIoQueueDispatchManual);

    queueConfig.EvtIoCanceledOnQueue = BalloonEvtIoCanceledOnQueue;

    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &devCtx->StatusQueue);

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
    WDFMEMORY              memory;
    VIO_SG                 sg;
    PVIOQUEUE              vq;
    PDEVICE_CONTEXT        devCtx = GetDeviceContext(WdfIoQueueGetDevice( Queue ));
    PDRIVER_CONTEXT        drvCtx = GetDriverContext(WdfGetDriver());
    NTSTATUS               status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "BalloonIoWrite Called! Queue 0x%p, Request 0x%p Length %d\n",
             Queue,Request,Length);

    if( Length < sizeof(BALLOON_STAT)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "BalloonIoWrite Buffer Length to small %d, expected is %d\n",
                 Length, sizeof(BALLOON_STAT));
        WdfRequestCompleteWithInformation(Request, STATUS_BUFFER_OVERFLOW, 0L);
        return;
    }

    status = WdfRequestRetrieveInputMemory(Request, &memory);
    if( !NT_SUCCESS(status) ) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "BalloonIoWrite Could not get request memory buffer 0x%08x\n",
                 status);
        WdfVerifierDbgBreakPoint();
        WdfRequestComplete(Request, status);
        return;
    }

    status = WdfMemoryCopyToBuffer( 
                             memory,
                             0,
                             drvCtx->MemStats,
                             Length );
    if( !NT_SUCCESS(status) ) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "WdfMemoryCopyFromBuffer failed 0x%08x\n", status);
        WdfRequestComplete(Request, status);
        return;
    }

    sg.physAddr = GetPhysicalAddress(drvCtx->MemStats);
    sg.ulSize = Length ;

    vq = devCtx->StatVirtQueue; 
    if(vq->vq_ops->add_buf(vq, &sg, 1, 0, devCtx)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "Cannot add buffer\n");
    }

    vq->vq_ops->kick(vq);

    status = WdfRequestForwardToIoQueue(Request, devCtx->StatusQueue);
    if ( !NT_SUCCESS(status) ) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "WdfRequestForwardToIoQueue failed: 0x%08x\n", status);
    }
    return;
}
