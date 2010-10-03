#include "stdafx.h"
#include "NetKVMnetsh.h"
#include "RegAccess.h"

//This NetSH Helper GUID {D9C599C4-8DCF-4a6a-93AA-A16FE6D5125C}
static const GUID KVMNET_HELPER_GUID = 
    { 0xd9c599c4, 0x8dcf, 0x4a6a, { 0x93, 0xaa, 0xa1, 0x6f, 0xe6, 0xd5, 0x12, 0x5c } };
static const DWORD KVMNET_HELPER_VERSION = 1;

static const LPCTSTR KVMNET_HELPER_NAME = TEXT("KVMNet");
static const LPWSTR KVMNET_HELPER_NAME_W = L"KVMNet";

DWORD WINAPI _KVMNetNetshStartHelper(__in  const GUID *pguidParent,
                                 __in  DWORD dwVersion)
{
    UNREFERENCED_PARAMETER(pguidParent);
    UNREFERENCED_PARAMETER(dwVersion);

    NETCO_DEBUG_PRINT(TEXT("_KVMNetNetshStartHelper called"));

    NS_CONTEXT_ATTRIBUTES attr;

    ZeroMemory(&attr, sizeof(attr));
    attr.dwVersion = KVMNET_HELPER_VERSION;
    attr.dwReserved = 0;
    attr.pwszContext = KVMNET_HELPER_NAME_W;
    attr.guidHelper = KVMNET_HELPER_GUID;
    attr.dwFlags = CMD_FLAG_LOCAL;
    attr.ulPriority = DEFAULT_CONTEXT_PRIORITY;
    attr.ulNumTopCmds = 0;
    attr.pTopCmds = NULL;
    attr.ulNumGroups = 0;
    attr.pCmdGroups = NULL;
    attr.pfnCommitFn = NULL;
    attr.pfnDumpFn = NULL;
    attr.pfnConnectFn = NULL;
    attr.pReserved = NULL;

    RegisterContext(&attr);
    return NO_ERROR;
}

DWORD WINAPI _KVMNetNetshStopHelper(__in  DWORD dwReserved)
{
    UNREFERENCED_PARAMETER(dwReserved);

    NETCO_DEBUG_PRINT(TEXT("_KVMNetNetshStopHelper called"));
    return NO_ERROR;
}

DWORD NETCO_API InitHelperDll(__in DWORD dwNetshVersion,
                                   PVOID pReserved)
{
    UNREFERENCED_PARAMETER(dwNetshVersion);
    UNREFERENCED_PARAMETER(pReserved);

    NS_HELPER_ATTRIBUTES attr;

    ZeroMemory(&attr, sizeof(attr));
    attr.guidHelper = KVMNET_HELPER_GUID;
    attr.dwVersion  = KVMNET_HELPER_VERSION;
    attr.pfnStart   = _KVMNetNetshStartHelper;
    attr.pfnStop    = _KVMNetNetshStopHelper;
    RegisterHelper( NULL, &attr );

    return NO_ERROR;
}

static HINSTANCE g_hinstThisDLL = NULL;

static DWORD _GetThisDLLPathName(LPTSTR szPathName, DWORD *pdwLength)
{
    DWORD dwPathLength = GetModuleFileName(g_hinstThisDLL, szPathName, *pdwLength);
    DWORD dwErr = GetLastError();
    if(ERROR_SUCCESS != dwErr)
    {
        NETCO_DEBUG_PRINT(TEXT("GetModuleFileName failed. Error code: ") << dwErr);
        return dwErr;
    }
    else if(dwPathLength == *pdwLength)
    {
        NETCO_DEBUG_PRINT(TEXT("Buffer provided to GetModuleFileName is too small"));
        return ERROR_BUFFER_OVERFLOW;
    }
    else
    {
        NETCO_DEBUG_PRINT(TEXT("DLL pathname: ") << szPathName);
        *pdwLength = dwPathLength;
        return ERROR_SUCCESS;
    }
}

static const LPCTSTR NETSH_HELPERS_LIST_PATH = TEXT("SOFTWARE\\Microsoft\\NetSh");
static const HKEY    NETSH_HELPERS_LIST_HIVE = HKEY_LOCAL_MACHINE;

DWORD NETCO_API RegisterKVMNetNetShHelper(void)
{
    try
    {
        TCHAR szDllPathName[MAX_PATH];
        DWORD dwPathNameLength = TBUF_SIZEOF(szDllPathName);

        DWORD dwErr = _GetThisDLLPathName(szDllPathName, &dwPathNameLength);
        if(ERROR_SUCCESS != dwErr)
        {
            tstringstream strmError;
            strmError << TEXT("_GetThisDLLPathName failed with code ") << dwErr;
            OutputDebugString(strmError.str().c_str());
            return dwErr;
        }

        neTKVMRegAccess regAccess(NETSH_HELPERS_LIST_HIVE, NETSH_HELPERS_LIST_PATH);
        if(!regAccess.WriteString(KVMNET_HELPER_NAME, szDllPathName, (dwPathNameLength + 1)*sizeof(TCHAR)))
        {
            dwErr = GetLastError();
            tstringstream strmError;
            strmError << TEXT("Registry operation failed with code ") << dwErr;
            OutputDebugString(strmError.str().c_str());
            return dwErr;
        }
        return ERROR_SUCCESS;
    }
    catch (const exception& ex)
    {
        OutputDebugStringA(ex.what());
        return ERROR_INSTALL_FAILURE;
    }
}
DWORD NETCO_API UnregisterKVMNetNetShHelper(void)
{
    try
    {
        neTKVMRegAccess regAccess(NETSH_HELPERS_LIST_HIVE, NETSH_HELPERS_LIST_PATH);
        regAccess.DeleteValue(KVMNET_HELPER_NAME);
        return ERROR_SUCCESS;
    }
    catch (const exception& ex)
    {
        OutputDebugStringA(ex.what());
        return ERROR_INSTALL_FAILURE;
    }
}

BOOL NETCO_API __stdcall DllMain(__in  HINSTANCE hinstDLL,
                                 __in  DWORD fdwReason,
                                 __in  LPVOID lpvReserved)
{
    NETCO_DEBUG_PRINT(TEXT("DllMain(") << fdwReason << TEXT(") called"));

    //Obtain DLL path name and store it for future use
    if(DLL_PROCESS_ATTACH == fdwReason)
    {
        g_hinstThisDLL = hinstDLL;
    }

    return TRUE;
}
