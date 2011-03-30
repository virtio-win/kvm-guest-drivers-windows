/**********************************************************************
 * Copyright (c) 2010  Red Hat, Inc.
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

// Global debug printout level and enable\disable flag
int nDebugLevel;
int bDebugPrint;


#if !defined(EVENT_TRACING)
ULONG DebugLevel = TRACE_LEVEL_INFORMATION;
ULONG DebugFlag = 0xff;
#else
ULONG DebugLevel;
ULONG DebugFlag;
#endif


void InitializeDebugPrints(PUNICODE_STRING RegistryPath)
{
    //TBD - Read nDebugLevel and bDebugPrint from the registry
    bDebugPrint = 1;
    nDebugLevel = 0;
}


NTSTATUS DriverEntry(IN PDRIVER_OBJECT  DriverObject,
					 IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS               status = STATUS_SUCCESS;
    WDF_DRIVER_CONFIG      config;
    WDF_OBJECT_ATTRIBUTES  attributes;

    WPP_INIT_TRACING(DriverObject, RegistryPath);

    InitializeDebugPrints(RegistryPath);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, 
        "Virtio-Serial driver started...built on %s %s\n", __DATE__, __TIME__);

    WDF_DRIVER_CONFIG_INIT(&config,VIOSerialEvtDeviceAdd);
    config.DriverPoolTag  = VIOSERIAL_DRIVER_MEMORY_TAG;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = VIOSerialEvtDriverContextCleanup;

    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,
                             &config,
                             WDF_NO_HANDLE);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
           "WdfDriverCreate failed - 0x%x\n", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    return status;
}

VOID
VIOSerialEvtDriverContextCleanup(
    IN WDFDRIVER Driver
    )
{
    PDRIVER_OBJECT  drvObj = WdfDriverWdmGetDriverObject( Driver );
    PAGED_CODE ();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--> %s\n", __FUNCTION__);
  
    WPP_CLEANUP( drvObj);
}

#if DBG
#define     TEMP_BUFFER_SIZE        512

#if COM_DEBUG

#define RHEL_DEBUG_PORT     ((PUCHAR)0x3F8)
ULONG
_cdecl
DbgPrintToComPort(
    __in LPTSTR Format,
    ...
    )
{   

    NTSTATUS   status;
    size_t     rc;

    status = RtlStringCbLengthA(Format, TEMP_BUFFER_SIZE, &rc); 
 
    if(NT_SUCCESS(status)) {
        WRITE_PORT_BUFFER_UCHAR(RHEL_DEBUG_PORT, (PUCHAR)Format, rc);
        WRITE_PORT_UCHAR(RHEL_DEBUG_PORT, '\r');
    } else {
        WRITE_PORT_UCHAR(RHEL_DEBUG_PORT, 'O');
        WRITE_PORT_UCHAR(RHEL_DEBUG_PORT, '\n');
    }
    return rc;
}
#endif COM_DEBUG
#endif DBG

#if DBG
#if COM_DEBUG
#define VioSerDbgPrint(__MSG__) DbgPrintToComPort __MSG__;
#else
#define VioSerDbgPrint(__MSG__) DbgPrint __MSG__;
#endif COM_DEBUG
#else DBG
#define VioSerDbgPrint(__MSG__) 
#endif DBG


#if !defined(EVENT_TRACING)
VOID
TraceEvents    (
    IN ULONG   TraceEventsLevel,
    IN ULONG   TraceEventsFlag,
    IN PCCHAR  DebugMessage,
    ...
    )
 {
#if DBG
    va_list    list;
    CHAR       debugMessageBuffer[TEMP_BUFFER_SIZE];
    NTSTATUS   status;

    va_start(list, DebugMessage);

    if (DebugMessage) {
        status = RtlStringCbVPrintfA( debugMessageBuffer,
                                      sizeof(debugMessageBuffer),
                                      DebugMessage,
                                      list );
        if(!NT_SUCCESS(status)) {

            VioSerDbgPrint (("VioSerial: RtlStringCbVPrintfA failed 0x%08x\n",
                      status));
            return;
        }
        if (TraceEventsLevel <= TRACE_LEVEL_INFORMATION ||
            (TraceEventsLevel <= DebugLevel &&
             ((TraceEventsFlag & DebugFlag) == TraceEventsFlag))) {
            VioSerDbgPrint((debugMessageBuffer));
        }
    }
    va_end(list);

    return;
#else
    UNREFERENCED_PARAMETER(TraceEventsLevel);
    UNREFERENCED_PARAMETER(TraceEventsFlag);
    UNREFERENCED_PARAMETER(DebugMessage);
#endif
}
#endif



