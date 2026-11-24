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
#define VIOGPU_SET_SOURCE_APPLIED    0x04
// The layouts per source the driver keeps an applied generation for, the oldest forgotten first.
#define VIOGPU_APPLIED_LAYOUTS       4

#pragma pack(1)
typedef struct _VIOGPU_DISP_MODE
{
    USHORT XResolution;
    USHORT YResolution;
} VIOGPU_DISP_MODE, *PVIOGPU_DISP_MODE;
#pragma pack()

// VIOGPU_GET_SOURCE_RESOLUTION: the size a helper gives the display one VidPN source drives, and its generation.
// SourceId is the source as DISPLAYCONFIG_PATH_SOURCE_INFO.id and D3DKMT_OPENADAPTERFROMHDC.VidPnSourceId name it;
// ScanoutId is the scanout the answer is for, the target of the source's path, which DISPLAYCONFIG_PATH_TARGET_INFO.id
// names. The size is the display's custom mode: the host's size, raised to 640x480 without FlexResolution, or the size
// VIOGPU_SET_CUSTOM_RESOLUTION set since. Size and generation are the display's, not the source's: when Windows moves
// the source to another display, the answer is that display's. The generation changes with each change of the size
// and each plug-in, to a value no display of the device has had since the device started, and is never 0. A helper
// applies a size whose generation differs from the last one it applied to the display, and otherwise leaves the size
// the user chose alone.
//
// The driver keeps that last generation for the helper, so that a helper started again, after a logoff or a disconnect
// of the console, still leaves the user's size alone. Layout is the helper's key for the set of active paths: Windows
// keeps a display's mode per layout, so a size applied in one layout is not in another. The answer's Applied is the
// generation VIOGPU_SET_SOURCE_APPLIED last recorded for the source in Layout, 0 for none. The driver keeps the last
// VIOGPU_APPLIED_LAYOUTS layouts per source, and forgets them when the device starts again, with its generations.
// VIOGPU_SET_SOURCE_APPLIED takes SourceId, Layout and Generation.
//
// The escape fails with STATUS_INVALID_PARAMETER for a source the device does not have, the
// status a driver without the escape gives: every device has source 0, so only source 0 tells the two apart. It fails
// with STATUS_DEVICE_NOT_CONNECTED while the source drives no connected display. VIOGPU_GET_CUSTOM_RESOLUTION and
// VIOGPU_SET_CUSTOM_RESOLUTION name no display: they act on the one display connected, or on display 0 while none is,
// and fail with STATUS_NOT_SUPPORTED while several are. Every escape fails with STATUS_INVALID_BUFFER_SIZE when
// DataLength or the buffer is shorter than its data.
#pragma pack(1)
typedef struct _VIOGPU_SOURCE_MODE
{
    ULONG SourceId;
    VIOGPU_DISP_MODE Resolution;
    ULONG Generation;
    ULONG ScanoutId;
    ULONGLONG Layout;
    ULONG Applied;
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

// The layout helpers and drivers of other builds share. Helpers built before VIOGPU_GET_SOURCE_RESOLUTION send an
// 8-byte VIOGPU_ESCAPE: the data of the escapes they know fits in it.
C_ASSERT(sizeof(VIOGPU_DISP_MODE) == 4);
C_ASSERT(FIELD_OFFSET(VIOGPU_ESCAPE, Id) == 4);
C_ASSERT(FIELD_OFFSET(VIOGPU_ESCAPE, Resolution) + sizeof(VIOGPU_DISP_MODE) <= 8);
C_ASSERT(sizeof(VIOGPU_SOURCE_MODE) == 28);

#define BASE_NAMED_OBJECTS    L"\\BaseNamedObjects\\"
#define GLOBAL_OBJECTS        L"Global\\"
// The driver's resolution events, in the global namespace, made by VIOGPU_GET_DEVICE_ID:
// RESOLUTION_EVENT_NAME<device id>, which the driver pulses, and RESOLUTION_EVENT_NAME<device id>_<VidPN source id>,
// an auto-reset event per source, which stays set until a wait on it returns, and so wakes one thread. The driver
// signals all of them when a display change leaves a display connected and when VIOGPU_SET_CUSTOM_RESOLUTION changes a
// size, and a source's own when Windows moves the source to another display, or onto one.
#define RESOLUTION_EVENT_NAME L"VioGpuResolutionEvent"
