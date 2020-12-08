#include "baseobj.h"

_When_((PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError("Must succeed pool allocations are forbidden. "
        "Allocation failures cause a system crash"))
    void* __cdecl operator new(size_t Size, POOL_TYPE PoolType)
{
    Size = (Size != 0) ? Size : 1;

    void* pObject = ExAllocatePoolWithTag(PoolType, Size, VIOGPUTAG);

    if (pObject != NULL)
    {
#if DBG
        RtlFillMemory(pObject, Size, 0xCD);
#else
        RtlZeroMemory(pObject, Size);
#endif // DBG
    }
    return pObject;
}

_When_((PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError("Must succeed pool allocations are forbidden. "
        "Allocation failures cause a system crash"))
    void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType)
{

    Size = (Size != 0) ? Size : 1;

    void* pObject = ExAllocatePoolWithTag(PoolType, Size, VIOGPUTAG);

    if (pObject != NULL)
    {
#if DBG
        RtlFillMemory(pObject, Size, 0xCD);
#else
        RtlZeroMemory(pObject, Size);
#endif
    }
    return pObject;
}

void __cdecl operator delete(void* pObject)
{

    if (pObject != NULL)
    {
        ExFreePoolWithTag(pObject, VIOGPUTAG);
    }
}

void __cdecl operator delete[](void* pObject)
{

    if (pObject != NULL)
    {
        ExFreePoolWithTag(pObject, VIOGPUTAG);
    }
}

void __cdecl operator delete(void *pObject, size_t Size)
{

    UNREFERENCED_PARAMETER(Size);
    ::operator delete (pObject);
}
