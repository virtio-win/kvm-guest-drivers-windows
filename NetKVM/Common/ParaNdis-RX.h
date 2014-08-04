#include "ParaNdis-VirtQueue.h"
#include "ndis56common.h"

class CParaNdisRX : public CNdisAllocatable < CParaNdisRX, 'XRHR' > {
public:
    CParaNdisRX();
    ~CParaNdisRX();

    bool Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex);

    BOOLEAN AddRxBufferToQueue(pRxNetDescriptor pBufferDescriptor);

    void PopulateQueue();

    void Renew() {
        VirtIODeviceRenewQueue(m_NetReceiveQueue);
    }

    void Shutdown() {
        NdisAcquireSpinLock(&m_Lock);
        virtqueue_shutdown(m_NetReceiveQueue);
        NdisReleaseSpinLock(&m_Lock);
    }

    BOOLEAN UpstreamPacketsPending() {
        return m_upstreamPacketPending != 0;
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
        virtqueue_enable_cb(m_NetReceiveQueue);
    }

    //TODO: Needs review/temporary?
    void DisableInterrupts() {
        virtqueue_disable_cb(m_NetReceiveQueue);
    }

    BOOLEAN RestartQueue();

    BOOLEAN IsInterruptEnabled() {
        return ParaNDIS_IsQueueInterruptEnabled(m_NetReceiveQueue);
    }


private:
    PPARANDIS_ADAPTER m_Context;


    struct virtqueue *       m_NetReceiveQueue;
    tCompletePhysicalAddress m_ReceiveQueueRing;
    /* list of Rx buffers available for data (under VIRTIO management) */
    LIST_ENTRY              m_NetReceiveBuffers;
    UINT                    m_NetNofReceiveBuffers;

    CNdisRefCounter         m_upstreamPacketPending;

    UINT m_nReusedRxBuffersCounter, m_nReusedRxBuffersLimit;

    NDIS_SPIN_LOCK          m_Lock;

    void ReuseReceiveBufferRegular(pRxNetDescriptor pBuffersDescriptor);
    void ReuseReceiveBufferPowerOff(pRxNetDescriptor pBuffersDescriptor);
private:
    int PrepareReceiveBuffers();
    pRxNetDescriptor CreateRxDescriptorOnInit();

    static BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) RestartQueueSynchronously(tSynchronizedContext *ctx);
};