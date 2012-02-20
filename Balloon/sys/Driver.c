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

    TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "Balloon driver, built on %s %s\n",
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
        return status;
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

    KeInitializeEvent(&drvCxt->InfEvent,
                      SynchronizationEvent,
                      FALSE
                      );

    KeInitializeEvent(&drvCxt->DefEvent,
                      SynchronizationEvent,
                      FALSE
                      );

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
