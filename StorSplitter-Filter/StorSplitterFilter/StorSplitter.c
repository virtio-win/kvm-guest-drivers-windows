/**
 * StorSplitter.c
 * * A WDF Storage Filter Driver that intercepts Read/Write requests.
 * If a request exceeds the maximum transfer size supported by the underlying
 * hardware (queried dynamically via SCSI VPD), the driver splits the request
 * into smaller, safe-sized chunks, dispatches them asynchronously, and
 * reconstructs the final completion status for the OS.
 */

#include <ntddk.h>
#include <wdf.h>
#include <ntddscsi.h>
#include <scsi.h>
#include <ntddstor.h> 
#include <ntdddisk.h> // Required for DISK_GEOMETRY and IOCTL_DISK_GET_DRIVE_GEOMETRY

#include <wdmsec.h>
#pragma comment(lib, "wdmsec.lib")

 // Default fallback size (63 pages = ~252KB) if hardware querying fails
#define DEFAULT_MAX_TRANSFER_SIZE (63 * PAGE_SIZE)

// Master Switch: 1 = Enabled (Default), 0 = Disabled system-wide.
// Can be toggled dynamically from user-mode via IOCTLs.
LONG g_SplitterEnabled = 1;

// Point this to the header that is shared between user and kernel
#include "../StorSplitterFilter/SharedIoctls.h"

// Standard SCSI VPD Page 0xB0 (Block Limits)
// Used to ask the storage controller exactly how much data it can handle at once.
#pragma pack(push, 1)
typedef struct _CUSTOM_VPD_BLOCK_LIMITS_PAGE
{
	UCHAR DeviceType : 5;
	UCHAR PeripheralQualifier : 3;
	UCHAR PageCode;             // Will be 0xB0
	USHORT PageLength;
	UCHAR WSNZ;
	UCHAR MaxCompareAndWriteLength;
	USHORT OptimalTransferLengthGranularity;
	ULONG MaximumTransferLength; // Max hardware transfer size in Blocks/Sectors
	ULONG OptimalTransferLength;
	ULONG MaximumPrefetchLength;
} CUSTOM_VPD_BLOCK_LIMITS_PAGE, * PCUSTOM_VPD_BLOCK_LIMITS_PAGE;
#pragma pack(pop)

// Attached to the WDFDEVICE. Holds state specific to the physical disk we are filtering.
typedef struct _FILTER_DEVICE_CONTEXT
{
	size_t MaxSafeTransferSizeBytes; // Calculated max size we are allowed to send
	ULONG SectorSize; // Added to track logical sector size dynamically
} FILTER_DEVICE_CONTEXT, * PFILTER_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILTER_DEVICE_CONTEXT, GetDeviceContext)

// Attached to each large WDFREQUEST (the Parent). Tracks the progress of the split pieces.
typedef struct _PARENT_REQUEST_CONTEXT
{
	LONG OutstandingChildren;  // Reference counter: how many pieces are still in flight?
	NTSTATUS FinalStatus;      // Holds the merged result (if any child fails, this records the failure)
	WDFSPINLOCK Lock;          // Thread-safe lock to protect FinalStatus overwrites from concurrent callbacks
} PARENT_REQUEST_CONTEXT, * PPARENT_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PARENT_REQUEST_CONTEXT, GetParentContext)

// Forward declarations of WDF Event Callbacks
EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE EvtDevicePrepareHardware;
EVT_WDF_IO_QUEUE_IO_READ EvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE EvtIoWrite;
EVT_WDF_REQUEST_COMPLETION_ROUTINE EvtChildRequestCompleted;

// Handles IOCTLs sent from user-mode applications (e.g., a GUI or CLI tool)
VOID EvtControlDeviceIoControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength, ULONG IoControlCode)
{
	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(InputBufferLength);

	NTSTATUS status = STATUS_SUCCESS;
	size_t bytesReturned = 0;

	switch (IoControlCode)
	{
	case IOCTL_SPLITTER_ENABLE:
		InterlockedExchange(&g_SplitterEnabled, 1);
		DbgPrint("[STOR-SPLIT] Splitter is now ENABLED system-wide.\n");
		break;

	case IOCTL_SPLITTER_DISABLE:
		InterlockedExchange(&g_SplitterEnabled, 0);
		DbgPrint("[STOR-SPLIT] Splitter is now DISABLED system-wide.\n");
		break;

	case IOCTL_SPLITTER_QUERY_STATUS:
		if (OutputBufferLength < sizeof(LONG))
		{
			status = STATUS_BUFFER_TOO_SMALL;
		}
		else
		{
			LONG* outBuffer = NULL;
			// Retrieve the memory buffer provided by the user-mode app
			status = WdfRequestRetrieveOutputBuffer(Request, sizeof(LONG), (PVOID*)&outBuffer, NULL);
			if (NT_SUCCESS(status))
			{
				*outBuffer = InterlockedCompareExchange(&g_SplitterEnabled, 0, 0);
				bytesReturned = sizeof(LONG);
			}
		}
		break;

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}

// Creates a standalone "Control Device" side-by-side with the filter.
// This allows user-mode apps to communicate with the driver even if the storage volumes are locked.
NTSTATUS CreateControlDevice(WDFDRIVER Driver)
{
	// SDDL string restricts access: SYSTEM and Built-in Administrators get Full Access (GA)
	DECLARE_CONST_UNICODE_STRING(sddlString, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");

	PWDFDEVICE_INIT deviceInit = WdfControlDeviceInitAllocate(Driver, &sddlString);
	if (deviceInit == NULL) return STATUS_INSUFFICIENT_RESOURCES;

	WdfDeviceInitSetExclusive(deviceInit, TRUE);

	// The names required for user-mode to open a handle
	DECLARE_CONST_UNICODE_STRING(ntDeviceName, L"\\Device\\StorSplitterCtrl");
	DECLARE_CONST_UNICODE_STRING(symbolicLinkName, L"\\DosDevices\\StorSplitterCtrl");

	NTSTATUS status = WdfDeviceInitAssignName(deviceInit, &ntDeviceName);
	if (!NT_SUCCESS(status))
	{
		WdfDeviceInitFree(deviceInit);
		return status;
	}

	WDFDEVICE controlDevice;
	status = WdfDeviceCreate(&deviceInit, WDF_NO_OBJECT_ATTRIBUTES, &controlDevice);
	if (!NT_SUCCESS(status))
	{
		WdfDeviceInitFree(deviceInit);
		return status;
	}

	status = WdfDeviceCreateSymbolicLink(controlDevice, &symbolicLinkName);
	if (!NT_SUCCESS(status))
	{
		WdfObjectDelete(controlDevice);
		return status;
	}

	// Set up a sequential queue purely for handling user-mode IOCTLs
	WDF_IO_QUEUE_CONFIG ioQueueConfig;
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchSequential);
	ioQueueConfig.EvtIoDeviceControl = EvtControlDeviceIoControl;

	status = WdfIoQueueCreate(controlDevice, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
	if (!NT_SUCCESS(status))
	{
		WdfObjectDelete(controlDevice);
		return status;
	}

	WdfControlFinishInitializing(controlDevice);
	return STATUS_SUCCESS;
}

// The absolute entry point for the kernel driver.
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	WDF_DRIVER_CONFIG config;
	WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd); // Register the AddDevice callback

	WDFDRIVER driver;
	NTSTATUS status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, &driver);

	if (NT_SUCCESS(status))
	{
		// Create the Control Device endpoint for user-mode apps
		CreateControlDevice(driver);
	}

	return status;
}

// Called every time the Plug-and-Play manager discovers a storage device in the driver stack.
NTSTATUS EvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
	UNREFERENCED_PARAMETER(Driver);
	NTSTATUS status;

	// Tell WDF that we are acting as a Filter driver, not a Function driver.
	WdfFdoInitSetFilter(DeviceInit);

	// Register PnP callbacks so we know when the hardware is powered up and ready to be queried
	WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
	pnpCallbacks.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

	// Allocate our per-device context space
	WDF_OBJECT_ATTRIBUTES deviceAttributes;
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, FILTER_DEVICE_CONTEXT);

	WDFDEVICE device;
	status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
	if (!NT_SUCCESS(status)) return status;

	// Set up a parallel queue to intercept Read and Write operations concurrently.
	WDF_IO_QUEUE_CONFIG queueConfig;
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
	queueConfig.EvtIoRead = EvtIoRead;
	queueConfig.EvtIoWrite = EvtIoWrite;

	return WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

// Called after the device is powered up. Perfect time to query hardware limits.
NTSTATUS EvtDevicePrepareHardware(WDFDEVICE Device, WDFCMRESLIST ResourcesRaw, WDFCMRESLIST ResourcesTranslated)
{
	UNREFERENCED_PARAMETER(ResourcesRaw);
	UNREFERENCED_PARAMETER(ResourcesTranslated);

	PFILTER_DEVICE_CONTEXT devCtx = GetDeviceContext(Device);
	WDFIOTARGET target = WdfDeviceGetIoTarget(Device);

	// Set safe defaults just in case the hardware queries fail
	devCtx->MaxSafeTransferSizeBytes = DEFAULT_MAX_TRANSFER_SIZE;
	devCtx->SectorSize = 512; // Fallback default

	// Step 1: Get actual logical sector size via IOCTL_DISK_GET_DRIVE_GEOMETRY
	DISK_GEOMETRY geometry = { 0 };
	WDF_MEMORY_DESCRIPTOR geometryDescriptor;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&geometryDescriptor, &geometry, sizeof(DISK_GEOMETRY));

	NTSTATUS status = WdfIoTargetSendIoctlSynchronously(
		target, NULL, IOCTL_DISK_GET_DRIVE_GEOMETRY,
		NULL, &geometryDescriptor, NULL, NULL
	);

	if (NT_SUCCESS(status) && geometry.BytesPerSector != 0)
	{
		devCtx->SectorSize = geometry.BytesPerSector; // Will properly detect 4Kn drives
	}

	// Step 2: Query SCSI VPD Page 0xB0 (Block Limits)
	SCSI_PASS_THROUGH spt = { 0 };
	spt.Length = sizeof(SCSI_PASS_THROUGH);
	spt.CdbLength = 6;
	spt.DataIn = SCSI_IOCTL_DATA_IN;
	spt.DataTransferLength = sizeof(CUSTOM_VPD_BLOCK_LIMITS_PAGE);
	spt.TimeOutValue = 2;
	spt.DataBufferOffset = sizeof(SCSI_PASS_THROUGH);

	// Craft the raw SCSI Command Descriptor Block (CDB)
	spt.Cdb[0] = SCSIOP_INQUIRY;
	spt.Cdb[1] = 0x01; // Enable Vital Product Data (EVPD)
	spt.Cdb[2] = 0xB0; // The Block Limits page
	spt.Cdb[4] = sizeof(CUSTOM_VPD_BLOCK_LIMITS_PAGE);

	size_t totalBufferSize = sizeof(SCSI_PASS_THROUGH) + sizeof(CUSTOM_VPD_BLOCK_LIMITS_PAGE);

	// Use Non-Paged pool because we are doing hardware I/O
	PUCHAR buffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, totalBufferSize, 'IPDS');
	if (!buffer) return STATUS_SUCCESS; // Silently fail and use defaults

	RtlCopyMemory(buffer, &spt, sizeof(SCSI_PASS_THROUGH));

	WDF_MEMORY_DESCRIPTOR inputDescriptor;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputDescriptor, buffer, (ULONG)totalBufferSize);

	WDF_MEMORY_DESCRIPTOR outputDescriptor;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputDescriptor, buffer, (ULONG)totalBufferSize);

	status = WdfIoTargetSendIoctlSynchronously(
		target, NULL, IOCTL_SCSI_PASS_THROUGH,
		&inputDescriptor, &outputDescriptor, NULL, NULL
	);

	if (NT_SUCCESS(status))
	{
		PSCSI_PASS_THROUGH pSpt = (PSCSI_PASS_THROUGH)buffer;
		if (pSpt->ScsiStatus == SCSISTAT_GOOD)
		{
			PCUSTOM_VPD_BLOCK_LIMITS_PAGE pVpd = (PCUSTOM_VPD_BLOCK_LIMITS_PAGE)(buffer + sizeof(SCSI_PASS_THROUGH));

			if (pVpd->PageCode == 0xB0)
			{
				// SCSI returns big-endian values, we must byte-swap to Windows little-endian
				ULONG maxTransferBlocks = _byteswap_ulong(pVpd->MaximumTransferLength);

				if (maxTransferBlocks > 0)
				{
					// Use dynamically retrieved logical sector size instead of hardcoded 512
					ULONG maxBytes = maxTransferBlocks * devCtx->SectorSize;
					DbgPrint("[SCSI INQUIRY] HW Max Transfer: %u blocks (%u bytes)\n", maxTransferBlocks, maxBytes);
					devCtx->MaxSafeTransferSizeBytes = maxBytes;
				}
			}
		}
	}

	ExFreePoolWithTag(buffer, 'IPDS');
	return STATUS_SUCCESS;
}

// Callback invoked asynchronously when one of our split "Child" chunks finishes at the hardware level.
VOID EvtChildRequestCompleted(WDFREQUEST ChildRequest, WDFIOTARGET Target, PWDF_REQUEST_COMPLETION_PARAMS Params, WDFCONTEXT Context)
{
	UNREFERENCED_PARAMETER(Target);
	WDFREQUEST parentRequest = (WDFREQUEST)Context; // Context contains the original huge request
	PPARENT_REQUEST_CONTEXT parentCtx = GetParentContext(parentRequest);
	NTSTATUS childStatus = Params->IoStatus.Status;

	// If this chunk failed, record the failure. Protect with a spinlock since chunks complete concurrently on different CPU cores.
	if (!NT_SUCCESS(childStatus))
	{
		WdfSpinLockAcquire(parentCtx->Lock);
		parentCtx->FinalStatus = childStatus;
		WdfSpinLockRelease(parentCtx->Lock);
	}

	WdfObjectDelete(ChildRequest); // Free the memory used by this sub-request

	// Atomically decrement the active chunk counter. 
	// If it hits 0, all chunks are done, and we can finally complete the parent.
	if (InterlockedDecrement(&parentCtx->OutstandingChildren) == 0)
	{
		NTSTATUS finalStatus = parentCtx->FinalStatus;

		WDF_REQUEST_PARAMETERS parentParams;
		WDF_REQUEST_PARAMETERS_INIT(&parentParams);
		WdfRequestGetParameters(parentRequest, &parentParams);

		// Calculate how many bytes we technically completed for the OS
		size_t bytesCompleted = NT_SUCCESS(finalStatus) ?
			(parentParams.Type == WdfRequestTypeRead ? parentParams.Parameters.Read.Length : parentParams.Parameters.Write.Length) : 0;

		// Inform the OS that the original massive request is done
		WdfRequestCompleteWithInformation(parentRequest, finalStatus, bytesCompleted);
	}
}

VOID EvtIoRead(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
	WDFDEVICE device = WdfIoQueueGetDevice(Queue);
	WDFIOTARGET target = WdfDeviceGetIoTarget(device);
	PFILTER_DEVICE_CONTEXT devCtx = GetDeviceContext(device);
	size_t activeSafeSize = devCtx->MaxSafeTransferSizeBytes;

	// FAST PATH: If the splitter is disabled, or the request is naturally small enough, 
	// just pass it down the stack immediately. Send-and-Forget means we don't even wait for the result.
	if (g_SplitterEnabled == 0 || Length <= activeSafeSize)
	{
		WDF_REQUEST_SEND_OPTIONS options;
		WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
		if (!WdfRequestSend(Request, target, &options))
		{
			WdfRequestComplete(Request, WdfRequestGetStatus(Request));
		}
		return;
	}

	// SPLIT PATH: The request is too large. We must slice it.
	WDF_REQUEST_PARAMETERS params;
	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);
	LONGLONG deviceOffset = params.Parameters.Read.DeviceOffset;

	WDFMEMORY parentMemory; // The giant buffer the OS wants us to read into
	NTSTATUS status = WdfRequestRetrieveOutputMemory(Request, &parentMemory);
	if (!NT_SUCCESS(status))
	{
		WdfRequestComplete(Request, status);
		return;
	}

	// Allocate a tracking context for the Parent request
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, PARENT_REQUEST_CONTEXT);
	PPARENT_REQUEST_CONTEXT parentCtx;
	status = WdfObjectAllocateContext(Request, &attributes, (PVOID*)&parentCtx);
	if (!NT_SUCCESS(status))
	{
		// INSUFFICIENT RESOURCES GUARD: Graceful failure if the system is out of memory
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return;
	}

	parentCtx->FinalStatus = STATUS_SUCCESS;

	// Create a spinlock to safely update FinalStatus later
	WDF_OBJECT_ATTRIBUTES lockAttributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
	lockAttributes.ParentObject = Request;
	WdfSpinLockCreate(&lockAttributes, &parentCtx->Lock);

	// LOOP BIAS: We start the counter at 1. This prevents the request from completing 
	// prematurely if chunk #1 finishes processing before we even finish submitting chunk #2.
	parentCtx->OutstandingChildren = 1;

	size_t memoryOffset = 0;

	// Slicing loop
	while (memoryOffset < Length)
	{
		size_t chunkSize = Length - memoryOffset;
		if (chunkSize > activeSafeSize) chunkSize = activeSafeSize; // Clamp size to hardware max

		WDFREQUEST childRequest;
		status = WdfRequestCreate(WDF_NO_OBJECT_ATTRIBUTES, target, &childRequest);
		if (!NT_SUCCESS(status))
		{
			// INSUFFICIENT RESOURCES GUARD: If we run out of memory mid-split, abort.
			WdfSpinLockAcquire(parentCtx->Lock);
			parentCtx->FinalStatus = STATUS_INSUFFICIENT_RESOURCES;
			WdfSpinLockRelease(parentCtx->Lock);
			break;
		}

		// Map the child's buffer to a specific slice of the parent's giant buffer
		WDFMEMORY_OFFSET memOffset;
		memOffset.BufferOffset = memoryOffset;
		memOffset.BufferLength = chunkSize;

		// Format the child request for reading
		status = WdfIoTargetFormatRequestForRead(target, childRequest, parentMemory, &memOffset, &deviceOffset);

		if (!NT_SUCCESS(status))
		{
			WdfObjectDelete(childRequest);
			WdfSpinLockAcquire(parentCtx->Lock);
			parentCtx->FinalStatus = status;
			WdfSpinLockRelease(parentCtx->Lock);
			break;
		}

		// Hook the completion routine
		WdfRequestSetCompletionRoutine(childRequest, EvtChildRequestCompleted, Request);
		InterlockedIncrement(&parentCtx->OutstandingChildren);

		// Send the chunk down to the disk driver
		if (!WdfRequestSend(childRequest, target, WDF_NO_SEND_OPTIONS))
		{
			// If it fails immediately (e.g. device removed), record the error
			status = WdfRequestGetStatus(childRequest);
			WdfSpinLockAcquire(parentCtx->Lock);
			parentCtx->FinalStatus = status;
			WdfSpinLockRelease(parentCtx->Lock);
			WdfObjectDelete(childRequest);
			InterlockedDecrement(&parentCtx->OutstandingChildren);
		}

		// Advance pointers for the next slice
		memoryOffset += chunkSize;
		deviceOffset += chunkSize;
	}

	// Remove the initial loop bias. If all chunks completed synchronously, this drops 
	// the counter to 0 and completes the parent request.
	if (InterlockedDecrement(&parentCtx->OutstandingChildren) == 0)
	{
		status = parentCtx->FinalStatus;
		WdfRequestComplete(Request, status);
	}
}

// EvtIoWrite is functionally identical to EvtIoRead, 
// except it retrieves Input memory and formats for Write.
VOID EvtIoWrite(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
	WDFDEVICE device = WdfIoQueueGetDevice(Queue);
	WDFIOTARGET target = WdfDeviceGetIoTarget(device);
	PFILTER_DEVICE_CONTEXT devCtx = GetDeviceContext(device);
	size_t activeSafeSize = devCtx->MaxSafeTransferSizeBytes;

	// Fast Path: Pass-through if Disabled OR if the request is small enough
	if (g_SplitterEnabled == 0 || Length <= activeSafeSize)
	{
		WDF_REQUEST_SEND_OPTIONS options;
		WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
		if (!WdfRequestSend(Request, target, &options))
		{
			WdfRequestComplete(Request, WdfRequestGetStatus(Request));
		}
		return;
	}

	// SPLIT PATH: The request is too large. We must slice it.
	WDF_REQUEST_PARAMETERS params;
	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);
	LONGLONG deviceOffset = params.Parameters.Write.DeviceOffset;

	WDFMEMORY parentMemory;
	NTSTATUS status = WdfRequestRetrieveInputMemory(Request, &parentMemory); // Input memory for Write
	if (!NT_SUCCESS(status))
	{
		WdfRequestComplete(Request, status);
		return;
	}

	// Allocate a tracking context for the Parent request
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, PARENT_REQUEST_CONTEXT);
	PPARENT_REQUEST_CONTEXT parentCtx;
	status = WdfObjectAllocateContext(Request, &attributes, (PVOID*)&parentCtx);
	if (!NT_SUCCESS(status))
	{
		// INSUFFICIENT RESOURCES GUARD
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return;
	}

	parentCtx->FinalStatus = STATUS_SUCCESS;

	// Create a spinlock to safely update FinalStatus later
	WDF_OBJECT_ATTRIBUTES lockAttributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
	lockAttributes.ParentObject = Request;
	WdfSpinLockCreate(&lockAttributes, &parentCtx->Lock);

	parentCtx->OutstandingChildren = 1; // Initial bias

	size_t memoryOffset = 0;

	// Slicing loop
	while (memoryOffset < Length)
	{
		size_t chunkSize = Length - memoryOffset;
		if (chunkSize > activeSafeSize) chunkSize = activeSafeSize; // Clamp size to hardware max

		WDFREQUEST childRequest;
		status = WdfRequestCreate(WDF_NO_OBJECT_ATTRIBUTES, target, &childRequest);
		if (!NT_SUCCESS(status))
		{
			// INSUFFICIENT RESOURCES GUARD
			WdfSpinLockAcquire(parentCtx->Lock);
			parentCtx->FinalStatus = STATUS_INSUFFICIENT_RESOURCES;
			WdfSpinLockRelease(parentCtx->Lock);
			break;
		}

		// Map the child's buffer to a specific slice of the parent's giant buffer
		WDFMEMORY_OFFSET memOffset;
		memOffset.BufferOffset = memoryOffset;
		memOffset.BufferLength = chunkSize;

		// Format the child request for writing
		status = WdfIoTargetFormatRequestForWrite(target, childRequest, parentMemory, &memOffset, &deviceOffset);

		if (!NT_SUCCESS(status))
		{
			WdfObjectDelete(childRequest);
			WdfSpinLockAcquire(parentCtx->Lock);
			parentCtx->FinalStatus = status;
			WdfSpinLockRelease(parentCtx->Lock);
			break;
		}

		// Hook the completion routine
		WdfRequestSetCompletionRoutine(childRequest, EvtChildRequestCompleted, Request);
		InterlockedIncrement(&parentCtx->OutstandingChildren);

		// Send the chunk down to the disk driver
		if (!WdfRequestSend(childRequest, target, WDF_NO_SEND_OPTIONS))
		{
			// If it fails immediately (e.g. device removed), record the error
			status = WdfRequestGetStatus(childRequest);
			WdfSpinLockAcquire(parentCtx->Lock);
			parentCtx->FinalStatus = status;
			WdfSpinLockRelease(parentCtx->Lock);
			WdfObjectDelete(childRequest);
			InterlockedDecrement(&parentCtx->OutstandingChildren);
		}

		// Advance pointers for the next slice
		memoryOffset += chunkSize;
		deviceOffset += chunkSize;
	}

	// Remove loop bias
	if (InterlockedDecrement(&parentCtx->OutstandingChildren) == 0)
	{
		status = parentCtx->FinalStatus;
		WdfRequestComplete(Request, status);
	}
}