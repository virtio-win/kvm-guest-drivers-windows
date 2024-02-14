#include "stdafx.h"

CMemStat::CMemStat()
{
    initialized = FALSE;
    locator = NULL;
    service = NULL;
    memset(m_Stats, -1, sizeof(m_Stats));
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
        return FALSE;
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

BOOL CMemStat::Update()
{
    SYSTEM_INFO sysinfo;
    MEMORYSTATUSEX statex = {sizeof(statex)};
    CComPtr< IEnumWbemClassObject > enumerator;
    CComPtr< IWbemClassObject > memory;
    ULONG retcnt;
    _variant_t var_val;
    HRESULT status  = S_OK;
    UINT idx = 0;
    SIZE_T minCacheSize = 0;
    SIZE_T maxCacheSize = 0;
    DWORD  flags = 0;

    GetSystemInfo(&sysinfo);

    status = service->ExecQuery(
                             L"WQL",
                             L"SELECT * FROM Win32_PerfRawData_PerfOS_Memory",
                             WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                             NULL,
                             &enumerator
                             );

    if (FAILED(status)) {
        PrintMessage("Cannot execute query");
        return FALSE;
    }

    if (enumerator == NULL || FAILED(enumerator->Next(
            WBEM_INFINITE,
            1L,
            &memory,
            &retcnt))) {
        PrintMessage("Cannot enumerate results");
        return FALSE;
    }

    if (retcnt > 0) {
        status = memory->Get( 
                             L"PagesInputPerSec",
                             0,
                             &var_val,
                             NULL,
                             NULL
                             );

        if (FAILED(status) || (var_val.vt == VT_NULL)) {
            PrintMessage("Cannot get PagesInputPerSec");
            var_val = (__int64)-1;
        }
        m_Stats[idx].tag = VIRTIO_BALLOON_S_SWAP_IN;
        m_Stats[idx].val = (__int64)var_val * sysinfo.dwPageSize;
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
            var_val = (__int64)-1;
        }
        m_Stats[idx].tag = VIRTIO_BALLOON_S_SWAP_OUT;
        m_Stats[idx].val = (__int64)var_val * sysinfo.dwPageSize;
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
            var_val = (__int64)-1;
        }
        m_Stats[idx].tag = VIRTIO_BALLOON_S_MAJFLT;
        m_Stats[idx].val = (long)var_val;
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
            var_val = (__int64)-1;
        }
        m_Stats[idx].tag = VIRTIO_BALLOON_S_MINFLT;
        m_Stats[idx].val = (long)var_val;
        idx++;

        GlobalMemoryStatusEx(&statex);

        m_Stats[idx].tag = VIRTIO_BALLOON_S_MEMFREE;
        m_Stats[idx].val = statex.ullAvailPhys;
        idx++;

        m_Stats[idx].tag = VIRTIO_BALLOON_S_MEMTOT;
        m_Stats[idx].val = statex.ullTotalPhys;
        idx++;

        status = memory->Get(
                             L"CacheBytes",
                             0,
                             &var_val,
                             NULL,
                             NULL
                             );

        if (FAILED(status) || (var_val.vt == VT_NULL)) {
            PrintMessage("Cannot get CacheBytes");
            var_val.vt = 0;
        }
        else if (GetSystemFileCacheSize(&minCacheSize, &maxCacheSize, &flags) &&
                (flags & FILE_CACHE_MIN_HARD_ENABLE) &&
                ((ULONGLONG)var_val > minCacheSize)) {
            var_val = (ULONGLONG)var_val - minCacheSize;
        }
        m_Stats[idx].tag = VIRTIO_BALLOON_S_AVAIL;
        m_Stats[idx++].val = statex.ullAvailPhys + (ULONGLONG)var_val/2;

        m_Stats[idx].tag = VIRTIO_BALLOON_S_CACHES;
        m_Stats[idx++].val = (ULONGLONG)var_val;

        m_Stats[idx].tag = VIRTIO_BALLOON_S_HTLB_PGALLOC;
        m_Stats[idx++].val = 0;

        m_Stats[idx].tag = VIRTIO_BALLOON_S_HTLB_PGFAIL;
        m_Stats[idx++].val = 0;
    }

    return TRUE;
}
