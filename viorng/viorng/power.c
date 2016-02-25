/*
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Refer to the LICENSE file for full details of the license.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 */

#include "viorng.h"

#include "power.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VirtRngEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, VirtRngEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, VirtRngEvtDeviceD0Entry)
#pragma alloc_text(PAGE, VirtRngEvtDeviceD0Exit)
#endif

NTSTATUS VirtRngEvtDevicePrepareHardware(IN WDFDEVICE Device,
                                         IN WDFCMRESLIST Resources,
                                         IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Resources);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p",
        Device);

    PAGED_CODE();

    status = VirtIOWdfInitialize(
        &context->VDevice,
        Device,
        ResourcesTranslated,
        NULL,
        VIRT_RNG_MEMORY_TAG,
        1 /* nMaxQueues */);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "VirtIOWdfInitialize failed with %x\n", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return status;
}

NTSTATUS VirtRngEvtDeviceReleaseHardware(IN WDFDEVICE Device,
                                         IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC!");

    PAGED_CODE();

    VirtIOWdfShutdown(&context->VDevice);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

NTSTATUS VirtRngEvtDeviceD0Entry(IN WDFDEVICE Device,
                                 IN WDF_POWER_DEVICE_STATE PreviousState)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    VIRTIO_WDF_QUEUE_PARAM param;

    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p",
        Device);

    PAGED_CODE();

    {
        param.bEnableInterruptSuppression = false;
        param.Interrupt = context->WdfInterrupt;
        param.szName = "requestq";

        status = VirtIOWdfInitQueues(&context->VDevice, 1, &context->VirtQueue, &param);
        if (NT_SUCCESS(status))
        {
            VirtIOWdfSetDriverOK(&context->VDevice);
        }
        else
        {
            VirtIOWdfSetDriverFailed(&context->VDevice);
            TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER,
                "VirtIOWdfInitQueues failed with %x\n", status);
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return status;
}

NTSTATUS VirtRngEvtDeviceD0Exit(IN WDFDEVICE Device,
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
