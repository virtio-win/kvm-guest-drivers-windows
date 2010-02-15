/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * File: virtio_stor_utils.h
 *
 *
 * This file contains debug print support routines and globals.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef ___VIOSTOR_UTILS_H___
#define ___VIOSTOR_UTILS_H___


#include <ntddk.h>
#ifdef USE_STORPORT
#define STOR_USE_SCSI_ALIASES
#include <storport.h>
#else
#include <scsi.h>
#endif
#include <stdarg.h>

int
_cdecl
_vsnprintf(
    char *buf,
    size_t size,
    const char *fmt,
    va_list args
    );
#define vsnprintf _vsnprintf

///////////////////
// DEBUG SUPPORT //
///////////////////
extern ULONG RhelDbgLevel;

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


//
// Just use the base of COM1
//
#define RHEL_DEBUG_PORT     ((PUCHAR)0x3F8)

ULONG
_cdecl
RhelDbgPrintToComPort(
    __in LPTSTR Format,
    ...
    );

#if DBG
#if RHEL_COM_DEBUG
#define RhelDbgPrint(__LEVEL__,__MSG__) \
    if (RhelDbgLevel >= (__LEVEL__)) {  \
        RhelDbgPrintToComPort __MSG__;   \
    }
#else
#define RhelDbgPrint(__LEVEL__,__MSG__) \
    if (RhelDbgLevel >= (__LEVEL__)) {  \
        DbgPrint __MSG__;               \
    }
#endif RHEL_COM_DEBUG
#else DBG
#define RhelDbgPrint(__LEVEL__,__MSG__)
#endif DBG

#endif ___VIOSTOR_UTILS_H___

