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
#include "pvpanic.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, PVPanicEvtDeviceAdd)
#pragma alloc_text(PAGE, PVPanicEvtDriverContextCleanup)
#endif

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject,
                     IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    // Initialize WPP tracing.
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %!FUNC!");

    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = PVPanicEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, PVPanicEvtDeviceAdd);

    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes,
        &config, WDF_NO_HANDLE);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfDriverCreate failed: %!STATUS!", status);
        WPP_CLEANUP(DriverObject);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- %!FUNC!");
    }

    return status;
}

NTSTATUS PVPanicEvtDeviceAdd(IN WDFDRIVER Driver,
                             IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
	WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_OBJECT_ATTRIBUTES attributes;

    UNREFERENCED_PARAMETER(Driver);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %!FUNC!");

    PAGED_CODE();

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

    pnpPowerCallbacks.EvtDevicePrepareHardware = PVPanicEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = PVPanicEvtDeviceReleaseHardware;
	pnpPowerCallbacks.EvtDeviceD0Entry = PVPanicEvtDeviceD0Entry;
	pnpPowerCallbacks.EvtDeviceD0Exit = PVPanicEvtDeviceD0Exit;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);

	WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, PVPanicEvtDeviceFileCreate,
		WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK);

	WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig,
		WDF_NO_OBJECT_ATTRIBUTES);

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfDeviceCreate failed: %!STATUS!", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- %!FUNC!");

    return status;
}

VOID PVPanicEvtDriverContextCleanup(IN WDFOBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-> %!FUNC!");

    PAGED_CODE();

    // Stop WPP tracing.
    WPP_CLEANUP(WdfDriverWdmGetDriverObject(DriverObject));
}

VOID PVPanicEvtDeviceFileCreate(IN WDFDEVICE Device,
								IN WDFREQUEST Request,
								IN WDFFILEOBJECT FileObject)
{
	UNREFERENCED_PARAMETER(Device);
	UNREFERENCED_PARAMETER(FileObject);

	WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
}
