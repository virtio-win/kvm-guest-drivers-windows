/**********************************************************************
 * Copyright (c) 2012-2016 Red Hat, Inc.
 *
 * File: utils.h
 *
 *
 * This file contains debug print support routines and globals.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef ___UTILS_H___
#define ___UTILS_H___


#include <ntddk.h>
#include <storport.h>
#include <stdarg.h>
#include "kdebugprint.h"
#include "evntrace.h"
#include "helper.h"

#define CHECKBIT(value, nbit) (((value) & (1 << (nbit))) != 0)
#define CHECKFLAG(value, flag) ((value & (flag)) == flag)
#define SETFLAG(value, flag) (value |= (flag))

#if 0
#define ENTER_FN() RhelDbgPrint(TRACE_LEVEL_VERBOSE, (("--> %s.\n"),__FUNCTION__))
#define EXIT_FN()  RhelDbgPrint(TRACE_LEVEL_VERBOSE, (("<-- %s.\n"),__FUNCTION__))
#define EXIT_ERR() RhelDbgPrint(TRACE_LEVEL_ERROR, (("<--> %s (%d).\n"), __FUNCTION__, __LINE__))
#define CHECK_CPU(Srb) { \
    PROCESSOR_NUMBER    ProcNumber; \
    ULONG               processor = KeGetCurrentProcessorNumberEx(&ProcNumber); \
    PSRB_EXTENSION srbExt  = SRB_EXTENSION(Srb); \
    if (ProcNumber.Group != srbExt->procNum.Group || \
        ProcNumber.Number != srbExt->procNum.Number) { \
           RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s Srb %p issued on %d::%d currentn %d::%d\n", \
                   __FUNCTION__, Srb, srbExt->procNum.Group, srbExt->procNum.Number, ProcNumber.Group, ProcNumber.Number)); \
    } \
}while (0);

#else
#define ENTER_FN()
#define EXIT_FN()
#define EXIT_ERR()
#define CHECK_CPU(Srb)
#endif

void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, IN PUNICODE_STRING RegistryPath);

extern int nViostorDebugLevel;

#define DBG 1
#define COM_DEBUG 1

#if DBG
int
_cdecl
_vsnprintf(
    char *buf,
    size_t size,
    const char *fmt,
    va_list args
    );
#define vsnprintf _vsnprintf

#define RhelDbgPrint(level, line) \
    if ((!bDebugPrint) || level > nViostorDebugLevel) {} \
    else VirtioDebugPrintProc line
#else
#define RhelDbgPrint(level, line) 
#endif

char *DbgGetScsiOpStr(PSCSI_REQUEST_BLOCK Srb);


///////////////////
// DEBUG SUPPORT //
///////////////////

#ifndef TRACE_LEVEL_INFORMATION
#define TRACE_LEVEL_NONE        0   // Tracing is not on
#define TRACE_LEVEL_CRITICAL    1   // Abnormal exit or termination
#define TRACE_LEVEL_FATAL       1   // Deprecated name for Abnormal exit or termination
#define TRACE_LEVEL_ERROR       2   // Severe errors that need logging
#define TRACE_LEVEL_WARNING     3   // Warnings such as allocation failure
#define TRACE_LEVEL_INFORMATION 4   // Includes non-error cases(e.g.,Entry-Exit)
#define TRACE_LEVEL_VERBOSE     5   // Detailed traces from intermediate steps
#define TRACE_LEVEL_RESERVED6   6
#define TRACE_LEVEL_RESERVED7   7
#define TRACE_LEVEL_RESERVED8   8
#define TRACE_LEVEL_RESERVED9   9
#endif // TRACE_LEVEL_INFORMATION


#endif ___UTILS_H___

