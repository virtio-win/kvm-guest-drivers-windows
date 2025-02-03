#include "ndis56common.h"
#include "kdebugprint.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_Util.tmh"
#endif

#ifdef DBG
void NetKvmAssert(bool Statement, ULONG Code)
{
    if (!Statement)
    {
        KeBugCheckEx(0x0ABCDEF0, 0x0ABCDEF0, Code, NDIS_MINIPORT_MAJOR_VERSION, NDIS_MINIPORT_MINOR_VERSION);
    }
}
#endif

bool CNdisSharedMemory::Allocate(ULONG Size, bool IsCached)
{
    m_Size = Size;
    m_IsCached = IsCached;
    NdisMAllocateSharedMemory(m_DrvHandle, Size, m_IsCached, &m_VA, &m_PA);
    return m_VA != nullptr;
}

CNdisSharedMemory::~CNdisSharedMemory()
{
    if (m_VA != nullptr)
    {
        NdisMFreeSharedMemory(m_DrvHandle, m_Size, m_IsCached, m_VA, m_PA);
        m_VA = nullptr;
    }
}

// Generic delete operators
// Must never be called
void __CRTDECL operator delete(void *) throw()
{
    NETKVM_ASSERT(FALSE);
#ifdef DBG
    KeBugCheck(100);
#endif
}

void __CRTDECL operator delete(void *, UINT64) throw()
{
    ASSERT(FALSE);
#ifdef DBG
    KeBugCheck(100);
#endif
}

void __CRTDECL operator delete(void *, unsigned int) throw()
{
    ASSERT(FALSE);
#ifdef DBG
    KeBugCheck(100);
#endif
}

void __CRTDECL operator delete[](void *) throw()
{
    NETKVM_ASSERT(FALSE);
#ifdef DBG
    KeBugCheck(100);
#endif
}

#ifdef RW_LOCK_62
bool CNdisRWLock::Create(NDIS_HANDLE miniportHandle)
{
    m_pLock = NdisAllocateRWLock(miniportHandle);
    if (!m_pLock)
    {
        DPrintf(0, "RSS RW lock allocation failed\n");
    }
    return m_pLock != 0;
}
#endif

ULONG ParaNdis_GetIndexFromAffinity(KAFFINITY affinity)
{
    DWORD index = 0;
    BOOLEAN result;
#ifdef _WIN64
    result = BitScanForward64(&index, affinity);
#else
    result = BitScanForward(&index, affinity);
#endif
    if (result && ((KAFFINITY)1 << index) == affinity)
    {
        return index;
    }
    return INVALID_PROCESSOR_INDEX;
}

ULONG ParaNdis_GetSystemCPUCount()
{
    ULONG nProcessors;

#if NDIS_SUPPORT_NDIS620
    nProcessors = NdisGroupActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#elif NDIS_SUPPORT_NDIS6
    nProcessors = NdisSystemProcessorCount();
#else
#error not supported
#endif

    return nProcessors;
}

void Parandis_UtilOnly_Trace(LONG level, LPCSTR s1, LPCSTR s2)
{
    if (!s2)
    {
        TraceNoPrefix(level, "%s\n", s1);
    }
    else
    {
        TraceNoPrefix(level, "[%s] - format concatenation failed for %s", s1, s2);
    }
}

bool CSystemThread::Start(PVOID Context)
{
    m_Context = Context;
    UpdateTimestamp(m_StartTime);
    // clang-format on
    NTSTATUS status = PsCreateSystemThread(
        &m_hThread,
        GENERIC_READ,
        NULL,
        NULL,
        NULL,
        [](PVOID Ctx) { ((CSystemThread *)Ctx)->ThreadProc(); },
        this);
    // clang-format on
    if (!NT_SUCCESS(status))
    {
        DPrintf(0, "Failed to start, status %X", status);
    }
    return m_hThread != NULL && NT_SUCCESS(status);
}

void CSystemThread::Stop()
{
    DPrintf(0, "Waiting for thread termination");
    m_Event.Notify();
    while (m_hThread)
    {
        NdisMSleep(20000);
    }
    DPrintf(0, "Terminated");
}

void CSystemThread::ThreadProc()
{
    PARANDIS_ADAPTER *context = (PARANDIS_ADAPTER *)m_Context;
    context->extraStatistics.lazyAllocTime = -1;
    while (!m_Event.Wait(1))
    {
        UINT n = 0;

        CMutexLockedContext sync(m_PowerMutex);

        for (UINT i = 0; i < context->nPathBundles; ++i)
        {
            n += context->pPathBundles[i].rxPath.AllocateMore();
        }
        if (n == 0)
        {
            DPrintf(0, "All the memory allocations done");
            m_Event.Notify();
            ULONGLONG endTimestamp;
            UpdateTimestamp(endTimestamp);
            context->extraStatistics.lazyAllocTime = (LONG)((endTimestamp - m_StartTime) / 10000);
        }
    }
    m_hThread = NULL;
}
