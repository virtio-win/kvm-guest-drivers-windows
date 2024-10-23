/*
* This file contains debug print support routines and globals.
*
* Copyright (c) 2012-2018 Red Hat, Inc.
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

#ifndef ___TRACING_H___
#define ___TRACING_H___

/* RUN_UNCHECKED - NO DEBUG OR TRACING
 * This option disables all debug messages and ETW for maximum performance.*/
#define RUN_UNCHECKED 1
#undef RUN_UNCHECKED // <== switch me here with comment

/* RUN_MIN_CHECKED - MINIMUM DEBUG OR FULL TRACING
 * This option includes select conditionally compiled debug messages.
 * However, if ETW is enabled instead of DEBUG, all traces will be included 
 * regardless of any compile-time conditional statements because the WPP will 
 * scan for all events and will ignore compile-time conditional statements. */
#define RUN_MIN_CHECKED 1
#undef RUN_MIN_CHECKED // <== switch me here with comment

/* EVENT_TRACING - CHOOSE BETWEEN DEBUG OR TRACING
 * if EVENT_TRACING is undefined, DEBUG (DBG) will be enabled below... */
#define EVENT_TRACING 1
//#undef EVENT_TRACING  // <== switch me here with comment

#include <ntddk.h>
#include <storport.h>
#include <stdarg.h>
#include "kdebugprint.h"

#define UCHAR_MAX 0xFF
#define DbgGetScsiOp(Srb) (SRB_CDB(Srb) ? SRB_CDB(Srb)->CDB6GENERIC.OperationCode : UCHAR_MAX)

char *DbgGetScsiOpStr(IN UCHAR opCode);
void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, IN PUNICODE_STRING RegistryPath);

#if !defined(EVENT_TRACING)
#define DBG 1
#define PRINT_DEBUG 1
#if !defined(PRINT_DEBUG)
#define COM_DEBUG 1
#endif
#endif

#if !defined(EVENT_TRACING)

extern ULONG driverDebugFlags;
extern int driverDebugLevel;
extern int nVioscsiDebugLevel;

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
#define TRACE_VQ                10
#define TRACE_LOCKS             11
#define TRACE_DPC               12
#define TRACE_MSIX              13
#define TRACE_ENTER_EXIT        14
#define TRACE_NOTIFY            15
#define TRACE_GUEST_FEATURES    16
#define TRACE_PCI_CAP           17
#define TRACE_MAPPING           18
#define TRACE_MSIX_CPU_AFFINITY 19
#define TRACE_REGISTRY          20
#define TRACE_ALL               31
#endif

#if DBG
#define RhelDbgPrint(Level, MSG, ...) \
    if ((!bDebugPrint) || Level > nVioscsiDebugLevel) {} \
    else VirtioDebugPrintProc (MSG, __VA_ARGS__)
#define VioScsiDbgBreak()\
    if (KD_DEBUGGER_ENABLED && !KD_DEBUGGER_NOT_PRESENT) DbgBreakPoint();
#else
#define RhelDbgPrint(Level, MSG, ...)
#define VioScsiDbgBreak()
#endif

#define ENTER_FN()  RhelDbgPrint(TRACE_LEVEL_VERBOSE, " --> %s.\n",__FUNCTION__)
#define EXIT_FN()  RhelDbgPrint(TRACE_LEVEL_VERBOSE, " <-- %s.\n",__FUNCTION__)
#define ENTER_INL_FN()  RhelDbgPrint(TRACE_LEVEL_VERBOSE, " --> %s.\n",__FUNCTION__)
#define EXIT_INL_FN()  RhelDbgPrint(TRACE_LEVEL_VERBOSE, " <-- %s.\n",__FUNCTION__)
#define EXIT_ERR() RhelDbgPrint(TRACE_LEVEL_ERROR, " <--> %s (%d).\n", __FUNCTION__, __LINE__)
#define ENTER_FN_SRB() RhelDbgPrint(TRACE_LEVEL_VERBOSE, " --> %s Srb = 0x%p.\n",__FUNCTION__, Srb)
#define EXIT_FN_SRB()  RhelDbgPrint(TRACE_LEVEL_VERBOSE, " <-- %s Srb = 0x%p.\n",__FUNCTION__, Srb)
#define ENTER_INL_FN_SRB() RhelDbgPrint(TRACE_LEVEL_VERBOSE, " --> %s Srb = 0x%p.\n",__FUNCTION__, Srb)
#define EXIT_INL_FN_SRB()  RhelDbgPrint(TRACE_LEVEL_VERBOSE, " <-- %s Srb = 0x%p.\n",__FUNCTION__, Srb)
#define LOG_SRB_INFO() RhelDbgPrint(TRACE_LEVEL_INFORMATION, "%s <--> Operation %s (0x%X), Target (%d::%d::%d), SRB 0x%p\n",__FUNCTION__, DbgGetScsiOpStr(DbgGetScsiOp(Srb)), DbgGetScsiOp(Srb), SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb), Srb)
#define LOG_SRB_INFO_FROM_INLFN() RhelDbgPrint(TRACE_LEVEL_INFORMATION, "%s <--> Operation %s (0x%X), Target (%d::%d::%d), SRB 0x%p\n",__FUNCTION__, DbgGetScsiOpStr(DbgGetScsiOp(Srb)), DbgGetScsiOp(Srb), SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb), Srb)

#else
#if defined(RUN_UNCHECKED) && !defined(RUN_MIN_CHECKED)
#undef EVENT_TRACING
#else
#pragma warning(disable: 28170)
#pragma warning(disable: 28251)
#include <stortrce.h>

// {194051B2-14C7-4987-A0E9-154377C58882}

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(VioScsiTraceGuid,(194051B2,14C7,4987,A0E9,154377C58882), \
        WPP_DEFINE_BIT(TRACE_LEVEL_NONE)               /* bit  0 = 0x00000001 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_FATAL)              /* bit  1 = 0x00000002 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_ERROR)              /* bit  2 = 0x00000004 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_WARNING)            /* bit  3 = 0x00000008 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_INFORMATION)        /* bit  4 = 0x00000010 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_VERBOSE)            /* bit  5 = 0x00000020 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_RESERVED6)          /* bit  6 = 0x00000040 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_RESERVED7)          /* bit  7 = 0x00000080 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_RESERVED8)          /* bit  8 = 0x00000100 */ \
        WPP_DEFINE_BIT(TRACE_LEVEL_RESERVED9)          /* bit  9 = 0x00000200 */ \
        WPP_DEFINE_BIT(TRACE_VQ)                       /* bit 10 = 0x00000400 */ \
        WPP_DEFINE_BIT(TRACE_LOCKS)                    /* bit 11 = 0x00000800 */ \
        WPP_DEFINE_BIT(TRACE_DPC)                      /* bit 12 = 0x00001000 */ \
        WPP_DEFINE_BIT(TRACE_MSIX)                     /* bit 13 = 0x00002000 */ \
        WPP_DEFINE_BIT(TRACE_ENTER_EXIT)               /* bit 14 = 0x00004000 */ \
        WPP_DEFINE_BIT(TRACE_NOTIFY)                   /* bit 15 = 0x00008000 */ \
        WPP_DEFINE_BIT(TRACE_GUEST_FEATURES)           /* bit 16 = 0x00008000 */ \
        WPP_DEFINE_BIT(TRACE_PCI_CAP)                  /* bit 17 = 0x00010000 */ \
        WPP_DEFINE_BIT(TRACE_MAPPING)                  /* bit 18 = 0x00020000 */ \
        WPP_DEFINE_BIT(TRACE_MSIX_CPU_AFFINITY)        /* bit 19 = 0x00040000 */ \
        WPP_DEFINE_BIT(TRACE_REGISTRY)                 /* bit 20 = 0x00080000 */ \
        WPP_DEFINE_BIT(TRACE_ALL)                      /* bit LAST PLACEHLDR  */ \
        )

// begin_wpp config
// USEPREFIX (RhelDbgPrint, "%!STDPREFIX! ####\t\t[%!FUNC!] DEBUG:");
// FUNC RhelDbgPrint(LEVEL, MSG, ...);
// end_wpp
#define WPP_Flags_LEVEL_LOGGER(Flags, level) WPP_LEVEL_LOGGER(Flags)
#define WPP_Flags_LEVEL_ENABLED(Flags, level) \
    (WPP_LEVEL_ENABLED(Flags) && \
    WPP_CONTROL(WPP_BIT_ ## Flags).Level >= level)

// begin_wpp config
// USEPREFIX (ENTER_FN, "%!STDPREFIX! ===>\t\t[%!FUNC!] X---X Working X---X");
// FUNC ENTER_FN{ENTRYLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_ENTRYLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_ENTRYLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (EXIT_FN, "%!STDPREFIX! <===\t\t[%!FUNC!] Processing complete.");
// FUNC EXIT_FN{EXITLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_EXITLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_EXITLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (ENTER_INL_FN(PVOID InlineFuncName), "%!STDPREFIX! ===>\t\t[%!FUNC!]>>>[%s] X---X Working X---X", InlineFuncName);
// FUNC ENTER_INL_FN{INLENTRYLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_INLENTRYLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_INLENTRYLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (EXIT_INL_FN(PVOID InlineFuncName), "%!STDPREFIX! <===\t\t[%!FUNC!]<<<[%s] Processing complete.", InlineFuncName);
// FUNC EXIT_INL_FN{INLEXITLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_INLEXITLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_INLEXITLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (EXIT_ERR, "%!STDPREFIX! >>>>\t\t[%!FUNC!] ERROR line %d", __LINE__);
// FUNC EXIT_ERR{ERRORLEVEL=TRACE_LEVEL_ERROR}(...);
// end_wpp
#define WPP_ERRORLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_ERRORLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (ENTER_FN_SRB(PVOID Srb), "%!STDPREFIX! ===>\t\t[%!FUNC!] SRB 0x%p", Srb);
// FUNC ENTER_FN_SRB{SRBENTRYLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_SRBENTRYLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_SRBENTRYLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (EXIT_FN_SRB(PVOID Srb), "%!STDPREFIX! <===\t\t[%!FUNC!] SRB 0x%p", Srb);
// FUNC EXIT_FN_SRB{SRBEXITLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_SRBEXITLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_SRBEXITLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (LOG_SRB_INFO(PVOID Srb), "%!STDPREFIX! ####\t\t[%!FUNC!] Operation %s (0x%X), Target (%d::%d::%d), SRB 0x%p", DbgGetScsiOpStr(DbgGetScsiOp(Srb)), DbgGetScsiOp(Srb), SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb), Srb);
// FUNC LOG_SRB_INFO{SRBINFOLEVEL=TRACE_LEVEL_INFORMATION}(...);
// end_wpp
#define WPP_SRBINFOLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_SRBINFOLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (ENTER_INL_FN_SRB(PVOID InlineFuncName, PVOID Srb), "%!STDPREFIX! ===>\t\t[%!FUNC!]>>>[%s] SRB 0x%p", InlineFuncName, Srb);
// FUNC ENTER_INL_FN_SRB{INLSRBENTRYLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_INLSRBENTRYLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_INLSRBENTRYLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (EXIT_INL_FN_SRB(PVOID InlineFuncName, PVOID Srb), "%!STDPREFIX! <===\t\t[%!FUNC!]<<<[%s] SRB 0x%p", InlineFuncName, Srb);
// FUNC EXIT_INL_FN_SRB{INLSRBEXITLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_INLSRBEXITLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_INLSRBEXITLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (LOG_SRB_INFO_FROM_INLFN(PVOID InlineFuncName, PVOID Srb), "%!STDPREFIX! ####\t\t[%!FUNC!]:[%s] Operation %s (0x%X), Target (%d::%d::%d), SRB 0x%p", InlineFuncName, DbgGetScsiOpStr(DbgGetScsiOp(Srb)), DbgGetScsiOp(Srb), SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb), Srb);
// FUNC LOG_SRB_INFO_FROM_INLFN{INLSRBINFOLEVEL=TRACE_LEVEL_INFORMATION}(...);
// end_wpp
#define WPP_INLSRBINFOLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_INLSRBINFOLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

//
// Configure WPP macros for optimum performance
// by disabling checking for 'WPP_INIT_TRACING'
// as we call it in DriverEntry() anyway.
//
#define WPP_CHECK_INIT
    
#endif

#endif

#endif //__TRACING_H___
