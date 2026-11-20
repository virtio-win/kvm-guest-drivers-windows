/*
 * Copyright (C) 2019-2020 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
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

#pragma once

#define VIOGPU_GET_DEVICE_ID         0x00
#define VIOGPU_GET_CUSTOM_RESOLUTION 0x01
#define VIOGPU_SET_CUSTOM_RESOLUTION 0x02
#define VIOGPU_GET_SOURCE_RESOLUTION 0x03

#pragma pack(1)
typedef struct _VIOGPU_DISP_MODE
{
    USHORT XResolution;
    USHORT YResolution;
} VIOGPU_DISP_MODE, *PVIOGPU_DISP_MODE;
#pragma pack()

// VIOGPU_GET_SOURCE_RESOLUTION: the size the host wants for the display one VidPN source drives. SourceId is the
// source as DISPLAYCONFIG_PATH_SOURCE_INFO.id names it. It fails with STATUS_INVALID_PARAMETER for an unknown source,
// as a driver without this escape does for the escape itself, and with STATUS_DEVICE_NOT_CONNECTED while the host has
// the source's scanout unplugged. Generation changes with each change of that size and each display plugged in, to a
// value no scanout of the device has had before, from 1: a helper applies a size whose generation it has not applied
// yet, and leaves a size the user chose alone otherwise. VIOGPU_GET_CUSTOM_RESOLUTION names no display: it is the
// size for the one display connected, and fails with STATUS_NOT_SUPPORTED while several are.
#pragma pack(1)
typedef struct _VIOGPU_SOURCE_MODE
{
    ULONG SourceId;
    VIOGPU_DISP_MODE Resolution;
    ULONG Generation;
} VIOGPU_SOURCE_MODE, *PVIOGPU_SOURCE_MODE;
#pragma pack()

#pragma pack(1)
typedef struct _VIOGPU_ESCAPE
{
    USHORT Type;
    USHORT DataLength;
    union {
        ULONG Id;
        VIOGPU_DISP_MODE Resolution;
        VIOGPU_SOURCE_MODE SourceMode;
    } DUMMYUNIONNAME;
} VIOGPU_ESCAPE, *PVIOGPU_ESCAPE;
#pragma pack()

#define BASE_NAMED_OBJECTS    L"\\BaseNamedObjects\\"
#define GLOBAL_OBJECTS        L"Global\\"
// The driver's resolution events, under the global namespace, made by the first VIOGPU_GET_DEVICE_ID:
// RESOLUTION_EVENT_NAME<device id>, which the driver pulses, and RESOLUTION_EVENT_NAME<device id>_<VidPN source id>,
// an auto-reset event per source that stays set until a wait on it returns, and so wakes one thread. The driver
// signals all of them once it has handled a display change that leaves a display connected.
#define RESOLUTION_EVENT_NAME L"VioGpuResolutionEvent"
