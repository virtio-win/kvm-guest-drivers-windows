/*
 * Virtio PCI driver
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Windows porting - Yan Vugenfirer <yvugenfi@redhat.com>
 *  WDDM porting - Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
 /**********************************************************************
  * Copyright (c) 2012-2020 Red Hat, Inc.
  *
  * This work is licensed under the terms of the GNU GPL, version 2.  See
  * the COPYING file in the top-level directory.
  *
 **********************************************************************/
#include "helper.h"
#include "viogpu.h"
#include "..\viogpudo\viogpudo.h"
#if !DBG
#include "viogpu_pci.tmh"
#endif


u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return READ_REGISTER_ULONG((PULONG)(ulRegister));
    }
    else {
        return READ_PORT_ULONG((PULONG)(ulRegister));
    }
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    if (ulRegister & ~PORT_MASK) {
        WRITE_REGISTER_ULONG((PULONG)(ulRegister), (ULONG)(ulValue));
    }
    else {
        WRITE_PORT_ULONG((PULONG)(ulRegister), (ULONG)(ulValue));
    }
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return READ_REGISTER_UCHAR((PUCHAR)(ulRegister));
    }
    else {
        return READ_PORT_UCHAR((PUCHAR)(ulRegister));
    }
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    if (ulRegister & ~PORT_MASK) {
        WRITE_REGISTER_UCHAR((PUCHAR)(ulRegister), (UCHAR)(bValue));
    }
    else {
        WRITE_PORT_UCHAR((PUCHAR)(ulRegister), (UCHAR)(bValue));
    }
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return READ_REGISTER_USHORT((PUSHORT)(ulRegister));
    }
    else {
        return READ_PORT_USHORT((PUSHORT)(ulRegister));
    }
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    if (ulRegister & ~PORT_MASK) {
        WRITE_REGISTER_USHORT((PUSHORT)(ulRegister), (USHORT)(wValue));
    }
    else {
        WRITE_PORT_USHORT((PUSHORT)(ulRegister), (USHORT)(wValue));
    }
}

void *mem_alloc_contiguous_pages(void *context, size_t size)
{
    PHYSICAL_ADDRESS HighestAcceptable;
    PVOID ptr = NULL;

    UNREFERENCED_PARAMETER(context);

    HighestAcceptable.QuadPart = 0xFFFFFFFFFF;
    ptr = MmAllocateContiguousMemory(size, HighestAcceptable);
    if (ptr) {
        RtlZeroMemory(ptr, size);
    }
    else {
        DbgPrint(TRACE_LEVEL_FATAL, ("Ran out of memory in alloc_pages_exact(%Id)\n", size));
    }
    return ptr;
}

void mem_free_contiguous_pages(void *context, void *virt)
{
    UNREFERENCED_PARAMETER(context);
    if (virt) {
        MmFreeContiguousMemory(virt);
    }
}

ULONGLONG mem_get_physical_address(void *context, void *virt)
{
    UNREFERENCED_PARAMETER(context);

    PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(virt);
    return pa.QuadPart;
}

void *mem_alloc_nonpaged_block(void *context, size_t size)
{
    UNREFERENCED_PARAMETER(context);
    PVOID ptr = ExAllocatePoolWithTag(
        NonPagedPoolNx,
        size,
        VIOGPUTAG);
    if (ptr) {
        RtlZeroMemory(ptr, size);
    }
    else {
        DbgPrint(TRACE_LEVEL_FATAL, ("Ran out of memory in alloc_pages_exact(%Id)\n", size));
    }
    return ptr;
}

void mem_free_nonpaged_block(void *context, void *addr)
{
    UNREFERENCED_PARAMETER(context);
    if (addr) {
        ExFreePoolWithTag(
            addr,
            VIOGPUTAG);
    }
}

PAGED_CODE_SEG_BEGIN
static int PCIReadConfig(
    VioGpuAdapter* pdev,
    int where,
    void *buffer,
    size_t length)
{
    PAGED_CODE();

    NTSTATUS Status;
    VioGpuDod* pVioGpu = pdev->GetVioGpu();
    PDXGKRNL_INTERFACE pDxgkInterface = pVioGpu->GetDxgkInterface();
    ULONG BytesRead = 0;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    Status = pDxgkInterface->DxgkCbReadDeviceSpace(pDxgkInterface->DeviceHandle,
        DXGK_WHICHSPACE_CONFIG,
        buffer,
        where,
        (ULONG)length,
        &BytesRead);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbReadDeviceSpace failed with status 0x%X\n", Status));
        return -1;
    }
    if (BytesRead != length)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("[%s] read %d bytes at %d\n", __FUNCTION__, BytesRead, where));
        return -1;
    }
    return 0;
}

static int pci_read_config_byte(void *context, int where, u8 *bVal)
{
    PAGED_CODE();
    VioGpuAdapter* pdev = static_cast<VioGpuAdapter*>(context);
    return PCIReadConfig(pdev, where, bVal, sizeof(*bVal));
}

int pci_read_config_word(void *context, int where, u16 *wVal)
{
    PAGED_CODE();
    VioGpuAdapter* pdev = static_cast<VioGpuAdapter*>(context);
    return PCIReadConfig(pdev, where, wVal, sizeof(*wVal));
}

int pci_read_config_dword(void *context, int where, u32 *dwVal)
{
    PAGED_CODE();
    VioGpuAdapter* pdev = static_cast<VioGpuAdapter*>(context);
    return PCIReadConfig(pdev, where, dwVal, sizeof(*dwVal));
}
PAGED_CODE_SEG_END

size_t pci_get_resource_len(void *context, int bar)
{
    VioGpuAdapter* pdev = static_cast<VioGpuAdapter*>(context);
    return pdev->GetPciResources()->GetBarSize(bar);
}

void *pci_map_address_range(void *context, int bar, size_t offset, size_t maxlen)
{
    UNREFERENCED_PARAMETER(maxlen);

    VioGpuAdapter* pdev = static_cast<VioGpuAdapter*>(context);
    return pdev->GetPciResources()->GetMappedAddress(bar, (ULONG)offset);
}

u16 vdev_get_msix_vector(void *context, int queue)
{
    VioGpuAdapter* pdev = static_cast<VioGpuAdapter*>(context);
    u16 vector = VIRTIO_MSI_NO_VECTOR;

    if (queue >= 0) {
        /* queue interrupt */
        if (pdev->IsMSIEnabled()) {
            vector = (u16)(queue + 1);
        }
    }
    else {
        vector = VIRTIO_GPU_MSIX_CONFIG_VECTOR;
    }
    return vector;
}

void vdev_sleep(void *context, unsigned int msecs)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    UNREFERENCED_PARAMETER(context);

    if (KeGetCurrentIrql() <= APC_LEVEL) {
        LARGE_INTEGER delay;
        delay.QuadPart = Int32x32To64(msecs, -10000);
        status = KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }

    if (!NT_SUCCESS(status)) {
        KeStallExecutionProcessor(1000 * msecs);
    }
}


VirtIOSystemOps VioGpuSystemOps = {
    ReadVirtIODeviceByte,
    ReadVirtIODeviceWord,
    ReadVirtIODeviceRegister,
    WriteVirtIODeviceByte,
    WriteVirtIODeviceWord,
    WriteVirtIODeviceRegister,
    mem_alloc_contiguous_pages,
    mem_free_contiguous_pages,
    mem_get_physical_address,
    mem_alloc_nonpaged_block,
    mem_free_nonpaged_block,
    pci_read_config_byte,
    pci_read_config_word,
    pci_read_config_dword,
    pci_get_resource_len,
    pci_map_address_range,
    vdev_get_msix_vector,
    vdev_sleep,
};


PVOID CPciBar::GetVA(PDXGKRNL_INTERFACE pDxgkInterface)
{
    NTSTATUS Status;
    if (m_BaseVA == nullptr)
    {
        if (m_bPortSpace)
        {
            if (m_bIoMapped)
            {
                Status = pDxgkInterface->DxgkCbMapMemory(pDxgkInterface->DeviceHandle,
                    m_BasePA,
                    m_uSize,
                    TRUE,
                    FALSE,
                    MmNonCached,
                    &m_BaseVA
                );
                if (Status == STATUS_SUCCESS)
                {
                    DbgPrint(TRACE_LEVEL_VERBOSE, ("[%s] mapped port BAR at %x\n", __FUNCTION__, m_BasePA.LowPart));
                }
                else
                {
                    m_BaseVA = nullptr;
                    DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbMapMemor (CmResourceTypePort) failed with status 0x%X\n", Status));
                }
            }
            else
            {
                m_BaseVA = (PUCHAR)(ULONG_PTR)m_BasePA.QuadPart;
            }
        }
        else
        {
            Status = pDxgkInterface->DxgkCbMapMemory(pDxgkInterface->DeviceHandle,
                m_BasePA,
                m_uSize,
                FALSE,
                FALSE,
                MmNonCached,
                &m_BaseVA
            );
            if (Status == STATUS_SUCCESS)
            {
                DbgPrint(TRACE_LEVEL_VERBOSE, ("[%s] mapped memory BAR at %I64x\n", __FUNCTION__, m_BasePA.QuadPart));
            }
            else
            {
                m_BaseVA = nullptr;
                DbgPrint(TRACE_LEVEL_ERROR, ("[%s] failed to map memory BAR at %I64x\n", __FUNCTION__, m_BasePA.QuadPart));
            }
        }
    }
    return m_BaseVA;
}

void CPciBar::Unmap(PDXGKRNL_INTERFACE pDxgkInterface)
{
    if (m_BaseVA != nullptr)
    {
        if (!m_bIoMapped)
        {
            pDxgkInterface->DxgkCbUnmapMemory(pDxgkInterface->DeviceHandle, m_BaseVA);
        }
    }
    m_BaseVA = nullptr;
}

bool CPciResources::Init(PDXGKRNL_INTERFACE pDxgkInterface, PCM_RESOURCE_LIST pResList)
{
    PCI_COMMON_HEADER pci_config = { 0 };
    ULONG BytesRead = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    bool interrupt_found = false;
    int bar = -1;

    m_pDxgkInterface = pDxgkInterface;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    Status = m_pDxgkInterface->DxgkCbReadDeviceSpace(m_pDxgkInterface->DeviceHandle,
        DXGK_WHICHSPACE_CONFIG,
        &pci_config,
        0,
        sizeof(pci_config),
        &BytesRead);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s BytesRead = %d\n", __FUNCTION__, BytesRead));

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbReadDeviceSpace failed with status 0x%X\n", Status));
        return false;
    }

    if (BytesRead != sizeof(pci_config))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("[%s] could not read PCI config space\n", __FUNCTION__));
        return false;
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s ListCount = %d\n", __FUNCTION__, pResList->Count));

    for (ULONG i = 0; i < pResList->Count; ++i)
    {
        PCM_FULL_RESOURCE_DESCRIPTOR pFullResDescriptor = &pResList->List[i];

        for (ULONG j = 0; j < pFullResDescriptor->PartialResourceList.Count; ++j)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDescriptor = &pFullResDescriptor->PartialResourceList.PartialDescriptors[j];
            switch (pResDescriptor->Type)
            {
            case CmResourceTypePort:
            {
                DbgPrint(TRACE_LEVEL_FATAL, ("CmResourceTypePort\n"));
                break;
            }
            break;
            case CmResourceTypeInterrupt:
            {
                m_InterruptFlags = pResDescriptor->Flags;
                if (IsMSIEnabled())
                {
                    DbgPrint(TRACE_LEVEL_FATAL, ("Found MSI Interrupt vector %d, level %d, affinity 0x%X, flags %X\n",
                        pResDescriptor->u.MessageInterrupt.Translated.Vector,
                        pResDescriptor->u.MessageInterrupt.Translated.Level,
                        (ULONG)pResDescriptor->u.MessageInterrupt.Translated.Affinity,
                        pResDescriptor->Flags));
                }
                else
                {
                    DbgPrint(TRACE_LEVEL_FATAL, ("Found Interrupt vector %d, level %d, affinity 0x%X, flags %X\n",
                        pResDescriptor->u.Interrupt.Vector,
                        pResDescriptor->u.Interrupt.Level,
                        (ULONG)pResDescriptor->u.Interrupt.Affinity,
                        pResDescriptor->Flags));
                }
                interrupt_found = true;
            }
            break;
            case CmResourceTypeMemory:
            {
                PHYSICAL_ADDRESS Start = pResDescriptor->u.Port.Start;
                ULONG len = pResDescriptor->u.Port.Length;
                bar = virtio_get_bar_index(&pci_config, Start);
                DbgPrint(TRACE_LEVEL_FATAL, ("Found IO memory at %08I64X(%d) bar %d\n", Start.QuadPart, len, bar));
                if (bar < 0)
                {
                    break;
                }
                m_Bars[bar] = CPciBar(Start, len, false, true);
            }
            break;
            case CmResourceTypeDma:
                DbgPrint(TRACE_LEVEL_FATAL, ("Dma\n"));
                break;
            case CmResourceTypeDeviceSpecific:
                DbgPrint(TRACE_LEVEL_FATAL, ("Device Specific\n"));
                break;
            case CmResourceTypeBusNumber:
                DbgPrint(TRACE_LEVEL_FATAL, ("Bus number\n"));
                break;
            default:
                DbgPrint(TRACE_LEVEL_ERROR, ("Unsupported descriptor type = %d\n", pResDescriptor->Type));
                break;
            }
        }
    }
    if (bar < 0 || !interrupt_found)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("[%s] resource enumeration failed\n", __FUNCTION__));
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
        BaseVA = m_Bars[bar].GetVA(m_pDxgkInterface);
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
        DbgPrint(TRACE_LEVEL_ERROR, ("[%s] failed to map BAR %d, offset %x\n", __FUNCTION__, bar, uOffset));
        return nullptr;
    }
}

PAGED_CODE_SEG_BEGIN
NTSTATUS
MapFrameBuffer(
    _In_                       PHYSICAL_ADDRESS    PhysicalAddress,
    _In_                       ULONG               Length,
    _Outptr_result_bytebuffer_(Length) VOID**      VirtualAddress)
{
    PAGED_CODE();

    if ((PhysicalAddress.QuadPart == (ULONGLONG)0) ||
        (Length == 0) ||
        (VirtualAddress == NULL))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("One of PhysicalAddress.QuadPart (0x%I64x), Length (%lu), VirtualAddress (%p) is NULL or 0\n",
            PhysicalAddress.QuadPart, Length, VirtualAddress));
        return STATUS_INVALID_PARAMETER;
    }

    *VirtualAddress = MmMapIoSpace(PhysicalAddress,
        Length,
        MmWriteCombined);
    if (*VirtualAddress == NULL)
    {

        *VirtualAddress = MmMapIoSpace(PhysicalAddress,
            Length,
            MmNonCached);
        if (*VirtualAddress == NULL)
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("MmMapIoSpace returned a NULL buffer when trying to allocate %lu bytes", Length));
            return STATUS_NO_MEMORY;
        }
    }

    DbgPrint(TRACE_LEVEL_FATAL, ("%s PhysicalAddress.QuadPart (0x%I64x), Length (%lu), VirtualAddress (%p)\n",
        __FUNCTION__, PhysicalAddress.QuadPart, Length, VirtualAddress));
    return STATUS_SUCCESS;
}

NTSTATUS
UnmapFrameBuffer(
    _In_reads_bytes_(Length) VOID* VirtualAddress,
    _In_                ULONG Length)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_FATAL, ("%s Length (%lu), VirtualAddress (%p)\n",
        __FUNCTION__, Length, VirtualAddress));
    if ((VirtualAddress == NULL) && (Length == 0))
    {
        return STATUS_SUCCESS;
    }
    else if ((VirtualAddress == NULL) || (Length == 0))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("Only one of Length (%lu), VirtualAddress (%p) is NULL or 0",
            Length, VirtualAddress));
        return STATUS_INVALID_PARAMETER;
    }

    MmUnmapIoSpace(VirtualAddress, Length);

    return STATUS_SUCCESS;
}
PAGED_CODE_SEG_END
