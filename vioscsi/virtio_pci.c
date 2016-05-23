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
 * Copyright (c) 2012-2016 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "VirtIO_PCI.h"
#include "utils.h"
#include "vioscsi.h"

/* The lower 64k of memory is never mapped so we can use the same routines
 * for both port I/O and memory access and use the address alone to decide
 * which space to use.
 */
#define PORT_MASK 0xFFFF

u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return StorPortReadRegisterUlong(NULL, (PULONG)(ulRegister));
    } else {
        return StorPortReadPortUlong(NULL, (PULONG)(ulRegister));
    }
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    if (ulRegister & ~PORT_MASK) {
        StorPortWriteRegisterUlong(NULL, (PULONG)(ulRegister), (ULONG)(ulValue));
    } else {
        StorPortWritePortUlong(NULL, (PULONG)(ulRegister), (ULONG)(ulValue));
    }
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return StorPortReadRegisterUchar(NULL, (PUCHAR)(ulRegister));
    } else {
        return StorPortReadPortUchar(NULL, (PUCHAR)(ulRegister));
    }
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    if (ulRegister & ~PORT_MASK) {
        StorPortWriteRegisterUchar(NULL, (PUCHAR)(ulRegister), (UCHAR)(bValue));
    } else {
        StorPortWritePortUchar(NULL, (PUCHAR)(ulRegister), (UCHAR)(bValue));
    }
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return StorPortReadRegisterUshort(NULL, (PUSHORT)(ulRegister));
    } else {
        return StorPortReadPortUshort(NULL, (PUSHORT)(ulRegister));
    }
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    if (ulRegister & ~PORT_MASK) {
        StorPortWriteRegisterUshort(NULL, (PUSHORT)(ulRegister), (USHORT)(wValue));
    } else {
        StorPortWritePortUshort(NULL, (PUSHORT)(ulRegister), (USHORT)(wValue));
    }
}

static void *alloc_pages_exact(void *context, size_t size)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    PVOID ptr = (PVOID)((ULONG_PTR)adaptExt->pageAllocationVa + adaptExt->pageOffset);

    if ((adaptExt->pageOffset + size) <= adaptExt->pageAllocationSize) {
        size = ROUND_TO_PAGES(size);
        adaptExt->pageOffset += size;
        RtlZeroMemory(ptr, size);
        return ptr;
    } else {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Ran out of memory in alloc_pages_exact(%Id)\n", size));
        return NULL;
    }
}

static void free_pages_exact(void *context, void *virt)
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(virt);

    /* We allocate pages from a single uncached extension by simply moving the
     * adaptExt->allocationOffset pointer forward. Nothing to do here.
     */
}

static ULONGLONG virt_to_phys(void *context, void *address)
{
    ULONG uLength;
    STOR_PHYSICAL_ADDRESS pa = StorPortGetPhysicalAddress(context, NULL, address, &uLength);
    return pa.QuadPart;
}

static void *kmalloc(void *context, size_t size)
{
    return VioScsiPoolAlloc(context, size);
}

static void kfree(void *context, void *addr)
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

static size_t pci_resource_len(void *context, int bar)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    if (bar < PCI_TYPE0_ADDRESSES) {
        return adaptExt->pci_bars[bar].uLength;
    }
    return 0;
}

static u32 pci_resource_flags(void *context, int bar)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    if (bar < PCI_TYPE0_ADDRESSES) {
        return (adaptExt->pci_bars[bar].bPortSpace ? IORESOURCE_IO : IORESOURCE_MEM);
    }
    return 0;
}

static void *pci_iomap_range(void *context, int bar, size_t offset, size_t maxlen)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    if (bar < PCI_TYPE0_ADDRESSES) {
        PVIRTIO_BAR pBar = &adaptExt->pci_bars[bar];
        if (pBar->pBase == NULL) {
            pBar->pBase = StorPortGetDeviceBase(
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

static void pci_iounmap(void *context, void *address)
{
    /* We map entire memory/IO regions on demand and the storage port driver
     * unmaps all of them on shutdown so nothing to do here.
     */
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(address);
}

static u16 pci_get_msix_vector(void *context, int queue)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    u16 vector;

    if (queue >= 0) {
        /* queue interrupt */
        if (adaptExt->msix_enabled) {
            vector = queue + 1;
        } else {
            vector = VIRTIO_MSI_NO_VECTOR;
        }
    } else {
        /* on-device-config-change interrupt */
        vector = VIRTIO_MSI_NO_VECTOR;
    }

    return vector;
}

static void msleep(void *context, unsigned int msecs)
{
    UNREFERENCED_PARAMETER(context);

    /* We can't really sleep in a storage miniport so we just busy wait. */
    KeStallExecutionProcessor(1000 * msecs);
}

VirtIOSystemOps VioScsiSystemOps = {
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
