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

        NdisFreeMdl(pThisMDL);
        ulPageDescIndex++;
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
    for (i = PARANDIS_FIRST_RX_DATA_PAGE; i < p->NumPages; i++)
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
pRxNetDescriptor CParaNdisRX::CreateMergeableRxDescriptor()
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

    p->PhysicalPages = (tCompletePhysicalAddress *)ParaNdis_AllocateMemory(m_Context,
                                                                           sizeof(tCompletePhysicalAddress) * 2);
    if (p->PhysicalPages == NULL)
    {
        DPrintf(0, "ERROR: Failed to allocate PhysicalPages array");
        goto error_exit;
    }

    NdisZeroMemory(p->PhysicalPages, sizeof(tCompletePhysicalAddress) * 2);

    p->OriginalPhysicalPages = p->PhysicalPages;

    if (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, PAGE_SIZE, &p->PhysicalPages[0]))
    {
        DPrintf(0, "ERROR: Failed to allocate physical memory (1 page = %u bytes)", PAGE_SIZE);
        goto error_exit;
    }

    // Setup for mergeable buffer with ANY_LAYOUT
    // Physical allocation: Only 1 page containing virtio header + payload
    // Logical layout: NumPages=2 for compatibility
    //   - PhysicalPages[0] and PhysicalPages[1] both point to the SAME physical page
    //   - This aliasing allows legacy code to access data via index 1 without modification
    p->NumPages = 2;
    p->NumOwnedPages = p->NumPages;
    p->HeaderPage = 0;
    p->DataStartOffset = (USHORT)m_Context->nVirtioHeaderSize;
    p->PhysicalPages[1] = p->PhysicalPages[0];

    // Combined header and data in single SG entry (ANY_LAYOUT)
    p->BufferSGLength = 1;
    p->BufferSGArray[0].physAddr = p->PhysicalPages[0].Physical;
    p->BufferSGArray[0].length = PAGE_SIZE;

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
    // For mergeable buffers with ANY_LAYOUT, use simplified allocation - just 1 page per buffer
    if (m_Context->bUseMergedBuffers && m_Context->bAnyLayout)
    {
        DPrintf(5, "Using mergeable buffer allocation (small buffers, combined layout)");
        return CreateMergeableRxDescriptor();
    }

    // Legacy path: either non-mergeable or without ANY_LAYOUT
    if (m_Context->bUseMergedBuffers && !m_Context->bAnyLayout)
    {
        DPrintf(0, "WARNING: Mergeable buffers require ANY_LAYOUT support, falling back to traditional mode");
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
    p->DataStartOffset = (p->HeaderPage == 0) ? 0 : (USHORT)m_Context->nVirtioHeaderSize;
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

    p->NumOwnedPages = p->NumPages;

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

// Disassemble a merged packet back to its original single-buffer state
// This is the inverse operation of AssembleMergedPacket, reversing all state changes:
//   - Frees extended MDL chain (created during assembly)
//   - Restores PhysicalPages pointer from inline array to original array
//   - Restores NumPages and NumOwnedPages to original values (2 for mergeable)
//   - Clears MergedBufferCount
void CParaNdisRX::DisassembleMergedPacket(pRxNetDescriptor pBuffer)
{
    PMDL pMDL = pBuffer->Holder;
    USHORT mdlCount = 0;

    while (pMDL && mdlCount < 1)
    {
        pMDL = NDIS_MDL_LINKAGE(pMDL);
        mdlCount++;
    }

    while (pMDL)
    {
        PMDL pNextMDL = NDIS_MDL_LINKAGE(pMDL);
        NdisFreeMdl(pMDL);
        pMDL = pNextMDL;
    }

    pMDL = pBuffer->Holder;
    if (pMDL)
    {
        NDIS_MDL_LINKAGE(pMDL) = NULL;
    }

    if (pBuffer->PhysicalPages != pBuffer->OriginalPhysicalPages)
    {
        pBuffer->PhysicalPages = pBuffer->OriginalPhysicalPages;
    }

    pBuffer->NumPages = 2;
    pBuffer->NumOwnedPages = 2;
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
        // Note: Additional buffers retain their original MDLs (we created NEW MDLs for the
        //       assembled packet in AssembleMergedPacket, so their original MDLs are intact)
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
        if (++m_nReusedRxBuffersCounter >= m_nReusedRxBuffersLimit)
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
    PVOID data = pBufferDescriptor->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE].Virtual;
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

        pRxNetDescriptor pProcessBuffer = pBufferDescriptor;

        // Mergeable buffer handling: always delegate to ProcessMergedBuffers if feature enabled
        // It will handle both single-buffer (numBuffers=1) and multi-buffer (numBuffers>1) cases
        if (m_Context->bUseMergedBuffers)
        {
            pProcessBuffer = ProcessMergedBuffers(pBufferDescriptor, nFullLength);
            if (!pProcessBuffer)
            {
                // Assembly failed, already handled internally
                continue;
            }
        }
        else
        {
            // Non-mergeable mode: set packet data length (subtract virtio header)
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
    virtio_net_hdr_mrg_rxbuf *pHeader = (virtio_net_hdr_mrg_rxbuf *)pFirstBuffer->PhysicalPages[pFirstBuffer->HeaderPage].Virtual;
    UINT16 numBuffers = pHeader->num_buffers;

    DPrintf(5, "ProcessMergedBuffers: numBuffers=%u, packetLength=%u", numBuffers, nFullLength);

    // Validate buffer count
    if (numBuffers == 0 || numBuffers > VIRTIO_NET_MAX_MRG_BUFS)
    {
        DPrintf(0, "ERROR: Invalid buffer count: %d (valid range: 1-%d)", numBuffers, VIRTIO_NET_MAX_MRG_BUFS);
        ReuseReceiveBufferNoLock(pFirstBuffer);
        return NULL;
    }

    // Single buffer case - no assembly needed, just return the original
    if (numBuffers == 1)
    {
        DPrintf(5, "Single buffer packet (no merge required)");
        // Set packet data length for single buffer (subtract virtio header)
        pFirstBuffer->PacketInfo.dataLength = nFullLength - m_Context->nVirtioHeaderSize;
        return pFirstBuffer;
    }

    // Multi-buffer case - assemble the packet
    DPrintf(5, "Multi-buffer packet detected: %u buffers required", numBuffers);

    // Initialize merge context (only fields that need initialization)
    m_MergeContext.ExpectedBuffers = numBuffers;
    m_MergeContext.CollectedBuffers = 0;
    m_MergeContext.TotalPacketLength = 0;

    // Add first buffer to merge context
    m_MergeContext.BufferSequence[m_MergeContext.CollectedBuffers] = pFirstBuffer;
    m_MergeContext.BufferActualLengths[m_MergeContext.CollectedBuffers] = nFullLength;
    m_MergeContext.CollectedBuffers++;
    // Calculate actual data length (subtract virtio header from first buffer)
    UINT32 firstBufferDataLength = nFullLength - m_Context->nVirtioHeaderSize;
    pFirstBuffer->PacketInfo.dataLength = firstBufferDataLength;
    m_MergeContext.TotalPacketLength += firstBufferDataLength;

    DPrintf(5,
            "Added first buffer (%u/%u), current total length: %u bytes",
            m_MergeContext.CollectedBuffers,
            m_MergeContext.ExpectedBuffers,
            m_MergeContext.TotalPacketLength);

    // Collect remaining buffers
    if (!CollectRemainingMergeBuffers())
    {
        // Failed to collect all buffers - protocol violation
        DPrintf(0,
                "ERROR: Incomplete buffer collection (have %u/%u) - packet corrupted",
                m_MergeContext.CollectedBuffers,
                m_MergeContext.ExpectedBuffers);
        ReuseCollectedBuffers();
        return NULL;
    }

    // All buffers collected, assemble the packet
    DPrintf(5,
            "All %u buffers collected, assembling packet (total: %u bytes)",
            m_MergeContext.CollectedBuffers,
            m_MergeContext.TotalPacketLength);

    pRxNetDescriptor pAssembledBuffer = AssembleMergedPacket();
    if (!pAssembledBuffer)
    {
        DPrintf(0, "ERROR: Failed to assemble merged packet");
        ReuseCollectedBuffers();
        return NULL;
    }

    DPrintf(5,
            "Successfully assembled packet: %u buffers -> %u bytes",
            m_MergeContext.CollectedBuffers,
            m_MergeContext.TotalPacketLength);

    return pAssembledBuffer;
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
        // Get next buffer - it MUST be available per VirtIO spec
        pBufferDescriptor = (pRxNetDescriptor)m_VirtQueue.GetBuf(&nFullLength);
        if (!pBufferDescriptor)
        {
            // Buffer unavailable = protocol violation or device error
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

        // Store buffer in sequence (bounds already guaranteed by ExpectedBuffers validation)
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

pRxNetDescriptor CParaNdisRX::AssembleMergedPacket()
{
    // Use the first buffer as the base for the assembled packet
    pRxNetDescriptor pAssembledBuffer = m_MergeContext.BufferSequence[0];

    // Update packet info with total length
    pAssembledBuffer->PacketInfo.dataLength = m_MergeContext.TotalPacketLength;

    // Multi-buffer case: save references to merged buffers using inline storage
    // Note: Single-buffer case (numBuffers==1) is handled in ProcessMergedBuffers, never reaches here
    UINT additionalBuffers = m_MergeContext.CollectedBuffers - 1;

    // CRITICAL: Prevent buffer overflow - inline array has limited capacity
    if (additionalBuffers > MAX_MERGED_BUFFERS)
    {
        DPrintf(0,
                "ERROR: Too many merged buffers %u (max: %u) - dropping packet",
                m_MergeContext.CollectedBuffers,
                MAX_MERGED_BUFFERS + 1);

        ReuseCollectedBuffers();
        return NULL;
    }

    // Copy buffer pointers to inline array (no allocation needed!)
    for (UINT i = 0; i < additionalBuffers; i++)
    {
        pAssembledBuffer->MergedBuffers[i] = m_MergeContext.BufferSequence[i + 1];
    }
    // MergedBufferCount = number of ADDITIONAL buffers (not including this one)
    pAssembledBuffer->MergedBufferCount = (USHORT)additionalBuffers;

    // Calculate total pages needed for merged packet:
    // - First buffer: 2 logical pages (PhysicalPages[0] and [1] are aliases of the same physical page)
    // - Each additional buffer: 1 page (subsequent buffers contain only data, no virtio header)
    // Formula: totalPages = 2 (first buffer) + (CollectedBuffers - 1) (additional buffers)
    //        = 1 + CollectedBuffers
    USHORT totalPages = 1 + m_MergeContext.CollectedBuffers;

    pAssembledBuffer->PhysicalPages = m_MergeContext.PhysicalPages;

    pAssembledBuffer->PhysicalPages[0] = pAssembledBuffer->OriginalPhysicalPages[0];
    pAssembledBuffer->PhysicalPages[1] = pAssembledBuffer->OriginalPhysicalPages[1];

    // Copy additional buffer pages into the inline PhysicalPages array
    // Start filling from index 2 (after first buffer's 2 pages)
    USHORT destPageIdx = 2;
    for (UINT i = 1; i < m_MergeContext.CollectedBuffers; i++)
    {
        pRxNetDescriptor pBuffer = m_MergeContext.BufferSequence[i];

        pAssembledBuffer->PhysicalPages[destPageIdx].Virtual = pBuffer->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE].Virtual;
        pAssembledBuffer->PhysicalPages[destPageIdx].Physical = pBuffer->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE].Physical;
        pAssembledBuffer->PhysicalPages[destPageIdx].size = m_MergeContext.BufferActualLengths[i];
        destPageIdx++;
    }

    // Update page count
    pAssembledBuffer->NumPages = totalPages;

    // Now create NEW MDLs for additional buffers covering their FULL payload
    // IMPORTANT: For subsequent buffers in mergeable mode, the ENTIRE buffer contains payload data
    // (no virtio header), so we must create MDLs covering the full buffer from offset 0,
    PMDL pPreviousMDL = NULL;
    PMDL pCurrentMDL = pAssembledBuffer->Holder;

    // Find the end of the first buffer's MDL chain
    while (pCurrentMDL)
    {
        pPreviousMDL = pCurrentMDL;
        pCurrentMDL = NDIS_MDL_LINKAGE(pCurrentMDL);
    }

    for (UINT i = 1; i < m_MergeContext.CollectedBuffers; i++)
    {
        // For subsequent buffers: create MDL for FULL buffer (no header to skip)
        // (PhysicalPages[0] and [1] are aliased in mergeable mode, so functionally equivalent)
        // Use actual received length instead of allocated buffer size
        PMDL pNewMDL = NdisAllocateMdl(m_Context->MiniportHandle,
                                       m_MergeContext.BufferSequence[i]->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE].Virtual,
                                       m_MergeContext.BufferActualLengths[i]);
        if (!pNewMDL)
        {
            DPrintf(0, "ERROR: Failed to allocate MDL for buffer %u, aborting packet assembly", i);
            // DisassembleMergedPacket will clean up partial state
            return NULL;
        }

        NDIS_MDL_LINKAGE(pPreviousMDL) = pNewMDL;

        NDIS_MDL_LINKAGE(pNewMDL) = NULL;
        pPreviousMDL = pNewMDL;
    }

    DPrintf(5,
            "Assembled packet: %u buffers, %u total pages, %u bytes",
            m_MergeContext.CollectedBuffers,
            totalPages,
            m_MergeContext.TotalPacketLength);

    return pAssembledBuffer;
}

void CParaNdisRX::ReuseCollectedBuffers()
{
    // Return any collected buffers to the free pool (error/cleanup path only)
    // Note: All BufferSequence[0..CollectedBuffers-1] are guaranteed to be valid pointers
    for (UINT i = 0; i < m_MergeContext.CollectedBuffers; i++)
    {
        ReuseReceiveBufferNoLock(m_MergeContext.BufferSequence[i]);
    }
}
