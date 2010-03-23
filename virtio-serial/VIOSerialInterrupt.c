/**********************************************************************
 * Copyright (c) 2010  Red Hat, Inc.
 *
 * File: VIOSerialInterrupt.c
 *
 * Placeholder for the interrupt handling related functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"
#include "VIOSerialDevice.h"
#include "VIOSerialCore.h"
#include "VIOSerialCoreQueue.h"

BOOLEAN VIOSerialInterruptIsr(IN WDFINTERRUPT Interrupt,
							  IN ULONG MessageID)
{
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfInterruptGetDevice(Interrupt));
	ULONG status = 0;
	BOOLEAN b;

	if(!pContext->isDeviceInitialized)
	{
		return FALSE;
	}

	status = VirtIODeviceISR(&pContext->IODevice);
	if(status == VIRTIO_SERIAL_INVALID_INTERRUPT_STATUS)
	{
		status = 0;
	}

	if(!!status)
	{
		DPrintf(6, ("Got ISR - it is ours %d!", status));
		WdfInterruptQueueDpcForIsr(Interrupt);
	}

	return !!status;
}

VOID VIOSerialInterruptDpc(IN WDFINTERRUPT Interrupt,
						   IN WDFOBJECT AssociatedObject)
{
	//TBD handle the transfer
	unsigned int len;
	int i;
	KIRQL IRQL;
	pIODescriptor pBufferDescriptor;
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfInterruptGetDevice(Interrupt));
	
	DEBUG_ENTRY(5);

	KeAcquireSpinLock(&pContext->DPCLock, &IRQL);
	//Get consumed buffers for transmit queues
	for(i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i++ )
	{
		if(pContext->SerialPorts[i].SendQueue)
		{
			while(pBufferDescriptor = pContext->SerialPorts[i].SendQueue->vq_ops->get_buf(pContext->SerialPorts[i].SendQueue, &len))
			{
				RemoveEntryList(&pBufferDescriptor->listEntry); // Remove from in use list
				InsertTailList(&pContext->SerialPorts[i].SendFreeBuffers, &pBufferDescriptor->listEntry);
			}
		}
	}

	//Get control messages
	if(pContext->SerialPorts[VIRTIO_SERIAL_CONTROL_PORT_INDEX].ReceiveQueue)
	{
		if(pBufferDescriptor = pContext->SerialPorts[VIRTIO_SERIAL_CONTROL_PORT_INDEX].ReceiveQueue->vq_ops->get_buf(pContext->SerialPorts[VIRTIO_SERIAL_CONTROL_PORT_INDEX].ReceiveQueue, &len))
		{
			DPrintf(0, ("Got control message"));
			//HandleIncomingControlMessage(pBufferDescriptor->DataInfo.Virtual, len);

			//Return the buffer to usage... - if we handle the mesages in workitem, the below line should move there
			AddRxBufferToQueue(&pContext->SerialPorts[VIRTIO_SERIAL_CONTROL_PORT_INDEX], pBufferDescriptor);
			pContext->SerialPorts[VIRTIO_SERIAL_CONTROL_PORT_INDEX].ReceiveQueue->vq_ops->kick(pContext->SerialPorts[VIRTIO_SERIAL_CONTROL_PORT_INDEX].ReceiveQueue);
		}
	}

	KeReleaseSpinLock(&pContext->DPCLock, IRQL);
}

static VOID VIOSerialEnableDisableInterrupt(PDEVICE_CONTEXT pContext,
											IN BOOLEAN bEnable)
{
	int i;

	DEBUG_ENTRY(0);

	if(!pContext)
		return;

	for(i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i++ )
	{
		if(pContext->SerialPorts[i].ReceiveQueue)
		{
			pContext->SerialPorts[i].ReceiveQueue->vq_ops->enable_interrupt(pContext->SerialPorts[i].ReceiveQueue, bEnable);
		}

		if(pContext->SerialPorts[i].SendQueue)
		{
			pContext->SerialPorts[i].SendQueue->vq_ops->enable_interrupt(pContext->SerialPorts[i].SendQueue, bEnable);
		}
	}

	if(bEnable) // Also kick
	{
		for(i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i++ )
		{
			if(pContext->SerialPorts[i].ReceiveQueue)
			{
				pContext->SerialPorts[i].ReceiveQueue->vq_ops->kick(pContext->SerialPorts[i].ReceiveQueue);
			}

			if(pContext->SerialPorts[i].SendQueue)
			{
				pContext->SerialPorts[i].SendQueue->vq_ops->kick(pContext->SerialPorts[i].SendQueue);
			}
		}
	}
}

NTSTATUS VIOSerialInterruptEnable(IN WDFINTERRUPT Interrupt,
								  IN WDFDEVICE AssociatedDevice)
{
	DEBUG_ENTRY(0);
	VIOSerialEnableDisableInterrupt(GetDeviceContext(WdfInterruptGetDevice(Interrupt)), 
									TRUE);

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialInterruptDisable(IN WDFINTERRUPT Interrupt,
								   IN WDFDEVICE AssociatedDevice)
{
	DEBUG_ENTRY(0);
	VIOSerialEnableDisableInterrupt(GetDeviceContext(WdfInterruptGetDevice(Interrupt)),
									FALSE);

	return STATUS_SUCCESS;
}
