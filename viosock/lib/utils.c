/*
 * This file contains various logger routines
 *
 * Copyright (c) 2010-2019 Red Hat, Inc.
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
#include "precomp.h"

#if defined(EVENT_TRACING)
#include "utils.tmh"
#endif

// Global debug printout level and enable\disable flag
BOOL g_bDebugPrint;
int g_DebugLevel;
ULONG g_DebugFlags;

#if !defined(EVENT_TRACING)

#define     TEMP_BUFFER_SIZE        256

void DebugPrintProc(const char *format, ...)
{
    char buf[256];
    va_list list;
    va_start(list, format);
    if (StringCbVPrintfA(buf, sizeof(buf), format, list) == S_OK)
    {
        OutputDebugStringA(buf);
    }
    va_end(list);
}
#endif

void InitDebugPrints()
{
    WPP_INIT_TRACING(NULL);
    //TODO - Read nDebugLevel and bDebugPrint from the registry
    g_DebugFlags = 0xffffffff;

    g_bDebugPrint = 1;
    g_DebugLevel = TRACE_LEVEL_INFORMATION;
}
