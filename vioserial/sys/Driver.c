/**********************************************************************
 * Copyright (c) 2010-2016 Red Hat, Inc.
 *
 * File: VIOSerialDriver.c
 *
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * Main driver file containing DriverEntry and driver related functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "precomp.h"
#include "vioser.h"


#if defined(EVENT_TRACING)
#include "Driver.tmh"
#endif

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VIOSerialEvtDriverContextCleanup;

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, VIOSerialEvtDriverContextCleanup)
#endif



NTSTATUS DriverEntry(IN PDRIVER_OBJECT  DriverObject,
                     IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS               status = STATUS_SUCCESS;
    WDF_DRIVER_CONFIG      config;
    WDF_OBJECT_ATTRIBUTES  attributes;
    WDFDRIVER              Driver;
    PDRIVER_CONTEXT        Context;

#if (NTDDI_VERSION > NTDDI_WIN7)
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
#endif
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
        "Virtio-Serial driver started...built on %s %s\n", __DATE__, __TIME__);

    WDF_DRIVER_CONFIG_INIT(&config,VIOSerialEvtDeviceAdd);
    config.DriverPoolTag  = VIOSERIAL_DRIVER_MEMORY_TAG;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DRIVER_CONTEXT);
    attributes.EvtCleanupCallback = VIOSerialEvtDriverContextCleanup;

    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,
                             &config,
                             &Driver);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
           "WdfDriverCreate failed - 0x%x\n", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    Context = GetDriverContext(Driver);

    // create a lookaside list used for allocating WRITE_BUFFER_ENTRY
    // structures by all devices
    status = WdfLookasideListCreate(WDF_NO_OBJECT_ATTRIBUTES,
                                    sizeof(WRITE_BUFFER_ENTRY),
                                    NonPagedPool,
                                    WDF_NO_OBJECT_ATTRIBUTES,
                                    VIOSERIAL_DRIVER_MEMORY_TAG,
                                    &Context->WriteBufferLookaside);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfLookasideListCreate failed - 0x%x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID
VIOSerialEvtDriverContextCleanup(
    IN WDFDRIVER Driver
    )
{
    PAGED_CODE ();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--> %s\n", __FUNCTION__);

    WPP_CLEANUP( WdfDriverWdmGetDriverObject(Driver) );
}

