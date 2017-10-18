#include "driver.h"
#include "device.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, IVSHMEMCreateDevice)
#pragma alloc_text (PAGE, IVSHMEMEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, IVSHMEMEvtDeviceReleaseHardware)
#pragma alloc_text (PAGE, IVSHMEMEvtD0Exit)
#pragma alloc_text (PAGE, IVSHMEMInterruptISR)
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

	status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_IVSHMEM, NULL);

	if (!NT_SUCCESS(status))
	{
		DEBUG_ERROR("%s", "Call to WdfDeviceCreateDeviceInterface failed");
		return status;
	}

    status = IVSHMEMQueueInitialize(device);

    return status;
}

NTSTATUS IVSHMEMEvtDevicePrepareHardware(_In_ WDFDEVICE Device, _In_ WDFCMRESLIST ResourcesRaw, _In_ WDFCMRESLIST ResourcesTranslated)
{
	PAGED_CODE();
	DEBUG_INFO("%s", __FUNCTION__);

	PDEVICE_CONTEXT deviceContext;
	deviceContext = DeviceGetContext(Device);

	NTSTATUS status = STATUS_SUCCESS;
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

	deviceContext->interrupts = (WDFINTERRUPT*)MmAllocateNonCachedMemory(
		sizeof(WDFINTERRUPT) * deviceContext->interruptCount);

	if (!deviceContext->interrupts)
	{
		DEBUG_ERROR("Failed to allocate space for %d interrupts", deviceContext->interrupts);
		return STATUS_DEVICE_CONFIGURATION_ERROR;
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
					status = STATUS_DEVICE_HARDWARE_ERROR;
					break;
				}

				deviceContext->devRegisters = (PIVSHMEMDeviceRegisters)MmMapIoSpace(
					descriptor->u.Memory.Start, descriptor->u.Memory.Length, MmNonCached);

				if (!deviceContext->devRegisters)
				{
					DEBUG_ERROR("%s", "Call to MmMapIoSpace failed");
					status = STATUS_DEVICE_HARDWARE_ERROR;
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
					status = STATUS_DEVICE_HARDWARE_ERROR;
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
			irqConfig.PassiveHandling = TRUE;
			irqConfig.InterruptTranslated = descriptor;
			irqConfig.InterruptRaw = WdfCmResourceListGetDescriptor(ResourcesRaw, i);
			if (!NT_SUCCESS(WdfInterruptCreate(Device, &irqConfig, WDF_NO_OBJECT_ATTRIBUTES,
				&deviceContext->interrupts[deviceContext->interruptsUsed++])))
			{
				DEBUG_ERROR("%s", "Call to WdfInterruptCreate failed");
				status = STATUS_DEVICE_HARDWARE_ERROR;
				break;
			}
			continue;
		}
	}

	if (memIndex == 0)
		status = STATUS_DEVICE_HARDWARE_ERROR;

	if (NT_SUCCESS(status))
	{
		DEBUG_INFO("Shared Memory: %p, %u bytes", deviceContext->shmemAddr.PhysicalAddress, deviceContext->shmemAddr.NumberOfBytes);
		DEBUG_INFO("Interrupts   : %d", deviceContext->interruptsUsed);
	}

	return status;
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
	DEBUG_INFO("%s", __FUNCTION__);

	device = WdfInterruptGetDevice(Interrupt);
	deviceContext = DeviceGetContext(device);

	UNREFERENCED_PARAMETER(device);
	UNREFERENCED_PARAMETER(deviceContext);
	UNREFERENCED_PARAMETER(MessageID);

	return TRUE;
}