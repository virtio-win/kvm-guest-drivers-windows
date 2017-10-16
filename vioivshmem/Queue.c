#include "driver.h"
#include "queue.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOIVSHMEMQueueInitialize)
#pragma alloc_text (PAGE, VIOIVSHMEMEvtIoDeviceControl)
#pragma alloc_text (PAGE, VIOIVSHMEMEvtIoStop)
#pragma alloc_text (PAGE, VIOIVSHMEMEvtDeviceFileCleanup)
#endif

NTSTATUS VIOIVSHMEMQueueInitialize(_In_ WDFDEVICE Device)
{
    WDFQUEUE queue;
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;

    PAGED_CODE();

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = VIOIVSHMEMEvtIoDeviceControl;
    queueConfig.EvtIoStop          = VIOIVSHMEMEvtIoStop;

    status = WdfIoQueueCreate(Device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);

    if(!NT_SUCCESS(status))
	{
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        return status;
    }

    return status;
}

VOID
VIOIVSHMEMEvtIoDeviceControl(
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

	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
	switch (IoControlCode)
	{
		case VIOIVSHMEM_IOCTL_REQUEST_SIZE:
		{
			if (OutputBufferLength != sizeof(size_t))
			{
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			size_t *out = NULL;
			if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, (PVOID)&out, NULL)))
			{
				status = STATUS_INVALID_USER_BUFFER;
				break;
			}

			*out = deviceContext->shmemAddr.NumberOfBytes;
			status = STATUS_SUCCESS;
			bytesReturned = sizeof(size_t);
			break;
		}

		case VIOIVSHMEM_IOCTL_REQUEST_MMAP:
		{
			// only one mapping at a time is allowed
			if (deviceContext->shmemMap)
			{
				status = STATUS_DEVICE_ALREADY_ATTACHED;
				break;
			}

			if (OutputBufferLength != sizeof(VIOIVSHMEM_MMAP))
			{
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			PVIOIVSHMEM_MMAP out = NULL;
			if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, (PVOID)&out, NULL)))
			{
				status = STATUS_INVALID_USER_BUFFER;
				break;
			}

			deviceContext->shmemMap = MmMapLockedPagesSpecifyCache(
				deviceContext->shmemMDL,
				UserMode,
				MmWriteCombined,
				NULL,
				FALSE,
				NormalPagePriority | MdlMappingNoExecute
			);

			if (!deviceContext->shmemMap)
			{
				status = STATUS_DRIVER_INTERNAL_ERROR;
				break;
			}

			deviceContext->owner = WdfRequestGetFileObject(Request);
			out->size = deviceContext->shmemAddr.NumberOfBytes;
			out->ptr  = deviceContext->shmemMap;
			status    = STATUS_SUCCESS;
			bytesReturned = sizeof(VIOIVSHMEM_MMAP);
			break;
		}

		case VIOIVSHMEM_IOCTL_RELEASE_MMAP:
		{
			// ensure the mapping exists
			if (!deviceContext->shmemMap)
			{
				status = STATUS_INVALID_DEVICE_REQUEST;
				break;
			}

			// ensure someone else other then the owner doesn't attempt to release the mapping
			if (deviceContext->owner != WdfRequestGetFileObject(Request))
			{
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
	}


	WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}

VOID
VIOIVSHMEMEvtIoStop(
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

VOID VIOIVSHMEMEvtDeviceFileCleanup(_In_ WDFFILEOBJECT FileObject)
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