#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"
#include "VIOSerialCore.h"
#include "VIOSerialCoreQueue.h"
#include "VIOSerialCoreControl.h"

PDEVICE_CONTEXT GetContextFromFileObject(IN WDFFILEOBJECT FileObject)
{
	PDEVICE_CONTEXT pContext = GetDeviceContext(WdfFileObjectGetDevice(FileObject));

	return pContext;
}

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

	VirtIODeviceGet(&pContext->IODevice,
					0,
					&pContext->consoleConfig,
					sizeof(VirtIOConsoleConfig));

	DPrintf(0 ,("VirtIOConsoleConfig->nr_ports %d", pContext->consoleConfig.nr_ports));
	DPrintf(0 ,("VirtIOConsoleConfig->max_nr_ports %d", pContext->consoleConfig.max_nr_ports));

	//Also count control queues
	pContext->consoleConfig.nr_ports +=1;

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

	if(pContext->isHostMultiport = VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_CONSOLE_F_MULTIPORT))
	{
		DPrintf(0, ("We have multiport host"));
		VirtIODeviceEnableGuestFeature(&pContext->IODevice, VIRTIO_CONSOLE_F_MULTIPORT);
	}

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

PVIOSERIAL_PORT MapFileToPort(WDFFILEOBJECT FileObject)
{
	//TBD - for now always return first port
	return &GetContextFromFileObject(FileObject)->SerialPorts[0];
}

void VSCGuestSetPortsReady(PDEVICE_CONTEXT pContext)
{
	unsigned int i;
	int nPortIndex;
	BOOLEAN bSendFor0 = TRUE;

	DEBUG_ENTRY(0);

	for (i = 0; i < pContext->consoleConfig.nr_ports; i++)
	{
		nPortIndex = VSCMapIndexToID(i);
		if(nPortIndex != 0 || bSendFor0 == TRUE) 
		{
			SendControlMessage(pContext, nPortIndex, VIRTIO_CONSOLE_PORT_READY, 1);
			bSendFor0 = FALSE;
		}
	}
}

NTSTATUS VSCGuestOpenedPort(WDFFILEOBJECT FileObject, PDEVICE_CONTEXT pContext)
{
	PVIOSERIAL_PORT pPort= MapFileToPort(FileObject);

	DEBUG_ENTRY(0);

	if(pPort)
	{
		//Send control message...
		SendControlMessage(pContext, pPort->id, VIRTIO_CONSOLE_PORT_OPEN, 1);
	}
	else
	{
		return STATUS_UNSUCCESSFUL;
	}
	
	return STATUS_SUCCESS;
}

void VSCGuestClosedPort(WDFFILEOBJECT FileObject, PDEVICE_CONTEXT pContext)
{
	PVIOSERIAL_PORT pPort = MapFileToPort(FileObject);
	
	DEBUG_ENTRY(0);

	if(pPort)
	{
		SendControlMessage(pContext, pPort->id, VIRTIO_CONSOLE_PORT_OPEN, 0);
	}
}

NTSTATUS VSCSendData(WDFFILEOBJECT FileObject, PDEVICE_CONTEXT pContext, PVOID pBuffer, size_t *pSize)
{
	unsigned int uiPortID;
	int i;
	NTSTATUS status = STATUS_SUCCESS;
	size_t sizeToSend = *pSize;
	size_t sizeChunk;

	DEBUG_ENTRY(0);

	//Will count acctual size sent
	*pSize = 0;
	
	while(sizeToSend)
	{
		//DPrintf(0, ("how much to send %d", sizeToSend));
		sizeChunk = sizeToSend > PAGE_SIZE? PAGE_SIZE : sizeToSend;
		if(!NT_SUCCESS(status = VSCSendCopyBuffer(MapFileToPort(FileObject),
												  (unsigned char *)pBuffer + *pSize,
												  sizeChunk,
												  pContext->DPCLock,
												  sizeToSend > PAGE_SIZE? FALSE : TRUE)))
		{
			break;
		}

		*pSize += sizeChunk;
		sizeToSend -= sizeChunk;

		//DPrintf(0, ("Last sent %d, total sent %d", sizeChunk, *pSize));
		
	}

	return status;
}

NTSTATUS VSCGetData(WDFFILEOBJECT FileObject, PDEVICE_CONTEXT pContext, WDFMEMORY * pMem, size_t *pSize)
{
	DEBUG_ENTRY(0);

	// For now let's assume only one chunck!

	return VSCRecieveCopyBuffer(MapFileToPort(FileObject),
								pMem,
								pSize,
								pContext->DPCLock,
								FALSE);
}

void VIOSerialQueueRequest(IN PDEVICE_CONTEXT pContext,
						   IN WDFFILEOBJECT FileObject,
						   IN WDFREQUEST Request)
{
	PVIOSERIAL_PORT pPort = MapFileToPort(FileObject);

	if(pPort)
	{
		pPort->lastReadRequest = Request;
	}
}
