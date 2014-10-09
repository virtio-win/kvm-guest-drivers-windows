#include "ParaNdis-VirtQueue.h"
#include "ndis56common.h"
#include "ParaNdis-AbstractPath.h"

class CParaNdisRX : public CParaNdisTemplatePath<CVirtQueue>, public CNdisAllocatable < CParaNdisRX, 'XRHR' > {
public:
    CParaNdisRX();
    ~CParaNdisRX();

    bool Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex);

    BOOLEAN AddRxBufferToQueue(pRxNetDescriptor pBufferDescriptor);

    void PopulateQueue();

    void Renew() {
        m_VirtQueue.Renew();
    }

    void Shutdown() {
        CLockedContext<CNdisSpinLock> autoLock(m_Lock);
        m_VirtQueue.Shutdown();
    }

    void FreeRxDescriptorsFromList();

    void ReuseReceiveBuffer(LONG regular, pRxNetDescriptor pBuffersDescriptor)
    {
        if (regular)
        {
            ReuseReceiveBufferRegular(pBuffersDescriptor);
        }
        else
        {
            ReuseReceiveBufferPowerOff(pBuffersDescriptor);
        }
    }

    VOID ProcessRxRing(CCHAR nCurrCpuReceiveQueue);

    void EnableInterrupts() {
        m_VirtQueue.EnableInterrupts();
    }

    //TODO: Needs review/temporary?
    void DisableInterrupts() {
        m_VirtQueue.DisableInterrupts();
    }

    BOOLEAN RestartQueue();

    BOOLEAN IsInterruptEnabled() {
        return m_VirtQueue.IsInterruptEnabled();
    }


private:
    /* list of Rx buffers available for data (under VIRTIO management) */
    LIST_ENTRY              m_NetReceiveBuffers;
    UINT                    m_NetNofReceiveBuffers;

    UINT m_nReusedRxBuffersCounter, m_nReusedRxBuffersLimit;

    void ReuseReceiveBufferRegular(pRxNetDescriptor pBuffersDescriptor);
    void ReuseReceiveBufferPowerOff(pRxNetDescriptor pBuffersDescriptor);

private:
    int PrepareReceiveBuffers();
    pRxNetDescriptor CreateRxDescriptorOnInit();

    static BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) RestartQueueSynchronously(tSynchronizedContext *ctx);
};
