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

extern "C" {

}
#define VIOGPU_GET_DEVICE_ID                              0x00
#define VIOGPU_GET_CUSTOM_RESOLUTION                      0x01

#pragma pack(1)
typedef struct _VIOGPU_DISP_MODE {
    USHORT XResolution;
    USHORT YResolution;
}VIOGPU_DISP_MODE, *PVIOGPU_DISP_MODE;
#pragma pack()

#pragma pack(1)
typedef struct  _VIOGPU_ESCAPE{
    USHORT      Type;
    USHORT      DataLength;
    union {
        ULONG Id;
        VIOGPU_DISP_MODE Resolution;
    } DUMMYUNIONNAME;
} VIOGPU_ESCAPE, *PVIOGPU_ESCAPE;
#pragma pack()

#define BASE_NAMED_OBJECTS    L"\\BaseNamedObjects\\"
#define GLOBAL_OBJECTS    L"Global\\"
#define RESOLUTION_EVENT_NAME L"VioGpuResolutionEvent"
