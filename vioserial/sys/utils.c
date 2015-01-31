/**********************************************************************
 * Copyright (c) 2010-2015 Red Hat, Inc.
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
    UNREFERENCED_PARAMETER(format);
}

static void NoDebugPrintFunc(const char *format, ...)
{
    UNREFERENCED_PARAMETER(format);
}


void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, PUNICODE_STRING RegistryPath)
{
    //TODO - Read nDebugLevel and bDebugPrint from the registry
#if defined(EVENT_TRACING)
    VirtioDebugPrintProc = DebugPrintFuncWPP;
#elif defined(PRINT_DEBUG)
    VirtioDebugPrintProc = DebugPrintFunc;
#elif defined(COM_DEBUG)
    VirtioDebugPrintProc = DebugPrintFuncSerial;
#else
    VirtioDebugPrintProc = NoDebugPrintFunc;
#endif
    driverDebugFlags = 0xffffffff;

    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    bDebugPrint = 1;
    driverDebugLevel = TRACE_LEVEL_FATAL;
    virtioDebugLevel = -1;
}

tDebugPrintFunc VirtioDebugPrintProc;
