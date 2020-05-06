/*
 * This file contains trace and debugging related definitions
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
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

#if ((OSVERSION_MASK & NTDDI_VERSION) > NTDDI_VISTA)
#define EVENT_TRACING
#endif

#if !defined(EVENT_TRACING)

extern BOOL g_bDebugPrint;
extern DWORD g_DebugFlags;
extern int g_DebugLevel;

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

//
// Define Debug Flags
//
#define DBG_INIT                0x00000001
#define DBG_SOCKET              0x00000002
#define DBG_INSTALL             0x00000004
#define DBG_RESERVED2           0x00000008
#define DBG_RESERVED3           0x00000010

#define TraceEvents(level, flags, message, ...) \
if (level > g_DebugLevel || !g_bDebugPrint || !(g_DebugFlags & flags)) {} \
else DebugPrintProc(message, __VA_ARGS__)

#define WPP_INIT_TRACING(a)
#define WPP_CLEANUP()

void DebugPrintProc(const char *format, ...);

#else
#define WPP_CHECK_FOR_NULL_STRING

//
// Define the tracing flags.
//
// Tracing GUID - 0A3BB318-098D-44CF-B9D9-10737393CD9B
//
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(VioSockTraceGuid,(0A3BB318,098D,44CF,B9D9,10737393CD9B), \
        WPP_DEFINE_BIT(DBG_INIT)             /* bit  0 = 0x00000001 */ \
        WPP_DEFINE_BIT(DBG_SOCKET)           /* bit  1 = 0x00000002 */ \
        WPP_DEFINE_BIT(DBG_INSTALL)          /* bit  2 = 0x00000004 */ \
        WPP_DEFINE_BIT(DBG_RESERVED2)        /* bit  3 = 0x00000008 */ \
        WPP_DEFINE_BIT(DBG_RESERVED3)        /* bit  4 = 0x00000010 */ \
        )

#define WPP_FLAG_LEVEL_LOGGER(flag, level) \
    WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level) \
    (WPP_LEVEL_ENABLED(flag) && WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) \
    WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
    (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

 //
 // This comment block is scanned by the trace preprocessor to define our
 // Trace function.
 //
 // begin_wpp config
 // FUNC Trace{FLAG=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...);
 // FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
 // end_wpp
 //

#endif

void InitDebugPrints();
