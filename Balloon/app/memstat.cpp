#include "memstat.h"

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
        return status;
    }
    initialized = TRUE;

    status = CoInitializeSecurity(NULL,
                            -1,
                            NULL,
                            NULL,
                            RPC_C_AUTHN_LEVEL_PKT,
                            RPC_C_IMP_LEVEL_IMPERSONATE,
                            NULL,
                            EOAC_NONE,
                            0);

    if (FAILED(status)) {
        return FALSE;
    }

    status = CoCreateInstance(CLSID_WbemLocator,
                            NULL,
                            CLSCTX_INPROC_SERVER,
                            IID_IWbemLocator,
                            reinterpret_cast< void** >( &locator ));

    if (FAILED(status)) {
        return FALSE;
    }

    status = locator->ConnectServer(L"root\\cimv2",
                            NULL,
                            NULL,
                            0L,
                            0L,
                            NULL,
                            NULL,
                            &service);
    if (FAILED(status)) {
        return FALSE;
    }

    status = CoSetProxyBlanket(service,
                            RPC_C_AUTHN_WINNT,
                            RPC_C_AUTHZ_NONE,
                            NULL,
                            RPC_C_AUTHN_LEVEL_CALL,
                            RPC_C_IMP_LEVEL_IMPERSONATE,
                            NULL,
                            EOAC_NONE);
    if (FAILED(status)) {
        return FALSE;
    }
    return TRUE;
}

HRESULT CMemStat::GetStat()
{
    CComPtr< IEnumWbemClassObject > enumerator;
    CComPtr< IWbemClassObject > memory;
    ULONG retcnt;
    _variant_t var_val;
    HRESULT status  = S_OK;
    status = service->ExecQuery(L"WQL", 
                            L"SELECT * FROM Win32_PerfFormattedData_PerfOS_Memory",
                            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, 
                            NULL,
                            &enumerator);
    if (FAILED(status)) {
        return status;
    }

    while (1)
    {
        status = enumerator->Next(WBEM_INFINITE,
                            1L,
                            &memory,
                            &retcnt);
        if (status == WBEM_S_FALSE) {
            status = S_OK;
            break;
        }

        if (FAILED(status)) {
            return status;
        }

        status = memory->Get( L"AvailableBytes", 
                            0, 
                            &var_val, 
                            NULL, 
                            NULL );
        if (FAILED(status)) {
            return status;
        }
        if (var_val.vt != VT_NULL) {
            printf("AvailableBytes = %d\n", (long)var_val);
        }
        status = memory->Get( L"PageFaultsPerSec", 
                            0, 
                            &var_val, 
                            NULL, 
                            NULL );
        if (FAILED(status)) {
            return status;
        }
        if (var_val.vt != VT_NULL) {
            printf("PageFaultsPerSec = %d\n", (long)var_val);
        }

        status = memory->Get( L"PageReadsPerSec", 
                            0, 
                            &var_val, 
                            NULL, 
                            NULL );
        if (FAILED(status)) {
            return status;
        }
        if (var_val.vt != VT_NULL) {
	    printf("PageReadsPerSec = %d\n", (long)var_val);
        }

        status = memory->Get( L"PagesInputPerSec", 
                            0, 
                            &var_val, 
                            NULL, 
                            NULL );
        if (FAILED(status)) {
            return status;
        }
        if (var_val.vt != VT_NULL) {
	    printf("PagesInputPerSec = %d\n", (long)var_val);
        }

        status = memory->Get( L"PagesOutputPerSec", 
                            0, 
                            &var_val, 
                            NULL, 
                            NULL );
        if (FAILED(status)) {
            return status;
        }
        if (var_val.vt != VT_NULL) {
	    printf("PagesOutputPerSec = %d\n", (long)var_val);
        }

    }
    return status;
}

BOOL  CMemStat::GetStatus(PBALLOON_STAT pStat)
{
    CComPtr< IEnumWbemClassObject > enumerator;
    CComPtr< IWbemClassObject > memory;
    ULONG retcnt;
    _variant_t var_val;
    HRESULT status  = S_OK;
    UINT idx = 0;
    if(!pStat) {
        return FALSE;
    }

    memset(pStat, 0, sizeof(*pStat));

    status = service->ExecQuery(L"WQL", 
                            L"SELECT * FROM Win32_PerfFormattedData_PerfOS_Memory",
                            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, 
                            NULL,
                            &enumerator);

    if (FAILED(status)) {
        return FALSE;
    }

    while (1)
    {
        status = enumerator->Next(WBEM_INFINITE,
                            1L,
                            &memory,
                            &retcnt);
        if (status == WBEM_S_FALSE) {
            status = S_OK;
            break;
        }

        if (FAILED(status)) {
            return FALSE;
        }

        status = memory->Get( L"AvailableBytes", 
                            0, 
                            &var_val, 
                            NULL, 
                            NULL );
        if (FAILED(status)) {
            return FALSE;
        }
        if (var_val.vt != VT_NULL) {
           pStat[idx].tag = VIRTIO_BALLOON_S_MEMFREE;
           pStat[idx].val = (long)var_val;
           idx++;
        }
        status = memory->Get( L"PageFaultsPerSec", 
                            0, 
                            &var_val, 
                            NULL, 
                            NULL );
        if (FAILED(status)) {
            return FALSE;
        }
        if (var_val.vt != VT_NULL) {
            pStat[idx].tag = VIRTIO_BALLOON_S_MINFLT;
            pStat[idx].val = (long)var_val;
            idx++;
        }

        status = memory->Get( L"PageReadsPerSec", 
                            0, 
                            &var_val, 
                            NULL, 
                            NULL );
        if (FAILED(status)) {
            return FALSE;
        }
        if (var_val.vt != VT_NULL) {
            pStat[idx].tag = VIRTIO_BALLOON_S_MAJFLT;
            pStat[idx].val = (long)var_val;
            idx++;
        }

        status = memory->Get( L"PagesInputPerSec", 
                            0, 
                            &var_val, 
                            NULL, 
                            NULL );
        if (FAILED(status)) {
            return FALSE;
        }
        if (var_val.vt != VT_NULL) {
            pStat[idx].tag = VIRTIO_BALLOON_S_SWAP_IN;
            pStat[idx].val = (long)var_val;
            idx++;
        }

        status = memory->Get( L"PagesOutputPerSec", 
                            0, 
                            &var_val, 
                            NULL, 
                            NULL );
        if (FAILED(status)) {
            return FALSE;
        }
        if (var_val.vt != VT_NULL) {
            pStat[idx].tag = VIRTIO_BALLOON_S_SWAP_OUT;
            pStat[idx].val = (long)var_val;
            idx++;
        }

        pStat[idx].tag = VIRTIO_BALLOON_S_MEMTOT;
        pStat[idx].val = 0xdeadbeef;

    }
    return TRUE;
}
