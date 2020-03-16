/*
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
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

#include "viofs.h"
#include "power.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VirtFsEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, VirtFsEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, VirtFsEvtDeviceD0Entry)
#pragma alloc_text(PAGE, VirtFsEvtDeviceD0Exit)
#endif

NTSTATUS VirtFsEvtDevicePrepareHardware(IN WDFDEVICE Device,
                                        IN WDFCMRESLIST Resources,
                                        IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;
    u64 HostFeatures, GuestFeatures = 0;

    UNREFERENCED_PARAMETER(Resources);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p",
        Device);

    PAGED_CODE();

    status = VirtIOWdfInitialize(&context->VDevice, Device,
        ResourcesTranslated, NULL, VIRT_FS_MEMORY_TAG);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER,
            "VirtIOWdfInitialize failed with %!STATUS!", status);
    }

    HostFeatures = VirtIOWdfGetDeviceFeatures(&context->VDevice);

    VirtIOWdfSetDriverFeatures(&context->VDevice, GuestFeatures,
        VIRTIO_F_IOMMU_PLATFORM);

    VirtIOWdfDeviceGet(&context->VDevice,
        FIELD_OFFSET(VIRTIO_FS_CONFIG, RequestQueues),
        &context->RequestQueues,
        sizeof(context->RequestQueues));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_POWER,
        "Request queues: %d", context->RequestQueues);

    context->VirtQueues = ExAllocatePoolWithTag(NonPagedPool,
        context->RequestQueues * sizeof(struct virtqueue*),
        VIRT_FS_MEMORY_TAG);

    if (context->VirtQueues != NULL)
    {
        RtlZeroMemory(context->VirtQueues,
            context->RequestQueues * sizeof(struct virtqueue*));
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER,
            "Failed to allocate request queues");

        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    if (NT_SUCCESS(status))
    {
        context->VirtQueueLocks = ExAllocatePoolWithTag(NonPagedPool,
            context->RequestQueues * sizeof(WDFSPINLOCK),
            VIRT_FS_MEMORY_TAG);
    }

    if (context->VirtQueueLocks != NULL)
    {
        WDF_OBJECT_ATTRIBUTES attributes;
        WDFSPINLOCK *lock;
        ULONG i;

        for (i = 0; i < context->RequestQueues; i++)
        {
            lock = &context->VirtQueueLocks[i];

            WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
            attributes.ParentObject = Device;

            status = WdfSpinLockCreate(&attributes, lock);
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER,
                    "WdfSpinLockCreate failed");
                break;
            }
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER,
            "Failed to allocate queue locks");

        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER,
        "<-- %!FUNC! Status: %!STATUS!", status);

    return status;
}

NTSTATUS VirtFsEvtDeviceReleaseHardware(IN WDFDEVICE Device,
                                        IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC!");

    PAGED_CODE();

    VirtIOWdfShutdown(&context->VDevice);

    if (context->VirtQueues != NULL)
    {
        ExFreePoolWithTag(context->VirtQueues, VIRT_FS_MEMORY_TAG);
        context->VirtQueues = NULL;
    }

    if (context->VirtQueueLocks != NULL)
    {
        ExFreePoolWithTag(context->VirtQueueLocks, VIRT_FS_MEMORY_TAG);
        context->VirtQueueLocks = NULL;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

NTSTATUS VirtFsEvtDeviceD0Entry(IN WDFDEVICE Device,
                                IN WDF_POWER_DEVICE_STATE PreviousState)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    VIRTIO_WDF_QUEUE_PARAM param;

    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p",
        Device);

    PAGED_CODE();

    param.Interrupt = context->WdfInterrupt;

    status = VirtIOWdfInitQueues(&context->VDevice,
        context->RequestQueues, context->VirtQueues, &param);

    if (NT_SUCCESS(status))
    {
        VirtIOWdfSetDriverOK(&context->VDevice);
    }
    else
    {
        VirtIOWdfSetDriverFailed(&context->VDevice);
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER,
            "VirtIOWdfInitQueues failed with %x", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return status;
}

NTSTATUS VirtFsEvtDeviceD0Exit(IN WDFDEVICE Device,
                               IN WDF_POWER_DEVICE_STATE TargetState)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(TargetState);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p",
        Device);

    PAGED_CODE();

    VirtIOWdfDestroyQueues(&context->VDevice);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}
