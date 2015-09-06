#include "ndis56common.h"
#include "kdebugprint.h"

CNBL::CNBL(PNET_BUFFER_LIST NBL, PPARANDIS_ADAPTER Context, CParaNdisTX &ParentTXPath)
    : m_NBL(NBL)
    , m_Context(Context)
    , m_ParentTXPath(&ParentTXPath)
{
    m_NBL->Scratch = this;
    m_LsoInfo.Value = NET_BUFFER_LIST_INFO(m_NBL, TcpLargeSendNetBufferListInfo);
    m_CsoInfo.Value = NET_BUFFER_LIST_INFO(m_NBL, TcpIpChecksumNetBufferListInfo);
}

CNBL::~CNBL()
{
    CDpcIrqlRaiser OnDpc;

    m_MappedBuffers.ForEachDetached([this](CNB *NB)
                                    { CNB::Destroy(NB, m_Context->MiniportHandle); });

    m_Buffers.ForEachDetached([this](CNB *NB)
                              { CNB::Destroy(NB, m_Context->MiniportHandle); });

    if(m_NBL)
    {
        auto NBL = DetachInternalObject();
        NET_BUFFER_LIST_NEXT_NBL(NBL) = nullptr;
        NdisMSendNetBufferListsComplete(m_Context->MiniportHandle, NBL, 0);
    }
}

bool CNBL::ParsePriority()
{
    NDIS_NET_BUFFER_LIST_8021Q_INFO priorityInfo;
    priorityInfo.Value = m_Context->ulPriorityVlanSetting ?
        NET_BUFFER_LIST_INFO(m_NBL, Ieee8021QNetBufferListInfo) : nullptr;

    if (!priorityInfo.TagHeader.VlanId)
    {
        priorityInfo.TagHeader.VlanId = m_Context->VlanId;
    }

    if (priorityInfo.TagHeader.CanonicalFormatId || !IsValidVlanId(m_Context, priorityInfo.TagHeader.VlanId))
    {
        DPrintf(0, ("[%s] Discarded invalid priority tag %p\n", __FUNCTION__, priorityInfo.Value));
        return false;
    }
    else if (priorityInfo.Value)
    {
        // ignore priority, if configured
        if (!IsPrioritySupported(m_Context))
            priorityInfo.TagHeader.UserPriority = 0;
        // ignore VlanId, if specified
        if (!IsVlanSupported(m_Context))
            priorityInfo.TagHeader.VlanId = 0;
        if (priorityInfo.Value)
        {
            m_TCI = static_cast<UINT16>(priorityInfo.TagHeader.UserPriority << 13 | priorityInfo.TagHeader.VlanId);
            DPrintf(1, ("[%s] Populated priority tag %p\n", __FUNCTION__, priorityInfo.Value));
        }
    }

    return true;
}

void CNBL::RegisterNB(CNB *NB)
{
    m_Buffers.PushBack(NB);
    m_BuffersNumber++;
}

void CNBL::RegisterMappedNB(CNB *NB)
{
    if (m_MappedBuffers.PushBack(NB) == m_BuffersNumber)
    {
        m_ParentTXPath->NBLMappingDone(this);
    }
}

bool CNBL::ParseBuffers()
{
    m_MaxDataLength = 0;

    for (auto NB = NET_BUFFER_LIST_FIRST_NB(m_NBL); NB != nullptr; NB = NET_BUFFER_NEXT_NB(NB))
    {
        CNB *NBHolder = new (m_Context->MiniportHandle) CNB(NB, this, m_Context);
        if(!NBHolder || !NBHolder->IsValid())
        {
            return false;
        }
        RegisterNB(NBHolder);
        m_MaxDataLength = max(m_MaxDataLength, NBHolder->GetDataLength());
    }

    if(m_MaxDataLength == 0)
    {
        DPrintf(0, ("Empty NBL (%p) dropped\n", __FUNCTION__, m_NBL));
        return false;
    }

    return true;
}

bool CNBL::NeedsLSO()
{
    return m_MaxDataLength > m_Context->MaxPacketSize.nMaxFullSizeOS;
}

bool CNBL::FitsLSO()
{
    return (m_MaxDataLength <= PARANDIS_MAX_LSO_SIZE + LsoTcpHeaderOffset() + MAX_TCP_HEADER_SIZE);
}

bool CNBL::ParseLSO()
{
    ASSERT(IsLSO());

    if (m_LsoInfo.LsoV1Transmit.Type != NDIS_TCP_LARGE_SEND_OFFLOAD_V1_TYPE &&
        m_LsoInfo.LsoV2Transmit.Type != NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE)
    {
        return false;
    }

    if (NeedsLSO() &&
        (!m_LsoInfo.LsoV2Transmit.MSS ||
         !m_LsoInfo.LsoV2Transmit.TcpHeaderOffset))
    {
        return false;
    }

    if (!FitsLSO())
    {
        return false;
    }

    if (!LsoTcpHeaderOffset() != !MSS())
    {
        return false;
    }

    if ((!m_Context->Offload.flags.fTxLso || !m_Context->bOffloadv4Enabled) &&
        m_LsoInfo.LsoV2Transmit.IPVersion == NDIS_TCP_LARGE_SEND_OFFLOAD_IPv4)
    {
        return false;
    }

    if (m_LsoInfo.LsoV2Transmit.Type == NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE &&
        m_LsoInfo.LsoV2Transmit.IPVersion == NDIS_TCP_LARGE_SEND_OFFLOAD_IPv6 &&
        (!m_Context->Offload.flags.fTxLsov6 || !m_Context->bOffloadv6Enabled))
    {
        return false;
    }

    return true;
}

template <typename TClassPred, typename TOffloadPred, typename TSupportedPred>
bool CNBL::ParseCSO(TClassPred IsClass, TOffloadPred IsOffload,
                    TSupportedPred IsSupported, LPSTR OffloadName)
{
    ASSERT(IsClass());
    UNREFERENCED_PARAMETER(IsClass);

    if (IsOffload())
    {
        if(!IsSupported())
        {
            DPrintf(0, ("[%s] %s request when it is not supported\n", __FUNCTION__, OffloadName));
#if FAIL_UNEXPECTED
            // ignore unexpected CS requests while this passes WHQL
            return false;
#endif
        }
    }
    return true;
}

bool CNBL::ParseOffloads()
{
    if (IsLSO())
    {
        if(!ParseLSO())
        {
            return false;
        }
    }
    else if (IsIP4CSO())
    {
        if(!ParseCSO([this] () -> bool { return IsIP4CSO(); },
                     [this] () -> bool { return m_CsoInfo.Transmit.TcpChecksum; },
                     [this] () -> bool { return m_Context->Offload.flags.fTxTCPChecksum && m_Context->bOffloadv4Enabled; },
                     "TCP4 CSO"))
        {
            return false;
        }
        else if(!ParseCSO([this] () -> bool { return IsIP4CSO(); },
                          [this] () -> bool { return m_CsoInfo.Transmit.UdpChecksum; },
                          [this] () -> bool { return m_Context->Offload.flags.fTxUDPChecksum && m_Context->bOffloadv4Enabled; },
                          "UDP4 CSO"))
        {
            return false;
        }

        if(!ParseCSO([this] () -> bool { return IsIP4CSO(); },
                     [this] () -> bool { return m_CsoInfo.Transmit.IpHeaderChecksum; },
                     [this] () -> bool { return m_Context->Offload.flags.fTxIPChecksum && m_Context->bOffloadv4Enabled; },
                     "IP4 CSO"))
        {
            return false;
        }
    }
    else if (IsIP6CSO())
    {
        if(!ParseCSO([this] () -> bool { return IsIP6CSO(); },
                     [this] () -> bool { return m_CsoInfo.Transmit.TcpChecksum; },
                     [this] () -> bool { return m_Context->Offload.flags.fTxTCPv6Checksum && m_Context->bOffloadv6Enabled; },
                     "TCP6 CSO"))
        {
            return false;
        }
        else if(!ParseCSO([this] () -> bool { return IsIP6CSO(); },
                          [this] () -> bool { return m_CsoInfo.Transmit.UdpChecksum; },
                          [this] () -> bool { return m_Context->Offload.flags.fTxUDPv6Checksum && m_Context->bOffloadv6Enabled; },
                          "UDP6 CSO"))
        {
            return false;
        }
    }

    return true;
}

void CNBL::StartMapping()
{
    CDpcIrqlRaiser OnDpc;

    AddRef();

    m_Buffers.ForEachDetached([this](CNB *NB)
                              {
                                  if (!NB->ScheduleBuildSGListForTx())
                                  {
                                      m_HaveFailedMappings = true;
                                      NB->MappingDone(nullptr);
                                  }
                              });

    Release();
}

void CNBL::OnLastReferenceGone()
{
    Destroy(this, m_Context->MiniportHandle);
}

CParaNdisTX::CParaNdisTX()
{ }

bool CParaNdisTX::Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex)
{
    m_Context = Context;
    m_queueIndex = (u16)DeviceQueueIndex;

    return m_VirtQueue.Create(DeviceQueueIndex,
        m_Context->IODevice,
        m_Context->MiniportHandle,
        m_Context->bDoPublishIndices ? true : false,
        m_Context->maxFreeTxDescriptors,
        m_Context->nVirtioHeaderSize,
        m_Context);
}

void CParaNdisTX::Send(PNET_BUFFER_LIST NBL)
{
    PNET_BUFFER_LIST nextNBL = nullptr;

    for(auto currNBL = NBL; currNBL != nullptr; currNBL = nextNBL)
    {
        nextNBL = NET_BUFFER_LIST_NEXT_NBL(currNBL);
        NET_BUFFER_LIST_NEXT_NBL(currNBL) = nullptr;

        auto NBLHolder = new (m_Context->MiniportHandle) CNBL(currNBL, m_Context, *this);

        if (NBLHolder == nullptr)
        {
            CNBL OnStack(currNBL, m_Context, *this);
            OnStack.SetStatus(NDIS_STATUS_RESOURCES);
            DPrintf(0, ("ERROR: Failed to allocate CNBL instance\n"));
            continue;
        }

        if(NBLHolder->Prepare() &&
           ParaNdis_IsSendPossible(m_Context))
        {
            NBLHolder->StartMapping();
        }
        else
        {
            NBLHolder->SetStatus(ParaNdis_ExactSendFailureStatus(m_Context));
            NBLHolder->Release();
        }
    }
}

void CParaNdisTX::NBLMappingDone(CNBL *NBLHolder)
{
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    if (NBLHolder->MappingSuceeded())
    {
        DoWithTXLock([NBLHolder, this](){ m_SendList.PushBack(NBLHolder); });
        DoPendingTasks(false);
    }
    else
    {
        NBLHolder->SetStatus(NDIS_STATUS_FAILURE);
        NBLHolder->Release();
    }
}

CNB *CNBL::PopMappedNB()
{
    m_MappedBuffersDetached++;
    return m_MappedBuffers.Pop();
}
void CNBL::PushMappedNB(CNB *NB)
{
    m_MappedBuffersDetached--;
    m_MappedBuffers.Push(NB);
}

//TODO: Needs review
void CNBL::NBComplete()
{
    m_BuffersDone++;
    m_MappedBuffersDetached--;
}

bool CNBL::IsSendDone()
{
    return m_BuffersDone == m_BuffersNumber;
}

//TODO: Needs review
void CNBL::CompleteMappedBuffers()
{
    m_MappedBuffers.ForEachDetached([this](CNB *NB)
                                        {
                                            NBComplete();
                                            CNB::Destroy(NB, m_Context->MiniportHandle);
                                        });
}

PNET_BUFFER_LIST CNBL::DetachInternalObject()
{
    m_MappedBuffers.ForEach([this](CNB *NB)
    {
        NB->ReleaseResources();
    });

    // do it for both LsoV1 and LsoV2
    if (IsLSO())
    {
        m_LsoInfo.LsoV1TransmitComplete.TcpPayload = m_TransferSize;
    }

    //Flush changes made in LSO structures
    NET_BUFFER_LIST_INFO(m_NBL, TcpLargeSendNetBufferListInfo) = m_LsoInfo.Value;

    auto Res = m_NBL;
    m_NBL = nullptr;
    return Res;
}

PNET_BUFFER_LIST CParaNdisTX::ProcessWaitingList()
{
    PNET_BUFFER_LIST CompletedNBLs = nullptr;


    m_WaitingList.ForEachDetachedIf([](CNBL* NBL) { return NBL->IsSendDone(); },
                                        [&](CNBL* NBL)
                                        {
                                            NBL->SetStatus(NDIS_STATUS_SUCCESS);
                                            auto RawNBL = NBL->DetachInternalObject();
                                            NBL->Release();
                                            NET_BUFFER_LIST_NEXT_NBL(RawNBL) = CompletedNBLs;
                                            CompletedNBLs = RawNBL;
                                        });

    return CompletedNBLs;
}

//TODO: Needs review
PNET_BUFFER_LIST CParaNdisTX::RemoveAllNonWaitingNBLs()
{
    PNET_BUFFER_LIST RemovedNBLs = nullptr;
    auto status = ParaNdis_ExactSendFailureStatus(m_Context);

    m_SendList.ForEachDetachedIf([](CNBL *NBL) { return !NBL->HaveDetachedBuffers(); },
                                     [&](CNBL *NBL)
                                     {
                                         NBL->SetStatus(status);
                                         auto RawNBL = NBL->DetachInternalObject();
                                         NBL->Release();
                                         NET_BUFFER_LIST_NEXT_NBL(RawNBL) = RemovedNBLs;
                                         RemovedNBLs = RawNBL;
                                     });

    m_SendList.ForEach([](CNBL *NBL) { NBL->CompleteMappedBuffers(); });

    return RemovedNBLs;
}

bool CParaNdisTX::Pause()
{
    PNET_BUFFER_LIST NBL = nullptr;
    bool res;

    DoWithTXLock([this, &NBL, &res]()
                 {
                     NBL = RemoveAllNonWaitingNBLs();
                     res = (!m_VirtQueue.HasPacketsInHW() && m_WaitingList.IsEmpty());
                 });

    if(NBL != nullptr)
    {
        NdisMSendNetBufferListsComplete(m_Context->MiniportHandle, NBL, 0);
    }

    return res;
}

PNET_BUFFER_LIST CParaNdisTX::BuildCancelList(PVOID CancelId)
{
    PNET_BUFFER_LIST CanceledNBLs = nullptr;
    TSpinLocker LockedContext(m_Lock);

    m_SendList.ForEachDetachedIf([CancelId](CNBL* NBL){ return NBL->MatchCancelID(CancelId) && !NBL->HaveDetachedBuffers(); },
                                     [this, &CanceledNBLs](CNBL* NBL)
                                     {
                                         NBL->SetStatus(NDIS_STATUS_SEND_ABORTED);
                                         auto RawNBL = NBL->DetachInternalObject();
                                         NBL->Release();
                                         NET_BUFFER_LIST_NEXT_NBL(RawNBL) = CanceledNBLs;
                                         CanceledNBLs = RawNBL;
                                     });

    return CanceledNBLs;
}

void CParaNdisTX::CancelNBLs(PVOID CancelId)
{
    auto CanceledNBLs = BuildCancelList(CancelId);
    if (CanceledNBLs != nullptr)
    {
        NdisMSendNetBufferListsComplete(m_Context->MiniportHandle, CanceledNBLs, 0);
    }
}

//TODO: Requires review
BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) CParaNdisTX::RestartQueueSynchronously(tSynchronizedContext *ctx)
{
    auto TXPath = static_cast<CParaNdisTX *>(ctx->Parameter);
    return !TXPath->m_VirtQueue.Restart();
}

//TODO: Requires review
bool CParaNdisTX::RestartQueue(bool DoKick)
{
    TSpinLocker LockedContext(m_Lock);
    auto res = ParaNdis_SynchronizeWithInterrupt(m_Context,
                                                 m_messageIndex,
                                                 CParaNdisTX::RestartQueueSynchronously,
                                                 this) ? true : false;

    if(DoKick)
    {
        Kick();
    }

    return res;
}

bool CParaNdisTX::SendMapped(bool IsInterrupt, PNET_BUFFER_LIST &NBLFailNow)
{
    if(!ParaNdis_IsSendPossible(m_Context))
    {
        NBLFailNow = RemoveAllNonWaitingNBLs();
        if (NBLFailNow)
        {
            DPrintf(0, (__FUNCTION__ " Failing send"));
        }
    }
    else
    {
        bool SentOutSomeBuffers = false;
        auto HaveBuffers = true;

        while (HaveBuffers && HaveMappedNBLs())
        {
            auto NBLHolder = PopMappedNBL();

            if (NBLHolder->HaveMappedBuffers())
            {
                auto NBHolder = NBLHolder->PopMappedNB();
                auto result = m_VirtQueue.SubmitPacket(*NBHolder);

                switch (result)
                {
                case SUBMIT_NO_PLACE_IN_QUEUE:
                    NBLHolder->PushMappedNB(NBHolder);
                    PushMappedNBL(NBLHolder);
                    HaveBuffers = false;
                    // break the loop, allow to kick and free some buffers
                    break;

                case SUBMIT_FAILURE:
                    __fallthrough;
                case SUBMIT_SUCCESS:
                    __fallthrough;
                case SUBMIT_PACKET_TOO_LARGE:
                    // if this NBL finished?
                    if (!NBLHolder->HaveMappedBuffers())
                    {
                        m_WaitingList.Push(NBLHolder);
                    }
                    else
                    {
                        // no, insert it back to the queue
                        PushMappedNBL(NBLHolder);
                    }

                    if (result == SUBMIT_SUCCESS)
                    {
                        SentOutSomeBuffers = true;
                    }
                    else
                    {
                        NBHolder->SendComplete();
                        CNB::Destroy(NBHolder, m_Context->MiniportHandle);
                    }
                    break;
                default:
                    ASSERT(false);
                    break;
                }
            }
            else
            {

                //TODO: Refactoring needed
                //This is a case when pause called, mapped list cleared but NBL is still in the send list
                m_WaitingList.Push(NBLHolder);
            }
        }

        if (SentOutSomeBuffers)
        {
            DPrintf(2, ("[%s] sent down\n", __FUNCTION__, SentOutSomeBuffers));
            if (IsInterrupt)
            {
                return true;
            }
            else
            {
                m_VirtQueue.Kick();
            }
        }
    }

    return false;
}

bool CParaNdisTX::DoPendingTasks(bool IsInterrupt)
{
    ONPAUSECOMPLETEPROC CallbackToCall = nullptr;
    PNET_BUFFER_LIST pNBLFailNow = nullptr;
    PNET_BUFFER_LIST pNBLReturnNow = nullptr;
    bool bDoKick = false;

    DoWithTXLock([&] ()
                 {
                    m_VirtQueue.ProcessTXCompletions();
                    bDoKick = SendMapped(IsInterrupt, pNBLFailNow);
                    pNBLReturnNow = ProcessWaitingList();
                    {
                        NdisDprAcquireSpinLock(&m_Context->m_CompletionLock);

                        if (m_Context->SendState == srsPausing)
                        {
                            CNdisPassiveWriteAutoLock tLock(m_Context->m_PauseLock);

                            if (m_Context->SendState == srsPausing && !ParaNdis_HasPacketsInHW(m_Context))
                            {
                                CallbackToCall = m_Context->SendPauseCompletionProc;
                                m_Context->SendPauseCompletionProc = nullptr;
                                m_Context->SendState = srsDisabled;
                            }
                        }
                    }
                 });

    if (pNBLFailNow)
    {
        NdisMSendNetBufferListsComplete(m_Context->MiniportHandle, pNBLFailNow,
                                        NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);
    }

    if (pNBLReturnNow)
    {
        NdisMSendNetBufferListsComplete(m_Context->MiniportHandle, pNBLReturnNow,
                                        NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);
    }

#pragma warning(suppress: 26110)
    NdisDprReleaseSpinLock(&m_Context->m_CompletionLock);

    if (CallbackToCall != nullptr)
    {
        CallbackToCall(m_Context);
    }

    return bDoKick;
}

void CNB::MappingDone(PSCATTER_GATHER_LIST SGL)
{
    m_SGL = SGL;
    m_ParentNBL->RegisterMappedNB(this);
}

CNB::~CNB()
{
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    if(m_SGL != nullptr)
    {
        NdisMFreeNetBufferSGList(m_Context->DmaHandle, m_SGL, m_NB);
    }
}

void CNB::ReleaseResources()
{
    if (m_SGL != nullptr)
    {
        NdisMFreeNetBufferSGList(m_Context->DmaHandle, m_SGL, m_NB);
        m_SGL = nullptr;
    }
}

bool CNB::ScheduleBuildSGListForTx()
{
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    return NdisMAllocateNetBufferSGList(m_Context->DmaHandle, m_NB, this,
                                        NDIS_SG_LIST_WRITE_TO_DEVICE, nullptr, 0) == NDIS_STATUS_SUCCESS;
}

void CNB::PopulateIPLength(IPHeader *IpHeader, USHORT IpLength) const
{
    if ((IpHeader->v4.ip_verlen & 0xF0) == 0x40)
    {
        if (!IpHeader->v4.ip_length)
        {
            IpHeader->v4.ip_length = swap_short(IpLength);
        }
    }
    else if ((IpHeader->v6.ip6_ver_tc & 0xF0) == 0x60)
    {
        if (!IpHeader->v6.ip6_payload_len)
        {
            IpHeader->v6.ip6_payload_len = swap_short(IpLength - IPV6_HEADER_MIN_SIZE);
        }
    }
    else
    {
        DPrintf(0, ("[%s] ERROR: Bad version of IP header!\n", __FUNCTION__));
    }
}

void CNB::SetupLSO(virtio_net_hdr *VirtioHeader, PVOID IpHeader, ULONG EthPayloadLength) const
{
    PopulateIPLength(reinterpret_cast<IPHeader*>(IpHeader), static_cast<USHORT>(EthPayloadLength));

    tTcpIpPacketParsingResult packetReview;
    packetReview = ParaNdis_CheckSumVerifyFlat(reinterpret_cast<IPv4Header*>(IpHeader), EthPayloadLength,
                                               pcrIpChecksum | pcrFixIPChecksum | pcrTcpChecksum | pcrFixPHChecksum,
                                               FALSE,
                                               __FUNCTION__);

    if (packetReview.xxpCheckSum == ppresPCSOK || packetReview.fixedXxpCS)
    {
        auto IpHeaderOffset = m_Context->Offload.ipHeaderOffset;
        auto VHeader = static_cast<virtio_net_hdr*>(VirtioHeader);
        auto PriorityHdrLen = (m_ParentNBL->TCI() != 0) ? ETH_PRIORITY_HEADER_SIZE : 0;

        VHeader->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
        VHeader->gso_type = packetReview.ipStatus == ppresIPV4 ? VIRTIO_NET_HDR_GSO_TCPV4 : VIRTIO_NET_HDR_GSO_TCPV6;
        VHeader->hdr_len = (USHORT)(packetReview.XxpIpHeaderSize + IpHeaderOffset + PriorityHdrLen);
        VHeader->gso_size = (USHORT)m_ParentNBL->MSS();
        VHeader->csum_start = (USHORT)(m_ParentNBL->TCPHeaderOffset() + PriorityHdrLen);
        VHeader->csum_offset = TCP_CHECKSUM_OFFSET;
    }
}

USHORT CNB::QueryL4HeaderOffset(PVOID PacketData, ULONG IpHeaderOffset) const
{
    USHORT Res;
    auto ppr = ParaNdis_ReviewIPPacket(RtlOffsetToPointer(PacketData, IpHeaderOffset),
                                       GetDataLength(), FALSE, __FUNCTION__);
    if (ppr.ipStatus != ppresNotIP)
    {
        Res = static_cast<USHORT>(IpHeaderOffset + ppr.ipHeaderSize);
    }
    else
    {
        DPrintf(0, ("[%s] ERROR: NOT an IP packet - expected troubles!\n", __FUNCTION__));
        Res = 0;
    }
    return Res;
}

void CNB::SetupCSO(virtio_net_hdr *VirtioHeader, ULONG L4HeaderOffset) const
{
    u16 PriorityHdrLen = m_ParentNBL->TCI() ? ETH_PRIORITY_HEADER_SIZE : 0;

    VirtioHeader->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
    VirtioHeader->csum_start = static_cast<u16>(L4HeaderOffset) + PriorityHdrLen;
    VirtioHeader->csum_offset = m_ParentNBL->IsTcpCSO() ? TCP_CHECKSUM_OFFSET : UDP_CHECKSUM_OFFSET;
}

void CNB::DoIPHdrCSO(PVOID IpHeader, ULONG EthPayloadLength) const
{
    ParaNdis_CheckSumVerifyFlat(IpHeader,
                                EthPayloadLength,
                                pcrIpChecksum | pcrFixIPChecksum, FALSE,
                                __FUNCTION__);
}

bool CNB::FillDescriptorSGList(CTXDescriptor &Descriptor, ULONG ParsedHeadersLength) const
{
    return Descriptor.SetupHeaders(ParsedHeadersLength) &&
           MapDataToVirtioSGL(Descriptor, ParsedHeadersLength + NET_BUFFER_DATA_OFFSET(m_NB));
}

bool CNB::MapDataToVirtioSGL(CTXDescriptor &Descriptor, ULONG Offset) const
{
    for (ULONG i = 0; i < m_SGL->NumberOfElements; i++)
    {
        if (Offset < m_SGL->Elements[i].Length)
        {
            PHYSICAL_ADDRESS PA;
            PA.QuadPart = m_SGL->Elements[i].Address.QuadPart + Offset;

            if (!Descriptor.AddDataChunk(PA, m_SGL->Elements[i].Length - Offset))
            {
                return false;
            }

            Offset = 0;
        }
        else
        {
            Offset -= m_SGL->Elements[i].Length;
        }
    }

    return true;
}

bool CNB::CopyHeaders(PVOID Destination, ULONG MaxSize, ULONG &HeadersLength, ULONG &L4HeaderOffset) const
{
    HeadersLength = 0;
    L4HeaderOffset = 0;

    if (m_ParentNBL->IsLSO() || m_ParentNBL->IsTcpCSO())
    {
        L4HeaderOffset = m_ParentNBL->TCPHeaderOffset();
        HeadersLength = L4HeaderOffset + sizeof(TCPHeader);
        Copy(Destination, HeadersLength);
    }
    else if (m_ParentNBL->IsUdpCSO())
    {
        Copy(Destination, MaxSize);
        L4HeaderOffset = QueryL4HeaderOffset(Destination, m_Context->Offload.ipHeaderOffset);
        HeadersLength = L4HeaderOffset + sizeof(UDPHeader);
    }
    else if (m_ParentNBL->IsIPHdrCSO())
    {
        Copy(Destination, MaxSize);
        HeadersLength = QueryL4HeaderOffset(Destination, m_Context->Offload.ipHeaderOffset);
        L4HeaderOffset = HeadersLength;
    }
    else
    {
        HeadersLength = ETH_HEADER_SIZE;
        Copy(Destination, HeadersLength);
    }

    return (HeadersLength <= MaxSize);
}

void CNB::BuildPriorityHeader(PETH_HEADER EthHeader, PVLAN_HEADER VlanHeader) const
{
    VlanHeader->TCI = RtlUshortByteSwap(m_ParentNBL->TCI());

    if (VlanHeader->TCI != 0)
    {
        VlanHeader->EthType = EthHeader->EthType;
        EthHeader->EthType = RtlUshortByteSwap(PRIO_HEADER_ETH_TYPE);
    }
}

void CNB::PrepareOffloads(virtio_net_hdr *VirtioHeader, PVOID IpHeader, ULONG EthPayloadLength, ULONG L4HeaderOffset) const
{
    *VirtioHeader = {};

    if (m_ParentNBL->IsLSO())
    {
        SetupLSO(VirtioHeader, IpHeader, EthPayloadLength);
    }
    else if (m_ParentNBL->IsTcpCSO() || m_ParentNBL->IsUdpCSO())
    {
        SetupCSO(VirtioHeader, L4HeaderOffset);
    }

    if (m_ParentNBL->IsIPHdrCSO())
    {
        DoIPHdrCSO(IpHeader, EthPayloadLength);
    }
}

bool CNB::BindToDescriptor(CTXDescriptor &Descriptor)
{
    if (m_SGL == nullptr)
    {
        return false;
    }

    Descriptor.SetNB(this);

    auto &HeadersArea = Descriptor.HeadersAreaAccessor();
    auto EthHeaders = HeadersArea.EthHeadersAreaVA();
    ULONG HeadersLength;
    ULONG L4HeaderOffset;

    if (!CopyHeaders(EthHeaders, HeadersArea.MaxEthHeadersSize(), HeadersLength, L4HeaderOffset))
    {
        return false;
    }

    BuildPriorityHeader(HeadersArea.EthHeader(), HeadersArea.VlanHeader());
    PrepareOffloads(HeadersArea.VirtioHeader(),
                    HeadersArea.IPHeaders(),
                    GetDataLength() - m_Context->Offload.ipHeaderOffset,
                    L4HeaderOffset);

    return FillDescriptorSGList(Descriptor, HeadersLength);
}

bool CNB::Copy(PVOID Dst, ULONG Length) const
{
    ULONG CurrOffset = NET_BUFFER_CURRENT_MDL_OFFSET(m_NB);
    ULONG Copied = 0;

    for (PMDL CurrMDL = NET_BUFFER_CURRENT_MDL(m_NB);
         CurrMDL != nullptr && Copied < Length;
         CurrMDL = CurrMDL->Next)
    {
        ULONG CurrLen;
        PVOID CurrAddr;

#if NDIS_SUPPORT_NDIS620
        NdisQueryMdl(CurrMDL, &CurrAddr, &CurrLen, LowPagePriority | MdlMappingNoExecute);
#else
        NdisQueryMdl(CurrMDL, &CurrAddr, &CurrLen, LowPagePriority);
#endif

        if (CurrAddr == nullptr)
        {
            break;
        }

        CurrLen = min(CurrLen - CurrOffset, Length - Copied);

        NdisMoveMemory(RtlOffsetToPointer(Dst, Copied),
                       RtlOffsetToPointer(CurrAddr, CurrOffset),
                       CurrLen);

        Copied += CurrLen;
        CurrOffset = 0;
    }

    return (Copied == Length);
}
