#pragma once
#include "helper.h"

#define REDHAT_PCI_VENDOR_ID       0x1AF4

#define PORT_MASK                  0xFFFF
#define VIRTIO_GPU_MSIX_CONFIG_VECTOR 0

class CPciBar
{
public:
    CPciBar(PHYSICAL_ADDRESS BasePA, ULONG uSize, bool bPortSpace, bool bIoMapped)
        : m_BasePA(BasePA)
        , m_uSize(uSize)
        , m_BaseVA(nullptr)
        , m_bPortSpace(bPortSpace)
        , m_bIoMapped(bIoMapped)
    {
        ASSERT(!m_bPortSpace || m_BasePA.HighPart == 0);
    }

    CPciBar() : CPciBar(PHYSICAL_ADDRESS(), 0, false, true)
     { }

    ~CPciBar()
    {
        ASSERT(m_BaseVA == nullptr);
    }

    ULONG GetSize()
    {
        return m_uSize;
    }

    bool IsPortSpace()
    {
        return m_bPortSpace;
    }

    PHYSICAL_ADDRESS GetPA()
    {
        return m_BasePA;
    }

    // Maps BAR into virtual memory if not already mapped
    PVOID GetVA(PDXGKRNL_INTERFACE pDxgkInterface);

    // Undoes the effect of GetVA
    void Unmap(PDXGKRNL_INTERFACE pDxgkInterface);

private:
    PHYSICAL_ADDRESS m_BasePA;
    ULONG            m_uSize;
    PVOID            m_BaseVA;
    bool             m_bPortSpace;
    bool             m_bIoMapped;
};

class CPciResources
{
public:
    CPciResources()
        : m_pDxgkInterface(nullptr),
          m_InterruptFlags(0)
    { }

    ~CPciResources()
    {
        for (UINT bar = 0; bar < PCI_TYPE0_ADDRESSES; bar++)
        {
            m_Bars[bar].Unmap(m_pDxgkInterface);
        }
    }

    bool Init(PDXGKRNL_INTERFACE pDxgkInterface, PCM_RESOURCE_LIST pResList);

    ULONG GetBarSize(UINT bar)
    {
        ASSERT(bar < PCI_TYPE0_ADDRESSES);
        return m_Bars[bar].GetSize();
    }

    USHORT GetInterruptFlags()
    {
        return m_InterruptFlags;
    }

    BOOLEAN IsMSIEnabled()
    {
        return (m_InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE);
    }

    CPciBar* GetPciBar(UINT bar)
    {
        ASSERT(bar < PCI_TYPE0_ADDRESSES);
        if (bar < PCI_TYPE0_ADDRESSES)
        {
            return &m_Bars[bar];
        }
        return NULL;
    }

    PVOID GetMappedAddress(UINT bar, ULONG uOffset);

private:
    PDXGKRNL_INTERFACE m_pDxgkInterface;
    USHORT             m_InterruptFlags;
    CPciBar            m_Bars[PCI_TYPE0_ADDRESSES];
};

NTSTATUS
MapFrameBuffer(
    _In_                PHYSICAL_ADDRESS    PhysicalAddress,
    _In_                ULONG               Length,
    _Outptr_result_bytebuffer_(Length) VOID**              VirtualAddress);

NTSTATUS
UnmapFrameBuffer(
    _In_reads_bytes_(Length) VOID* VirtualAddress,
    _In_                ULONG Length);

