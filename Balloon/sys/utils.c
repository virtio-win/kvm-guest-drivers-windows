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
#define     TEMP_BUFFER_SIZE        256
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

// Global debug printout level and enable\disable flag
int virtioDebugLevel;
int bDebugPrint;
int driverDebugLevel;
ULONG driverDebugFlags;



#if defined(COM_DEBUG)

#define RHEL_DEBUG_PORT     ((PUCHAR)0x3F8)

static void DebugPrintFuncSerial(const char *format, ...)
{
	char buf[TEMP_BUFFER_SIZE];
	NTSTATUS status;
	size_t len;
	va_list list;
	va_start(list, format);
	status = RtlStringCbVPrintfA(buf, sizeof(buf), format, list);
	if (status == STATUS_SUCCESS) 
	{
		len = strlen(buf);
	}
	else
	{
		len = 2;
		buf[0] = 'O';
		buf[1] = '\n';
	}
	if (len)
	{
        WRITE_PORT_BUFFER_UCHAR(RHEL_DEBUG_PORT, (PUCHAR)buf, len);
        WRITE_PORT_UCHAR(RHEL_DEBUG_PORT, '\r');
	}
}
#endif

#if defined(PRINT_DEBUG)
static void DebugPrintFunc(const char *format, ...)
{
	va_list list;
	va_start(list, format);
	vDbgPrintEx(DPFLTR_DEFAULT_ID, 9 | DPFLTR_MASK, format, list);
}
#endif

static void DebugPrintFuncWPP(const char *format, ...)
{
	// TODO later, if needed
}

static void NoDebugPrintFunc(const char *format, ...)
{

}


void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, PUNICODE_STRING RegistryPath)
{
    //TBD - Read nDebugLevel and bDebugPrint from the registry
    bDebugPrint = 1;
    virtioDebugLevel = 0;
#if defined(EVENT_TRACING)
	VirtioDebugPrintProc = DebugPrintFuncWPP;
#elif defined(PRINT_DEBUG)
	VirtioDebugPrintProc = DebugPrintFunc;
#elif defined(COM_DEBUG)
	VirtioDebugPrintProc = DebugPrintFuncSerial;
#else
	VirtioDebugPrintProc = NoDebugPrintFunc;
#endif
	driverDebugLevel = TRACE_LEVEL_INFORMATION;
	driverDebugFlags = 0xff;

	driverDebugLevel = TRACE_LEVEL_VERBOSE;
    virtioDebugLevel = 4;
}

tDebugPrintFunc VirtioDebugPrintProc;
