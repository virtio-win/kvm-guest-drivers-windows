/*
* This file contains debug print support routines and globals.
*
* Copyright (c) 2008-2018 Red Hat, Inc.
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

#ifndef ___VIOSTOR_TRACING_H___
#define ___VIOSTOR_TRACING_H___

//#define DBG 1

#include <ntddk.h>
#include <storport.h>
#include <stdarg.h>

//#define PRINT_DEBUG 1
//#define COM_DEBUG 1



#if !defined(DBG)
#define EVENT_TRACING 1
#endif

#if !defined(EVENT_TRACING)
#include "kdebugprint.h"

extern ULONG driverDebugFlags;
extern int driverDebugLevel;
extern int nViostorDebugLevel;

#if !defined(TRACE_LEVEL_NONE)
#define TRACE_LEVEL_NONE        0
#define TRACE_LEVEL_CRITICAL    1
#define TRACE_LEVEL_FATAL       1
#define TRACE_LEVEL_ERROR       2
#define TRACE_LEVEL_WARNING     3
#define TRACE_LEVEL_INFORMATION 4
#define TRACE_LEVEL_VERBOSE     5
#define TRACE_LEVEL_RESERVED6   6
#define TRACE_LEVEL_RESERVED7   7
#define TRACE_LEVEL_RESERVED8   8
#define TRACE_LEVEL_RESERVED9   9
#endif

#if DBG
#define RhelDbgPrint(Level, MSG, ...) \
    if ((!bDebugPrint) || Level > nViostorDebugLevel) {} \
    else VirtioDebugPrintProc (MSG, __VA_ARGS__)
#define VioStorDbgBreak()\
    if (KD_DEBUGGER_ENABLED && !KD_DEBUGGER_NOT_PRESENT) DbgBreakPoint();
#else
#define RhelDbgPrint(Level, MSG, ...)
#define VioStorDbgBreak()  {}
#endif

#else
#pragma warning(disable: 28170)
#pragma warning(disable: 28251)
#include <stortrce.h>

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(VioStorTraceGuid,(B17FA150,8C45,482E,9EB8,29611A862BF3), \
        WPP_DEFINE_BIT(TRACE_LEVEL_NONE)               /* bit  0 = 0x00000001 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_FATAL)              /* bit  1 = 0x00000002 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_ERROR)              /* bit  2 = 0x00000004 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_WARNING)            /* bit  3 = 0x00000008 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_INFORMATION)        /* bit  4 = 0x00000010 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_VERBOSE)            /* bit  5 = 0x00000020 */ \
        )

#define WPP_Flags_LEVEL_LOGGER(Flags, level)                                  \
    WPP_LEVEL_LOGGER(Flags)
#define WPP_Flags_LEVEL_ENABLED(Flags, level)                                 \
    (level <= virtioDebugLevel)

#define WPP_FLAGS_LEVEL_STATUS_LOGGER(Flags, level, status)                                  \
    WPP_LEVEL_LOGGER(Flags)
#define WPP_Flags_LEVEL_STATUS_ENABLED(Flags, level, status)                                 \
    (level <= virtioDebugLevel)

// begin_wpp config
// USEPREFIX (RhelDbgPrint, "%!STDPREFIX! %!FUNC!");
// FUNC RhelDbgPrint(LEVEL, MSG, ...);
// end_wpp

#define WPP_CHECK_FOR_NULL_STRING

#endif
#endif //___VIOSTOR_TRACING_H___
