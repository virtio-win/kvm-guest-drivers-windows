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
#endif // ALLOC_PRAGMA

int nDebugLevel = 0;
int bDebugPrint = 0;

BOOLEAN 
LogError(
    __in PDRIVER_OBJECT  drvObj,
    __in NTSTATUS        ErrorCode
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
    ULONG ulValue;

    ulValue = READ_PORT_ULONG((PULONG)(ulRegister));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Rd R[%x] = %x\n", (ULONG)(ulRegister), ulValue);
    return ulValue;
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Wr R[%x] = %x\n", (ULONG)(ulRegister), ulValue);
    WRITE_PORT_ULONG( (PULONG)(ulRegister),(ULONG)(ulValue) );
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    u8 bValue;

    bValue = READ_PORT_UCHAR((PUCHAR)(ulRegister));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Rd R[%x] = %x\n", (ULONG)(ulRegister), bValue);
    return bValue;
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Wr R[%x] = %x\n", (ULONG)(ulRegister), bValue);
    WRITE_PORT_UCHAR((PUCHAR)(ulRegister),(UCHAR)(bValue));
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    u16 wValue;

    wValue = READ_PORT_USHORT((PUSHORT)(ulRegister));

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Rd R[%x] = %x\n", (ULONG)(ulRegister), wValue);

    return wValue;
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "Wr R[%x] = %x\n", (ULONG)(ulRegister), wValue);
    WRITE_PORT_USHORT((PUSHORT)(ulRegister),(USHORT)(wValue));
}
