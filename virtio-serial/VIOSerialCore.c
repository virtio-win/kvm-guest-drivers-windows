#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"
#include "VIOSerialCore.h"
#include "VIOSerialMemUtils.h"

static void FreeBufferDescriptor(pIODescriptor pBufferDescriptor)
{
	if(pBufferDescriptor)
	{
		if (pBufferDescriptor->DataInfo.Virtual)
			FreePhysical(&pBufferDescriptor->DataInfo.Virtual);
		ExFreePoolWithTag(pBufferDescriptor, VIOSERIAL_DRIVER_MEMORY_TAG);
		pBufferDescriptor = NULL;
	}
}

static void FreeDescriptorsFromList(PLIST_ENTRY pListRoot, PKSPIN_LOCK pLock)
{
	pIODescriptor pBufferDescriptor;
	LIST_ENTRY TempList;
	KIRQL IRQL;

	InitializeListHead(&TempList);
	KeAcquireSpinLock(pLock, &IRQL);
	while(!IsListEmpty(pListRoot))
	{
		pBufferDescriptor = (pIODescriptor)RemoveHeadList(pListRoot);
		InsertTailList(&TempList, &pBufferDescriptor->listEntry);
	}
	KeReleaseSpinLock(pLock, IRQL);

	while(!IsListEmpty(&TempList))
	{
		pBufferDescriptor = (pIODescriptor)RemoveHeadList(&TempList);
		FreeBufferDescriptor(pBufferDescriptor);
	}
}

static pIODescriptor AllocateIOBuffer(ULONG size)
{
	pIODescriptor p;
	p = (pIODescriptor)ExAllocatePoolWithTag(NonPagedPool,
											 size,
											 VIOSERIAL_DRIVER_MEMORY_TAG);

	if (p)
	{
		BOOLEAN b1 = FALSE, b2 = FALSE;
		RtlZeroMemory(p, sizeof(IODescriptor));
		p->DataInfo.size = size;
		p->DataInfo.Virtual = AllocatePhysical(size);

		if(p->DataInfo.Virtual)
		{
			p->DataInfo.Physical = GetPhysicalAddress(p->DataInfo.Virtual);
			RtlZeroMemory(p->DataInfo.Virtual, p->DataInfo.size);
		}
		else
		{
			FreeBufferDescriptor(p);
			p = NULL;
			DPrintf(0, ("[INITPHYS](Failed to allocate physical memory block"));
		}
	}
	else
	{
		DPrintf(0, ("[INITPHYS] Failed to allocate IODescriptor!"));
	}

	if (p)
	{
		DPrintf(3, ("[INITPHYS] Data v%p(p%08lX)",
					p->DataInfo.Virtual, p->DataInfo.Physical.LowPart));
	}

	return p;
}

static BOOLEAN AddRxBufferToQueue(PVIOSERIAL_PORT pPort, pIODescriptor pBufferDescriptor)
{
	struct VirtIOBufferDescriptor sg[1];

	sg[0].physAddr = pBufferDescriptor->DataInfo.Physical;
	sg[0].ulSize = pBufferDescriptor->DataInfo.size;

	return 0 == pPort->ReceiveQueue->vq_ops->add_buf(pPort->ReceiveQueue,
													 sg,
													 0,
													 1,		// How many buffers to add
													 pBufferDescriptor);
}

// **********************************************************
// Allocates maximum RX buffers for incoming dara
// Buffers are chained in pPort->ReceiveBuffers
// Parameters:
//     PVIOSERIAL_PORT
// ***********************************************************
static int PrepareReceiveBuffers(PVIOSERIAL_PORT pPort)
{
	int nRet = 0;
	unsigned int i;
	DEBUG_ENTRY(4);

	for (i = 0; i < pPort->MaxReceiveBuffers; ++i)
	{
		pIODescriptor pBuffersDescriptor = AllocateIOBuffer(PAGE_SIZE);
		if (!pBuffersDescriptor) break;

		if (!AddRxBufferToQueue(pPort, pBuffersDescriptor))
		{
			FreeBufferDescriptor(pBuffersDescriptor);
			break;
		}

		InsertTailList(&pPort->ReceiveBuffers, &pBuffersDescriptor->listEntry);

		pPort->NofReceiveBuffers++;
	}

	DPrintf(0, ("[%s] Port %x, NofReceiveBuffers %d\n", __FUNCTION__, pPort, pPort->NofReceiveBuffers));

	return nRet;
}

static void PrepareTransmitBuffers(PVIOSERIAL_PORT pPort)
{
	unsigned int nBuffers, nMaxBuffers;
	DEBUG_ENTRY(4);
	nMaxBuffers = VirtIODeviceGetQueueSize(pPort->SendQueue);
	
	//TBD - if needed
	//if (nMaxBuffers > pContext->maxFreeTxDescriptors) nMaxBuffers = pContext->maxFreeTxDescriptors;

	for (nBuffers = 0; nBuffers < nMaxBuffers; ++nBuffers)
	{
		pIODescriptor pBufferDescriptor = AllocateIOBuffer(PAGE_SIZE);
		if (!pBufferDescriptor) break;

		InsertTailList(&pPort->SendFreeBuffers, &pBufferDescriptor->listEntry);
		pPort->NofSendFreeBuffers++;
	}

	DPrintf(0, ("[%s] available %d Tx descriptors for port %x",
		__FUNCTION__, pPort->NofSendFreeBuffers, pPort));
}

static void VSCCleanupQueues(IN WDFOBJECT WdfDevice)
{
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfDevice);
	int i;

	for(i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i++ )
	{
		/* TBD - check if needed
	// list NetReceiveBuffersWaiting must be free 
			do
			{
				NdisAcquireSpinLock(&pContext->ReceiveLock);
				b = !IsListEmpty(&pContext->NetReceiveBuffersWaiting);
				NdisReleaseSpinLock(&pContext->ReceiveLock);
				if (b)
				{
					DPrintf(0, ("[%s] There are waiting buffers", __FUNCTION__));
					PrintStatistics(pContext);
					NdisMSleep(5000000);
				}
			}while (b);
		*/

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

		// this can be freed, queue shut down
		FreeDescriptorsFromList(&pContext->SerialPorts[i].ReceiveBuffers,
								&pContext->DPCLock);

		// this can be freed, queue shut down
		FreeDescriptorsFromList(&pContext->SerialPorts[i].SendInUseBuffers,
								&pContext->DPCLock);

		// this can be freed, send disabled
		FreeDescriptorsFromList(&pContext->SerialPorts[i].SendFreeBuffers,
								&pContext->DPCLock);
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

	for(i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i++)
	{
		if(i == 1) // Control Port
		{
			pContext->SerialPorts[i].MaxReceiveBuffers = VIRTIO_SERIAL_MAX_CONTROL_RECEIVE_BUFFERS;
		}
		else
		{
			pContext->SerialPorts[i].MaxReceiveBuffers = VIRTIO_SERIAL_MAX_PORT_RECEIVE_BUFFERS;
		}

		pContext->SerialPorts[i].ReceiveQueue = VirtIODeviceFindVirtualQueue(&pContext->IODevice, i * 2, NULL);
		pContext->SerialPorts[i].SendQueue = VirtIODeviceFindVirtualQueue(&pContext->IODevice, (i * 2 ) + 1, NULL);

		if (pContext->SerialPorts[i].ReceiveQueue && pContext->SerialPorts[i].SendQueue)
		{
			PrepareTransmitBuffers(&pContext->SerialPorts[i]);
			PrepareReceiveBuffers(&pContext->SerialPorts[i]);
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
