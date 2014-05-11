#include "ndis56common.h"

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
            SetPriorityData(m_PriorityData, priorityInfo.TagHeader.UserPriority, priorityInfo.TagHeader.VlanId);
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

CParaNdisTX::CParaNdisTX(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex)
    : m_Context(Context)
    , m_VirtQueue(DeviceQueueIndex,
                  m_Context->IODevice,
                  m_Context->MiniportHandle,
                  m_Context->bDoPublishIndices ? true : false,
                  m_Context->maxFreeTxDescriptors,
                  m_Context->nVirtioHeaderSize,
                  m_Context->MaxPacketSize.nMaxFullSizeHwTx,
                  m_Context)
{ }

void CParaNdisTX::Send(PNET_BUFFER_LIST NBL)
{
    PNET_BUFFER_LIST nextNBL = nullptr;

    for(auto currNBL = NBL; currNBL != nullptr; currNBL = nextNBL)
    {
        m_NetTxPacketsToReturn.AddRef();

        nextNBL = NET_BUFFER_LIST_NEXT_NBL(currNBL);
        NET_BUFFER_LIST_NEXT_NBL(currNBL) = nullptr;

        auto NBLHolder = new (m_Context->MiniportHandle) CNBL(currNBL, m_Context, *this);

        if (NBLHolder == nullptr)
        {
            CNBL OnStack(currNBL, m_Context, *this);
            OnStack.SetStatus(NDIS_STATUS_RESOURCES);
            m_NetTxPacketsToReturn.Release();
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
            m_NetTxPacketsToReturn.Release();
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
        m_NetTxPacketsToReturn.Release();
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

//TODO: This function makes intermediate parameters copying that is redundant
//To be dropped in favor of direct CNBL interface acess
void CParaNdisTX::InitializeTransferParameters(CNB *NB, tTxOperationParameters *pParams)
{
    auto NBL = NB->GetParentNBL();

    pParams->NB = NB;
    pParams->ulDataSize = NB->GetDataLength();
    pParams->offloalMss = NBL->MSS();
    pParams->tcpHeaderOffset = NBL->TCPHeaderOffset();
    pParams->flags = NBL->IsLSO() ? pcrLSO : 0;
    /*
    NdisQueryNetBufferPhysicalCount(pnbe->netBuffer)
    may give wrong number of fragment, bigger due to current offset
    */
    pParams->nofSGFragments = NB->GetSGLLength();
    //if (pnbe->pSGList) PrintMDLChain(pParams->packet, pnbe->pSGList);
    if (NBL->ProtocolID() == NDIS_PROTOCOL_ID_TCP_IP)
    {
        pParams->flags |= pcrIsIP;
    }
    if (NBL->PriorityDataPacked())
    {
        pParams->flags |= pcrPriorityTag;
    }
    if (NBL->IsTcpCSO())
    {
        pParams->flags |= pcrTcpChecksum;
    }
    if (NBL->IsUdpCSO())
    {
        pParams->flags |= pcrUdpChecksum;
    }
    if (NBL->IsIPHdrCSO())
    {
        pParams->flags |= pcrIpChecksum;
    }
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
                                                 m_Context->ulTxMessage,
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
                tTxOperationParameters Params;
                InitializeTransferParameters(NBHolder, &Params);
                auto result = m_VirtQueue.DoSubmitPacket(&Params);

                switch (result.error)
                {
                case cpeNoBuffer:
                case cpeNoIndirect:
                    NBLHolder->PushMappedNB(NBHolder);
                    PushMappedNBL(NBLHolder);
                    HaveBuffers = false;
                    // break the loop, allow to kick and free some buffers
                    break;

                case cpeInternalError:
                case cpeOK:
                case cpeTooLarge:
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

                    if (result.error == cpeOK)
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

                    if (!m_VirtQueue.HasPacketsInHW() && m_Context->SendState == srsPausing)
                    {
                        CallbackToCall = m_Context->SendPauseCompletionProc;
                        m_Context->SendPauseCompletionProc = nullptr;
                        m_Context->SendState = srsDisabled;
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

bool CNB::ScheduleBuildSGListForTx()
{
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    return NdisMAllocateNetBufferSGList(m_Context->DmaHandle, m_NB, this,
                                        NDIS_SG_LIST_WRITE_TO_DEVICE, nullptr, 0) == NDIS_STATUS_SUCCESS;
}

//TODO: Temporary, needs review
tCopyPacketResult CNB::PacketCopier(PVOID dest, ULONG maxSize, BOOLEAN bPreview)
{
    tCopyPacketResult result;
    ULONG PriorityDataLong = 0;
    ULONG nCopied = 0;
    ULONG ulOffset = NET_BUFFER_CURRENT_MDL_OFFSET(m_NB);
    ULONG nToCopy = NET_BUFFER_DATA_LENGTH(m_NB);
    PMDL  pMDL = NET_BUFFER_CURRENT_MDL(m_NB);
    result.error = cpeOK;
    if (!bPreview) PriorityDataLong = m_ParentNBL->PriorityDataPacked();
    if (nToCopy > maxSize) nToCopy = bPreview ? maxSize : 0;

    while (pMDL && nToCopy)
    {
        ULONG len;
        PVOID addr;
        NdisQueryMdl(pMDL, &addr, &len, NormalPagePriority);
        if (addr && len)
        {
            // total to copy from this MDL
            len -= ulOffset;
            if (len > nToCopy) len = nToCopy;
            nToCopy -= len;
            if ((PriorityDataLong & 0xFFFF) &&
                nCopied < ETH_PRIORITY_HEADER_OFFSET &&
                (nCopied + len) >= ETH_PRIORITY_HEADER_OFFSET)
            {
                ULONG nCopyNow = ETH_PRIORITY_HEADER_OFFSET - nCopied;
                NdisMoveMemory(dest, (PCHAR)addr + ulOffset, nCopyNow);
                dest = (PCHAR)dest + nCopyNow;
                addr = (PCHAR)addr + nCopyNow;
                NdisMoveMemory(dest, &PriorityDataLong, ETH_PRIORITY_HEADER_SIZE);
                nCopied += ETH_PRIORITY_HEADER_SIZE;
                dest = (PCHAR)dest + ETH_PRIORITY_HEADER_SIZE;
                nCopyNow = len - nCopyNow;
                if (nCopyNow) NdisMoveMemory(dest, (PCHAR)addr + ulOffset, nCopyNow);
                dest = (PCHAR)dest + nCopyNow;
                ulOffset = 0;
                nCopied += len;
            }
            else
            {
                NdisMoveMemory(dest, (PCHAR)addr + ulOffset, len);
                dest = (PCHAR)dest + len;
                ulOffset = 0;
                nCopied += len;
            }
        }
        pMDL = pMDL->Next;
    }

    DEBUG_EXIT_STATUS(4, nCopied);
    result.size = nCopied;
    return result;
}
