#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"
#include "VIOSerialCore.h"


NTSTATUS VSCInit(IN WDFOBJECT WdfDevice)
{
	//TBD -----------------------

	NTSTATUS		status = STATUS_SUCCESS;
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfDevice);

	DPrintFunctionName(0);

	//Init Spin locks
	KeInitializeSpinLock(&pContext->DPCLock);

	VirtIODeviceSetIOAddress(&pContext->IODevice, (ULONG_PTR)pContext->PortBase);
	VirtIODeviceDumpRegisters(&pContext->IODevice);
	VirtIODeviceReset(&pContext->IODevice);

	VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
	VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER);


	//KeInitializeSpinLock();
	//TBD init queues
	//pContext->vqueue = = VirtIODeviceFindVirtualQueue(&pContext->IODevice, 1, NULL);
	//if (NULL == devCtx->pContext->vqueue) 
	//{
	//	status = STATUS_INSUFFICIENT_RESOURCES;
	//}
	//InitQueues(pContext);

	if(!NT_SUCCESS(status))
	{
		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_FAILED);
	}
	else
	{
//		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
	}

	return status;
}

NTSTATUS VSCDeinit(IN WDFOBJECT WdfDevice)
{
	NTSTATUS		status = STATUS_SUCCESS;
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfDevice);

	DPrintFunctionName(0);

	VirtIODeviceRemoveStatus(&pContext->IODevice , VIRTIO_CONFIG_S_DRIVER_OK);

	//TBD - clean up queues
	//for(,,) //clean all the queues
	//{
	//	if(pContext->VirtQueue) 
	//	{
	//		pContext->VirtQueue->vq_ops->shutdown(pContext->VirtQueue);
	//		VirtIODeviceDeleteVirtualQueue(pContext->VirtQueue);
	//		pContext->VirtQueue = NULL;
	//	}
	//}

	VirtIODeviceReset(&pContext->IODevice);

	return status;
}

NTSTATUS VSCGuestOpenedPort(/* TBD */)
{

	return STATUS_SUCCESS;
}

void VSCGuestClosedPort(/* TBD */)
{

}

NTSTATUS VSCSendData(/* TBD ,*/PVOID pBuffer, size_t *pSize)
{

	return STATUS_SUCCESS;
}

NTSTATUS VSCGetData(/* TBD ,*/WDFMEMORY * pMem, size_t *pSize)
{
	//WdfMemoryCopyFromBuffer

	return STATUS_SUCCESS;
}
