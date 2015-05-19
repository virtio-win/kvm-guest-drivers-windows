#include "ndis56common.h"
#include "kdebugprint.h"
#include "ParaNdis_DebugHistory.h"

CParaNdisRX::CParaNdisRX() : m_nReusedRxBuffersCounter(0), m_NetNofReceiveBuffers(0)
{
    InitializeListHead(&m_NetReceiveBuffers);

    NdisAllocateSpinLock(&m_UnclassifiedPacketsQueue.Lock);
    InitializeListHead(&m_UnclassifiedPacketsQueue.BuffersList);
    m_UnclassifiedPacketsQueue.ActiveProcessorsCount = 0;
}

CParaNdisRX::~CParaNdisRX()
{
    NdisFreeSpinLock(&m_UnclassifiedPacketsQueue.Lock);
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
    m_Reinsert = true;

    m_VirtQueue.Kick();

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

void CParaNdisRX::FreeRxDescriptorsFromList()
{
    while (!IsListEmpty(&m_NetReceiveBuffers))
    {
        pRxNetDescriptor pBufferDescriptor = (pRxNetDescriptor)RemoveHeadList(&m_NetReceiveBuffers);
        ParaNdis_FreeRxBufferDescriptor(m_Context, pBufferDescriptor);
    }
}

void CParaNdisRX::ReuseReceiveBufferNoLock(pRxNetDescriptor pBuffersDescriptor)
{
    DEBUG_ENTRY(4);

    m_Context->m_rxPacketsOutsideRing.Release();

    if (!m_Reinsert)
    {
        InsertTailList(&m_NetReceiveBuffers, &pBuffersDescriptor->listEntry);
        m_NetNofReceiveBuffers++;
        return;
    } 
    else if (AddRxBufferToQueue(pBuffersDescriptor))
    {
        InsertTailList(&m_NetReceiveBuffers, &pBuffersDescriptor->listEntry);
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

VOID CParaNdisRX::ProcessRxRing(CCHAR nCurrCpuReceiveQueue)
{
    pRxNetDescriptor pBufferDescriptor;
    unsigned int nFullLength;

#ifndef PARANDIS_SUPPORT_RSS
    UNREFERENCED_PARAMETER(nCurrCpuReceiveQueue);
#endif

    CLockedContext<CNdisSpinLock> autoLock(m_Lock);

    while (NULL != (pBufferDescriptor = (pRxNetDescriptor)m_VirtQueue.GetBuf(&nFullLength)))
    {
        /* The counter m_rxPacketsOutsideRing is increased when the packet is removed from ring; it is decreased
        in CParaNdisRX::ReuseReceiveBuffer either in case of error or when NDIS calls ParaNdis6_ReturnNetBufferLists
        indicating the return of a receive buffer under miniport driver ownership */

        m_Context->m_rxPacketsOutsideRing.AddRef();
        RemoveEntryList(&pBufferDescriptor->listEntry);
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
            pBufferDescriptor->Queue->ReuseReceiveBufferNoLock(pBufferDescriptor);
            m_Context->Statistics.ifInErrors++;
            m_Context->Statistics.ifInDiscards++;
            continue;
        }

#ifdef PARANDIS_SUPPORT_RSS
        CCHAR nTargetReceiveQueueNum;
        GROUP_AFFINITY TargetAffinity;
        PROCESSOR_NUMBER TargetProcessor;

        nTargetReceiveQueueNum = ParaNdis_GetScalingDataForPacket(
            m_Context,
            &pBufferDescriptor->PacketInfo,
            &TargetProcessor);

        if (nTargetReceiveQueueNum == PARANDIS_RECEIVE_UNCLASSIFIED_PACKET)
        {
            ParaNdis_ReceiveQueueAddBuffer(&m_UnclassifiedPacketsQueue, pBufferDescriptor);
        }
        else
        {
            ParaNdis_ReceiveQueueAddBuffer(&m_Context->ReceiveQueues[nTargetReceiveQueueNum], pBufferDescriptor);

            if (nTargetReceiveQueueNum != nCurrCpuReceiveQueue)
            {
                ParaNdis_ProcessorNumberToGroupAffinity(&TargetAffinity, &TargetProcessor);
                ParaNdis_QueueRSSDpc(m_Context, m_messageIndex, &TargetAffinity);
            }
        }
#else
       ParaNdis_ReceiveQueueAddBuffer(&m_UnclassifiedPacketsQueue, pBufferDescriptor);
#endif
    }
}

void CParaNdisRX::PopulateQueue()
{
    LIST_ENTRY TempList;
    CLockedContext<CNdisSpinLock> autoLock(m_Lock);


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
    m_Reinsert = true;

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
