#pragma once

extern "C" {
#include "osdep.h"
}

#include "kdebugprint.h"

typedef enum
{
    PLACEMENT_NEW
} parandis_placement;

template <typename T, ULONG Tag>
class CNdisAllocatable
{
public:
    void* operator new(size_t /* size */, void *ptr, parandis_placement placement) throw()
        { UNREFERENCED_PARAMETER(placement); return ptr; }


    void* operator new(size_t Size, NDIS_HANDLE MiniportHandle) throw()
        { return NdisAllocateMemoryWithTagPriority(MiniportHandle, (UINT) Size, Tag, NormalPoolPriority); }

    static void Destroy(T *ptr, NDIS_HANDLE MiniportHandle)
    {
        ptr->~T();
        NdisFreeMemoryWithTagPriority(MiniportHandle, ptr, Tag);
    }

protected:
    CNdisAllocatable() {};
    ~CNdisAllocatable() {};

    /* Objects's array can't be freed by NdisFreeMemoryWithTagPriority, as C++ array constructor uses the
    * first several bytes for array length. Array  destructor can't get additional argument, so passing
    * NDIS_HANDLE to the array destructor is impossible. Therefore, the array constructors and destructor
    * are declared private and object arrays has be created and destroyed in two steps - construction by
    * allocating array memory with NdisAllocateMemoryWithTagPriority and then in-place constructing the
    * objects and destructing in the reverse order. */
    void* operator new[](size_t /* size */, void *ptr, parandis_placement placement) throw() = delete;

    void* operator new[](size_t Size, NDIS_HANDLE MiniportHandle) throw() = delete;
    void* operator new[](size_t Size) throw() = delete;
    void operator delete[](void *) = delete;

private:
    /* The delete operator can't be disabled like array constructors and destructors, as default destructor
    * and default constructor depend on the delete operator availability */
    void operator delete(void *) {}
};

class CNdisSpinLock
{
public:
    CNdisSpinLock()
    { NdisAllocateSpinLock(&m_Lock); }
    ~CNdisSpinLock()
    { NdisFreeSpinLock(&m_Lock); }

#pragma warning(push)
#pragma warning(disable:28167) // The function changes IRQL and doesn't restore
    void Lock()
    { NdisAcquireSpinLock(&m_Lock); }
#pragma warning(disable:26110) // Caller failing to hold lock before calling function 'KeReleaseSpinLock'
    void Unlock()
    { NdisReleaseSpinLock(&m_Lock); }
#pragma warning(pop)

private:
    NDIS_SPIN_LOCK m_Lock;

    CNdisSpinLock(const CNdisSpinLock&) = delete;
    CNdisSpinLock& operator= (const CNdisSpinLock&) = delete;
};

template <typename T>
class CLockedContext
{
public:
    CLockedContext(T &LockObject)
        : m_LockObject(LockObject)
    { m_LockObject.Lock(); }

    ~CLockedContext()
    { m_LockObject.Unlock(); }

private:
    T &m_LockObject;

    CLockedContext(const CLockedContext&) = delete;
    CLockedContext& operator= (const CLockedContext&) = delete;
};

typedef CLockedContext<CNdisSpinLock> TSpinLocker;

class CNdisRefCounter
{
public:
    CNdisRefCounter() {}

    void AddRef() { NdisInterlockedIncrement(&m_Counter); }
    void AddRef(ULONG RefCnt);
    LONG Release() { return NdisInterlockedDecrement(&m_Counter); }
    LONG Release(ULONG RefCnt);
    operator LONG () { return m_Counter; }
private:
    LONG m_Counter = 0;

    CNdisRefCounter(const CNdisRefCounter&) = delete;
    CNdisRefCounter& operator= (const CNdisRefCounter&) = delete;
};

class CRefCountingObject
{
public:
    CRefCountingObject()
    { AddRef(); }

    void AddRef()
    { m_RefCounter.AddRef(); }

    void Release()
    {
        if (m_RefCounter.Release() == 0)
        {
            OnLastReferenceGone();
        }
    }

protected:
    virtual void OnLastReferenceGone() = 0;

private:
    CNdisRefCounter m_RefCounter;

    CRefCountingObject(const CRefCountingObject&) = delete;
    CRefCountingObject& operator= (const CRefCountingObject&) = delete;
};

class CLockedAccess
{
public:
    void Lock() { m_Lock.Lock(); }
    void Unlock() { m_Lock.Unlock(); }
private:
    CNdisSpinLock m_Lock;
};

class CRawAccess
{
public:
    void Lock() { }
    void Unlock() { }
};

class CCountingObject
{
public:
    void CounterIncrement() { m_Counter++; }
    void CounterDecrement() { m_Counter--; }
    ULONG GetCount() { return m_Counter; }
private:
    ULONG m_Counter = 0;
};

class CNonCountingObject
{
public:
    void CounterIncrement() { }
    void CounterDecrement() { }
protected:
    ULONG GetCount() { return 0; }
};

#define DECLARE_CNDISLIST_ENTRY(type)                                                   \
    private:                                                                            \
        PLIST_ENTRY GetListEntry()                                                      \
        { return &m_ListEntry; }                                                        \
                                                                                        \
        static type *GetByListEntry(PLIST_ENTRY entry)                                  \
        { return static_cast<type*>(CONTAINING_RECORD(entry, type, m_ListEntry)); }     \
                                                                                        \
        template<typename type, typename AnyAccess, typename AnyStrategy>               \
        friend class CNdisList;                                                         \
                                                                                        \
        LIST_ENTRY m_ListEntry


template <typename TEntryType, typename TAccessStrategy, typename TCountingStrategy>
class CNdisList : private TAccessStrategy, public TCountingStrategy
{
public:
    CNdisList()
    { InitializeListHead(&m_List); }

    bool IsEmpty()
    { return IsListEmpty(&m_List) ? true : false; }

    TEntryType *Pop()
    {
        CLockedContext<TAccessStrategy> LockedContext(*this);
        return Pop_LockLess();
    }

    ULONG Push(TEntryType *Entry)
    {
        CLockedContext<TAccessStrategy> LockedContext(*this);
        InsertHeadList(&m_List, Entry->GetListEntry());
        CounterIncrement();
        return GetCount();
    }

    ULONG PushBack(TEntryType *Entry)
    {
        CLockedContext<TAccessStrategy> LockedContext(*this);
        InsertTailList(&m_List, Entry->GetListEntry());
        CounterIncrement();
        return GetCount();
    }

    void Remove(TEntryType *Entry)
    {
        CLockedContext<TAccessStrategy> LockedContext(*this);
        Remove_LockLess(Entry->GetListEntry());
    }

    template <typename TFunctor>
    void ForEachDetached(TFunctor Functor)
    {
        CLockedContext<TAccessStrategy> LockedContext(*this);
        while (!IsListEmpty(&m_List))
        {
            Functor(Pop_LockLess());
        }
    }

    template <typename TPredicate, typename TFunctor>
    void ForEachDetachedIf(TPredicate Predicate, TFunctor Functor)
    {
        ForEachPrepareIf(Predicate, [this](PLIST_ENTRY Entry){ Remove_LockLess(Entry); }, Functor);
    }

    template <typename TFunctor>
    void ForEach(TFunctor Functor)
    {
        ForEachPrepareIf([](TEntryType*) { return true; }, [](PLIST_ENTRY){}, Functor);
    }

private:
    template <typename TPredicate, typename TPrepareFunctor, typename TFunctor>
    void ForEachPrepareIf(TPredicate Predicate, TPrepareFunctor Prepare, TFunctor Functor)
    {
        CLockedContext<TAccessStrategy> LockedContext(*this);

        PLIST_ENTRY NextEntry = nullptr;

        for (auto CurrEntry = m_List.Flink; CurrEntry != &m_List; CurrEntry = NextEntry)
        {
            NextEntry = CurrEntry->Flink;
            auto Object = TEntryType::GetByListEntry(CurrEntry);

            if (Predicate(Object))
            {
                Prepare(CurrEntry);
                Functor(Object);
            }
        }
    }

    TEntryType *Pop_LockLess()
    {
        CounterDecrement();
        return TEntryType::GetByListEntry(RemoveHeadList(&m_List));
    }

    void Remove_LockLess(PLIST_ENTRY Entry)
    {
        RemoveEntryList(Entry);
        CounterDecrement();
    }

    LIST_ENTRY m_List;
};

class CDpcIrqlRaiser
{
public:

#pragma warning(push)
#pragma warning(disable:28167) // The function changes IRQL and doesn't restore
    CDpcIrqlRaiser()
        : m_OriginalIRQL(KeRaiseIrqlToDpcLevel())
    { }

    ~CDpcIrqlRaiser()
    { KeLowerIrql(m_OriginalIRQL); }
#pragma warning(push)
#pragma warning(disable:28167) // The function changes IRQL and doesn't restore

    CDpcIrqlRaiser(const CDpcIrqlRaiser&) = delete;
    CDpcIrqlRaiser& operator= (const CDpcIrqlRaiser&) = delete;

private:
    KIRQL m_OriginalIRQL;
};

class CNdisSharedMemory
{
public:
    explicit CNdisSharedMemory()
        : m_DrvHandle(NULL)
    {}

    bool Create(NDIS_HANDLE DrvHandle)
    {
        m_DrvHandle = DrvHandle;
        return true;
    }

    ~CNdisSharedMemory();
    bool Allocate(ULONG Size, bool IsCached = true);

    ULONG GetSize() const {return m_Size; }
    PVOID GetVA() const {return m_VA; }
    NDIS_PHYSICAL_ADDRESS GetPA() const {return m_PA; }
private:
    NDIS_HANDLE m_DrvHandle;

    PVOID m_VA = nullptr;
    NDIS_PHYSICAL_ADDRESS m_PA = NDIS_PHYSICAL_ADDRESS();
    ULONG m_Size = 0;
    bool m_IsCached = false;

    CNdisSharedMemory(const CNdisSharedMemory&) = delete;
    CNdisSharedMemory& operator= (const CNdisSharedMemory&) = delete;
};

bool __inline ParaNdis_IsPassive()
{
    return (KeGetCurrentIrql() < DISPATCH_LEVEL);
}

#if NDIS_SUPPORT_NDIS620
#define RW_LOCK_62
#elif NDIS_SUPPORT_NDIS6
#define RW_LOCK_60
#elif
#error  Read/Write lock not supported by NDIS before 6.0
#endif

class CNdisRWLockState 
{
private:
#ifdef RW_LOCK_60
    LOCK_STATE m_state;
#endif
#ifdef RW_LOCK_62
    LOCK_STATE_EX m_state;
#endif
    friend class CNdisRWLock;
};

class CNdisRWLock : public CNdisAllocatable < CNdisRWLock, 'RWLK'> 
{
public:
#ifdef RW_LOCK_60
    bool Create(NDIS_HANDLE) 
    {
        NdisInitializeReadWriteLock(&m_lock);
        return true;
    }
#endif
#ifdef RW_LOCK_62
    CNdisRWLock() : m_pLock(nullptr) {}
    bool Create(NDIS_HANDLE miniportHandle);
#endif

    ~CNdisRWLock() 
    {
#ifdef RW_LOCK_62
        if (m_pLock != nullptr)
            NdisFreeRWLock(m_pLock);
#endif
    }

    _Acquires_shared_lock_(m_pLock)
    void acquireRead(CNdisRWLockState &lockState)
    {
#ifdef RW_LOCK_60
        NdisAcquireReadWriteLock(&m_lock, 0, &lockState.m_state);
#endif
#ifdef RW_LOCK_62
        NdisAcquireRWLockRead(m_pLock, &lockState.m_state, 0);
#endif
    }

    _Acquires_exclusive_lock_(m_pLock)
    void acquireWrite(CNdisRWLockState &lockState)
    {
#ifdef RW_LOCK_60
        NdisAcquireReadWriteLock(&m_lock, 1, &lockState.m_state);
#endif
#ifdef RW_LOCK_62
        NdisAcquireRWLockWrite(m_pLock, &lockState.m_state, 0);
#endif
    }

    _Requires_lock_held_(this->m_pLock)
    void release(CNdisRWLockState &lockState)
    {
#ifdef RW_LOCK_60
        NdisReleaseReadWriteLock(&m_lock, &lockState.m_state);
#endif
#ifdef RW_LOCK_62
        NdisReleaseRWLock(m_pLock, &lockState.m_state);
#endif
    }

    _Acquires_shared_lock_(this->m_pLock)
    void acquireReadDpr(CNdisRWLockState &lockState)
    {
        ASSERTMSG("Unexpected IRQL level", KeGetCurrentIrql() == DISPATCH_LEVEL);

#ifdef RW_LOCK_60
        NdisDprAcquireReadWriteLock(&m_lock, 0, &lockState.m_state);
#endif
#ifdef RW_LOCK_62
        NdisAcquireRWLockRead(m_pLock, &lockState.m_state, NDIS_RWL_AT_DISPATCH_LEVEL);
#endif
    }

    _Acquires_exclusive_lock_(this->m_pLock)
    void acquireWriteDpr(CNdisRWLockState &lockState)
    {
        ASSERTMSG("Unexpected IRQL level", KeGetCurrentIrql() == DISPATCH_LEVEL);
#ifdef RW_LOCK_60
        NdisDprAcquireReadWriteLock(&m_lock, 1, &lockState.m_state);
#endif
#ifdef RW_LOCK_62
        NdisAcquireRWLockWrite(m_pLock, &lockState.m_state, NDIS_RWL_AT_DISPATCH_LEVEL);
#endif
    }

    _Requires_lock_held_(m_pLock)
    void releaseDpr(CNdisRWLockState &lockState)
    {
        ASSERTMSG("Unexpected IRQL level", KeGetCurrentIrql() == DISPATCH_LEVEL);
#ifdef RW_LOCK_60
        NdisDprReleaseReadWriteLock(&m_lock, &lockState.m_state);
#endif
#ifdef RW_LOCK_62
        NdisReleaseRWLock(m_pLock, &lockState.m_state);
#endif
    }

private:
#ifdef RW_LOCK_60
    NDIS_RW_LOCK m_lock;
#endif
#ifdef RW_LOCK_62
    PNDIS_RW_LOCK_EX m_pLock;
#endif
};

template <void (CNdisRWLock::*Acquire)(CNdisRWLockState&), void (CNdisRWLock::*Release)(CNdisRWLockState&)>   class CNdisAutoRWLock 
{
public:
    CNdisAutoRWLock(CNdisRWLock &_lock) : lock(_lock)
    {
        (lock.*Acquire)(lockState);
    }

    ~CNdisAutoRWLock() 
    {
        (lock.*Release)(lockState);
    }
private:
    CNdisRWLock &lock;
    CNdisRWLockState lockState;
    CNdisAutoRWLock &operator=(const CNdisAutoRWLock &) {}
};

typedef CNdisAutoRWLock<&CNdisRWLock::acquireRead, &CNdisRWLock::release> CNdisPassiveReadAutoLock;
typedef CNdisAutoRWLock<&CNdisRWLock::acquireReadDpr, &CNdisRWLock::releaseDpr> CNdisDispatchReadAutoLock;
typedef CNdisAutoRWLock<&CNdisRWLock::acquireWrite, &CNdisRWLock::release> CNdisPassiveWriteAutoLock;
typedef CNdisAutoRWLock<&CNdisRWLock::acquireWriteDpr, &CNdisRWLock::releaseDpr> CNdisDispatchWriteAutoLock;

/* The conversion function returns index of the single raised bit in the affinity mask or INVALID_PROCESSOR_INDEX
  if more than one bit is raised */

ULONG ParaNdis_GetIndexFromAffinity(KAFFINITY affinity);

ULONG ParaNdis_GetSystemCPUCount();

template <size_t PrintWidth, size_t ColumnWidth, typename TTable, typename... AccessorsFuncs>
void ParaNdis_PrintTable(int DebugPrintLevel, TTable table, size_t Size, LPCSTR Format, AccessorsFuncs... Accessors)
{
    CHAR Line[PrintWidth + 1];
    CHAR *LinePos, *End;
    NTSTATUS Res;

    CHAR FullFormat[32] = "%d: ";

    if (RtlStringCbCatA(FullFormat, sizeof(FullFormat), Format) != STATUS_SUCCESS)
    {
        DPrintf(0, ("[%s] - format concatenation failed for %s\n", __FUNCTION__, Format));
        return;
    }

    size_t  i = 0;
    memset(Line, ' ', sizeof(Line));
    Line[PrintWidth] = 0;
    LinePos = Line;

    while (i < Size)
    {
        for (size_t j = 0; j < PrintWidth / ColumnWidth && i < Size; j++, i++)
        {
            Res = RtlStringCbPrintfExA(LinePos, ColumnWidth, &End, NULL, STRSAFE_FILL_ON_FAILURE | ' ',
                FullFormat, i, Accessors(table + i)...);
            if (Res == STATUS_SUCCESS)
                *End = ' ';
            LinePos += ColumnWidth;

        }
        DPrintf(DebugPrintLevel, ("%s", Line));
        memset(Line, ' ', sizeof(Line));
        Line[PrintWidth] = 0;
        LinePos = Line;
    }
}
void ParaNdis_PrintCharArray(int DebugPrintLevel, const CCHAR *data, size_t length);

