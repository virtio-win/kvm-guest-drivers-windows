/*
 * Copyright (C) 2015-2016 Red Hat, Inc.
 *
 * This software is licensed under the GNU General Public License,
 * version 2 (GPLv2) (see COPYING for details), subject to the following
 * clarification.
 *
 * With respect to binaries built using the Microsoft(R) Windows Driver
 * Kit (WDK), GPLv2 does not extend to any code contained in or derived
 * from the WDK ("WDK Code"). As to WDK Code, by using or distributing
 * such binaries you agree to be bound by the Microsoft Software License
 * Terms for the WDK. All WDK Code is considered by the GPLv2 licensors
 * to qualify for the special exception stated in section 3 of GPLv2
 * (commonly known as the system library exception).
 *
 * There is NO WARRANTY for this software, express or implied,
 * including the implied warranties of NON-INFRINGEMENT, TITLE,
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Refer to the LICENSE file for full details of the license.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
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
	UCHAR features;
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

            default:
                break;
        }
    }

    if (!context->IoBaseAddress)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, "Port not found.");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

	features = READ_PORT_UCHAR((PUCHAR)(context->IoBaseAddress));
	if ((features & PVPANIC_PANICKED) != PVPANIC_PANICKED)
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER, 
			"Panic notification feature is not supported.");
		return STATUS_DEVICE_CONFIGURATION_ERROR;
	}

	TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

    return STATUS_SUCCESS;
}

NTSTATUS PVPanicEvtDeviceReleaseHardware(IN WDFDEVICE Device,
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

NTSTATUS PVPanicEvtDeviceD0Entry(IN WDFDEVICE Device,
								 IN WDF_POWER_DEVICE_STATE PreviousState)
{
	PDEVICE_CONTEXT context = GetDeviceContext(Device);

	UNREFERENCED_PARAMETER(PreviousState);

	TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p",
		Device);

	PAGED_CODE();

	if (PVPanicRegisterBugCheckCallback(context->IoBaseAddress) == FALSE)
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER,
			"Failed to register bug check callback function.");
	}

	TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

	return STATUS_SUCCESS;
}

NTSTATUS PVPanicEvtDeviceD0Exit(IN WDFDEVICE Device,
								IN WDF_POWER_DEVICE_STATE TargetState)
{
	UNREFERENCED_PARAMETER(Device);
	UNREFERENCED_PARAMETER(TargetState);

	TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "--> %!FUNC! Device: %p",
		Device);

	PAGED_CODE();

	if (PVPanicDeregisterBugCheckCallback() == FALSE)
	{
		TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER,
			"Failed to unregister bug check callback function.");
	}

	TraceEvents(TRACE_LEVEL_VERBOSE, DBG_POWER, "<-- %!FUNC!");

	return STATUS_SUCCESS;
}
