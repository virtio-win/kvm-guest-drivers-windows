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

static PVIOSERIAL_PORT MapFileToPort(PDEVICE_CONTEXT pContext)
{
	//TBD - for now always return first port
	return &pContext->SerialPorts[0];
}

void VSCGuestSetPortsReady(PDEVICE_CONTEXT pContext)
{
	int i;
	int nPortIndex;
	BOOLEAN bSendFor0 = TRUE;

	DEBUG_ENTRY(0);

	for (i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i++)
	{
		nPortIndex = VSCMapIndexToID(i);
		if(nPortIndex != 0 || bSendFor0 == TRUE) 
		{
			SendControlMessage(pContext, nPortIndex, VIRTIO_CONSOLE_PORT_READY, 1);
			bSendFor0 = FALSE;
		}
	}
}

NTSTATUS VSCGuestOpenedPort(PDEVICE_CONTEXT pContext)
{
	PVIOSERIAL_PORT pPort= MapFileToPort(pContext);

	DEBUG_ENTRY(0);

	if(pPort)
	{
		//Send control message...
		SendControlMessage(pContext, pPort->id, VIRTIO_CONSOLE_PORT_OPEN, 1);
		SendControlMessage(pContext, pPort->id, VIRTIO_CONSOLE_PORT_READY, 1);
	}
	else
	{
		return STATUS_UNSUCCESSFUL;
	}
	
	return STATUS_SUCCESS;
}

void VSCGuestClosedPort(PDEVICE_CONTEXT pContext)
{
	PVIOSERIAL_PORT pPort = MapFileToPort(pContext);
	
	DEBUG_ENTRY(0);

	if(pPort)
	{
		SendControlMessage(pContext, pPort->id, VIRTIO_CONSOLE_PORT_OPEN, 0);
	}
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
}

