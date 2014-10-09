/*
 * Virtio PCI driver
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Windows porting - Yan Vugenfirer <yvugenfi@redhat.com>
 *  StorPort/ScsiPort code adjustment Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "VirtIO_PCI.h"
#include "VirtIO.h"
#include "utils.h"
#include "vioscsi.h"


u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    return StorPortReadPortUlong(NULL, (PULONG)(ulRegister));
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    StorPortWritePortUlong(NULL, (PULONG)(ulRegister),(ULONG)(ulValue) );
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    return StorPortReadPortUchar(NULL, (PUCHAR)(ulRegister));
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    StorPortWritePortUchar(NULL, (PUCHAR)(ulRegister),(UCHAR)(bValue));
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    return StorPortReadPortUshort(NULL, (PUSHORT)(ulRegister));
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    StorPortWritePortUshort(NULL, (PUSHORT)(ulRegister),(USHORT)(wValue));
}
