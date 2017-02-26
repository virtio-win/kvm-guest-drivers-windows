/*
 * Copyright (C) 2015-2017 Red Hat, Inc.
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
#include "..\..\Tools\vendor.check.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, PVPanicEvtDeviceAdd)
#pragma alloc_text(PAGE, PVPanicEvtDriverContextCleanup)
#pragma alloc_text(PAGE, PVPanicEvtQueueDeviceControl)
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
    WDF_IO_QUEUE_CONFIG queueConfig;
    PDEVICE_CONTEXT context;
    DECLARE_CONST_UNICODE_STRING(dosDeviceName, PVPANIC_DOS_DEVICE_NAME);

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

    status = WdfDeviceCreateSymbolicLink(device, &dosDeviceName);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfDeviceCreateSymbolicLink failed: %!STATUS!", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = PVPanicEvtQueueDeviceControl;

    context = GetDeviceContext(device);
    status = WdfIoQueueCreate(device,
                              &queueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &context->IoctlQueue);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfIoQueueCreate failed: %!STATUS!", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(device,
                                                  context->IoctlQueue,
                                                  WdfRequestTypeDeviceControl);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfDeviceConfigureRequestDispatching failed: %!STATUS!", status);
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

	WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID PVPanicEvtQueueDeviceControl(IN WDFQUEUE Queue,
                                  IN WDFREQUEST Request,
                                  IN size_t OutputBufferLength,
                                  IN size_t InputBufferLength,
                                  IN ULONG IoControlCode)
{
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG    length = 0;
    PVOID    buffer;
    size_t   buffer_length;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "--> %!FUNC!");

    switch (IoControlCode)
    {
    case IOCTL_GET_CRASH_DUMP_HEADER:
    {
        // Return the full result of KeInitializeCrashDumpHeader.
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &buffer, &buffer_length);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTLS,
                "WdfRequestRetrieveInputBuffer failed: %!STATUS!", status);
            break;
        }

        status = KeInitializeCrashDumpHeader(DUMP_TYPE_FULL,
                                             0, // flags, must be 0
                                             buffer,
                                             (ULONG)buffer_length,
                                             &length);
        if (status == STATUS_INVALID_PARAMETER_4)
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }
    }

    WdfRequestCompleteWithInformation(Request, status, NT_SUCCESS(status) ? length : 0);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTLS, "<-- %!FUNC!");
}
