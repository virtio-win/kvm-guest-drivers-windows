#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"
#include "VIOSerialCore.h"
#include "VIOSerialCoreQueue.h"
#include "VIOSerialCoreControl.h"

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
		DPrintf(0, ("Setting VIRTIO_CONFIG_S_FAILED flag"));
		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_FAILED);
	}
	else
	{
		DPrintf(0, ("Setting VIRTIO_CONFIG_S_DRIVER_OK flag"));
		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
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

	//Send control message...
	//SendControlMessage(, , VIRTIO_CONSOLE_PORT_OPEN, 1);
	//??? SendControlMessage(, , VIRTIO_CONSOLE_PORT_READY, 1);

	return STATUS_SUCCESS;
}

void VSCGuestClosedPort(/* TBD */)
{
	DEBUG_ENTRY(0);

	//SendControlMessage(, , VIRTIO_CONSOLE_PORT_OPEN, 0);
}

static PVIOSERIAL_PORT MapFileToPort(PDEVICE_CONTEXT pContext)
{
	//TBD - for now always return first port
	return &pContext->SerialPorts[0];
}

NTSTATUS VSCSendData(PDEVICE_CONTEXT pContext, PVOID pBuffer, size_t *pSize)
{
	unsigned int uiPortID;
	int i;
	NTSTATUS status = STATUS_SUCCESS;
	size_t sizeToSend = *pSize;
	size_t sizeChunk;

	DEBUG_ENTRY(0);

	//Will count acctual size sent
	*pSize = 0;
	
	while(*pSize < sizeToSend)
	{
		sizeChunk = sizeToSend > PAGE_SIZE? PAGE_SIZE : sizeToSend;
		if(!NT_SUCCESS(status = VSCSendCopyBuffer(MapFileToPort(pContext),
												  (unsigned char *)pBuffer + *pSize,
												  sizeChunk,
												  &pContext->DPCLock,
												  sizeToSend > PAGE_SIZE? FALSE : TRUE)))
		{
			break;
		}

		*pSize += sizeChunk;
		sizeToSend -= sizeChunk;
		
	}

	return status;
}

NTSTATUS VSCGetData(PDEVICE_CONTEXT pContext, WDFMEMORY * pMem, size_t *pSize)
{
	DEBUG_ENTRY(0);

	// For now let's assume only one chunck!

	return VSCRecieveCopyBuffer(MapFileToPort(pContext),
								pMem,
								pSize,
								&pContext->DPCLock);

	return STATUS_SUCCESS;
}

