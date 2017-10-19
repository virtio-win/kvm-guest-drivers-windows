#include "driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, IVSHMEMCreateDevice)
#pragma alloc_text (PAGE, IVSHMEMEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, IVSHMEMEvtDeviceReleaseHardware)
#pragma alloc_text (PAGE, IVSHMEMEvtD0Exit)
#endif

NTSTATUS IVSHMEMCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    PDEVICE_CONTEXT deviceContext;
    WDFDEVICE device;
    NTSTATUS status;

    PAGED_CODE();
    DEBUG_INFO("%s", __FUNCTION__);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = IVSHMEMEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = IVSHMEMEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry         = IVSHMEMEvtD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit          = IVSHMEMEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, NULL, NULL, IVSHMEMEvtDeviceFileCleanup);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

    if (!NT_SUCCESS(status))
    {
        DEBUG_ERROR("%s", "Call to WdfDeviceCreate failed");
        return status;
    }

    deviceContext = DeviceGetContext(device);
    RtlZeroMemory(deviceContext, sizeof(DEVICE_CONTEXT));
    KeInitializeSpinLock(&deviceContext->eventListLock);
    InitializeListHead(&deviceContext->eventList);

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_IVSHMEM, NULL);

    if (!NT_SUCCESS(status))
    {
        DEBUG_ERROR("%s", "Call to WdfDeviceCreateDeviceInterface failed");
        return status;
    }

    status = IVSHMEMQueueInitialize(device);
    if (!NT_SUCCESS(status))
    {
        DEBUG_ERROR("%s", "IVSHMEMQueueInitialize failed");
        return status;
    }

    return status;
}

NTSTATUS IVSHMEMEvtDevicePrepareHardware(_In_ WDFDEVICE Device, _In_ WDFCMRESLIST ResourcesRaw, _In_ WDFCMRESLIST ResourcesTranslated)
{
    PAGED_CODE();
    DEBUG_INFO("%s", __FUNCTION__);

    PDEVICE_CONTEXT deviceContext;
    deviceContext = DeviceGetContext(Device);

    NTSTATUS result = STATUS_SUCCESS;
    int memIndex = 0;

    const ULONG resCount = WdfCmResourceListGetCount(ResourcesTranslated);
    for (ULONG i = 0; i < resCount; ++i)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
        descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if (!descriptor)
        {
            DEBUG_ERROR("%s", "Call to WdfCmResourceListGetDescriptor failed");
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        if (descriptor->Type == CmResourceTypeInterrupt && descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
            ++deviceContext->interruptCount;
    }

	if (deviceContext->interruptCount > 0)
	{
		deviceContext->interrupts = (WDFINTERRUPT*)MmAllocateNonCachedMemory(
			sizeof(WDFINTERRUPT) * deviceContext->interruptCount);

		if (!deviceContext->interrupts)
		{
			DEBUG_ERROR("Failed to allocate space for %d interrupts", deviceContext->interrupts);
			return STATUS_DEVICE_CONFIGURATION_ERROR;
		}
	}

    for (ULONG i = 0; i < resCount; ++i)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
        descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if (!descriptor)
        {
            DEBUG_ERROR("%s", "Call to WdfCmResourceListGetDescriptor failed");
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        if (descriptor->Type == CmResourceTypeMemory)
        {
            // control registers
            if (memIndex == 0)
            {
                if (descriptor->u.Memory.Length != sizeof(IVSHMEMDeviceRegisters))
                {
                    DEBUG_ERROR("Resource size was %u long when %u was expected",
                        descriptor->u.Memory.Length, sizeof(IVSHMEMDeviceRegisters));
                    result = STATUS_DEVICE_HARDWARE_ERROR;
                    break;
                }

                deviceContext->devRegisters = (PIVSHMEMDeviceRegisters)MmMapIoSpace(
                    descriptor->u.Memory.Start, descriptor->u.Memory.Length, MmNonCached);

                if (!deviceContext->devRegisters)
                {
                    DEBUG_ERROR("%s", "Call to MmMapIoSpace failed");
                    result = STATUS_DEVICE_HARDWARE_ERROR;
                    break;
                }
            }
            else
            // shared memory resource
            if ((deviceContext->interruptCount == 0 && memIndex == 1) || memIndex == 2)
            {
                deviceContext->shmemAddr.PhysicalAddress = descriptor->u.Memory.Start;
                deviceContext->shmemAddr.NumberOfBytes = descriptor->u.Memory.Length;
                if (!NT_SUCCESS(MmAllocateMdlForIoSpace(&deviceContext->shmemAddr, 1, &deviceContext->shmemMDL)))
                {
                    DEBUG_ERROR("%s", "Call to MmAllocateMdlForIoSpace failed");
                    result = STATUS_DEVICE_HARDWARE_ERROR;
                    break;
                }
            }

            ++memIndex;
            continue;
        }

        if (descriptor->Type == CmResourceTypeInterrupt &&
            (descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
        {
            WDF_INTERRUPT_CONFIG irqConfig;
            WDF_INTERRUPT_CONFIG_INIT(&irqConfig, IVSHMEMInterruptISR, NULL);
            irqConfig.InterruptTranslated = descriptor;
            irqConfig.InterruptRaw = WdfCmResourceListGetDescriptor(ResourcesRaw, i);
            irqConfig.PassiveHandling = FALSE;

            NTSTATUS status = WdfInterruptCreate(Device, &irqConfig, WDF_NO_OBJECT_ATTRIBUTES,
                &deviceContext->interrupts[deviceContext->interruptsUsed]);

            if (!NT_SUCCESS(status))
            {
                DEBUG_ERROR("Call to WdfInterruptCreate failed: %08x", status);
                result = status;
                break;
            }
            ++deviceContext->interruptsUsed;
            continue;
        }
    }

    if (memIndex == 0)
        result = STATUS_DEVICE_HARDWARE_ERROR;

    if (NT_SUCCESS(result))
    {
        DEBUG_INFO("Shared Memory: %p, %u bytes", deviceContext->shmemAddr.PhysicalAddress, deviceContext->shmemAddr.NumberOfBytes);
        DEBUG_INFO("Interrupts   : %d", deviceContext->interruptsUsed);
    }

    return result;
}

NTSTATUS IVSHMEMEvtDeviceReleaseHardware(_In_ WDFDEVICE Device, _In_ WDFCMRESLIST ResourcesTranslated)
{
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PAGED_CODE();
    DEBUG_INFO("%s", __FUNCTION__);

    PDEVICE_CONTEXT deviceContext;
    deviceContext = DeviceGetContext(Device);

    if (deviceContext->devRegisters)
    {
        MmUnmapIoSpace(deviceContext->devRegisters, sizeof(PIVSHMEMDeviceRegisters));
    }

    if (deviceContext->shmemMap)
    {
        MmUnmapLockedPages(deviceContext->shmemMap, deviceContext->shmemMDL);
        deviceContext->shmemMap = NULL;
    }

    if (deviceContext->shmemMDL)
    {
        IoFreeMdl(deviceContext->shmemMDL);
        deviceContext->shmemMDL = NULL;
    }

    if (deviceContext->interrupts)
    {
        for (int i = 0; i < deviceContext->interruptsUsed; ++i)
            WdfObjectDelete(deviceContext->interrupts[i]);

        MmFreeNonCachedMemory(
            deviceContext->interrupts,
            sizeof(WDFINTERRUPT) * deviceContext->interruptCount
        );

        deviceContext->interruptCount = 0;
        deviceContext->interruptsUsed = 0;
        deviceContext->interrupts = NULL;
    }

    LIST_ENTRY *entry;
    while((entry = RemoveHeadList(&deviceContext->eventList)) != NULL)
    {
        PIVSHMEMEventListEntry event = CONTAINING_RECORD(entry, IVSHMEMEventListEntry, ListEntry);
        ObDereferenceObject(event->event);
        MmFreeNonCachedMemory(event, sizeof(IVSHMEMEventListEntry));
    }

    return STATUS_SUCCESS;
}

NTSTATUS IVSHMEMEvtD0Entry(_In_ WDFDEVICE Device, _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PreviousState);
    DEBUG_INFO("%s", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS IVSHMEMEvtD0Exit(_In_ WDFDEVICE Device, _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PreviousState);
    PAGED_CODE();
    DEBUG_INFO("%s", __FUNCTION__);
    return STATUS_SUCCESS;
}

BOOLEAN IVSHMEMInterruptISR(_In_ WDFINTERRUPT Interrupt, _In_ ULONG MessageID)
{
    WDFDEVICE device;
    PDEVICE_CONTEXT deviceContext;

    device = WdfInterruptGetDevice(Interrupt);
    deviceContext = DeviceGetContext(device);

    // there is no need to lock this list, we run at higher priority
    // and as such will not be interrupted by others that might try
    // to touch it

    PLIST_ENTRY entry = deviceContext->eventList.Flink;
    while (entry != &deviceContext->eventList)
    {
        PIVSHMEMEventListEntry event = CONTAINING_RECORD(entry, IVSHMEMEventListEntry, ListEntry);
        PLIST_ENTRY next = entry->Flink;
        if (event->vector == MessageID)
        {
            KeSetEvent(event->event, 0, FALSE);
            ObDereferenceObject(event->event);
            RemoveEntryList(entry);
            MmFreeNonCachedMemory(entry, sizeof(IVSHMEMEventListEntry));
        }
        entry = next;
    }

    return TRUE;
}
