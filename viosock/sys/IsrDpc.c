/*
 * Interrupt handling functions
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
#include "IsrDpc.tmh"
#endif

EVT_WDF_INTERRUPT_ISR       VIOSockInterruptIsr;
EVT_WDF_INTERRUPT_DPC       VIOSockInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE    VIOSockInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE   VIOSockInterruptDisable;

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSockInterruptInit)
#endif

NTSTATUS
VIOSockInterruptInit(
    IN WDFDEVICE hDevice)
{
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_INTERRUPT_CONFIG         interruptConfig;
    PDEVICE_CONTEXT              pContext = GetDeviceContext(hDevice);
    NTSTATUS                     status = STATUS_SUCCESS;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s\n", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_INTERRUPT_CONFIG_INIT(
        &interruptConfig,
        VIOSockInterruptIsr,
        VIOSockInterruptDpc
    );

    interruptConfig.EvtInterruptEnable = VIOSockInterruptEnable;
    interruptConfig.EvtInterruptDisable = VIOSockInterruptDisable;

    status = WdfInterruptCreate(
        hDevice,
        &interruptConfig,
        &attributes,
        &pContext->WdfInterrupt
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "Failed to create interrupt: %x\n", status);
        return status;
    }

    return status;
}

static
BOOLEAN
VIOSockInterruptIsr(
    IN WDFINTERRUPT Interrupt,
    IN ULONG MessageID)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfInterruptGetDevice(Interrupt));
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

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-- %s, serviced: %s\n", __FUNCTION__, serviced ? "TRUE" : "FALSE");

    return serviced;
}

static
VOID
VIOSockInterruptDpc(
    IN WDFINTERRUPT Interrupt,
    IN WDFOBJECT AssociatedObject)
{
    WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
    PDEVICE_CONTEXT pContext = GetDeviceContext(Device);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    // handle the Event queue
    VIOSockEvtVqProcess(pContext);

    // handle the Read queue
    VIOSockRxVqProcess(pContext);

    // handle the Write queue
    VIOSockTxVqProcess(pContext);
}

static
NTSTATUS
VIOSockInterruptEnable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfInterruptGetDevice(Interrupt));

    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);

    virtqueue_enable_cb(pContext->EvtVq);
    virtqueue_kick(pContext->EvtVq);

    virtqueue_enable_cb(pContext->RxVq);
    virtqueue_kick(pContext->RxVq);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

static
NTSTATUS
VIOSockInterruptDisable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE AssociatedDevice)
{
    PDEVICE_CONTEXT pContext = GetDeviceContext(WdfInterruptGetDevice(Interrupt));
    UNREFERENCED_PARAMETER(AssociatedDevice);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);

    if (pContext->TxVq)
        virtqueue_disable_cb(pContext->TxVq);

    if (pContext->RxVq)
        virtqueue_disable_cb(pContext->RxVq);

    if (pContext->EvtVq)
        virtqueue_disable_cb(pContext->EvtVq);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INTERRUPT, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}
