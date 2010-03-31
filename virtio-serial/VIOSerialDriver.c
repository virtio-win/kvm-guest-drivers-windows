/**********************************************************************
 * Copyright (c) 2010  Red Hat, Inc.
 *
 * File: VIOSerialDriver.c
 *
 * Main driver file containing DriverEntry and driver related functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"


#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#endif

// Global debug printout level and enable\disable flag
int nDebugLevel;
int bDebugPrint;

void InitializeDebugPrints(PUNICODE_STRING RegistryPath)
{
	//TBD - Read nDebugLevel and bDebugPrint from the registry
	bDebugPrint = 1;
	nDebugLevel = 0;
}


NTSTATUS DriverEntry(IN PDRIVER_OBJECT  DriverObject,
					 IN PUNICODE_STRING RegistryPath)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG configWDF;

	InitializeDebugPrints(RegistryPath);
	DPrintf(0, ("Virtio-Serial driver started...\n"));

	WDF_DRIVER_CONFIG_INIT(&configWDF,VIOSerialEvtDeviceAdd);
	configWDF.DriverPoolTag  = VIOSERIAL_DRIVER_MEMORY_TAG;

	// Create driver object
	if(!NT_SUCCESS(status = WdfDriverCreate(DriverObject,
											RegistryPath,
											WDF_NO_OBJECT_ATTRIBUTES,
											&configWDF,
											WDF_NO_HANDLE)))
	{
		DPrintf(0, ("WdfDriverCreate failed - 0x%x\n", status));
	}

	return status;
}
