#pragma once

extern "C"
{

#include <ndis.h>
#include "osdep.h"
#include "virtio_pci.h"
#include "VirtIO.h"

}

#include "ParaNdis-Util.h"

class CNB;
typedef struct _tagPARANDIS_ADAPTER *PPARANDIS_ADAPTER;

typedef enum
{
    SUBMIT_SUCCESS = 0,
    SUBMIT_PACKET_TOO_LARGE,
    SUBMIT_NO_PLACE_IN_QUEUE,
    SUBMIT_FAILURE
} SubmitTxPacketResult;

class CTXHeaders
{
public:
    CTXHeaders(NDIS_HANDLE DrvHandle, ULONG VirtioHdrSize)
        : m_HeadersBuffer(DrvHandle)
        , m_VirtioHdrSize(VirtioHdrSize)
    {}

    bool Allocate();

    virtio_net_hdr_basic *VirtioHeader() const
    { return static_cast<virtio_net_hdr_basic*>(m_VirtioHeaderVA); }
    ULONG VirtioHeaderLength() const
    { return m_VirtioHdrSize; }
    PETH_HEADER EthHeader() const
    { return static_cast<PETH_HEADER>(m_EthHeaderVA); }
    PVLAN_HEADER VlanHeader() const
    { return static_cast<PVLAN_HEADER>(m_VlanHeaderVA); }
    PVOID IPHeaders() const
    { return m_IPHeadersVA; }
    PVOID EthHeadersAreaVA() const
    { return m_EthHeaderVA; }

    ULONG MaxEthHeadersSize() const
    { return m_MaxEthHeadersSize; }
    ULONG IpHeadersSize() const
    { return m_MaxEthHeadersSize - ETH_HEADER_SIZE; }

    PHYSICAL_ADDRESS VirtioHeaderPA() const
    { return m_VirtioHeaderPA; }
    PHYSICAL_ADDRESS EthHeaderPA() const
    { return m_EthHeaderPA; }
    PHYSICAL_ADDRESS VlanHeaderPA() const
    { return m_VlanHeaderPA; }
    PHYSICAL_ADDRESS IPHeadersPA() const
    { return m_IPHeadersPA; }

private:
    CNdisSharedMemory m_HeadersBuffer;
    ULONG m_VirtioHdrSize;

    PVOID m_VlanHeaderVA = nullptr;
    PVOID m_VirtioHeaderVA = nullptr;
    PVOID m_EthHeaderVA = nullptr;
    PVOID m_IPHeadersVA = nullptr;

    PHYSICAL_ADDRESS m_VlanHeaderPA = PHYSICAL_ADDRESS();
    PHYSICAL_ADDRESS m_VirtioHeaderPA = PHYSICAL_ADDRESS();
    PHYSICAL_ADDRESS m_EthHeaderPA = PHYSICAL_ADDRESS();
    PHYSICAL_ADDRESS m_IPHeadersPA = PHYSICAL_ADDRESS();

    ULONG m_MaxEthHeadersSize;

    CTXHeaders(const CTXHeaders&) = delete;
    CTXHeaders& operator= (const CTXHeaders&) = delete;
};

class CTXDescriptor : public CNdisAllocatable<CTXDescriptor, 'DTHR'>
{
public:
    CTXDescriptor(NDIS_HANDLE DrvHandle,
                  ULONG VirtioHeaderSize,
                  struct VirtIOBufferDescriptor *VirtioSGL,
                  ULONG VirtioSGLSize,
                  bool Indirect)
        : m_Headers(DrvHandle, VirtioHeaderSize)
        , m_IndirectArea(DrvHandle)
        , m_VirtioSGL(VirtioSGL)
        , m_VirtioSGLSize(VirtioSGLSize)
        , m_Indirect(Indirect)
    {}

    bool Create()
    { return m_Headers.Allocate() && (!m_Indirect || m_IndirectArea.Allocate(PAGE_SIZE)); }

    SubmitTxPacketResult Enqueue(struct virtqueue *VirtQueue, ULONG TotalDescriptors, ULONG FreeDescriptors);

    CTXHeaders &HeadersAreaAccessor()
    { return m_Headers; }
    ULONG GetUsedBuffersNum()
    { return m_UsedBuffersNum; }
    void SetNB(CNB *NB)
    { m_NB = NB; }
    CNB* GetNB()
    { return m_NB; }

    bool AddDataChunk(const PHYSICAL_ADDRESS &PA, ULONG Length);
    bool SetupHeaders(ULONG ParsedHeadersLength);

private:
    CTXHeaders m_Headers;
    CNdisSharedMemory m_IndirectArea;
    bool m_Indirect;

    struct VirtIOBufferDescriptor *m_VirtioSGL;
    ULONG m_VirtioSGLSize;
    ULONG m_CurrVirtioSGLEntry;

    ULONG m_UsedBuffersNum = 0;
    CNB *m_NB = nullptr;

    CTXDescriptor(const CTXDescriptor&) = delete;
    CTXDescriptor& operator= (const CTXDescriptor&) = delete;

    DECLARE_CNDISLIST_ENTRY(CTXDescriptor);
};

class CVirtQueue
{
public:
    ULONG GetRingSize()
    { return VirtIODeviceGetQueueSize(m_VirtQueue); }

    void Renew();

protected:
    CVirtQueue(UINT Index,
               VirtIODevice &IODevice,
               NDIS_HANDLE DrvHandle,
               bool UsePublishedIndices)
        : m_DrvHandle(DrvHandle)
        , m_Index(Index)
        , m_IODevice(IODevice)
        , m_SharedMemory(DrvHandle)
        , m_UsePublishedIndices(UsePublishedIndices)
    {}

    virtual ~CVirtQueue()
    { Delete(); }

    bool Create();
    const NDIS_HANDLE m_DrvHandle;

private:
    bool AllocateQueueMemory();
    void Delete();

    UINT m_Index;
    VirtIODevice &m_IODevice;

    CNdisSharedMemory m_SharedMemory;
    bool m_UsePublishedIndices;

protected:
    //TODO: Temporary, must to be private
    struct virtqueue *m_VirtQueue = nullptr;

    CVirtQueue(const CVirtQueue&) = delete;
    CVirtQueue& operator= (const CVirtQueue&) = delete;
};

class CTXVirtQueue : public CVirtQueue
{
public:
    CTXVirtQueue(UINT Index,
                 VirtIODevice &IODevice,
                 NDIS_HANDLE DrvHandle,
                 bool UsePublishedIndices,
                 ULONG MaxBuffers,
                 ULONG HeaderSize,
                 PPARANDIS_ADAPTER Context)
        : CVirtQueue(Index, IODevice, DrvHandle, UsePublishedIndices)
        , m_MaxBuffers(MaxBuffers)
        , m_HeaderSize(HeaderSize)
        , m_Context(Context)
    { }

    virtual ~CTXVirtQueue();

    bool Create();

    SubmitTxPacketResult SubmitPacket(CNB &NB);

    //TODO: Temporary, needs review
    UINT VirtIONetReleaseTransmitBuffers();

    //TODO: Needs review
    void ProcessTXCompletions();

    //TODO: Needs review
    void OnTransmitBufferReleased(CTXDescriptor *TXDescriptor);

    //TODO: Needs review / temporary
    void Kick()
    {
#ifdef PARANDIS_TEST_TX_KICK_ALWAYS
        m_VirtQueue->vq_ops->kick_always(m_VirtQueue);
#else
        m_VirtQueue->vq_ops->kick(m_VirtQueue);
#endif
    }

    //TODO: Needs review / temporary
    bool Restart()
    { return m_VirtQueue->vq_ops->restart(m_VirtQueue); }

    bool HasPacketsInHW()
    { return !m_DescriptorsInUse.IsEmpty(); }

    //TODO: Needs review
    void Shutdown();

    //TODO: Needs review
    bool HasHWBuffersIsUse()
    { return m_FreeHWBuffers != m_TotalHWBuffers; }

    //TODO: Needs review/temporary?
    void EnableInterrupts()
    { m_VirtQueue->vq_ops->enable_interrupt(m_VirtQueue); }

    //TODO: Needs review/temporary?
    void DisableInterrupts()
    { m_VirtQueue->vq_ops->disable_interrupt(m_VirtQueue); }

    //TODO: Needs review/temporary?
    bool IsInterruptEnabled()
    { return m_VirtQueue->vq_ops->is_interrupt_enabled(m_VirtQueue) ? true : false; }

    //TODO: Needs review/temporary?
    ULONG GetFreeTXDescriptors()
    { return m_Descriptors.GetCount(); }

    //TODO: Needs review/temporary?
    ULONG GetFreeHWBuffers()
    { return m_FreeHWBuffers; }

private:
    bool PrepareBuffers();
    void FreeBuffers();
    ULONG m_MaxBuffers;
    ULONG m_HeaderSize;

    void KickQueueOnOverflow();
    void UpdateTXStats(const CNB &NB, CTXDescriptor &Descriptor);

    CNdisList<CTXDescriptor, CRawAccess, CCountingObject> m_Descriptors;
    CNdisList<CTXDescriptor, CRawAccess, CNonCountingObject> m_DescriptorsInUse;
    ULONG m_TotalDescriptors = 0;
    ULONG m_FreeHWBuffers = 0;
    //TODO: Temporary
    ULONG m_TotalHWBuffers = 0;
    //TODO: Needs review
    bool m_DoKickOnNoBuffer = false;

    struct VirtIOBufferDescriptor *m_SGTable = nullptr;
    ULONG m_SGTableCapacity = 0;

    //TODO Temporary, must go way
    PPARANDIS_ADAPTER m_Context;
};
