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

static struct virtqueue* FindVirtualQueue(VirtIODevice *VirtDevice,
                                          ULONG Index,
                                          USHORT Vector)
{
    struct virtqueue *vq = NULL;
    ULONG entries, size;

    VirtIODeviceQueryQueueAllocation(VirtDevice, Index, &entries, &size);
    if (size)
    {
        PHYSICAL_ADDRESS HighestAcceptable;
        PVOID p;

        HighestAcceptable.QuadPart = 0xFFFFFFFFFF;
        p = MmAllocateContiguousMemory(size, HighestAcceptable);
        if (p)
        {
            vq = VirtIODevicePrepareQueue(VirtDevice, Index,
                MmGetPhysicalAddress(p), p, size, p, FALSE);
        }
    }

    if (vq && (Vector != VIRTIO_MSI_NO_VECTOR))
    {
        WriteVirtIODeviceWord(VirtDevice->addr + VIRTIO_MSI_QUEUE_VECTOR, Vector);
        Vector = ReadVirtIODeviceWord(VirtDevice->addr + VIRTIO_MSI_QUEUE_VECTOR);
    }

    return vq;
}

static VOID DeleteQueue(struct virtqueue **pvq)
{
    struct virtqueue *vq = *pvq;

    if (vq)
    {
        PVOID p;

        VirtIODeviceDeleteQueue(vq, &p);
        *pvq = NULL;
        MmFreeContiguousMemory(p);
    }
}

NTSTATUS VirtRngEvtDevicePrepareHardware(IN WDFDEVICE Device,
                                         IN WDFCMRESLIST Resources,
                                         IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR desc;
    BOOLEAN signaled = FALSE;
    ULONG i;

    UNREFERENCED_PARAMETER(Resources);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p",
        Device);

    PAGED_CODE();

    for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); ++i)
    {
        desc = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        switch (desc->Type)
        {
            case CmResourceTypePort:
            {
                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER,
                    "I/O mapped CSR: (%x) Length: (%d)",
                    desc->u.Port.Start.LowPart, desc->u.Port.Length);

                context->MappedPort = !(desc->Flags & CM_RESOURCE_PORT_IO);
                context->IoRange = desc->u.Port.Length;

                if (context->MappedPort)
                {
                    context->IoBaseAddress = MmMapIoSpace(desc->u.Port.Start,
                        desc->u.Port.Length, MmNonCached);
                }
                else
                {
                    context->IoBaseAddress =
                        (PVOID)(ULONG_PTR)desc->u.Port.Start.QuadPart;
                }

                break;
            }

            case CmResourceTypeInterrupt:
            {
                signaled = !!(desc->Flags &
                    (CM_RESOURCE_INTERRUPT_LATCHED | CM_RESOURCE_INTERRUPT_MESSAGE));

                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER,
                    "Interrupt Level: 0x%08x, Vector: 0x%08x Signaled: %!BOOLEAN!",
                    desc->u.Interrupt.Level, desc->u.Interrupt.Vector, signaled);

                break;
            }

            default:
                break;
        }
    }

    if (!context->IoBaseAddress)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "Port not found.");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    VirtIODeviceInitialize(&context->VirtDevice,
        (ULONG_PTR)(context->IoBaseAddress), VirtIODeviceSizeRequired(1));
    VirtIODeviceSetMSIXUsed(&context->VirtDevice, signaled);
    VirtIODeviceReset(&context->VirtDevice);

    if (signaled)
    {
        WriteVirtIODeviceWord(
            context->VirtDevice.addr + VIRTIO_MSI_CONFIG_VECTOR, 1);
        (VOID)ReadVirtIODeviceWord(
            context->VirtDevice.addr + VIRTIO_MSI_CONFIG_VECTOR);
    }

    VirtIODeviceAddStatus(&context->VirtDevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

NTSTATUS VirtRngEvtDeviceReleaseHardware(IN WDFDEVICE Device,
                                         IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC!");

    PAGED_CODE();

    if (context->MappedPort && context->IoBaseAddress)
    {
        MmUnmapIoSpace(context->IoBaseAddress, context->IoRange);
        context->IoBaseAddress = NULL;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

NTSTATUS VirtRngEvtDeviceD0Entry(IN WDFDEVICE Device,
                                 IN WDF_POWER_DEVICE_STATE PreviousState)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    WDF_INTERRUPT_INFO info;
    USHORT vector;

    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p",
        Device);

    PAGED_CODE();

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(context->WdfInterrupt, &info);
    vector = info.MessageSignaled ? 0 : VIRTIO_MSI_NO_VECTOR;

    context->VirtQueue = FindVirtualQueue(&context->VirtDevice, 0, vector);
    if (context->VirtQueue)
    {
        VirtIODeviceAddStatus(&context->VirtDevice, VIRTIO_CONFIG_S_DRIVER_OK);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "Failed to find queue!");
        status = STATUS_NOT_FOUND;
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

    VirtIODeviceRemoveStatus(&context->VirtDevice, VIRTIO_CONFIG_S_DRIVER_OK);
    DeleteQueue(&context->VirtQueue);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}
