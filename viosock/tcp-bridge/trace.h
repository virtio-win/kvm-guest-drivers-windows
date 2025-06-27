/*
 * This file contains trace and debugging related definitions
 *
 * Copyright 2025 Red Hat, Inc. and/or its affiliates.
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

#define EVENT_TRACING

#ifndef EVENT_TRACING

extern ULONG driverDebugFlags;
extern int driverDebugLevel;

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
#define DBG_PNP                 0x00000002
#define DBG_THREAD_VT           0x00000004
#define DBG_THREAD_VT_TRANSFER  0x00000008

#define TraceEvents(level, flags, message, ...) \
if (level > driverDebugLevel || !bDebugPrint || !(driverDebugFlags & flags)) {} \
else VirtioDebugPrintProc(message, __VA_ARGS__)

#define WPP_INIT_TRACING(a,b)
#define WPP_CLEANUP(DriverObject)

#else
#define WPP_CHECK_FOR_NULL_STRING

//
// Define the tracing flags.
//
// Tracing GUID - FA4E6ED1-CF45-46DD-98E8-5F1FBF166526
//
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(VioSockTcpBridgeTraceGuid,                   \
                            (FA4E6ED1, CF45, 46DD, 98E8, 5F1FBF166526), \
        WPP_DEFINE_BIT(DBG_INIT)               /* bit  0 = 0x00000001 */ \
        WPP_DEFINE_BIT(DBG_PNP)                /* bit  1 = 0x00000002 */ \
        WPP_DEFINE_BIT(DBG_THREAD_VT)          /* bit  2 = 0x00000004 */ \
        WPP_DEFINE_BIT(DBG_THREAD_VT_TRANSFER) /* bit  3 = 0x00000008 */ \
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
// CUSTOM_TYPE(State,ItemListLong(CLOSE, CONNECTING, CONNECTED, CLOSING, LISTEN));
// CUSTOM_TYPE(Op,ItemListShort(INVALID, REQUEST, RESPONSE, RST, SHUTDOWN, RW, CREDIT_UPDATE, CREDIT_REQUEST));
// end_wpp
//

#endif
