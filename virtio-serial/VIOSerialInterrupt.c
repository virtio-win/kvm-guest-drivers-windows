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

BOOLEAN VIOSerialInterruptIsr(IN WDFINTERRUPT Interrupt,
							  IN ULONG MessageID)
{
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfInterruptGetDevice(Interrupt));
	ULONG status;
	BOOLEAN b;

	status = VirtIODeviceISR(&pContext->IODevice);
	/* TBD - something we did in NetKVM (corner PM case), not sure it is needed it here
	if(status == VIRTIO_NET_INVALID_INTERRUPT_STATUS ||
	   pContext->powerState != NetDeviceStateD0)
	{
		status = 0;
	}
*/
	if(!!status)
	{
		WdfInterruptQueueDpcForIsr(Interrupt);
	}

	return !!status;
}

VOID VIOSerialInterruptDpc(IN WDFINTERRUPT Interrupt,
						   IN WDFOBJECT AssociatedObject)
{
	//TBD handle the transfer
}

static VOID VIOSerialEnableDisableInterrupt(IN WDFINTERRUPT Interrupt,
											IN BOOLEAN bEnable)
{
	PDEVICE_CONTEXT	pContext = GetDeviceContext(WdfInterruptGetDevice(Interrupt));
	int i;

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

NTSTATUS VIOSerialInterruptEnable(IN WDFINTERRUPT Interrupt,
								  IN WDFDEVICE AssociatedDevice)
{
	VIOSerialEnableDisableInterrupt(Interrupt, TRUE);

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialInterruptDisable(IN WDFINTERRUPT Interrupt,
								   IN WDFDEVICE AssociatedDevice)
{
	VIOSerialEnableDisableInterrupt(Interrupt, FALSE);

	return STATUS_SUCCESS;
}
