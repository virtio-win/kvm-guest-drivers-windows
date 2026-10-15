/*
 * This file contains balloon driver routines
 *
 * Copyright (c) 2009-2017  Red Hat, Inc.
 *
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@redhat.com>
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
#pragma alloc_text(PAGE, BalloonEvtDeviceContextCleanup)
#pragma alloc_text(PAGE, BalloonEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, BalloonEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, BalloonEvtDeviceD0Exit)
#pragma alloc_text(PAGE, BalloonEvtDeviceD0ExitPreInterruptsDisabled)
#pragma alloc_text(PAGE, BalloonDeviceAdd)
#pragma alloc_text(PAGE, BalloonCloseWorkerThread)
#pragma alloc_text(PAGE, BalloonCloseReportingThread)
#pragma alloc_text(PAGE, BalloonEvtDeviceSurpriseRemoval)
#endif // ALLOC_PRAGMA

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
#define LOMEMEVENTNAME L"\\KernelObjects\\LowMemoryCondition"
DECLARE_CONST_UNICODE_STRING(evLowMemString, LOMEMEVENTNAME);
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

NTSTATUS
BalloonDeviceAdd(IN WDFDRIVER Driver, IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device;
    PDEVICE_CONTEXT devCtx = NULL;
    WDF_INTERRUPT_CONFIG interruptConfig;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
#ifdef USE_BALLOON_SERVICE
    WDF_FILEOBJECT_CONFIG fileConfig;
#endif // USE_BALLOON_SERVICE

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

    pnpPowerCallbacks.EvtDevicePrepareHardware = BalloonEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = BalloonEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = BalloonEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = BalloonEvtDeviceD0Exit;
    pnpPowerCallbacks.EvtDeviceD0ExitPreInterruptsDisabled = BalloonEvtDeviceD0ExitPreInterruptsDisabled;
    pnpPowerCallbacks.EvtDeviceSurpriseRemoval = BalloonEvtDeviceSurpriseRemoval;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = BalloonEvtDeviceContextCleanup;

    /* The driver initializes all the queues under lock of
     * WDF object of the device. If we use default execution
     * level, this lock is spinlock and common blocks required
     * for queues can't be allocated on DISPATCH. So, we change
     * the execution level to PASSIVE -> the lock is fast mutex
     */
    attributes.ExecutionLevel = WdfExecutionLevelPassive;

#ifdef USE_BALLOON_SERVICE
    attributes.SynchronizationScope = WdfSynchronizationScopeDevice;

    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, WDF_NO_EVENT_CALLBACK, BalloonEvtFileClose, WDF_NO_EVENT_CALLBACK);

    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);
#endif // USE_BALLOON_SERVICE

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceCreate failed with status 0x%08x\n", status);
        return status;
    }

    devCtx = GetDeviceContext(device);

    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, BalloonInterruptIsr, BalloonInterruptDpc);

    interruptConfig.EvtInterruptEnable = BalloonInterruptEnable;
    interruptConfig.EvtInterruptDisable = BalloonInterruptDisable;

#ifndef USE_BALLOON_SERVICE
    // Serialize the DPC routine with queue operations to prevent races
    // around PendingWriteRequest and HandleWriteRequest.
    interruptConfig.AutomaticSerialization = TRUE;
#endif // !USE_BALLOON_SERVICE

    status = WdfInterruptCreate(device, &interruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &devCtx->WdfInterrupt);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfInterruptCreate failed: 0x%08x\n", status);
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_BALLOON, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfDeviceCreateDeviceInterface failed with status 0x%08x\n", status);
        return status;
    }

    devCtx->SurpriseRemoval = FALSE;
    devCtx->bShutDown = FALSE;
    devCtx->num_pages = 0;
    devCtx->PageListHead.Next = NULL;
    ExInitializeNPagedLookasideList(&devCtx->LookAsideList,
                                    NULL,
                                    NULL,
                                    POOL_NX_ALLOCATION,
                                    sizeof(PAGE_LIST_ENTRY),
                                    BALLOON_MGMT_POOL_TAG,
                                    0);
    devCtx->bListInitialized = TRUE;
    devCtx->pfns_table = NULL;
    devCtx->MemStats = NULL;

    KeInitializeEvent(&devCtx->HostAckEvent, SynchronizationEvent, FALSE);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;

    status = WdfSpinLockCreate(&attributes, &devCtx->StatQueueLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfSpinLockCreate failed 0x%x\n", status);
        return status;
    }

    status = WdfSpinLockCreate(&attributes, &devCtx->InfDefQueueLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfSpinLockCreate failed 0x%x\n", status);
        return status;
    }

    status = WdfSpinLockCreate(&attributes, &devCtx->ReportingLock);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfSpinLockCreate failed 0x%x\n", status);
        return status;
    }

#ifdef USE_BALLOON_SERVICE
    status = BalloonQueueInitialize(device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "BalloonQueueInitialize failed with status 0x%08x\n", status);
        return status;
    }
#else  // USE_BALLOON_SERVICE
    status = StatInitializeWorkItem(device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "StatInitializeWorkItem failed with status 0x%08x\n", status);
        return status;
    }
#endif // USE_BALLOON_SERVICE

    KeInitializeEvent(&devCtx->WakeUpThread, SynchronizationEvent, FALSE);
    KeInitializeEvent(&devCtx->RepAckEvent, SynchronizationEvent, FALSE);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID BalloonEvtDeviceContextCleanup(IN WDFOBJECT Device)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext((WDFDEVICE)Device);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if (devCtx->bListInitialized)
    {
        ExDeleteNPagedLookasideList(&devCtx->LookAsideList);
        devCtx->bListInitialized = FALSE;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
BalloonEvtDevicePrepareHardware(IN WDFDEVICE Device,
                                IN WDFCMRESLIST ResourceList,
                                IN WDFCMRESLIST ResourceListTranslated)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT devCtx = NULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourceList);

    PAGED_CODE();

    devCtx = GetDeviceContext(Device);

    status = VirtIOWdfInitialize(&devCtx->VDevice, Device, ResourceListTranslated, NULL, BALLOON_MGMT_POOL_TAG);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "VirtIOWdfInitialize failed with %x\n", status);
        return status;
    }

    if (NT_SUCCESS(status))
    {
        devCtx->MemStats = (PBALLOON_STAT)VirtIOWdfDeviceAllocDmaMemory(&devCtx->VDevice.VIODevice,
                                                                        PAGE_SIZE,
                                                                        BALLOON_MGMT_POOL_TAG);
    }

    if (devCtx->MemStats)
    {
        RtlFillMemory(devCtx->MemStats, sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR, -1);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "Failed to allocate MemStats block\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    /* use BALLOON_MGMT_POOL_TAG also for tagging common memory blocks */
    if (NT_SUCCESS(status))
    {
        devCtx->pfns_table = (PPFN_NUMBER)VirtIOWdfDeviceAllocDmaMemory(&devCtx->VDevice.VIODevice,
                                                                        PAGE_SIZE,
                                                                        BALLOON_MGMT_POOL_TAG);
    }

    if (devCtx->pfns_table == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "Failed to allocate PFNS_TABLE block\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
BalloonEvtDeviceReleaseHardware(IN WDFDEVICE Device, IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT devCtx = NULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    WdfObjectAcquireLock(Device);

    devCtx = GetDeviceContext(Device);

    VirtIOWdfDeviceFreeDmaMemoryByTag(&devCtx->VDevice.VIODevice, BALLOON_MGMT_POOL_TAG);
    devCtx->MemStats = NULL;
    devCtx->pfns_table = NULL;

    WdfObjectReleaseLock(Device);

    VirtIOWdfShutdown(&devCtx->VDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
BalloonCreateWorkerThread(IN WDFDEVICE Device)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;
    HANDLE hThread = 0;
    OBJECT_ATTRIBUTES oa;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if (devCtx->Thread != NULL)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_PNP, "Balloon thread already exists (0x%p)\n", devCtx->Thread);
        goto Exit;
    }

    devCtx->bShutDown = FALSE;

    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &oa, NULL, NULL, BalloonRoutine, Device);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "failed to create worker thread status 0x%08x\n", status);
        goto Exit;
    }

    status = ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID *)&devCtx->Thread, NULL);
    if (!NT_SUCCESS(status))
    {
        NTSTATUS waitStatus = STATUS_UNSUCCESSFUL;

        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "failed to reference thread: status 0x%08x\n", status);
        devCtx->bShutDown = TRUE;
        KeSetEvent(&devCtx->WakeUpThread, EVENT_INCREMENT, FALSE);
        waitStatus = ZwWaitForSingleObject(hThread, FALSE, NULL);
        if (!NT_SUCCESS(waitStatus))
        {
            TraceEvents(TRACE_LEVEL_WARNING,
                        DBG_PNP,
                        "Unable to wait for the thread handle 0x%p: 0x%x\n",
                        hThread,
                        waitStatus);
        }

        goto CloseThread;
    }

    KeSetPriorityThread(devCtx->Thread, LOW_REALTIME_PRIORITY);
    KeSetEvent(&devCtx->WakeUpThread, EVENT_INCREMENT, FALSE);

CloseThread:
    ZwClose(hThread);
Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
BalloonCloseWorkerThread(IN WDFDEVICE Device)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    PAGED_CODE();

    if (NULL != devCtx->Thread)
    {
        devCtx->bShutDown = TRUE;
        KeSetEvent(&devCtx->WakeUpThread, EVENT_INCREMENT, FALSE);
        status = KeWaitForSingleObject(devCtx->Thread, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "KeWaitForSingleObject didn't succeed status 0x%08x\n", status);
        }
        ObDereferenceObject(devCtx->Thread);
        devCtx->Thread = NULL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

/*
 * The reporting thread holds free pages at low priority (see
 * BalloonReportRoutine). Created only when free page reporting was
 * negotiated, so a driver without the feature keeps exactly one worker
 * thread, as before.
 */
NTSTATUS
BalloonCreateReportingThread(IN WDFDEVICE Device)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;
    HANDLE hThread = 0;
    OBJECT_ATTRIBUTES oa;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if (devCtx->RepThread != NULL)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_PNP, "Reporting thread already exists (0x%p)\n", devCtx->RepThread);
        goto Exit;
    }

    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &oa, NULL, NULL, BalloonReportRoutine, Device);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "failed to create reporting thread status 0x%08x\n", status);
        goto Exit;
    }

    status = ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID *)&devCtx->RepThread, NULL);
    if (!NT_SUCCESS(status))
    {
        NTSTATUS waitStatus = STATUS_UNSUCCESSFUL;

        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "failed to reference reporting thread: status 0x%08x\n", status);
        devCtx->bShutDown = TRUE;
        KeSetEvent(&devCtx->RepAckEvent, EVENT_INCREMENT, FALSE);
        waitStatus = ZwWaitForSingleObject(hThread, FALSE, NULL);
        if (!NT_SUCCESS(waitStatus))
        {
            TraceEvents(TRACE_LEVEL_WARNING,
                        DBG_PNP,
                        "Unable to wait for the reporting thread handle 0x%p: 0x%x\n",
                        hThread,
                        waitStatus);
        }

        goto CloseThread;
    }

    /* any guest thread can preempt a hold cycle; the command thread keeps
     * its real-time priority and never runs hold work itself (see
     * BalloonReportCheckRelease) */
    KeSetPriorityThread(devCtx->RepThread, LOW_PRIORITY);

CloseThread:
    ZwClose(hThread);
Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
BalloonCloseReportingThread(IN WDFDEVICE Device)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    PAGED_CODE();

    if (NULL != devCtx->RepThread)
    {
        devCtx->bShutDown = TRUE;
        KeSetEvent(&devCtx->RepAckEvent, EVENT_INCREMENT, FALSE);
        status = KeWaitForSingleObject(devCtx->RepThread, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "KeWaitForSingleObject didn't succeed status 0x%08x\n", status);
        }
        ObDereferenceObject(devCtx->RepThread);
        devCtx->RepThread = NULL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
BalloonEvtDeviceD0Entry(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE PreviousState)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    status = BalloonInit(Device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "BalloonInit failed with status 0x%08x\n", status);
        goto Terminate;
    }

    /* with free page reporting negotiated, initialize the reporting state
     * before the worker thread starts: the thread reads the reporting
     * parameters */
    if (devCtx->RepVirtQueue != NULL)
    {
        status = BalloonReportInitialize(Device);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "BalloonReportInitialize failed with status 0x%08x\n", status);
            goto Terminate;
        }
    }

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
    /* open the memory manager's low memory condition event before the
     * worker thread starts: the inflate path refuses to take pages under
     * a low memory condition (see BalloonFill), and with free page
     * reporting negotiated the worker thread also waits on the event */
    devCtx->evLowMem = IoCreateNotificationEvent((PUNICODE_STRING)&evLowMemString, &devCtx->hLowMem);
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

    status = BalloonCreateWorkerThread(Device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "BalloonCreateWorkerThread failed with status 0x%08x\n", status);
        goto Terminate;
    }

    /* the reporting thread exists only with free page reporting */
    if (devCtx->RepVirtQueue != NULL)
    {
        status = BalloonCreateReportingThread(Device);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "BalloonCreateReportingThread failed with status 0x%08x\n", status);
            /* the command thread is already running - stop it before the
             * failure path tears the queues down */
            BalloonCloseWorkerThread(Device);
            goto Terminate;
        }
    }

Terminate:
    if (!NT_SUCCESS(status))
    {
        BalloonTerm(Device);
    }

    return status;
}

NTSTATUS
BalloonEvtDeviceD0Exit(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE TargetState)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(TargetState);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--> %s\n", __FUNCTION__);

    PAGED_CODE();

    /* stop both threads before closing the low memory condition event:
     * the command thread waits on the event and the event object goes
     * away with the last handle. The reporting thread must be gone
     * before the held pages are released below. */
    BalloonCloseReportingThread(Device);
    BalloonCloseWorkerThread(Device);

    if (devCtx->RepVirtQueue != NULL)
    {
        BalloonReportReleaseAll(Device);
        devCtx->RepVirtQueue = NULL;
    }

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
    if (devCtx->evLowMem)
    {
        ZwClose(devCtx->hLowMem);
        devCtx->evLowMem = NULL;
    }
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

#ifndef USE_BALLOON_SERVICE
    /*
     * interrupts were already disabled (between BalloonEvtDeviceD0ExitPreInterruptsDisabled and this call)
     * we should flush StatWorkItem before calling BalloonTerm which will delete virtio queues
     */
    WdfWorkItemFlush(devCtx->StatWorkItem);
#endif // !USE_BALLOON_SERVICE

    BalloonTerm(Device);

    return STATUS_SUCCESS;
}

NTSTATUS
BalloonEvtDeviceD0ExitPreInterruptsDisabled(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE TargetState)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--> %s\n", __FUNCTION__);

    PAGED_CODE();

    BalloonCloseReportingThread(Device);
    BalloonCloseWorkerThread(Device);
    if (devCtx->RepVirtQueue != NULL)
    {
        BalloonReportReleaseAll(Device);
    }
    if (TargetState == WdfPowerDeviceD3Final)
    {
        while (devCtx->num_pages)
        {
            BalloonLeak(Device, devCtx->num_pages);
        }

        BalloonSetSize(Device, devCtx->num_pages);
    }
    return STATUS_SUCCESS;
}

VOID BalloonEvtDeviceSurpriseRemoval(IN WDFDEVICE Device)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--> %s\n", __FUNCTION__);

    PAGED_CODE();

    devCtx->SurpriseRemoval = TRUE;
    KeSetEvent(&devCtx->HostAckEvent, EVENT_INCREMENT, FALSE);
    KeSetEvent(&devCtx->RepAckEvent, EVENT_INCREMENT, FALSE);
}

BOOLEAN
BalloonInterruptIsr(IN WDFINTERRUPT WdfInterrupt, IN ULONG MessageID)
{
    PDEVICE_CONTEXT devCtx = NULL;
    WDFDEVICE Device;

    UNREFERENCED_PARAMETER(MessageID);

    Device = WdfInterruptGetDevice(WdfInterrupt);
    devCtx = GetDeviceContext(Device);

    if (VirtIOWdfGetISRStatus(&devCtx->VDevice) > 0)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
        WdfInterruptQueueDpcForIsr(WdfInterrupt);
        return TRUE;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s No Isr indicated\n", __FUNCTION__);
    }
    return FALSE;
}

VOID BalloonInterruptDpc(IN WDFINTERRUPT WdfInterrupt, IN WDFOBJECT WdfDevice)
{
    unsigned int len;
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);
    PVOID buffer;

    BOOLEAN bInfDefAck = FALSE;
    BOOLEAN bRepAck = FALSE;
    UNREFERENCED_PARAMETER(WdfInterrupt);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(devCtx->InfDefQueueLock);
    if (virtqueue_get_buf(devCtx->InfVirtQueue, &len))
    {
        bInfDefAck = TRUE;
    }
    if (virtqueue_get_buf(devCtx->DefVirtQueue, &len))
    {
        bInfDefAck = TRUE;
    }
    if (devCtx->RepVirtQueue != NULL && virtqueue_get_buf(devCtx->RepVirtQueue, &len))
    {
        bRepAck = TRUE;
    }
    WdfSpinLockRelease(devCtx->InfDefQueueLock);

    if (bInfDefAck)
    {
        KeSetEvent(&devCtx->HostAckEvent, EVENT_INCREMENT, FALSE);
    }
    if (bRepAck)
    {
        /* the reporting thread owns the report requests, it gets its own
         * acknowledgment event (the command thread never waits on them) */
        KeSetEvent(&devCtx->RepAckEvent, EVENT_INCREMENT, FALSE);
    }

    if (devCtx->StatVirtQueue)
    {
        WdfSpinLockAcquire(devCtx->StatQueueLock);
        buffer = virtqueue_get_buf(devCtx->StatVirtQueue, &len);
        WdfSpinLockRelease(devCtx->StatQueueLock);

        if (buffer)
        {
#ifdef USE_BALLOON_SERVICE
            WDFREQUEST request = devCtx->PendingWriteRequest;

            devCtx->HandleWriteRequest = TRUE;

            if ((request != NULL) && (WdfRequestUnmarkCancelable(request) != STATUS_CANCELLED))
            {
                NTSTATUS status;
                PVOID buffer;
                size_t length = 0;

                devCtx->PendingWriteRequest = NULL;

                status = WdfRequestRetrieveInputBuffer(request, 0, &buffer, &length);
                if (!NT_SUCCESS(status))
                {
                    length = 0;
                }
                WdfRequestCompleteWithInformation(request, status, length);
            }
#else  // USE_BALLOON_SERVICE
            /*
             * According to MSDN 'Using Framework Work Items' article:
             * Create one or more work items that your driver requeues as necessary.
             * Subsequently, each time that the driver's EvtInterruptDpc callback
             * function is called it must determine if the EvtWorkItem callback
             * function has run. If the EvtWorkItem callback function has not run,
             * the EvtInterruptDpc callback function does not call WdfWorkItemEnqueue,
             * because the work item is still queued.
             * A few drivers might need to call WdfWorkItemFlush to flush their work
             * items from the work-item queue.
             *
             * For each dpc (i.e. interrupt) we'll push stats exactly that many times.
             */
            if (1 == InterlockedIncrement(&devCtx->WorkCount))
            {
                WdfWorkItemEnqueue(devCtx->StatWorkItem);
            }
#endif // USE_BALLOON_SERVICE
        }
    }

    if (devCtx->Thread)
    {
        KeSetEvent(&devCtx->WakeUpThread, EVENT_INCREMENT, FALSE);
    }

    /* a completed report request only unblocks the in-flight wait of
     * the reporting thread (RepAckEvent); the next hold cycle is paced
     * by the reporting interval, like the Linux page_reporting_delay_ms
     * paces the next reporting pass */
}

NTSTATUS
BalloonInterruptEnable(IN WDFINTERRUPT WdfInterrupt, IN WDFDEVICE WdfDevice)
{
    PDEVICE_CONTEXT devCtx = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    devCtx = GetDeviceContext(WdfDevice);
    EnableInterrupt(WdfInterrupt, devCtx);
    BalloonInterruptIsr(WdfInterrupt, 0);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
BalloonInterruptDisable(IN WDFINTERRUPT WdfInterrupt, IN WDFDEVICE WdfDevice)
{
    PDEVICE_CONTEXT devCtx = NULL;
    UNREFERENCED_PARAMETER(WdfInterrupt);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    devCtx = GetDeviceContext(WdfDevice);
    DisableInterrupt(devCtx);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

#ifdef USE_BALLOON_SERVICE
VOID BalloonEvtFileClose(IN WDFFILEOBJECT FileObject)
{
    WDFDEVICE Device = WdfFileObjectGetDevice(FileObject);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-> %s\n", __FUNCTION__);

    // synchronize with the device to make sure it doesn't exit D0 from underneath us
    WdfObjectAcquireLock(Device);

    if (devCtx->MemStats)
    {
        RtlFillMemory(devCtx->MemStats, sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR, -1);
    }

    if (devCtx->StatVirtQueue && devCtx->MemStats)
    {
        BalloonMemStats(Device);
    }

    WdfObjectReleaseLock(Device);
}
#endif // USE_BALLOON_SERVICE

VOID BalloonSetSize(IN WDFOBJECT WdfDevice, IN size_t num)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);
    u32 actual = (u32)num;
    VirtIOWdfDeviceSet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, actual), &actual, sizeof(actual));
}

LONGLONG
BalloonGetSize(IN WDFOBJECT WdfDevice)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);

    u32 v;
    VirtIOWdfDeviceGet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, num_pages), &v, sizeof(v));
    return (LONGLONG)v - devCtx->num_pages;
}

/*
 * The balloon command thread. It serves the host's inflate and deflate
 * commands and, with free page reporting negotiated, makes the reporting
 * release decisions. The wait at the top of the loop schedules both:
 *
 *   - Balloon commands wake the thread through WakeUpThread (see
 *     BalloonInterruptDpc).
 *   - The wait timeout is the commit headroom watchdog: while pages are
 *     held it fires every REPORTING_COMMIT_POLL_MS and the thread
 *     re-evaluates the watermarks (Windows exposes no event for a low
 *     commit limit). With nothing held the thread waits indefinitely -
 *     discovering newly freed memory is the reporting thread's job.
 *   - LowMemoryCondition hands all held pages back at once and starts
 *     the hold cooldown. The event stays signaled for as long as the
 *     low memory condition lasts, so the thread paces its re-checks at
 *     REPORTING_EVENT_RETRY_MS instead of re-running the release path.
 *
 * All three dispatch arms converge on BalloonReportCheckRelease, which
 * hands held pages back when the guest needs them; taking pages is not
 * this thread's business - the reporting thread (BalloonReportRoutine)
 * holds them at its own cadence. Hold work never runs on this thread:
 * a hold cycle can stretch arbitrarily under CPU contention (it runs at
 * low priority so that guest threads can preempt it), and keeping it
 * off the command thread bounds the host command latency by what the
 * command service itself does, independently of the reporting load.
 * Release decisions stay here: they are a few MDL frees (milliseconds)
 * and are the guest's way out of memory pressure, so they run at the
 * thread's real-time priority.
 *
 * The memory manager also exposes a HighMemoryCondition event; it is
 * deliberately not waited on. Its threshold is internal and does not
 * track the driver's own watermark, and the reporting interval already
 * bounds the latency of noticing freed memory, so waiting on it would
 * only buy a head start of less than one interval - not worth an extra
 * wait object and dispatch arm.
 *
 * Balloon commands are latency sensitive (the host waits for the actual
 * size ack), so the thread runs at real-time priority; the reporting
 * work is opportunistic (the host has no contract for when pages get
 * reported) and runs at low priority on the reporting thread, where any
 * guest thread can preempt it.
 */
VOID BalloonRoutine(IN PVOID pContext)
{
    WDFOBJECT Device = (WDFOBJECT)pContext;
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    NTSTATUS status = STATUS_SUCCESS;
    LONGLONG diff;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Balloon thread started....\n");

    for (;;)
    {
        PVOID waitObjects[2];
        ULONG waitCount = 1;
        ULONG lowIndex = 0;
        LARGE_INTEGER watchdogTimeout;
        PLARGE_INTEGER timeout = NULL;

        waitObjects[0] = &devCtx->WakeUpThread;

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
        /* only the reporting decisions wait on the low memory event, the
         * inflate path polls it (see BalloonFill); the event is opened
         * for the whole D0 period before this thread starts, see
         * BalloonEvtDeviceD0Entry */
        if (devCtx->RepVirtQueue != NULL && devCtx->evLowMem != NULL)
        {
            lowIndex = waitCount;
            waitObjects[waitCount++] = devCtx->evLowMem;
        }
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

        /* with free page reporting the thread runs the commit headroom
         * watchdog while pages are held, otherwise wait indefinitely;
         * the unlocked read only picks the cadence, so it stays benign */
        if (devCtx->RepVirtQueue != NULL && devCtx->ReportingMdlCount != 0)
        {
            watchdogTimeout.QuadPart = Int32x32To64(REPORTING_COMMIT_POLL_MS, -10000);
            timeout = &watchdogTimeout;
        }

        status = KeWaitForMultipleObjects(waitCount, waitObjects, WaitAny, Executive, KernelMode, FALSE, timeout, NULL);

        if (devCtx->bShutDown)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Exiting Thread!\n");
            break;
        }

        if (status == STATUS_WAIT_0)
        {
            diff = BalloonGetSize(Device);
            if (diff > 0)
            {
                status = BalloonFill(Device, (size_t)(diff));
            }
            else if (diff < 0)
            {
                status = BalloonLeak(Device, (size_t)(-diff));
            }

            if (status == STATUS_TIMEOUT || status == STATUS_NO_MORE_ENTRIES)
            {
                TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "Failed to inform the host\n");
                if (diff != 0)
                {
                    TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "Operation is in progress, continuing\n");
                    KeSetEvent(&devCtx->WakeUpThread, IO_NO_INCREMENT, FALSE);
                }
            }

            BalloonSetSize(Device, devCtx->num_pages);
        }
#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
        else if (devCtx->RepVirtQueue != NULL && lowIndex != 0 && status == (NTSTATUS)(STATUS_WAIT_0 + lowIndex))
        {
            /* low memory condition: hand everything back at once (the
             * guest's fastest way out of the condition, hence real-time
             * priority) and pace the re-checks while the event stays
             * signaled - notification events stay signaled until the
             * memory manager clears them, and re-running the release path
             * would change nothing */
            LARGE_INTEGER retryDelay;

            BalloonReportCheckRelease(Device);

            retryDelay.QuadPart = Int32x32To64(REPORTING_EVENT_RETRY_MS, -10000);
            while (!devCtx->bShutDown && IsLowMemory(Device))
            {
                KeDelayExecutionThread(KernelMode, FALSE, &retryDelay);
            }
        }
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM
        else if (status != STATUS_TIMEOUT || devCtx->RepVirtQueue == NULL)
        {
            /* no dispatch arm for this wake reason */
            continue;
        }

        /* after a command or on a watchdog tick: release held pages if
         * the guest needs them (BalloonReportCheckRelease is a no-op
         * without free page reporting) */
        BalloonReportCheckRelease(Device);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Thread about to exit...\n");

    PsTerminateSystemThread(STATUS_SUCCESS);
}

/*
 * The free page reporting thread. It runs one hold cycle per reporting
 * interval (the counterpart of the Linux page_reporting_delay_ms, which
 * paces a reporting pass the same way): each cycle holds at most
 * REPORTING_BATCHES_PER_CYCLE batches of free pages and stops early at
 * the watermarks, checked before every batch. The thread runs at low
 * priority for its whole lifetime: any guest thread can preempt a hold
 * cycle, and neither the host commands nor the release decisions ever
 * wait for it.
 */
VOID BalloonReportRoutine(IN PVOID pContext)
{
    WDFOBJECT Device = (WDFOBJECT)pContext;
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
    LARGE_INTEGER intervalTimeout;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Reporting thread started....\n");

    for (;;)
    {
        /* the interval is the reclaim cadence: one hold cycle per tick,
         * nothing in the guest can accelerate it, and the gates bound
         * what a cycle takes */
        intervalTimeout.QuadPart = Int32x32To64(devCtx->ReportingIntervalMs, -10000);

        KeDelayExecutionThread(KernelMode, FALSE, &intervalTimeout);

        if (devCtx->bShutDown)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Exiting reporting thread!\n");
            break;
        }

        BalloonReportHold(Device);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Reporting thread about to exit...\n");

    PsTerminateSystemThread(STATUS_SUCCESS);
}
