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

#if NDIS_SUPPORT_NDIS620
    GROUP_AFFINITY DPCAffinity;
#else
    ULONG DPCTargetProcessor = 0;
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