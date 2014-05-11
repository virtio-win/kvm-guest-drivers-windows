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
class CTXDescriptor;
typedef struct _tagPARANDIS_ADAPTER *PPARANDIS_ADAPTER;

//TODO: requires review, temporary?
typedef enum { cpeOK, cpeNoBuffer, cpeInternalError, cpeTooLarge, cpeNoIndirect } tCopyPacketError;
typedef struct _tagCopyPacketResult
{
    ULONG       size;
    tCopyPacketError error;
}tCopyPacketResult;

//TODO: requires review, temporary?
typedef struct _tagTxOperationParameters
{
    CNB            *NB;
    UINT            nofSGFragments;
    ULONG           ulDataSize;
    ULONG           offloalMss;
    ULONG           tcpHeaderOffset;
    ULONG           flags;      //see tPacketOffloadRequest
}tTxOperationParameters;

//TODO: requires review, temporary?
typedef struct _tagMapperResult
{
    USHORT  usBuffersMapped;
    USHORT  usBufferSpaceUsed;
    ULONG   ulDataSize;
}tMapperResult;

class CVirtQueue
{
public:
    ULONG GetSize()
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
                 ULONG DataSize,
                 PPARANDIS_ADAPTER Context)
        : CVirtQueue(Index, IODevice, DrvHandle, UsePublishedIndices)
        , m_MaxBuffers(MaxBuffers)
        , m_HeaderSize(HeaderSize)
        , m_DataSize(DataSize)
        , m_Context(Context)
    { }

    virtual ~CTXVirtQueue();

    bool Create();

    //TODO: Temporary, needs review
    tCopyPacketResult DoCopyPacketData(tTxOperationParameters *pParams);
    //TODO: Temporary Requires review
    VOID PacketMapper(
        CNB *NB,
        struct VirtIOBufferDescriptor *buffers,
        CTXDescriptor &TXDescriptor,
        tMapperResult *pMapperResult
        );

    //TODO: Temporary: needs review
    tCopyPacketResult DoSubmitPacket(tTxOperationParameters *Params);

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
    ULONG m_DataSize;

    CNdisList<CTXDescriptor, CRawAccess, CCountingObject> m_Descriptors;
    CNdisList<CTXDescriptor, CRawAccess, CNonCountingObject> m_DescriptorsInUse;
    ULONG m_TotalDescriptors = 0;
    ULONG m_FreeHWBuffers = 0;
    //TODO: Temporary
    ULONG m_TotalHWBuffers = 0;
    //TODO: Needs review
    bool m_DoKickOnNoBuffer = false;

    struct VirtIOBufferDescriptor *m_SGTable = nullptr;

    //TODO Temporary, must go way
    PPARANDIS_ADAPTER m_Context;
};
