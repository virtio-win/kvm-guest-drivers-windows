/*
 * Copyright (c) 2023 Virtuozzo International GmbH
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

#if !defined(EVENT_TRACING)

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
#define DBG_VIOWSK                0x00000001

#define DBG_TEST                  0x00000001

#define WPP_INIT_TRACING(a,b)
#define WPP_CLEANUP(DriverObject)

#else
#define WPP_CHECK_FOR_NULL_STRING

//
// Define the tracing flags.
//
// Tracing GUID - C2D7F82F-CE5F-4408-8A37-8B9FE2B3D52E
//

// {13b9cfb4-b962-4b43-b59d-92242fab52e3}
// {46e3298a-70b1-49c6-b9fd-8691980b7adf}
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(WskTraceGuid,(13b9cfb4,b962,4b43,b59d,92242fab52e3), \
        WPP_DEFINE_BIT(DBG_VIOWSK)             /* bit  0 = 0x00000001 */ \
        ) \
    WPP_DEFINE_CONTROL_GUID(WskTestTraceGuid,(46e3298a,70b1,49c6,b9fd,8691980b7adf), \
        WPP_DEFINE_BIT(DBG_TEST)             /* bit  0 = 0x00000001 */ \
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
 //
 // USEPREFIX(DEBUG_ENTER_FUNCTION, "%!STDPREFIX! %!FUNC!(");
 // USESUFFIX(DEBUG_ENTER_FUNCTION, ")");
 // USEPREFIX(DEBUG_ENTER_FUNCTION_NO_ARGS, "%!STDPREFIX! %!FUNC!()");
 // USEPREFIX(DEBUG_EXIT_FUNCTION, "%!STDPREFIX! %!FUNC!(-)");
 // USEPREFIX(DEBUG_EXIT_FUNCTION_VOID, "%!STDPREFIX! %!FUNC!");
 // USESUFFIX(DEBUG_EXIT_FUNCTION_VOID, "(-)");
 // 
 // FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
 // FUNC DEBUG_ENTER_FUNCTION{LEVEL=TRACE_LEVEL_VERBOSE, FLAGS=DBG_TEST}(MSG, ...);
 // FUNC DEBUG_ENTER_FUNCTION_NO_ARGS{LEVEL=TRACE_LEVEL_VERBOSE, FLAGS=DBG_TEST}();
 // FUNC DEBUG_EXIT_FUNCTION{LEVEL=TRACE_LEVEL_VERBOSE, FLAGS=DBG_TEST}(MSG, ...);
 // FUNC DEBUG_EXIT_FUNCTION_VOID{LEVEL=TRACE_LEVEL_VERBOSE, FLAGS=DBG_TEST}();
 // FUNC DEBUG_ERROR{LEVEL=TRACE_LEVEL_ERROR, FLAGS=DBG_TEST}(MSG, ...);
 // FUNC DEBUG_WARNING{LEVEL=TRACE_LEVEL_WARNING, FLAGS=DBG_TEST}(MSG, ...);
 // FUNC DEBUG_TRACE{LEVEL=TRACE_LEVEL_VERBOSE, FLAGS=DBG_TEST}(MSG, ...);
 // FUNC DEBUG_INFO{LEVEL=TRACE_LEVEL_INFORMATION, FLAGS=DBG_TEST}(MSG, ...);
 //
 // end_wpp
 //

#endif
