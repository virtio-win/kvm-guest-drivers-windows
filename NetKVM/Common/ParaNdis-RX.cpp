#include "ndis56common.h"

CParaNdisRX::CParaNdisRX() : m_nReusedRxBuffersCounter(0), m_NetNofReceiveBuffers(0)
{
    InitializeListHead(&m_NetReceiveBuffers);
}

CParaNdisRX::~CParaNdisRX()
{
}

bool CParaNdisRX::Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex)
{
    m_Context = Context;
    m_queueIndex = (u16)DeviceQueueIndex;

    if (!m_VirtQueue.Create(DeviceQueueIndex,
        m_Context->IODevice,
        m_Context->MiniportHandle,
        m_Context->bDoPublishIndices ? true : false))
    {
        DPrintf(0, ("CParaNdisRX::Create - virtqueue creation failed\n"));
        return false;
    }

    m_nReusedRxBuffersLimit = m_Context->NetMaxReceiveBuffers / 4 + 1;

    if (!PrepareReceiveBuffers()) {
        DPrintf(0, ("CParaNdisRX::Create - PrepareReceiveBuffers failed"));
        return false;
    }

    return true;
}

int CParaNdisRX::PrepareReceiveBuffers()
{
    UINT i;
    DEBUG_ENTRY(4);

    NdisZeroMemory(m_ReservedRxBufferMemory, sizeof(m_ReservedRxBufferMemory));
    m_RxBufferIndex = 0;
    m_RxBufferOffset = 0;

    for (i = 0; i < m_Context->NetMaxReceiveBuffers; ++i)
    {
        pRxNetDescriptor pBuffersDescriptor = CreateRxDescriptorOnInit();
        if (!pBuffersDescriptor) break;

        pBuffersDescriptor->Queue = this;
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

    m_VirtQueue.Kick();

    return m_NetNofReceiveBuffers;
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
        if (!InitialAllocatePhysicalMemory(&p->PhysicalPages[p->PagesAllocated]))
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

/* TODO - make it method in pRXNetDescriptor */
BOOLEAN CParaNdisRX::AddRxBufferToQueue(pRxNetDescriptor pBufferDescriptor)
{
    return 0 <= pBufferDescriptor->Queue->m_VirtQueue.AddBuf(
        pBufferDescriptor->BufferSGArray,
        0,
        pBufferDescriptor->PagesAllocated,
        pBufferDescriptor,
        m_Context->bUseIndirect ? pBufferDescriptor->IndirectArea.Virtual : NULL,
        m_Context->bUseIndirect ? pBufferDescriptor->IndirectArea.Physical.QuadPart : 0);
}

BOOLEAN CParaNdisRX::InitialAllocatePhysicalMemory(tCompletePhysicalAddress* Address) {
    if (Address->size % PAGE_SIZE) {
        DPrintf(0, ("[%s] size (%d) is not page aligned\n", __FUNCTION__, Address->size));
        return FALSE;
    }
    while (m_RxBufferIndex < ARRAYSIZE(m_ReservedRxBufferMemory)) {
        tCompletePhysicalAddress* bulkBuffer = &m_ReservedRxBufferMemory[m_RxBufferIndex];
        if (bulkBuffer->size == 0) {
            bulkBuffer->size = 1024 * 256;
            if (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, bulkBuffer)) {
              DPrintf(0, ("[%s] fail to allocate memory with slot %d\n", __FUNCTION__, m_RxBufferIndex));
              break;
            }
        }
        if (bulkBuffer->size - m_RxBufferOffset >= Address->size) {
            Address->Physical.QuadPart = bulkBuffer->Physical.QuadPart + m_RxBufferOffset;
            Address->Virtual = (PCHAR)(bulkBuffer->Virtual) + m_RxBufferOffset;
            m_RxBufferOffset += Address->size;
            return TRUE;
        } else {
            m_RxBufferIndex++;
            m_RxBufferOffset = 0;
        }
    }
    return FALSE;
}

void CParaNdisRX::FreeRxDescriptorsFromList()
{
    while (!IsListEmpty(&m_NetReceiveBuffers))
    {
        pRxNetDescriptor pBufferDescriptor = (pRxNetDescriptor)RemoveHeadList(&m_NetReceiveBuffers);
        ParaNdis_FreeRxBufferDescriptor(m_Context, pBufferDescriptor);
    }
    for (UINT i = 0; i < ARRAYSIZE(m_ReservedRxBufferMemory); i++) {
        if (m_ReservedRxBufferMemory[i].Virtual) {
            ParaNdis_FreePhysicalMemory(m_Context, &m_ReservedRxBufferMemory[i]);
        }
    }
}

void CParaNdisRX::ReuseReceiveBufferRegular(pRxNetDescriptor pBuffersDescriptor)
{
    DEBUG_ENTRY(4);

    if (!pBuffersDescriptor)
        return;

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
            m_VirtQueue.KickAlways();
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

    while (NULL != (pBufferDescriptor = (pRxNetDescriptor)m_VirtQueue.GetBuf(&nFullLength)))
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
            pBufferDescriptor->Queue->ReuseReceiveBufferNoLock(m_Context->ReuseBufferRegular, pBufferDescriptor);
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
        {
            ParaNdis_QueueRSSDpc(m_Context, m_messageIndex, &TargetAffinity);
        }
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
    m_VirtQueue.Kick();
}

BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) CParaNdisRX::RestartQueueSynchronously(tSynchronizedContext *ctx)
{
    CVirtQueue *queue = (CVirtQueue *) ctx->Parameter;
    bool res = queue->Restart();

    ParaNdis_DebugHistory(ctx->pContext, hopDPC, (PVOID)ctx->Parameter, 0x20, res, 0);
    return !res;
}

BOOLEAN CParaNdisRX::RestartQueue()
{
    return ParaNdis_SynchronizeWithInterrupt(m_Context,
        m_messageIndex,
        RestartQueueSynchronously,
        &m_VirtQueue);
}
