/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: IsrDpc.c
 *
 * Author(s):
 *
 * Interrupt related functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioinput.h"

#if defined(EVENT_TRACING)
#include "IsrDpc.tmh"
#endif

static
VOID
VIOInputEnableInterrupt(PINPUT_DEVICE pContext)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s enable\n", __FUNCTION__);

    if (!pContext)
        return;

    if (pContext->EventQ)
    {
        virtqueue_enable_cb(pContext->EventQ);
        virtqueue_kick(pContext->EventQ);
    }
    if (pContext->StatusQ)
    {
        virtqueue_enable_cb(pContext->StatusQ);
        virtqueue_kick(pContext->StatusQ);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s enable\n", __FUNCTION__);
}

static
VOID
VIOInputDisableInterrupt(PINPUT_DEVICE pContext)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s disable\n", __FUNCTION__);

    if (!pContext)
        return;

    if (pContext->EventQ)
    {
        virtqueue_disable_cb(pContext->EventQ);
    }
    if (pContext->StatusQ)
    {
        virtqueue_disable_cb(pContext->StatusQ);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s disable\n", __FUNCTION__);
}

NTSTATUS
VIOInputInterruptEnable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    VIOInputEnableInterrupt(GetDeviceContext(WdfInterruptGetDevice(Interrupt)));
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
VIOInputInterruptDisable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    VIOInputDisableInterrupt(GetDeviceContext(WdfInterruptGetDevice(Interrupt)));
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

BOOLEAN
VIOInputInterruptIsr(
    IN WDFINTERRUPT Interrupt,
    IN ULONG MessageID)
{
    PINPUT_DEVICE pContext = GetDeviceContext(WdfInterruptGetDevice(Interrupt));
    WDF_INTERRUPT_INFO info;
    BOOLEAN serviced;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(Interrupt, &info);

    // Schedule a DPC if the device is using message-signaled interrupts, or
    // if the device ISR status is enabled.
    if (info.MessageSignaled || VirtIOWdfGetISRStatus(&pContext->VDevice))
    {
        WdfInterruptQueueDpcForIsr(Interrupt);
        serviced = TRUE;
    }
    else
    {
        serviced = FALSE;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return serviced;
}

VOID
VIOInputQueuesInterruptDpc(
    IN WDFINTERRUPT Interrupt,
    IN WDFOBJECT AssociatedObject)
{
    WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
    PINPUT_DEVICE pContext = GetDeviceContext(Device);
    PVIRTIO_INPUT_EVENT pEvent;
    PVIRTIO_INPUT_EVENT_WITH_REQUEST pEventReq;
    UINT len;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(pContext->EventQLock);
    while ((pEvent = virtqueue_get_buf(pContext->EventQ, &len)) != NULL)
    {
        // translate event to a HID report and complete a pending HID request
        ProcessInputEvent(pContext, pEvent);

        // add the buffer back to the queue
        VIOInputAddInBuf(pContext->EventQ, pEvent);
    }
    WdfSpinLockRelease(pContext->EventQLock);

    WdfSpinLockAcquire(pContext->StatusQLock);
    while ((pEventReq = virtqueue_get_buf(pContext->StatusQ, &len)) != NULL)
    {
        // complete the pending request
        if (pEventReq->Request != NULL)
        {
            WdfRequestComplete(pEventReq->Request, STATUS_SUCCESS);
        }

        // free the buffer
        ExFreePoolWithTag(pEventReq, VIOINPUT_DRIVER_MEMORY_TAG);
    }
    WdfSpinLockRelease(pContext->StatusQLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}
