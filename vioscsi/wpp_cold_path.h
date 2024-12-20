/*
* This file contains debug print support routines and globals.
*
* Copyright (c) 2012-2024 Red Hat, Inc.
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

#ifndef ___WPPCOLDPATH_H___
#define ___WPPCOLDPATH_H___

/* Custom RhelDbgPrint Entries */

// begin_wpp config
// USEPREFIX (RhelDbgPrint, "%!STDPREFIX! ####\t\t[%!FUNC!] DEBUG:");
// FUNC RhelDbgPrint(LEVEL, MSG, ...);
// end_wpp

// begin_wpp config
// USEPREFIX (RhelDbgPrintInline(PVOID ICN, PVOID IFN), "%!STDPREFIX! ===>\t\t[%s]:[%s] DEBUG:", ICN, IFN);
// FUNC RhelDbgPrintInline(LEVEL, MSG, ...);
// end_wpp

#define WPP_Flags_LEVEL_LOGGER(flags, lvl) WPP_LEVEL_LOGGER(flags)
#define WPP_Flags_LEVEL_ENABLED(flags, lvl) \
    (WPP_LEVEL_ENABLED(flags) && \
    WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

/* Cold Path Entries */

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
// USEPREFIX (ENTER_INL_FN(PVOID ICN, PVOID IFN), "%!STDPREFIX! ===>\t\t[%s]>>>[%s] X---X Working X---X", ICN, IFN);
// FUNC ENTER_INL_FN{INLENTRYLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_INLENTRYLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_INLENTRYLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (EXIT_INL_FN(PVOID ICN, PVOID IFN), "%!STDPREFIX! <===\t\t[%s]<<<[%s] Processing complete.", ICN, IFN);
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
// USEPREFIX (ENTER_INL_FN_SRB(PVOID ICN, PVOID IFN, PVOID Srb), "%!STDPREFIX! ===>\t\t[%s]>>>[%s] SRB 0x%p", ICN, IFN, Srb);
// FUNC ENTER_INL_FN_SRB{INLSRBENTRYLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_INLSRBENTRYLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_INLSRBENTRYLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (EXIT_INL_FN_SRB(PVOID ICN, PVOID IFN, PVOID Srb), "%!STDPREFIX! <===\t\t[%s]<<<[%s] SRB 0x%p", ICN, IFN, Srb);
// FUNC EXIT_INL_FN_SRB{INLSRBEXITLEVEL=TRACE_ENTER_EXIT}(...);
// end_wpp
#define WPP_INLSRBEXITLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_INLSRBEXITLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

// begin_wpp config
// USEPREFIX (LOG_SRB_INFO_FROM_INLFN(PVOID ICN, PVOID IFN, PVOID Srb), "%!STDPREFIX! ####\t\t[%s]:[%s] Operation %s (0x%X), Target (%d::%d::%d), SRB 0x%p", ICN, IFN, DbgGetScsiOpStr(DbgGetScsiOp(Srb)), DbgGetScsiOp(Srb), SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb), Srb);
// FUNC LOG_SRB_INFO_FROM_INLFN{INLSRBINFOLEVEL=TRACE_LEVEL_INFORMATION}(...);
// end_wpp
#define WPP_INLSRBINFOLEVEL_ENABLED(LEVEL) WPP_LEVEL_ENABLED(LEVEL)
#define WPP_INLSRBINFOLEVEL_LOGGER(LEVEL) WPP_LEVEL_LOGGER(LEVEL)

#endif //__WPPCOLDPATH_H___
