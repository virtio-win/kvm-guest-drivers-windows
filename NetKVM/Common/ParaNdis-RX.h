#pragma once
#include "ParaNdis-VirtQueue.h"
#include "ParaNdis-AbstractPath.h"

class CParaNdisRX : public CParaNdisTemplatePath<CVirtQueue>, public CNdisAllocatable<CParaNdisRX, 'XRHR'>
{
  public:
    CParaNdisRX();
    ~CParaNdisRX();

    bool Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex);

    BOOLEAN AddRxBufferToQueue(pRxNetDescriptor pBufferDescriptor);

    void PopulateQueue();

    void FreeRxDescriptorsFromList();

    void ReuseReceiveBuffer(pRxNetDescriptor pBuffersDescriptor)
    {
        TPassiveSpinLocker autoLock(m_Lock);

        ReuseReceiveBufferNoLock(pBuffersDescriptor);
    }

    BOOLEAN IsRxBuffersShortage()
    {
        return m_NetNofReceiveBuffers < m_MinRxBufferLimit;
    }

    VOID ProcessRxRing(CCHAR nCurrCpuReceiveQueue);

    BOOLEAN RestartQueue();

    void Shutdown()
    {
        TPassiveSpinLocker autoLock(m_Lock);

        m_VirtQueue.Shutdown();
        m_Reinsert = false;
    }

    void KickRXRing();

    PARANDIS_RECEIVE_QUEUE &UnclassifiedPacketsQueue()
    {
        return m_UnclassifiedPacketsQueue;
    }
    UINT GetFreeRxBuffers() const
    {
        return m_NetNofReceiveBuffers;
    }
    BOOLEAN AllocateMore();

  private:
    /* list of Rx buffers available for data (under VIRTIO management) */
    LIST_ENTRY m_NetReceiveBuffers;
    UINT m_NetNofReceiveBuffers = 0;
    UINT m_NetMaxReceiveBuffers = 0;
    UINT m_MinRxBufferLimit;

    UINT m_nReusedRxBuffersCounter = 0;
    UINT m_nReusedRxBuffersLimit = 0;

    bool m_Reinsert = true;

    PARANDIS_RECEIVE_QUEUE m_UnclassifiedPacketsQueue;

// Maximum mergeable packet size per VirtIO spec: 65562 bytes (including 12-byte header)
// Required buffers: ceil(65562 / 4096) = 17 PAGE-sized buffers maximum
#define VIRTIO_NET_MAX_MRG_BUFS   17

// Merge buffer context structure - pre-allocated to avoid hot-path allocation
// Maximum PhysicalPages needed: First buffer (2 pages) + additional buffers (16 * 1 page) = 18 pages
// Note: First buffer has 2 logical pages (PhysicalPages[0] and [1] alias to same physical page)
#define MAX_MERGED_PHYSICAL_PAGES 18
    struct _MergeBufferContext
    {
        pRxNetDescriptor BufferSequence[VIRTIO_NET_MAX_MRG_BUFS];
        UINT32 BufferActualLengths[VIRTIO_NET_MAX_MRG_BUFS];
        UINT16 ExpectedBuffers;
        UINT16 CollectedBuffers;
        UINT32 TotalPacketLength;

        // Pre-allocated array for merged packet assembly (eliminates allocate/copy/free in hot path)
        tCompletePhysicalAddress PhysicalPages[MAX_MERGED_PHYSICAL_PAGES];
    } m_MergeContext;

    void ReuseReceiveBufferNoLock(pRxNetDescriptor pBuffersDescriptor);
    pRxNetDescriptor ProcessMergedBuffers(pRxNetDescriptor pFirstBuffer, UINT nFullLength);
    BOOLEAN CollectRemainingMergeBuffers();
    pRxNetDescriptor AssembleMergedPacket();
    void ReuseCollectedBuffers();
    void ProcessReceivedPacket(pRxNetDescriptor pBufferDescriptor, CCHAR nCurrCpuReceiveQueue);

    // Helper function for mergeable buffer state management
    void DisassembleMergedPacket(pRxNetDescriptor pBuffer);

  private:
    int PrepareReceiveBuffers();
    pRxNetDescriptor CreateRxDescriptorOnInit();
    pRxNetDescriptor CreateMergeableRxDescriptor(); // Simplified descriptor for mergeable buffers
    void RecalculateLimits();
};

#ifdef PARANDIS_SUPPORT_RSS
VOID ParaNdis_ResetRxClassification(PARANDIS_ADAPTER *pContext);
#endif
