#include "ndis56common.h"
#include "kdebugprint.h"
#include "ParaNdis_DebugHistory.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_RX.tmh"
#endif

static FORCEINLINE VOID ParaNdis_ReceiveQueueAddBuffer(PPARANDIS_RECEIVE_QUEUE pQueue, pRxNetDescriptor pBuffer)
{
    NdisInterlockedInsertTailList(&pQueue->BuffersList,
        &pBuffer->ReceiveQueueListEntry,
        &pQueue->Lock);
}

static void ParaNdis_UnbindRxBufferFromPacket(
    pRxNetDescriptor p)
{
    PMDL NextMdlLinkage = p->Holder;
    ULONG ulPageDescIndex = PARANDIS_FIRST_RX_DATA_PAGE;

    while (NextMdlLinkage != NULL)
    {
        PMDL pThisMDL = NextMdlLinkage;
        NextMdlLinkage = NDIS_MDL_LINKAGE(pThisMDL);

        NdisAdjustMdlLength(pThisMDL, p->PhysicalPages[ulPageDescIndex].size);
        NdisFreeMdl(pThisMDL);
        ulPageDescIndex++;
    }
}

static BOOLEAN ParaNdis_BindRxBufferToPacket(
    PARANDIS_ADAPTER *pContext,
    pRxNetDescriptor p)
{
    ULONG i;
    PMDL *NextMdlLinkage = &p->Holder;

    for (i = PARANDIS_FIRST_RX_DATA_PAGE; i < p->BufferSGLength; i++)
    {
        *NextMdlLinkage = NdisAllocateMdl(
            pContext->MiniportHandle,
            p->PhysicalPages[i].Virtual,
            p->PhysicalPages[i].size);
        if (*NextMdlLinkage == NULL) goto error_exit;

        NextMdlLinkage = &(NDIS_MDL_LINKAGE(*NextMdlLinkage));
    }
    *NextMdlLinkage = NULL;

    return TRUE;

error_exit:

    ParaNdis_UnbindRxBufferFromPacket(p);
    return FALSE;
}

static void ParaNdis_FreeRxBufferDescriptor(PARANDIS_ADAPTER *pContext, pRxNetDescriptor p)
{
    ULONG i;

    ParaNdis_UnbindRxBufferFromPacket(p);
    for (i = 0; i < p->BufferSGLength; i++)
    {
        ParaNdis_FreePhysicalMemory(pContext, &p->PhysicalPages[i]);
    }

    if (p->BufferSGArray) NdisFreeMemory(p->BufferSGArray, 0, 0);
    if (p->PhysicalPages) NdisFreeMemory(p->PhysicalPages, 0, 0);
    NdisFreeMemory(p, 0, 0);
}

CParaNdisRX::CParaNdisRX() : m_nReusedRxBuffersCounter(0), m_NetNofReceiveBuffers(0)
{
    InitializeListHead(&m_NetReceiveBuffers);

    NdisAllocateSpinLock(&m_UnclassifiedPacketsQueue.Lock);
    InitializeListHead(&m_UnclassifiedPacketsQueue.BuffersList);
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
        &m_Context->IODevice,
        m_Context->MiniportHandle))
    {
        DPrintf(0, ("CParaNdisRX::Create - virtqueue creation failed\n"));
        return false;
    }

    PrepareReceiveBuffers();

    m_nReusedRxBuffersLimit = m_Context->NetMaxReceiveBuffers / 4 + 1;

    CreatePath();

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
    DPrintf(0, "[%s] MaxReceiveBuffers %d\n", __FUNCTION__, m_Context->NetMaxReceiveBuffers);
    m_Reinsert = true;

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

    p->BufferSGLength = 0;
    while (ulNumPages > 0)
    {
        // Allocate the first page separately, the rest can be one contiguous block
        ULONG ulPagesToAlloc = (p->BufferSGLength == 0 ? 1 : ulNumPages);

        while (!ParaNdis_InitialAllocatePhysicalMemory(
                    m_Context,
                    PAGE_SIZE * ulPagesToAlloc,
                    &p->PhysicalPages[p->BufferSGLength]))
        {
            // Retry with half the pages
            if (ulPagesToAlloc == 1)
                goto error_exit;
            else
                ulPagesToAlloc /= 2;
        }

        p->BufferSGArray[p->BufferSGLength].physAddr = p->PhysicalPages[p->BufferSGLength].Physical;
        p->BufferSGArray[p->BufferSGLength].length = p->PhysicalPages[p->BufferSGLength].size;

        ulNumPages -= ulPagesToAlloc;
        p->BufferSGLength++;
    }

    //First page is for virtio header, size needs to be adjusted correspondingly
    p->BufferSGArray[0].length = m_Context->nVirtioHeaderSize;

    ULONG indirectAreaOffset = ALIGN_UP(m_Context->nVirtioHeaderSize, ULONGLONG);
    //Pre-cache indirect area addresses
    p->IndirectArea.Physical.QuadPart = p->PhysicalPages[0].Physical.QuadPart + indirectAreaOffset;
    p->IndirectArea.Virtual = RtlOffsetToPointer(p->PhysicalPages[0].Virtual, indirectAreaOffset);
    p->IndirectArea.size = PAGE_SIZE - indirectAreaOffset;

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
        pBufferDescriptor->BufferSGLength,
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
            DPrintf(0, " Error: NetNofReceiveBuffers > NetMaxReceiveBuffers(%d>%d)\n",
                m_NetNofReceiveBuffers, m_Context->NetMaxReceiveBuffers);
        }

        /* TODO - nReusedRXBuffers per queue or per context ?*/
        if (++m_nReusedRxBuffersCounter >= m_nReusedRxBuffersLimit)
        {
            m_nReusedRxBuffersCounter = 0;
            m_VirtQueue.Kick();
        }
    }
    else
    {
        /* TODO - NetMaxReceiveBuffers per queue or per context ?*/
        DPrintf(0, "FAILED TO REUSE THE BUFFER!!!!\n");
        ParaNdis_FreeRxBufferDescriptor(m_Context, pBuffersDescriptor);
        m_Context->NetMaxReceiveBuffers--;
    }
}

VOID CParaNdisRX::KickRXRing()
{
    m_VirtQueue.Kick();
}

#if PARANDIS_SUPPORT_RSS
static FORCEINLINE VOID ParaNdis_QueueRSSDpc(PARANDIS_ADAPTER *pContext, ULONG MessageIndex, PGROUP_AFFINITY pTargetAffinity)
{
    NdisMQueueDpcEx(pContext->InterruptHandle, MessageIndex, pTargetAffinity, NULL);
}

static FORCEINLINE CCHAR ParaNdis_GetScalingDataForPacket(PARANDIS_ADAPTER *pContext, PNET_PACKET_INFO pPacketInfo, PPROCESSOR_NUMBER pTargetProcessor)
{
    return ParaNdis6_RSSGetScalingDataForPacket(&pContext->RSSParameters, pPacketInfo, pTargetProcessor);
}
#endif

static FORCEINLINE BOOLEAN ParaNdis_PerformPacketAnalysis(
#if PARANDIS_SUPPORT_RSS
    PPARANDIS_RSS_PARAMS RSSParameters,
#endif
    PNET_PACKET_INFO PacketInfo,
    PVOID HeadersBuffer,
    ULONG DataLength)
{
    if (!ParaNdis_AnalyzeReceivedPacket(HeadersBuffer, DataLength, PacketInfo))
        return FALSE;

#if PARANDIS_SUPPORT_RSS
    if (RSSParameters->RSSMode != PARANDIS_RSS_DISABLED)
    {
        ParaNdis6_RSSAnalyzeReceivedPacket(RSSParameters, HeadersBuffer, PacketInfo);
    }
#endif
    return TRUE;
}

#define LogRedirectedPacket(p)
#if !defined(LogRedirectedPacket)
static void LogRedirectedPacket(pRxNetDescriptor pBufferDescriptor)
{
    NET_PACKET_INFO *pi = &pBufferDescriptor->PacketInfo;
    LPCSTR packetType = "Unknown";
    IPv4Header *pIp4Header = NULL;
    TCPHeader  *pTcpHeader = NULL;
    UDPHeader  *pUdpHeader = NULL;
    //IPv6Header *pIp6Header = NULL;
    switch (pi->RSSHash.Type)
    {
    case NDIS_HASH_TCP_IPV4:
        packetType = "TCP_IPV4";
        pIp4Header = (IPv4Header *)RtlOffsetToPointer(pi->headersBuffer, pi->L2HdrLen);
        pTcpHeader = (TCPHeader *)RtlOffsetToPointer(pIp4Header, pi->L3HdrLen);
        break;
    case NDIS_HASH_IPV4:
        packetType = "IPV4";
        pIp4Header = (IPv4Header *)RtlOffsetToPointer(pi->headersBuffer, pi->L2HdrLen);
        break;
    case NDIS_HASH_TCP_IPV6:
        packetType = "TCP_IPV6";
        break;
    case NDIS_HASH_TCP_IPV6_EX:
        packetType = "TCP_IPV6EX";
        break;
    case NDIS_HASH_IPV6_EX:
        packetType = "TCP_IPV6EX";
        break;
    case NDIS_HASH_IPV6:
        packetType = "TCP_IPV6";
        break;
#if (NDIS_SUPPORT_NDIS680)
    case NDIS_HASH_UDP_IPV4:
        packetType = "UDP_IPV4";
        pIp4Header = (IPv4Header *)RtlOffsetToPointer(pi->headersBuffer, pi->L2HdrLen);
        pUdpHeader = (UDPHeader *)RtlOffsetToPointer(pIp4Header, pi->L3HdrLen);
        break;
    case NDIS_HASH_UDP_IPV6:
        packetType = "UDP_IPV6";
        break;
    case NDIS_HASH_UDP_IPV6_EX:
        packetType = "UDP_IPV6EX";
        break;
#endif
    default:
        break;
    }
    if (pTcpHeader)
    {
        TraceNoPrefix(0, "%s: %s %d.%d.%d.%d:%d->%d.%d.%d.%d:%d\n", __FUNCTION__, packetType,
            pIp4Header->ip_srca[0], pIp4Header->ip_srca[1], pIp4Header->ip_srca[2], pIp4Header->ip_srca[3],
            RtlUshortByteSwap(pTcpHeader->tcp_src),
            pIp4Header->ip_desta[0], pIp4Header->ip_desta[1], pIp4Header->ip_desta[2], pIp4Header->ip_desta[3],
            RtlUshortByteSwap(pTcpHeader->tcp_dest));
    }
    else if (pUdpHeader)
    {
        TraceNoPrefix(0, "%s: %s %d.%d.%d.%d:%d->%d.%d.%d.%d:%d\n", __FUNCTION__, packetType,
            pIp4Header->ip_srca[0], pIp4Header->ip_srca[1], pIp4Header->ip_srca[2], pIp4Header->ip_srca[3],
            RtlUshortByteSwap(pUdpHeader->udp_src),
            pIp4Header->ip_desta[0], pIp4Header->ip_desta[1], pIp4Header->ip_desta[2], pIp4Header->ip_desta[3],
            RtlUshortByteSwap(pUdpHeader->udp_dest));
    }
    else if (pIp4Header)
    {
        TraceNoPrefix(0, "%s: %s %d.%d.%d.%d(%d)->%d.%d.%d.%d\n", __FUNCTION__, packetType,
            pIp4Header->ip_srca[0], pIp4Header->ip_srca[1], pIp4Header->ip_srca[2], pIp4Header->ip_srca[3],
            pIp4Header->ip_protocol,
            pIp4Header->ip_desta[0], pIp4Header->ip_desta[1], pIp4Header->ip_desta[2], pIp4Header->ip_desta[3]);
    }
    else
    {
        TraceNoPrefix(0, "%s: %s\n", __FUNCTION__, packetType);
    }
}
#endif

VOID CParaNdisRX::ProcessRxRing(CCHAR nCurrCpuReceiveQueue)
{
    pRxNetDescriptor pBufferDescriptor;
    unsigned int nFullLength;

#ifndef PARANDIS_SUPPORT_RSS
    UNREFERENCED_PARAMETER(nCurrCpuReceiveQueue);
#endif

    TDPCSpinLocker autoLock(m_Lock);

    while (NULL != (pBufferDescriptor = (pRxNetDescriptor)m_VirtQueue.GetBuf(&nFullLength)))
    {
        RemoveEntryList(&pBufferDescriptor->listEntry);
        m_NetNofReceiveBuffers--;

        BOOLEAN packetAnalysisRC;

        packetAnalysisRC = ParaNdis_PerformPacketAnalysis(
#if PARANDIS_SUPPORT_RSS
            &m_Context->RSSParameters,
#endif

            &pBufferDescriptor->PacketInfo,
            pBufferDescriptor->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE].Virtual,
            nFullLength - m_Context->nVirtioHeaderSize);


        if (!packetAnalysisRC)
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
            m_Context->extraStatistics.framesRSSUnclassified++;
        }
        else
        {
            ParaNdis_ReceiveQueueAddBuffer(&m_Context->ReceiveQueues[nTargetReceiveQueueNum], pBufferDescriptor);

            if (nTargetReceiveQueueNum != nCurrCpuReceiveQueue)
            {
                ParaNdis_ProcessorNumberToGroupAffinity(&TargetAffinity, &TargetProcessor);
                ParaNdis_QueueRSSDpc(m_Context, m_messageIndex, &TargetAffinity);
                m_Context->extraStatistics.framesRSSMisses++;
                LogRedirectedPacket(pBufferDescriptor);
            }
            else
            {
                m_Context->extraStatistics.framesRSSHits++;
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
    TPassiveSpinLocker autoLock(m_Lock);


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
            DPrintf(0, "FAILED TO REUSE THE BUFFER!!!!\n");
            ParaNdis_FreeRxBufferDescriptor(m_Context, pBufferDescriptor);
            m_Context->NetMaxReceiveBuffers--;
        }
    }
    m_Reinsert = true;
}

BOOLEAN CParaNdisRX::RestartQueue()
{
    return ParaNdis_SynchronizeWithInterrupt(m_Context,
                                             m_messageIndex,
                                             RestartQueueSynchronously,
                                             this);
}

#ifdef PARANDIS_SUPPORT_RSS
VOID ParaNdis_ResetRxClassification(PARANDIS_ADAPTER *pContext)
{
    ULONG i;

    for (i = PARANDIS_FIRST_RSS_RECEIVE_QUEUE; i < ARRAYSIZE(pContext->ReceiveQueues); i++)
    {
        PPARANDIS_RECEIVE_QUEUE pCurrQueue = &pContext->ReceiveQueues[i];
        NdisAcquireSpinLock(&pCurrQueue->Lock);

        while (!IsListEmpty(&pCurrQueue->BuffersList))
        {
            PLIST_ENTRY pListEntry = RemoveHeadList(&pCurrQueue->BuffersList);
            pRxNetDescriptor pBufferDescriptor = CONTAINING_RECORD(pListEntry, RxNetDescriptor, ReceiveQueueListEntry);
            ParaNdis_ReceiveQueueAddBuffer(&pBufferDescriptor->Queue->UnclassifiedPacketsQueue(), pBufferDescriptor);
        }

        NdisReleaseSpinLock(&pCurrQueue->Lock);
    }
}
#endif
