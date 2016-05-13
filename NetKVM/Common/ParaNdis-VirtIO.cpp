/**********************************************************************
* Copyright (c) 2008-2016 Red Hat, Inc.
*
* File: ParaNdis-VirtIO.c
*
* This file contains NDIS driver VirtIO callbacks
*
* This work is licensed under the terms of the GNU GPL, version 2.  See
* the COPYING file in the top-level directory.
*
**********************************************************************/
#include "ndis56common.h"
#include "kdebugprint.h"
#include "osdep.h"
#include "ParaNdis-VirtIO.h"

PVOID CPciBar::GetVA(NDIS_HANDLE DrvHandle)
{
    if (m_BaseVA == nullptr)
    {
        if (m_bPortSpace)
        {
            if (NDIS_STATUS_SUCCESS == NdisMRegisterIoPortRange(
                &m_BaseVA,
                DrvHandle,
                m_BasePA.LowPart,
                m_uSize))
            {
                DPrintf(6, ("[%s] mapped port BAR at %x\n", __FUNCTION__, m_BasePA.LowPart));
            }
            else
            {
                m_BaseVA = nullptr;
                DPrintf(0, ("[%s] failed to map port BAR at %x\n", __FUNCTION__, m_BasePA.LowPart));
            }
        }
        else
        {
            if (NDIS_STATUS_SUCCESS == NdisMMapIoSpace(
                &m_BaseVA,
                DrvHandle,
                m_BasePA,
                m_uSize))
            {
                DPrintf(6, ("[%s] mapped memory BAR at %I64x\n", __FUNCTION__, m_BasePA.QuadPart));
            }
            else
            {
                m_BaseVA = nullptr;
                DPrintf(0, ("[%s] failed to map memory BAR at %I64x\n", __FUNCTION__, m_BasePA.QuadPart));
            }
        }
    }
    return m_BaseVA;
}

void CPciBar::Unmap(NDIS_HANDLE DrvHandle)
{
    if (m_BaseVA != nullptr)
    {
        if (m_bPortSpace)
        {
            NdisMDeregisterIoPortRange(
                DrvHandle,
                m_BasePA.LowPart,
                m_uSize,
                m_BaseVA);
        }
        else
        {
            NdisMUnmapIoSpace(
                DrvHandle,
                m_BaseVA,
                m_uSize);
        }
    }
}

bool CPciResources::Init(NDIS_HANDLE DrvHandle, PNDIS_RESOURCE_LIST RList)
{
    PCI_COMMON_HEADER pci_config;
    bool interrupt_found = false;
    int bar = -1;

    m_DrvHandle = DrvHandle;

    ULONG read = NdisMGetBusData(
        m_DrvHandle,
        PCI_WHICHSPACE_CONFIG,
        0,
        &pci_config,
        sizeof(pci_config));
    if (read != sizeof(pci_config))
    {
        DPrintf(0, ("[%s] could not read PCI config space\n", __FUNCTION__));
        return false;
    }

    for (UINT i = 0; i < RList->Count; i++)
    {
        ULONG type = RList->PartialDescriptors[i].Type;
        if (type == CmResourceTypePort)
        {
            PHYSICAL_ADDRESS Start = RList->PartialDescriptors[i].u.Port.Start;
            ULONG len = RList->PartialDescriptors[i].u.Port.Length;
            bar = virtio_get_bar_index(&pci_config, Start);
            DPrintf(0, ("Found IO ports at %08lX(%d) bar %d\n", Start.LowPart, len, bar));
            if (bar < 0)
            {
                break;
            }
            m_Bars[bar] = CPciBar(Start, len, true);
        }
        else if (type == CmResourceTypeMemory)
        {
            PHYSICAL_ADDRESS Start = RList->PartialDescriptors[i].u.Port.Start;
            ULONG len = RList->PartialDescriptors[i].u.Port.Length;
            bar = virtio_get_bar_index(&pci_config, Start);
            DPrintf(0, ("Found IO memory at %08I64X(%d) bar %d\n", Start.QuadPart, len, bar));
            if (bar < 0)
            {
                break;
            }
            m_Bars[bar] = CPciBar(Start, len, false);
        }
        else if (type == CmResourceTypeInterrupt)
        {
            m_InterruptFlags = RList->PartialDescriptors[i].Flags;
            DPrintf(0, ("Found Interrupt vector %d, level %d, affinity 0x%X, flags %X\n",
                RList->PartialDescriptors[i].u.Interrupt.Vector,
                RList->PartialDescriptors[i].u.Interrupt.Level,
                (ULONG)RList->PartialDescriptors[i].u.Interrupt.Affinity,
                RList->PartialDescriptors[i].Flags));
            interrupt_found = true;
        }
    }

    if (bar < 0 || !interrupt_found)
    {
        DPrintf(0, ("[%s] resource enumeration failed\n", __FUNCTION__));
        return false;
    }
    return true;
}

PVOID CPciResources::GetMappedAddress(UINT bar, ULONG uOffset)
{
    PVOID BaseVA = nullptr;
    ASSERT(bar < PCI_TYPE0_ADDRESSES);

    if (uOffset < m_Bars[bar].GetSize())
    {
        BaseVA = m_Bars[bar].GetVA(m_DrvHandle);
    }
    if (BaseVA != nullptr)
    {
        if (m_Bars[bar].IsPortSpace())
        {
            // use physical address for port I/O
            return (PUCHAR)(ULONG_PTR)m_Bars[bar].GetPA().LowPart + uOffset;
        }
        else
        {
            // use virtual address for memory I/O
            return (PUCHAR)BaseVA + uOffset;
        }
    }
    else
    {
        DPrintf(0, ("[%s] failed to map BAR %d, offset %x\n", __FUNCTION__, bar, uOffset));
        return nullptr;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
//
// ReadVirtIODeviceRegister\WriteVirtIODeviceRegister
// NDIS specific implementation of the IO space read\write
//
// The lower 64k of memory is never mapped so we can use the same routines
// for both port I/O and memory access and use the address alone to decide
// which space to use.
/////////////////////////////////////////////////////////////////////////////////////

#define PORT_MASK 0xFFFF

u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    ULONG ulValue;

    if (ulRegister & ~PORT_MASK) {
        NdisReadRegisterUlong(ulRegister, &ulValue);
    } else {
        NdisRawReadPortUlong(ulRegister, &ulValue);
    }

    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, ulValue));
    return ulValue;
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, ulValue));

    if (ulRegister & ~PORT_MASK) {
        NdisWriteRegisterUlong((PULONG)ulRegister, ulValue);
    } else {
        NdisRawWritePortUlong(ulRegister, ulValue);
    }
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    u8 bValue;

    if (ulRegister & ~PORT_MASK) {
        NdisReadRegisterUchar(ulRegister, &bValue);
    } else {
        NdisRawReadPortUchar(ulRegister, &bValue);
    }

    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, bValue));

    return bValue;
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, bValue));

    if (ulRegister & ~PORT_MASK) {
        NdisWriteRegisterUchar((PUCHAR)ulRegister, bValue);
    } else {
        NdisRawWritePortUchar(ulRegister, bValue);
    }
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    u16 wValue;

    if (ulRegister & ~PORT_MASK) {
        NdisReadRegisterUshort(ulRegister, &wValue);
    } else {
        NdisRawReadPortUshort(ulRegister, &wValue);
    }

    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, wValue));

    return wValue;
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
#if 1
    if (ulRegister & ~PORT_MASK) {
        NdisWriteRegisterUshort((PUSHORT)ulRegister, wValue);
    } else {
        NdisRawWritePortUshort(ulRegister, wValue);
    }
#else
    // test only to cause long TX waiting queue of NDIS packets
    // to recognize it and request for reset via Hang handler
    static int nCounterToFail = 0;
    static const int StartFail = 200, StopFail = 600;
    BOOLEAN bFail = FALSE;
    DPrintf(6, ("%s> R[%x] = %x\n", __FUNCTION__, (ULONG)ulRegister, wValue));
    if ((ulRegister & 0x1F) == 0x10)
    {
        nCounterToFail++;
        bFail = nCounterToFail >= StartFail && nCounterToFail < StopFail;
    }
    if (!bFail) NdisRawWritePortUshort(ulRegister, wValue);
    else
    {
        DPrintf(0, ("%s> FAILING R[%x] = %x\n", __FUNCTION__, (ULONG)ulRegister, wValue));
    }
#endif
}

static void *alloc_pages_exact(void *context, size_t size)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;
    CNdisSharedMemory *pMem = pContext->pPageAllocator;
    PVOID retVal = nullptr;

    if (pMem && pMem->GetSize() >= size)
    {
        retVal = pMem->GetVA();
        NdisZeroMemory(retVal, size);
    }

    if (retVal)
    {
        DPrintf(6, ("[%s] returning %p, size %x\n", __FUNCTION__, retVal, (ULONG)size));
    }
    else
    {
        DPrintf(0, ("[%s] failed to allocate size %x\n", __FUNCTION__, (ULONG)size));
    }
    return retVal;
}

static void free_pages_exact(void *context, void *virt, size_t size)
{
    /* The actual allocation and deallocation is tracked by instances of
     * CNdisSharedMemory whose lifetime is controlled by their owning queues.
     * When queues are torn down on power-off, the memory is not actually
     * freed. Nothing to do here.
     */
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(virt);
    UNREFERENCED_PARAMETER(size);
}

static ULONGLONG virt_to_phys(void *context, void *address)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;
    CNdisSharedMemory *pMem = pContext->pPageAllocator;
    ULONG_PTR uAddr = (ULONG_PTR)address;

    ULONG_PTR uBase = (ULONG_PTR)pMem->GetVA();
    if (uAddr >= uBase && uAddr < (uBase + pMem->GetSize()))
    {
        ULONGLONG retVal = pMem->GetPA().QuadPart + (uAddr - uBase);

        DPrintf(6, ("[%s] translated %p to %I64X\n", __FUNCTION__, address, retVal));
        return retVal;
    }

    DPrintf(0, ("[%s] failed to translate %p\n", __FUNCTION__, address));
    return 0;
}

static void *kmalloc(void *context, size_t size)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;
    PVOID retVal;

    retVal = NdisAllocateMemoryWithTagPriority(
        pContext->MiniportHandle,
        (UINT)size,
        PARANDIS_MEMORY_TAG,
        NormalPoolPriority);

    if (retVal)
    {
        NdisZeroMemory(retVal, size);
        DPrintf(6, ("[%s] returning %p, len %x\n", __FUNCTION__, retVal, (ULONG)size));
    }
    else
    {
        DPrintf(0, ("[%s] failed to allocate size %x\n", __FUNCTION__, (ULONG)size));
    }
    return retVal;
}

static void kfree(void *context, void *addr)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;

    NdisFreeMemoryWithTagPriority(pContext->MiniportHandle, addr, PARANDIS_MEMORY_TAG);
    DPrintf(6, ("[%s] freed %p\n", __FUNCTION__, addr));
}

static int PCIReadConfig(
    PPARANDIS_ADAPTER pContext,
    int where,
    void *buffer,
    size_t length)
{
    ULONG read;

    read = NdisMGetBusData(
        pContext->MiniportHandle,
        PCI_WHICHSPACE_CONFIG,
        where,
        buffer,
        (ULONG)length);

    if (read == length)
    {
        DPrintf(6, ("[%s] read %d bytes at %d\n", __FUNCTION__, read, where));
        return 0;
    }
    else
    {
        DPrintf(0, ("[%s] failed to read %d bytes at %d\n", __FUNCTION__, read, where));
        return -1;
    }
}

static int pci_read_config_byte(void *context, int where, u8 *bVal)
{
    return PCIReadConfig((PPARANDIS_ADAPTER)context, where, bVal, sizeof(*bVal));
}

static int pci_read_config_word(void *context, int where, u16 *wVal)
{
    return PCIReadConfig((PPARANDIS_ADAPTER)context, where, wVal, sizeof(*wVal));
}

static int pci_read_config_dword(void *context, int where, u32 *dwVal)
{
    return PCIReadConfig((PPARANDIS_ADAPTER)context, where, dwVal, sizeof(*dwVal));
}

static void msleep(void *context, unsigned int msecs)
{
    UNREFERENCED_PARAMETER(context);

    NdisMSleep(1000 * msecs);
}

static size_t pci_resource_len(void *context, int bar)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;

    if (bar < PCI_TYPE0_ADDRESSES) {
        return pContext->PciResources.GetBarSize(bar);
    }

    DPrintf(0, ("[%s] queried invalid BAR %d\n", __FUNCTION__, bar));
    return 0;
}

static u32 pci_resource_flags(void *context, int bar)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;

    if (bar < PCI_TYPE0_ADDRESSES)
    {
        return (pContext->PciResources.IsPortBar(bar) ? IORESOURCE_IO : IORESOURCE_MEM);
    }

    DPrintf(0, ("[%s] queried invalid BAR %d\n", __FUNCTION__, bar));
    return 0;
}

static void *pci_iomap_range(void *context, int bar, size_t offset, size_t maxlen)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;

    UNREFERENCED_PARAMETER(maxlen);

    if (bar < PCI_TYPE0_ADDRESSES)
    {
        return pContext->PciResources.GetMappedAddress(bar, (ULONG)offset);
    } 

    DPrintf(0, ("[%s] queried invalid BAR %d\n", __FUNCTION__, bar));
    return nullptr;
}

static void pci_iounmap(void *context, void *address)
{
    /* We map entire memory/IO regions on demand and unmap all of them on shutdown
     * so nothing to do here.
     */
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(address);
}

static u16 pci_get_msix_vector(void *context, int queue)
{
    /* Interrupt vectors are set up in CParaNdisAbstractPath::SetupMessageIndex,
     * no need to figure out MSI here as part of queue initialization.
     */
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(queue);

    return VIRTIO_MSI_NO_VECTOR;
}

VirtIOSystemOps ParaNdisSystemOps = {
    alloc_pages_exact,
    free_pages_exact,
    virt_to_phys,
    kmalloc,
    kfree,
    pci_read_config_byte,
    pci_read_config_word,
    pci_read_config_dword,
    pci_resource_len,
    pci_resource_flags,
    pci_get_msix_vector,
    pci_iomap_range,
    pci_iounmap,
    msleep,
};
