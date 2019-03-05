/*
 * Virtio PCI driver
 *
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Windows porting - Yan Vugenfirer <yvugenfi@redhat.com>
 *  StorPort/ScsiPort code adjustment Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "osdep.h"
#include "virtio_pci.h"
//#include "utils.h"
//#include "vioscsi.h"
#include "helper.h"
#include "trace.h"

#if defined(EVENT_TRACING)
#include "virtio_pci.tmh"
#endif

/* The lower 64k of memory is never mapped so we can use the same routines
 * for both port I/O and memory access and use the address alone to decide
 * which space to use.
 */
#define PORT_MASK 0xFFFF

static u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return StorPortReadRegisterUlong(NULL, (PULONG)(ulRegister));
    } else {
        return StorPortReadPortUlong(NULL, (PULONG)(ulRegister));
    }
}

static void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    if (ulRegister & ~PORT_MASK) {
        StorPortWriteRegisterUlong(NULL, (PULONG)(ulRegister), (ULONG)(ulValue));
    } else {
        StorPortWritePortUlong(NULL, (PULONG)(ulRegister), (ULONG)(ulValue));
    }
}

static u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return StorPortReadRegisterUchar(NULL, (PUCHAR)(ulRegister));
    } else {
        return StorPortReadPortUchar(NULL, (PUCHAR)(ulRegister));
    }
}

static void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    if (ulRegister & ~PORT_MASK) {
        StorPortWriteRegisterUchar(NULL, (PUCHAR)(ulRegister), (UCHAR)(bValue));
    } else {
        StorPortWritePortUchar(NULL, (PUCHAR)(ulRegister), (UCHAR)(bValue));
    }
}

static u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return StorPortReadRegisterUshort(NULL, (PUSHORT)(ulRegister));
    } else {
        return StorPortReadPortUshort(NULL, (PUSHORT)(ulRegister));
    }
}

static void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    if (ulRegister & ~PORT_MASK) {
        StorPortWriteRegisterUshort(NULL, (PUSHORT)(ulRegister), (USHORT)(wValue));
    } else {
        StorPortWritePortUshort(NULL, (PUSHORT)(ulRegister), (USHORT)(wValue));
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
        RhelDbgPrint(TRACE_LEVEL_FATAL, " Ran out of memory in alloc_pages_exact(%Id)\n", size);
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
    STOR_PHYSICAL_ADDRESS pa = StorPortGetPhysicalAddress(context, NULL, virt, &uLength);
    return pa.QuadPart;
}

static void *mem_alloc_nonpaged_block(void *context, size_t size)
{
    return VioScsiPoolAlloc(context, size);
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

static u16 vdev_get_msix_vector(void *context, int queue)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)context;
    u16 vector;

    if (queue >= 0) {
        /* queue interrupt */
        if (adaptExt->msix_enabled) {
            if (adaptExt->msix_one_vector) {
                vector = 0;
            } else {
                vector = QUEUE_TO_MESSAGE(queue);
            }
        } else {
            vector = VIRTIO_MSI_NO_VECTOR;
        }
    } else {
        /* on-device-config-change interrupt */
        vector = VIRTIO_MSI_NO_VECTOR;
    }

    return vector;
}

static void vdev_sleep(void *context, unsigned int msecs)
{
    UNREFERENCED_PARAMETER(context);

    /* We can't really sleep in a storage miniport so we just busy wait. */
    StorPortStallExecution(1000 * msecs);
}

VirtIOSystemOps VioScsiSystemOps = {
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
    .vdev_get_msix_vector = vdev_get_msix_vector,
    .vdev_sleep = vdev_sleep,
};
