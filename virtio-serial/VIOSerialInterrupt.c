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

NTSTATUS VIOSerialInterruptEnable(IN WDFINTERRUPT Interrupt,
								  IN WDFDEVICE AssociatedDevice)
{
	//TBD

	return STATUS_SUCCESS;
}
//vring_enable_interrupts
NTSTATUS VIOSerialInterruptDisable(IN WDFINTERRUPT Interrupt,
								   IN WDFDEVICE AssociatedDevice)
{
	//TBD

	return STATUS_SUCCESS;
}
