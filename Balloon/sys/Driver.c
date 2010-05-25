/**********************************************************************
 * Copyright (c) 2009  Red Hat, Inc.
 *
 * File: driver.c
 * 
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This file contains balloon driver routines 
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "precomp.h"

#if defined(EVENT_TRACING)
#include "Driver.tmh"
#endif

#if !defined(EVENT_TRACING)
ULONG DebugLevel = TRACE_LEVEL_INFORMATION;
ULONG DebugFlag = 0x2f;
#else
ULONG DebugLevel;
ULONG DebugFlag;
#endif


#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, EvtDriverContextCleanup)

NTSTATUS DriverEntry(
                      IN PDRIVER_OBJECT   DriverObject, 
                      IN PUNICODE_STRING  RegistryPath
                      )
{
    WDF_DRIVER_CONFIG      config;
    NTSTATUS               status;
    WDFDRIVER              driver;
    WDF_OBJECT_ATTRIBUTES  attrib;
    PDRIVER_CONTEXT        drvCxt;

    WPP_INIT_TRACING( DriverObject, RegistryPath );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"--> %s\n", __FUNCTION__);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Balloon driver, built on %s %s\n",
            __DATE__, __TIME__);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrib, DRIVER_CONTEXT);
    attrib.EvtCleanupCallback = EvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, BalloonDeviceAdd);

    status =  WdfDriverCreate(
                      DriverObject,
                      RegistryPath,
                      &attrib,
                      &config,
                      &driver);
    if(!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"WdfDriverCreate failed with status 0x%08x\n", status);
        WPP_CLEANUP(DriverObject);
    }

    drvCxt = GetDriverContext(driver);

    drvCxt->num_pages = 0;
    drvCxt->PageListHead.Next = NULL;
    ExInitializeNPagedLookasideList(
                      &drvCxt->LookAsideList,
                      NULL,
                      NULL,
                      0,
                      sizeof(PAGE_LIST_ENTRY),
                      BALLOON_MGMT_POOL_TAG,
                      0
                      );

    drvCxt->pfns_table = 
              ExAllocatePoolWithTag(
                      NonPagedPool,
                      PAGE_SIZE,
                      BALLOON_MGMT_POOL_TAG
                      );

    if(drvCxt->pfns_table == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"ExAllocatePoolWithTag failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        WPP_CLEANUP(DriverObject);
        return status;
    }
  
    drvCxt->MemStats = 
              ExAllocatePoolWithTag(
                      NonPagedPool,
                      sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR,
                      BALLOON_MGMT_POOL_TAG
                      );

    if(drvCxt->MemStats == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"ExAllocatePoolWithTag failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        WPP_CLEANUP(DriverObject);
        return status;
    }
    RtlFillMemory (drvCxt->MemStats, sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR, -1);
    WDF_OBJECT_ATTRIBUTES_INIT(&attrib);
    attrib.ParentObject = driver;

    status = WdfSpinLockCreate(&attrib, &drvCxt->SpinLock);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,"WdfSpinLockCreate failed 0x%08x\n",status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"<-- %s\n", __FUNCTION__);

    LogError( DriverObject, BALLOON_STARTED);
    return status;
}

VOID
EvtDriverContextCleanup(
    IN WDFDRIVER Driver
    )
{
    PDRIVER_CONTEXT drvCxt = GetDriverContext( Driver );
    PDRIVER_OBJECT  drvObj = WdfDriverWdmGetDriverObject( Driver );
    PAGED_CODE ();
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,"--> %s\n", __FUNCTION__);

    ExDeleteNPagedLookasideList(&drvCxt->LookAsideList);
    ExFreePoolWithTag(
                   drvCxt->pfns_table,
                   BALLOON_MGMT_POOL_TAG
                   );

    ExFreePoolWithTag(
                   drvCxt->MemStats,
                   BALLOON_MGMT_POOL_TAG
                   );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
  
    LogError( drvObj, BALLOON_STOPPED);
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
#define BalloonDbgPrint(__MSG__) DbgPrintToComPort __MSG__;
#else
#define BalloonDbgPrint(__MSG__) DbgPrint __MSG__;
#endif COM_DEBUG
#else DBG
#define BalloonDbgPrint(__MSG__) 
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

            BalloonDbgPrint ((__DRIVER_NAME ": RtlStringCbVPrintfA failed 0x%08x\n",
                      status));
            return;
        }
        if (TraceEventsLevel <= TRACE_LEVEL_INFORMATION ||
            (TraceEventsLevel <= DebugLevel &&
             ((TraceEventsFlag & DebugFlag) == TraceEventsFlag))) {
            BalloonDbgPrint((debugMessageBuffer));
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
