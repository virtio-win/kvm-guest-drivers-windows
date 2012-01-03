/**********************************************************************
 * Copyright (c) 2009  Red Hat, Inc.
 *
 * File: device.c
 *
 * Author(s):
 *
 * This file contains various vioserial driver routines
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "precomp.h"

#if defined(EVENT_TRACING)
#include "utils.tmh"
#endif

u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    return READ_PORT_ULONG((PULONG)(ulRegister));
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    WRITE_PORT_ULONG( (PULONG)(ulRegister),(ULONG)(ulValue) );
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    return READ_PORT_UCHAR((PUCHAR)(ulRegister));
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    WRITE_PORT_UCHAR((PUCHAR)(ulRegister),(UCHAR)(bValue));
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    return READ_PORT_USHORT((PUSHORT)(ulRegister));
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    WRITE_PORT_USHORT((PUSHORT)(ulRegister),(USHORT)(wValue));
}

ULONG_PTR GetVirtIODeviceAddr( PVOID pVirtIODevice )
{
	return ((VirtIODevice *)pVirtIODevice)->addr ;
}

void SetVirtIODeviceAddr(PVOID pVirtIODevice, ULONG_PTR addr)
{
	((VirtIODevice *)pVirtIODevice)->addr = addr;
}

struct virtqueue *VirtIODeviceFindVirtualQueue_InDrv(PVOID vp_dev,
                                                     unsigned index,
                                                     unsigned vector,
    						     bool (*callback)(struct virtqueue *vq),
						     PVOID Context,											                             PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
					             VOID (*freemem)(PVOID Context, PVOID Address, pmeminfo pmi ),
						     BOOLEAN Cached, BOOLEAN bPhysical)
{
	UNREFERENCED_PARAMETER(vp_dev);
	UNREFERENCED_PARAMETER(index);
	UNREFERENCED_PARAMETER(callback);
	UNREFERENCED_PARAMETER(Context);
	UNREFERENCED_PARAMETER(allocmem);
	UNREFERENCED_PARAMETER(freemem);
	UNREFERENCED_PARAMETER(Cached);
	UNREFERENCED_PARAMETER(bPhysical);

	return NULL;
}

int GetPciConfig(PVOID pVirtIODevice)
{
	UNREFERENCED_PARAMETER(pVirtIODevice);
	return VIRTIO_PCI_CONFIG;
}

PVOID drv_alloc_needed_mem(PVOID vdev, PVOID Context,
						   PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
						   ULONG size, pmeminfo pmi)
{
	UNREFERENCED_PARAMETER(vdev);
	return alloc_needed_mem(Context, allocmem, size, pmi);
}

PHYSICAL_ADDRESS GetPhysicalAddress(PVOID addr)
{
	return MmGetPhysicalAddress(addr);
}

ULONG
_cdecl
RhelDbgPrintToComPort(
    IN LPTSTR Format,
    ...
    )
{
    UNREFERENCED_PARAMETER(Format);
    return 0;
}
