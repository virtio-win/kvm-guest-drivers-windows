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

private:
    bool m_interruptReported = false;
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

    bool Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex);

protected:
    PPARANDIS_ADAPTER m_Context;
    CNdisSpinLock m_Lock;

    VQ m_VirtQueue;
    tCompletePhysicalAddress m_VirtQueueRing;
};