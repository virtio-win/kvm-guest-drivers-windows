#include "memstat.h"
#include "utils.h"

CMemStat::CMemStat()
{
    initialized = FALSE;
    locator = NULL;
    service = NULL;
}

CMemStat::~CMemStat()
{
    service = NULL;
    locator = NULL;

    if (initialized == TRUE) {
        CoUninitialize();
    }
}

BOOL CMemStat::Init()
{
    HRESULT status  = S_OK;
    status = CoInitializeEx(NULL,
                            COINIT_MULTITHREADED);
    if (FAILED(status)) {
        PrintMessage("Cannot initialize COM");
        return status;
    }
    initialized = TRUE;

    status = CoInitializeSecurity(
                             NULL,
                             -1,
                             NULL,
                             NULL,
                             RPC_C_AUTHN_LEVEL_PKT,
                             RPC_C_IMP_LEVEL_IMPERSONATE,
                             NULL,
                             EOAC_NONE,
                             0
                             );

    if (FAILED(status)) {
        PrintMessage("Cannot initialize security");
        return FALSE;
    }

    status = CoCreateInstance(
                             CLSID_WbemLocator,
                             NULL,
                             CLSCTX_INPROC_SERVER,
                             IID_IWbemLocator,
                             reinterpret_cast< void** >( &locator )
                             );

    if (FAILED(status)) {
        PrintMessage("Cannot create instance");
        return FALSE;
    }

    status = locator->ConnectServer(
                             L"root\\cimv2",
                             NULL,
                             NULL,
                             0L,
                             0L,
                             NULL,
                             NULL,
                             &service
                             );
    if (FAILED(status)) {
        PrintMessage("Cannot connect to wmi server");
        return FALSE;
    }

    status = CoSetProxyBlanket(service,
                             RPC_C_AUTHN_WINNT,
                             RPC_C_AUTHZ_NONE,
                             NULL,
                             RPC_C_AUTHN_LEVEL_CALL,
                             RPC_C_IMP_LEVEL_IMPERSONATE,
                             NULL,
                             EOAC_NONE
                             );
    if (FAILED(status)) {
        PrintMessage("Cannot set proxy blanket");
        return FALSE;
    }
    return TRUE;
}

BOOL  CMemStat::GetStatus(PBALLOON_STAT pStat)
{
    MEMORYSTATUSEX statex = {sizeof(statex)};
    CComPtr< IEnumWbemClassObject > enumerator;
    CComPtr< IWbemClassObject > memory;
    ULONG retcnt;
    _variant_t var_val;
    HRESULT status  = S_OK;
    UINT idx = 0;
    if(!pStat) {
        PrintMessage("Invalid pointer");
        return FALSE;
    }

    status = service->ExecQuery(
                             L"WQL",
                             L"SELECT * FROM Win32_PerfFormattedData_PerfOS_Memory",
                             WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                             NULL,
                             &enumerator
                             );

    if (FAILED(status)) {
        PrintMessage("Cannot execute query");
        return FALSE;
    }

    while(enumerator)
    {
        status = enumerator->Next(
                             WBEM_INFINITE,
                             1L,
                             &memory,
                             &retcnt
                             );
        if (retcnt == 0) {
            break;
        }

        status = memory->Get( 
                             L"PagesInputPerSec",
                             0,
                             &var_val,
                             NULL,
                             NULL
                             );

        if (FAILED(status) || (var_val.vt == VT_NULL)) {
            PrintMessage("Cannot get PagesInputPerSec");
            var_val.vt =  -1;
        }
        pStat[idx].tag = VIRTIO_BALLOON_S_SWAP_IN;
        pStat[idx].val = (long)var_val;
        idx++;

        status = memory->Get( 
                             L"PagesOutputPerSec",
                             0,
                             &var_val,
                             NULL,
                             NULL
                             );

        if (FAILED(status) || (var_val.vt == VT_NULL)) {
            PrintMessage("Cannot get PagesOutputPerSec");
            var_val.vt =  -1;
        }
        pStat[idx].tag = VIRTIO_BALLOON_S_SWAP_OUT;
        pStat[idx].val = (long)var_val;
        idx++;

        status = memory->Get( 
                             L"PageReadsPerSec",
                             0,
                             &var_val,
                             NULL,
                             NULL
                             );

        if (FAILED(status) || (var_val.vt == VT_NULL)) {
            PrintMessage("Cannot get PageReadsPerSec");
            var_val.vt =  -1;
        }
        pStat[idx].tag = VIRTIO_BALLOON_S_MAJFLT;
        pStat[idx].val = (long)var_val;
        idx++;

        status = memory->Get( 
                             L"PageFaultsPerSec",
                             0,
                             &var_val,
                             NULL,
                             NULL
                             );

        if (FAILED(status) || (var_val.vt == VT_NULL)) {
            PrintMessage("Cannot get PageFaultsPerSec");
            var_val.vt =  -1;
        }
        pStat[idx].tag = VIRTIO_BALLOON_S_MINFLT;
        pStat[idx].val = (long)var_val;
        idx++;

        GlobalMemoryStatusEx(&statex);

        pStat[idx].tag = VIRTIO_BALLOON_S_MEMFREE;
        pStat[idx].val = statex.ullAvailPhys;
        idx++;

        pStat[idx].tag = VIRTIO_BALLOON_S_MEMTOT;
        pStat[idx].val = statex.ullTotalPhys;
    }
    return TRUE;
}
