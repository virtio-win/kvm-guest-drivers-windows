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
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "VirtIO_PCI.h"
#include "virtio_stor_utils.h"
#include "virtio_stor.h"

/* The lower 64k of memory is never mapped so we can use the same routines
 * for both port I/O and memory access and use the address alone to decide
 * which space to use.
 */
#define PORT_MASK 0xFFFF

u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return ScsiPortReadRegisterUlong((PULONG)(ulRegister));
    } else {
        return ScsiPortReadPortUlong((PULONG)(ulRegister));
    }
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    if (ulRegister & ~PORT_MASK) {
        ScsiPortWriteRegisterUlong((PULONG)(ulRegister), (ULONG)(ulValue));
    } else {
        ScsiPortWritePortUlong((PULONG)(ulRegister), (ULONG)(ulValue));
    }
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return ScsiPortReadRegisterUchar((PUCHAR)(ulRegister));
    } else {
        return ScsiPortReadPortUchar((PUCHAR)(ulRegister));
    }
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    if (ulRegister & ~PORT_MASK) {
        ScsiPortWriteRegisterUchar((PUCHAR)(ulRegister), (UCHAR)(bValue));
    } else {
        ScsiPortWritePortUchar((PUCHAR)(ulRegister), (UCHAR)(bValue));
    }
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return ScsiPortReadRegisterUshort((PUSHORT)(ulRegister));
    } else {
        return ScsiPortReadPortUshort((PUSHORT)(ulRegister));
    }
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    if (ulRegister & ~PORT_MASK) {
        ScsiPortWriteRegisterUshort((PUSHORT)(ulRegister), (USHORT)(wValue));
    } else {
        ScsiPortWritePortUshort((PUSHORT)(ulRegister), (USHORT)(wValue));
    }
}
