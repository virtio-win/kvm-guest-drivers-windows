#pragma once

extern "C"
{

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio.h"
#include "VirtIODmb.h"

#include "ethernetutils.h"
}

#include "ParaNdis-Util.h"
#include "virtio_net.h"

class CNB;
class CTXVirtQueue;
typedef struct _PARANDIS_ADAPTER *PPARANDIS_ADAPTER;

typedef enum class _tagSubmitTxPacketResult
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
    {
    }

    void Initialize(ULONG VirtioHdrSize, const tCompletePhysicalAddress &Buffer);

    virtio_net_hdr *VirtioHeader() const
    {
        return static_cast<virtio_net_hdr *>(m_VirtioHeaderVA);
    }
    ULONG VirtioHeaderLength() const
    {
        return m_VirtioHdrSize;
    }
    PETH_HEADER EthHeader() const
    {
        return static_cast<PETH_HEADER>(m_EthHeaderVA);
    }
    PVLAN_HEADER VlanHeader() const
    {
        return static_cast<PVLAN_HEADER>(m_VlanHeaderVA);
    }
    PVOID IPHeaders() const
    {
        return m_IPHeadersVA;
    }
    PVOID EthHeadersAreaVA() const
    {
        return m_EthHeaderVA;
    }
    PVOID HeadersBufferVA() const
    {
        return m_HeadersBuffer.Virtual;
    }
    // Bytes from the start of the headers buffer to the end of
    // ParsedHeadersLength bytes of Ethernet headers.
    ULONG UsedLength(ULONG ParsedHeadersLength) const
    {
        return RtlPointerToOffset(m_HeadersBuffer.Virtual, m_EthHeaderVA) + ParsedHeadersLength;
    }

    ULONG MaxEthHeadersSize() const
    {
        return m_MaxEthHeadersSize;
    }
    ULONG IpHeadersSize() const
    {
        return m_MaxEthHeadersSize - ETH_HEADER_SIZE;
    }

    PHYSICAL_ADDRESS VirtioHeaderPA() const
    {
        return m_VirtioHeaderPA;
    }
    PHYSICAL_ADDRESS EthHeaderPA() const
    {
        return m_EthHeaderPA;
    }
    PHYSICAL_ADDRESS VlanHeaderPA() const
    {
        return m_VlanHeaderPA;
    }
    PHYSICAL_ADDRESS IPHeadersPA() const
    {
        return m_IPHeadersPA;
    }

  private:
    tCompletePhysicalAddress m_HeadersBuffer;
    ULONG m_VirtioHdrSize = 0;

    PVOID m_VlanHeaderVA = nullptr;
    PVOID m_VirtioHeaderVA = nullptr;
    PVOID m_EthHeaderVA = nullptr;
    PVOID m_IPHeadersVA = nullptr;

    PHYSICAL_ADDRESS m_VlanHeaderPA = PHYSICAL_ADDRESS();
    PHYSICAL_ADDRESS m_VirtioHeaderPA = PHYSICAL_ADDRESS();
    PHYSICAL_ADDRESS m_EthHeaderPA = PHYSICAL_ADDRESS();
    PHYSICAL_ADDRESS m_IPHeadersPA = PHYSICAL_ADDRESS();

    ULONG m_MaxEthHeadersSize = 0;

    CTXHeaders(const CTXHeaders &) = delete;
    CTXHeaders &operator=(const CTXHeaders &) = delete;
};

class CTXDescriptor : public CNdisAllocatable<CTXDescriptor, 'DTHR'>
{
  public:
    CTXDescriptor()
    {
    }

    ~CTXDescriptor()
    {
        if (m_DmbHeadersVA != nullptr)
        {
            virtio_dmb_free(m_DmbDevice, m_DmbHeadersVA, PAGE_SIZE);
            m_DmbHeadersVA = nullptr;
        }
    }

    // DmbDevice is the device when its Device Memory Buffer is active, else
    // nullptr. With a DMB the headers are built in this descriptor's private
    // page, as usual, and copied into a DMB page of the same layout when the
    // packet is enqueued, so the device never sees guest RAM and the driver
    // never parses memory the device can change.
    bool Create(NDIS_HANDLE DrvHandle,
                ULONG VirtioHeaderSize,
                struct VirtIOBufferDescriptor *VirtioSGL,
                ULONG VirtioSGLSize,
                bool Indirect,
                bool AnyLayout,
                VirtIODevice *DmbDevice)
    {
        m_MemoryBuffer.Initialize(DrvHandle);
        // allocate 8K buffer (or 4K if indirect is not used)
        if (!m_MemoryBuffer.Allocate(PAGE_SIZE * (Indirect ? 2 : 1)))
        {
            return false;
        }
        // The headers page is copied into the DMB by prefix length, including
        // padding the header builders never write, so start it zeroed.
        RtlZeroMemory(m_MemoryBuffer.GetVA(), PAGE_SIZE * (Indirect ? 2 : 1));
        m_VirtioSGL = VirtioSGL;
        m_VirtioSGLSize = VirtioSGLSize;
        m_Indirect = Indirect;
        m_AnyLayout = AnyLayout;

        // first 4K is for headers area
        tCompletePhysicalAddress headers;
        headers.Physical = m_MemoryBuffer.GetPA();
        headers.Virtual = m_MemoryBuffer.GetVA();
        headers.size = PAGE_SIZE;

        if (DmbDevice != nullptr)
        {
            u64 offset = 0;

            // Indirect tables would also have to live in the DMB; the caller
            // does not negotiate them in DMB mode.
            NETKVM_ASSERT(!Indirect);
            m_DmbHeadersVA = virtio_dmb_alloc(DmbDevice, PAGE_SIZE, &offset);
            if (m_DmbHeadersVA == nullptr)
            {
                return false;
            }
            m_DmbDevice = DmbDevice;
            // The device addresses of the header chunks are DMB offsets.
            headers.Physical.QuadPart = (LONGLONG)offset;
        }
        m_Headers.Initialize(VirtioHeaderSize, headers);

        // second 4K is for indirect area
        if (m_Indirect)
        {
            m_IndirectArea.Physical = m_MemoryBuffer.GetPA();
            m_IndirectArea.Physical.QuadPart += PAGE_SIZE;
            m_IndirectArea.Virtual = RtlOffsetToPointer(m_MemoryBuffer.GetVA(), PAGE_SIZE);
            m_IndirectArea.size = PAGE_SIZE;
        }
        return true;
    }

    SubmitTxPacketResult Enqueue(CTXVirtQueue *Queue, ULONG TotalDescriptors, ULONG FreeDescriptors);

    CTXHeaders &HeadersAreaAccessor()
    {
        return m_Headers;
    }
    ULONG GetUsedBuffersNum()
    {
        return m_UsedBuffersNum;
    }
    void SetNB(CNB *NB)
    {
        m_NB = NB;
    }
    CNB *GetNB()
    {
        return m_NB;
    }

    bool AddDataChunk(const PHYSICAL_ADDRESS &PA, ULONG Length);
    bool SetupHeaders(ULONG ParsedHeadersLength);

    bool HasRoom(ULONG NumEntries)
    {
        return (m_VirtioSGLSize - m_CurrVirtioSGLEntry) >= NumEntries;
    }

  private:
    CTXHeaders m_Headers;
    CNdisSharedMemory m_MemoryBuffer;
    // DMB copy of the headers page (DMB mode only) and the number of bytes
    // of it the current packet uses, from the start of the page.
    VirtIODevice *m_DmbDevice = nullptr;
    PVOID m_DmbHeadersVA = nullptr;
    ULONG m_HeadersUsedLength = 0;
    tCompletePhysicalAddress m_IndirectArea = {};
    bool m_Indirect = false;
    bool m_AnyLayout = false;

    struct VirtIOBufferDescriptor *m_VirtioSGL = nullptr;
    ULONG m_VirtioSGLSize = 0;
    ULONG m_CurrVirtioSGLEntry = 0;

    ULONG m_UsedBuffersNum = 0;
    CNB *m_NB = nullptr;

    CTXDescriptor(const CTXDescriptor &) = delete;
    CTXDescriptor &operator=(const CTXDescriptor &) = delete;

    DECLARE_CNDISLIST_ENTRY(CTXDescriptor);
};

class CVirtQueue
{
  public:
    CVirtQueue() : m_DrvHandle(NULL), m_Index(0xFFFFFFFF), m_IODevice(NULL), m_CanTouchHardware(true)
    {
    }

    virtual ~CVirtQueue()
    {
        Delete();
    }

    bool Create(UINT Index, VirtIODevice *IODevice, NDIS_HANDLE DrvHandle);

    ULONG GetRingSize()
    {
        return virtio_get_queue_size(m_VirtQueue);
    }

    void Renew();

    void Shutdown()
    {
        if (m_VirtQueue && CanTouchHardware())
        {
            virtqueue_shutdown(m_VirtQueue);
        }
        Delete();
    }

    void DoNotTouchHardware()
    {
        m_CanTouchHardware = false;
    }

    void AllowTouchHardware()
    {
        m_CanTouchHardware = true;
    }

    bool CanTouchHardware()
    {
        return m_CanTouchHardware;
    }

    u16 SetMSIVector(u16 vector);

    int AddBuf(struct VirtIOBufferDescriptor sg[],
               unsigned int out_num,
               unsigned int in_num,
               void *data,
               void *va_indirect,
               ULONGLONG phys_indirect)
    {
        return virtqueue_add_buf(m_VirtQueue, sg, out_num, in_num, data, va_indirect, phys_indirect);
    }

    void *GetBuf(unsigned int *len)
    {
        return virtqueue_get_buf(m_VirtQueue, len);
    }

    // TODO: Needs review / temporary
    void Kick()
    {
        virtqueue_kick(m_VirtQueue);
    }

    // TODO: Needs review / temporary
    void KickAlways()
    {
        virtqueue_notify(m_VirtQueue);
    }

    bool Restart()
    {
        if (!virtqueue_enable_cb(m_VirtQueue))
        {
            virtqueue_disable_cb(m_VirtQueue);
            return false;
        }

        return true;
    }

    // TODO: Needs review/temporary?
    void EnableInterruptsDelayed()
    {
        virtqueue_enable_cb_delayed(m_VirtQueue);
    }

    // TODO: Needs review/temporary?
    void EnableInterrupts()
    {
        virtqueue_enable_cb(m_VirtQueue);
    }

    // TODO: Needs review/temporary?
    void DisableInterrupts()
    {
        virtqueue_disable_cb(m_VirtQueue);
    }

    // The device wrote an invalid used-ring entry; see struct virtqueue.broken.
    bool IsBroken() const
    {
        return m_VirtQueue != nullptr && virtqueue_is_broken(m_VirtQueue);
    }

    bool IsValid() const
    {
        return m_VirtQueue != nullptr;
    }

  protected:
    NDIS_HANDLE m_DrvHandle;

  private:
    bool AllocateQueueMemory();
    void Delete();

    bool m_CanTouchHardware;

    UINT m_Index;
    VirtIODevice *m_IODevice;

    CNdisSharedMemory m_SharedMemory;
    struct virtqueue *m_VirtQueue = nullptr;

    CVirtQueue(const CVirtQueue &) = delete;
    CVirtQueue &operator=(const CVirtQueue &) = delete;
};

typedef CNdisList<CNB, CRawAccess, CNonCountingObject> CRawCNBList;

class CTXVirtQueue : public CVirtQueue
{
  public:
    CTXVirtQueue()
    {
    }

    virtual ~CTXVirtQueue();

    bool Create(UINT Index,
                VirtIODevice *IODevice,
                NDIS_HANDLE DrvHandle,
                ULONG MaxBuffers,
                ULONG HeaderSize,
                PPARANDIS_ADAPTER Context);

    SubmitTxPacketResult SubmitPacket(CNB &NB);

    void ProcessTXCompletions(CRawCNBList &listDone, bool bKill = false);
    bool Alive()
    {
        return !m_Killed;
    }

    // TODO: Needs review/temporary?
    ULONG GetFreeTXDescriptors()
    {
        return m_Descriptors.GetCount();
    }

    // TODO: Needs review/temporary?
    ULONG GetFreeHWBuffers()
    {
        return m_FreeHWBuffers;
    }

    ULONG GetActualQueueSize() const
    {
        return m_TotalDescriptors;
    }

    // TODO: Needs review
    void Shutdown();

  private:
    UINT ReleaseTransmitBuffers(CRawCNBList &listDone);
    void ReleaseOneBuffer(CTXDescriptor *TXDescriptor, CRawCNBList &listDone);
    bool PrepareBuffers();
    void FreeBuffers();
    ULONG m_MaxBuffers = 0;
    ULONG m_HeaderSize = 0;

    void KickQueueOnOverflow();
    void UpdateTXStats(const CNB &NB, CTXDescriptor &Descriptor);

    CNdisList<CTXDescriptor, CRawAccess, CCountingObject> m_Descriptors;
    CNdisList<CTXDescriptor, CRawAccess, CNonCountingObject> m_DescriptorsInUse;
    ULONGLONG m_LastTxCompletionTimestamp = 0;
    ULONG m_TotalDescriptors = 0;
    ULONG m_FreeHWBuffers = 0;
    // TODO: Temporary
    ULONG m_TotalHWBuffers = 0;
    // TODO: Needs review
    bool m_DoKickOnNoBuffer = false;

    struct VirtIOBufferDescriptor *m_SGTable = nullptr;
    ULONG m_SGTableCapacity = 0;
    bool m_Killed = false;

    // TODO Temporary, must go way
    PPARANDIS_ADAPTER m_Context = nullptr;
};
