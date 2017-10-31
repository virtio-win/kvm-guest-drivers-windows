#include "driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, IVSHMEMQueueInitialize)
#endif

#ifdef _WIN64
// 32bit struct for when a 32bit application sends IOCTL codes
typedef struct IVSHMEM_MMAP32
{
    IVSHMEM_PEERID peerID;  // our peer id
    IVSHMEM_SIZE   size;    // the size of the memory region
    UINT32         ptr;     // pointer to the memory region
    UINT16         vectors; // the number of vectors available
}
IVSHMEM_MMAP32, *PIVSHMEM_MMAP32;
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
            if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, (PVOID *)&out, NULL)))
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
            if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, (PVOID *)&out, NULL)))
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

#ifdef _WIN64
            PIRP  irp = WdfRequestWdmGetIrp(Request);
            const BOOLEAN is32Bit   = IoIs32bitProcess(irp);
            const size_t  bufferLen = is32Bit ? sizeof(IVSHMEM_MMAP32) : sizeof(IVSHMEM_MMAP);
#else
            const size_t  bufferLen = sizeof(IVSHMEM_MMAP);
#endif
            PVOID buffer;

            if (OutputBufferLength != bufferLen)
            {
                DEBUG_ERROR("IOCTL_IVSHMEM_REQUEST_MMAP: Invalid size, expected %u but got %u", bufferLen, OutputBufferLength);
                status = STATUS_INVALID_BUFFER_SIZE;
                break;
            }

            if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, bufferLen, (PVOID *)&buffer, NULL)))
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
#ifdef _WIN64
            if (is32Bit)
            {
                PIVSHMEM_MMAP32 out = (PIVSHMEM_MMAP32)buffer;
                out->peerID  = (UINT16)deviceContext->devRegisters->ivProvision;
                out->size    = (UINT64)deviceContext->shmemAddr.NumberOfBytes;
                out->ptr     = PtrToUint(deviceContext->shmemMap);
                out->vectors = deviceContext->interruptsUsed;
            }
            else
#endif
            {
                PIVSHMEM_MMAP out = (PIVSHMEM_MMAP)buffer;
                out->peerID   = (UINT16)deviceContext->devRegisters->ivProvision;
                out->size     = (UINT64)deviceContext->shmemAddr.NumberOfBytes;
                out->ptr      = deviceContext->shmemMap;
                out->vectors  = deviceContext->interruptsUsed;
            }
            status = STATUS_SUCCESS;
            bytesReturned = bufferLen;
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

            WRITE_REGISTER_ULONG(
                &deviceContext->devRegisters->doorbell,
                (ULONG)in->vector | ((ULONG)in->peerID << 16));

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

            // early non locked quick check to see if we are out of event space
            if (deviceContext->eventBufferUsed == MAX_EVENTS)
            {
                DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REGISTER_EVENT: Event buffer full");
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            PIVSHMEM_EVENT in;
            if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID)&in, NULL)))
            {
                DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REGISTER_EVENT: Failed to retrieve the input buffer");
                status = STATUS_INVALID_USER_BUFFER;
                break;
            }

            PRKEVENT hObject;
            if (!NT_SUCCESS(ObReferenceObjectByHandle(
                    in->event,
                    SYNCHRONIZE | EVENT_MODIFY_STATE,
                    *ExEventObjectType,
                    UserMode,
                    &hObject,
                    NULL)))
            {
                DEBUG_ERROR("%s", "Unable to reference user-mode event object");
                status = STATUS_INVALID_HANDLE;
                break;
            }

            // clear the event in case the caller didn't think to
            KeClearEvent(hObject);

            // lock the event list so we can push the new entry into it
            KIRQL oldIRQL;
            KeAcquireSpinLock(&deviceContext->eventListLock, &oldIRQL);
            {
                // check again if there is space before we search as we now hold the lock
                if (deviceContext->eventBufferUsed == MAX_EVENTS)
                {
                    KeReleaseSpinLock(&deviceContext->eventListLock, oldIRQL);

                    DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REGISTER_EVENT: Event buffer full");
                    ObDereferenceObject(hObject);
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                // look for a free slot
                BOOLEAN done = FALSE;
                for (UINT16 i = 0; i < MAX_EVENTS; ++i)
                {
                    PIVSHMEMEventListEntry event = &deviceContext->eventBuffer[i];
                    if (event->event != NULL)
                        continue;

                    // found one, assign the event to it and add it to the list
                    event->owner      = WdfRequestGetFileObject(Request);
                    event->event      = hObject;
                    event->vector     = in->vector;
                    event->singleShot = in->singleShot;
                    ++deviceContext->eventBufferUsed;
                    InsertTailList(&deviceContext->eventList, &event->ListEntry);
                    done = TRUE;
                    break;
                }

                // this should never occur, if it does it indicates memory corruption
                if (!done)
                {
                    DEBUG_ERROR(
                        "IOCTL_IVSHMEM_REGISTER_EVENT: deviceContext->eventBufferUsed (%u) < MAX_EVENTS (%u) but no slots found!",
                        deviceContext->eventBufferUsed, MAX_EVENTS);
                    KeBugCheckEx(CRITICAL_STRUCTURE_CORRUPTION, 0, 0, 0, 0x1C);
                }
            }
            KeReleaseSpinLock(&deviceContext->eventListLock, oldIRQL);

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
	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(ActionFlags);
    WdfRequestStopAcknowledge(Request, TRUE);
    return;
}

VOID IVSHMEMEvtDeviceFileCleanup(_In_ WDFFILEOBJECT FileObject)
{
    PDEVICE_CONTEXT deviceContext = DeviceGetContext(WdfFileObjectGetDevice(FileObject));

    // remove queued events that belonged to the session
    KIRQL oldIRQL;
    KeAcquireSpinLock(&deviceContext->eventListLock, &oldIRQL);
    PLIST_ENTRY entry = deviceContext->eventList.Flink;
    while (entry != &deviceContext->eventList)
    {
        _Analysis_assume_(entry != NULL);
        PIVSHMEMEventListEntry event = CONTAINING_RECORD(entry, IVSHMEMEventListEntry, ListEntry);
        if (event->owner != FileObject)
        {
            entry = entry->Flink;
            continue;
        }

        PLIST_ENTRY next = entry->Flink;
        RemoveEntryList(entry);
        ObDereferenceObject(event->event);
        event->owner = NULL;
        event->event = NULL;
        event->vector = 0;
        --deviceContext->eventBufferUsed;
        entry = next;
    }
    KeReleaseSpinLock(&deviceContext->eventListLock, oldIRQL);

    if (!deviceContext->shmemMap)
        return;

    if (deviceContext->owner != FileObject)
        return;

    MmUnmapLockedPages(deviceContext->shmemMap, deviceContext->shmemMDL);
    deviceContext->shmemMap = NULL;
    deviceContext->owner    = NULL;
}
