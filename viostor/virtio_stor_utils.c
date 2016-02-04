/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: virtio_stor_utils.c
 *
 * This file contains debug print routine implementation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include"virtio_stor_utils.h"
#include <ntstrsafe.h>

// Global debug printout level and enable\disable flag

// Global debug printout level and enable\disable flag
int virtioDebugLevel;
int bDebugPrint;
int nViostorDebugLevel;


#if defined(COM_DEBUG)

#define RHEL_DEBUG_PORT     ((PUCHAR)0x3F8)
#define TEMP_BUFFER_SIZE	256

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
        //
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
	nViostorDebugLevel = TRACE_LEVEL_ERROR;

#if defined(EVENT_TRACING)
	VirtioDebugPrintProc = DebugPrintFuncWPP;
#elif defined(PRINT_DEBUG)
	VirtioDebugPrintProc = DebugPrintFunc;
#elif defined(COM_DEBUG)
	VirtioDebugPrintProc = DebugPrintFuncSerial;
#else
	VirtioDebugPrintProc = NoDebugPrintFunc;
#endif
}

tDebugPrintFunc VirtioDebugPrintProc;
