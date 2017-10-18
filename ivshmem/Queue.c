#include "driver.h"
#include "queue.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, IVSHMEMQueueInitialize)
#pragma alloc_text (PAGE, IVSHMEMEvtIoDeviceControl)
#pragma alloc_text (PAGE, IVSHMEMEvtIoStop)
#pragma alloc_text (PAGE, IVSHMEMEvtDeviceFileCleanup)
#endif

NTSTATUS IVSHMEMQueueInitialize(_In_ WDFDEVICE Device)
{
    WDFQUEUE queue;
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;

    PAGED_CODE();

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = IVSHMEMEvtIoDeviceControl;
    queueConfig.EvtIoStop          = IVSHMEMEvtIoStop;

    status = WdfIoQueueCreate(Device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);

    if(!NT_SUCCESS(status))
	{
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        return status;
    }

    return status;
}

VOID
IVSHMEMEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
	PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "%!FUNC! Queue 0x%p, Request 0x%p OutputBufferLength %d InputBufferLength %d IoControlCode %d", 
                Queue, Request, (int) OutputBufferLength, (int) InputBufferLength, IoControlCode);

	WDFDEVICE hDevice = WdfIoQueueGetDevice(Queue);
	PDEVICE_CONTEXT deviceContext = DeviceGetContext(hDevice);
	size_t bytesReturned = 0;

	// revision 0 devices have to wait until the shared memory has been provided to the vm
	if (deviceContext->devRegisters->ivProvision < 0)
	{
		DEBUG_INFO("Device not ready yet, ivProvision = %d", deviceContext->devRegisters->ivProvision);
		WdfRequestCompleteWithInformation(Request, STATUS_DEVICE_NOT_READY, 0);
		return;
	}

	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
	switch (IoControlCode)
	{
		case IOCTL_IVSHMEM_REQUEST_PEERID:
		{
			if (OutputBufferLength != sizeof(IVSHMEM_PEERID))
			{
				DEBUG_ERROR("IOCTL_IVSHMEM_REQUEST_PEERID: Invalid size, expected %u but got %u", sizeof(IVSHMEM_PEERID), OutputBufferLength);
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			IVSHMEM_PEERID *out = NULL;
			if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, (PVOID)&out, NULL)))
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REQUEST_PEERID: Failed to retrieve the output buffer");
				status = STATUS_INVALID_USER_BUFFER;
				break;
			}

			*out = (IVSHMEM_PEERID)deviceContext->devRegisters->ivProvision;
			status = STATUS_SUCCESS;
			bytesReturned = sizeof(IVSHMEM_PEERID);
			break;
		}

		case IOCTL_IVSHMEM_REQUEST_SIZE:
		{
			if (OutputBufferLength != sizeof(IVSHMEM_SIZE))
			{
				DEBUG_ERROR("IOCTL_IVSHMEM_REQUEST_SIZE: Invalid size, expected %u but got %u", sizeof(IVSHMEM_SIZE), OutputBufferLength);
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			IVSHMEM_SIZE *out = NULL;
			if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, (PVOID)&out, NULL)))
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REQUEST_SIZE: Failed to retrieve the output buffer");
				status = STATUS_INVALID_USER_BUFFER;
				break;
			}

			*out = deviceContext->shmemAddr.NumberOfBytes;
			status = STATUS_SUCCESS;
			bytesReturned = sizeof(IVSHMEM_SIZE);
			break;
		}

		case IOCTL_IVSHMEM_REQUEST_MMAP:
		{
			// only one mapping at a time is allowed
			if (deviceContext->shmemMap)
			{
				status = STATUS_DEVICE_ALREADY_ATTACHED;
				break;
			}

			if (OutputBufferLength != sizeof(IVSHMEM_MMAP))
			{
				DEBUG_ERROR("IOCTL_IVSHMEM_REQUEST_MMAP: Invalid size, expected %u but got %u", sizeof(IVSHMEM_MMAP), OutputBufferLength);
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			PIVSHMEM_MMAP out = NULL;
			if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, (PVOID)&out, NULL)))
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REQUEST_MMAP: Failed to retrieve the output buffer");
				status = STATUS_INVALID_USER_BUFFER;
				break;
			}

			__try
			{
				deviceContext->shmemMap = MmMapLockedPagesSpecifyCache(
					deviceContext->shmemMDL,
					UserMode,
					MmWriteCombined,
					NULL,
					FALSE,
					NormalPagePriority | MdlMappingNoExecute
				);
			}
			__except(EXCEPTION_EXECUTE_HANDLER)
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REQUEST_MMAP: Exception trying to map pages");
				status = STATUS_DRIVER_INTERNAL_ERROR;
				break;
			}

			if (!deviceContext->shmemMap)
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REQUEST_MMAP: shmemMap is NULL");
				status = STATUS_DRIVER_INTERNAL_ERROR;
				break;
			}

			deviceContext->owner = WdfRequestGetFileObject(Request);
			out->peerID   = (UINT16)deviceContext->devRegisters->ivProvision;
			out->size     = (UINT64)deviceContext->shmemAddr.NumberOfBytes;
			out->ptr      = deviceContext->shmemMap;
			out->vectors  = deviceContext->interruptsUsed;
			status        = STATUS_SUCCESS;
			bytesReturned = sizeof(IVSHMEM_MMAP);
			break;
		}

		case IOCTL_IVSHMEM_RELEASE_MMAP:
		{
			// ensure the mapping exists
			if (!deviceContext->shmemMap)
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_RELEASE_MMAP: not mapped");
				status = STATUS_INVALID_DEVICE_REQUEST;
				break;
			}

			// ensure someone else other then the owner doesn't attempt to release the mapping
			if (deviceContext->owner != WdfRequestGetFileObject(Request))
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_RELEASE_MMAP: Invalid owner");
				status = STATUS_INVALID_HANDLE;
				break;
			}

			MmUnmapLockedPages(deviceContext->shmemMap, deviceContext->shmemMDL);
			deviceContext->shmemMap = NULL;
			deviceContext->owner    = NULL;
			status = STATUS_SUCCESS;
			bytesReturned = 0;
			break;
		}

		case IOCTL_IVSHMEM_RING_DOORBELL:
		{
			// ensure someone else other then the owner doesn't attempt to trigger IRQs
			if (deviceContext->owner != WdfRequestGetFileObject(Request))
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_RING_DOORBELL: Invalid owner");
				status = STATUS_INVALID_HANDLE;
				break;
			}

			if (InputBufferLength != sizeof(IVSHMEM_RING))
			{
				DEBUG_ERROR("IOCTL_IVSHMEM_RING_DOORBELL: Invalid size, expected %u but got %u", sizeof(IVSHMEM_RING), InputBufferLength);
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			PIVSHMEM_RING in;
			if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID)&in, NULL)))
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_RING_DOORBELL: Failed to retrieve the input buffer");
				status = STATUS_INVALID_USER_BUFFER;
				break;
			}
		
			deviceContext->devRegisters->doorbell |= (UINT32)in->vector | (in->peerID << 16);
			status = STATUS_SUCCESS;
			bytesReturned = 0;
			break;
		}

		case IOCTL_IVSHMEM_REGISTER_EVENT:
		{
			// ensure someone else other then the owner isn't attempting to register events
			if (deviceContext->owner != WdfRequestGetFileObject(Request))
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REGISTER_EVENT: Invalid owner");
				status = STATUS_INVALID_HANDLE;
				break;
			}

			if (InputBufferLength != sizeof(IVSHMEM_EVENT))
			{
				DEBUG_ERROR("IOCTL_IVSHMEM_REGISTER_EVENT: Invalid size, expected %u but got %u", sizeof(PIVSHMEM_EVENT), InputBufferLength);
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			PIVSHMEM_EVENT in;
			if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID)&in, NULL)))
			{
				DEBUG_ERROR("%s", "IOCTL_IVSHMEM_RING_DOORBELL: Failed to retrieve the input buffer");
				status = STATUS_INVALID_USER_BUFFER;
				break;
			}

			PIRP irp = WdfRequestWdmGetIrp(Request);			
			PRKEVENT hObject;
			if (!NT_SUCCESS(ObReferenceObjectByHandle(
					in->event,
					SYNCHRONIZE | EVENT_MODIFY_STATE,
					*ExEventObjectType,
					irp->RequestorMode,
					&hObject,
					NULL)))
			{
				DEBUG_ERROR("%s", "Unable to reference user-mode event object");
				status = STATUS_INVALID_HANDLE;
				break;
			}

			PIVSHMEMEventListEntry event = (PIVSHMEMEventListEntry)
				MmAllocateNonCachedMemory(sizeof(IVSHMEMEventListEntry));
			if (!event)
			{
				DEBUG_ERROR("%s", "Unable to allocate PIVSHMEMEventListEntry");
				ObDereferenceObject(hObject);
				status = STATUS_MEMORY_NOT_ALLOCATED;
				break;
			}

			event->event  = hObject;
			event->vector = in->vector;
			ExInterlockedInsertTailList(&deviceContext->eventList,
				&event->ListEntry, &deviceContext->eventListLock);

			bytesReturned = 0;
			status = STATUS_SUCCESS;
			break;
		}
	}


	WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}

VOID
IVSHMEMEvtIoStop(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG ActionFlags
)
{
	PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "%!FUNC! Queue 0x%p, Request 0x%p ActionFlags %d", 
                Queue, Request, ActionFlags);

	WdfRequestStopAcknowledge(Request, TRUE);
    return;
}

VOID IVSHMEMEvtDeviceFileCleanup(_In_ WDFFILEOBJECT FileObject)
{
	PAGED_CODE();
	TraceEvents(TRACE_LEVEL_INFORMATION,
		TRACE_QUEUE,
		"%!FUNC! File 0x%p",
		FileObject);

	PDEVICE_CONTEXT deviceContext = DeviceGetContext(WdfFileObjectGetDevice(FileObject));
	if (!deviceContext->shmemMap)
		return;

	if (deviceContext->owner != FileObject)
		return;

	MmUnmapLockedPages(deviceContext->shmemMap, deviceContext->shmemMDL);
	deviceContext->shmemMap = NULL;
	deviceContext->owner    = NULL;
}