#include "ParaNdis-Util.h"
#include "ndis56common.h"

#ifdef DBG
void NetKvmAssert(bool Statement, ULONG Code)
{
    if (!Statement)
    {
#pragma warning (push)
#pragma warning (disable:28159)
        KeBugCheckEx(0x0ABCDEF0,
                     0x0ABCDEF0,
                     Code,
                     NDIS_MINIPORT_MAJOR_VERSION,
                     NDIS_MINIPORT_MINOR_VERSION);
#pragma warning (pop)
    }
}
#endif

void CNdisRefCounter::AddRef(ULONG RefCnt)
{
    for (auto i = 0UL; i < RefCnt; ++i)
    {
        AddRef();
    }
}

LONG CNdisRefCounter::Release(ULONG RefCnt)
{
    LONG res = m_Counter;

    for (auto i = 0UL; i < RefCnt; ++i)
    {
        res = Release();
    }

    return res;
}

bool CNdisSharedMemory::Allocate(ULONG Size, bool IsCached)
{
    m_Size = Size;
    m_IsCached = IsCached;
    NdisMAllocateSharedMemory(m_DrvHandle, Size, m_IsCached, &m_VA, &m_PA);
    return m_VA != nullptr;
}

CNdisSharedMemory::~CNdisSharedMemory()
{
    if(m_VA != nullptr)
    {
        NdisMFreeSharedMemory(m_DrvHandle, m_Size, m_IsCached, m_VA, m_PA);
    }
}

//Generic delete operators
//Must never be called
void __CRTDECL operator delete(void *) throw()
{
    NETKVM_ASSERT(FALSE);
#ifdef DBG
#pragma warning (push)
#pragma warning (disable:28159)
    KeBugCheck(100);
#pragma warning (pop)
#endif
}

void __CRTDECL operator delete(void *, UINT64) throw()
{
    ASSERT(FALSE);
#ifdef DBG
#pragma warning (push)
#pragma warning (disable:28159)
   KeBugCheck(100);
#pragma warning (pop)
#endif
}

void __CRTDECL operator delete(void *, unsigned int) throw()
{
    ASSERT(FALSE);
#ifdef DBG
#pragma warning (push)
#pragma warning (disable:28159)
    KeBugCheck(100);
#pragma warning (pop)
#endif
}

void __CRTDECL operator delete[](void *) throw()
{
    NETKVM_ASSERT(FALSE);
#ifdef DBG
#pragma warning (push)
#pragma warning (disable:28159)
    KeBugCheck(100);
#pragma warning (pop)
#endif
}

#ifdef RW_LOCK_62
bool CNdisRWLock::Create(NDIS_HANDLE miniportHandle) {
    m_pLock = NdisAllocateRWLock(miniportHandle);
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
