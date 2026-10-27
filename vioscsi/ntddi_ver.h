/*
 * This file contains NTDDI_ globals
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

#ifndef ___NTDDIVER_H__
#define ___NTDDIVER_H__

// #include <sdkddkver.h>

// The following are additional definitions not found in current EWDK(s)
// at <drive:>\Program Files\Windows Kits\10\Include\10.0.<build>.0\shared\sdkddkver.h

#ifndef NTDDI_WINTHRESHOLD
#define NTDDI_WINTHRESHOLD 0x0A000000 /* ABRACADABRA_THRESHOLD */
#endif

#define NTDDI_THRESHOLD NTDDI_WINTHRESHOLD

#ifndef NTDDI_WIN10_NI
#define NTDDI_WIN10_NI 0x0A00000C // Windows 10.0.22449-22631  / Nickel       / 22H2
#define NTDDI_WIN10_CU 0x0A00000D // Windows 10.0.25057-25236  / Copper       / 23H1
#endif

#ifndef NTDDI_WIN11
#define NTDDI_WIN11    NTDDI_WIN10_CO
#define NTDDI_WIN11_CO NTDDI_WIN10_CO // Windows 10.0.21277-22000  / Cobalt       / 21H2
#define NTDDI_WIN11_NI NTDDI_WIN10_NI // Windows 10.0.22449-22631  / Nickel       / 22H2
#define NTDDI_WIN11_CU NTDDI_WIN10_CU // Windows 10.0.25057-25236  / Copper       / 23H1
#endif

#ifndef NTDDI_WIN11_ZN
#define NTDDI_WIN11_ZN 0x0A00000E // Windows 10.0.25246-25398  / Zinc         / 23H2
#define NTDDI_WIN11_GA 0x0A00000F // Windows 10.0.25905-25941  / Gallium      / 24H1
#define NTDDI_WIN11_GE 0x0A000010 // Windows 10.0.25947-26100  / Germanium    / 24H2
#endif

#ifndef NTDDI_WIN11_DT
#define NTDDI_WIN11_DT 0x0A000011 // Windows 10.0.27686-27691  / Dilithium    / 25H1
#define NTDDI_WIN11_SE 0x0A000012 // Windows 10.0.27764        / Selenium     / 25H2
#endif

#endif ___NTDDIVER_H__
