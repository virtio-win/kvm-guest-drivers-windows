#include "ndis56common.h"
#include "kdebugprint.h"
#include "ParaNdis_DebugHistory.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_RX.tmh"
#endif

// define as 0 to allocate all the required buffer at once
// #define INITIAL_RX_BUFFERS  0
#define INITIAL_RX_BUFFERS 16

static FORCEINLINE VOID ParaNdis_ReceiveQueueAddBuffer(PPARANDIS_RECEIVE_QUEUE pQueue, pRxNetDescriptor pBuffer)
{
    NdisInterlockedInsertTailList(&pQueue->BuffersList, &pBuffer->ReceiveQueueListEntry, &pQueue->Lock);
}

static void ParaNdis_UnbindRxBufferFromPacket(pRxNetDescriptor p)
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

static BOOLEAN ParaNdis_BindRxBufferToPacket(PARANDIS_ADAPTER *pContext, pRxNetDescriptor p)
{
    ULONG i;
    PMDL *NextMdlLinkage = &p->Holder;

    for (i = PARANDIS_FIRST_RX_DATA_PAGE; i < p->BufferSGLength; i++)
    {
        *NextMdlLinkage = NdisAllocateMdl(pContext->MiniportHandle,
                                          p->PhysicalPages[i].Virtual,
                                          p->PhysicalPages[i].size);
        if (*NextMdlLinkage == NULL)
        {
            goto error_exit;
        }

        NextMdlLinkage = &(NDIS_MDL_LINKAGE(*NextMdlLinkage));
    }
    *NextMdlLinkage = NULL;

    return TRUE;

error_exit:

    ParaNdis_UnbindRxBufferFromPacket(p);
    return FALSE;
}

static bool IsRegionInside(const tCompletePhysicalAddress &a1, const tCompletePhysicalAddress &a2)
{
    const LONGLONG &p1 = a1.Physical.QuadPart;
    const LONGLONG &p2 = a2.Physical.QuadPart;
    return p1 >= p2 && p1 <= p2 + a2.size;
}

static void ParaNdis_FreeRxBufferDescriptor(PARANDIS_ADAPTER *pContext, pRxNetDescriptor p)
{
    ULONG i;

    ParaNdis_UnbindRxBufferFromPacket(p);
    for (i = 0; i < p->BufferSGLength; i++)
    {
        if (!p->PhysicalPages[i].Virtual)
        {
            break;
        }
        // do not try do free the region derived from header block
        if (i != 0 && IsRegionInside(p->PhysicalPages[i], p->PhysicalPages[0]))
        {
            continue;
        }
        ParaNdis_FreePhysicalMemory(pContext, &p->PhysicalPages[i]);
    }

    if (p->BufferSGArray)
    {
        NdisFreeMemory(p->BufferSGArray, 0, 0);
    }
    if (p->PhysicalPages)
    {
        NdisFreeMemory(p->PhysicalPages, 0, 0);
    }
    NdisFreeMemory(p, 0, 0);
}

CParaNdisRX::CParaNdisRX()
{
    InitializeListHead(&m_NetReceiveBuffers);
}

CParaNdisRX::~CParaNdisRX()
{
}

// called during initialization
// also later during additional allocations under m_Lock
// when we update m_NetMaxReceiveBuffers, we also update
// m_nReusedRxBuffersLimit, set m_nReusedRxBuffersLimit to zero
// and kick the rx queue
void CParaNdisRX::RecalculateLimits()
{
    m_nReusedRxBuffersLimit = m_NetMaxReceiveBuffers / 4 + 1;
    m_nReusedRxBuffersCounter = 0;
    m_MinRxBufferLimit = m_NetMaxReceiveBuffers * m_Context->MinRxBufferPercent / 100;
    DPrintf(0,
            "[%s] m_NetMaxReceiveBuffers %d, m_MinRxBufferLimit %u\n",
            __FUNCTION__,
            m_NetMaxReceiveBuffers,
            m_MinRxBufferLimit);
}

bool CParaNdisRX::Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex)
{
    m_Context = Context;
    m_queueIndex = (u16)DeviceQueueIndex;
    m_NetMaxReceiveBuffers = Context->bFastInit ? INITIAL_RX_BUFFERS : 0;
    if (!m_NetMaxReceiveBuffers || m_NetMaxReceiveBuffers > Context->maxRxBufferPerQueue)
    {
        m_NetMaxReceiveBuffers = Context->maxRxBufferPerQueue;
    }

    if (!m_VirtQueue.Create(DeviceQueueIndex, &m_Context->IODevice, m_Context->MiniportHandle))
    {
        DPrintf(0, ("CParaNdisRX::Create - virtqueue creation failed\n"));
        return false;
    }

    PrepareReceiveBuffers();

    CreatePath();

    return true;
}

int CParaNdisRX::PrepareReceiveBuffers()
{
    int nRet = 0;
    UINT i;
    DEBUG_ENTRY(4);

    for (i = 0; i < m_NetMaxReceiveBuffers; ++i)
    {
        pRxNetDescriptor pBuffersDescriptor = CreateRxDescriptorOnInit();
        if (!pBuffersDescriptor)
        {
            break;
        }

        pBuffersDescriptor->Queue = this;

        if (!AddRxBufferToQueue(pBuffersDescriptor))
        {
            ParaNdis_FreeRxBufferDescriptor(m_Context, pBuffersDescriptor);
            break;
        }

        InsertTailList(&m_NetReceiveBuffers, &pBuffersDescriptor->listEntry);

        m_NetNofReceiveBuffers++;
    }
    m_NetMaxReceiveBuffers = m_NetNofReceiveBuffers;

    RecalculateLimits();

    if (m_Context->extraStatistics.minFreeRxBuffers == 0 ||
        m_Context->extraStatistics.minFreeRxBuffers > m_NetNofReceiveBuffers)
    {
        m_Context->extraStatistics.minFreeRxBuffers = m_NetNofReceiveBuffers;
    }
    m_Reinsert = true;

    return nRet;
}

pRxNetDescriptor CParaNdisRX::CreateRxDescriptorOnInit()
{
    // For RX packets we allocate following pages
    //   X pages needed to fit most of data payload (or all the payload)
    //   1 page or less for virtio header, indirect buffers array and the data tail if any
    //   virtio header and indirect buffers take ~300 bytes
    //   if the data tail (payload % page size) is small it is also goes to the header block
    ULONG ulNumDataPages = m_Context->RxLayout.TotalAllocationsPerBuffer; // including header block
    ULONG sgArraySize = m_Context->RxLayout.IndirectEntries;

    pRxNetDescriptor p = (pRxNetDescriptor)ParaNdis_AllocateMemory(m_Context, sizeof(*p));
    if (p == NULL)
    {
        return NULL;
    }

    NdisZeroMemory(p, sizeof(*p));

    p->BufferSGArray = (struct
                        VirtIOBufferDescriptor *)ParaNdis_AllocateMemory(m_Context,
                                                                         sizeof(*p->BufferSGArray) * sgArraySize);
    if (p->BufferSGArray == NULL)
    {
        goto error_exit;
    }

    p->PhysicalPages = (tCompletePhysicalAddress *)ParaNdis_AllocateMemory(m_Context,
                                                                           sizeof(*p->PhysicalPages) * sgArraySize);
    if (p->PhysicalPages == NULL)
    {
        goto error_exit;
    }

    // must initialize for case of exit in the middle of the loop
    NdisZeroMemory(p->PhysicalPages, sizeof(*p->PhysicalPages) * sgArraySize);

    p->BufferSGLength = 0;

    while (ulNumDataPages > 0)
    {
        // Allocate the first block separately, the rest can be one contiguous block
        ULONG ulPagesToAlloc = (p->BufferSGLength == 0) ? 1 : ulNumDataPages;
        ULONG sizeToAlloc = (p->BufferSGLength == 0) ? m_Context->RxLayout.HeaderPageAllocation
                                                     : PAGE_SIZE * ulPagesToAlloc;

        while (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, sizeToAlloc, &p->PhysicalPages[p->BufferSGLength]))
        {
            // Retry with half the pages
            if (ulPagesToAlloc == 1)
            {
                goto error_exit;
            }
            else
            {
                ulPagesToAlloc /= 2;
                sizeToAlloc = PAGE_SIZE * ulPagesToAlloc;
            }
        }

        p->BufferSGArray[p->BufferSGLength].physAddr = p->PhysicalPages[p->BufferSGLength].Physical;
        p->BufferSGArray[p->BufferSGLength].length = p->PhysicalPages[p->BufferSGLength].size;

        ulNumDataPages -= ulPagesToAlloc;
        p->BufferSGLength++;
    }

    // First page is for virtio header, size needs to be adjusted correspondingly
    p->BufferSGArray[0].length = m_Context->nVirtioHeaderSize;

    ULONG offsetInTheHeader = m_Context->RxLayout.ReserveForHeader;
    // Pre-cache indirect area addresses
    p->IndirectArea.Physical.QuadPart = p->PhysicalPages[0].Physical.QuadPart + offsetInTheHeader;
    p->IndirectArea.Virtual = RtlOffsetToPointer(p->PhysicalPages[0].Virtual, offsetInTheHeader);
    p->IndirectArea.size = m_Context->RxLayout.ReserveForIndirectArea;

    if (m_Context->RxLayout.ReserveForPacketTail)
    {
        // the payload tail is located in the header block
        offsetInTheHeader += m_Context->RxLayout.ReserveForIndirectArea;

        // fill the tail's physical page fields
        p->PhysicalPages[p->BufferSGLength].Physical.QuadPart = p->PhysicalPages[0].Physical.QuadPart +
                                                                offsetInTheHeader;
        p->PhysicalPages[p->BufferSGLength].Virtual = RtlOffsetToPointer(p->PhysicalPages[0].Virtual,
                                                                         offsetInTheHeader);
        p->PhysicalPages[p->BufferSGLength].size = m_Context->RxLayout.ReserveForPacketTail;

        // fill the tail's SG buffer fields
        p->BufferSGArray[p->BufferSGLength].physAddr.QuadPart = p->PhysicalPages[p->BufferSGLength].Physical.QuadPart;
        p->BufferSGArray[p->BufferSGLength].length = p->PhysicalPages[p->BufferSGLength].size;
        p->BufferSGLength++;
    }
    else
    {
        // the payload tail is located in the full data block
        // and was already allocated and counted
    }

    if (!ParaNdis_BindRxBufferToPacket(m_Context, p))
    {
        goto error_exit;
    }

    return p;

error_exit:
    ParaNdis_FreeRxBufferDescriptor(m_Context, p);
    return NULL;
}

/* must be called on PASSIVE from system thread */
BOOLEAN CParaNdisRX::AllocateMore()
{
    BOOLEAN result = false;

    // if the queue is not ready, try again later
    if (!m_pVirtQueue->IsValid() || !m_Reinsert)
    {
        DPrintf(1, " Queue is not ready, try later\n");
        return true;
    }

    if (m_NetMaxReceiveBuffers >= m_Context->maxRxBufferPerQueue ||
        m_NetMaxReceiveBuffers >= m_pVirtQueue->GetRingSize())
    {
        return result;
    }
    pRxNetDescriptor pBuffersDescriptor = CreateRxDescriptorOnInit();

    TPassiveSpinLocker autoLock(m_Lock);

    if (pBuffersDescriptor)
    {
        pBuffersDescriptor->Queue = this;
        if (m_pVirtQueue->CanTouchHardware() && AddRxBufferToQueue(pBuffersDescriptor))
        {
            InsertTailList(&m_NetReceiveBuffers, &pBuffersDescriptor->listEntry);
            m_NetNofReceiveBuffers++;
            m_NetMaxReceiveBuffers++;
            RecalculateLimits();
            KickRXRing();
            result = true;
        }
        else
        {
            ParaNdis_FreeRxBufferDescriptor(m_Context, pBuffersDescriptor);
        }
    }
    return result;
}

/* TODO - make it method in pRXNetDescriptor */
BOOLEAN CParaNdisRX::AddRxBufferToQueue(pRxNetDescriptor pBufferDescriptor)
{
    return 0 <=
           pBufferDescriptor->Queue->m_VirtQueue.AddBuf(pBufferDescriptor->BufferSGArray,
                                                        0,
                                                        pBufferDescriptor->BufferSGLength,
                                                        pBufferDescriptor,
                                                        m_Context->bUseIndirect ? pBufferDescriptor->IndirectArea.Virtual
                                                                                : NULL,
                                                        m_Context->bUseIndirect ? pBufferDescriptor->IndirectArea.Physical.QuadPart
                                                                                : 0);
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

        if (m_NetNofReceiveBuffers > m_NetMaxReceiveBuffers)
        {
            DPrintf(0,
                    " Error: m_NetNofReceiveBuffers > m_NetMaxReceiveBuffers (%d>%d)\n",
                    m_NetNofReceiveBuffers,
                    m_NetMaxReceiveBuffers);
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
        m_NetMaxReceiveBuffers--;
    }
}

VOID CParaNdisRX::KickRXRing()
{
    m_VirtQueue.Kick();
}

#if PARANDIS_SUPPORT_RSS
static FORCEINLINE VOID ParaNdis_QueueRSSDpc(PARANDIS_ADAPTER *pContext,
                                             ULONG MessageIndex,
                                             PGROUP_AFFINITY pTargetAffinity)
{
    NdisMQueueDpcEx(pContext->InterruptHandle, MessageIndex, pTargetAffinity, NULL);
}

static FORCEINLINE CCHAR ParaNdis_GetScalingDataForPacket(PARANDIS_ADAPTER *pContext,
                                                          PNET_PACKET_INFO pPacketInfo,
                                                          PPROCESSOR_NUMBER pTargetProcessor)
{
    return ParaNdis6_RSSGetScalingDataForPacket(&pContext->RSSParameters, pPacketInfo, pTargetProcessor);
}
#endif

static ULONG ShallPassPacket(PARANDIS_ADAPTER *pContext, PNET_PACKET_INFO pPacketInfo)
{
    ULONG i;

    if (pPacketInfo->dataLength > pContext->MaxPacketSize.nMaxFullSizeOsRx + ETH_PRIORITY_HEADER_SIZE)
    {
        return FALSE;
    }

    if ((pPacketInfo->dataLength > pContext->MaxPacketSize.nMaxFullSizeOsRx) && !pPacketInfo->hasVlanHeader)
    {
        return FALSE;
    }

    if (IsVlanSupported(pContext) && pPacketInfo->hasVlanHeader)
    {
        if (pContext->VlanId && pContext->VlanId != pPacketInfo->Vlan.VlanId)
        {
            return FALSE;
        }
    }

    if (pContext->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
    {
        return TRUE;
    }

    if (pPacketInfo->isUnicast)
    {
        ULONG Res;

        if (!(pContext->PacketFilter & NDIS_PACKET_TYPE_DIRECTED))
        {
            return FALSE;
        }

        ETH_COMPARE_NETWORK_ADDRESSES_EQ_SAFE(pPacketInfo->ethDestAddr, pContext->CurrentMacAddress, &Res);
        return !Res;
    }

    if (pPacketInfo->isBroadcast)
    {
        return (pContext->PacketFilter & NDIS_PACKET_TYPE_BROADCAST);
    }

    // Multi-cast

    if (pContext->PacketFilter & NDIS_PACKET_TYPE_ALL_MULTICAST)
    {
        return TRUE;
    }

    if (!(pContext->PacketFilter & NDIS_PACKET_TYPE_MULTICAST))
    {
        return FALSE;
    }

    for (i = 0; i < pContext->MulticastData.nofMulticastEntries; i++)
    {
        ULONG Res;
        PUCHAR CurrMcastAddr = &pContext->MulticastData.MulticastList[i * ETH_ALEN];

        ETH_COMPARE_NETWORK_ADDRESSES_EQ_SAFE(pPacketInfo->ethDestAddr, CurrMcastAddr, &Res);

        if (!Res)
        {
            return TRUE;
        }
    }

    return FALSE;
}

#define LogRedirectedPacket(p)
#if !defined(LogRedirectedPacket)
static void LogRedirectedPacket(pRxNetDescriptor pBufferDescriptor)
{
    NET_PACKET_INFO *pi = &pBufferDescriptor->PacketInfo;
    LPCSTR packetType = "Unknown";
    IPv4Header *pIp4Header = NULL;
    TCPHeader *pTcpHeader = NULL;
    UDPHeader *pUdpHeader = NULL;
    // IPv6Header *pIp6Header = NULL;
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
            packetType = "IPV6EX";
            break;
        case NDIS_HASH_IPV6:
            packetType = "IPV6";
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
        TraceNoPrefix(0,
                      "%s: %s %d.%d.%d.%d:%d->%d.%d.%d.%d:%d\n",
                      __FUNCTION__,
                      packetType,
                      pIp4Header->ip_srca[0],
                      pIp4Header->ip_srca[1],
                      pIp4Header->ip_srca[2],
                      pIp4Header->ip_srca[3],
                      RtlUshortByteSwap(pTcpHeader->tcp_src),
                      pIp4Header->ip_desta[0],
                      pIp4Header->ip_desta[1],
                      pIp4Header->ip_desta[2],
                      pIp4Header->ip_desta[3],
                      RtlUshortByteSwap(pTcpHeader->tcp_dest));
    }
    else if (pUdpHeader)
    {
        TraceNoPrefix(0,
                      "%s: %s %d.%d.%d.%d:%d->%d.%d.%d.%d:%d\n",
                      __FUNCTION__,
                      packetType,
                      pIp4Header->ip_srca[0],
                      pIp4Header->ip_srca[1],
                      pIp4Header->ip_srca[2],
                      pIp4Header->ip_srca[3],
                      RtlUshortByteSwap(pUdpHeader->udp_src),
                      pIp4Header->ip_desta[0],
                      pIp4Header->ip_desta[1],
                      pIp4Header->ip_desta[2],
                      pIp4Header->ip_desta[3],
                      RtlUshortByteSwap(pUdpHeader->udp_dest));
    }
    else if (pIp4Header)
    {
        TraceNoPrefix(0,
                      "%s: %s %d.%d.%d.%d(%d)->%d.%d.%d.%d\n",
                      __FUNCTION__,
                      packetType,
                      pIp4Header->ip_srca[0],
                      pIp4Header->ip_srca[1],
                      pIp4Header->ip_srca[2],
                      pIp4Header->ip_srca[3],
                      pIp4Header->ip_protocol,
                      pIp4Header->ip_desta[0],
                      pIp4Header->ip_desta[1],
                      pIp4Header->ip_desta[2],
                      pIp4Header->ip_desta[3]);
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

    if (m_Context->extraStatistics.minFreeRxBuffers > m_NetNofReceiveBuffers)
    {
        m_Context->extraStatistics.minFreeRxBuffers = m_NetNofReceiveBuffers;
    }

    while (NULL != (pBufferDescriptor = (pRxNetDescriptor)m_VirtQueue.GetBuf(&nFullLength)))
    {
        RemoveEntryList(&pBufferDescriptor->listEntry);
        m_NetNofReceiveBuffers--;

        // basic MAC-based analysis + L3 header info
        BOOLEAN packetAnalysisRC = ParaNdis_AnalyzeReceivedPacket(pBufferDescriptor->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE].Virtual,
                                                                  nFullLength - m_Context->nVirtioHeaderSize,
                                                                  &pBufferDescriptor->PacketInfo);

        if (!packetAnalysisRC)
        {
            pBufferDescriptor->Queue->ReuseReceiveBufferNoLock(pBufferDescriptor);
            m_Context->Statistics.ifInErrors++;
            m_Context->Statistics.ifInDiscards++;
            continue;
        }

        // filtering based on prev stage analysis
        if (!ShallPassPacket(m_Context, &pBufferDescriptor->PacketInfo))
        {
            pBufferDescriptor->Queue->ReuseReceiveBufferNoLock(pBufferDescriptor);
            m_Context->Statistics.ifInDiscards++;
            m_Context->extraStatistics.framesFilteredOut++;
            continue;
        }
#ifdef PARANDIS_SUPPORT_RSS
        if (m_Context->RSSParameters.RSSMode != PARANDIS_RSS_MODE::PARANDIS_RSS_DISABLED)
        {
            ParaNdis6_RSSAnalyzeReceivedPacket(&m_Context->RSSParameters,
                                               pBufferDescriptor->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE].Virtual,
                                               &pBufferDescriptor->PacketInfo);
        }
        CCHAR nTargetReceiveQueueNum;
        GROUP_AFFINITY TargetAffinity;
        PROCESSOR_NUMBER TargetProcessor;

        nTargetReceiveQueueNum = ParaNdis_GetScalingDataForPacket(m_Context,
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
                if (m_Context->bPollModeEnabled)
                {
                    // ensure the NDIS just schedules the other poll and does not do anything
                    // otherwise if both polls are configured to the same CPU
                    // this may cause a deadlock in return nbl path
                    KIRQL prev = KeRaiseIrqlToSynchLevel();
                    ParaNdisPollNotify(m_Context, nTargetReceiveQueueNum, "RSS");
                    KeLowerIrql(prev);
                }
                else
                {
                    ParaNdis_ProcessorNumberToGroupAffinity(&TargetAffinity, &TargetProcessor);
                    ParaNdis_QueueRSSDpc(m_Context, m_messageIndex, &TargetAffinity);
                }
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
        pRxNetDescriptor pBufferDescriptor = (pRxNetDescriptor)RemoveHeadList(&m_NetReceiveBuffers);
        InsertTailList(&TempList, &pBufferDescriptor->listEntry);
    }
    m_NetNofReceiveBuffers = 0;
    while (!IsListEmpty(&TempList))
    {
        pRxNetDescriptor pBufferDescriptor = (pRxNetDescriptor)RemoveHeadList(&TempList);
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
            m_NetMaxReceiveBuffers--;
        }
    }
    m_Reinsert = true;
}

BOOLEAN CParaNdisRX::RestartQueue()
{
    return ParaNdis_SynchronizeWithInterrupt(m_Context, m_messageIndex, RestartQueueSynchronously, this);
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
