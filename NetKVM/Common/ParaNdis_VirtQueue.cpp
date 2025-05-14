#include "ndis56common.h"
#include "ParaNdis-VirtQueue.h"
#include "kdebugprint.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_VirtQueue.tmh"
#endif

bool CVirtQueue::AllocateQueueMemory()
{
    USHORT NumEntries;
    ULONG RingSize, HeapSize;

    if (!CanTouchHardware())
    {
        return false;
    }

    NTSTATUS status = virtio_query_queue_allocation(m_IODevice, m_Index, &NumEntries, &RingSize, &HeapSize);
    if (!NT_SUCCESS(status))
    {
        DPrintf(0, "virtio_query_queue_allocation(%d) failed with error %x", m_Index, status);
        return false;
    }

    return (RingSize != 0) ? m_SharedMemory.Allocate(RingSize) : false;
}

void CVirtQueue::Renew()
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)m_IODevice->DeviceContext;

    if (!CanTouchHardware())
    {
        return;
    }

    pContext->pPageAllocator = &m_SharedMemory;
    NTSTATUS status = virtio_find_queue(m_IODevice, m_Index, &m_VirtQueue);
    pContext->pPageAllocator = nullptr;

    if (!NT_SUCCESS(status))
    {
        DPrintf(0, "queue setup failed for index %u with error %x", m_Index, status);
        m_VirtQueue = nullptr;
    }
}

bool CVirtQueue::Create(UINT Index, VirtIODevice *IODevice, NDIS_HANDLE DrvHandle)
{
    m_DrvHandle = DrvHandle;
    m_Index = Index;
    m_IODevice = IODevice;

    m_SharedMemory.Initialize(DrvHandle);

    NETKVM_ASSERT(m_VirtQueue == nullptr);

    if (AllocateQueueMemory())
    {
        Renew();
    }
    else
    {
        DPrintf(0, "queue memory allocation failed, index = %d", Index);
    }

    return m_VirtQueue != nullptr;
}

void CVirtQueue::Delete()
{
    if (m_VirtQueue != nullptr)
    {
        virtio_delete_queue(m_VirtQueue);
        m_VirtQueue = nullptr;
    }
}

u16 CVirtQueue::SetMSIVector(u16 vector)
{
    return virtio_set_queue_vector(m_VirtQueue, vector);
}

bool CTXVirtQueue::PrepareBuffers()
{
    auto NumBuffers = min(m_MaxBuffers, GetRingSize());
    auto SGTableCapacity = m_SGTableCapacity;

    if (SGTableCapacity > NumBuffers)
    {
        DPrintf(0, "Limit m_SGTableCapacity by %d", NumBuffers);
        SGTableCapacity = NumBuffers;
    }

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
                             SGTableCapacity,
                             m_Context->bUseIndirect ? true : false,
                             m_Context->bAnyLayout ? true : false))
        {
            CTXDescriptor::Destroy(TXDescr, m_Context->MiniportHandle);
            break;
        }

        m_Descriptors.Push(TXDescr);
    }

    m_FreeHWBuffers = m_TotalHWBuffers = m_TotalDescriptors;
    DPrintf(0, "available %d Tx descriptors", m_TotalDescriptors);

    if (m_Context->extraStatistics.minFreeTxBuffers == 0 ||
        m_Context->extraStatistics.minFreeTxBuffers > m_FreeHWBuffers)
    {
        m_Context->extraStatistics.minFreeTxBuffers = m_FreeHWBuffers;
    }
    return m_TotalDescriptors > 0;
}

void CTXVirtQueue::FreeBuffers()
{
    m_Descriptors.ForEachDetached([this](CTXDescriptor *TXDescr) {
        CTXDescriptor::Destroy(TXDescr, m_Context->MiniportHandle);
    });

    m_TotalDescriptors = m_FreeHWBuffers = 0;
}

bool CTXVirtQueue::Create(UINT Index,
                          VirtIODevice *IODevice,
                          NDIS_HANDLE DrvHandle,
                          ULONG MaxBuffers,
                          ULONG HeaderSize,
                          PPARANDIS_ADAPTER Context)
{
    if (!CVirtQueue::Create(Index, IODevice, DrvHandle))
    {
        return false;
    }

    m_MaxBuffers = MaxBuffers;
    m_HeaderSize = HeaderSize;
    m_Context = Context;

    m_SGTableCapacity = m_Context->bUseIndirect ? virtio_get_indirect_page_capacity() : GetRingSize();
    m_SGTableCapacity = min(m_SGTableCapacity, m_Context->uMaxFragmentsInOneNB);

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
    if (m_SGTable != nullptr)
    {
        NdisFreeMemory(m_SGTable, 0, 0);
        m_SGTable = nullptr;
    }

    FreeBuffers();
}

void CTXVirtQueue::KickQueueOnOverflow()
{
    EnableInterruptsDelayed();

    if (m_DoKickOnNoBuffer)
    {
        KickAlways();
        m_DoKickOnNoBuffer = false;
    }
}

void CTXVirtQueue::UpdateTXStats(const CNB &NB, CTXDescriptor &Descriptor)
{
    auto &HeadersArea = Descriptor.HeadersAreaAccessor();
    PVOID EthHeader = HeadersArea.EthHeader();

    // TODO: Statistics must be atomic
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

    if (m_Context->extraStatistics.minFreeTxBuffers > m_FreeHWBuffers)
    {
        m_Context->extraStatistics.minFreeTxBuffers = m_FreeHWBuffers;
    }
}

SubmitTxPacketResult CTXVirtQueue::SubmitPacket(CNB &NB)
{
    if (!m_Descriptors.GetCount())
    {
        KickQueueOnOverflow();
        return SubmitTxPacketResult::SUBMIT_NO_PLACE_IN_QUEUE;
    }

    auto TXDescriptor = m_Descriptors.Pop();
    auto status = NB.BindToDescriptor(*TXDescriptor);

    switch (status)
    {
        case NBMappingStatus::FAILURE:
            {
                NB.ReturnPages();
                m_Descriptors.Push(TXDescriptor);
                NB.Report(0, false);
                return SubmitTxPacketResult::SUBMIT_FAILURE;
            }
        case NBMappingStatus::NO_RESOURCE:
            {
                m_Descriptors.Push(TXDescriptor);
                return SubmitTxPacketResult::SUBMIT_NO_PLACE_IN_QUEUE;
            }
        case NBMappingStatus::SUCCESS:
            {
                // do nothing
            }
    }

    auto res = TXDescriptor->Enqueue(this, m_TotalHWBuffers, m_FreeHWBuffers);

    switch (res)
    {
        case SubmitTxPacketResult::SUBMIT_NO_PLACE_IN_QUEUE:
            {
                KickQueueOnOverflow();
                // Fall-through
                __fallthrough;
            }
        case SubmitTxPacketResult::SUBMIT_PACKET_TOO_LARGE:
            __fallthrough;
        case SubmitTxPacketResult::SUBMIT_FAILURE:
            {
                m_Descriptors.Push(TXDescriptor);
                NB.Report(0, false);
                break;
            }
        case SubmitTxPacketResult::SUBMIT_SUCCESS:
            {
                m_FreeHWBuffers -= TXDescriptor->GetUsedBuffersNum();
                m_DescriptorsInUse.PushBack(TXDescriptor);
                UpdateTXStats(NB, *TXDescriptor);
                NB.Report(1, true);
                break;
            }
    }

    return res;
}

void CTXVirtQueue::ReleaseOneBuffer(CTXDescriptor *TXDescriptor, CRawCNBList &listDone)
{
    if (!TXDescriptor->GetUsedBuffersNum())
    {
        DPrintf(0, "ERROR: nofUsedBuffers not set!");
    }
    TXDescriptor->GetNB()->ReturnPages();
    m_FreeHWBuffers += TXDescriptor->GetUsedBuffersNum();
    listDone.PushBack(TXDescriptor->GetNB());
    m_Descriptors.Push(TXDescriptor);
    DPrintf(3, "Free Tx: desc %d, buff %d", m_Descriptors.GetCount(), m_FreeHWBuffers);
}

UINT CTXVirtQueue::ReleaseTransmitBuffers(CRawCNBList &listDone)
{
    UINT len, i = 0;
    CTXDescriptor *TXDescriptor;

    DEBUG_ENTRY(4);

    while (NULL != (TXDescriptor = (CTXDescriptor *)GetBuf(&len)))
    {
        m_DescriptorsInUse.Remove(TXDescriptor);
        ReleaseOneBuffer(TXDescriptor, listDone);
        ++i;
    }
    if (i)
    {
        UpdateTimestamp(m_LastTxCompletionTimestamp);
        m_DoKickOnNoBuffer = true;
    }
    DPrintf((i ? 3 : 5), "returning i = %d", i);
    return i;
}

// TODO: Needs review
void CTXVirtQueue::ProcessTXCompletions(CRawCNBList &listDone, bool bKill)
{
    if (m_Descriptors.GetCount() < m_TotalDescriptors)
    {
        if (!bKill && !m_Killed)
        {
            ReleaseTransmitBuffers(listDone);
        }
        else
        {
            LPCSTR func = __FUNCTION__;
            m_Killed = true;
            m_DescriptorsInUse.ForEachDetached([&](CTXDescriptor *TXDescriptor) {
                TraceNoPrefix(0, "[%s] kill: releasing buffer\n", func);
                ReleaseOneBuffer(TXDescriptor, listDone);
            });
        }
    }
}

void CTXVirtQueue::Shutdown()
{
    CVirtQueue::Shutdown();

    m_DescriptorsInUse.ForEachDetached([this](CTXDescriptor *TXDescriptor) {
        m_Descriptors.Push(TXDescriptor);
        m_FreeHWBuffers += TXDescriptor->GetUsedBuffersNum();
    });
}

SubmitTxPacketResult CTXDescriptor::Enqueue(CTXVirtQueue *Queue, ULONG TotalDescriptors, ULONG FreeDescriptors)
{
    m_UsedBuffersNum = m_Indirect ? 1 : m_CurrVirtioSGLEntry;

    if (m_UsedBuffersNum > TotalDescriptors)
    {
        return SubmitTxPacketResult::SUBMIT_PACKET_TOO_LARGE;
    }

    if (FreeDescriptors < m_UsedBuffersNum)
    {
        return SubmitTxPacketResult::SUBMIT_NO_PLACE_IN_QUEUE;
    }

    if (0 <= Queue->AddBuf(m_VirtioSGL,
                           m_CurrVirtioSGLEntry,
                           0,
                           this,
                           m_IndirectArea.Virtual,
                           m_IndirectArea.Physical.QuadPart))
    {
        return SubmitTxPacketResult::SUBMIT_SUCCESS;
    }

    return SubmitTxPacketResult::SUBMIT_FAILURE;
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
            return AddDataChunk(m_Headers.VirtioHeaderPA(), m_Headers.VirtioHeaderLength() + ParsedHeadersLength);
        }
        else
        {
            return AddDataChunk(m_Headers.VirtioHeaderPA(), m_Headers.VirtioHeaderLength()) &&
                   AddDataChunk(m_Headers.EthHeaderPA(), ParsedHeadersLength);
        }
    }
    else
    {
        NETKVM_ASSERT(ParsedHeadersLength >= ETH_HEADER_SIZE);

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

void CTXHeaders::Initialize(ULONG VirtioHdrSize, const tCompletePhysicalAddress &Buffer)
{
    m_VirtioHdrSize = VirtioHdrSize;
    m_HeadersBuffer = Buffer;

    auto VA = m_HeadersBuffer.Virtual;
    auto PA = m_HeadersBuffer.Physical;
    ULONG prioSize = ALIGN_UP(ETH_PRIORITY_HEADER_SIZE, ULONGLONG);

    // Headers buffer layout:
    //     Priority header
    //     Virtio header
    //     Ethernet headers

    m_VlanHeaderVA = VA;
    m_VirtioHeaderVA = RtlOffsetToPointer(m_VlanHeaderVA, prioSize);
    m_EthHeaderVA = RtlOffsetToPointer(m_VirtioHeaderVA, m_VirtioHdrSize);
    m_IPHeadersVA = RtlOffsetToPointer(m_EthHeaderVA, ETH_HEADER_SIZE);

    m_VirtioHeaderPA.QuadPart = PA.QuadPart + RtlPointerToOffset(VA, m_VirtioHeaderVA);
    m_VlanHeaderPA.QuadPart = PA.QuadPart + RtlPointerToOffset(VA, m_VlanHeaderVA);
    m_EthHeaderPA.QuadPart = PA.QuadPart + RtlPointerToOffset(VA, m_EthHeaderVA);
    m_IPHeadersPA.QuadPart = PA.QuadPart + RtlPointerToOffset(VA, m_IPHeadersVA);

    m_MaxEthHeadersSize = m_HeadersBuffer.size - prioSize - m_VirtioHdrSize;
}
