/*
 * Copyright (C) 2015-2017 Red Hat, Inc.
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

#include "pvpanic.h"
#include "power.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, PVPanicEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, PVPanicEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, PVPanicEvtDeviceD0Entry)
#pragma alloc_text(PAGE, PVPanicEvtDeviceD0Exit)
#endif

NTSTATUS PVPanicEvtDevicePrepareHardware(IN WDFDEVICE Device,
                                         IN WDFCMRESLIST Resources,
                                         IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR desc;
    ULONG i;

    UNREFERENCED_PARAMETER(Resources);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p", Device);

    PAGED_CODE();
    bEmitCrashLoadedEvent = FALSE;
    context->MappedPort = FALSE;
    context->IoBaseAddress = NULL;
    context->MemBaseAddress = NULL;

    for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); ++i)
    {
        desc = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        switch (desc->Type)
        {
            case CmResourceTypePort:
                TraceEvents(TRACE_LEVEL_VERBOSE,
                            DBG_POWER,
                            "I/O mapped CSR: (%lx) Length: (%lu)",
                            desc->u.Port.Start.LowPart,
                            desc->u.Port.Length);

                context->MappedPort = !(desc->Flags & CM_RESOURCE_PORT_IO);
                context->IoRange = desc->u.Port.Length;

                if (context->MappedPort)
                {
#if defined(NTDDI_WINTHRESHOLD) && (NTDDI_VERSION >= NTDDI_WINTHRESHOLD)
                    context->IoBaseAddress =
                        MmMapIoSpaceEx(desc->u.Port.Start, desc->u.Port.Length, PAGE_READWRITE | PAGE_NOCACHE);
#else
                    context->IoBaseAddress = MmMapIoSpace(desc->u.Port.Start, desc->u.Port.Length, MmNonCached);
#endif
                }
                else
                {
                    context->IoBaseAddress = (PVOID)(ULONG_PTR)desc->u.Port.Start.QuadPart;
                }
                if (BusType & PVPANIC_PCI)
                {
                    TraceEvents(TRACE_LEVEL_ERROR,
                                DBG_POWER,
                                "The coexistence of ISA and PCI pvpanic device is not supported, so "
                                "the ISA pvpanic driver fails to be loaded because the PCI pvpanic "
                                "driver has been loaded");
                    return STATUS_DEVICE_CONFIGURATION_ERROR;
                }
                else
                {
                    PvPanicPortOrMemAddress = (PUCHAR)context->IoBaseAddress;
                }

                break;

            case CmResourceTypeMemory:
                TraceEvents(TRACE_LEVEL_VERBOSE,
                            DBG_POWER,
                            "Memory mapped CSR: (%lx) Length: (%lu)",
                            desc->u.Memory.Start.LowPart,
                            desc->u.Memory.Length);

                context->MemRange = desc->u.Memory.Length;
#if defined(NTDDI_WINTHRESHOLD) && (NTDDI_VERSION >= NTDDI_WINTHRESHOLD)
                context->MemBaseAddress =
                    MmMapIoSpaceEx(desc->u.Memory.Start, desc->u.Memory.Length, PAGE_READWRITE | PAGE_NOCACHE);
#else
                context->MemBaseAddress = MmMapIoSpace(desc->u.Memory.Start, desc->u.Memory.Length, MmNonCached);
#endif
                if (BusType & PVPANIC_ISA)
                {
                    TraceEvents(TRACE_LEVEL_ERROR,
                                DBG_POWER,
                                "The coexistence of ISA and PCI pvpanic device is not supported, so "
                                "the PCI pvpanic driver fails to be loaded because the ISA pvpanic "
                                "driver has been loaded");
                    return STATUS_DEVICE_CONFIGURATION_ERROR;
                }
                else
                {
                    PvPanicPortOrMemAddress = (PUCHAR)context->MemBaseAddress;
                }

                break;
            default:
                break;
        }
    }

    if (!(context->IoBaseAddress || context->MemBaseAddress))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "Memory or Port not found.");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (context->IoBaseAddress)
    {
        BusType |= PVPANIC_ISA;
        SupportedFeature = READ_PORT_UCHAR((PUCHAR)(context->IoBaseAddress));
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_POWER,
                    "read feature from IoBaseAddress 0x%p SupportedFeature 0x%x \n",
                    context->IoBaseAddress,
                    SupportedFeature);
    }
    else if (context->MemBaseAddress)
    {
        BusType |= PVPANIC_PCI;
        SupportedFeature = *(PUCHAR)(context->MemBaseAddress);
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_POWER,
                    "read feature 0x%p *MemBaseAddress 0x%x SupportedFeature 0x%x \n",
                    context->MemBaseAddress,
                    *(PUSHORT)(context->MemBaseAddress),
                    SupportedFeature);
    }
    if (SupportedFeature & (PVPANIC_PANICKED | PVPANIC_CRASHLOADED))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_POWER,
                    "PVPANIC_PANICKED notification feature %s supported.",
                    (SupportedFeature & PVPANIC_PANICKED) ? "is" : "is not");
        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_POWER,
                    "PVPANIC_CRASHLOADED notification feature %s supported.",
                    (SupportedFeature & PVPANIC_CRASHLOADED) ? "is" : "is not");
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "Panic notification feature is not supported.");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

NTSTATUS PVPanicEvtDeviceReleaseHardware(IN WDFDEVICE Device, IN WDFCMRESLIST ResourcesTranslated)
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
    if (context->MemBaseAddress)
    {
        MmUnmapIoSpace(context->MemBaseAddress, context->MemRange);
        context->MemBaseAddress = NULL;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

NTSTATUS PVPanicEvtDeviceD0Entry(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE PreviousState)
{
    PDEVICE_CONTEXT context = GetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p", Device);

    PAGED_CODE();

    if (context->IoBaseAddress)
    {
        PVPanicRegisterBugCheckCallback(context->IoBaseAddress, (PUCHAR)("PVPanic"));
    }
    if (context->MemBaseAddress)
    {
        PVPanicRegisterBugCheckCallback(context->MemBaseAddress, (PUCHAR)("PVPanic-PCI"));
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

NTSTATUS PVPanicEvtDeviceD0Exit(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(TargetState);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p", Device);

    PAGED_CODE();

    PVPanicDeregisterBugCheckCallback();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}
