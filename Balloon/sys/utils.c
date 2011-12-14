/**********************************************************************
 * Copyright (c) 2009  Red Hat, Inc.
 *
 * File: device.c
 *
 * Author(s):
 *
 * This file contains various balloon driver routines
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "precomp.h"

#if defined(EVENT_TRACING)
#include "utils.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGESRP0,LogError)
#endif

int nDebugLevel = 0;
int bDebugPrint = 0;

BOOLEAN
LogError(
    IN PDRIVER_OBJECT  drvObj,
    IN NTSTATUS        ErrorCode
   )
{
    PIO_ERROR_LOG_PACKET Packet;

    UCHAR PacketSize;

    PAGED_CODE();

    if (drvObj == NULL) return FALSE;

    PacketSize = sizeof(IO_ERROR_LOG_PACKET);
    Packet = IoAllocateErrorLogEntry(drvObj,PacketSize);
    if (Packet == NULL) return FALSE;

    Packet->ErrorCode         = ErrorCode;
    Packet->UniqueErrorValue  = 0,
    Packet->RetryCount        = 0;
    Packet->SequenceNumber    = 0;
    Packet->IoControlCode     = 0;
    Packet->DumpDataSize      = 0;
    Packet->MajorFunctionCode = 0;
    Packet->FinalStatus       = 0;
    IoWriteErrorLogEntry(Packet);
    return TRUE;
}

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
