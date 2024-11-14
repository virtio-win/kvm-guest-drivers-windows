/*
 * This file contains virtio-mem driver routines.
 *
 * Copyright (c) 2022 Red Hat, Inc.
 *
 * Author(s):
 *  Marek Kedzierski <mkedzier@redhat.com>
 *  virtiolib by the virtio-win Team
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

#if defined(EVENT_TRACING)
#include "Device.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, ViomemEvtDeviceContextCleanup)
#pragma alloc_text(PAGE, ViomemEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, ViomemEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, ViomemEvtDeviceD0Exit)
#pragma alloc_text(PAGE, ViomemEvtDeviceD0ExitPreInterruptsDisabled)
#pragma alloc_text(PAGE, ViomemDeviceAdd)
#pragma alloc_text(PAGE, ViomemCloseWorkerThread)

#endif // ALLOC_PRAGMA


NTSTATUS
ViomemDeviceAdd(
    IN WDFDRIVER  Driver,
    IN PWDFDEVICE_INIT  DeviceInit)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDFDEVICE                    device;
    PDEVICE_CONTEXT              devCtx = NULL;
    WDF_INTERRUPT_CONFIG         interruptConfig;
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Entry\n", __FUNCTION__);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

    pnpPowerCallbacks.EvtDevicePrepareHardware      = ViomemEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware      = ViomemEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry              = ViomemEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit               = ViomemEvtDeviceD0Exit;
    pnpPowerCallbacks.EvtDeviceD0ExitPreInterruptsDisabled = ViomemEvtDeviceD0ExitPreInterruptsDisabled;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = ViomemEvtDeviceContextCleanup;

    /* The driver initializes all the queues under lock of
     * WDF object of the device. If we use default execution
     * level, this lock is spinlock and common blocks required
     * for queues can't be allocated on DISPATCH. So, we change
     * the execution level to PASSIVE -> the lock is fast mutex
     */
    attributes.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfDeviceCreate failed with status 0x%08x\n", status);
        return status;
    }

    devCtx = GetDeviceContext(device);

    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
                            ViomemISR,
                            ViomemDPC);

    interruptConfig.EvtInterruptEnable  = ViomemEnableInterrupts;
    interruptConfig.EvtInterruptDisable = ViomemDisableInterrupts;

    status = WdfInterruptCreate(device,
                            &interruptConfig,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            &devCtx->WdfInterrupt);
    if (!NT_SUCCESS (status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfInterruptCreate failed: 0x%08x\n", status);
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_VIOMEM, NULL);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfDeviceCreateDeviceInterface failed with status 0x%08x\n", status);
        return status;
    }

	memset(&devCtx->MemoryConfiguration, 0, sizeof(devCtx->MemoryConfiguration));
    devCtx->finishProcessing = FALSE;

    KeInitializeEvent(&devCtx->hostAcknowledge, SynchronizationEvent, FALSE);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;

    status = WdfSpinLockCreate(
        &attributes,
        &devCtx->infVirtQueueLock
        );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfSpinLockCreate failed 0x%x\n", status);
        return status;
    }

    KeInitializeEvent(&devCtx->WakeUpThread,
                      SynchronizationEvent,
                      FALSE
                      );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Return\n", __FUNCTION__);
    return status;
}

VOID
ViomemEvtDeviceContextCleanup(
    IN WDFOBJECT  Device
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext((WDFDEVICE)Device);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Entry\n", __FUNCTION__);

	//
	// Currently nothing here.
	//

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Return\n", __FUNCTION__);
}

NTSTATUS
ViomemEvtDevicePrepareHardware(
    IN WDFDEVICE    Device,
    IN WDFCMRESLIST ResourceList,
    IN WDFCMRESLIST ResourceListTranslated
    )
{
    NTSTATUS            status         = STATUS_SUCCESS;
    PDEVICE_CONTEXT     devCtx = NULL;
	virtio_mem_config configReqest = { 0 };

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Entry\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourceList);

    PAGED_CODE();

    devCtx = GetDeviceContext(Device);

    status = VirtIOWdfInitialize(
        &devCtx->VDevice,
        Device,
        ResourceListTranslated,
        NULL,
		VIRTIO_MEM_POOL_TAG);

	if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "VirtIOWdfInitialize failed with %x\n", status);
        return status;
    }

	//
	// Initialize memory for request, response, and bitmap.
	//

	if (NT_SUCCESS(status))
	{
		devCtx->plugRequest = (virtio_mem_req*)VirtIOWdfDeviceAllocDmaMemory(
			&devCtx->VDevice.VIODevice, PAGE_SIZE, VIRTIO_MEM_POOL_TAG);
	}

	if (devCtx->plugRequest)
	{
		RtlFillMemory(devCtx->plugRequest, sizeof(virtio_mem_req), VIRTIO_MEM_POOL_TAG);
	}
	else
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "Failed to allocate MemStats block\n");
		status = STATUS_INSUFFICIENT_RESOURCES;
	}

	if (NT_SUCCESS(status))
	{
		devCtx->MemoryResponse = (virtio_mem_resp*)VirtIOWdfDeviceAllocDmaMemory(
			&devCtx->VDevice.VIODevice, PAGE_SIZE, VIRTIO_MEM_POOL_TAG);
	}

	if (devCtx->MemoryResponse)
	{
		RtlFillMemory(devCtx->MemoryResponse, sizeof(virtio_mem_resp), VIRTIO_MEM_POOL_TAG);
	}
	else
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "Failed to allocate MemoryResponse block\n");
		status = STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlFillMemory(&devCtx->memoryBitmapHandle, sizeof(RTL_BITMAP), 0);
	devCtx->bitmapBuffer = NULL;

	//
	// Set the processing state to initialization.
	//

	devCtx->state = VIOMEM_PROCESS_STATE_INIT;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Return\n", __FUNCTION__);
    return status;
}

NTSTATUS
ViomemEvtDeviceReleaseHardware (
    IN WDFDEVICE      Device,
    IN WDFCMRESLIST   ResourcesTranslated
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Entry\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    WdfObjectAcquireLock(Device);

    devCtx = GetDeviceContext(Device);

    VirtIOWdfDeviceFreeDmaMemoryByTag(&devCtx->VDevice.VIODevice, VIRTIO_MEM_POOL_TAG);

    WdfObjectReleaseLock(Device);

    VirtIOWdfShutdown(&devCtx->VDevice);

    if(devCtx->bitmapBuffer)
    {
	    ExFreePoolWithTag(devCtx->bitmapBuffer, VIRTIO_MEM_POOL_TAG);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s Return\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
ViomemCreateWorkerThread(
    IN WDFDEVICE  Device
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(Device);
    NTSTATUS            status = STATUS_SUCCESS;
    HANDLE              hThread = 0;
    OBJECT_ATTRIBUTES   oa;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Entry\n", __FUNCTION__);
    devCtx->finishProcessing = FALSE;

    if(devCtx->Thread == NULL)
    {
        InitializeObjectAttributes(&oa, NULL,
            OBJ_KERNEL_HANDLE, NULL, NULL);

        status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &oa, NULL, NULL,
                                          ViomemWorkerThread, Device);

        if(!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
              "failed to create worker thread status 0x%08x\n", status);
            return status;
        }

        ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL,
                                  KernelMode, (PVOID*)&devCtx->Thread, NULL);
        KeSetPriorityThread(devCtx->Thread, LOW_REALTIME_PRIORITY);

        ZwClose(hThread);
    }

    KeSetEvent(&devCtx->WakeUpThread, EVENT_INCREMENT, FALSE);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Return\n", __FUNCTION__);
    return status;
}

NTSTATUS
ViomemCloseWorkerThread(
    IN WDFDEVICE  Device
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(Device);
    NTSTATUS            status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Entry\n", __FUNCTION__);

    PAGED_CODE();

    if(NULL != devCtx->Thread)
    {
        devCtx->finishProcessing = TRUE;
        KeSetEvent(&devCtx->WakeUpThread, EVENT_INCREMENT, FALSE);
        status = KeWaitForSingleObject(devCtx->Thread, Executive, KernelMode, FALSE, NULL);
        if(!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
              "KeWaitForSingleObject didn't succeed status 0x%08x\n", status);
        }
        ObDereferenceObject(devCtx->Thread);
        devCtx->Thread = NULL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Return\n", __FUNCTION__);
    return status;
}


NTSTATUS
ViomemEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PreviousState
    )
{
    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Entry\n", __FUNCTION__);

    status = ViomemInit(Device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "ViomemInit failed with status 0x%08x\n", status);
        ViomemTerminate(Device);
        return status;
    }

    status = ViomemCreateWorkerThread(Device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "ViomemCreateWorkerThread failed with status 0x%08x\n", status);
    }

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Return \n", __FUNCTION__);

    return status;
}

NTSTATUS
ViomemEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(TargetState);

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Entry\n", __FUNCTION__);

    PAGED_CODE();

    ViomemTerminate(Device);

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Return\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

NTSTATUS
ViomemEvtDeviceD0ExitPreInterruptsDisabled(
	IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(Device);

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Entry\n", __FUNCTION__);

    PAGED_CODE();

    ViomemCloseWorkerThread(Device);
    if (TargetState == WdfPowerDeviceD3Final)
    {
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Return\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

BOOLEAN
ViomemISR(
    IN WDFINTERRUPT WdfInterrupt,
    IN ULONG        MessageID
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;
    WDFDEVICE           Device;
    UNREFERENCED_PARAMETER( MessageID );

    Device = WdfInterruptGetDevice(WdfInterrupt);
    devCtx = GetDeviceContext(Device);

    if (VirtIOWdfGetISRStatus(&devCtx->VDevice) > 0)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "%s queueing DPC\n", __FUNCTION__);
        WdfInterruptQueueDpcForIsr( WdfInterrupt );
        return TRUE;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "%s No ISR indicated\n", __FUNCTION__);
    }
    return FALSE;
}

VOID
ViomemDPC(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
    )
{
    unsigned int          len;
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);
    PVOID                 buffer;

    BOOLEAN               bHostAck = FALSE;
    UNREFERENCED_PARAMETER( WdfInterrupt );

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Entry\n", __FUNCTION__);

    WdfSpinLockAcquire(devCtx->infVirtQueueLock);

	if (virtqueue_has_buf(devCtx->infVirtQueue))
	{
        bHostAck = TRUE;
    }
    WdfSpinLockRelease(devCtx->infVirtQueueLock);

    if(bHostAck)
    {
        KeSetEvent (&devCtx->hostAcknowledge, EVENT_INCREMENT, FALSE);
    }

    if(devCtx->Thread)
    {
       KeSetEvent(&devCtx->WakeUpThread, EVENT_INCREMENT, FALSE);
    }

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Return\n", __FUNCTION__);
}

NTSTATUS
ViomemEnableInterrupts(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Entry\n", __FUNCTION__);

    devCtx = GetDeviceContext(WdfDevice);
    EnableInterrupt(WdfInterrupt, devCtx);
    ViomemISR(WdfInterrupt, 0);

	TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "%s Return\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
ViomemDisableInterrupts(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;
    UNREFERENCED_PARAMETER( WdfInterrupt );

	TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "%s Entry\n", __FUNCTION__);

    devCtx = GetDeviceContext(WdfDevice);
    DisableInterrupt(devCtx);

	TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "%s Return\n", __FUNCTION__);
    return STATUS_SUCCESS;
}
