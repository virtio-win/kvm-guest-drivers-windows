/*
 * Interrupt related functions
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
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
        VIOInputAddInBuf(
            pContext->EventQ,
            pEvent,
            VirtIOWdfDeviceGetPhysicalAddress(&pContext->VDevice.VIODevice, pEvent));
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
        pContext->StatusQMemBlock->return_slice(pContext->StatusQMemBlock, pEventReq);
    }
    WdfSpinLockRelease(pContext->StatusQLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}
