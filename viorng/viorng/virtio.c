/*
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Refer to the LICENSE file for full details of the license.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 */

#include "viorng.h"

#include "virtio.tmh"

static void NoDebugPrintFunc(const char *format, ...)
{
    UNREFERENCED_PARAMETER(format);
}

typedef void (*tDebugPrintFunc)(const char *format, ...);
tDebugPrintFunc VirtioDebugPrintProc = NoDebugPrintFunc;

int virtioDebugLevel = 0;
int bDebugPrint = 0;

u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    return READ_PORT_ULONG((PULONG)(ulRegister));
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    WRITE_PORT_ULONG((PULONG)(ulRegister), (ULONG)(ulValue));
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    return READ_PORT_UCHAR((PUCHAR)(ulRegister));
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    WRITE_PORT_UCHAR((PUCHAR)(ulRegister), (UCHAR)(bValue));
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    return READ_PORT_USHORT((PUSHORT)(ulRegister));
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    WRITE_PORT_USHORT((PUSHORT)(ulRegister), (USHORT)(wValue));
}
