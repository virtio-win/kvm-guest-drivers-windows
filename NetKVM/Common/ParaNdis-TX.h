#pragma once
#include "ParaNdis-VirtQueue.h"
#include "ParaNdis-AbstractPath.h"
#include "ParaNdis_GuestAnnounce.h"
#include "ParaNdis_LockFreeQueue.h"

/* Must be a power of 2 */
#define PARANDIS_TX_LOCK_FREE_QUEUE_DEFAULT_SIZE 2048

class CNB;
class CParaNdisTX;

class CNBL;

class CNB : public CNdisAllocatableViaHelper<CNB>
{
public:
    CNB(PNET_BUFFER NB, CNBL *ParentNBL, PPARANDIS_ADAPTER Context, CAllocationHelper<CNB> *Allocator)
        : m_NB(NB)
        , m_ParentNBL(ParentNBL)
        , m_Context(Context)
        , CNdisAllocatableViaHelper<CNB>(Allocator)
    { }

    ~CNB();

    bool IsValid() const
    {
        return (GetDataLength() != 0);
    }

    ULONG GetDataLength() const
    {
        return NET_BUFFER_DATA_LENGTH(m_NB);
    }

    bool ScheduleBuildSGListForTx();

    void MappingDone(PSCATTER_GATHER_LIST SGL);
    void ReleaseResources();

    CNBL *GetParentNBL() const
    {
        return m_ParentNBL;
    }

    ULONG GetSGLLength() const
    {
        return m_SGL->NumberOfElements;
    }

    bool BindToDescriptor(CTXDescriptor &Descriptor);
private:
    ULONG Copy(PVOID Dst, ULONG Length) const;
    bool CopyHeaders(PVOID Destination, ULONG MaxSize, ULONG &HeadersLength, ULONG &L4HeaderOffset) const;
    void BuildPriorityHeader(PETH_HEADER EthHeader, PVLAN_HEADER VlanHeader) const;
    void PrepareOffloads(virtio_net_hdr *VirtioHeader, PVOID IpHeader, ULONG EthPayloadLength, ULONG L4HeaderOffset) const;
    void SetupLSO(virtio_net_hdr *VirtioHeader, PVOID IpHeader, ULONG EthPayloadLength) const;
    void SetupUSO(virtio_net_hdr *VirtioHeader, PVOID IpHeader, ULONG EthPayloadLength) const;
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

class CNBL : public CNdisAllocatableViaHelper<CNBL>,
             public CRefCountingObject,
             public CAllocationHelper<CNB>
{
public:
    CNBL(PNET_BUFFER_LIST NBL, PPARANDIS_ADAPTER Context, CParaNdisTX &ParentTXPath, CAllocationHelper<CNBL> *NBLAllocator, CAllocationHelper<CNB> *NBAllocator);
    ~CNBL();

    /* CAllocationHelper<CNB> */
    CNB *Allocate() override
    {
        return (CNB *)&m_CNB_Storage;
    }
    void Deallocate(CNB *ptr) override
    {
        UNREFERENCED_PARAMETER(ptr);
    }

    bool Prepare()
    { return (ParsePriority() && ParseBuffers() && ParseOffloads()); }
    void StartMapping();
    void RegisterMappedNB(CNB *NB);
    bool MappingSucceeded() { return !m_HaveFailedMappings; }
    void SetStatus(NDIS_STATUS Status)
    { m_NBL->Status = Status; }

    // called under m_Lock of parent TX path for CNBL object in Send list
    CNB *PopMappedNB();
    // called under m_Lock of parent TX path for CNBL object in Send list
    void PushMappedNB(CNB *NBHolder);
    // called under m_Lock of parent TX path for CNBL object in Send list
    bool HaveMappedBuffers()
    { return !m_Buffers.IsEmpty(); }

    bool HaveDetachedBuffers()
    { return m_MappedBuffersDetached != 0; }

    PNET_BUFFER_LIST DetachInternalObject();
    //TODO: Needs review
    void NBComplete();
    bool IsSendDone();

    UCHAR ProtocolID()
    {
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
#if PARANDIS_SUPPORT_USO
    bool IsUSO()
    { return (m_UsoInfo.Value != nullptr); }
    ULONG UsoMSS()
    { return m_UsoInfo.Transmit.MSS; }
    ULONG UsoHeaderOffset()
    { return m_UsoInfo.Transmit.UdpHeaderOffset; }
#else
    bool IsUSO() { return false; }
    ULONG UsoMSS() { return 0; }
    ULONG UsoHeaderOffset() { return 0; }
#endif
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

    void RegisterNB(CNB *NB);
    bool ParsePriority();
    bool ParseBuffers();
    bool ParseOffloads();
    bool ParseLSO();
    bool ParseUSO();
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
    // align storage for CNB on pointer size boundary and provide enough room for it
    ULONG_PTR m_CNB_Storage[(sizeof(CNB) + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR)];
    bool m_HaveFailedMappings = false;

    CNdisList<CNB, CRawAccess, CNonCountingObject> m_Buffers;

    ULONG m_BuffersNumber = 0;
    CNdisRefCounter m_BuffersMapped;
    CNdisRefCounter m_MappedBuffersDetached;
    CNdisRefCounter m_BuffersDone;

    ULONG m_MaxDataLength = 0;
    ULONG m_TransferSize = 0;
    ULONG  m_LogIndex;

    UINT16 m_TCI = 0;

    NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO m_LsoInfo;
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO m_CsoInfo;
#if PARANDIS_SUPPORT_USO
    NDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO m_UsoInfo;
#endif
    CAllocationHelper<CNB> *m_NBAllocator;

    CNBL(const CNBL&) = delete;
    CNBL& operator= (const CNBL&) = delete;

    DECLARE_CNDISLIST_ENTRY(CNBL);
};

typedef CNdisList<CNBL, CRawAccess, CNonCountingObject> CRawCNBLList;

typedef CLockFreeDynamicQueue<CNBL> CLockFreeCNBLQueue;

class CParaNdisTX : public CParaNdisTemplatePath<CTXVirtQueue>, public CNdisAllocatable<CParaNdisTX, 'XTHR'>
{
public:
    CParaNdisTX() = default;
    ~CParaNdisTX();

    bool Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex);

    void Send(PNET_BUFFER_LIST pNBL);

    void NBLMappingDone(CNBL *NBLHolder);

    template <typename TFunctor>
    void DoWithTXLock(TFunctor Functor)
    {
        TDPCSpinLocker LockedContext(m_Lock);
        Functor();
    }

    //TODO: Needs review
    void CancelNBLs(PVOID CancelId);

    bool RestartQueue();

    //TODO: Needs review/temporary?
    ULONG GetFreeTXDescriptors()
    { return m_VirtQueue.GetFreeTXDescriptors(); }

    //TODO: Needs review/temporary?
    ULONG GetFreeHWBuffers()
    { return m_VirtQueue.GetFreeHWBuffers(); }

    bool DoPendingTasks(CNBL *nblHolder);

    void CompleteOutstandingNBLChain(PNET_BUFFER_LIST NBL, ULONG Flags = 0);
    void CompleteOutstandingInternalNBL(PNET_BUFFER_LIST NBL, BOOLEAN UnregisterOutstanding = TRUE);
private:

    virtual void Notify(SMNotifications message) override;

    bool SendMapped(bool IsInterrupt, CRawCNBLList& toWaitingList);

    bool FillQueue();

    void PostProcessPendingTask(CRawCNBList& toFree, CRawCNBLList& completed);
    PNET_BUFFER_LIST ProcessWaitingList(CRawCNBLList& completed);
    PNET_BUFFER_LIST BuildCancelList(PVOID CancelId);

    bool HaveMappedNBLs() { return !m_SendQueue.IsEmpty(); }

    CNBL *PopMappedNBL() { return m_SendQueue.Dequeue(); }
    CNBL *PeekMappedNBL() { return m_SendQueue.Peek(); }
    void PushMappedNBL(CNBL *NBLHolder) { m_SendQueue.Enqueue(NBLHolder); }

    CDataFlowStateMachine m_StateMachine;
    bool m_StateMachineRegistered = false;

    // indication that DPC waits on TX lock
    CNdisRefCounter m_DpcWaiting;

    CLockFreeCNBLQueue m_SendQueue;

    CRawCNBLList m_WaitingList;
    CNdisSpinLock m_WaitingListLock;

    CPool<CNB, 'BNHR'>  m_nbPool;
    CPool<CNBL, 'LNHR'> m_nblPool;
};
