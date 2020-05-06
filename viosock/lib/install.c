/*
 * Socket provider registration
 *
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
#include <initguid.h>
#include "..\inc\install.h"

#if defined(EVENT_TRACING)
#include "install.tmh"
#endif

 // {9980498C-E732-482B-9F67-924273A56666}
DEFINE_GUID(GUID_VIOSOCK_STREAM,
    0x9980498c, 0xe732, 0x482b, 0x9f, 0x67, 0x92, 0x42, 0x73, 0xa5, 0x66, 0x66);

#define VIOSOCK_PROTOCOL_STREAM _T("Virtio Vsock STREAM")

static WSAPROTOCOL_INFO g_ProtocolInfo = {
    .dwServiceFlags1 = XP1_GRACEFUL_CLOSE | XP1_GUARANTEED_DELIVERY | XP1_GUARANTEED_ORDER | XP1_IFS_HANDLES,
    .dwServiceFlags2 = 0,
    .dwServiceFlags3 = 0,
    .dwServiceFlags4 = 0,
    .dwProviderFlags = PFL_MATCHES_PROTOCOL_ZERO,
    .ProviderId = {0},
    .dwCatalogEntryId = 0,
    .ProtocolChain.ChainLen = BASE_PROTOCOL,
    .ProtocolChain.ChainEntries = {0},
    .iVersion = 0,
    .iAddressFamily = AF_VSOCK,
    .iMaxSockAddr = sizeof(struct sockaddr_in),
    .iMinSockAddr = sizeof(struct sockaddr_vm),
    .iSocketType = SOCK_STREAM,
    .iProtocol = 0,
    .iProtocolMaxOffset = 0,
    .iNetworkByteOrder = LITTLEENDIAN,
    .iSecurityScheme = SECURITY_PROTOCOL_NONE,
    .dwMessageSize = 0,
    .dwProviderReserved = 0,
    .szProtocol = VIOSOCK_PROTOCOL_STREAM
};

#define VIOSOCK_DLL_PATH _T("%SystemRoot%\\System32\\viosocklib.dll")

#ifdef AMD64
BOOL
InstallProtocol()
{
    BOOL bRes = FALSE;
    INT iErrno, iRes = WSCInstallProvider64_32((LPGUID)&GUID_VIOSOCK_STREAM, VIOSOCK_DLL_PATH, &g_ProtocolInfo, 1, &iErrno);

    if (iRes != ERROR_SUCCESS)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INSTALL, "WSCInstallProvider64_32 failed: %d\n", iErrno);
        SetLastError(iErrno);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INSTALL, "%ws protocol installed\n", g_ProtocolInfo.szProtocol);
        bRes = TRUE;
    }
    return bRes;
}
#else //AMD64
BOOL
InstallProtocol()
{
    BOOL bIsWow, bRes = FALSE;
    INT iErrno, iRes;

    if (IsWow64Process(GetCurrentProcess(), &bIsWow) && bIsWow)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INSTALL, "Using x86 lib on WOW64\n");
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    iRes = WSCInstallProvider((LPGUID)&GUID_VIOSOCK_STREAM, (const TCHAR*)VIOSOCK_DLL_PATH, &g_ProtocolInfo, 1, &iErrno);
    if (iRes != ERROR_SUCCESS)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INSTALL, "WSCInstallProvider failed: %d\n", iErrno);
        SetLastError(iErrno);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INSTALL, "%ws protocol installed\n", g_ProtocolInfo.szProtocol);
        bRes = TRUE;
    }
    return bRes;
}
#endif //AMD64

BOOL
DeinstallProtocol()
{
    BOOL bRes = FALSE;
    INT iErrno, iRes = WSCDeinstallProvider((LPGUID)&GUID_VIOSOCK_STREAM, &iErrno);

#ifndef AMD64
    BOOL bIsWow;
    if (IsWow64Process(GetCurrentProcess(), &bIsWow) && bIsWow)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INSTALL, "Using x86 lib on WOW64\n");
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
#endif

    if (iRes != ERROR_SUCCESS)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INSTALL, "WSCDeinstallProvider failed: %d\n", iErrno);
        SetLastError(iErrno);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INSTALL, "Protocol deinstalled\n");

#ifdef AMD64
        iRes = WSCDeinstallProvider32((LPGUID)&GUID_VIOSOCK_STREAM, &iErrno);
        if (iRes != ERROR_SUCCESS)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INSTALL, "WSCDeinstallProvider32 failed: %d\n", iErrno);
            SetLastError(iErrno);
            return FALSE;
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INSTALL, "Protocol deinstalled from WOW64\n");
#endif //AMD64
        bRes = TRUE;
    }
    return bRes;
}

