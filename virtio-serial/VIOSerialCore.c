#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"
#include "VIOSerialCore.h"
#include "VIOSerialCoreQueue.h"

NTSTATUS VSCInit(IN WDFOBJECT WdfDevice)
{
	NTSTATUS		status = STATUS_SUCCESS;
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfDevice);


	DEBUG_ENTRY(0);

	if(pContext->pPortBase == NULL)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	VirtIODeviceSetIOAddress(&pContext->IODevice, (ULONG_PTR)pContext->pPortBase);
	VirtIODeviceDumpRegisters(&pContext->IODevice);
	VirtIODeviceReset(&pContext->IODevice);

	VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
	VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER);

	VSCInitQueues(pContext);

	if(!NT_SUCCESS(status))
	{
		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_FAILED);
	}
	else
	{
		//TBD - clear it to enable transfer
//		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
	}

	//TBD
	//If we don't get InterruptEnable from framework- kick the queues


	return status;
}

NTSTATUS VSCDeinit(IN WDFOBJECT WdfDevice)
{
	NTSTATUS		status = STATUS_SUCCESS;
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfDevice);

	DEBUG_ENTRY(0);

	VirtIODeviceRemoveStatus(&pContext->IODevice , VIRTIO_CONFIG_S_DRIVER_OK);
	VSCCleanupQueues(pContext);
	VirtIODeviceReset(&pContext->IODevice);

	return status;
}

NTSTATUS VSCGuestOpenedPort(/* TBD */)
{
	DEBUG_ENTRY(0);

	return STATUS_SUCCESS;
}

void VSCGuestClosedPort(/* TBD */)
{
	DEBUG_ENTRY(0);

}

NTSTATUS VSCSendData(/* TBD ,*/PVOID pBuffer, size_t *pSize)
{
	DEBUG_ENTRY(0);

	return STATUS_SUCCESS;
}

NTSTATUS VSCGetData(/* TBD ,*/WDFMEMORY * pMem, size_t *pSize)
{
	DEBUG_ENTRY(0);
	//WdfMemoryCopyFromBuffer

	return STATUS_SUCCESS;
}
