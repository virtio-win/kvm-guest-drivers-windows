#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"
#include "VIOSerialCore.h"

/*
static void PrepareTransmitBuffers(PDEVICE_CONTEXT	pContext,
								   VIOSERIAL_PORT * pPort)
{
	UINT nBuffers, nMaxBuffers;
	DEBUG_ENTRY(4);
	nMaxBuffers = VirtIODeviceGetQueueSize(pContext->NetSendQueue) / 2;
	if (nMaxBuffers > pContext->maxFreeTxDescriptors) nMaxBuffers = pContext->maxFreeTxDescriptors;

	for (nBuffers = 0; nBuffers < nMaxBuffers; ++nBuffers)
	{
		pIONetDescriptor pBuffersDescriptor =
			AllocatePairOfBuffersOnInit(
				pContext,
				pContext->nVirtioHeaderSize,
				pContext->MaxPacketSize.nMaxFullSizeHwTx,
				TRUE);
		if (!pBuffersDescriptor) break;

		NdisZeroMemory(pBuffersDescriptor->HeaderInfo.Virtual, pBuffersDescriptor->HeaderInfo.size);
		InsertTailList(&pContext->NetFreeSendBuffers, &pBuffersDescriptor->listEntry);
		pContext->nofFreeTxDescriptors++;
	}

	pContext->maxFreeTxDescriptors = pContext->nofFreeTxDescriptors;
	pContext->nofFreeHardwareBuffers = pContext->nofFreeTxDescriptors * 2;
	pContext->maxFreeHardwareBuffers = pContext->minFreeHardwareBuffers = pContext->nofFreeHardwareBuffers;
	DPrintf(0, ("[%s] available %d Tx descriptors, %d hw buffers",
		__FUNCTION__, pContext->nofFreeTxDescriptors, pContext->nofFreeHardwareBuffers));
}
*/
static void VSCCleanupQueues(IN WDFOBJECT WdfDevice)
{
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfDevice);
	int i;

	for(i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i++ )
	{
		if(pContext->SerialPorts[i].ReceiveQueue)
		{
			pContext->SerialPorts[i].ReceiveQueue->vq_ops->shutdown(pContext->SerialPorts[i].ReceiveQueue);
			VirtIODeviceDeleteVirtualQueue(pContext->SerialPorts[i].ReceiveQueue);
			pContext->SerialPorts[i].ReceiveQueue = NULL;
		}

		if(pContext->SerialPorts[i].SendQueue)
		{
			pContext->SerialPorts[i].SendQueue->vq_ops->shutdown(pContext->SerialPorts[i].SendQueue);
			VirtIODeviceDeleteVirtualQueue(pContext->SerialPorts[i].SendQueue);
			pContext->SerialPorts[i].SendQueue = NULL;
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
		pContext->SerialPorts[i].ReceiveQueue = VirtIODeviceFindVirtualQueue(&pContext->IODevice, i, NULL);
		pContext->SerialPorts[i].SendQueue = VirtIODeviceFindVirtualQueue(&pContext->IODevice, i + 1, NULL);

		if (pContext->SerialPorts[i].ReceiveQueue && pContext->SerialPorts[i].SendQueue)
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
	VSCCleanupQueues(WdfDevice);
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
