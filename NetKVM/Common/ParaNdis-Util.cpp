#include "ParaNdis-Util.h"
#include "ndis56common.h"

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
    ASSERT(FALSE);
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
    int shift = 0;

    while (shift < sizeof(affinity) * 8)
    {
        switch (affinity & 0xff)
        {
        case 0:
            break;
        case 0x01:
            return shift ;
        case 0x02:
            return shift + 1;
        case 0x04:
            return shift + 2;
        case 0x08:
            return shift + 3;
        case 0x10:
            return shift + 4;
        case 0x20:
            return shift + 5;
        case 0x40:
            return shift + 6;
        case 0x80:
            return shift + 7;
        default:
            return INVALID_PROCESSOR_INDEX;
        }
        affinity >>= 8;
        shift += 8;
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
