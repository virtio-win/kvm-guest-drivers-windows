/**********************************************************************
 * Copyright (c) 2016  Red Hat, Inc.
 *
 * File: MemPortIO.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Implementation of memory & port access routines required by VirtioLib
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "virtio_pci.h"

/* The lower 64k of memory is never mapped so we can use the same routines
 * for both port I/O and memory access and use the address alone to decide
 * which space to use.
 */
#define PORT_MASK 0xFFFF

/* READ_REGISTER_* and WRITE_REGISTER_* take volatile ptr in newer DDKs */
#if NTDDI_VERSION >= NTDDI_WIN7
#define _VOLATILE volatile
#else
#define _VOLATILE
#endif

#define READ_PORT_OR_REGISTER(address, width)                       \
    (address & ~PORT_MASK) ?                                        \
        READ_REGISTER_##width((_VOLATILE width *)address) :         \
        READ_PORT_##width((width *)address)

#define WRITE_PORT_OR_REGISTER(address, width, value)               \
    (address & ~PORT_MASK) ?                                        \
        WRITE_REGISTER_##width((_VOLATILE width *)address, value) : \
        WRITE_PORT_##width((width *)address, value)

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    return READ_PORT_OR_REGISTER(ulRegister, UCHAR);
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    WRITE_PORT_OR_REGISTER(ulRegister, UCHAR, bValue);
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    return READ_PORT_OR_REGISTER(ulRegister, USHORT);
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    WRITE_PORT_OR_REGISTER(ulRegister, USHORT, wValue);
}

u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    return READ_PORT_OR_REGISTER(ulRegister, ULONG);
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    WRITE_PORT_OR_REGISTER(ulRegister, ULONG, ulValue);
}
