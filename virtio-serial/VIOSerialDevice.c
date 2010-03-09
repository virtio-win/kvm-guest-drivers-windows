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
	NTSTATUS						status = STATUS_SUCCESS;
	WDF_OBJECT_ATTRIBUTES			fdoAttributes;
	WDFDEVICE						hDevice;
	WDF_PNPPOWER_EVENT_CALLBACKS	stPnpPowerCallbacks;
	DECLARE_CONST_UNICODE_STRING(strVIOSerialSymbolicLink, VIOSERIAL_SYMBOLIC_LINK);
	WDF_IO_QUEUE_CONFIG				queueCfg;
	WDF_FILEOBJECT_CONFIG			fileCfg;
	WDFQUEUE						queue;
	
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

// Create file object to handle Open\Close events
	WDF_FILEOBJECT_CONFIG_INIT(&fileCfg,
							   VIOSerialEvtDeviceFileCreate,
							   VIOSerialEvtFileClose,
							   WDF_NO_EVENT_CALLBACK);

	WdfDeviceInitSetFileObjectConfig(DeviceInit,
									 &fileCfg,
									 WDF_NO_OBJECT_ATTRIBUTES);

	if (!NT_SUCCESS(status = WdfDeviceCreate(&DeviceInit, &fdoAttributes, &hDevice)))
	{
		DPrintf(0, ("WdfDeviceCreate failed - 0x%x\n", status));
		return status;
	}

	/*
	TBD - after initial implementation change to raw mode.
The mode of operation in which a device's driver stack does not include a function driver. A device running in raw mode is being controlled primarily by the bus driver. Upper-level, lower-level, and/or bus filter drivers might be included in the driver stack. 
If a bus driver can control a device in raw mode, it sets RawDeviceOK in the DEVICE_CAPABILITIES structure.
	*/

	//Create symbolic link to represent the device in the system
	if (!NT_SUCCESS(status = WdfDeviceCreateSymbolicLink(hDevice, &strVIOSerialSymbolicLink)))
	{
		DPrintf(0, ("WdfDeviceCreateSymbolicLink failed - 0x%x\n", status));
		return status;
	}

	//Create the IO queue to handle IO requests
	// TDB - check if parallel mode is more apropreate
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueCfg, WdfIoQueueDispatchSequential);

	queueCfg.EvtIoDeviceControl = VIOSerialEvtIoDeviceControl;
	queueCfg.EvtIoRead = VIOSerialEvtIoRead;
	queueCfg.EvtIoWrite = VIOSerialEvtIoWrite;

	status = WdfIoQueueCreate(hDevice,
							  &queueCfg,
							  WDF_NO_OBJECT_ATTRIBUTES,
							  &queue
							  );

	if (!NT_SUCCESS (status))
	{
		DPrintf(0, ("WdfIoQueueCreate failed - 0x%x\n", status));
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
	//VSCDeinit(Device);
	
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

VOID VIOSerialEvtIoDeviceControl(IN WDFQUEUE  Queue,
								 IN WDFREQUEST  Request,
								 IN size_t OutputBufferLength,
								 IN size_t InputBufferLength,
								 IN ULONG  IoControlCode)
{
	DPrintFunctionName(0);
/*VOID
  WdfRequestCompleteWithInformation(
    IN WDFREQUEST  Request,
    IN NTSTATUS  Status,
    IN ULONG_PTR  Information
   );*/
}

VOID VIOSerialEvtIoRead(IN WDFQUEUE  Queue,
						IN WDFREQUEST Request,
						IN size_t Length)
{
	WDFMEMORY outMemory;
	size_t size = Length;
	PVOID buffer = NULL;
	NTSTATUS status;

	DPrintFunctionName(0);

	if(NT_SUCCESS(WdfRequestRetrieveOutputMemory(Request, &outMemory)))
	{
		status = VSCGetData(&outMemory, &size);
	}

	WdfRequestCompleteWithInformation(Request, status, size);
}

VOID VIOSerialEvtIoWrite(IN WDFQUEUE  Queue,
						 IN WDFREQUEST Request,
						 IN size_t Length)
{
	WDFMEMORY inMemory;
	size_t size = 0;
	PVOID buffer = NULL;
	NTSTATUS status;

	DPrintFunctionName(0);
	
	if(NT_SUCCESS(WdfRequestRetrieveInputMemory(Request, &inMemory)))
	{
		buffer = WdfMemoryGetBuffer(inMemory, &size);

		//Just for safety- checking that the buffer size of IRP is not smaller than
		// the size of the embedded memory object
		if(size > Length)
		{
			size = Length;
		}

		status = VSCSendData(buffer, &size);
	}

	WdfRequestCompleteWithInformation(Request, status, size);
}


void VIOSerialEvtDeviceFileCreate(IN WDFDEVICE Device,
								  IN WDFREQUEST Request,
								  IN WDFFILEOBJECT FileObject)
{
	NTSTATUS status;
	DPrintFunctionName(0);

	if(NT_SUCCESS(status = VSCGuestOpenedPort(/* TBD */)))
	{
		//TBD - do some stuff on the device level on file open if needed
	}

	WdfRequestComplete(Request, status);
}

VOID VIOSerialEvtFileClose(IN WDFFILEOBJECT FileObject)
{
	DPrintFunctionName(0);
	//Clean up on file close

	VSCGuestClosedPort(/* TBD */);
}
