/**********************************************************************
 * Copyright (c) 2012  Red Hat, Inc.
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
#if defined(EVENT_TRACING)
#include "evntrace.h"
#endif

#define CHECKBIT(value, nbit) (((value) & (1 << (nbit))) != 0)

#if 1
#define ENTER_FN() RhelDbgPrint(TRACE_LEVEL_VERBOSE, (("--> %s.\n"),__FUNCTION__))
#define EXIT_FN()  RhelDbgPrint(TRACE_LEVEL_VERBOSE, (("<-- %s.\n"),__FUNCTION__))
#define EXIT_ERR() RhelDbgPrint(TRACE_LEVEL_VERBOSE, (("<--> %s (%d).\n"), __FUNCTION__, __LINE__))
#else
#define ENTER_FN()
#define EXIT_FN()
#define EXIT_ERR()
#endif

void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, IN PUNICODE_STRING RegistryPath);

extern int nViostorDebugLevel;

#ifdef DBG
#define ENABLE_TRACE 1
#endif

#ifdef ENABLE_TRACE
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
#define TRACE_LEVEL_FATAL       1   // Abnormal exit or termination
#define TRACE_LEVEL_ERROR       2   // Severe errors that need logging
#define TRACE_LEVEL_WARNING     3   // Warnings such as allocation failure
#define TRACE_LEVEL_INFORMATION 4   // Includes non-error cases(e.g.,Entry-Exit)
#define TRACE_LEVEL_VERBOSE     5   // Detailed traces from intermediate steps
#define TRACE_LEVEL_RESERVED6   6
#define TRACE_LEVEL_RESERVED7   7
#define TRACE_LEVEL_RESERVED8   8
#define TRACE_LEVEL_RESERVED9   9
#endif // TRACE_LEVEL_INFORMATION


#endif // ___UTILS_H___

