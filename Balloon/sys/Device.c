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
#endif // ALLOC_PRAGMA

#define LOMEMEVENTNAME L"\\KernelObjects\\LowMemoryCondition"
DECLARE_CONST_UNICODE_STRING(evLowMemString, LOMEMEVENTNAME);


NTSTATUS
BalloonDeviceAdd(
    IN WDFDRIVER  Driver,
    IN PWDFDEVICE_INIT  DeviceInit)
{
    NTSTATUS                     status = STATUS_SUCCESS;
    WDFDEVICE                    device;
    PDEVICE_CONTEXT              devCtx = NULL;
    WDF_INTERRUPT_CONFIG         interruptConfig;
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
#ifdef USE_BALLOON_SERVICE
    WDF_FILEOBJECT_CONFIG        fileConfig;
#endif // USE_BALLOON_SERVICE

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

    pnpPowerCallbacks.EvtDevicePrepareHardware      = BalloonEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware      = BalloonEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry              = BalloonEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit               = BalloonEvtDeviceD0Exit;
    pnpPowerCallbacks.EvtDeviceD0ExitPreInterruptsDisabled = BalloonEvtDeviceD0ExitPreInterruptsDisabled;

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

    WDF_FILEOBJECT_CONFIG_INIT(
                            &fileConfig,
                            WDF_NO_EVENT_CALLBACK,
                            BalloonEvtFileClose,
                            WDF_NO_EVENT_CALLBACK
                            );

    WdfDeviceInitSetFileObjectConfig(DeviceInit,
                            &fileConfig,
                            WDF_NO_OBJECT_ATTRIBUTES);
#endif // USE_BALLOON_SERVICE

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfDeviceCreate failed with status 0x%08x\n", status);
        return status;
    }

    devCtx = GetDeviceContext(device);

    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
                            BalloonInterruptIsr,
                            BalloonInterruptDpc);

    interruptConfig.EvtInterruptEnable  = BalloonInterruptEnable;
    interruptConfig.EvtInterruptDisable = BalloonInterruptDisable;

#ifndef USE_BALLOON_SERVICE
    // Serialize the DPC routine with queue operations to prevent races
    // around PendingWriteRequest and HandleWriteRequest.
    interruptConfig.AutomaticSerialization = TRUE;
#endif // !USE_BALLOON_SERVICE

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

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_BALLOON, NULL);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "WdfDeviceCreateDeviceInterface failed with status 0x%08x\n", status);
        return status;
    }

    devCtx->bShutDown = FALSE;
    devCtx->num_pages = 0;
    devCtx->PageListHead.Next = NULL;
    ExInitializeNPagedLookasideList(
                      &devCtx->LookAsideList,
                      NULL,
                      NULL,
#if !defined(NTDDI_WIN8) || (NTDDI_VERSION < NTDDI_WIN8)
                      0,
#else
                      POOL_NX_ALLOCATION,
#endif
                      sizeof(PAGE_LIST_ENTRY),
                      BALLOON_MGMT_POOL_TAG,
                      0
                      );
    devCtx->bListInitialized = TRUE;
    devCtx->pfns_table = NULL;
    devCtx->MemStats = NULL;


    KeInitializeEvent(&devCtx->HostAckEvent,
                      SynchronizationEvent,
                      FALSE
                      );

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;

    status = WdfSpinLockCreate(
        &attributes,
        &devCtx->StatQueueLock
        );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfSpinLockCreate failed 0x%x\n", status);
        return status;
    }

    status = WdfSpinLockCreate(
        &attributes,
        &devCtx->InfDefQueueLock
        );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfSpinLockCreate failed 0x%x\n", status);
        return status;
    }

#ifdef USE_BALLOON_SERVICE
    status = BalloonQueueInitialize(device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "BalloonQueueInitialize failed with status 0x%08x\n", status);
        return status;
    }
#else // USE_BALLOON_SERVICE
    status = StatInitializeWorkItem(device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "StatInitializeWorkItem failed with status 0x%08x\n", status);
        return status;
    }
#endif // USE_BALLOON_SERVICE

    KeInitializeEvent(&devCtx->WakeUpThread,
                      SynchronizationEvent,
                      FALSE
                      );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID
BalloonEvtDeviceContextCleanup(
    IN WDFOBJECT  Device
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext((WDFDEVICE)Device);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    if(devCtx->bListInitialized)
    {
        ExDeleteNPagedLookasideList(&devCtx->LookAsideList);
        devCtx->bListInitialized = FALSE;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
BalloonEvtDevicePrepareHardware(
    IN WDFDEVICE    Device,
    IN WDFCMRESLIST ResourceList,
    IN WDFCMRESLIST ResourceListTranslated
    )
{
    NTSTATUS            status         = STATUS_SUCCESS;
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourceList);

    PAGED_CODE();

    devCtx = GetDeviceContext(Device);

    status = VirtIOWdfInitialize(
        &devCtx->VDevice,
        Device,
        ResourceListTranslated,
        NULL,
        BALLOON_MGMT_POOL_TAG);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "VirtIOWdfInitialize failed with %x\n", status);
        return status;
    }

    if (NT_SUCCESS(status))
    {
        devCtx->MemStats = (PBALLOON_STAT)VirtIOWdfDeviceAllocDmaMemory(&devCtx->VDevice.VIODevice, PAGE_SIZE, BALLOON_MGMT_POOL_TAG);
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
        devCtx->pfns_table = (PPFN_NUMBER)VirtIOWdfDeviceAllocDmaMemory(&devCtx->VDevice.VIODevice, PAGE_SIZE, BALLOON_MGMT_POOL_TAG);
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
BalloonEvtDeviceReleaseHardware (
    IN WDFDEVICE      Device,
    IN WDFCMRESLIST   ResourcesTranslated
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;

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
BalloonCreateWorkerThread(
    IN WDFDEVICE  Device
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(Device);
    NTSTATUS            status = STATUS_SUCCESS;
    HANDLE              hThread = 0;
    OBJECT_ATTRIBUTES   oa;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);
    devCtx->bShutDown = FALSE;

    if(NULL == devCtx->Thread)
    {
        InitializeObjectAttributes(&oa, NULL, 
            OBJ_KERNEL_HANDLE, NULL, NULL);

        status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &oa, NULL, NULL,
                                          BalloonRoutine, Device);

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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
BalloonCloseWorkerThread(
    IN WDFDEVICE  Device
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(Device);
    NTSTATUS            status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    PAGED_CODE();

    if(NULL != devCtx->Thread)
    {
        devCtx->bShutDown = TRUE;
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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}


NTSTATUS
BalloonEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PreviousState
    )
{
    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    status = BalloonInit(Device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "BalloonInit failed with status 0x%08x\n", status);
        BalloonTerm(Device);
        return status;
    }

    status = BalloonCreateWorkerThread(Device);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
           "BalloonCreateWorkerThread failed with status 0x%08x\n", status);
    } 

    devCtx->evLowMem = IoCreateNotificationEvent(
        (PUNICODE_STRING)&evLowMemString, &devCtx->hLowMem);

    return status;
}

NTSTATUS
BalloonEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(TargetState);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--> %s\n", __FUNCTION__);

    PAGED_CODE();

    if (devCtx->evLowMem)
    {
        ZwClose(devCtx->hLowMem);
        devCtx->evLowMem = NULL;
    }

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
BalloonEvtDeviceD0ExitPreInterruptsDisabled(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--> %s\n", __FUNCTION__);

    PAGED_CODE();

    BalloonCloseWorkerThread(Device);
    if (TargetState == WdfPowerDeviceD3Final)
    {
       while(devCtx->num_pages)
       {
          BalloonLeak(Device, devCtx->num_pages);
       }

       BalloonSetSize(Device, devCtx->num_pages);
    }
    return STATUS_SUCCESS;
}

BOOLEAN
BalloonInterruptIsr(
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
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
        WdfInterruptQueueDpcForIsr( WdfInterrupt );
        return TRUE;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s No Isr indicated\n", __FUNCTION__);
    }
    return FALSE;
}

VOID
BalloonInterruptDpc(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
    )
{
    unsigned int          len;
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);
    PVOID                 buffer;

    BOOLEAN               bHostAck = FALSE;
    UNREFERENCED_PARAMETER( WdfInterrupt );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(devCtx->InfDefQueueLock);
    if (virtqueue_get_buf(devCtx->InfVirtQueue, &len))
    {
        bHostAck = TRUE;
    }
    if (virtqueue_get_buf(devCtx->DefVirtQueue, &len))
    {
        bHostAck = TRUE;
    }
    WdfSpinLockRelease(devCtx->InfDefQueueLock);

    if(bHostAck)
    {
        KeSetEvent (&devCtx->HostAckEvent, EVENT_INCREMENT, FALSE);
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

            if ((request != NULL) &&
                (WdfRequestUnmarkCancelable(request) != STATUS_CANCELLED))
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
#else // USE_BALLOON_SERVICE
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
            if (1==InterlockedIncrement(&devCtx->WorkCount))
            {
                WdfWorkItemEnqueue(devCtx->StatWorkItem);
            }
#endif // USE_BALLOON_SERVICE
        }
    }

    if(devCtx->Thread)
    {
       KeSetEvent(&devCtx->WakeUpThread, EVENT_INCREMENT, FALSE);
    }
}

NTSTATUS
BalloonInterruptEnable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    devCtx = GetDeviceContext(WdfDevice);
    EnableInterrupt(WdfInterrupt, devCtx);
    BalloonInterruptIsr(WdfInterrupt, 0);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
BalloonInterruptDisable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    )
{
    PDEVICE_CONTEXT     devCtx = NULL;
    UNREFERENCED_PARAMETER( WdfInterrupt );

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    devCtx = GetDeviceContext(WdfDevice);
    DisableInterrupt(devCtx);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

#ifdef USE_BALLOON_SERVICE
VOID
BalloonEvtFileClose(
    IN WDFFILEOBJECT    FileObject
    )
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

VOID
BalloonSetSize(
    IN WDFOBJECT WdfDevice,
    IN size_t    num
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);
    u32 actual = (u32)num;
    VirtIOWdfDeviceSet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, actual), &actual, sizeof(actual));
}

LONGLONG
BalloonGetSize(
    IN WDFOBJECT WdfDevice
    )
{
    PDEVICE_CONTEXT       devCtx = GetDeviceContext(WdfDevice);

    u32 v;
    VirtIOWdfDeviceGet(&devCtx->VDevice, FIELD_OFFSET(VIRTIO_BALLOON_CONFIG, num_pages), &v, sizeof(v));
    return (LONGLONG)v - devCtx->num_pages;
}

VOID
BalloonRoutine(
    IN PVOID pContext
    )
{
    WDFOBJECT Device  =  (WDFOBJECT)pContext;
    PDEVICE_CONTEXT                 devCtx = GetDeviceContext(Device);

    NTSTATUS            status = STATUS_SUCCESS;
    LONGLONG            diff;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Balloon thread started....\n");

    for (;;)
    {
        status = KeWaitForSingleObject(&devCtx->WakeUpThread, Executive,
                                       KernelMode, FALSE, NULL);
        if(STATUS_WAIT_0 == status)
        {
            if(devCtx->bShutDown)
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Exiting Thread!\n");
                break;
            }
            else
            {
                diff = BalloonGetSize(Device);
                if (diff > 0)
                {
                    BalloonFill(Device, (size_t)(diff));
                }
                else if (diff < 0)
                {
                    BalloonLeak(Device, (size_t)(-diff));
                }
                BalloonSetSize(Device, devCtx->num_pages);
            }
        }
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Thread about to exit...\n");

    PsTerminateSystemThread(STATUS_SUCCESS);
}
