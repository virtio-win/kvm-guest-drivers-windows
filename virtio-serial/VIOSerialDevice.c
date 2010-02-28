/**********************************************************************
 * Copyright (c) 2010  Red Hat, Inc.
 *
 * File: VIOSerialDevice.c
 *
 * Placeholder for the device related functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSerialEvtDeviceAdd)
//#pragma alloc_text (PAGE, VIOSerialEvtIoRead)
//#pragma alloc_text (PAGE, VIOSerialEvtIoWrite)
//#pragma alloc_text (PAGE, VIOSerialEvtIoDeviceControl)
#endif


/////////////////////////////////////////////////////////////////////////////////
//
// VIOSerialEvtDeviceAdd
//
// Called by WDF framework as a callback for AddDevice from PNP manager.
// New device object instance should be initialized here
//
/////////////////////////////////////////////////////////////////////////////////
NTSTATUS VIOSerialEvtDeviceAdd(IN WDFDRIVER Driver,IN PWDFDEVICE_INIT DeviceInit)
{
	NTSTATUS				status = STATUS_SUCCESS;
	WDF_OBJECT_ATTRIBUTES	fdoAttributes;
	//TBD
	//PVIOSERIAL_FDO			fdoData;
	WDFDEVICE				hDevice;

    WDFQUEUE               queue;
	WDF_IO_QUEUE_CONFIG    queueConfig;

	UNREFERENCED_PARAMETER(Driver);
	PAGED_CODE();

	DPrintFunctionName(0)

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fdoAttributes, VIOSERIAL_FDO);

	if (!NT_SUCCESS(status = WdfDeviceCreate(&DeviceInit, &fdoAttributes, &hDevice)))
	{
		DPrintf(0, ("WdfDeviceCreate failed - 0x%x\n", status));
		return status;
	}

	return status;
}

