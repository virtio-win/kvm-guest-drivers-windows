#pragma once
#include "ParaNdis-VirtQueue.h"

class CParaNdisAbstractPath
{
public:
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

private:
    bool m_interruptReported = false;
protected:
    PPARANDIS_ADAPTER m_Context;

    u16 m_messageIndex = (u16)-1;
    u16 m_queueIndex = (u16)-1;
};


template <class VQ> class CParaNdisTemplatePath : public CParaNdisAbstractPath {
public:
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