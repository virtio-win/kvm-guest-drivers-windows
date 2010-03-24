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
#include "VIOSerialDevice.h"
#include "VIOSerialCore.h"


// Break huge add device into chunks
static void VIOSerialInitPowerManagement(IN PWDFDEVICE_INIT DeviceInit,
										 IN WDF_PNPPOWER_EVENT_CALLBACKS *stPnpPowerCallbacks);
static void VIOSerialInitFileObject(IN PWDFDEVICE_INIT DeviceInit,
									WDF_FILEOBJECT_CONFIG * pFileCfg);
static NTSTATUS VIOSerialInitIO(WDFDEVICE hDevice);
static void VIOSerialInitDeviceContext(WDFDEVICE hDevice);
static NTSTATUS VIOSerialInitInterruptHandling(WDFDEVICE hDevice);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSerialEvtDeviceAdd)
#pragma alloc_text (PAGE, VIOSerialEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, VIOSerialEvtDeviceReleaseHardware)

#pragma alloc_text (PAGE, VIOSerialInitPowerManagement)
#pragma alloc_text (PAGE, VIOSerialInitFileObject)
#pragma alloc_text (PAGE, VIOSerialInitIO)
#pragma alloc_text (PAGE, VIOSerialInitDeviceContext)

//#pragma alloc_text (PAGE, VIOSerialEvtIoRead)
//#pragma alloc_text (PAGE, VIOSerialEvtIoWrite)
//#pragma alloc_text (PAGE, VIOSerialEvtIoDeviceControl)
#endif


static void VIOSerialInitPowerManagement(IN PWDFDEVICE_INIT DeviceInit,
										 IN WDF_PNPPOWER_EVENT_CALLBACKS *stPnpPowerCallbacks)
{
	PAGED_CODE();
	DEBUG_ENTRY(0);

	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(stPnpPowerCallbacks);
	stPnpPowerCallbacks->EvtDevicePrepareHardware = VIOSerialEvtDevicePrepareHardware;
	stPnpPowerCallbacks->EvtDeviceReleaseHardware = VIOSerialEvtDeviceReleaseHardware;
	stPnpPowerCallbacks->EvtDeviceD0Entry = VIOSerialEvtDeviceD0Entry;
	stPnpPowerCallbacks->EvtDeviceD0Exit = VIOSerialEvtDeviceD0Exit;

	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, stPnpPowerCallbacks);
}

static void VIOSerialInitFileObject(IN PWDFDEVICE_INIT DeviceInit,
									WDF_FILEOBJECT_CONFIG * pFileCfg)
{
	PAGED_CODE();
	DEBUG_ENTRY(0);

// Create file object to handle Open\Close events
	WDF_FILEOBJECT_CONFIG_INIT(pFileCfg,
							   VIOSerialEvtDeviceFileCreate,
							   VIOSerialEvtFileClose,
							   WDF_NO_EVENT_CALLBACK);

	WdfDeviceInitSetFileObjectConfig(DeviceInit,
									 pFileCfg,
									 WDF_NO_OBJECT_ATTRIBUTES);

}

static NTSTATUS VIOSerialInitIO(WDFDEVICE hDevice)
{
	WDF_IO_QUEUE_CONFIG		queueCfg;
	NTSTATUS				status;
	WDFQUEUE				queue;
	DECLARE_CONST_UNICODE_STRING(strVIOSerialSymbolicLink, VIOSERIAL_SYMBOLIC_LINK);

	PAGED_CODE();
	DEBUG_ENTRY(0);

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

	return STATUS_SUCCESS;
}

static void VIOSerialInitDeviceContext(WDFDEVICE hDevice)
{
	PDEVICE_CONTEXT	pContext;
	int i;

	PAGED_CODE();
	DEBUG_ENTRY(0);

	pContext = GetDeviceContext(hDevice);

	if(pContext)
	{
		memset(pContext, 0, sizeof(DEVICE_CONTEXT));
		//Init Spin locks
		WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES,
						  &pContext->DPCLock);

		for(i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i++)
		{
			InitializeListHead(&pContext->SerialPorts[i].ReceiveBuffers);
			InitializeListHead(&pContext->SerialPorts[i].SendFreeBuffers);
			InitializeListHead(&pContext->SerialPorts[i].SendInUseBuffers);
		}
	}
}

static NTSTATUS VIOSerialInitInterruptHandling(WDFDEVICE hDevice)
{
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_INTERRUPT_CONFIG interruptConfig;
	PDEVICE_CONTEXT	pContext = GetDeviceContext(hDevice);
	NTSTATUS status = STATUS_SUCCESS;

	DEBUG_ENTRY(0);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
	WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
							  VIOSerialInterruptIsr,
							  VIOSerialInterruptDpc);

	interruptConfig.EvtInterruptEnable = VIOSerialInterruptEnable;
	interruptConfig.EvtInterruptDisable = VIOSerialInterruptDisable;

	status = WdfInterruptCreate(hDevice,
								&interruptConfig,
								&attributes,
								&pContext->WdfInterrupt);

	if (!NT_SUCCESS (status))
	{
		DPrintf(0, ("WdfInterruptCreate failed: %x\n", status));
		return status;
	}

	return status;
}

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
	WDF_FILEOBJECT_CONFIG			fileCfg;
	
	UNREFERENCED_PARAMETER(Driver);
	PAGED_CODE();
	DEBUG_ENTRY(0);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fdoAttributes, DEVICE_CONTEXT);
	VIOSerialInitPowerManagement(DeviceInit, &stPnpPowerCallbacks);
	VIOSerialInitFileObject(DeviceInit, &fileCfg);

	if (!NT_SUCCESS(status = WdfDeviceCreate(&DeviceInit, &fdoAttributes, &hDevice)))
	{
		DPrintf(0, ("WdfDeviceCreate failed - 0x%x\n", status));
		return status;
	}

	VIOSerialInitDeviceContext(hDevice);

	if(!NT_SUCCESS(status = VIOSerialInitIO(hDevice)))
	{
		return status;
	}

	if(!NT_SUCCESS(status = VIOSerialInitInterruptHandling(hDevice)))
	{
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
	int nListSize = 0;
	PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDescriptor;
	int i = 0;
	PDEVICE_CONTEXT pContext = GetDeviceContext(Device);
	bool bPortFound = FALSE;
	NTSTATUS status = STATUS_SUCCESS;

	DEBUG_ENTRY(0);

	nListSize = WdfCmResourceListGetCount(ResourcesTranslated);

	for (i = 0; i < nListSize; i++)
	{
		if(pResDescriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i))
		{
			switch(pResDescriptor->Type)
			{
				case CmResourceTypePort:
					pContext->bPortMapped =
							(pResDescriptor->Flags & CM_RESOURCE_PORT_IO) ? FALSE : TRUE;

					pContext->PortBasePA = pResDescriptor->u.Port.Start;
					pContext->uPortLength = pResDescriptor->u.Port.Length;

					DPrintf(0, ("IO Port Info  [%08I64X-%08I64X]",
							pResDescriptor->u.Port.Start.QuadPart,
							pResDescriptor->u.Port.Start.QuadPart +
							pResDescriptor->u.Port.Length));

					if (pContext->bPortMapped ) // Port is IO mapped
					{
						pContext->pPortBase = MmMapIoSpace(pContext->PortBasePA,
														   pContext->uPortLength,
														   MmNonCached);

						if (!pContext->pPortBase) {
							DPrintf(0, ("%s>>> %s", __FUNCTION__, "Failed to map IO port!"));
							return STATUS_INSUFFICIENT_RESOURCES;
						}
					}
					else // Memory mapped port
					{
						pContext->pPortBase = (PVOID)(ULONG_PTR)pContext->PortBasePA.QuadPart;
					}

					bPortFound = TRUE;

					break;
				///
				case CmResourceTypeInterrupt:
					// Print out interrupt info- debugging only
					DPrintf(0, ("Resource Type Interrupt"));
					DPrintf(0, ("Interrupt.Level %x", pResDescriptor->u.Interrupt.Level));
					DPrintf(0, ("Interrupt.Vector %x", pResDescriptor->u.Interrupt.Vector));
					DPrintf(0, ("Interrupt.Affinity %x", pResDescriptor->u.Interrupt.Affinity));

					break;
			}
		}
	}

	if(!bPortFound)
	{
		DPrintf(0, ("%s>>> %s", __FUNCTION__, "IO port wasn't found!"));
		return STATUS_DEVICE_CONFIGURATION_ERROR;
	}

	VSCInit(Device);
	
	pContext->isDeviceInitialized = TRUE;
	VSCGuestSetPortsReady(pContext);

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialEvtDeviceReleaseHardware(IN WDFDEVICE Device,
										   IN WDFCMRESLIST ResourcesTranslated)
{
	PDEVICE_CONTEXT pContext = GetDeviceContext(Device);
	UNREFERENCED_PARAMETER(ResourcesTranslated);
	
	PAGED_CODE();
	
	DEBUG_ENTRY(0);
	
	VSCDeinit(Device);

	pContext->isDeviceInitialized = FALSE;
	
	if (pContext->pPortBase && pContext->bPortMapped) 
	{
		MmUnmapIoSpace(pContext->pPortBase, pContext->uPortLength);
	}

	pContext->pPortBase = (ULONG_PTR)NULL;

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialEvtDeviceD0Entry(IN WDFDEVICE Device, 
								   WDF_POWER_DEVICE_STATE  PreviousState)
{
	DEBUG_ENTRY(0);

	//TBD - "power up" the device

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialEvtDeviceD0Exit(IN WDFDEVICE Device, 
								  IN WDF_POWER_DEVICE_STATE TargetState)
{
	DEBUG_ENTRY(0);

	//TBD - "power down" the device

	return STATUS_SUCCESS;
}

static PDEVICE_CONTEXT GetContextFromQueue(IN WDFQUEUE Queue)
{
	PDEVICE_CONTEXT pContext = GetDeviceContext(WdfIoQueueGetDevice(Queue));

	return pContext;
}

VOID VIOSerialEvtIoDeviceControl(IN WDFQUEUE Queue,
								 IN WDFREQUEST Request,
								 IN size_t OutputBufferLength,
								 IN size_t InputBufferLength,
								 IN ULONG  IoControlCode)
{
	DEBUG_ENTRY(0);

	/* Do we need to handle IOCTLs?*/
	WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID VIOSerialEvtRequestCancel(IN WDFREQUEST Request)
{
	WdfRequestComplete(Request, STATUS_CANCELLED);

	VIOSerialQueueRequest(GetContextFromQueue(WdfRequestGetIoQueue(Request)),
						  WdfRequestGetFileObject(Request),
						  NULL);

	return;
}

VOID VIOSerialEvtIoRead(IN WDFQUEUE  Queue,
						IN WDFREQUEST Request,
						IN size_t Length)
{
	WDFMEMORY outMemory;
	size_t size = Length;
	PVOID buffer = NULL;
	NTSTATUS status;
	PDEVICE_CONTEXT pContext = GetContextFromQueue(Queue);

	DEBUG_ENTRY(0);

	if(NT_SUCCESS(WdfRequestRetrieveOutputMemory(Request, &outMemory)))
	{
		status = VSCGetData(WdfRequestGetFileObject(Request), 
							pContext,
							&outMemory,
							&size);
	}

	WdfRequestCompleteWithInformation(Request, status, size);
/*
	/if(status != STATUS_UNSUCCESSFUL)
	{
		WdfRequestCompleteWithInformation(Request, status, size);
	}
	else  //There was no data to in queue, add
	{
		WdfSpinLockAcquire(pContext->DPCLock);
		if(STATUS_SUCCESS(WdfRequestMarkCancelableEx(Request,
						  VIOSerialEvtRequestCancel))
		{
			VIOSerialQueueRequest(pContext, WdfRequestGetFileObject(Request), Request);
		}
		WdfSpinLockRelease(pContext->DPCLock);
	}

*/
/*

VOID
  EchoEvtTimerFunc(
    IN WDFTIMER  Timer
    )
{
    NTSTATUS  Status;
    WDFREQUEST  Request;
    WDFQUEUE  queue;
    PQUEUE_CONTEXT  queueContext;

    // Retrieve our saved copy of the request handle.
    queue = WdfTimerGetParentObject(Timer);
    queueContext = QueueGetContext(queue);
    Request = queueContext->CurrentRequest;

    // We cannot call WdfRequestUnmarkCancelable
    // after a request completes, so check here to see
    // if EchoEvtRequestCancel cleared our saved
    // request handle. 
    if( Request != NULL ) {
        Status = WdfRequestUnmarkCancelable(Request);
        if(Status != STATUS_CANCELLED) {
            queueContext->CurrentRequest = NULL;
            Status = queueContext->CurrentStatus;
            WdfRequestComplete(
                               Request,
                               Status
                               );
        }
    }

    return;
}
*/

}

VOID VIOSerialEvtIoWrite(IN WDFQUEUE  Queue,
						 IN WDFREQUEST Request,
						 IN size_t Length)
{
	WDFMEMORY inMemory;
	size_t size = 0;
	PVOID buffer = NULL;
	NTSTATUS status;

	DEBUG_ENTRY(0);
	
	if(NT_SUCCESS(WdfRequestRetrieveInputMemory(Request, &inMemory)))
	{
		buffer = WdfMemoryGetBuffer(inMemory, &size);

		//Just for safety- checking that the buffer size of IRP is not smaller than
		// the size of the embedded memory object
		if(size > Length)
		{
			size = Length;
		}

		status = VSCSendData(WdfRequestGetFileObject(Request),
							 GetContextFromQueue(Queue),
							 buffer,
							 &size);
	}

	WdfRequestCompleteWithInformation(Request, status, size);
}


void VIOSerialEvtDeviceFileCreate(IN WDFDEVICE Device,
								  IN WDFREQUEST Request,
								  IN WDFFILEOBJECT FileObject)
{
	NTSTATUS status = STATUS_SUCCESS;
	DEBUG_ENTRY(0);

	if(NT_SUCCESS(status = VSCGuestOpenedPort(FileObject, GetDeviceContext(Device))))
	{
		//TBD - do some stuff on the device level on file open if needed
	}

	WdfRequestComplete(Request, status);
}

VOID VIOSerialEvtFileClose(IN WDFFILEOBJECT FileObject)
{
	DEBUG_ENTRY(0);
	//Clean up on file close

	VSCGuestClosedPort(FileObject, GetContextFromFileObject(FileObject));
}
