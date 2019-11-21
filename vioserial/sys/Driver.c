/*
 * Main driver file containing DriverEntry and driver related functions
 *
 * Copyright (c) 2010-2017 Red Hat, Inc.
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
#include "vioser.h"

#if defined(EVENT_TRACING)
#include "Driver.tmh"
#endif

DRIVER_INITIALIZE DriverEntry;

// Context cleanup callbacks generally run at IRQL <= DISPATCH_LEVEL but
// WDFDRIVER context cleanup is guaranteed to run at PASSIVE_LEVEL.
// Annotate the prototype to make static analysis happy.
EVT_WDF_OBJECT_CONTEXT_CLEANUP _IRQL_requires_(PASSIVE_LEVEL) VIOSerialEvtDriverContextCleanup;

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
    InitializeDebugPrints(DriverObject, RegistryPath);
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
    IN WDFOBJECT Driver
    )
{
    PAGED_CODE ();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--> %s\n", __FUNCTION__);

    WPP_CLEANUP( WdfDriverWdmGetDriverObject((WDFDRIVER)Driver) );
}

