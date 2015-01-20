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

    void FreeRxDescriptorsFromList();

    void ReuseReceiveBuffer(LONG regular, pRxNetDescriptor pBuffersDescriptor)
    {
        CLockedContext<CNdisSpinLock> autoLock(m_Lock);

        ReuseReceiveBufferNoLock(regular, pBuffersDescriptor);
    }

    void ReuseReceiveBufferNoLock(LONG regular, pRxNetDescriptor pBuffersDescriptor)
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

    BOOLEAN RestartQueue();

private:
    /* list of Rx buffers available for data (under VIRTIO management) */
    LIST_ENTRY              m_NetReceiveBuffers;
    UINT                    m_NetNofReceiveBuffers;

    UINT m_nReusedRxBuffersCounter, m_nReusedRxBuffersLimit;

    // Reserved Memory for Rx Buffers. Each memory block will be 256K.
    // The total limit is 256 * 256k = 64M. The actually Physical memory
    // will be allocated via NdisMAllocateSharedMemory() as needed when
    // the actual buffer is allocated upon driver init.
    tCompletePhysicalAddress m_ReservedRxBufferMemory[256];
    // The next available memory address within current memory block. It
    // should be PAGE aligned.
    ULONG m_RxBufferOffset;
    // The current memory block. If it's exhausted, index is incremented
    // to use next one.
    ULONG m_RxBufferIndex;

    // False if we run out of reserved memory. True otherwise.
    BOOLEAN InitialAllocatePhysicalMemory(tCompletePhysicalAddress* Address);

    void ReuseReceiveBufferRegular(pRxNetDescriptor pBuffersDescriptor);
    void ReuseReceiveBufferPowerOff(pRxNetDescriptor pBuffersDescriptor);

private:
    int PrepareReceiveBuffers();
    pRxNetDescriptor CreateRxDescriptorOnInit();

    static BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) RestartQueueSynchronously(tSynchronizedContext *ctx);
};
