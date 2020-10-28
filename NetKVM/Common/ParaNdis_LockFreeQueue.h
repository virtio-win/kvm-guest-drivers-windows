#pragma once

/*
* Multiple producer single consumer lock free queue implementation
*
* This implementation of lock free queue is based on the freebsd implementation
* of a lock free queue which can be found in:
* https://svnweb.freebsd.org/base/release/8.0.0/sys/sys/buf_ring.h?view=markup
* https://svnweb.freebsd.org/base/release/8.0.0/sys/kern/subr_bufring.c?view=markup
*
* Copyright Red Hat, Inc. 2017
*
* Authors:
*  Sameeh Jubran <sjubran@redhat.com>
*
*/

template <typename TEntryType>
class CLockFreeQueue
{
public:
    CLockFreeQueue() :
        m_Context(nullptr),
        m_ProducerSize(0),
        m_ConsumerSize(0),
        m_ProducerMask(0),
        m_ConsumerMask(0),
        m_ProducerHead(0),
        m_ConsumerHead(0),
        m_ProducerTail(0),
        m_ConsumerTail(0),
        m_PQueueRing(nullptr)
    {
    }

    BOOLEAN Create(PPARANDIS_ADAPTER pContext, INT size)
    {
        m_Context = pContext;
        m_ProducerSize = size;
        m_ConsumerSize = size;
        m_ProducerMask = size - 1;
        m_ConsumerMask = size - 1;

        /*
         * The size of the queue has to be a power of two in order to easily
         * implement the overflow cyclic check
         */

        if (!IsPowerOfTwo(size))
        {
            return FALSE;
        }

        m_PQueueRing = (TEntryType **) ParaNdis_AllocateMemory(pContext, sizeof(TEntryType *) * size);
        if (m_PQueueRing == nullptr)
        {
            return FALSE;
        }
        return TRUE;
    }

    ~CLockFreeQueue()
    {
        if (m_PQueueRing != nullptr)
        {
            NdisFreeMemory(m_PQueueRing, 0, 0);
            m_PQueueRing = nullptr;
        }
    }

   /*
    * multi-producer safe lock-free ring buffer enqueue
    */

    bool Enqueue(TEntryType *entry)
    {
        LONG producer_head, producer_next, consumer_tail;
        /* Critical section */
        {
            do {
                producer_head = m_ProducerHead;
                producer_next = (producer_head + 1) & m_ProducerMask;
                consumer_tail = m_ConsumerTail;

                if (producer_next == consumer_tail) {
                    return FALSE;
                }
            } while (InterlockedCompareExchange(&m_ProducerHead, producer_next, producer_head) != producer_head);

            m_PQueueRing[producer_head] = entry;
            KeMemoryBarrier();

           /*
            * If there are other enqueues in progress
            * that preceded us, we need to wait for them
            * to complete
            */
            while (m_ProducerTail != producer_head)
            {}

            m_ProducerTail = producer_next;
        }
        return TRUE;
    }

   /*
    * single-consumer dequeue
    * should be called under lock!
    */

    TEntryType *Dequeue()
    {
        LONG consumer_head, consumer_next;
        volatile LONG producer_tail;
        TEntryType *entry;

        consumer_head = m_ConsumerHead;
        producer_tail = m_ProducerTail;

        consumer_next = (consumer_head + 1) & m_ConsumerMask;

        if (consumer_head == producer_tail)
        {
            return nullptr;
        }

        m_ConsumerHead = consumer_next;
        entry = m_PQueueRing[consumer_head];

        m_ConsumerTail = consumer_next;

        return entry;
    }

    TEntryType *DequeueMC()
    {
        LONG consumer_head, consumer_next, producer_tail;
        TEntryType *entry;

        /* Critical section */
        {
            do {
                consumer_head = m_ConsumerHead;
                producer_tail = m_ProducerTail;
                consumer_next = (consumer_head + 1) & m_ConsumerMask;

                if (consumer_head == producer_tail) {
                    return nullptr;
                }
            } while (InterlockedCompareExchange(&m_ConsumerHead, consumer_next, consumer_head) != consumer_head);

            entry = m_PQueueRing[consumer_head];
            KeMemoryBarrier();

            /*
            * If there are other enqueues in progress
            * that preceded us, we need to wait for them
            * to complete
            */
            while (m_ConsumerTail != consumer_head)
            {
            }

            m_ConsumerTail = consumer_next;
        }
        return entry;
    }

   /*
    * single-consumer peek operation
    * should be called under lock!
    */

    TEntryType *Peek()
    {
        if (m_ConsumerHead == m_ProducerTail)
        {
            return nullptr;
        }
        return m_PQueueRing[m_ConsumerHead];
    }

    BOOLEAN IsEmpty()
    {
        return (m_ConsumerHead == m_ProducerTail);
    }

    BOOLEAN IsFull()
    {
        return (((m_ProducerHead + 1) & m_ProducerMask) == m_ConsumerTail);
    }

    BOOLEAN IsPowerOfTwo(INT x)
    {
        return (x & (x - 1)) == 0 && (x != 0);
    }

private:

    volatile LONG m_ProducerHead;
    volatile LONG m_ProducerTail;
    INT m_ProducerSize;
    INT m_ProducerMask;
    volatile LONG m_ConsumerHead;
    volatile LONG m_ConsumerTail;
    INT m_ConsumerSize;
    INT m_ConsumerMask;
    TEntryType **m_PQueueRing;

    PPARANDIS_ADAPTER m_Context;
};


template <typename TEntryType>
class CLockFreeDynamicQueue : public CPlacementAllocatable
{
public:
    CLockFreeDynamicQueue() : m_QueueFullListIsEmpty(TRUE), m_Size(0), m_ElementCount(0)
    {}

    BOOLEAN Create(PPARANDIS_ADAPTER pContext, INT size)
    {
        m_QueueFullListIsEmpty = TRUE;
        m_Size = size;
        m_ElementCount = 0;
        return m_Queue.Create(pContext, size);
    }

    // Multiple Producer Safe Enqueue
    void Enqueue(TEntryType * entry)
    {
        InterlockedIncrement(&m_ElementCount);
        if (!m_QueueFullListIsEmpty || !m_Queue.Enqueue(entry))
        {
            TPassiveSpinLocker LockedContext(m_QueueFullListLock);
            m_QueueFullList.PushBack(entry);
            InterlockedExchange(&m_QueueFullListIsEmpty, m_QueueFullList.IsEmpty());
        }
    }

    // Single consumer Dequeue, a lock is needed when using this method

    TEntryType *Dequeue()
    {
        TEntryType * ptr = m_Queue.Dequeue();
        if (ptr == nullptr)
        {
            FillQueue();
            ptr = m_Queue.Dequeue();
        }
        DecrementCount(ptr != nullptr);
        return ptr;
    }

    // Multiple consumer Dequeue

    TEntryType *DequeueMC()
    {
        TEntryType * ptr = m_Queue.DequeueMC();
        if (ptr == nullptr)
        {
            FillQueue();
            ptr = m_Queue.DequeueMC();
        }
        DecrementCount(ptr != nullptr);
        return ptr;
    }

    TEntryType *Peek()
    {
        TEntryType * element = m_Queue.Peek();
        if (element == nullptr && !m_QueueFullListIsEmpty)
        {
            FillQueue();
            element = m_Queue.Peek();
        }
        return element;
    }

    BOOLEAN IsEmpty()
    {
        return m_Queue.IsEmpty() ? (BOOLEAN) m_QueueFullListIsEmpty : FALSE;
    }

    BOOLEAN IsPowerOfTwo(INT x)
    {
        return m_Queue.IsPowerOfTwo(x);
    }

private:

    void CallFillQueue()
    {
        if (m_AccessCount > m_Size)
        {
            FillQueue();
        }
    }

    void DecrementCount(BOOLEAN decrement)
    {
        if (decrement)
        {
            InterlockedDecrement(&m_ElementCount);
        }
    }

    bool FillQueue()
    {
        bool res = FALSE;
        TEntryType *entry = nullptr;

        if (!m_QueueFullListIsEmpty)
        {
            /* LockedContext Locker will always be locked during all of it's lifetime,
            especially when it's being destructed. SDV does not recognize that due to encapsulation
            so the warning is suppressed*/
#pragma warning(suppress: 26110)
            TPassiveSpinLocker LockedContext(m_QueueFullListLock);
            do
            {
                entry = m_QueueFullList.Pop();
                if (entry != nullptr)
                {
                    res = m_Queue.Enqueue(entry);
                    if (!res)
                    {
                        m_QueueFullList.Push(entry);
                    }
                } else
                {
                    break;
                }
            } while (res);

            InterlockedExchange(&m_QueueFullListIsEmpty, m_QueueFullList.IsEmpty());
        }
        return res;
    }

    CLockFreeQueue<TEntryType> m_Queue;


    CNdisList<TEntryType, CRawAccess, CNonCountingObject> m_QueueFullList;
    CNdisSpinLock m_QueueFullListLock;
    volatile LONG m_QueueFullListIsEmpty;
    volatile LONG m_ElementCount;
    volatile LONG m_Size;
};