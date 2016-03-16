/**********************************************************************
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * File: Driver.c
 *
 * Author(s):
 *
 * Main driver file containing DriverEntry and driver related functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioinput.h"

#if defined(EVENT_TRACING)
#include "Driver.tmh"
#endif

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VIOInputEvtDriverContextCleanup;

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, VIOInputEvtDriverContextCleanup)
#endif

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT  DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS              status = STATUS_SUCCESS;
    WDF_DRIVER_CONFIG     config;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDRIVER             driver;

#if (NTDDI_VERSION > NTDDI_WIN7)
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
#endif
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
        "Virtio-input driver started...built on %s %s\n", __DATE__, __TIME__);

    WDF_DRIVER_CONFIG_INIT(&config,VIOInputEvtDeviceAdd);
    config.DriverPoolTag  = VIOINPUT_DRIVER_MEMORY_TAG;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = VIOInputEvtDriverContextCleanup;

    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,
                             &config,
                             &driver);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
           "WdfDriverCreate failed - 0x%x\n", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID
VIOInputEvtDriverContextCleanup(
    IN WDFDRIVER Driver)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--> %s\n", __FUNCTION__);

    WPP_CLEANUP(WdfDriverWdmGetDriverObject(Driver));
}
