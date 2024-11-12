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
 * 3. Neither the names of the copyright holders nor the names of their
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. THIS SOFTWARE IS
 * PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ___TRACING_H___
#define ___TRACING_H___

#define EVENT_TRACING 1

#include "kdebugprint.h"
#include <ntddk.h>
#include <stdarg.h>
#include <storport.h>

#define UCHAR_MAX 0xFF
#define DbgGetScsiOp(Srb)                                                     \
  (SRB_CDB (Srb) ? SRB_CDB (Srb)->CDB6GENERIC.OperationCode : UCHAR_MAX)

char *DbgGetScsiOpStr (IN UCHAR opCode);
void InitializeDebugPrints (IN PDRIVER_OBJECT DriverObject,
                            IN PUNICODE_STRING RegistryPath);

#if !defined(EVENT_TRACING)
#define define DBG 1
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
#define TRACE_LEVEL_NONE 0
#define TRACE_LEVEL_CRITICAL 1
#define TRACE_LEVEL_FATAL 1
#define TRACE_LEVEL_ERROR 2
#define TRACE_LEVEL_WARNING 3
#define TRACE_LEVEL_INFORMATION 4
#define TRACE_LEVEL_VERBOSE 5
#define TRACE_LEVEL_RESERVED6 6
#define TRACE_LEVEL_RESERVED7 7
#define TRACE_LEVEL_RESERVED8 8
#define TRACE_LEVEL_RESERVED9 9
#endif

#if DBG
#define RhelDbgPrint(Level, MSG, ...)                                         \
  if ((!bDebugPrint) || Level > nVioscsiDebugLevel)                           \
    {                                                                         \
    }                                                                         \
  else                                                                        \
    VirtioDebugPrintProc (MSG, __VA_ARGS__)
#define VioScsiDbgBreak()                                                     \
  if (KD_DEBUGGER_ENABLED && !KD_DEBUGGER_NOT_PRESENT)                        \
    DbgBreakPoint ();
#else
#define RhelDbgPrint(Level, MSG, ...)
#define VioScsiDbgBreak()
#endif

#define ENTER_FN()                                                            \
  RhelDbgPrint (TRACE_LEVEL_VERBOSE, " --> %s.\n", __FUNCTION__)
#define EXIT_FN()                                                             \
  RhelDbgPrint (TRACE_LEVEL_VERBOSE, " <-- %s.\n", __FUNCTION__)
#define EXIT_ERR()                                                            \
  RhelDbgPrint (TRACE_LEVEL_ERROR, " <--> %s (%d).\n", __FUNCTION__, __LINE__)
#define ENTER_FN_SRB()                                                        \
  RhelDbgPrint (TRACE_LEVEL_VERBOSE, " --> %s Srb = 0x%p.\n", __FUNCTION__,   \
                Srb)
#define EXIT_FN_SRB()                                                         \
  RhelDbgPrint (TRACE_LEVEL_VERBOSE, " <-- %s Srb = 0x%p.\n", __FUNCTION__,   \
                Srb)
#define LOG_SRB_INFO()                                                        \
  RhelDbgPrint (                                                              \
      TRACE_LEVEL_INFORMATION,                                                \
      "%s <--> Operation %s (0x%X), Target (%d::%d::%d), SRB 0x%p\n",         \
      __FUNCTION__, DbgGetScsiOpStr (DbgGetScsiOp (Srb)), DbgGetScsiOp (Srb), \
      SRB_PATH_ID (Srb), SRB_TARGET_ID (Srb), SRB_LUN (Srb), Srb)

#else
#pragma warning(disable : 28170)
#pragma warning(disable : 28251)
#include <stortrce.h>

// {194051B2-14C7-4987-A0E9-154377C58882}

#define WPP_CONTROL_GUIDS                                                     \
  WPP_DEFINE_CONTROL_GUID (                                                   \
      VioScsiTraceGuid, (194051B2, 14C7, 4987, A0E9, 154377C58882),           \
      WPP_DEFINE_BIT (TRACE_LEVEL_NONE)        /* bit  0 = 0x00000001 */      \
      WPP_DEFINE_BIT (TRACE_LEVEL_FATAL)       /* bit  1 = 0x00000002 */      \
      WPP_DEFINE_BIT (TRACE_LEVEL_ERROR)       /* bit  2 = 0x00000004 */      \
      WPP_DEFINE_BIT (TRACE_LEVEL_WARNING)     /* bit  3 = 0x00000008 */      \
      WPP_DEFINE_BIT (TRACE_LEVEL_INFORMATION) /* bit  4 = 0x00000010 */      \
      WPP_DEFINE_BIT (TRACE_LEVEL_VERBOSE)     /* bit  5 = 0x00000020 */      \
  )

// begin_wpp config
// USEPREFIX (RhelDbgPrint, "%!STDPREFIX! %!FUNC!");
// FUNC RhelDbgPrint(LEVEL, MSG, ...);
// end_wpp

#define WPP_Flags_LEVEL_LOGGER(Flags, level) WPP_LEVEL_LOGGER (Flags)
#define WPP_Flags_LEVEL_ENABLED(Flags, level)                                 \
  (WPP_LEVEL_ENABLED (Flags) && WPP_CONTROL (WPP_BIT_##Flags).Level >= level)

// begin_wpp config
// USEPREFIX (ENTER_FN, "%!STDPREFIX! [%!FUNC!] --> entry");
// FUNC ENTER_FN{ENTRYLEVEL=TRACE_LEVEL_VERBOSE}(...);
// end_wpp

#define WPP_ENTRYLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED (LEVEL)
#define WPP_ENTRYLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER (LEVEL)

// begin_wpp config
// USEPREFIX (EXIT_FN, "%!STDPREFIX! [%!FUNC!] <-- exit");
// FUNC EXIT_FN{EXITLEVEL=TRACE_LEVEL_VERBOSE}(...);
// end_wpp
#define WPP_EXITLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED (LEVEL)
#define WPP_EXITLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER (LEVEL)

// begin_wpp config
// USEPREFIX (EXIT_ERR, "%!STDPREFIX! <--> %!FUNC! ERROR line %d", __LINE__);
// FUNC EXIT_ERR{ERRORLEVEL=TRACE_LEVEL_ERROR}(...);
// end_wpp
#define WPP_ERRORLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED (LEVEL)
#define WPP_ERRORLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER (LEVEL)

// begin_wpp config
// USEPREFIX (ENTER_FN_SRB(PVOID Srb), "%!STDPREFIX! ---> %!FUNC! 0x%p.", Srb);
// FUNC ENTER_FN_SRB{SRBENTRYLEVEL=TRACE_LEVEL_INFORMATION}(...);
// end_wpp
#define WPP_SRBENTRYLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED (LEVEL)
#define WPP_SRBENTRYLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER (LEVEL)

// begin_wpp config
// USEPREFIX (EXIT_FN_SRB(PVOID Srb), "%!STDPREFIX! <--- %!FUNC! 0x%p.", Srb);
// FUNC EXIT_FN_SRB{SRBEXITLEVEL=TRACE_LEVEL_INFORMATION}(...);
// end_wpp
#define WPP_SRBEXITLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED (LEVEL)
#define WPP_SRBEXITLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER (LEVEL)

// begin_wpp config
// USEPREFIX (LOG_SRB_INFO(PVOID Srb), "%!STDPREFIX! %!FUNC! <--> Operation %s
// (0x%X), Target (%d::%d::%d), SRB 0x%p", DbgGetScsiOpStr(DbgGetScsiOp(Srb)),
// DbgGetScsiOp(Srb), SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb), Srb);
// FUNC LOG_SRB_INFO{SRBINFOLEVEL=TRACE_LEVEL_INFORMATION}(...);
// end_wpp
#define WPP_SRBINFOLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED (LEVEL)
#define WPP_SRBINFOLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER (LEVEL)

#endif

#endif //__TRACING_H___
