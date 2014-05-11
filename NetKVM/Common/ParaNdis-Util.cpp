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
}

void __CRTDECL operator delete[](void *) throw()
{
    ASSERT(FALSE);
}
