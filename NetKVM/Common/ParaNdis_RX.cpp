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

    while (NextMdlLinkage != NULL)
    {
        PMDL pThisMDL = NextMdlLinkage;
        NextMdlLinkage = NDIS_MDL_LINKAGE(pThisMDL);

        NdisFreeMdl(pThisMDL);
    }
}

static BOOLEAN ParaNdis_BindRxBufferToPacket(PARANDIS_ADAPTER *pContext, pRxNetDescriptor p)
{
    ULONG i, offset = p->DataStartOffset;
    PMDL *NextMdlLinkage = &p->Holder;

    // for first page adjust the start and size of the MDL.
    // It would be better to span the MDL on entire page and
    // create the NBL with offset. But in 2 NDIS tests (RSS and
    // SendReceiveReply) the protocol driver fails to recognize
    // the packet pattern because it is looking for it in wrong
    // place, i.e. the driver fails to process the NB with offset
    // that is not zero. TODO: open the bug report.
    for (i = p->FirstRxDataPage; i < p->NumPages; i++)
    {
        *NextMdlLinkage = NdisAllocateMdl(pContext->MiniportHandle,
                                          RtlOffsetToPointer(p->PhysicalPages[i].Virtual, offset),
                                          p->PhysicalPages[i].size - offset);
        offset = 0;

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

    // Free pre-allocated full page MDL (for mergeable buffers)
    if (p->FullPageMDL)
    {
        NdisFreeMdl(p->FullPageMDL);
        p->FullPageMDL = NULL;
    }

    for (i = 0; i < p->NumOwnedPages; i++)
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
    DPrintf(0, "m_NetMaxReceiveBuffers %d, m_MinRxBufferLimit %u", m_NetMaxReceiveBuffers, m_MinRxBufferLimit);
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
        DPrintf(0, "CParaNdisRX::Create - virtqueue creation failed");
        return false;
    }

    PrepareReceiveBuffers();

    CreatePath();

    return true;
}

static void DumpDescriptor(pRxNetDescriptor p, int level)
{
    USHORT i;
    for (i = 0; i < p->NumPages; ++i)
    {
        auto &page = p->PhysicalPages[i];
        DPrintf(level, "page[%d]: %p of %d", i, (PVOID)page.Physical.QuadPart, page.size);
    }
    for (i = 0; i < p->BufferSGLength; ++i)
    {
        auto &sg = p->BufferSGArray[i];
        DPrintf(level, "sg[%d]: %p of %d", i, (PVOID)sg.physAddr.QuadPart, sg.length);
    }
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

        DumpDescriptor(pBuffersDescriptor, i == 0 ? 2 : 7);

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

// Simplified buffer creation for mergeable buffers - just 1 page per buffer
pRxNetDescriptor CParaNdisRX::CreateMergeableRxDescriptorOnInit()
{
    pRxNetDescriptor p = (pRxNetDescriptor)ParaNdis_AllocateMemory(m_Context, sizeof(*p));
    if (p == NULL)
    {
        DPrintf(0, "ERROR: Failed to allocate memory for RX descriptor");
        return NULL;
    }

    NdisZeroMemory(p, sizeof(*p));

    p->BufferSGArray = (struct VirtIOBufferDescriptor *)ParaNdis_AllocateMemory(m_Context,
                                                                                sizeof(VirtIOBufferDescriptor));
    if (p->BufferSGArray == NULL)
    {
        DPrintf(0, "ERROR: Failed to allocate SG array");
        goto error_exit;
    }

    p->PhysicalPages = (tCompletePhysicalAddress *)ParaNdis_AllocateMemory(m_Context, sizeof(tCompletePhysicalAddress));
    if (p->PhysicalPages == NULL)
    {
        DPrintf(0, "ERROR: Failed to allocate PhysicalPages array");
        goto error_exit;
    }

    NdisZeroMemory(p->PhysicalPages, sizeof(tCompletePhysicalAddress));

    p->OriginalPhysicalPages = p->PhysicalPages;

    if (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, PAGE_SIZE, &p->PhysicalPages[0]))
    {
        DPrintf(0, "ERROR: Failed to allocate physical memory (1 page = %u bytes)", PAGE_SIZE);
        goto error_exit;
    }

    // Setup for mergeable buffer with ANY_LAYOUT
    // Physical allocation: Single buffer containing virtio header + payload
    // Logical layout: NumPages=1, single page for header+data
    p->NumPages = 1;
    p->NumOwnedPages = 1;
    p->HeaderPage = 0;
    p->FirstRxDataPage = 0;
    p->DataStartOffset = (USHORT)m_Context->nVirtioHeaderSize;

    // Combined header and data in single SG entry (ANY_LAYOUT)
    p->BufferSGLength = 1;
    p->BufferSGArray[0].physAddr = p->PhysicalPages[0].Physical;
    p->BufferSGArray[0].length = PAGE_SIZE;

    // Pre-allocate MDL covering entire page to avoid MDL allocation/free
    // in hot path during packet assembly/disassembly.
    // The usable area is limited by NET_BUFFER length, not MDL size.
    // For merged packets, we just chain pre-allocated MDLs together.
    p->FullPageMDL = NdisAllocateMdl(m_Context->MiniportHandle, p->PhysicalPages[0].Virtual, p->PhysicalPages[0].size);
    if (p->FullPageMDL == NULL)
    {
        DPrintf(0, "ERROR: Failed to pre-allocate full page MDL");
        goto error_exit;
    }
    NDIS_MDL_LINKAGE(p->FullPageMDL) = NULL;

    if (!ParaNdis_BindRxBufferToPacket(m_Context, p))
    {
        DPrintf(0, "ERROR: Failed to bind RX buffer to packet");
        goto error_exit;
    }

    return p;

error_exit:
    ParaNdis_FreeRxBufferDescriptor(m_Context, p);
    return NULL;
}

pRxNetDescriptor CParaNdisRX::CreateRxDescriptorOnInit()
{
    if (m_Context->bUseMergedBuffers && m_Context->bMergeableBuffersConfigured && m_Context->bAnyLayout &&
        m_Context->RxLayout.TotalAllocationsPerBuffer > 1)
    {
        DPrintf(5, "Using mergeable buffer allocation");
        return CreateMergeableRxDescriptorOnInit();
    }

    // For RX packets we allocate following pages
    //   X pages needed to fit most of data payload (or all the payload)
    //   1 page or less for virtio header, indirect buffers array and the data tail if any
    //   virtio header and indirect buffers take ~300 bytes
    //   if the data tail (payload % page size) is small it is also goes to the header block
    ULONG ulNumDataPages = m_Context->RxLayout.TotalAllocationsPerBuffer; // including header block
    ULONG sgArraySize = m_Context->RxLayout.IndirectEntries;
    bool bLargeSingleAllocation = ulNumDataPages > 1 && m_Context->bRxSeparateTail == 0 &&
                                  m_Context->RxLayout.ReserveForPacketTail;

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
    p->HeaderPage = m_Context->RxLayout.ReserveForHeader ? 0 : 1;
    p->FirstRxDataPage = 1;
    p->DataStartOffset = (p->HeaderPage == 0) ? 0 : (USHORT)m_Context->nVirtioHeaderSize;
    p->OriginalPhysicalPages = p->PhysicalPages;
    auto &pageNumber = p->NumPages;

    while (ulNumDataPages > 0)
    {
        // Allocate the first block separately, the rest can be one contiguous block
        ULONG ulPagesToAlloc = (pageNumber == 0) ? 1 : ulNumDataPages;
        ULONG sizeToAlloc = (pageNumber == 0) ? m_Context->RxLayout.HeaderPageAllocation : PAGE_SIZE * ulPagesToAlloc;
        if (pageNumber > 0 && bLargeSingleAllocation)
        {
            sizeToAlloc += m_Context->RxLayout.ReserveForPacketTail;
        }

        while (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, sizeToAlloc, &p->PhysicalPages[pageNumber]))
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
                bLargeSingleAllocation = false;
            }
        }

        if (pageNumber || p->HeaderPage == 0)
        {
            p->BufferSGArray[p->BufferSGLength].physAddr = p->PhysicalPages[pageNumber].Physical;
            p->BufferSGArray[p->BufferSGLength].length = p->PhysicalPages[pageNumber].size;
            p->BufferSGLength++;
        }
        pageNumber++;
        ulNumDataPages -= ulPagesToAlloc;
    }

    // First page is for virtio header, size needs to be adjusted correspondingly
    if (p->HeaderPage == 0)
    {
        p->BufferSGArray[0].length = m_Context->nVirtioHeaderSize;
    }

    ULONG offsetInTheHeader = m_Context->RxLayout.ReserveForHeader;
    // Pre-cache indirect area addresses
    p->IndirectArea.Physical.QuadPart = p->PhysicalPages[0].Physical.QuadPart + offsetInTheHeader;
    p->IndirectArea.Virtual = RtlOffsetToPointer(p->PhysicalPages[0].Virtual, offsetInTheHeader);
    p->IndirectArea.size = m_Context->RxLayout.ReserveForIndirectArea;

    if (m_Context->RxLayout.ReserveForPacketTail && !bLargeSingleAllocation)
    {
        // the payload tail is located in the header block
        offsetInTheHeader += m_Context->RxLayout.ReserveForIndirectArea;

        // fill the tail's physical page fields
        p->PhysicalPages[pageNumber].Physical.QuadPart = p->PhysicalPages[0].Physical.QuadPart + offsetInTheHeader;
        p->PhysicalPages[pageNumber].Virtual = RtlOffsetToPointer(p->PhysicalPages[0].Virtual, offsetInTheHeader);
        p->PhysicalPages[pageNumber].size = m_Context->RxLayout.ReserveForPacketTail;

        // fill the tail's SG buffer fields
        p->BufferSGArray[p->BufferSGLength].physAddr.QuadPart = p->PhysicalPages[pageNumber].Physical.QuadPart;
        p->BufferSGArray[p->BufferSGLength].length = p->PhysicalPages[pageNumber].size;
        p->BufferSGLength++;
        pageNumber++;
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

    p->NumOwnedPages = p->NumPages;

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
        DPrintf(1, "Queue is not ready, try later");
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

// Disassemble a merged packet back to its original single-buffer state.
// Unlink the MDL chain and restore original page mapping and counters.
void CParaNdisRX::DisassembleMergedPacket(pRxNetDescriptor pBuffer)
{
    PMDL pMDL = pBuffer->Holder;
    PMDL pNextMDL = NULL;

    while (pMDL)
    {
        pNextMDL = NDIS_MDL_LINKAGE(pMDL);
        NDIS_MDL_LINKAGE(pMDL) = NULL;
        pMDL = pNextMDL;
    }

    pBuffer->PhysicalPages = pBuffer->OriginalPhysicalPages;
    pBuffer->NumPages = 1;
    pBuffer->NumOwnedPages = 1;
    pBuffer->MergedBufferCount = 0;
}

void CParaNdisRX::ReuseReceiveBufferNoLock(pRxNetDescriptor pBuffersDescriptor)
{
    DEBUG_ENTRY(4);

    // Handle merged packets: recursively reuse all constituent buffers
    if (pBuffersDescriptor->MergedBufferCount > 0)
    {
        DPrintf(4, "Reusing merged packet with %u additional buffers", pBuffersDescriptor->MergedBufferCount);

        // Reuse additional buffers stored in inline array
        // Note: Additional buffers retain their original MDLs (we used pre-allocated
        //       FullPageMDLs in AssembleMergedPacket, so their original MDLs are intact)
        for (USHORT i = 0; i < pBuffersDescriptor->MergedBufferCount; i++)
        {
            ReuseReceiveBufferNoLock(pBuffersDescriptor->MergedBuffers[i]);
        }

        // Disassemble the first buffer back to its original single-buffer state
        DisassembleMergedPacket(pBuffersDescriptor);
    }

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
                    "Error: m_NetNofReceiveBuffers > m_NetMaxReceiveBuffers (%d>%d)",
                    m_NetNofReceiveBuffers,
                    m_NetMaxReceiveBuffers);
        }

        /* TODO - nReusedRXBuffers per queue or per context ?*/
        m_nReusedRxBuffersCounter++;
        if (IsRxBuffersShortage() || m_nReusedRxBuffersCounter >= m_nReusedRxBuffersLimit)
        {
            m_nReusedRxBuffersCounter = 0;
            m_VirtQueue.Kick();
        }
    }
    else
    {
        /* TODO - NetMaxReceiveBuffers per queue or per context ?*/
        DPrintf(0, "FAILED TO REUSE THE BUFFER!!!!");
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

// Handles packet analysis, filtering, RSS processing, and queue assignment
void CParaNdisRX::ProcessReceivedPacket(pRxNetDescriptor pBufferDescriptor, CCHAR nCurrCpuReceiveQueue)
{
    // Get data pointer (skip virtio header)
    PVOID data = pBufferDescriptor->PhysicalPages[pBufferDescriptor->FirstRxDataPage].Virtual;
    data = RtlOffsetToPointer(data, pBufferDescriptor->DataStartOffset);

    // basic MAC-based analysis + L3 header info
    BOOLEAN packetAnalysisRC = ParaNdis_AnalyzeReceivedPacket(data,
                                                              pBufferDescriptor->PacketInfo.dataLength,
                                                              &pBufferDescriptor->PacketInfo);

    if (!packetAnalysisRC)
    {
        ReuseReceiveBufferNoLock(pBufferDescriptor);
        m_Context->Statistics.ifInErrors++;
        m_Context->Statistics.ifInDiscards++;
        return;
    }

    // filtering based on prev stage analysis
    if (!ShallPassPacket(m_Context, &pBufferDescriptor->PacketInfo))
    {
        pBufferDescriptor->Queue->ReuseReceiveBufferNoLock(pBufferDescriptor);
        m_Context->Statistics.ifInDiscards++;
        m_Context->extraStatistics.framesFilteredOut++;
        return;
    }

#ifdef PARANDIS_SUPPORT_RSS
    if (m_Context->RSSParameters.RSSMode != PARANDIS_RSS_MODE::PARANDIS_RSS_DISABLED)
    {
        ParaNdis6_RSSAnalyzeReceivedPacket(&m_Context->RSSParameters, data, &pBufferDescriptor->PacketInfo);
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
    UNREFERENCED_PARAMETER(nCurrCpuReceiveQueue);
    ParaNdis_ReceiveQueueAddBuffer(&m_UnclassifiedPacketsQueue, pBufferDescriptor);
#endif
}

VOID CParaNdisRX::ProcessRxRing(CCHAR nCurrCpuReceiveQueue)
{
    pRxNetDescriptor pBufferDescriptor;
    unsigned int nFullLength;

    TDPCSpinLocker autoLock(m_Lock);

    if (m_Context->extraStatistics.minFreeRxBuffers > m_NetNofReceiveBuffers)
    {
        m_Context->extraStatistics.minFreeRxBuffers = m_NetNofReceiveBuffers;
    }

    while (NULL != (pBufferDescriptor = (pRxNetDescriptor)m_VirtQueue.GetBuf(&nFullLength)))
    {
        RemoveEntryList(&pBufferDescriptor->listEntry);
        m_NetNofReceiveBuffers--;

        pRxNetDescriptor pProcessBuffer = pBufferDescriptor;

        // Mergeable buffers: delegate to ProcessMergedBuffers (handles both single/multi-buffer)
        if (m_Context->bUseMergedBuffers)
        {
            pProcessBuffer = ProcessMergedBuffers(pBufferDescriptor, nFullLength);
            if (!pProcessBuffer)
            {
                continue; // Assembly failed, buffer already reused
            }
        }
        else
        {
            // Non-mergeable: single buffer only, set data length
            pProcessBuffer->PacketInfo.dataLength = nFullLength - m_Context->nVirtioHeaderSize;
        }

        // Unified processing path for both single and merged packets
        // Note: For mergeable buffers, dataLength is already set in ProcessMergedBuffers
        ProcessReceivedPacket(pProcessBuffer, nCurrCpuReceiveQueue);
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
            DPrintf(0, "FAILED TO REUSE THE BUFFER!!!!");
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

//
// Merge Buffer Implementation Functions
//

// Handle mergeable buffer packets: fast path for single buffer, assembly for multi-buffer
// Returns: original descriptor (single buffer), assembled descriptor (multi-buffer), or NULL (error)
// Note: Caller must call ProcessReceivedPacket on returned descriptor
pRxNetDescriptor CParaNdisRX::ProcessMergedBuffers(pRxNetDescriptor pFirstBuffer, UINT nFullLength)
{
    // Access mergeable header to get num_buffers
    virtio_net_hdr_mrg_rxbuf *pMrgHeader = (virtio_net_hdr_mrg_rxbuf *)pFirstBuffer->PhysicalPages[0].Virtual;
    UINT16 numBuffers = pMrgHeader->num_buffers;

    DPrintf(5, "Received packet: length=%u, num_buffers=%u", nFullLength, numBuffers);

    // Fast path: single buffer packet (most common case)
    if (numBuffers == 1)
    {
        pFirstBuffer->PacketInfo.dataLength = nFullLength - m_Context->nVirtioHeaderSize;
        return pFirstBuffer;
    }

    // Multi-buffer packet: collect and assemble
    // Initialize merge context with first buffer
    m_MergeContext.BufferSequence[0] = pFirstBuffer;
    m_MergeContext.BufferActualLengths[0] = nFullLength;
    m_MergeContext.ExpectedBuffers = numBuffers;
    m_MergeContext.CollectedBuffers = 1;
    m_MergeContext.TotalPacketLength = nFullLength - m_Context->nVirtioHeaderSize;

    if (!m_Context->bMergeableBuffersConfigured)
    {
        // Unexpected multi-buffer packet in traditional mode (ACKed for compatibility but no 4KB pool).
        // Even if the packet is dropped, we MUST drain all remaining descriptors (fragments)
        // belonging to this packet from the VirtQueue. If we don't, the next GetBuf call would
        // retrieve a data-only fragment (which has no VirtIO header) and misinterpret it as
        // the start of a new packet, causing VirtIO protocol desynchronization
        DPrintf(0, "ERROR: Received merged packet (%u buffers) in traditional mode. Dropping.", numBuffers);

        CollectRemainingMergeBuffers();
        ReuseCollectedBuffers();

        return NULL;
    }

    DPrintf(5,
            "Multi-buffer packet: expecting %u total buffers, first buffer payload=%u",
            numBuffers,
            m_MergeContext.TotalPacketLength);

    // Collect remaining buffers
    if (!CollectRemainingMergeBuffers())
    {
        DPrintf(0, "ERROR: Failed to collect all merge buffers");
        ReuseCollectedBuffers();
        return NULL;
    }

    // Assemble into single logical packet
    return AssembleMergedPacket();
}

// Collect remaining buffers for a mergeable packet
// PREREQUISITE: The first buffer (BufferSequence[0]) has already been collected and stored
//               by the initial GetBuf call. This function collects buffers 2..N.
//
// Per VirtIO spec: For mergeable RX buffers, when num_buffers > 1, all buffers for a packet
// are atomically available in the virtqueue. This function retrieves the remaining (N-1) buffers.
//
// Returns: TRUE if all remaining buffers successfully collected, FALSE on error
BOOLEAN CParaNdisRX::CollectRemainingMergeBuffers()
{
    unsigned int nFullLength;
    pRxNetDescriptor pBufferDescriptor;

    DPrintf(5,
            "Collecting remaining %u buffers (have %u, need %u)",
            m_MergeContext.ExpectedBuffers - m_MergeContext.CollectedBuffers,
            m_MergeContext.CollectedBuffers,
            m_MergeContext.ExpectedBuffers);

    // Collect the remaining (N-1) buffers that form the rest of this packet
    // Note: BufferSequence[0] already contains the first buffer from initial GetBuf
    while (m_MergeContext.CollectedBuffers < m_MergeContext.ExpectedBuffers)
    {
        pBufferDescriptor = (pRxNetDescriptor)m_VirtQueue.GetBuf(&nFullLength);
        if (!pBufferDescriptor)
        {
            DPrintf(0,
                    "ERROR: Buffer %u/%u unavailable - VirtIO protocol violation",
                    m_MergeContext.CollectedBuffers + 1,
                    m_MergeContext.ExpectedBuffers);
            return FALSE;
        }

        RemoveEntryList(&pBufferDescriptor->listEntry);
        m_NetNofReceiveBuffers--;

        DPrintf(5,
                "Collected buffer %u/%u: length=%u bytes",
                m_MergeContext.CollectedBuffers + 1,
                m_MergeContext.ExpectedBuffers,
                nFullLength);

        m_MergeContext.BufferSequence[m_MergeContext.CollectedBuffers] = pBufferDescriptor;
        m_MergeContext.BufferActualLengths[m_MergeContext.CollectedBuffers] = nFullLength;
        m_MergeContext.CollectedBuffers++;

        // For subsequent buffers, all data is payload (no virtio header)
        m_MergeContext.TotalPacketLength += nFullLength;

        DPrintf(5,
                "Buffer %u added: dataLength=%u, totalLength=%u",
                m_MergeContext.CollectedBuffers,
                nFullLength,
                m_MergeContext.TotalPacketLength);
    }

    DPrintf(5,
            "All %u buffers collected successfully, total packet length: %u bytes",
            m_MergeContext.CollectedBuffers,
            m_MergeContext.TotalPacketLength);
    return TRUE;
}

// Assemble multiple buffers into a single logical packet using pre-allocated resources.
// Uses inline arrays for buffer references and chains pre-allocated MDLs - no hot path allocation.
// Returns the first buffer as the assembled packet with additional buffers linked.
pRxNetDescriptor CParaNdisRX::AssembleMergedPacket()
{
    pRxNetDescriptor pAssembledBuffer = m_MergeContext.BufferSequence[0];
    pAssembledBuffer->PacketInfo.dataLength = m_MergeContext.TotalPacketLength;

    UINT additionalBuffers = m_MergeContext.CollectedBuffers - 1;
    for (UINT i = 0; i < additionalBuffers; i++)
    {
        pAssembledBuffer->MergedBuffers[i] = m_MergeContext.BufferSequence[i + 1];
    }
    pAssembledBuffer->MergedBufferCount = (USHORT)additionalBuffers;

    // Calculate total pages needed for merged packet:
    // - First buffer: 1 page (header + data in same page)
    // - Each additional buffer: 1 page (subsequent buffers contain only data, no virtio header)
    USHORT totalPages = (USHORT)m_MergeContext.CollectedBuffers;
    pAssembledBuffer->PhysicalPages = m_MergeContext.PhysicalPages;
    pAssembledBuffer->PhysicalPages[0] = pAssembledBuffer->OriginalPhysicalPages[0];

    USHORT destPageIdx = 1;
    for (UINT i = 1; i < m_MergeContext.CollectedBuffers; i++)
    {
        pRxNetDescriptor pBuffer = m_MergeContext.BufferSequence[i];
        pAssembledBuffer->PhysicalPages[destPageIdx].Virtual = pBuffer->PhysicalPages[pBuffer->FirstRxDataPage].Virtual;
        pAssembledBuffer->PhysicalPages[destPageIdx].Physical = pBuffer->PhysicalPages[pBuffer->FirstRxDataPage].Physical;
        pAssembledBuffer->PhysicalPages[destPageIdx].size = m_MergeContext.BufferActualLengths[i];
        destPageIdx++;
    }
    pAssembledBuffer->NumPages = totalPages;

    // Chain pre-allocated MDLs (no alloc/free in hot path)
    PMDL pPreviousMDL = pAssembledBuffer->Holder;

    for (UINT i = 1; i < m_MergeContext.CollectedBuffers; i++)
    {
        pRxNetDescriptor pBuffer = m_MergeContext.BufferSequence[i];

        NDIS_MDL_LINKAGE(pPreviousMDL) = pBuffer->FullPageMDL;
        NDIS_MDL_LINKAGE(pBuffer->FullPageMDL) = NULL;
        pPreviousMDL = pBuffer->FullPageMDL;
    }

    DPrintf(5,
            "Assembled packet: %u buffers, %u total pages, %u bytes (using pre-allocated MDLs)",
            m_MergeContext.CollectedBuffers,
            totalPages,
            m_MergeContext.TotalPacketLength);

    return pAssembledBuffer;
}

void CParaNdisRX::ReuseCollectedBuffers()
{
    for (UINT i = 0; i < m_MergeContext.CollectedBuffers; i++)
    {
        ReuseReceiveBufferNoLock(m_MergeContext.BufferSequence[i]);
    }
}
