/*
 * Copyright (c) 2020 Red Hat, Inc.
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

#include "stdafx.h"

typedef struct _tNamedEntry
{
    ULONG value;
    LPCSTR name;
}tNamedEntry;

#define MAKE_ENTRY(e) { e, #e},
#define GET_NAME(table, val) GetName(table, ELEMENTS_IN(table), val)

static LPCSTR GetName(const tNamedEntry* table, UINT size, ULONG val)
{
    for (UINT i = 0; i < size; ++i)
    {
        if (table[i].value == val) return table[i].name;
    }
    return "Unknown";
}

LPCSTR GetName(const eServiceControl& val)
{
    static tNamedEntry names[] = {
        MAKE_ENTRY(SERVICE_CONTROL_STOP)
        MAKE_ENTRY(SERVICE_CONTROL_PAUSE)
        MAKE_ENTRY(SERVICE_CONTROL_CONTINUE)
        MAKE_ENTRY(SERVICE_CONTROL_INTERROGATE)
        MAKE_ENTRY(SERVICE_CONTROL_SHUTDOWN)
        MAKE_ENTRY(SERVICE_CONTROL_PARAMCHANGE)
        MAKE_ENTRY(SERVICE_CONTROL_NETBINDADD)
        MAKE_ENTRY(SERVICE_CONTROL_NETBINDREMOVE)
        MAKE_ENTRY(SERVICE_CONTROL_NETBINDENABLE)
        MAKE_ENTRY(SERVICE_CONTROL_NETBINDDISABLE)
        MAKE_ENTRY(SERVICE_CONTROL_DEVICEEVENT)
        MAKE_ENTRY(SERVICE_CONTROL_HARDWAREPROFILECHANGE)
        MAKE_ENTRY(SERVICE_CONTROL_POWEREVENT)
        MAKE_ENTRY(SERVICE_CONTROL_SESSIONCHANGE)
        MAKE_ENTRY(SERVICE_CONTROL_PRESHUTDOWN)
        MAKE_ENTRY(SERVICE_CONTROL_TIMECHANGE)
        MAKE_ENTRY(SERVICE_CONTROL_TRIGGEREVENT)
    };
    return GET_NAME(names, val);
}
