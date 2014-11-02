#pragma once
#include "ParaNdis-VirtQueue.h"

class CParaNdisAbstractPath
{
public:
#if NDIS_SUPPORT_NDIS620
    CParaNdisAbstractPath()
    {
        memset(&DPCAffinity, 0, sizeof(DPCAffinity));
    }
#else
    CParaNdisAbstractPath() : DPCTargetProcessor(0) {}
#endif

    bool WasInterruptReported() 
    {
        return m_interruptReported;
    }

    void ClearInterruptReport()
    {
        m_interruptReported = false;
    }

    void ReportInterrupt() {
        m_interruptReported = true;
    }

    UINT getMessageIndex() {
        return m_messageIndex;
    }

    UINT getQueueIndex() {
        return m_queueIndex;
    }

    NDIS_STATUS SetupMessageIndex(u16 queueCardinal);

    /* TODO - Path classes should inherit from CVirtQueue*/
    virtual void DisableInterrupts()
    {
        m_pVirtQueue->DisableInterrupts();
    }

    void EnableInterrupts()
    {
        m_pVirtQueue->EnableInterrupts();
    }

    ULONG getCPUIndex()
    {
#if NDIS_SUPPORT_NDIS620
        PROCESSOR_NUMBER procNumber;

        procNumber.Group = DPCAffinity.Group;
        ULONG number = ParaNdis_GetIndexFromAffinity(DPCAffinity.Mask);
        if (number == INVALID_PROCESSOR_INDEX)
        {
            DPrintf(0, ("[%s] : bad in-group processor index: mask 0x%lx\n", __FUNCTION__, (ULONG)DPCAffinity.Mask));
            ASSERT(FALSE);
            return INVALID_PROCESSOR_INDEX;
        }

        procNumber.Number = (UCHAR)number;
        procNumber.Reserved = 0;

        ULONG procIndex = KeGetProcessorIndexFromNumber(&procNumber);
        ASSERTMSG("Bad processor Index", procIndex != INVALID_PROCESSOR_INDEX);
        return procIndex;
#else
        return ParaNdis_GetIndexFromAffinity(DPCTargetProcessor);
#endif
    }

#if NDIS_SUPPORT_NDIS620
    GROUP_AFFINITY DPCAffinity;
#else
    KAFFINITY DPCTargetProcessor = 0;
#endif

protected:
    PPARANDIS_ADAPTER m_Context;
    CVirtQueue *m_pVirtQueue;

    u16 m_messageIndex = (u16)-1;
    u16 m_queueIndex = (u16)-1;
    bool m_interruptReported;
};


template <class VQ> class CParaNdisTemplatePath : public CParaNdisAbstractPath {
public:
    CParaNdisTemplatePath() {
        m_pVirtQueue = &m_VirtQueue;
    }

    void Renew()
    {
        m_VirtQueue.Renew();
    }

    void Shutdown()
    {
        TSpinLocker LockedContext(m_Lock);
        m_VirtQueue.Shutdown();
    }

    void EnableInterrupts()
    {
        m_VirtQueue.EnableInterrupts();
    }

    //TODO: Needs review/temporary?
    void DisableInterrupts()
    {
        m_VirtQueue.DisableInterrupts();
    }

    //TODO: Needs review/temporary?
    bool IsInterruptEnabled()
    {
        return m_VirtQueue.IsInterruptEnabled();
    }

protected:
    CNdisSpinLock m_Lock;

    VQ m_VirtQueue;
    tCompletePhysicalAddress m_VirtQueueRing;
};