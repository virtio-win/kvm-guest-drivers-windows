#include "ndis56common.h"

CParaNdisRX::CParaNdisRX() : m_nReusedRxBuffersCounter(0), m_NetNofReceiveBuffers(0)
{
    InitializeListHead(&m_NetReceiveBuffers);
}

CParaNdisRX::~CParaNdisRX()
{
    ParaNdis_DeleteQueue(m_Context, &m_NetReceiveQueue, &m_ReceiveQueueRing);
}

bool CParaNdisRX::Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex)
{
    ULONG size;

    m_Context = Context;

    m_nReusedRxBuffersLimit = m_Context->NetMaxReceiveBuffers / 4 + 1;

    VirtIODeviceQueryQueueAllocation(&m_Context->IODevice, DeviceQueueIndex, &size, &m_ReceiveQueueRing.size);
    if (m_ReceiveQueueRing.size == 0)
    {
        DPrintf(0, ("CParaNdisRX::Create: VirtIODeviceQueryQueueAllocation failed\n"));
        return false;
    }

    if (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, &m_ReceiveQueueRing))
    {
        DPrintf(0, ("CParaNdisRX::Create: ParaNdis_InitialAllocatePhysicalMemory failed\n"));
        return 0;
    }

    m_NetReceiveQueue = VirtIODevicePrepareQueue(
            &m_Context->IODevice,
            DeviceQueueIndex,
            m_ReceiveQueueRing.Physical,
            m_ReceiveQueueRing.Virtual,
            m_ReceiveQueueRing.size,
            NULL,
            m_Context->bDoPublishIndices);
    if (m_NetReceiveQueue == NULL)
    {
        DPrintf(0, ("CParaNdisRX::Create: VirtIODevicePrepareQueue failed\n"));
        return false;
    }
    PrepareReceiveBuffers();

    return true;
}

int CParaNdisRX::PrepareReceiveBuffers()
{
    int nRet = 0;
    UINT i;
    DEBUG_ENTRY(4);

    for (i = 0; i < m_Context->NetMaxReceiveBuffers; ++i)
    {
        pRxNetDescriptor pBuffersDescriptor = CreateRxDescriptorOnInit();
        if (!pBuffersDescriptor) break;

        if (!AddRxBufferToQueue(pBuffersDescriptor))
        {
            ParaNdis_FreeRxBufferDescriptor(m_Context, pBuffersDescriptor);
            break;
        }

        InsertTailList(&m_NetReceiveBuffers, &pBuffersDescriptor->listEntry);

        m_NetNofReceiveBuffers++;
    }
    /* TODO - NetMaxReceiveBuffers should take into account all queues */
    m_Context->NetMaxReceiveBuffers = m_NetNofReceiveBuffers;
    DPrintf(0, ("[%s] MaxReceiveBuffers %d\n", __FUNCTION__, m_Context->NetMaxReceiveBuffers));

    virtqueue_kick(m_NetReceiveQueue);

    return nRet;
}

pRxNetDescriptor CParaNdisRX::CreateRxDescriptorOnInit()
{
    //For RX packets we allocate following pages
    //  1 page for virtio header and indirect buffers array
    //  X pages needed to fit maximal length buffer of data
    //  The assumption is virtio header and indirect buffers array fit 1 page
    ULONG ulNumPages = m_Context->MaxPacketSize.nMaxDataSizeHwRx / PAGE_SIZE + 2;

    pRxNetDescriptor p = (pRxNetDescriptor)ParaNdis_AllocateMemory(m_Context, sizeof(*p));
    if (p == NULL) return NULL;

    NdisZeroMemory(p, sizeof(*p));

    p->BufferSGArray = (struct VirtIOBufferDescriptor *)
        ParaNdis_AllocateMemory(m_Context, sizeof(*p->BufferSGArray) * ulNumPages);
    if (p->BufferSGArray == NULL) goto error_exit;

    p->PhysicalPages = (tCompletePhysicalAddress *)
        ParaNdis_AllocateMemory(m_Context, sizeof(*p->PhysicalPages) * ulNumPages);
    if (p->PhysicalPages == NULL) goto error_exit;

    for (p->PagesAllocated = 0; p->PagesAllocated < ulNumPages; p->PagesAllocated++)
    {
        p->PhysicalPages[p->PagesAllocated].size = PAGE_SIZE;
        if (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, &p->PhysicalPages[p->PagesAllocated]))
            goto error_exit;

        p->BufferSGArray[p->PagesAllocated].physAddr = p->PhysicalPages[p->PagesAllocated].Physical;
        p->BufferSGArray[p->PagesAllocated].length = PAGE_SIZE;
    }

    //First page is for virtio header, size needs to be adjusted correspondingly
    p->BufferSGArray[0].length = m_Context->nVirtioHeaderSize;

    //Pre-cache indirect area addresses
    p->IndirectArea.Physical.QuadPart = p->PhysicalPages[0].Physical.QuadPart + m_Context->nVirtioHeaderSize;
    p->IndirectArea.Virtual = RtlOffsetToPointer(p->PhysicalPages[0].Virtual, m_Context->nVirtioHeaderSize);
    p->IndirectArea.size = PAGE_SIZE - m_Context->nVirtioHeaderSize;

    if (!ParaNdis_BindRxBufferToPacket(m_Context, p))
        goto error_exit;

    return p;

error_exit:
    ParaNdis_FreeRxBufferDescriptor(m_Context, p);
    return NULL;
}

BOOLEAN CParaNdisRX::AddRxBufferToQueue(pRxNetDescriptor pBufferDescriptor)
{
    return 0 <= virtqueue_add_buf(
        m_Context->RXPath.m_NetReceiveQueue,
        pBufferDescriptor->BufferSGArray,
        0,
        pBufferDescriptor->PagesAllocated,
        pBufferDescriptor,
        m_Context->bUseIndirect ? pBufferDescriptor->IndirectArea.Virtual : NULL,
        m_Context->bUseIndirect ? pBufferDescriptor->IndirectArea.Physical.QuadPart : 0);
}

void CParaNdisRX::FreeRxDescriptorsFromList()
{
    ASSERT(m_upstreamPacketPending == 0);

    while (!IsListEmpty(&m_NetReceiveBuffers))
    {
        pRxNetDescriptor pBufferDescriptor = (pRxNetDescriptor)RemoveHeadList(&m_NetReceiveBuffers);
        ParaNdis_FreeRxBufferDescriptor(m_Context, pBufferDescriptor);
    }
}

void CParaNdisRX::ReuseReceiveBufferRegular(pRxNetDescriptor pBuffersDescriptor)
{
    DEBUG_ENTRY(4);

    if (!pBuffersDescriptor)
        return;

    CLockedContext<CNdisSpinLock> autoLock(m_Lock);

    m_Context->m_upstreamPacketPending.Release();

    if (AddRxBufferToQueue(pBuffersDescriptor))
    {
        m_NetNofReceiveBuffers++;

        if (m_NetNofReceiveBuffers > m_Context->NetMaxReceiveBuffers)
        {
            DPrintf(0, (" Error: NetNofReceiveBuffers > NetMaxReceiveBuffers(%d>%d)\n",
                m_NetNofReceiveBuffers, m_Context->NetMaxReceiveBuffers));
        }

        /* TODO - nReusedRXBuffes per queue or per context ?*/
        if (++m_nReusedRxBuffersCounter >= m_nReusedRxBuffersLimit)
        {
            m_nReusedRxBuffersCounter = 0;
            virtqueue_kick(m_NetReceiveQueue);
        }

        /* TODO - Context ReceiveState should take into account all queues */
        if (m_Context->m_upstreamPacketPending == 0)
        {
            if (m_Context->ReceiveState == srsPausing || m_Context->ReceivePauseCompletionProc)
            {
                ONPAUSECOMPLETEPROC callback = m_Context->ReceivePauseCompletionProc;
                m_Context->ReceiveState = srsDisabled;
                m_Context->ReceivePauseCompletionProc = NULL;
                ParaNdis_DebugHistory(m_Context, hopInternalReceivePause, NULL, 0, 0, 0);
                if (callback) callback(m_Context);
            }
        }
    }
    else
    {
        /* TODO - NetMaxReceiveBuffers per queue or per context ?*/
        DPrintf(0, ("FAILED TO REUSE THE BUFFER!!!!\n"));
        ParaNdis_FreeRxBufferDescriptor(m_Context, pBuffersDescriptor);
        m_Context->NetMaxReceiveBuffers--;
    }
}

void CParaNdisRX::ReuseReceiveBufferPowerOff(pRxNetDescriptor)
{
    m_Context->m_upstreamPacketPending.Release();
}

VOID CParaNdisRX::ProcessRxRing(CCHAR nCurrCpuReceiveQueue)
{
    pRxNetDescriptor pBufferDescriptor;
    unsigned int nFullLength;

    CLockedContext<CNdisSpinLock> autoLock(m_Lock);

    while (NULL != (pBufferDescriptor = (pRxNetDescriptor)virtqueue_get_buf(m_NetReceiveQueue, &nFullLength)))
    {
        CCHAR nTargetReceiveQueueNum;
        GROUP_AFFINITY TargetAffinity;
        PROCESSOR_NUMBER TargetProcessor;

        m_Context->m_upstreamPacketPending.AddRef();
        m_NetNofReceiveBuffers--;

        BOOLEAN packetAnalyzisRC;

        packetAnalyzisRC = ParaNdis_PerformPacketAnalyzis(
#if PARANDIS_SUPPORT_RSS
            &m_Context->RSSParameters,
#endif
            &pBufferDescriptor->PacketInfo,
            pBufferDescriptor->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE].Virtual,
            nFullLength - m_Context->nVirtioHeaderSize);


        if (!packetAnalyzisRC)
        {
            m_Context->RXPath.ReuseReceiveBuffer(m_Context->ReuseBufferRegular, pBufferDescriptor);
            m_Context->Statistics.ifInErrors++;
            m_Context->Statistics.ifInDiscards++;
            continue;
        }

        nTargetReceiveQueueNum = ParaNdis_GetScalingDataForPacket(
            m_Context,
            &pBufferDescriptor->PacketInfo,
            &TargetProcessor);

        ParaNdis_ReceiveQueueAddBuffer(&m_Context->ReceiveQueues[nTargetReceiveQueueNum], pBufferDescriptor);
        ParaNdis_ProcessorNumberToGroupAffinity(&TargetAffinity, &TargetProcessor);

        if ((nTargetReceiveQueueNum != PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED) &&
            (nTargetReceiveQueueNum != nCurrCpuReceiveQueue))
            ParaNdis_QueueRSSDpc(m_Context, &TargetAffinity);
    }
}

void CParaNdisRX::PopulateQueue()
{
    LIST_ENTRY TempList;

    InitializeListHead(&TempList);

    while (!IsListEmpty(&m_NetReceiveBuffers))
    {
        pRxNetDescriptor pBufferDescriptor =
            (pRxNetDescriptor)RemoveHeadList(&m_NetReceiveBuffers);
        InsertTailList(&TempList, &pBufferDescriptor->listEntry);
    }
    m_NetNofReceiveBuffers = 0;
    while (!IsListEmpty(&TempList))
    {
        pRxNetDescriptor pBufferDescriptor =
            (pRxNetDescriptor)RemoveHeadList(&TempList);
        if (AddRxBufferToQueue(pBufferDescriptor))
        {
            InsertTailList(&m_NetReceiveBuffers, &pBufferDescriptor->listEntry);
            m_NetNofReceiveBuffers++;
        }
        else
        {
            /* TODO - NetMaxReceiveBuffers should take into account all queues */
            DPrintf(0, ("FAILED TO REUSE THE BUFFER!!!!\n"));
            ParaNdis_FreeRxBufferDescriptor(m_Context, pBufferDescriptor);
            m_Context->NetMaxReceiveBuffers--;
        }
    }
    virtqueue_kick(m_NetReceiveQueue);
}

BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) CParaNdisRX::RestartQueueSynchronously(tSynchronizedContext *ctx)
{
    struct virtqueue * _vq = (struct virtqueue *) ctx->Parameter;
    bool res = virtqueue_enable_cb(_vq);

    ParaNdis_DebugHistory(ctx->pContext, hopDPC, (PVOID)ctx->Parameter, 0x20, res, 0);
    return !res;
}


BOOLEAN CParaNdisRX::RestartQueue()
{
    return ParaNdis_SynchronizeWithInterrupt(m_Context,
        m_Context->ulRxMessage,
        RestartQueueSynchronously,
        m_NetReceiveQueue);
}
