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

// Forwards
static NTSTATUS ioctl_request_peerid(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
);

static NTSTATUS ioctl_request_size(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
);

static NTSTATUS ioctl_request_mmap(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          InputBufferLength,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned,
    BOOLEAN               ForKernel
);

static NTSTATUS ioctl_release_mmap(
    const PDEVICE_CONTEXT DeviceContext,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
);

static NTSTATUS ioctl_ring_doorbell(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          InputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
);

static NTSTATUS ioctl_register_event(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          InputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
);

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
            status = ioctl_request_peerid(deviceContext, OutputBufferLength, Request, &bytesReturned);
            break;
  
        case IOCTL_IVSHMEM_REQUEST_SIZE:
            status = ioctl_request_size(deviceContext, OutputBufferLength, Request, &bytesReturned);
            break;

        case IOCTL_IVSHMEM_REQUEST_MMAP:
            status = ioctl_request_mmap(deviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned, FALSE);
            break;
        case IOCTL_IVSHMEM_REQUEST_KMAP:
            status = ioctl_request_mmap(deviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned, TRUE);
            break;
        case IOCTL_IVSHMEM_RELEASE_MMAP:
            status = ioctl_release_mmap(deviceContext, Request, &bytesReturned);
            break;

        case IOCTL_IVSHMEM_RING_DOORBELL:
            status = ioctl_ring_doorbell(deviceContext, InputBufferLength, Request, &bytesReturned);
            break;

        case IOCTL_IVSHMEM_REGISTER_EVENT:
            status = ioctl_register_event(deviceContext, InputBufferLength, Request, &bytesReturned);
            break;
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

static NTSTATUS ioctl_request_peerid(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
)
{
    if (OutputBufferLength != sizeof(IVSHMEM_PEERID))
    {
        DEBUG_ERROR("IOCTL_IVSHMEM_REQUEST_PEERID: Invalid size, expected %u but got %u", sizeof(IVSHMEM_PEERID), OutputBufferLength);
        return STATUS_INVALID_BUFFER_SIZE;
    }
  
    IVSHMEM_PEERID *out = NULL;
    if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, (PVOID *)&out, NULL)))
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REQUEST_PEERID: Failed to retrieve the output buffer");
        return STATUS_INVALID_USER_BUFFER;
    }
  
    *out           = (IVSHMEM_PEERID)DeviceContext->devRegisters->ivProvision;
    *BytesReturned = sizeof(IVSHMEM_PEERID);
    return STATUS_SUCCESS;
}

static NTSTATUS ioctl_request_size(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
)
{
    if (OutputBufferLength != sizeof(IVSHMEM_SIZE))
    {
        DEBUG_ERROR("IOCTL_IVSHMEM_REQUEST_SIZE: Invalid size, expected %u but got %u", sizeof(IVSHMEM_SIZE), OutputBufferLength);
        return STATUS_INVALID_BUFFER_SIZE;
    }
  
    IVSHMEM_SIZE *out = NULL;
    if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, (PVOID *)&out, NULL)))
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REQUEST_SIZE: Failed to retrieve the output buffer");
        return STATUS_INVALID_USER_BUFFER;
    }
  
    *out           = DeviceContext->shmemAddr.NumberOfBytes;
    *BytesReturned = sizeof(IVSHMEM_SIZE);
    return STATUS_SUCCESS;
}

static NTSTATUS ioctl_request_mmap(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          InputBufferLength,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned,
    BOOLEAN               ForKernel
)
{
    // only one mapping at a time is allowed
    if (DeviceContext->shmemMap)
        return STATUS_DEVICE_ALREADY_ATTACHED;
  
    if (InputBufferLength != sizeof(IVSHMEM_MMAP_CONFIG))
    {
        DEBUG_ERROR("IOCTL_IVSHMEM_MMAP: Invalid input size, expected %u but got %u", sizeof(IVSHMEM_MMAP_CONFIG), InputBufferLength);
        return STATUS_INVALID_BUFFER_SIZE;
    }
  
    PIVSHMEM_MMAP_CONFIG in;
    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID)&in, NULL)))
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_MMAP: Failed to retrieve the input buffer");
        return STATUS_INVALID_USER_BUFFER;
    }
  
    MEMORY_CACHING_TYPE cacheType;
    switch (in->cacheMode)
    {
        case IVSHMEM_CACHE_NONCACHED    : cacheType = MmNonCached    ; break;
        case IVSHMEM_CACHE_CACHED       : cacheType = MmCached       ; break;
        case IVSHMEM_CACHE_WRITECOMBINED: cacheType = MmWriteCombined; break;
        default:
          DEBUG_ERROR("IOCTL_IVSHMEM_MMAP: Invalid cache mode: %u", in->cacheMode);
          return STATUS_INVALID_PARAMETER;
    }
  
  #ifdef _WIN64
    PIRP  irp = WdfRequestWdmGetIrp(Request);
    const BOOLEAN is32Bit = IoIs32bitProcess(irp);
    const size_t  bufferLen = is32Bit ? sizeof(IVSHMEM_MMAP32) : sizeof(IVSHMEM_MMAP);
  #else
    const size_t  bufferLen = sizeof(IVSHMEM_MMAP);
  #endif
    PVOID buffer;
  
    if (OutputBufferLength != bufferLen)
    {
        DEBUG_ERROR("IOCTL_IVSHMEM_REQUEST_MMAP: Invalid size, expected %u but got %u", bufferLen, OutputBufferLength);
        return STATUS_INVALID_BUFFER_SIZE;
    }
  
    if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, bufferLen, (PVOID *)&buffer, NULL)))
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REQUEST_MMAP: Failed to retrieve the output buffer");
        return STATUS_INVALID_USER_BUFFER;
    }
  
    __try
    {
        DeviceContext->shmemMap = MmMapLockedPagesSpecifyCache(
          DeviceContext->shmemMDL,
          ForKernel ? KernelMode : UserMode,
          cacheType,
          NULL,
          FALSE,
          NormalPagePriority | MdlMappingNoExecute
        );
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REQUEST_MMAP: Exception trying to map pages");
        return STATUS_DRIVER_INTERNAL_ERROR;
    }
  
    if (!DeviceContext->shmemMap)
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REQUEST_MMAP: shmemMap is NULL");
        return STATUS_DRIVER_INTERNAL_ERROR;
    }
  
    DeviceContext->owner = WdfRequestGetFileObject(Request);
  #ifdef _WIN64
    if (is32Bit)
    {
        PIVSHMEM_MMAP32 out = (PIVSHMEM_MMAP32)buffer;
        out->peerID  = (UINT16)DeviceContext->devRegisters->ivProvision;
        out->size    = (UINT64)DeviceContext->shmemAddr.NumberOfBytes;
        out->ptr     = PtrToUint(DeviceContext->shmemMap);
        out->vectors = DeviceContext->interruptsUsed;
    }
    else
  #endif
    {
        PIVSHMEM_MMAP out = (PIVSHMEM_MMAP)buffer;
        out->peerID  = (UINT16)DeviceContext->devRegisters->ivProvision;
        out->size    = (UINT64)DeviceContext->shmemAddr.NumberOfBytes;
        out->ptr     = DeviceContext->shmemMap;
        out->vectors = DeviceContext->interruptsUsed;
    }
    
    *BytesReturned = bufferLen;
    return STATUS_SUCCESS;
}

static NTSTATUS ioctl_release_mmap(
    const PDEVICE_CONTEXT DeviceContext,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
)
{
    // ensure the mapping exists
    if (!DeviceContext->shmemMap)
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_RELEASE_MMAP: not mapped");
        return STATUS_INVALID_DEVICE_REQUEST;
    }
  
    // ensure someone else other then the owner doesn't attempt to release the mapping
    if (DeviceContext->owner != WdfRequestGetFileObject(Request))
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_RELEASE_MMAP: Invalid owner");
        return STATUS_INVALID_HANDLE;
    }
  
    MmUnmapLockedPages(DeviceContext->shmemMap, DeviceContext->shmemMDL);
    DeviceContext->shmemMap = NULL;
    DeviceContext->owner    = NULL;
    *BytesReturned = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS ioctl_ring_doorbell(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          InputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
)
{
    // ensure someone else other then the owner doesn't attempt to trigger IRQs
    if (DeviceContext->owner != WdfRequestGetFileObject(Request))
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_RING_DOORBELL: Invalid owner");
        return STATUS_INVALID_HANDLE;
    }
  
    if (InputBufferLength != sizeof(IVSHMEM_RING))
    {
        DEBUG_ERROR("IOCTL_IVSHMEM_RING_DOORBELL: Invalid size, expected %u but got %u", sizeof(IVSHMEM_RING), InputBufferLength);
        return STATUS_INVALID_BUFFER_SIZE;
    }
  
    PIVSHMEM_RING in;
    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID)&in, NULL)))
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_RING_DOORBELL: Failed to retrieve the input buffer");
        return STATUS_INVALID_USER_BUFFER;   
    }
  
    WRITE_REGISTER_ULONG(
      &DeviceContext->devRegisters->doorbell,
      (ULONG)in->vector | ((ULONG)in->peerID << 16));
  
    *BytesReturned = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS ioctl_register_event(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          InputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
)
{
    // ensure someone else other then the owner isn't attempting to register events
    if (DeviceContext->owner != WdfRequestGetFileObject(Request))
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REGISTER_EVENT: Invalid owner");
        return STATUS_INVALID_HANDLE;
    }
  
    if (InputBufferLength != sizeof(IVSHMEM_EVENT))
    {
        DEBUG_ERROR("IOCTL_IVSHMEM_REGISTER_EVENT: Invalid size, expected %u but got %u", sizeof(PIVSHMEM_EVENT), InputBufferLength);
        return STATUS_INVALID_BUFFER_SIZE;
    }
  
    // early non locked quick check to see if we are out of event space
    if (DeviceContext->eventBufferUsed == MAX_EVENTS)
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REGISTER_EVENT: Event buffer full");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
  
    PIVSHMEM_EVENT in;
    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID)&in, NULL)))
    {
        DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REGISTER_EVENT: Failed to retrieve the input buffer");
        return STATUS_INVALID_USER_BUFFER;
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
        return STATUS_INVALID_HANDLE;
    }
  
    // clear the event in case the caller didn't think to
    KeClearEvent(hObject);
  
    // lock the event list so we can push the new entry into it
    KIRQL oldIRQL;
    KeAcquireSpinLock(&DeviceContext->eventListLock, &oldIRQL);
    {
        // check again if there is space before we search as we now hold the lock
        if (DeviceContext->eventBufferUsed == MAX_EVENTS)
        {
            KeReleaseSpinLock(&DeviceContext->eventListLock, oldIRQL);
      
            DEBUG_ERROR("%s", "IOCTL_IVSHMEM_REGISTER_EVENT: Event buffer full");
            ObDereferenceObject(hObject);
            return STATUS_INSUFFICIENT_RESOURCES;      
        }
    
        // look for a free slot
        BOOLEAN done = FALSE;
        for (UINT16 i = 0; i < MAX_EVENTS; ++i)
        {
            PIVSHMEMEventListEntry event = &DeviceContext->eventBuffer[i];
            if (event->event != NULL)
                continue;
      
            // found one, assign the event to it and add it to the list
            event->owner = WdfRequestGetFileObject(Request);
            event->event = hObject;
            event->vector = in->vector;
            event->singleShot = in->singleShot;
            ++DeviceContext->eventBufferUsed;
            InsertTailList(&DeviceContext->eventList, &event->ListEntry);
            done = TRUE;
            break;
        }
    
        // this should never occur, if it does it indicates memory corruption
        if (!done)
        {
            DEBUG_ERROR(
              "IOCTL_IVSHMEM_REGISTER_EVENT: deviceContext->eventBufferUsed (%u) < MAX_EVENTS (%u) but no slots found!",
              DeviceContext->eventBufferUsed, MAX_EVENTS);
            KeBugCheckEx(CRITICAL_STRUCTURE_CORRUPTION, 0, 0, 0, 0x1C);
        }
    }
    KeReleaseSpinLock(&DeviceContext->eventListLock, oldIRQL);
  
    *BytesReturned = 0;
    return STATUS_SUCCESS;
}
