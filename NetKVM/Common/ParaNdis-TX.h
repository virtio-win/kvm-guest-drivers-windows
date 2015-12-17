#pragma once
#include "ParaNdis-VirtQueue.h"
#include "ParaNdis-AbstractPath.h"

class CNB;
class CParaNdisTX;

typedef struct _tagPARANDIS_ADAPTER *PPARANDIS_ADAPTER;
class CNBL;

typedef CNdisAllocatable<CNBL, 'LNHR'> CNBLAllocator;

class CNBL : public CNBLAllocator, public CRefCountingObject
{
public:
    CNBL(PNET_BUFFER_LIST NBL, PPARANDIS_ADAPTER Context, CParaNdisTX &ParentTXPath);
    ~CNBL();
    bool Prepare()
    { return (ParsePriority() && ParseBuffers() && ParseOffloads()); }
    void StartMapping();
    void RegisterMappedNB(CNB *NB);
    bool MappingSuceeded() { return !m_HaveFailedMappings; }
    void SetStatus(NDIS_STATUS Status)
    { m_NBL->Status = Status; }

    CNB *PopMappedNB();
    void PushMappedNB(CNB *NBHolder);
    bool HaveMappedBuffers()
    { return !m_MappedBuffers.IsEmpty(); }

    bool HaveDetachedBuffers()
    { return m_MappedBuffersDetached != 0; }

    //TODO: Needs review
    void CompleteMappedBuffers();

    PNET_BUFFER_LIST DetachInternalObject();
    //TODO: Needs review
    void NBComplete();
    bool IsSendDone();

    UCHAR ProtocolID()
    {
        #pragma warning(suppress: 4302)
        return reinterpret_cast<UCHAR>(NET_BUFFER_LIST_INFO(m_NBL, NetBufferListProtocolId));
    }
    bool MatchCancelID(PVOID ID)
    { return NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(m_NBL) == ID; }
    ULONG MSS()
    { return m_LsoInfo.LsoV2Transmit.MSS; }
    ULONG TCPHeaderOffset()
    { return IsLSO() ? LsoTcpHeaderOffset() : CsoTcpHeaderOffset(); }
    UINT16 TCI()
    { return m_TCI; }
    bool IsLSO()
    { return (m_LsoInfo.Value != nullptr); }
    bool IsTcpCSO()
    { return m_CsoInfo.Transmit.TcpChecksum; }
    bool IsUdpCSO()
    { return m_CsoInfo.Transmit.UdpChecksum; }
    bool IsIPHdrCSO()
    { return m_CsoInfo.Transmit.IpHeaderChecksum; }
    void UpdateLSOTxStats(ULONG ChunkSize)
    {
        if (m_LsoInfo.LsoV1TransmitComplete.Type == NDIS_TCP_LARGE_SEND_OFFLOAD_V1_TYPE)
        {
            m_TransferSize += ChunkSize;
        }
    }

private:
    virtual void OnLastReferenceGone() override;

    static void Destroy(CNBL *ptr, NDIS_HANDLE MiniportHandle)
    { CNBLAllocator::Destroy(ptr, MiniportHandle); }

    void RegisterNB(CNB *NB);
    bool ParsePriority();
    bool ParseBuffers();
    bool ParseOffloads();
    bool ParseLSO();
    bool NeedsLSO();
    ULONG LsoTcpHeaderOffset()
    { return m_LsoInfo.LsoV2Transmit.TcpHeaderOffset; }
    ULONG CsoTcpHeaderOffset()
    { return m_CsoInfo.Transmit.TcpHeaderOffset; }
    bool FitsLSO();
    bool IsIP4CSO()
    { return m_CsoInfo.Transmit.IsIPv4; }
    bool IsIP6CSO()
    { return m_CsoInfo.Transmit.IsIPv6; }

    template <typename TClassPred, typename TOffloadPred, typename TSupportedPred>
    bool ParseCSO(TClassPred IsClass, TOffloadPred IsOffload,
                  TSupportedPred IsSupported, LPSTR OffloadName);

    PNET_BUFFER_LIST m_NBL;
    PPARANDIS_ADAPTER m_Context;
    CParaNdisTX *m_ParentTXPath;
    bool m_HaveFailedMappings = false;

    CNdisList<CNB, CRawAccess, CNonCountingObject> m_Buffers;

    ULONG m_BuffersNumber = 0;
    CNdisList<CNB, CLockedAccess, CCountingObject> m_MappedBuffers;

    ULONG m_MappedBuffersDetached = 0;
    //TODO: Needs review
    ULONG m_BuffersDone = 0;

    ULONG m_MaxDataLength = 0;
    ULONG m_TransferSize = 0;

    UINT16 m_TCI = 0;

    NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO m_LsoInfo;
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO m_CsoInfo;

    CNBL(const CNBL&) = delete;
    CNBL& operator= (const CNBL&) = delete;

    DECLARE_CNDISLIST_ENTRY(CNBL);
};

class CNB : public CNdisAllocatable<CNB, 'BNHR'>
{
public:
    CNB(PNET_BUFFER NB, CNBL *ParentNBL, PPARANDIS_ADAPTER Context)
        : m_NB(NB)
        , m_ParentNBL(ParentNBL)
        , m_Context(Context)
    { }

    ~CNB();

    bool IsValid() const
    { return (GetDataLength() != 0); }

    ULONG GetDataLength() const
    { return NET_BUFFER_DATA_LENGTH(m_NB); }

    bool ScheduleBuildSGListForTx();

    void MappingDone(PSCATTER_GATHER_LIST SGL);
    void ReleaseResources();

    CNBL *GetParentNBL() const
    { return m_ParentNBL; }

    ULONG GetSGLLength() const
    { return m_SGL->NumberOfElements; }

    //TODO: Needs review
    void SendComplete()
    {
        m_ParentNBL->NBComplete();
    }

    bool BindToDescriptor(CTXDescriptor &Descriptor);
private:
    bool Copy(PVOID Dst, ULONG Length) const;
    bool CopyHeaders(PVOID Destination, ULONG MaxSize, ULONG &HeadersLength, ULONG &L4HeaderOffset) const;
    void BuildPriorityHeader(PETH_HEADER EthHeader, PVLAN_HEADER VlanHeader) const;
    void PrepareOffloads(virtio_net_hdr *VirtioHeader, PVOID IpHeader, ULONG EthPayloadLength, ULONG L4HeaderOffset) const;
    void SetupLSO(virtio_net_hdr *VirtioHeader, PVOID IpHeader, ULONG EthPayloadLength) const;
    USHORT QueryL4HeaderOffset(PVOID PacketData, ULONG IpHeaderOffset) const;
    void DoIPHdrCSO(PVOID EthHeaders, ULONG HeadersLength) const;
    void SetupCSO(virtio_net_hdr *VirtioHeader, ULONG L4HeaderOffset) const;
    bool FillDescriptorSGList(CTXDescriptor &Descriptor, ULONG DataOffset) const;
    bool MapDataToVirtioSGL(CTXDescriptor &Descriptor, ULONG Offset) const;
    void PopulateIPLength(IPHeader *IpHeader, USHORT IpLength) const;

    PNET_BUFFER m_NB;
    CNBL *m_ParentNBL;
    PPARANDIS_ADAPTER m_Context;
    PSCATTER_GATHER_LIST m_SGL = nullptr;

    CNB(const CNB&) = delete;
    CNB& operator= (const CNB&) = delete;

    DECLARE_CNDISLIST_ENTRY(CNB);
};

typedef struct _tagSynchronizedContext tSynchronizedContext;

class CParaNdisTX : public CParaNdisTemplatePath<CTXVirtQueue>, public CNdisAllocatable<CParaNdisTX, 'XTHR'>
{
public:
    CParaNdisTX();

    bool Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex);

    void Send(PNET_BUFFER_LIST pNBL);

    void NBLMappingDone(CNBL *NBLHolder);

    template <typename TFunctor>
    void DoWithTXLock(TFunctor Functor)
    {
        TSpinLocker LockedContext(m_Lock);
        Functor();
    }

    //TODO: Needs review
    bool Pause();
    //TODO: Needs review
    void CancelNBLs(PVOID CancelId);

    //TODO: Temporary!!!
    void Kick()
    { m_VirtQueue.Kick(); }

    //TODO: Requires review
    bool RestartQueue(bool DoKick);

    //TODO: Requires review
    static BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) RestartQueueSynchronously(tSynchronizedContext *ctx);

    //TODO: Needs review/temporary?
    ULONG GetFreeTXDescriptors()
    { return m_VirtQueue.GetFreeTXDescriptors(); }

    //TODO: Needs review/temporary?
    ULONG GetFreeHWBuffers()
    { return m_VirtQueue.GetFreeHWBuffers(); }

    //TODO: Needs review
    bool DoPendingTasks(bool IsInterrupt);

    bool QueueHasPacketInHW()
    {
        return m_VirtQueue.HasPacketsInHW();
    }

private:

    //TODO: Needs review
    bool SendMapped(bool IsInterrupt, PNET_BUFFER_LIST &NBLFailNow);

    PNET_BUFFER_LIST ProcessWaitingList();
    PNET_BUFFER_LIST BuildCancelList(PVOID CancelId);

    //TODO: Needs review
    PNET_BUFFER_LIST RemoveAllNonWaitingNBLs();

    bool HaveMappedNBLs() { return !m_SendList.IsEmpty(); }
    CNBL *PopMappedNBL() { return m_SendList.Pop(); }
    void PushMappedNBL(CNBL *NBLHolder) { m_SendList.Push(NBLHolder); }

    CNdisList<CNBL, CRawAccess, CNonCountingObject> m_SendList;
    CNdisList<CNBL, CRawAccess, CNonCountingObject> m_WaitingList;
};
