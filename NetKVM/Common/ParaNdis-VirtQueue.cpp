#include "ParaNdis-VirtQueue.h"
#include "ndis56common.h"

bool CVirtQueue::AllocateQueueMemory()
{
    ULONG NumEntries, AllocationSize;
    VirtIODeviceQueryQueueAllocation(&m_IODevice, m_Index, &NumEntries, &AllocationSize);
    return (AllocationSize != 0) ? m_SharedMemory.Allocate(AllocationSize) : false;
}

void CVirtQueue::Renew()
{
    auto virtioDev = m_VirtQueue->vdev;
    auto info = &virtioDev->info[m_VirtQueue->ulIndex];
    auto pageNum = static_cast<ULONG>(info->phys.QuadPart >> PAGE_SHIFT);
    DPrintf(0, ("[%s] devaddr %p, queue %d, pfn %x\n", __FUNCTION__, virtioDev->addr, info->queue_index, pageNum));
    WriteVirtIODeviceWord(virtioDev->addr + VIRTIO_PCI_QUEUE_SEL, static_cast<UINT16>(info->queue_index));
    WriteVirtIODeviceRegister(virtioDev->addr + VIRTIO_PCI_QUEUE_PFN, pageNum);
}

bool CVirtQueue::Create()
{
    ASSERT(m_VirtQueue == nullptr);

    if(AllocateQueueMemory())
    {
        m_VirtQueue = VirtIODevicePrepareQueue(&m_IODevice,
                                               m_Index,
                                               m_SharedMemory.GetPA(),
                                               m_SharedMemory.GetVA(),
                                               m_SharedMemory.GetSize(),
                                               nullptr,
                                               static_cast<BOOLEAN>(m_UsePublishedIndices));
    }

    return m_VirtQueue != nullptr;
}

void CVirtQueue::Delete()
{
    if (m_VirtQueue != nullptr)
    {
        VirtIODeviceDeleteQueue(m_VirtQueue, nullptr);
    }
}

class CTXDescriptor : public CNdisAllocatable<CTXDescriptor, 'DTHR'>
{
public:
    CTXDescriptor(NDIS_HANDLE DrvHandle, ULONG HeaderSize, ULONG DataSize)
        : m_Header(DrvHandle)
        , m_Data(DrvHandle)
        , m_HeaderSize(HeaderSize)
        , m_DataSize(DataSize)
    {}

    bool Create()
    { return (m_Header.Allocate(m_HeaderSize) && m_Data.Allocate(m_DataSize)); }

    NDIS_PHYSICAL_ADDRESS HeaderPA()
    { return m_Header.GetPA(); }
    PVOID HeaderVA()
    { return m_Header.GetVA(); }
    ULONG HeaderSize()
    { return m_Header.GetSize(); }
    NDIS_PHYSICAL_ADDRESS DataPA()
    { return m_Data.GetPA(); }
    PVOID DataVA()
    { return m_Data.GetVA(); }
    ULONG DataSize()
    { return m_Data.GetSize(); }

    //TODO: Temporary, must go
    void SetUsedBuffersNum(ULONG Num)
    { m_UsedBuffersNum = Num; }
    ULONG GetUsedBuffersNum()
    { return m_UsedBuffersNum; }
    void SetNB(CNB *NB)
    { m_NB = NB; }
    CNB* GetNB()
    { return m_NB; }

private:
    CNdisSharedMemory m_Header;
    CNdisSharedMemory m_Data;
    ULONG m_HeaderSize;
    ULONG m_DataSize;

    ULONG m_UsedBuffersNum = 0;
    CNB *m_NB = nullptr;

    CTXDescriptor(const CTXDescriptor&) = delete;
    CTXDescriptor& operator= (const CTXDescriptor&) = delete;

    DECLARE_CNDISLIST_ENTRY(CTXDescriptor);
};

bool CTXVirtQueue::PrepareBuffers()
{
    auto NumBuffers = GetSize() / 2;
    NumBuffers = min(m_MaxBuffers, NumBuffers);

    for (m_TotalDescriptors = 0; m_TotalDescriptors < NumBuffers; m_TotalDescriptors++)
    {
        auto TXDescr = new (m_Context->MiniportHandle) CTXDescriptor(m_DrvHandle, m_HeaderSize, m_DataSize);
        if (TXDescr == nullptr)
        {
            break;
        }

        if (!TXDescr->Create())
        {
            CTXDescriptor::Destroy(TXDescr, m_Context->MiniportHandle);
            break;
        }

        m_Descriptors.Push(TXDescr);
    }

    m_FreeHWBuffers = m_TotalHWBuffers = m_TotalDescriptors * 2;
    DPrintf(0, ("[%s] available %d Tx descriptors\n", __FUNCTION__, m_TotalDescriptors));

    return m_TotalDescriptors > 0;
}

void CTXVirtQueue::FreeBuffers()
{
    m_Descriptors.ForEachDetached([this](CTXDescriptor* TXDescr)
                                      { CTXDescriptor::Destroy(TXDescr, m_Context->MiniportHandle); });

    m_TotalDescriptors = m_FreeHWBuffers = 0;
}

bool CTXVirtQueue::Create()
{
    if (!CVirtQueue::Create() || !PrepareBuffers())
    {
        return false;
    }

    auto SGBuffer = ParaNdis_AllocateMemoryRaw(m_DrvHandle, m_TotalDescriptors * 2 * sizeof(m_SGTable[0]));
    m_SGTable = static_cast<struct VirtIOBufferDescriptor *>(SGBuffer);

    return m_SGTable != nullptr;
}

CTXVirtQueue::~CTXVirtQueue()
{
    if(m_SGTable != nullptr)
    {
        NdisFreeMemory(m_SGTable, 0, 0);
    }

    FreeBuffers();
}

//TODO: Temporary, needs review
static eInspectedPacketType QueryPacketType(PVOID data)
{
    if (ETH_IS_BROADCAST(data))
        return iptBroadcast;
    if (ETH_IS_MULTICAST(data))
        return iptMilticast;
    return iptUnicast;
}
static ULONG FORCEINLINE QueryTcpHeaderOffset(PVOID packetData, ULONG ipHeaderOffset, ULONG ipPacketLength)
{
    ULONG res;
    tTcpIpPacketParsingResult ppr = ParaNdis_ReviewIPPacket(
        (PUCHAR)packetData + ipHeaderOffset,
        ipPacketLength,
        __FUNCTION__);
    if (ppr.xxpStatus == ppresXxpKnown)
    {
        res = ipHeaderOffset + ppr.ipHeaderSize;
    }
    else
    {
        DPrintf(0, ("[%s] ERROR: NOT a TCP or UDP packet - expected troubles!\n", __FUNCTION__));
        res = 0;
    }
    return res;
}
static FORCEINLINE ULONG CalculateTotalOffloadSize(
    ULONG packetSize,
    ULONG mss,
    ULONG ipheaderOffset,
    ULONG maxPacketSize,
    tTcpIpPacketParsingResult packetReview)
{
    ULONG ul = 0;
    ULONG tcpipHeaders = packetReview.XxpIpHeaderSize;
    ULONG allHeaders = tcpipHeaders + ipheaderOffset;
#if 1
    if (tcpipHeaders && (mss + allHeaders) <= maxPacketSize)
    {
        ul = packetSize - allHeaders;
    }
    DPrintf(1, ("[%s]%s %d/%d, headers %d)\n", __FUNCTION__, !ul ? "ERROR:" : "", ul, mss, allHeaders));
#else
    UINT  calculationType = 3;
    if (tcpipHeaders && (mss + allHeaders) <= maxPacketSize)
    {
        ULONG nFragments = (packetSize - allHeaders)/mss;
        ULONG last = (packetSize - allHeaders)%mss;
        ULONG tcpHeader = tcpipHeaders - packetReview.ipHeaderSize;
        switch (calculationType)
        {
            case 0:
                ul = nFragments * (mss + allHeaders) + last + (last ? allHeaders : 0);
                break;
            case 1:
                ul = nFragments * (mss + tcpipHeaders) + last + (last ? tcpipHeaders : 0);
                break;
            case 2:
                ul = nFragments * (mss + tcpHeader) + last + (last ? tcpHeader : 0);
                break;
            case 3:
                ul = packetSize - allHeaders;
                break;
            case 4:
                ul = packetSize - ETH_HEADER_SIZE;
                break;
            case 5:
                ul = packetSize - ipheaderOffset;
                break;
            default:
                break;
        }
    }
    DPrintf(1, ("[%s:%d]%s %d/%d, headers %d)\n",
        __FUNCTION__, calculationType, !ul ? "ERROR:" : "", ul, mss, allHeaders));
#endif
    return ul;
}

static void __inline PopulateIPPacketLength(PVOID pIpHeader, PNET_BUFFER packet, ULONG ipHeaderOffset)
{
    IPv4Header *pHeader = (IPv4Header *)pIpHeader;
    if ((pHeader->ip_verlen & 0xF0) == 0x40)
    {
        if (!pHeader->ip_length) {
            pHeader->ip_length = swap_short((USHORT)(NET_BUFFER_DATA_LENGTH(packet) - ipHeaderOffset));
        }
    }
}

//TODO: Temporary, needs review
tCopyPacketResult CTXVirtQueue::DoCopyPacketData(tTxOperationParameters *pParams)
{
    tCopyPacketResult result;
    tCopyPacketResult CopierResult;
    struct VirtIOBufferDescriptor sg[2];
    ULONG flags = pParams->flags;
    UINT nRequiredHardwareBuffers = 2;
    result.size  = 0;
    result.error = cpeOK;
    if (m_Descriptors.GetCount() < nRequiredHardwareBuffers)
    {
        result.error = cpeNoBuffer;
    }
    if(result.error == cpeOK)
    {
        auto TXDescriptor = m_Descriptors.Pop();
        NdisZeroMemory(TXDescriptor->HeaderVA(), TXDescriptor->HeaderSize());
        sg[0].physAddr = TXDescriptor->HeaderPA();
        sg[0].ulSize = TXDescriptor->HeaderSize();
        sg[1].physAddr = TXDescriptor->DataPA();

        CopierResult = pParams->NB->PacketCopier(TXDescriptor->DataVA(),
                                                 TXDescriptor->DataSize(),
                                                 FALSE);
        sg[1].ulSize = result.size = CopierResult.size;
        // did NDIS ask us to compute CS?
        if ((flags & (pcrTcpChecksum | pcrUdpChecksum | pcrIpChecksum)) != 0)
        {
            // we asked
            unsigned short addPriorityLen = (pParams->flags & pcrPriorityTag) ? ETH_PRIORITY_HEADER_SIZE : 0;
            PVOID ipPacket = RtlOffsetToPointer(
                TXDescriptor->DataVA(), m_Context->Offload.ipHeaderOffset + addPriorityLen);
            ULONG ipPacketLength = CopierResult.size - m_Context->Offload.ipHeaderOffset - addPriorityLen;
            if (!pParams->tcpHeaderOffset &&
                (flags & (pcrTcpChecksum | pcrUdpChecksum)) )
            {
                pParams->tcpHeaderOffset = QueryTcpHeaderOffset(
                    TXDescriptor->DataVA(),
                    m_Context->Offload.ipHeaderOffset + addPriorityLen,
                    ipPacketLength);
            }
            else
            {
                pParams->tcpHeaderOffset += addPriorityLen;
            }

            if (flags & (pcrTcpChecksum | pcrUdpChecksum))
            {
                // hardware offload
                virtio_net_hdr_basic *pvnh = (virtio_net_hdr_basic *) TXDescriptor->HeaderVA();
                pvnh->csum_start = (USHORT)pParams->tcpHeaderOffset;
                pvnh->csum_offset = (flags & pcrTcpChecksum) ? TCP_CHECKSUM_OFFSET : UDP_CHECKSUM_OFFSET;
                pvnh->flags |= VIRTIO_NET_HDR_F_NEEDS_CSUM;
            }
            if (flags & (pcrIpChecksum))
            {
                ParaNdis_CheckSumVerifyFlat(
                    ipPacket,
                    ipPacketLength,
                    pcrIpChecksum | pcrFixIPChecksum,
                    __FUNCTION__);
            }
        }
        if (result.size)
        {
            eInspectedPacketType packetType;
            packetType = QueryPacketType(TXDescriptor->DataVA());

            TXDescriptor->SetUsedBuffersNum(nRequiredHardwareBuffers);
            m_FreeHWBuffers -= nRequiredHardwareBuffers;
            if (0 > m_VirtQueue->vq_ops->add_buf(
                m_VirtQueue,
                sg,
                2,
                0,
                TXDescriptor,
                NULL,
                0
                ))
            {
                TXDescriptor->SetUsedBuffersNum(0);
                m_FreeHWBuffers += nRequiredHardwareBuffers;
                result.error = cpeInternalError;
                result.size  = 0;
                DPrintf(0, ("[%s] Unexpected ERROR adding buffer to TX engine!..\n", __FUNCTION__));
            }
            if (result.error != cpeOK)
            {
                m_Descriptors.Push(TXDescriptor);
            }
            else
            {
                ULONG reportedSize = pParams->ulDataSize;
                TXDescriptor->SetNB(pParams->NB);
                m_DescriptorsInUse.PushBack(TXDescriptor);
                //TODO: Must be atomic increment
                m_Context->Statistics.ifHCOutOctets += reportedSize;
                switch (packetType)
                {
                    case iptBroadcast:
                        m_Context->Statistics.ifHCOutBroadcastOctets += reportedSize;
                        m_Context->Statistics.ifHCOutBroadcastPkts++;
                        break;
                    case iptMilticast:
                        m_Context->Statistics.ifHCOutMulticastOctets += reportedSize;
                        m_Context->Statistics.ifHCOutMulticastPkts++;
                        break;
                    default:
                        m_Context->Statistics.ifHCOutUcastOctets += reportedSize;
                        m_Context->Statistics.ifHCOutUcastPkts++;
                        break;
                }
            }
        }
        else
        {
            DPrintf(0, ("[%s] Unexpected ERROR in copying packet data! Continue...\n", __FUNCTION__));
            m_Descriptors.Push(TXDescriptor);
            // the buffer is not copied and the callback will not be called
            result.error = cpeInternalError;
        }
    }

    return result;
}

//TODO: Temporary Requires review
VOID CTXVirtQueue::PacketMapper(
    CNB *NB,
    struct VirtIOBufferDescriptor *buffers,
    CTXDescriptor &TXDescriptor,
    tMapperResult *pMapperResult
    )
{
    USHORT nBuffersMapped = 0;

    PSCATTER_GATHER_LIST pSGList = NB->GetSGL();
    if (pSGList)
    {
        UINT i, lengthGet = 0, lengthPut = 0;
        SCATTER_GATHER_ELEMENT *pSGElements = pSGList->Elements;
        auto NBL = NB->GetParentNBL();
        UINT nCompleteBuffersToSkip = 0;
        UINT nBytesSkipInFirstBuffer = NET_BUFFER_CURRENT_MDL_OFFSET(NB->GetInternalObject());
        ULONG PriorityDataLong = NBL->PriorityDataPacked();
        if (NBL->IsLSO() || NBL->IsTcpCSO() || NBL->IsUdpCSO() || NBL->IsIPHdrCSO())
        {
            // for IP CS only tcpHeaderOffset could be not set
            lengthGet = NBL->TCPHeaderOffset() ?
                (NBL->TCPHeaderOffset() + sizeof(TCPHeader)) :
                (ETH_HEADER_SIZE + MAX_IPV4_HEADER_SIZE + sizeof(TCPHeader));
        }
        if (PriorityDataLong && !lengthGet)
        {
            lengthGet = ETH_HEADER_SIZE;
        }
        if (lengthGet)
        {
            ULONG len = 0;
            for (i = 0; i < pSGList->NumberOfElements; ++i)
            {
                len += pSGList->Elements[i].Length - nBytesSkipInFirstBuffer;
                DPrintf(3, ("[%s] buffer %d of %d->%d\n",
                    __FUNCTION__, nCompleteBuffersToSkip, pSGElements[i].Length, len));
                if (len > lengthGet)
                {
                    nBytesSkipInFirstBuffer = pSGList->Elements[i].Length - (len - lengthGet);
                    break;
                }
                nCompleteBuffersToSkip++;
                nBytesSkipInFirstBuffer = 0;
            }

            // this can happen only with short UDP packet with checksum offload required
            if (lengthGet > len) lengthGet = len;

            lengthPut = lengthGet + (PriorityDataLong ? ETH_PRIORITY_HEADER_SIZE : 0);
        }

        if (lengthPut > TXDescriptor.DataSize())
        {
            DPrintf(0, ("[%s] ERROR: can not substitute %d bytes, sending as is\n", __FUNCTION__, lengthPut));
            nCompleteBuffersToSkip = 0;
            lengthPut = lengthGet = 0;
            nBytesSkipInFirstBuffer = NET_BUFFER_CURRENT_MDL_OFFSET(NB->GetInternalObject());
        }

        if (lengthPut)
        {
            // we replace 1 or more HW buffers with one buffer preallocated for data
            buffers->physAddr = TXDescriptor.DataPA();
            buffers->ulSize   = lengthPut;
            pMapperResult->usBufferSpaceUsed = (USHORT)lengthPut;
            pMapperResult->ulDataSize += lengthGet;
            nBuffersMapped = (USHORT)(pSGList->NumberOfElements - nCompleteBuffersToSkip + 1);
            pSGElements += nCompleteBuffersToSkip;
            buffers++;
            DPrintf(1, ("[%s] (%d bufs) skip %d buffers + %d bytes\n",
                __FUNCTION__, pSGList->NumberOfElements, nCompleteBuffersToSkip, nBytesSkipInFirstBuffer));
        }
        else
        {
            nBuffersMapped = (USHORT)pSGList->NumberOfElements;
        }

        for (i = nCompleteBuffersToSkip; i < pSGList->NumberOfElements; ++i)
        {
            if (nBytesSkipInFirstBuffer)
            {
                buffers->physAddr.QuadPart = pSGElements->Address.QuadPart + nBytesSkipInFirstBuffer;
                buffers->ulSize   = pSGElements->Length - nBytesSkipInFirstBuffer;
                DPrintf(2, ("[%s] using HW buffer %d of %d-%d\n", __FUNCTION__, i, pSGElements->Length, nBytesSkipInFirstBuffer));
                nBytesSkipInFirstBuffer = 0;
            }
            else
            {
                buffers->physAddr = pSGElements->Address;
                buffers->ulSize   = pSGElements->Length;
            }
            pMapperResult->ulDataSize += buffers->ulSize;
            pSGElements++;
            buffers++;
        }

        if (lengthPut)
        {
            PVOID pBuffer = TXDescriptor.DataVA();
            NB->PacketCopier(pBuffer, lengthGet, TRUE);
            if (NBL->IsLSO())
            {
                tTcpIpPacketParsingResult packetReview;
                ULONG dummyTransferSize = 0;
                ULONG flags = pcrIpChecksum | pcrFixIPChecksum | pcrTcpChecksum | pcrFixPHChecksum;
                USHORT saveBuffers = nBuffersMapped;
                PVOID pIpHeader = RtlOffsetToPointer(pBuffer, m_Context->Offload.ipHeaderOffset);
                nBuffersMapped = 0;
                PopulateIPPacketLength(pIpHeader, NB->GetInternalObject(), m_Context->Offload.ipHeaderOffset);
                packetReview = ParaNdis_CheckSumVerifyFlat(
                    pIpHeader,
                    lengthGet - m_Context->Offload.ipHeaderOffset,
                    flags,
                    __FUNCTION__);
                if (packetReview.xxpCheckSum == ppresPCSOK || packetReview.fixedXxpCS)
                {
                    dummyTransferSize = CalculateTotalOffloadSize(
                        pMapperResult->ulDataSize,
                        NBL->MSS(),
                        m_Context->Offload.ipHeaderOffset,
                        m_Context->MaxPacketSize.nMaxFullSizeOS,
                        packetReview);
                    if (packetReview.xxpStatus == ppresXxpIncomplete)
                    {
                        DPrintf(0, ("[%s] CHECK: IPHO %d, TCPHO %d, IPHS %d, XXPHS %d\n", __FUNCTION__,
                            m_Context->Offload.ipHeaderOffset,
                            NBL->TCPHeaderOffset(),
                            packetReview.ipHeaderSize,
                            packetReview.XxpIpHeaderSize
                            ));

                    }
                }
                else
                {
                    DPrintf(0, ("[%s] ERROR locating IP header in %d bytes(IP header of %d)\n", __FUNCTION__,
                        lengthGet, packetReview.ipHeaderSize));
                }

                NBL->IncrementLSOv1TransferSize(dummyTransferSize);

                if (dummyTransferSize)
                {
                    auto pheader = static_cast<virtio_net_hdr_basic*>(TXDescriptor.HeaderVA());
                    unsigned short addPriorityLen = PriorityDataLong ? ETH_PRIORITY_HEADER_SIZE : 0;
                    pheader->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
                    pheader->gso_type = packetReview.ipStatus == ppresIPV4 ?
                        VIRTIO_NET_HDR_GSO_TCPV4 : VIRTIO_NET_HDR_GSO_TCPV6;
                    pheader->hdr_len  = (USHORT)(packetReview.XxpIpHeaderSize + m_Context->Offload.ipHeaderOffset) + addPriorityLen;
                    pheader->gso_size = (USHORT)NBL->MSS();
                    pheader->csum_start = (USHORT)NBL->TCPHeaderOffset() + addPriorityLen;
                    pheader->csum_offset = TCP_CHECKSUM_OFFSET;

                    nBuffersMapped = saveBuffers;
                }
            }
            else if (NBL->IsIPHdrCSO())
            {
                PVOID pIpHeader = RtlOffsetToPointer(pBuffer, m_Context->Offload.ipHeaderOffset);
                ParaNdis_CheckSumVerifyFlat(
                    pIpHeader,
                    lengthGet - m_Context->Offload.ipHeaderOffset,
                    pcrIpChecksum | pcrFixIPChecksum,
                    __FUNCTION__);
            }

            if (PriorityDataLong && nBuffersMapped)
            {
                RtlMoveMemory(
                    RtlOffsetToPointer(pBuffer, ETH_PRIORITY_HEADER_OFFSET + ETH_PRIORITY_HEADER_SIZE),
                    RtlOffsetToPointer(pBuffer, ETH_PRIORITY_HEADER_OFFSET),
                    lengthGet - ETH_PRIORITY_HEADER_OFFSET
                    );
                NdisMoveMemory(
                    RtlOffsetToPointer(pBuffer, ETH_PRIORITY_HEADER_OFFSET),
                    &PriorityDataLong,
                    sizeof(ETH_PRIORITY_HEADER_SIZE));
                DPrintf(1, ("[%s] Populated priority value %lX\n", __FUNCTION__, PriorityDataLong));
            }
        }
    }
    else
    {
        DPrintf(0, ("[%s] ERROR: packet (nbe %p) is not mapped!\n", __FUNCTION__, NB));
    }
    pMapperResult->usBuffersMapped = nBuffersMapped;
}

//TODO: Temporary: needs review
tCopyPacketResult CTXVirtQueue::DoSubmitPacket(tTxOperationParameters *Params)
{
    tCopyPacketResult result;
    tMapperResult mapResult = {0,0,0};
    // populating priority tag or LSO MAY require additional SG element
    UINT nRequiredBuffers;
    BOOLEAN bUseCopy = FALSE;

    nRequiredBuffers = Params->nofSGFragments + 1 + ((Params->flags & (pcrPriorityTag | pcrLSO)) ? 1 : 0);

    result.size = 0;
    result.error = cpeOK;
    if (Params->nofSGFragments == 0 ||          // theoretical case
        ((~Params->flags & pcrLSO) && nRequiredBuffers > m_TotalHWBuffers) // to many fragments and normal size of packet
        )
    {
        nRequiredBuffers = 2;
        bUseCopy = TRUE;
    }
    else if (m_Context->bUseIndirect && !(Params->flags & pcrNoIndirect))
    {
        nRequiredBuffers = 1;
    }

    // I do not think this will help, but at least we can try freeing some buffers right now
    if (m_FreeHWBuffers < nRequiredBuffers || !m_Descriptors.GetCount())
    {
        VirtIONetReleaseTransmitBuffers();
    }

    if (nRequiredBuffers > m_TotalHWBuffers)
    {
        // LSO and too many buffers, impossible to send
        result.error = cpeTooLarge;
        DPrintf(0, ("[%s] ERROR: too many fragments(%d required, %d max.avail)!\n", __FUNCTION__,
            nRequiredBuffers, m_TotalHWBuffers));
    }
    else if (m_FreeHWBuffers < nRequiredBuffers || !m_Descriptors.GetCount())
    {
        m_VirtQueue->vq_ops->delay_interrupt(m_VirtQueue);
        result.error = cpeNoBuffer;
    }
    else if (Params->offloalMss && bUseCopy)
    {
        result.error = cpeInternalError;
        DPrintf(0, ("[%s] ERROR: expecting SG for TSO! (%d buffers, %d bytes)\n", __FUNCTION__,
            Params->nofSGFragments, Params->ulDataSize));
    }
    else if (bUseCopy)
    {
        result = DoCopyPacketData(Params);
    }
    else
    {
        UINT nMappedBuffers;
        ULONGLONG paOfIndirectArea = 0;
        PVOID vaOfIndirectArea = NULL;
        auto TXDescriptor = m_Descriptors.Pop();
        NdisZeroMemory(TXDescriptor->HeaderVA(), TXDescriptor->HeaderSize());
        m_SGTable[0].physAddr = TXDescriptor->HeaderPA();
        m_SGTable[0].ulSize = TXDescriptor->HeaderSize();
        PacketMapper(Params->NB, m_SGTable + 1, *TXDescriptor, &mapResult);
        nMappedBuffers = mapResult.usBuffersMapped;
        if (nMappedBuffers)
        {
            nMappedBuffers++;
            if (m_Context->bUseIndirect && !(Params->flags & pcrNoIndirect))
            {
                ULONG space1 = (mapResult.usBufferSpaceUsed + 7) & ~7;
                ULONG space2 = nMappedBuffers * SIZE_OF_SINGLE_INDIRECT_DESC;
                if (TXDescriptor->DataSize() >= (space1 + space2))
                {
                    vaOfIndirectArea = RtlOffsetToPointer(TXDescriptor->DataVA(), space1);
                    paOfIndirectArea = TXDescriptor->DataPA().QuadPart + space1;
                    //TODO: Statistics must be atomic
                    m_Context->extraStatistics.framesIndirect++;
                }
                else if (nMappedBuffers <= m_FreeHWBuffers)
                {
                    // send as is, no indirect
                }
                else
                {
                    result.error = cpeNoIndirect;
                    DPrintf(0, ("[%s] Unexpected ERROR of placement!\n", __FUNCTION__));
                }
            }
            if (result.error == cpeOK)
            {
                if (Params->flags & (pcrTcpChecksum | pcrUdpChecksum))
                {
                    unsigned short addPriorityLen = (Params->flags & pcrPriorityTag) ? ETH_PRIORITY_HEADER_SIZE : 0;

                    auto pheader = static_cast<virtio_net_hdr_basic *>(TXDescriptor->HeaderVA());
                    pheader->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
                    if (!Params->tcpHeaderOffset)
                    {
                        Params->tcpHeaderOffset = QueryTcpHeaderOffset(
                            TXDescriptor->DataVA(),
                            m_Context->Offload.ipHeaderOffset + addPriorityLen,
                            mapResult.usBufferSpaceUsed - m_Context->Offload.ipHeaderOffset - addPriorityLen);
                    }
                    else
                    {
                        Params->tcpHeaderOffset += addPriorityLen;
                    }
                    pheader->csum_start = (USHORT)Params->tcpHeaderOffset;
                    pheader->csum_offset = (Params->flags & pcrTcpChecksum) ? TCP_CHECKSUM_OFFSET : UDP_CHECKSUM_OFFSET;
                }

                if (0 <= m_VirtQueue->vq_ops->add_buf(
                    m_VirtQueue,
                    m_SGTable,
                    nMappedBuffers,
                    0,
                    TXDescriptor,
                    vaOfIndirectArea,
                    paOfIndirectArea))
                {
                    TXDescriptor->SetUsedBuffersNum(nMappedBuffers);
                    m_FreeHWBuffers -= nMappedBuffers;
                    TXDescriptor->SetNB(Params->NB);
                    result.size = Params->ulDataSize;
                    DPrintf(2, ("[%s] Submitted %d buffers (%d bytes), avail %d desc, %d bufs\n",
                        __FUNCTION__, nMappedBuffers, result.size,
                        m_Descriptors.GetCount(), m_FreeHWBuffers
                    ));
                }
                else
                {
                    result.error = cpeInternalError;
                    DPrintf(0, ("[%s] Unexpected ERROR adding buffer to TX engine!..\n", __FUNCTION__));
                }
            }
        }
        else
        {
            DPrintf(0, ("[%s] Unexpected ERROR: packet not mapped\n!", __FUNCTION__));
            result.error = cpeInternalError;
        }

        if (result.error == cpeOK)
        {
            UCHAR ethernetHeader[sizeof(ETH_HEADER)];
            eInspectedPacketType packetType;
            /* get the ethernet header for review */
            Params->NB->PacketCopier(ethernetHeader, sizeof(ethernetHeader), TRUE);
            packetType = QueryPacketType(ethernetHeader);

            m_DescriptorsInUse.PushBack(TXDescriptor);

            //TODO: Statistics must be atomic
            m_Context->Statistics.ifHCOutOctets += result.size;
            switch (packetType)
            {
                case iptBroadcast:
                    m_Context->Statistics.ifHCOutBroadcastOctets += result.size;
                    m_Context->Statistics.ifHCOutBroadcastPkts++;
                    break;
                case iptMilticast:
                    m_Context->Statistics.ifHCOutMulticastOctets += result.size;
                    m_Context->Statistics.ifHCOutMulticastPkts++;
                    break;
                default:
                    m_Context->Statistics.ifHCOutUcastOctets += result.size;
                    m_Context->Statistics.ifHCOutUcastPkts++;
                    break;
            }

            if (Params->flags & pcrLSO)
                m_Context->extraStatistics.framesLSO++;
        }
        else
        {
            m_Descriptors.Push(TXDescriptor);
        }
    }
    if (result.error == cpeNoBuffer && m_DoKickOnNoBuffer)
    {
        m_VirtQueue->vq_ops->kick_always(m_VirtQueue);
        m_DoKickOnNoBuffer = false;
    }
    if (result.error == cpeOK)
    {
        if (Params->flags & (pcrTcpChecksum | pcrUdpChecksum))
            m_Context->extraStatistics.framesCSOffload++;
    }
    return result;
}

//TODO: Temporary, needs review
UINT CTXVirtQueue::VirtIONetReleaseTransmitBuffers()
{
    UINT len, i = 0;
    CTXDescriptor *TXDescriptor;

    DEBUG_ENTRY(4);

    while(NULL != (TXDescriptor = (CTXDescriptor *) m_VirtQueue->vq_ops->get_buf(m_VirtQueue, &len)))
    {
        m_DescriptorsInUse.Remove(TXDescriptor);
        if (!TXDescriptor->GetUsedBuffersNum())
        {
            DPrintf(0, ("[%s] ERROR: nofUsedBuffers not set!\n", __FUNCTION__));
        }
        m_FreeHWBuffers += TXDescriptor->GetUsedBuffersNum();
        OnTransmitBufferReleased(TXDescriptor);
        m_Descriptors.Push(TXDescriptor);
        DPrintf(3, ("[%s] Free Tx: desc %d, buff %d\n", __FUNCTION__, m_Descriptors.GetCount(), m_FreeHWBuffers));
        TXDescriptor->SetUsedBuffersNum(0);
        ++i;
    }
    if (i)
    {
        NdisGetCurrentSystemTime(&m_Context->LastTxCompletionTimeStamp);
        m_DoKickOnNoBuffer = true;
        m_Context->nDetectedStoppedTx = 0;
    }
    DEBUG_EXIT_STATUS((i ? 3 : 5), i);
    return i;
}

//TODO: Needs review
void CTXVirtQueue::ProcessTXCompletions()
{
    if (m_Descriptors.GetCount() < m_TotalDescriptors)
    {
        VirtIONetReleaseTransmitBuffers();
    }
}

//TODO: Needs review
void CTXVirtQueue::OnTransmitBufferReleased(CTXDescriptor *TXDescriptor)
{
    auto NB = TXDescriptor->GetNB();
    NB->SendComplete();
    CNB::Destroy(NB, m_Context->MiniportHandle);
}

void CTXVirtQueue::Shutdown()
{
    m_VirtQueue->vq_ops->shutdown(m_VirtQueue);

    m_DescriptorsInUse.ForEachDetached([this](CTXDescriptor *TXDescriptor)
                                           {
                                                m_Descriptors.Push(TXDescriptor);
                                                m_FreeHWBuffers += TXDescriptor->GetUsedBuffersNum();
                                           });
}
