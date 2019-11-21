/*
 * This file contains various vioinput driver routines
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "precomp.h"
#include "vioinput.h"

#if defined(EVENT_TRACING)
#include "utils.tmh"
#endif

#define     TEMP_BUFFER_SIZE        256

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
    va_end(list);
}
#endif

#if defined(PRINT_DEBUG)
static void DebugPrintFunc(const char *format, ...)
{
    va_list list;
    va_start(list, format);
    vDbgPrintEx(DPFLTR_DEFAULT_ID, 9 | DPFLTR_MASK, format, list);
    va_end(list);
}
#endif

static void DebugPrintFuncWPP(const char *format, ...)
{
    char buf[256];
    NTSTATUS status;
    va_list list;
    va_start(list, format);
    status = RtlStringCbVPrintfA(buf, sizeof(buf), format, list);
    if (status == STATUS_SUCCESS)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "%s", buf);
    }
    va_end(list);
}

static void NoDebugPrintFunc(const char *format, ...)
{
    UNREFERENCED_PARAMETER(format);
}


void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, PUNICODE_STRING RegistryPath)
{
    WPP_INIT_TRACING(DriverObject, RegistryPath);
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
    driverDebugLevel = TRACE_LEVEL_INFORMATION;
    virtioDebugLevel = 1;
}

tDebugPrintFunc VirtioDebugPrintProc;
