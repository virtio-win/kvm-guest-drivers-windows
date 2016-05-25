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
 *  StorPort/ScsiPort code adjustment Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "VirtIO_PCI.h"
#include "virtio_stor_utils.h"
#include "virtio_stor.h"

/* The lower 64k of memory is never mapped so we can use the same routines
 * for both port I/O and memory access and use the address alone to decide
 * which space to use.
 */
#define PORT_MASK 0xFFFF

static u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return ScsiPortReadRegisterUlong((PULONG)(ulRegister));
    } else {
        return ScsiPortReadPortUlong((PULONG)(ulRegister));
    }
}

static void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    if (ulRegister & ~PORT_MASK) {
        ScsiPortWriteRegisterUlong((PULONG)(ulRegister), (ULONG)(ulValue));
    } else {
        ScsiPortWritePortUlong((PULONG)(ulRegister), (ULONG)(ulValue));
    }
}

static u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return ScsiPortReadRegisterUchar((PUCHAR)(ulRegister));
    } else {
        return ScsiPortReadPortUchar((PUCHAR)(ulRegister));
    }
}

static void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    if (ulRegister & ~PORT_MASK) {
        ScsiPortWriteRegisterUchar((PUCHAR)(ulRegister), (UCHAR)(bValue));
    } else {
        ScsiPortWritePortUchar((PUCHAR)(ulRegister), (UCHAR)(bValue));
    }
}

static u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return ScsiPortReadRegisterUshort((PUSHORT)(ulRegister));
    } else {
        return ScsiPortReadPortUshort((PUSHORT)(ulRegister));
    }
}

static void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    if (ulRegister & ~PORT_MASK) {
        ScsiPortWriteRegisterUshort((PUSHORT)(ulRegister), (USHORT)(wValue));
    } else {
        ScsiPortWritePortUshort((PUSHORT)(ulRegister), (USHORT)(wValue));
    }
}

static void *mem_alloc_contiguous_pages(void *context, size_t size)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    PVOID ptr = (PVOID)((ULONG_PTR)adaptExt->pageAllocationVa + adaptExt->pageOffset);

    if ((adaptExt->pageOffset + size) <= adaptExt->pageAllocationSize) {
        size = ROUND_TO_PAGES(size);
        adaptExt->pageOffset += size;
        RtlZeroMemory(ptr, size);
        return ptr;
    } else {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Ran out of memory in %s(%Id)\n", __FUNCTION__, size));
        return NULL;
    }
}

static void mem_free_contiguous_pages(void *context, void *virt)
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(virt);

    /* We allocate pages from a single uncached extension by simply moving the
     * adaptExt->allocationOffset pointer forward. Nothing to do here.
     */
}

static ULONGLONG mem_get_physical_address(void *context, void *virt)
{
    ULONG uLength;
    SCSI_PHYSICAL_ADDRESS pa = ScsiPortGetPhysicalAddress(context, NULL, virt, &uLength);
    return pa.QuadPart;
}

static void *mem_alloc_nonpaged_block(void *context, size_t size)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    PVOID ptr = (PVOID)((ULONG_PTR)adaptExt->poolAllocationVa + adaptExt->poolOffset);

    if ((adaptExt->poolOffset + size) <= adaptExt->poolAllocationSize) {
        adaptExt->poolOffset += size;
        RtlZeroMemory(ptr, size);
        return ptr;
    } else {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Ran out of memory in %s(%Id)\n", __FUNCTION__, size));
        return NULL;
    }
}

static void mem_free_nonpaged_block(void *context, void *addr)
{
    /* We allocate memory from a single non-paged pool allocation by simply moving
     * the adaptExt->poolOffset pointer forward. Nothing to do here.
     */
}

static int pci_read_config_byte(void *context, int where, u8 *bVal)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    *bVal = adaptExt->pci_config_buf[where];
    return 0;
}

static int pci_read_config_word(void *context, int where, u16 *wVal)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    *wVal = *(u16 *)&adaptExt->pci_config_buf[where];
    return 0;
}

static int pci_read_config_dword(void *context, int where, u32 *dwVal)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    *dwVal = *(u32 *)&adaptExt->pci_config_buf[where];
    return 0;
}

static size_t pci_get_resource_len(void *context, int bar)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    if (bar < PCI_TYPE0_ADDRESSES) {
        return adaptExt->pci_bars[bar].uLength;
    }
    return 0;
}

static void *pci_map_address_range(void *context, int bar, size_t offset, size_t maxlen)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    if (bar < PCI_TYPE0_ADDRESSES) {
        PVIRTIO_BAR pBar = &adaptExt->pci_bars[bar];
        if (pBar->pBase == NULL) {
#ifndef USE_STORPORT
            if (!ScsiPortValidateRange(
                adaptExt,
                PCIBus,
                adaptExt->system_io_bus_number,
                pBar->BasePA,
                pBar->uLength,
                !!pBar->bPortSpace)) {
                LogError(adaptExt,
                        SP_INTERNAL_ADAPTER_ERROR,
                        __LINE__);

                RhelDbgPrint(TRACE_LEVEL_FATAL, ("Range validation failed %I64x for %x bytes\n",
                            pBar->BasePA.QuadPart,
                            pBar->uLength));

                return NULL;
            }
#endif
            pBar->pBase = ScsiPortGetDeviceBase(
                adaptExt,
                PCIBus,
                adaptExt->system_io_bus_number,
                pBar->BasePA,
                pBar->uLength,
                !!pBar->bPortSpace);
        }
        if (pBar->pBase != NULL && offset < pBar->uLength) {
            return (PUCHAR)pBar->pBase + offset;
        }
    }
    return NULL;
}

static void pci_unmap_address_range(void *context, void *address)
{
    /* We map entire memory/IO regions on demand and the storage port driver
     * unmaps all of them on shutdown so nothing to do here.
     */
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(address);
}

static u16 vdev_get_msix_vector(void *context, int queue)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    u16 vector;

    if (!adaptExt->dump_mode && adaptExt->msix_vectors > 1) {
        if (queue >= 0) {
            /* queue interrupt */
            vector = (u16)(adaptExt->msix_vectors - 1);
        } else {
            /* on-device-config-change interrupt */
            vector = 0;
        }
    }
    else {
        vector = VIRTIO_MSI_NO_VECTOR;
    }

    return vector;
}

static void vdev_sleep(void *context, unsigned int msecs)
{
    UNREFERENCED_PARAMETER(context);

    /* We can't really sleep in a storage miniport so we just busy wait. */
    KeStallExecutionProcessor(1000 * msecs);
}

VirtIOSystemOps VioStorSystemOps = {
    .vdev_read_byte = ReadVirtIODeviceByte,
    .vdev_read_word = ReadVirtIODeviceWord,
    .vdev_read_dword = ReadVirtIODeviceRegister,
    .vdev_write_byte = WriteVirtIODeviceByte,
    .vdev_write_word = WriteVirtIODeviceWord,
    .vdev_write_dword = WriteVirtIODeviceRegister,
    .mem_alloc_contiguous_pages = mem_alloc_contiguous_pages,
    .mem_free_contiguous_pages = mem_free_contiguous_pages,
    .mem_get_physical_address = mem_get_physical_address,
    .mem_alloc_nonpaged_block = mem_alloc_nonpaged_block,
    .mem_free_nonpaged_block = mem_free_nonpaged_block,
    .pci_read_config_byte = pci_read_config_byte,
    .pci_read_config_word = pci_read_config_word,
    .pci_read_config_dword = pci_read_config_dword,
    .pci_get_resource_len = pci_get_resource_len,
    .pci_map_address_range = pci_map_address_range,
    .pci_unmap_address_range = pci_unmap_address_range,
    .vdev_get_msix_vector = vdev_get_msix_vector,
    .vdev_sleep = vdev_sleep,
};
