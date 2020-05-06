#include "ndis56common.h"
#include "kdebugprint.h"
#include "ParaNdis_DebugHistory.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_TX.tmh"
#endif

CNBL::CNBL(PNET_BUFFER_LIST NBL, PPARANDIS_ADAPTER Context, CParaNdisTX &ParentTXPath, CAllocationHelper<CNBL> *NBLAllocator, CAllocationHelper<CNB> *NBAllocator)
    : m_NBL(NBL)
    , m_Context(Context)
    , m_ParentTXPath(&ParentTXPath)
    , CNdisAllocatableViaHelper<CNBL>(NBLAllocator)
    , m_NBAllocator(NBAllocator)
{
    m_NBL->Scratch = this;
    m_LsoInfo.Value = NET_BUFFER_LIST_INFO(m_NBL, TcpLargeSendNetBufferListInfo);
    m_CsoInfo.Value = NET_BUFFER_LIST_INFO(m_NBL, TcpIpChecksumNetBufferListInfo);
    ParaNdis_DebugNBLIn(NBL, m_LogIndex);
}

CNBL::~CNBL()
{
    CDpcIrqlRaiser OnDpc;

    m_Buffers.ForEachDetached([this](CNB *NB)
                              { CNB::Destroy(NB); });

    if(m_NBL)
    {
        auto NBL = DetachInternalObject();
        NETKVM_ASSERT(NET_BUFFER_LIST_NEXT_NBL(NBL) == nullptr);
        if (CallCompletionForNBL(m_Context, NBL))
        {
            m_ParentTXPath->CompleteOutstandingNBLChain(NBL);
        } else
        {
            m_ParentTXPath->CompleteOutstandingInternalNBL(NBL);
        }
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
        DPrintf(0, "[%s] Discarded invalid priority tag %p\n", __FUNCTION__, priorityInfo.Value);
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
            DPrintf(1, "[%s] Populated priority tag %p\n", __FUNCTION__, priorityInfo.Value);
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
    UNREFERENCED_PARAMETER(NB);
    if (m_BuffersNumber == (ULONG)m_BuffersMapped.AddRef())
    {
        m_ParentTXPath->NBLMappingDone(this);
    }
}

bool CNBL::ParseBuffers()
{
    m_MaxDataLength = 0;
    CAllocationHelper<CNB> *pNBAllocator = this;

    for (auto NB = NET_BUFFER_LIST_FIRST_NB(m_NBL); NB != nullptr; NB = NET_BUFFER_NEXT_NB(NB))
    {
        CNB *NBHolder = new (pNBAllocator) CNB(NB, this, m_Context, pNBAllocator);
        pNBAllocator = m_NBAllocator;
        if(!NBHolder || !NBHolder->IsValid())
        {
            return false;
        }
        RegisterNB(NBHolder);
        m_MaxDataLength = max(m_MaxDataLength, NBHolder->GetDataLength());
    }

    if(m_MaxDataLength == 0)
    {
        DPrintf(0, "[%s] - Empty NBL (%p) dropped\n", __FUNCTION__, m_NBL);
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
    NETKVM_ASSERT(IsLSO());

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
    NETKVM_ASSERT(IsClass());
    UNREFERENCED_PARAMETER(IsClass);

    if (IsOffload())
    {
        if(!IsSupported())
        {
            DPrintf(0, "[%s] %s request when it is not supported\n", __FUNCTION__, OffloadName);
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

    m_Buffers.ForEach([this](CNB *NB)
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
    Destroy(this);
}

CParaNdisTX::~CParaNdisTX()
{

    TPassiveSpinLocker LockedContext(m_Lock);
    CNBL* NBL = nullptr;

    NBL = m_SendQueue.Dequeue();

    while (NBL)
    {
        NBL->~CNBL();
        NBL = m_SendQueue.Dequeue();
    }

    m_SendQueue.~CLockFreeDynamicQueue();

    DPrintf(1, "Pools state %d-> NB: %d, NBL: %d\n", m_queueIndex, m_nbPool.GetCount(), m_nblPool.GetCount());
    if (m_StateMachineRegistered)
    {
        m_Context->m_StateMachine.UnregisterFlow(m_StateMachine);
        m_StateMachineRegistered = false;
    }
}

bool CParaNdisTX::Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex)
{
    m_Context = Context;
    m_queueIndex = (u16)DeviceQueueIndex;

    Context->m_StateMachine.RegisterFlow(m_StateMachine);
    m_StateMachineRegistered = true;

    m_nbPool.Create(Context->MiniportHandle);
    m_nblPool.Create(Context->MiniportHandle);

    CreatePath();

    return m_VirtQueue.Create(DeviceQueueIndex,
        &m_Context->IODevice,
        m_Context->MiniportHandle,
        m_Context->maxFreeTxDescriptors,
        m_Context->nVirtioHeaderSize,
        m_Context) &&
        m_SendQueue.Create(Context, m_SendQueue.IsPowerOfTwo(m_Context->maxFreeTxDescriptors) ?
            8 * m_Context->maxFreeTxDescriptors : PARANDIS_TX_LOCK_FREE_QUEUE_DEFAULT_SIZE);
}

void CParaNdisTX::CompleteOutstandingNBLChain(PNET_BUFFER_LIST NBL, ULONG Flags)
{
    ULONG NBLNum = ParaNdis_CountNBLs(NBL);

    ParaNdis_CompleteNBLChain(m_Context->MiniportHandle, NBL, Flags);

    m_StateMachine.UnregisterOutstandingItems(NBLNum);
}

void CParaNdisTX::CompleteOutstandingInternalNBL(PNET_BUFFER_LIST NBL, BOOLEAN UnregisterOutstanding /*= TRUE*/)
{
    ULONG NBLNum = ParaNdis_CountNBLs(NBL);

    CGuestAnnouncePackets::NblCompletionCallback(NBL);

    if (UnregisterOutstanding)
    {
        m_StateMachine.UnregisterOutstandingItems(NBLNum);
    }
}

void CParaNdisTX::Send(PNET_BUFFER_LIST NBL)
{
    PNET_BUFFER_LIST nextNBL = nullptr;
    NDIS_STATUS RejectionStatus = NDIS_STATUS_FAILURE;
    BOOLEAN CallCompletion = CallCompletionForNBL(m_Context, NBL);

    if (!m_StateMachine.RegisterOutstandingItems(ParaNdis_CountNBLs(NBL), &RejectionStatus))
    {
        if (CallCompletion)
        {
            ParaNdis_CompleteNBLChainWithStatus(m_Context->MiniportHandle, NBL, RejectionStatus);
        }
        else
        {
            CompleteOutstandingInternalNBL(NBL, FALSE);
        }
        return;
    }

    for(auto currNBL = NBL; currNBL != nullptr; currNBL = nextNBL)
    {
        nextNBL = NET_BUFFER_LIST_NEXT_NBL(currNBL);
        NET_BUFFER_LIST_NEXT_NBL(currNBL) = nullptr;

        auto NBLHolder = new (&m_nblPool) CNBL(currNBL, m_Context, *this, &m_nblPool, &m_nbPool);

        if (NBLHolder == nullptr)
        {
            currNBL->Status = NDIS_STATUS_RESOURCES;
            if (CallCompletion)
            {
                CompleteOutstandingNBLChain(currNBL);
            }
            else
            {
                CompleteOutstandingInternalNBL(NBL);
            }
            DPrintf(0, "ERROR: Failed to allocate CNBL instance\n");
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
    NETKVM_ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    if (NBLHolder->MappingSucceeded() && m_VirtQueue.Alive())
    {
        m_SendQueue.Enqueue(NBLHolder);

        if (m_DpcWaiting == 0)
        {
            DoPendingTasks(NBLHolder);
        }
    }
    else
    {
        NBLHolder->SetStatus(NDIS_STATUS_FAILURE);
        NBLHolder->Release();
    }
}

CNB *CNBL::PopMappedNB()
{
    m_MappedBuffersDetached.AddRef();
    return m_Buffers.Pop();
}
void CNBL::PushMappedNB(CNB *NB)
{
    m_MappedBuffersDetached.Release();
    m_Buffers.Push(NB);
}

void CNBL::NBComplete()
{
    m_BuffersDone.AddRef();
    m_MappedBuffersDetached.Release();
}

bool CNBL::IsSendDone()
{
    return (LONG)m_BuffersDone == (LONG)m_BuffersNumber && !HaveDetachedBuffers();
}

PNET_BUFFER_LIST CNBL::DetachInternalObject()
{
    m_Buffers.ForEach([this](CNB *NB)
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
    ParaNdis_DebugNBLOut(m_LogIndex, Res);
    m_NBL = nullptr;
    return Res;
}

PNET_BUFFER_LIST CParaNdisTX::ProcessWaitingList(CRawCNBLList& completed)
{
    PNET_BUFFER_LIST CompletedNBLs = nullptr;

    // locked part under waiting list lock
    {
        TDPCSpinLocker LockedContext(m_WaitingListLock);

        completed.ForEachDetachedIf([](CNBL* NBL) { return !NBL->IsSendDone(); },
            [&](CNBL* NBL)
        {
            m_WaitingList.PushBack(NBL);
        });

        m_WaitingList.ForEachDetachedIf([](CNBL* NBL) { return NBL->IsSendDone(); },
            [&](CNBL* NBL)
        {
            completed.PushBack(NBL);
        });
    }
    // end of locked part under waiting list lock

    completed.ForEachDetached([&](CNBL* NBL)
    {
        NBL->SetStatus(NDIS_STATUS_SUCCESS);
        auto RawNBL = NBL->DetachInternalObject();
        NBL->Release();
        if (CallCompletionForNBL(m_Context, RawNBL))
        {
            NET_BUFFER_LIST_NEXT_NBL(RawNBL) = CompletedNBLs;
            CompletedNBLs = RawNBL;
        }
        else
        {
            CompleteOutstandingInternalNBL(RawNBL);
        }
    });

    return CompletedNBLs;
}

void CParaNdisTX::Notify(SMNotifications message)
{
    CRawCNBList  nbToFree;
    CRawCNBLList completedNBLs;

    __super::Notify(message);

    if (message != SupriseRemoved)
    {
        // probably similar processing we'll do in case of reset
        return;
    }

    CDpcIrqlRaiser OnDpc;

    // pause follows after we return all the NBLs
    DoWithTXLock([&]()
    {
        //
        m_VirtQueue.ProcessTXCompletions(nbToFree, true);

        while (HaveMappedNBLs())
        {
            CNBL *nbl = PopMappedNBL();
            nbl->SetStatus(NDIS_STATUS_SEND_ABORTED);
            completedNBLs.Push(nbl);
            while (nbl->HaveMappedBuffers())
            {
                CNB *nb = nbl->PopMappedNB();
                CNB::Destroy(nb);
                nbl->NBComplete();
            }
        }
    });

    PostProcessPendingTask(nbToFree, completedNBLs);
}

PNET_BUFFER_LIST CParaNdisTX::BuildCancelList(PVOID CancelId)
{
    PNET_BUFFER_LIST CanceledNBLs = nullptr;
    TPassiveSpinLocker LockedContext(m_Lock);
    CNBL* NBL = nullptr;

    NBL = PopMappedNBL();
    do
    {
        if (NBL->MatchCancelID(CancelId) && !NBL->HaveDetachedBuffers())
        {
            NBL->SetStatus(NDIS_STATUS_SEND_ABORTED);
            auto RawNBL = NBL->DetachInternalObject();
            NBL->Release();
            NET_BUFFER_LIST_NEXT_NBL(RawNBL) = CanceledNBLs;
            CanceledNBLs = RawNBL;
        }
        NBL = PopMappedNBL();
    } while (NBL);

    return CanceledNBLs;
}

void CParaNdisTX::CancelNBLs(PVOID CancelId)
{
    auto CanceledNBLs = BuildCancelList(CancelId);
    if (CanceledNBLs != nullptr)
    {
        CompleteOutstandingNBLChain(CanceledNBLs);
    }
}

//called with TX lock held
bool CParaNdisTX::RestartQueue()
{
    auto res = ParaNdis_SynchronizeWithInterrupt(m_Context,
                                                 m_messageIndex,
                                                 RestartQueueSynchronously,
                                                 this) ? true : false;
    return res;
}

//called with TX lock held
//returns queue restart status
bool CParaNdisTX::SendMapped(bool IsInterrupt, CRawCNBLList& toWaitingList)
{
    bool SentOutSomeBuffers = false;
    bool bRestartStatus = false;
    bool HaveBuffers = true;

    if(ParaNdis_IsSendPossible(m_Context))
    {

        while (HaveBuffers && HaveMappedNBLs())
        {
            auto NBLHolder = PeekMappedNBL();

            if (NBLHolder->HaveMappedBuffers())
            {
                auto NBHolder = NBLHolder->PopMappedNB();
                auto result = m_VirtQueue.SubmitPacket(*NBHolder);

                switch (result)
                {
                case SUBMIT_NO_PLACE_IN_QUEUE:
                    NBLHolder->PushMappedNB(NBHolder);
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
                        /* We use PeekMappedNBL method to get the current NBL
                         * that should be processed from the queue, when we finish
                         * sending all it's NBs, we should pop it from the queue.
                         */
                        PopMappedNBL();
                        toWaitingList.Push(NBLHolder);
                    }
                    else
                    {
                        // no, keep it in the queue
                    }

                    if (result == SUBMIT_SUCCESS)
                    {
                        SentOutSomeBuffers = true;
                    }
                    else
                    {
                        CNB::Destroy(NBHolder);
                        NBLHolder->NBComplete();
                    }
                    break;
                default:
                    NETKVM_ASSERT(false);
                    break;
                }
            }
            else
            {
                NETKVM_ASSERT(false);
            }
        }
    }

    if (IsInterrupt)
    {
        bRestartStatus = RestartQueue();
    }

    if (SentOutSomeBuffers || !HaveBuffers)
    {
        m_VirtQueue.Kick();
    }

    return bRestartStatus;
}

void CParaNdisTX::PostProcessPendingTask(
    CRawCNBList& nbToFree,
    CRawCNBLList& completedNBLs)
{
    PNET_BUFFER_LIST pNBLReturnNow = nullptr;
    if (!nbToFree.IsEmpty() || !completedNBLs.IsEmpty())
    {
        nbToFree.ForEachDetached([](CNB *NB)
        {
            CNBL *NBL = NB->GetParentNBL();
            CNB::Destroy(NB);
            NBL->NBComplete();
        }
        );

        pNBLReturnNow = ProcessWaitingList(completedNBLs);
        if (pNBLReturnNow)
        {
            CompleteOutstandingNBLChain(pNBLReturnNow, NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);
        }
    }
}

bool CParaNdisTX::DoPendingTasks(CNBL *nblHolder)
{
    bool bRestartQueueStatus = false;
    bool bFromDpc = nblHolder == nullptr;
    CRawCNBList  nbToFree;
    CRawCNBLList completedNBLs;

    if (bFromDpc)
    {
        m_DpcWaiting.AddRef();
    }

    DoWithTXLock([&]()
    {
        m_VirtQueue.ProcessTXCompletions(nbToFree);

        if (bFromDpc)
        {
            m_DpcWaiting.Release();
        }

        if (bFromDpc || 0 == (LONG)m_DpcWaiting)
        {
            bRestartQueueStatus = SendMapped(bFromDpc, completedNBLs);
            if (bRestartQueueStatus)
            {
                // we can enter here only when we called from DPC
                // if we can't enable interrupts on queue right now,
                // we can retrieve completed packets and try again
                m_VirtQueue.ProcessTXCompletions(nbToFree);
                bRestartQueueStatus = SendMapped(true, completedNBLs);
            }
        } else
        {
            // the call initiated by Send(), we can give up
            // and let pending DPC do the job instead of wait
        }
    });

    PostProcessPendingTask(nbToFree, completedNBLs);

    return bRestartQueueStatus;
}

void CNB::MappingDone(PSCATTER_GATHER_LIST SGL)
{
    m_SGL = SGL;
    m_ParentNBL->RegisterMappedNB(this);
}

CNB::~CNB()
{
    NETKVM_ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

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
    NETKVM_ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

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
        DPrintf(0, "[%s] ERROR: Bad version of IP header!\n", __FUNCTION__);
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
        DPrintf(0, "[%s] ERROR: NOT an IP packet - expected troubles!\n", __FUNCTION__);
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
        HeadersLength = Copy(Destination, MaxSize);
        L4HeaderOffset = QueryL4HeaderOffset(Destination, m_Context->Offload.ipHeaderOffset);
    }
    else if (m_ParentNBL->IsIPHdrCSO())
    {
        HeadersLength = Copy(Destination, MaxSize);
        L4HeaderOffset = QueryL4HeaderOffset(Destination, m_Context->Offload.ipHeaderOffset);
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

ULONG CNB::Copy(PVOID Dst, ULONG Length) const
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
        NdisQueryMdl(CurrMDL, &CurrAddr, &CurrLen, MM_PAGE_PRIORITY(LowPagePriority | MdlMappingNoExecute));
#else
        NdisQueryMdl(CurrMDL, &CurrAddr, &CurrLen, MM_PAGE_PRIORITY(LowPagePriority));
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

    return Copied;
}
