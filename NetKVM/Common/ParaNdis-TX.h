#pragma once
#include "ParaNdis-VirtQueue.h"
#include "ParaNdis-AbstractPath.h"
#include "ParaNdis_GuestAnnounce.h"
#include "ParaNdis_LockFreeQueue.h"
#include "ParaNdis_DebugHistory.h"

/* Must be a power of 2 */
#define PARANDIS_TX_LOCK_FREE_QUEUE_DEFAULT_SIZE 2048
#define NBL_MAINTAIN_HISTORY                     0

/* the maximum number of pages that a single network packet can span.
refer linux kernel code #define MAX_SKB_FRAGS (65536/PAGE_SIZE + 1), */
#define MAX_PACKET_PAGES                         17

class CNB;
class CParaNdisTX;

class CNBL;

typedef enum class _tagNBMappingStatus
{
    SUCCESS = 0,
    NO_RESOURCE,
    FAILURE
} NBMappingStatus;

struct CExtendedNBStorage
{
    ULONG m_UsedPagesCount;
    /* Array to hold pages storing packet data. Although MAX_PACKET_PAGES - 1 might be
     * sufficient since protocol headers are not stored, we're currently using
     * MAX_PACKET_PAGES for simplicity and potential future use cases.
     */
    CNdisSharedMemory *m_UsedPages[MAX_PACKET_PAGES];
    SCATTER_GATHER_ELEMENT m_Elements[MAX_PACKET_PAGES];
};

class CNB : public CNdisAllocatableViaHelper<CNB>
{
  public:
    CNB(PNET_BUFFER NB, CNBL *ParentNBL, PPARANDIS_ADAPTER Context, CAllocationHelper<CNB> *Allocator)
        : m_NB(NB), m_ParentNBL(ParentNBL), m_Context(Context), CNdisAllocatableViaHelper<CNB>(Allocator)
    {
    }

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

    NBMappingStatus BindToDescriptor(CTXDescriptor &Descriptor);
    void Report(int level, bool Success);
    void ReturnPages();

  private:
    ULONG Copy(PVOID Dst, ULONG Length) const;
    static ULONG CopyFromMdlChain(PVOID Dst, ULONG Length, PMDL &Source, ULONG &Offset);
    bool CopyHeaders(PVOID Destination, ULONG MaxSize, ULONG &HeadersLength, ULONG &L4HeaderOffset) const;
    void BuildPriorityHeader(PETH_HEADER EthHeader, PVLAN_HEADER VlanHeader) const;
    void PrepareOffloads(virtio_net_hdr *VirtioHeader,
                         PVOID IpHeader,
                         ULONG EthPayloadLength,
                         ULONG L4HeaderOffset) const;
    void SetupLSO(virtio_net_hdr *VirtioHeader, PVOID IpHeader, ULONG EthPayloadLength) const;
    void SetupUSO(virtio_net_hdr *VirtioHeader, PVOID IpHeader, ULONG EthPayloadLength) const;
    USHORT QueryL4HeaderOffset(PVOID PacketData, ULONG IpHeaderOffset) const;
    void DoIPHdrCSO(PVOID EthHeaders, ULONG HeadersLength) const;
    void SetupCSO(virtio_net_hdr *VirtioHeader, ULONG L4HeaderOffset) const;
    NBMappingStatus FillDescriptorSGList(CTXDescriptor &Descriptor, ULONG DataOffset);
    NBMappingStatus MapDataToVirtioSGL(CTXDescriptor &Descriptor, ULONG Offset) const;
    void PopulateIPLength(IPHeader *IpHeader, USHORT IpLength) const;
    NBMappingStatus MapCopyDataToVirtioSGL(CTXDescriptor &Descriptor) const;
    NBMappingStatus AllocateAndFillCopySGL(ULONG ParsedHeadersLength);

    PNET_BUFFER m_NB;
    CNBL *m_ParentNBL;
    PPARANDIS_ADAPTER m_Context;
    PSCATTER_GATHER_LIST m_SGL = nullptr;
    CExtendedNBStorage *m_ExtraNBStorage = nullptr;

    CNB(const CNB &) = delete;
    CNB &operator=(const CNB &) = delete;

    DECLARE_CNDISLIST_ENTRY(CNB);
};

typedef struct _tChainOfNbls
{
    CNdisRefCounter m_NumChained;
    CNdisRefCounter m_NumCompleted;
    CNBL *m_FirstInChain;
} CChainOfNbls;

class NBLHistory : public CNdisAllocatable<NBLHistory, 'NBLH'>
{
    DECLARE_CNDISLIST_ENTRY(NBLHistory);

  public:
    NBLHistory(LPCSTR Func, LPCSTR Title, LPCSTR ParameterMeaning, PVOID Parameter, LPCSTR ValueMeaning, ULONG Value);

  protected:
    ULONGLONG m_Timestamp;
    LPCSTR m_Function;
    LPCSTR m_Title;
    LPCSTR m_ParameterMeaning;
    PVOID m_Parameter;
    LPCSTR m_ValueMeaning;
    ULONG m_Value;
    ULONG m_CurrentCPU;
};

typedef CNdisList<NBLHistory, CLockedAccess, CNonCountingObject> CHistoryList;

class CNBL : public CNdisAllocatableViaHelper<CNBL>, public CRefCountingObject, public CAllocationHelper<CNB>
{
  public:
    CNBL(PNET_BUFFER_LIST NBL,
         PPARANDIS_ADAPTER Context,
         CParaNdisTX &ParentTXPath,
         CAllocationHelper<CNBL> *NBLAllocator,
         CAllocationHelper<CNB> *NBAllocator);
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
    {
        return (ParsePriority() && ParseBuffers() && ParseOffloads());
    }
#if NBL_CHAINS
    void Die()
    {
        // RtlFailFast(FAST_FAIL_RANGE_CHECK_FAILURE);
        DbgBreakPoint();
    }
    void SetInChain(PNET_BUFFER_LIST FirstInChain)
    {
        if (FirstInChain != m_NBL)
        {
            m_Chain.m_FirstInChain = (CNBL *)FirstInChain->Scratch;
            m_Chain.m_FirstInChain->m_Chain.m_NumChained.AddRef();
            m_Chain.m_FirstInChain->AddRef();
        }
    }
    void UnsetInChain()
    {
        // the NBL was failed mapping, if does not go
        // to transmit path
        ParaNdis_DebugHistory(this,
                              eHistoryLogOperation::hopSendComplete,
                              m_NBL,
                              !m_Chain.m_FirstInChain,
                              m_NBL->Status,
                              GetCurrentRefCounterUnsafe());
        if (m_Chain.m_FirstInChain)
        {
            m_Chain.m_FirstInChain->m_Chain.m_NumChained.Release();
            // the head of chain will be released in the destructor
        }
    }
    void CompleteInChain()
    {
        auto head = m_Chain.m_FirstInChain;
        if (head)
        {
            LONG completed = head->m_Chain.m_NumCompleted.AddRef();
            ParaNdis_DebugHistory(this,
                                  eHistoryLogOperation::hopSendCompleteChain,
                                  head,
                                  completed,
                                  0,
                                  head->GetCurrentRefCounterUnsafe());
            AddHistory(__FUNCTION__, "", "Head", head, "Completed", completed);
            if (head->m_Chain.m_NumChained < completed)
            {
                // this is a fatal problem, probably caused by
                // double completion, may cause to list corruption
                // TODO: assertion??
                Die();
            }
        }
    }
#else
    void SetInChain(PNET_BUFFER_LIST FirstInChain)
    {
        UNREFERENCED_PARAMETER(FirstInChain);
    }
    void UnsetInChain()
    {
    }
    void CompleteInChain()
    {
    }
#endif
    void StartMapping();
    void RegisterMappedNB(CNB *NB);
    bool MappingSucceeded()
    {
        return !m_HaveFailedMappings;
    }
    void SetStatus(NDIS_STATUS Status)
    {
        m_NBL->Status = Status;
    }

    // called under m_Lock of parent TX path for CNBL object in Send list
    CNB *PopMappedNB();
    // called under m_Lock of parent TX path for CNBL object in Send list
    void PushMappedNB(CNB *NBHolder);
    // called under m_Lock of parent TX path for CNBL object in Send list
    bool HaveMappedBuffers()
    {
        return !m_Buffers.IsEmpty();
    }

    PNET_BUFFER_LIST DetachInternalObject();
    // TODO: Needs review
    void NBComplete();
    bool IsSendDone();

    UCHAR ProtocolID()
    {
        return reinterpret_cast<UCHAR>(NET_BUFFER_LIST_INFO(m_NBL, NetBufferListProtocolId));
    }
    bool MatchCancelID(PVOID ID)
    {
        return NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(m_NBL) == ID;
    }
    ULONG MSS()
    {
        return m_LsoInfo.LsoV2Transmit.MSS;
    }
    ULONG TCPHeaderOffset()
    {
        return IsLSO() ? LsoTcpHeaderOffset() : CsoTcpHeaderOffset();
    }
    UINT16 TCI()
    {
        return m_TCI;
    }
    bool IsLSO()
    {
        return (m_LsoInfo.Value != nullptr);
    }
#if PARANDIS_SUPPORT_USO
    bool IsUSO()
    {
        return (m_UsoInfo.Value != nullptr);
    }
    ULONG UsoMSS()
    {
        return m_UsoInfo.Transmit.MSS;
    }
    ULONG UsoHeaderOffset()
    {
        return m_UsoInfo.Transmit.UdpHeaderOffset;
    }
#else
    bool IsUSO()
    {
        return false;
    }
    ULONG UsoMSS()
    {
        return 0;
    }
    ULONG UsoHeaderOffset()
    {
        return 0;
    }
#endif
    bool IsTcpCSO()
    {
        return m_CsoInfo.Transmit.TcpChecksum;
    }
    bool IsUdpCSO()
    {
        return m_CsoInfo.Transmit.UdpChecksum;
    }
    bool IsIPHdrCSO()
    {
        return m_CsoInfo.Transmit.IpHeaderChecksum;
    }
    void UpdateLSOTxStats(ULONG ChunkSize)
    {
        if (m_LsoInfo.LsoV1TransmitComplete.Type == NDIS_TCP_LARGE_SEND_OFFLOAD_V1_TYPE)
        {
            m_TransferSize += ChunkSize;
        }
    }
    bool HasNBL() const
    {
        return m_NBL != nullptr;
    }
    ULONG NumberOfBuffers() const
    {
        return m_BuffersNumber;
    }
    CParaNdisTX *GetParentTXPath() const
    {
        return m_ParentTXPath;
    }
#if NBL_MAINTAIN_HISTORY
    void AddHistory(LPCSTR Func,
                    LPCSTR Title,
                    LPCSTR ParameterMeaning = NULL,
                    PVOID Parameter = NULL,
                    LPCSTR ValueMeaning = NULL,
                    ULONG Value = NULL);
#else
    void AddHistory(...)
    {
    }
#endif
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
    {
        return m_LsoInfo.LsoV2Transmit.TcpHeaderOffset;
    }
    ULONG CsoTcpHeaderOffset()
    {
        return m_CsoInfo.Transmit.TcpHeaderOffset;
    }
    bool FitsLSO();
    bool IsIP4CSO()
    {
        return m_CsoInfo.Transmit.IsIPv4;
    }
    bool IsIP6CSO()
    {
        return m_CsoInfo.Transmit.IsIPv6;
    }

    template <typename TClassPred, typename TOffloadPred, typename TSupportedPred>
    bool ParseCSO(TClassPred IsClass, TOffloadPred IsOffload, TSupportedPred IsSupported, LPSTR OffloadName);

    PNET_BUFFER_LIST m_NBL;
    PPARANDIS_ADAPTER m_Context;
    CParaNdisTX *m_ParentTXPath;
    // align storage for CNB on pointer size boundary and provide enough room for it
    ULONG_PTR m_CNB_Storage[(sizeof(CNB) + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR)];
    bool m_HaveFailedMappings = false;
    bool m_AllNBCompleted = false;

    CNdisList<CNB, CRawAccess, CNonCountingObject> m_Buffers;

    ULONG m_BuffersNumber = 0;
    CNdisRefCounter m_BuffersMapped;
    CNdisRefCounter m_MappedBuffersDetached;
    CNdisRefCounter m_BuffersDone;

    ULONG m_MaxDataLength = 0;
    ULONG m_TransferSize = 0;
    ULONG m_LogIndex;

    UINT16 m_TCI = 0;

    NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO m_LsoInfo;
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO m_CsoInfo;
#if PARANDIS_SUPPORT_USO
    NDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO m_UsoInfo;
#endif
    CAllocationHelper<CNB> *m_NBAllocator;

    CChainOfNbls m_Chain = {};
    CHistoryList m_History;

    CNBL(const CNBL &) = delete;
    CNBL &operator=(const CNBL &) = delete;

    DECLARE_CNDISLIST_ENTRY(CNBL);
};

typedef CNdisList<CNBL, CRawAccess, CNonCountingObject> CRawCNBLList;

typedef CNdisList<CNdisSharedMemory, CRawAccess, CCountingObject> CRawPageList;

typedef CLockFreeDynamicQueue<CNBL> CLockFreeCNBLQueue;

class CParaNdisTX : public CParaNdisTemplatePath<CTXVirtQueue>, public CNdisAllocatable<CParaNdisTX, 'XTHR'>
{
  public:
    CParaNdisTX() = default;
    ~CParaNdisTX();

    bool Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex);

    void Send(PNET_BUFFER_LIST pNBL);

    void NBLMappingDone(CNBL *NBLHolder);

    template <typename TFunctor> void DoWithTXLock(TFunctor Functor)
    {
        TDPCSpinLocker LockedContext(m_Lock);
        Functor();
    }

    // TODO: Needs review
    void CancelNBLs(PVOID CancelId);

    bool RestartQueue();

    // TODO: Needs review/temporary?
    ULONG GetFreeTXDescriptors()
    {
        return m_VirtQueue.GetFreeTXDescriptors();
    }

    ULONG GetActualQueueSize() const
    {
        return m_VirtQueue.GetActualQueueSize();
    }

    // TODO: Needs review/temporary?
    ULONG GetFreeHWBuffers()
    {
        return m_VirtQueue.GetFreeHWBuffers();
    }

    bool DoPendingTasks(CNBL *nblHolder);

    void CompleteOutstandingNBLChain(PNET_BUFFER_LIST NBL, ULONG Flags = 0);
    void CompleteOutstandingInternalNBL(PNET_BUFFER_LIST NBL, BOOLEAN UnregisterOutstanding = TRUE);

    bool BorrowPages(CExtendedNBStorage *extraNBStorage, ULONG NeedPages);
    void ReturnPages(CExtendedNBStorage *extraNBStorage);
    void CheckStuckPackets(ULONG GraceTimeMillies);

  private:
    virtual void Notify(SMNotifications message) override;

    bool SendMapped(bool IsInterrupt, CRawCNBLList &toWaitingList);

    void DropAllNBls(CRawCNBLList &Completed, NDIS_STATUS Status);

    bool FillQueue();

    void PostProcessPendingTask(CRawCNBList &toFree, CRawCNBLList &completed);
    PNET_BUFFER_LIST ProcessWaitingList(CRawCNBLList &completed);

    bool HaveMappedNBLs()
    {
        return !m_SendQueue.IsEmpty();
    }

    CNBL *PopMappedNBL()
    {
        return m_SendQueue.Dequeue();
    }
    CNBL *PeekMappedNBL()
    {
        return m_SendQueue.Peek();
    }

    bool AllocateExtraPages();
    void FreeExtraPages();

    ULONG IsStuck();

    CDataFlowStateMachine m_StateMachine;
    bool m_StateMachineRegistered = false;

    // indication that DPC waits on TX lock
    CNdisRefCounter m_DpcWaiting;

    CLockFreeCNBLQueue m_SendQueue;

    CRawCNBLList m_WaitingList;
    CNdisSpinLock m_WaitingListLock;

    CRawPageList m_ExtraPages;

    struct
    {
        ULONGLONG LastTxProcess;
        ULONGLONG LastAudit;
        ULONGLONG LastSendTime;
        ULONG Stucks;
        ULONG Recovered;
    } m_AuditState = {};

    CPool<CNB, 'BNHR'> m_nbPool;
    CPool<CNBL, 'LNHR'> m_nblPool;
};
