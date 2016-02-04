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
#ifdef USE_STORPORT
#define STOR_USE_SCSI_ALIASES
#include <storport.h>
#else
#include <scsi.h>
#endif
#include <stdarg.h>
#include "kdebugprint.h"
#include "evntrace.h"

#define CHECKBIT(value, nbit) (((value) & (1 << (nbit))) != 0)

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

#endif ___VIOSTOR_UTILS_H___

