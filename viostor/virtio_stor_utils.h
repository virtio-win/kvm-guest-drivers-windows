/*
 * This file contains debug print support routines and globals.
 *
 * Copyright (c) 2008-2017 Red Hat, Inc.
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
#ifndef ___VIOSTOR_UTILS_H___
#define ___VIOSTOR_UTILS_H___


#include "virtio_stor_trace.h"

#define CHECKBIT(value, nbit) virtio_is_feature_enabled(value, nbit)
#define CHECKFLAG(value, flag) ((value & (flag)) == flag)
#define SETFLAG(value, flag) (value |= (flag))

#define CACHE_LINE_SIZE 64
#define ROUND_TO_CACHE_LINES(Size)  (((ULONG_PTR)(Size) + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1))

void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, IN PUNICODE_STRING RegistryPath);

VOID
LogError(
    IN PVOID HwDeviceExtension,
    IN ULONG ErrorCode,
    IN ULONG UniqueId
    );

#endif ___VIOSTOR_UTILS_H___

