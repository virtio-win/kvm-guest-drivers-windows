#include "ndis56common.h"
#include "ParaNdis-VirtQueue.h"
#include "kdebugprint.h"

bool CVirtQueue::AllocateQueueMemory()
{
    ULONG NumEntries, AllocationSize;
    VirtIODeviceQueryQueueAllocation(m_IODevice, m_Index, &NumEntries, &AllocationSize);
    return (AllocationSize != 0) ? m_SharedMemory.Allocate(AllocationSize) : false;
}

void CVirtQueue::Renew()
{
    auto virtioDev = m_VirtQueue->vdev;
    auto info = &virtioDev->info[m_VirtQueue->index];
    auto pageNum = static_cast<ULONG>(info->phys.QuadPart >> PAGE_SHIFT);
    DPrintf(0, ("[%s] devaddr %p, queue %d, pfn %x\n", __FUNCTION__, virtioDev->addr, info->queue_index, pageNum));
    WriteVirtIODeviceWord(virtioDev->addr + VIRTIO_PCI_QUEUE_SEL, static_cast<UINT16>(info->queue_index));
    WriteVirtIODeviceRegister(virtioDev->addr + VIRTIO_PCI_QUEUE_PFN, pageNum);
}

bool CVirtQueue::Create(UINT Index,
    VirtIODevice *IODevice,
    NDIS_HANDLE DrvHandle,
    bool UsePublishedIndices)
{
    m_DrvHandle = DrvHandle;
    m_Index = Index;
    m_IODevice = IODevice;
    if (!m_SharedMemory.Create(DrvHandle))
    {
        DPrintf(0, ("[%s] - shared memory creation failed\n", __FUNCTION__));
        return false;
    }

    m_UsePublishedIndices = UsePublishedIndices;

    ASSERT(m_VirtQueue == nullptr);

    if(AllocateQueueMemory())
    {
        m_VirtQueue = VirtIODevicePrepareQueue(m_IODevice,
                                               m_Index,
                                               m_SharedMemory.GetPA(),
                                               m_SharedMemory.GetVA(),
                                               m_SharedMemory.GetSize(),
                                               nullptr,
                                               static_cast<BOOLEAN>(m_UsePublishedIndices));
        if (m_VirtQueue == nullptr)
        {
            DPrintf(0, ("[%s] - queue preparation failed for index %u\n", __FUNCTION__, Index));
        }
    }
    else
    {
        DPrintf(0, ("[%s] - queue memory allocation failed\n", __FUNCTION__, Index));
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

void CVirtQueue::Shutdown()
{
    virtqueue_shutdown(m_VirtQueue);
}

bool CTXVirtQueue::PrepareBuffers()
{
    auto NumBuffers = min(m_MaxBuffers, GetRingSize());

    for (m_TotalDescriptors = 0; m_TotalDescriptors < NumBuffers; m_TotalDescriptors++)
    {
        auto TXDescr = new (m_Context->MiniportHandle) CTXDescriptor();
        if (TXDescr == nullptr)
        {
            break;
        }

        if (!TXDescr->Create(m_DrvHandle,
            m_HeaderSize,
            m_SGTable,
            m_SGTableCapacity,
            m_Context->bUseIndirect ? true : false,
            m_Context->bAnyLaypout ? true : false))
        {
            CTXDescriptor::Destroy(TXDescr, m_Context->MiniportHandle);
            break;
        }

        m_Descriptors.Push(TXDescr);
    }

    m_FreeHWBuffers = m_TotalHWBuffers = NumBuffers;
    DPrintf(0, ("[%s] available %d Tx descriptors\n", __FUNCTION__, m_TotalDescriptors));

    return m_TotalDescriptors > 0;
}

void CTXVirtQueue::FreeBuffers()
{
    m_Descriptors.ForEachDetached([this](CTXDescriptor* TXDescr)
                                      { CTXDescriptor::Destroy(TXDescr, m_Context->MiniportHandle); });

    m_TotalDescriptors = m_FreeHWBuffers = 0;
}

bool CTXVirtQueue::Create(UINT Index,
    VirtIODevice *IODevice,
    NDIS_HANDLE DrvHandle,
    bool UsePublishedIndices,
    ULONG MaxBuffers,
    ULONG HeaderSize,
    PPARANDIS_ADAPTER Context)
{
    if (!CVirtQueue::Create(Index, IODevice, DrvHandle, UsePublishedIndices)) {
        return false;
    }

    m_MaxBuffers = MaxBuffers;
    m_HeaderSize = HeaderSize;
    m_Context = Context;

    m_SGTableCapacity = m_Context->bUseIndirect ? VirtIODeviceIndirectPageCapacity() : GetRingSize();

    auto SGBuffer = ParaNdis_AllocateMemoryRaw(m_DrvHandle, m_SGTableCapacity * sizeof(m_SGTable[0]));
    m_SGTable = static_cast<struct VirtIOBufferDescriptor *>(SGBuffer);

    if (m_SGTable == nullptr)
    {
        return false;
    }

    return PrepareBuffers();
}

CTXVirtQueue::~CTXVirtQueue()
{
    if(m_SGTable != nullptr)
    {
        NdisFreeMemory(m_SGTable, 0, 0);
    }

    FreeBuffers();
}

void CTXVirtQueue::KickQueueOnOverflow()
{
    virtqueue_enable_cb_delayed(m_VirtQueue);

    if (m_DoKickOnNoBuffer)
    {
        virtqueue_notify(m_VirtQueue);
        m_DoKickOnNoBuffer = false;
    }
}

void CTXVirtQueue::UpdateTXStats(const CNB &NB, CTXDescriptor &Descriptor)
{
    auto &HeadersArea = Descriptor.HeadersAreaAccessor();
    PVOID EthHeader = HeadersArea.EthHeader();

    //TODO: Statistics must be atomic
    auto BytesSent = NB.GetDataLength();
    auto NBL = NB.GetParentNBL();

    m_Context->Statistics.ifHCOutOctets += BytesSent;

    if (ETH_IS_BROADCAST(EthHeader))
    {
        m_Context->Statistics.ifHCOutBroadcastOctets += BytesSent;
        m_Context->Statistics.ifHCOutBroadcastPkts++;
    }
    else if (ETH_IS_MULTICAST(EthHeader))
    {
        m_Context->Statistics.ifHCOutMulticastOctets += BytesSent;
        m_Context->Statistics.ifHCOutMulticastPkts++;
    }
    else
    {
        m_Context->Statistics.ifHCOutUcastOctets += BytesSent;
        m_Context->Statistics.ifHCOutUcastPkts++;
    }

    if (NBL->IsLSO())
    {
        m_Context->extraStatistics.framesLSO++;

        auto EthHeaders = Descriptor.HeadersAreaAccessor().EthHeadersAreaVA();
        auto TCPHdr = reinterpret_cast<TCPHeader *>(RtlOffsetToPointer(EthHeaders, NBL->TCPHeaderOffset()));

        NBL->UpdateLSOTxStats(NB.GetDataLength() - NBL->TCPHeaderOffset() - TCP_HEADER_LENGTH(TCPHdr));
    }
    else if (NBL->IsTcpCSO() || NBL->IsUdpCSO())
    {
        m_Context->extraStatistics.framesCSOffload++;
    }
}

SubmitTxPacketResult CTXVirtQueue::SubmitPacket(CNB &NB)
{
    if (!m_Descriptors.GetCount())
    {
        KickQueueOnOverflow();
        return SUBMIT_NO_PLACE_IN_QUEUE;
    }

    auto TXDescriptor = m_Descriptors.Pop();
    if (!NB.BindToDescriptor(*TXDescriptor))
    {
        m_Descriptors.Push(TXDescriptor);
        return SUBMIT_FAILURE;
    }

    auto res = TXDescriptor->Enqueue(m_VirtQueue, m_TotalHWBuffers, m_FreeHWBuffers);

    switch (res)
    {
        case SUBMIT_NO_PLACE_IN_QUEUE:
        {
            KickQueueOnOverflow();
            //Fall-through
            __fallthrough;
        }
        case SUBMIT_PACKET_TOO_LARGE:
            __fallthrough;
        case SUBMIT_FAILURE:
        {
            m_Descriptors.Push(TXDescriptor);
            break;
        }
        case SUBMIT_SUCCESS:
        {
            m_FreeHWBuffers -= TXDescriptor->GetUsedBuffersNum();
            m_DescriptorsInUse.PushBack(TXDescriptor);
            UpdateTXStats(NB, *TXDescriptor);
            break;
        }
    }

    return res;
}

//TODO: Temporary, needs review
UINT CTXVirtQueue::VirtIONetReleaseTransmitBuffers()
{
    UINT len, i = 0;
    CTXDescriptor *TXDescriptor;

    DEBUG_ENTRY(4);

    while(NULL != (TXDescriptor = (CTXDescriptor *) virtqueue_get_buf(m_VirtQueue, &len)))
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
        ++i;
    }
    if (i)
    {
        NdisGetCurrentSystemTime(&m_Context->LastTxCompletionTimeStamp);
        m_DoKickOnNoBuffer = true;
    }
    DPrintf((i ? 3 : 5), ("[%s] returning i = %d\n", __FUNCTION__, i)); 
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
    virtqueue_shutdown(m_VirtQueue);

    m_DescriptorsInUse.ForEachDetached([this](CTXDescriptor *TXDescriptor)
                                           {
                                                m_Descriptors.Push(TXDescriptor);
                                                m_FreeHWBuffers += TXDescriptor->GetUsedBuffersNum();
                                           });
}

SubmitTxPacketResult CTXDescriptor::Enqueue(struct virtqueue *VirtQueue, ULONG TotalDescriptors, ULONG FreeDescriptors)
{
    m_UsedBuffersNum = m_Indirect ? 1 : m_CurrVirtioSGLEntry;

    if (m_UsedBuffersNum > TotalDescriptors)
    {
        return SUBMIT_PACKET_TOO_LARGE;
    }

    if (FreeDescriptors < m_UsedBuffersNum)
    {
        return SUBMIT_NO_PLACE_IN_QUEUE;
    }

    if (0 <= virtqueue_add_buf(VirtQueue,
                               m_VirtioSGL,
                               m_CurrVirtioSGLEntry,
                               0,
                               this,
                               m_IndirectArea.GetVA(),
                               m_IndirectArea.GetPA().QuadPart))
    {
        return SUBMIT_SUCCESS;
    }

    return SUBMIT_FAILURE;
}

bool CTXDescriptor::AddDataChunk(const PHYSICAL_ADDRESS &PA, ULONG Length)
{
    if (m_CurrVirtioSGLEntry < m_VirtioSGLSize)
    {
        m_VirtioSGL[m_CurrVirtioSGLEntry].physAddr = PA;
        m_VirtioSGL[m_CurrVirtioSGLEntry].length = Length;
        m_CurrVirtioSGLEntry++;

        return true;
    }

    return false;
}

bool CTXDescriptor::SetupHeaders(ULONG ParsedHeadersLength)
{
    m_CurrVirtioSGLEntry = 0;

    if (m_Headers.VlanHeader()->TCI == 0)
    {
        if (m_AnyLayout)
        {
            return AddDataChunk(m_Headers.VirtioHeaderPA(), m_Headers.VirtioHeaderLength() +
                                ParsedHeadersLength);
        }
        else
        {
            return AddDataChunk(m_Headers.VirtioHeaderPA(), m_Headers.VirtioHeaderLength()) &&
                   AddDataChunk(m_Headers.EthHeaderPA(), ParsedHeadersLength);
        }

    }
    else
    {
        ASSERT(ParsedHeadersLength >= ETH_HEADER_SIZE);

        if (!AddDataChunk(m_Headers.VirtioHeaderPA(), m_Headers.VirtioHeaderLength()) ||
            !AddDataChunk(m_Headers.EthHeaderPA(), ETH_HEADER_SIZE) ||
            !AddDataChunk(m_Headers.VlanHeaderPA(), ETH_PRIORITY_HEADER_SIZE))
        {
            return false;
        }

        if (ParsedHeadersLength > ETH_HEADER_SIZE)
        {
            return AddDataChunk(m_Headers.IPHeadersPA(), ParsedHeadersLength - ETH_HEADER_SIZE);
        }

        return true;
    }
}

bool CTXHeaders::Allocate()
{
    if (m_HeadersBuffer.Allocate(PAGE_SIZE))
    {
        auto VA = m_HeadersBuffer.GetVA();
        auto PA = m_HeadersBuffer.GetPA();

        //Headers buffer layout:
        //    Priority header
        //    Virtio header
        //    Ethernet headers

        m_VlanHeaderVA   = VA;
        m_VirtioHeaderVA = RtlOffsetToPointer(m_VlanHeaderVA, ETH_PRIORITY_HEADER_SIZE);
        m_EthHeaderVA    = RtlOffsetToPointer(m_VirtioHeaderVA, m_VirtioHdrSize);
        m_IPHeadersVA    = RtlOffsetToPointer(m_EthHeaderVA, ETH_HEADER_SIZE);

        m_VirtioHeaderPA.QuadPart = PA.QuadPart + RtlPointerToOffset(VA, m_VirtioHeaderVA);
        m_VlanHeaderPA.QuadPart   = PA.QuadPart + RtlPointerToOffset(VA, m_VlanHeaderVA);
        m_EthHeaderPA.QuadPart    = PA.QuadPart + RtlPointerToOffset(VA, m_EthHeaderVA);
        m_IPHeadersPA.QuadPart    = PA.QuadPart + RtlPointerToOffset(VA, m_IPHeadersVA);

        m_MaxEthHeadersSize = m_HeadersBuffer.GetSize() - ETH_PRIORITY_HEADER_SIZE - m_VirtioHdrSize;

        return true;
    }

    return false;
}
