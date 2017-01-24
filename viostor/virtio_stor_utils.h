/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
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
#include <storport.h>
#include <stdarg.h>
#include "kdebugprint.h"
#include "evntrace.h"

//#define DBG 1
//#define COM_DEBUG 1

#define CHECKBIT(value, nbit) virtio_is_feature_enabled(value, nbit)
#define CHECKFLAG(value, flag) ((value & (flag)) == flag)
#define SETFLAG(value, flag) (value |= (flag))

#define CACHE_LINE_SIZE 64
#define ROUND_TO_CACHE_LINES(Size)  (((ULONG_PTR)(Size) + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1))

int
_cdecl
_vsnprintf(
    char *buf,
    size_t size,
    const char *fmt,
    va_list args
    );
#define vsnprintf _vsnprintf

void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, PUNICODE_STRING RegistryPath);

extern int nViostorDebugLevel;

#if DBG
#define RhelDbgPrint(level, line) \
    if ((!bDebugPrint) || level > nViostorDebugLevel) {} \
    else VirtioDebugPrintProc line
#else
#define RhelDbgPrint(level, line) 
#endif

VOID
LogError(
    IN PVOID HwDeviceExtension,
    IN ULONG ErrorCode,
    IN ULONG UniqueId
    );

#endif ___VIOSTOR_UTILS_H___

