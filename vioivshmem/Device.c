#include "driver.h"
#include "device.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOIVSHMEMCreateDevice)
#pragma alloc_text (PAGE, VIOIVSHMEMEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, VIOIVSHMEMEvtDeviceReleaseHardware)
#endif

NTSTATUS VIOIVSHMEMCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    PDEVICE_CONTEXT deviceContext;
    WDFDEVICE device;
    NTSTATUS status;

    PAGED_CODE();

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

	WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
	pnpPowerCallbacks.EvtDevicePrepareHardware = VIOIVSHMEMEvtDevicePrepareHardware;
	pnpPowerCallbacks.EvtDeviceReleaseHardware = VIOIVSHMEMEvtDeviceReleaseHardware;
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

	WDF_FILEOBJECT_CONFIG fileConfig;
	WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, NULL, NULL, VIOIVSHMEMEvtDeviceFileCleanup);
	WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

	if (!NT_SUCCESS(status))
	{
		DEBUG_ERROR("%s", "Call to WdfDeviceCreate failed");
		return status;
	}

    deviceContext = DeviceGetContext(device);
	RtlZeroMemory(deviceContext, sizeof(DEVICE_CONTEXT));

	status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_VIOIVSHMEM, NULL);

	if (!NT_SUCCESS(status))
	{
		DEBUG_ERROR("%s", "Call to WdfDeviceCreateDeviceInterface failed");
		return status;
	}

    status = VIOIVSHMEMQueueInitialize(device);

    return status;
}

NTSTATUS VIOIVSHMEMEvtDevicePrepareHardware(_In_ WDFDEVICE Device, _In_ WDFCMRESLIST ResourceRaw, _In_ WDFCMRESLIST ResourcesTranslated)
{
	UNREFERENCED_PARAMETER(ResourceRaw);
	UNREFERENCED_PARAMETER(ResourcesTranslated);

	PAGED_CODE();

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

		if (descriptor->Type == CmResourceTypeMemory)
		{
			// control registers
			if (memIndex == 0)
			{
				if (deviceContext->devRegisters)
				{
					DEBUG_ERROR("%s", "Found second resource of type CmResourceTypePort, only expected one.");
					status = STATUS_DEVICE_HARDWARE_ERROR;
					break;
				}

				if (descriptor->u.Memory.Length != sizeof(VIOIVSHMEMDeviceRegisters))
				{
					DEBUG_ERROR("Resource size was %u long when %u was expected",
						descriptor->u.Memory.Length, sizeof(VIOIVSHMEMDeviceRegisters));
					status = STATUS_DEVICE_HARDWARE_ERROR;
					break;
				}

				DEBUG_INFO("Registers    : %p", descriptor->u.Memory.Start);
				deviceContext->devRegisters = (PVIOIVSHMEMDeviceRegisters)MmMapIoSpace(
					descriptor->u.Memory.Start, descriptor->u.Memory.Length, MmNonCached);

				if (!deviceContext->devRegisters)
				{
					DEBUG_ERROR("%s", "Call to MmMapIoSpace failed");
					status = STATUS_DEVICE_HARDWARE_ERROR;
					break;
				}

				++memIndex;
				continue;
			}

			if (resCount == 7 && memIndex == 1)
			{
				DEBUG_INFO("MSI-X        : %p", descriptor->u.Memory.Start);
				//TODO
				++memIndex;
				continue;
			}

			// shared memory resource
			if ((resCount == 5 && memIndex == 1) || (resCount == 7 && memIndex == 2))
			{
				if (deviceContext->shmemMap)
				{
					DEBUG_ERROR("%s", "Found second resource of type CmResourceTypeMemory, only expected one.");
					status = STATUS_DEVICE_HARDWARE_ERROR;
					break;
				}

				DEBUG_INFO("Shared Memory: %p, %u bytes", descriptor->u.Memory.Start, descriptor->u.Memory.Length);
				deviceContext->shmemAddr.PhysicalAddress = descriptor->u.Memory.Start;
				deviceContext->shmemAddr.NumberOfBytes = descriptor->u.Memory.Length;
				if (!NT_SUCCESS(MmAllocateMdlForIoSpace(&deviceContext->shmemAddr, 1, &deviceContext->shmemMDL)))
				{
					DEBUG_ERROR("%s", "Call to MmAllocateMdlForIoSpace failed");
					status = STATUS_DEVICE_HARDWARE_ERROR;
					break;
				}

				++memIndex;
				continue;
			}

			continue;
		}

		if (descriptor->Type == CmResourceTypeInterrupt)
		{
			DEBUG_INFO("Interrupt    : %x", descriptor->u.Interrupt.Vector);
			//TODO
			continue;
		}
	}

	if (!NT_SUCCESS(status))
	{
		if (deviceContext->devRegisters)
		{
			MmUnmapIoSpace(deviceContext->devRegisters, sizeof(PVIOIVSHMEMDeviceRegisters));
			deviceContext->devRegisters = NULL;
		}

		if (deviceContext->shmemMDL)
		{
			IoFreeMdl(deviceContext->shmemMDL);
			deviceContext->shmemMDL = NULL;
		}
	}

	return status;
}

NTSTATUS VIOIVSHMEMEvtDeviceReleaseHardware(_In_ WDFDEVICE Device, _In_ WDFCMRESLIST ResourcesTranslated)
{
	UNREFERENCED_PARAMETER(ResourcesTranslated);

	PAGED_CODE();

	PDEVICE_CONTEXT deviceContext;
	deviceContext = DeviceGetContext(Device);

	if (deviceContext->devRegisters)
	{
		MmUnmapIoSpace(deviceContext->devRegisters, sizeof(PVIOIVSHMEMDeviceRegisters));
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

	return STATUS_SUCCESS;
}
