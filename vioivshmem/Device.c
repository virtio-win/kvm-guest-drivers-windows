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
		return status;

    deviceContext = DeviceGetContext(device);
	RtlZeroMemory(deviceContext, sizeof(DEVICE_CONTEXT));

	status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_VIOIVSHMEM, NULL);

	if (!NT_SUCCESS(status))
		return status;

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

	ULONG memoryIndex = 0;
	for (ULONG i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); ++i)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
		descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
		if (!descriptor)
			return STATUS_DEVICE_CONFIGURATION_ERROR;

		if (descriptor->Type != CmResourceTypeMemory)
			continue;

		// BAR2 is the shared memory segment
		if (memoryIndex == 1)
		{
			deviceContext->shmemAddr.PhysicalAddress = descriptor->u.Memory.Start;
			deviceContext->shmemAddr.NumberOfBytes   = descriptor->u.Memory.Length;
			if (!NT_SUCCESS(MmAllocateMdlForIoSpace(&deviceContext->shmemAddr, 1, &deviceContext->shmemMDL)))
				return STATUS_DEVICE_HARDWARE_ERROR;
			break;
		}

		++memoryIndex;
	}

	return STATUS_SUCCESS;
}

NTSTATUS VIOIVSHMEMEvtDeviceReleaseHardware(_In_ WDFDEVICE Device, _In_ WDFCMRESLIST ResourcesTranslated)
{
	UNREFERENCED_PARAMETER(ResourcesTranslated);

	PAGED_CODE();

	PDEVICE_CONTEXT deviceContext;
	deviceContext = DeviceGetContext(Device);

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
