/*
 * This include file contains NTDDI_ globals and routines
 *
 * Copyright (c) 2012-2026 Red Hat, Inc.
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

// clang-format off
#ifndef NTDDI_WINTHRESHOLD
#define NTDDI_WINTHRESHOLD 0x0A000000 /* ABRACADABRA_THRESHOLD */
#endif

#define NTDDI_THRESHOLD NTDDI_WINTHRESHOLD

#ifndef NTDDI_WIN10_NI
#define NTDDI_WIN10_NI 0x0A00000C     // Windows 10.0.22449-22631  / Nickel       / 22H2
#define NTDDI_WIN10_CU 0x0A00000D     // Windows 10.0.25057-25236  / Copper       / 23H1
#endif

#ifndef NTDDI_WIN11
#define NTDDI_WIN11    NTDDI_WIN10_CO
#define NTDDI_WIN11_CO NTDDI_WIN10_CO // Windows 10.0.21277-22000  / Cobalt       / 21H2
#define NTDDI_WIN11_NI NTDDI_WIN10_NI // Windows 10.0.22449-22631  / Nickel       / 22H2
#define NTDDI_WIN11_CU NTDDI_WIN10_CU // Windows 10.0.25057-25236  / Copper       / 23H1
#endif

#ifndef NTDDI_WIN11_ZN
#define NTDDI_WIN11_ZN 0x0A00000E     // Windows 10.0.25246-25398  / Zinc         / 23H2
#define NTDDI_WIN11_GA 0x0A00000F     // Windows 10.0.25905-25941  / Gallium      / 24H1
#define NTDDI_WIN11_GE 0x0A000010     // Windows 10.0.25947-26100  / Germanium    / 24H2
#endif

#ifndef NTDDI_WIN11_DT
#define NTDDI_WIN11_DT 0x0A000011     // Windows 10.0.27686-27686  / Dilithium    / 25H1
#define NTDDI_WIN11_SE NTDDI_WIN11_DT // Windows 10.0.27691-27788  / Selenium     / 25H2
#endif

#ifndef NTDDI_WIN11_BR
#define NTDDI_WIN11_BR 0x0A000012     // Windows 10.0.27798-28020  / Bromine      / 26H1
#define NTDDI_WIN11_KR NTDDI_WIN11_BR // Windows 10.0.29426-?      / Krypton      / 26H2
#endif
// clang-format on

static const char *GetNtddiDesc()
{
    switch (NTDDI_VERSION)
    {
        case NTDDI_WIN10:
            return "NTDDI_VERSION : THRESHOLD | Windows 10.0.10240 | 1507 | Threshold 1";
        case NTDDI_WIN10_TH2:
            return "NTDDI_VERSION : WIN10_TH2 | Windows 10.0.10586 | 1511 | Threshold 2";
        case NTDDI_WIN10_RS1:
            return "NTDDI_VERSION : WIN10_RS1 | Windows 10.0.14393 | 1607 | Redstone 1";
        case NTDDI_WIN10_RS2:
            return "NTDDI_VERSION : WIN10_RS2 | Windows 10.0.15063 | 1703 | Redstone 2";
        case NTDDI_WIN10_RS3:
            return "NTDDI_VERSION : WIN10_RS3 | Windows 10.0.16299 | 1709 | Redstone 3";
        case NTDDI_WIN10_RS4:
            return "NTDDI_VERSION : WIN10_RS4 | Windows 10.0.17134 | 1803 | Redstone 4";
        case NTDDI_WIN10_RS5:
            return "NTDDI_VERSION : WIN10_RS5 | Windows 10.0.17763 | 1809 | Redstone 5";
        case NTDDI_WIN10_19H1:
            return "NTDDI_VERSION : WIN10_19H1 | Windows 10.0.18362 | 19H1 | Titanium";
        case NTDDI_WIN10_VB:
            return "NTDDI_VERSION : WIN10_VB | Windows 10.0.19041 | 2004 | Vibranium";
        case NTDDI_WIN10_MN:
            return "NTDDI_VERSION : WIN10_MN | Windows 10.0.19042 | 20H2 | Manganese";
        case NTDDI_WIN10_FE:
            return "NTDDI_VERSION : WIN10_FE | Windows 10.0.19043 | 21H1 | Iron";
        case NTDDI_WIN10_CO:
            return "NTDDI_VERSION : WIN10_CO | Windows 10.0.19044-22000 | 21H2 | Cobalt";
        case NTDDI_WIN10_NI:
            return "NTDDI_VERSION : WIN10_NI | Windows 10.0.22449-22631 | 22H2 | Nickel";
        case NTDDI_WIN10_CU:
            return "NTDDI_VERSION : WIN10_CU | Windows 10.0.25057-25236 | 23H1 | Copper";
        case NTDDI_WIN11_ZN:
            return "NTDDI_VERSION : WIN11_ZN | Windows 10.0.25246-25398 | 23H2 | Zinc";
        case NTDDI_WIN11_GA:
            return "NTDDI_VERSION : WIN11_GA | Windows 10.0.25905-25941 | 24H1 | Gallium";
        case NTDDI_WIN11_GE:
            return "NTDDI_VERSION : WIN11_GE | Windows 10.0.25947-26100 | 24H2 | Germanium";
        case NTDDI_WIN11_DT:
            return "NTDDI_VERSION : WIN11_DT | Windows 10.0.27686-27691 | 25H1 | Dilithium";
        case NTDDI_WIN11_BR:
            return "NTDDI_VERSION : WIN11_BR | Windows 10.0.27798-28020 | 26H1 | Bromine";
        default:
            return "UNKNOWN";
    }
}

#endif //___NTDDIVER_H__
