#pragma once
#include "ParaNdis-VirtQueue.h"
#include "ndis56common.h"

template <class VQ> class CParaNdisAbstractPath {
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
    PPARANDIS_ADAPTER m_Context;

    CNdisSpinLock m_Lock;

    VQ m_VirtQueue;
    tCompletePhysicalAddress m_VirtQueueRing;
};