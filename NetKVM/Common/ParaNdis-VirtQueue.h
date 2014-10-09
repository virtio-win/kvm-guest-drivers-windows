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
    CTXHeaders()
    {}

    bool Create(NDIS_HANDLE DrvHandle, ULONG VirtioHdrSize) 
    {
        m_VirtioHdrSize = VirtioHdrSize;
        return m_HeadersBuffer.Create(DrvHandle);
    }

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
    CTXDescriptor()
    {}

    bool Create(NDIS_HANDLE DrvHandle,
                  ULONG VirtioHeaderSize,
                  struct VirtIOBufferDescriptor *VirtioSGL,
                  ULONG VirtioSGLSize,
                  bool Indirect,
                  bool AnyLayout)
    {
        if (!m_Headers.Create(DrvHandle, VirtioHeaderSize))
            return false;
        if (!m_IndirectArea.Create(DrvHandle))
            return false;
        m_VirtioSGL = VirtioSGL;
        m_VirtioSGLSize = VirtioSGLSize;
        m_Indirect = Indirect;
        m_AnyLayout = AnyLayout;


        return m_Headers.Allocate() && (!m_Indirect || m_IndirectArea.Allocate(PAGE_SIZE));
    }

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
    bool m_AnyLayout;

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
    CVirtQueue()
        : m_DrvHandle(NULL)
        , m_Index(0xFFFFFFFF)
        , m_IODevice(NULL)
        , m_UsePublishedIndices(false)
    {}

    virtual ~CVirtQueue()
    {
        Delete();
    }

    bool Create(UINT Index,
        VirtIODevice *IODevice,
        NDIS_HANDLE DrvHandle,
        bool UsePublishedIndices);

    ULONG GetRingSize()
    { return VirtIODeviceGetQueueSize(m_VirtQueue); }

    void Renew();

    void Shutdown();

    int AddBuf(struct VirtIOBufferDescriptor sg[],
        unsigned int out_num,
        unsigned int in_num,
        void *data,
        void *va_indirect,
        ULONGLONG phys_indirect)
    { return virtqueue_add_buf(m_VirtQueue, sg, out_num, in_num, data, 
          va_indirect, phys_indirect); }

    void KickAlways()
    { virtqueue_kick(m_VirtQueue); }

    void* GetBuf(unsigned int *len)
    { return virtqueue_get_buf(m_VirtQueue, len); }

    //TODO: Needs review / temporary
    void Kick()
    { virtqueue_kick(m_VirtQueue); }

    bool Restart()
    { return virtqueue_enable_cb(m_VirtQueue); }

    //TODO: Needs review/temporary?
    void EnableInterrupts()
    { virtqueue_enable_cb(m_VirtQueue); }

    //TODO: Needs review/temporary?
    void DisableInterrupts()
    { virtqueue_disable_cb(m_VirtQueue); }

    //TODO: Needs review/temporary?
    bool IsInterruptEnabled()
    { return virtqueue_is_interrupt_enabled(m_VirtQueue) ? true : false; }

protected:
    NDIS_HANDLE m_DrvHandle;

private:
    bool AllocateQueueMemory();
    void Delete();

    UINT m_Index;
    VirtIODevice *m_IODevice;

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
    CTXVirtQueue()
    { }

    virtual ~CTXVirtQueue();

    bool Create(UINT Index,
        VirtIODevice *IODevice,
        NDIS_HANDLE DrvHandle,
        bool UsePublishedIndices,
        ULONG MaxBuffers,
        ULONG HeaderSize,
        PPARANDIS_ADAPTER Context);

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
        virtqueue_notify(m_VirtQueue);
#else
        virtqueue_kick(m_VirtQueue);
#endif
    }

    //TODO: Needs review / temporary
    bool Restart()
    { return virtqueue_enable_cb(m_VirtQueue); }

    bool HasPacketsInHW()
    { return !m_DescriptorsInUse.IsEmpty(); }

    //TODO: Needs review
    bool HasHWBuffersIsUse()
    { return m_FreeHWBuffers != m_TotalHWBuffers; }

    //TODO: Needs review/temporary?
    void EnableInterrupts()
    { virtqueue_enable_cb(m_VirtQueue); }

    //TODO: Needs review/temporary?
    void DisableInterrupts()
    { virtqueue_disable_cb(m_VirtQueue); }

    //TODO: Needs review/temporary?
    bool IsInterruptEnabled()
    { return virtqueue_is_interrupt_enabled(m_VirtQueue) ? true : false; }

    //TODO: Needs review/temporary?
    ULONG GetFreeTXDescriptors()
    { return m_Descriptors.GetCount(); }

    //TODO: Needs review/temporary?
    ULONG GetFreeHWBuffers()
    { return m_FreeHWBuffers; }

    //TODO: Needs review
    void Shutdown();

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
