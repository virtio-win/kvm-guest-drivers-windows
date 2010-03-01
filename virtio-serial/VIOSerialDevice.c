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
#include "VIOSerialCore.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSerialEvtDeviceAdd)
#pragma alloc_text (PAGE, VIOSerialEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, VIOSerialEvtDeviceReleaseHardware)

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
	//PDEVICE_CONTEXT			pContext;
	WDFDEVICE				hDevice;
	WDF_PNPPOWER_EVENT_CALLBACKS stPnpPowerCallbacks;
	
	UNREFERENCED_PARAMETER(Driver);
	PAGED_CODE();

	DPrintFunctionName(0);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fdoAttributes, DEVICE_CONTEXT);

	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&stPnpPowerCallbacks);
	stPnpPowerCallbacks.EvtDevicePrepareHardware = VIOSerialEvtDevicePrepareHardware;
	stPnpPowerCallbacks.EvtDeviceReleaseHardware = VIOSerialEvtDeviceReleaseHardware;
	stPnpPowerCallbacks.EvtDeviceD0Entry = VIOSerialEvtDeviceD0Entry;
	stPnpPowerCallbacks.EvtDeviceD0Exit = VIOSerialEvtDeviceD0Exit;

	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &stPnpPowerCallbacks);

	if (!NT_SUCCESS(status = WdfDeviceCreate(&DeviceInit, &fdoAttributes, &hDevice)))
	{
		DPrintf(0, ("WdfDeviceCreate failed - 0x%x\n", status));
		return status;
	}

	return status;
}

/////////////////////////////////////////////////////////////////////////////////
//
// VIOSerialEvtDevicePrepareHardware
//
// Init virtio interface for usage
//
/////////////////////////////////////////////////////////////////////////////////
NTSTATUS VIOSerialEvtDevicePrepareHardware(IN WDFDEVICE Device,
										   IN WDFCMRESLIST ResourcesRaw,
										   IN WDFCMRESLIST ResourcesTranslated)
{
	DPrintFunctionName(0);

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialEvtDeviceReleaseHardware(IN WDFDEVICE Device,
										   IN WDFCMRESLIST ResourcesTranslated)
{
	PDEVICE_CONTEXT pContext = NULL;
	UNREFERENCED_PARAMETER(ResourcesTranslated);
	
	PAGED_CODE();
	
	DPrintFunctionName(0);
	
	//TBD - uncomment after initaliation is implemented
	//VIOSerialDeinit(Device);
	
	pContext = GetDeviceContext(Device);
	
	if (pContext ->PortBase) 
	{
		//TBD - unmap the port
	/*	if (pContext->PortMapped) 
		{
			MmUnmapIoSpace(pContext ->PortBase, pContext->PortCount);
		}*/

		pContext->PortBase = (ULONG_PTR)NULL;
	}

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialEvtDeviceD0Entry(IN WDFDEVICE Device, 
								   WDF_POWER_DEVICE_STATE  PreviousState)
{
	DPrintFunctionName(0);

	//TBD - "power up" the device

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialEvtDeviceD0Exit(IN WDFDEVICE Device, 
								  IN WDF_POWER_DEVICE_STATE TargetState)
{
	DPrintFunctionName(0);

	//TBD - "power down" the device

	return STATUS_SUCCESS;
}

