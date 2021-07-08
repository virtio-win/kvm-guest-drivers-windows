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

#define __CPLUSPLUS

    #include <stddef.h>
    #include <string.h>
    #include <stdarg.h>
    #include <stdio.h>
    #include <stdlib.h>

    #include <initguid.h>

    #include <ntddk.h>

    #ifndef FAR
    #define FAR
    #endif

    #include <windef.h>
    #include <winerror.h>

    #include <wingdi.h>
    #include <stdarg.h>

    #include <winddi.h>
    #include <ntddvdeo.h>

    #include <d3dkmddi.h>
    #include <d3dkmthk.h>

    #include <ntstrsafe.h>
    #include <ntintsafe.h>

    #include <dispmprt.h>

    #include "trace.h"
    #include "osdep.h"
    #include "virtio_pci.h"
    #include "virtio.h"
    #include "virtio_ring.h"
    #include "kdebugprint.h"
    #include "viogpu_pci.h"
    #include "viogpu.h"
    #include "viogpu_queue.h"
    #include "viogpu_idr.h"
    #include "viogpum.h"
}

#define MAX_CHILDREN               1
#define MAX_VIEWS                  1
#define BITS_PER_BYTE              8

#define POINTER_SIZE               64
#if NTDDI_VERSION > NTDDI_WINBLUE
#define MIN_WIDTH_SIZE             640
#define MIN_HEIGHT_SIZE            480
#else
#define MIN_WIDTH_SIZE             1024
#define MIN_HEIGHT_SIZE            768
#endif
#define NOM_WIDTH_SIZE             1024
#define NOM_HEIGHT_SIZE            768

#define VIOGPUTAG                  'OIVg'

extern VirtIOSystemOps VioGpuSystemOps;

#define VIOGPU_LOG_ASSERTION0(Msg) NT_ASSERT(FALSE)
#define VIOGPU_LOG_ASSERTION1(Msg,Param1) NT_ASSERT(FALSE)
#define VIOGPU_ASSERT(exp) {if (!(exp)) {VIOGPU_LOG_ASSERTION0(#exp);}}


#if DBG
#define VIOGPU_ASSERT_CHK(exp) VIOGPU_ASSERT(exp)
#else
#define VIOGPU_ASSERT_CHK(exp) {}
#endif

#define PAGED_CODE_SEG __declspec(code_seg("PAGE"))
#define PAGED_CODE_SEG_BEGIN \
    __pragma(code_seg(push)) \
    __pragma(code_seg("PAGE"))

#define PAGED_CODE_SEG_END \
    __pragma(code_seg(pop))
