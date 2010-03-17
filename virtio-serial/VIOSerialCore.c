#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"
#include "VIOSerialCore.h"

static void VSCCleanupQueues(IN WDFOBJECT WdfDevice)
{
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfDevice);
	int i;

	for(i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i++ )
	{
		if(pContext->SerialDevices[i].ReceiveQueue)
		{
			pContext->SerialDevices[i].ReceiveQueue->vq_ops->shutdown(pContext->SerialDevices[i].ReceiveQueue);
			VirtIODeviceDeleteVirtualQueue(pContext->SerialDevices[i].ReceiveQueue);
			pContext->SerialDevices[i].ReceiveQueue =  NULL;
		}

		if(pContext->SerialDevices[i].SendQueue)
		{
			pContext->SerialDevices[i].SendQueue->vq_ops->shutdown(pContext->SerialDevices[i].SendQueue);
			VirtIODeviceDeleteVirtualQueue(pContext->SerialDevices[i].SendQueue);
			pContext->SerialDevices[i].SendQueue =  NULL;
		}
	}
}

NTSTATUS VSCInit(IN WDFOBJECT WdfDevice)
{
	//TBD -----------------------

	NTSTATUS		status = STATUS_SUCCESS;
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfDevice);
	int i;

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

	for(i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i += 2)
	{
		pContext->SerialDevices[i].ReceiveQueue = VirtIODeviceFindVirtualQueue(&pContext->IODevice, i, NULL);
		pContext->SerialDevices[i].SendQueue = VirtIODeviceFindVirtualQueue(&pContext->IODevice, i + 1, NULL);

		if (pContext->SerialDevices[i].ReceiveQueue && pContext->SerialDevices[i].SendQueue)
		{
		//	PrepareTransmitBuffers(pContext);
		//	PrepareReceiveBuffers(pContext);
		}
		else
		{
			VSCCleanupQueues(WdfDevice);
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}
	}

	if(!NT_SUCCESS(status))
	{
		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_FAILED);
	}
	else
	{
		//TBD - clear it to enable transfer
//		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
	}

	return status;
}

NTSTATUS VSCDeinit(IN WDFOBJECT WdfDevice)
{
	NTSTATUS		status = STATUS_SUCCESS;
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfDevice);

	DEBUG_ENTRY(0);

	VirtIODeviceRemoveStatus(&pContext->IODevice , VIRTIO_CONFIG_S_DRIVER_OK);
	VSCCleanupQueues(WdfDevice);
	VirtIODeviceReset(&pContext->IODevice);

	//TBD
	//If we don't get InterruptEnable from framework- kick the queues

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
