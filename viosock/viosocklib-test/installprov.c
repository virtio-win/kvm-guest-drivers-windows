#include "pch.h"
#include <initguid.h>
#include "installprov.h"

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
    .ProtocolChain.ChainLen = 1,
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
        _tprintf(_T("WSCInstallProvider64_32 failed: %d\n"), iErrno);
        SetLastError(iErrno);
    }
    else
    {
        _tprintf(_T("%s protocol installed\n"), g_ProtocolInfo.szProtocol);
        bRes = TRUE;
    }
    return bRes;
}
#else //AMD64
BOOL
InstallProtocol()
{
    BOOL bRes = FALSE;
    INT iErrno, iRes = WSCInstallProvider((LPGUID)&GUID_VIOSOCK_STREAM,(const TCHAR*) VIOSOCK_DLL_PATH, &g_ProtocolInfo, 1, &iErrno);

    if (iRes != ERROR_SUCCESS)
    {
        _tprintf(_T("WSCInstallProvider failed: %d\n"), iErrno);
        SetLastError(iErrno);
    }
    else
    {
        _tprintf(_T("%s protocol installed\n"), g_ProtocolInfo.szProtocol);
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

    if (iRes != ERROR_SUCCESS)
    {
        _tprintf(_T("WSCDeinstallProvider failed: %d\n"), iErrno);
        SetLastError(iErrno);
    }
    else
    {
        _tprintf(_T("Protocol deinstalled\n"));

#ifdef AMD64
        iRes = WSCDeinstallProvider32((LPGUID)&GUID_VIOSOCK_STREAM, &iErrno);
        if (iRes != ERROR_SUCCESS)
        {
            _tprintf(_T("WSCDeinstallProvider32 failed: %d\n"), iErrno);
            SetLastError(iErrno);
            return FALSE;
        }
#endif //AMD64
        _tprintf(_T("Protocol deinstalled from WOW64\n"));
        bRes = TRUE;
    }
    return bRes;
}

VOID
PrintProtocolInfo(
    LPWSAPROTOCOL_INFO pProtocolInfo
)
{
    _tprintf(_T("szProtocol: %s\n\
\tdwServiceFlags1: 0x%x\n\
\tdwServiceFlags2: 0x%x\n\
\tdwServiceFlags3: 0x%x\n\
\tdwServiceFlags4: 0x%x\n\
\tdwProviderFlags: 0x%x\n\
\tProviderId: {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n\
\tdwCatalogEntryId: %u\n\
\tProtocolChain.ChainLen: %u\n\
\tiVersion: %d\n\
\tiAddressFamily: %d\n\
\tiMaxSockAddr: %d\n\
\tiMinSockAddr: %d\n\
\tiSocketType: %d\n\
\tiProtocol: %d\n\
\tiProtocolMaxOffset: %d\n\
\tiNetworkByteOrder: %d\n\
\tiSecurityScheme: %d\n\
\tdwMessageSize: %u\n\
\tdwProviderReserved: %u\n\n"),
        pProtocolInfo->szProtocol,
        pProtocolInfo->dwServiceFlags1,
        pProtocolInfo->dwServiceFlags2,
        pProtocolInfo->dwServiceFlags3,
        pProtocolInfo->dwServiceFlags4,
        pProtocolInfo->dwProviderFlags,
        pProtocolInfo->ProviderId.Data1,
        pProtocolInfo->ProviderId.Data2,
        pProtocolInfo->ProviderId.Data3,
        pProtocolInfo->ProviderId.Data4[0],
        pProtocolInfo->ProviderId.Data4[1],
        pProtocolInfo->ProviderId.Data4[2],
        pProtocolInfo->ProviderId.Data4[3],
        pProtocolInfo->ProviderId.Data4[4],
        pProtocolInfo->ProviderId.Data4[5],
        pProtocolInfo->ProviderId.Data4[6],
        pProtocolInfo->ProviderId.Data4[7],
        pProtocolInfo->dwCatalogEntryId,
        pProtocolInfo->ProtocolChain.ChainLen,
        pProtocolInfo->iVersion,
        pProtocolInfo->iAddressFamily,
        pProtocolInfo->iMaxSockAddr,
        pProtocolInfo->iMinSockAddr,
        pProtocolInfo->iSocketType,
        pProtocolInfo->iProtocol,
        pProtocolInfo->iProtocolMaxOffset,
        pProtocolInfo->iNetworkByteOrder,
        pProtocolInfo->iSecurityScheme,
        pProtocolInfo->dwMessageSize,
        pProtocolInfo->dwProviderReserved
    );
}

#ifdef AMD64
BOOL
EnumProtocols32()
{
    BOOL bRes = FALSE;
    DWORD lpdwBufferLength = 0;
    INT iErrno, iRes = WSCEnumProtocols32(NULL, NULL, &lpdwBufferLength, &iErrno);

    if (iRes == ERROR_SUCCESS || iErrno != WSAENOBUFS)
    {
        _tprintf(_T("WSCEnumProtocols32 (query size) failed: %d\n"), iErrno);
        SetLastError(iErrno);
    }
    else
    {
        LPWSAPROTOCOL_INFO pProtocolList = malloc(lpdwBufferLength);

        if (pProtocolList)
        {
            int iProtos = WSCEnumProtocols32(NULL, pProtocolList, &lpdwBufferLength, &iErrno);
            if (iProtos == SOCKET_ERROR)
            {
                _tprintf(_T("WSCEnumProtocols32 (get list) failed: %d\n"), iErrno);
                SetLastError(iErrno);
            }
            else
            {
                int i;

                _tprintf(_T("--------WOW Protocol list:--------\n"));
                for (i = 0; i < iProtos; ++i)
                    PrintProtocolInfo(&pProtocolList[i]);

                bRes = TRUE;
            }
            free(pProtocolList);
        }
        else
        {
            _tprintf(_T("malloc failed: %d\n"), GetLastError());
        }
    }
    return bRes;
}
#endif //AMD64

BOOL
EnumProtocols()
{
    BOOL bRes = FALSE;
    DWORD lpdwBufferLength = 0;
    INT iErrno, iRes = WSCEnumProtocols(NULL, NULL, &lpdwBufferLength, &iErrno);

    if (iRes == ERROR_SUCCESS || iErrno != WSAENOBUFS)
    {
        _tprintf(_T("WSCEnumProtocols (query size) failed: %d\n"), iErrno);
        SetLastError(iErrno);
    }
    else
    {
        LPWSAPROTOCOL_INFO pProtocolList = malloc(lpdwBufferLength);

        if (pProtocolList)
        {
            int iProtos = WSCEnumProtocols(NULL, pProtocolList, &lpdwBufferLength, &iErrno);
            if (iProtos == SOCKET_ERROR)
            {
                _tprintf(_T("WSCEnumProtocols (get list) failed: %d\n"), iErrno);
                SetLastError(iErrno);
            }
            else
            {
                int i;

                _tprintf(_T("Protocol list:\n"));
                for (i=0; i < iProtos; ++i)
                    PrintProtocolInfo(&pProtocolList[i]);
#ifdef AMD64
                bRes = EnumProtocols32();
#else //AMD64
                bRes = TRUE;
#endif //AMD64
            }
        }
        else
        {
            _tprintf(_T("malloc failed: %d\n"), GetLastError());
        }
    }
    return bRes;
}
