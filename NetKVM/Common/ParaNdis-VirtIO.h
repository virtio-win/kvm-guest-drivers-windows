#pragma once

#include "ndis56common.h"

extern VirtIOSystemOps ParaNdisSystemOps;

class CPciBar
{
public:
    CPciBar(NDIS_PHYSICAL_ADDRESS BasePA, ULONG uSize, bool bPortSpace)
        : m_BasePA(BasePA)
        , m_uSize(uSize)
        , m_BaseVA(nullptr)
        , m_bPortSpace(bPortSpace)
    {
        ASSERT(!m_bPortSpace || m_BasePA.HighPart == 0);
    }

    CPciBar() : CPciBar(NDIS_PHYSICAL_ADDRESS(), 0, false)
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

    NDIS_PHYSICAL_ADDRESS GetPA()
    {
        return m_BasePA;
    }

    // Maps BAR into virtual memory if not already mapped
    PVOID GetVA(NDIS_HANDLE DrvHandle);

    // Undoes the effect of GetVA
    void Unmap(NDIS_HANDLE DrvHandle);

private:
    NDIS_PHYSICAL_ADDRESS m_BasePA;
    ULONG                 m_uSize;
    PVOID                 m_BaseVA;
    bool                  m_bPortSpace;
};

class CPciResources
{
public:
    CPciResources()
        : m_DrvHandle(nullptr),
          m_InterruptFlags(0)
    { }

    ~CPciResources()
    {
        for (UINT bar = 0; bar < PCI_TYPE0_ADDRESSES; bar++)
        {
            m_Bars[bar].Unmap(m_DrvHandle);
        }
    }

    // Initializes BARs according to the given resource list. Internally reads
    // PCI config space to determine BAR indices of individual resources as this
    // information is not provided by WDM.
    bool Init(NDIS_HANDLE DrvHandle, PNDIS_RESOURCE_LIST RList);

    ULONG GetBarSize(UINT bar)
    {
        ASSERT(bar < PCI_TYPE0_ADDRESSES);
        return m_Bars[bar].GetSize();
    }

    bool IsPortBar(UINT bar)
    {
        ASSERT(bar < PCI_TYPE0_ADDRESSES);
        return m_Bars[bar].IsPortSpace();
    }

    USHORT GetInterruptFlags()
    {
        return m_InterruptFlags;
    }

    // Maps the BAR if not already mapped and returns the port or virtual
    // address at the given offset. This function's return value can be
    // passed to the NdisWriteRegister / NdisReadRegister (memory I/O) or
    // NdisRawWritePort / NdisRawReadPort (port I/O) family of functions.
    PVOID GetMappedAddress(UINT bar, ULONG uOffset);

private:
    NDIS_HANDLE m_DrvHandle;
    USHORT      m_InterruptFlags;
    CPciBar     m_Bars[PCI_TYPE0_ADDRESSES];
};
