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
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "VirtIO.h"
#include "VirtIO_PCI.h"
#include "VirtIO_Ring.h"
#include "kdebugprint.h"
#include "virtio_stor_utils.h"
#include "virtio_stor.h"

/*
bool
VirtIODeviceGetHostFeature(
    IN PVOID DeviceExtension,
    unsigned uFeature)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s\n", __FUNCTION__));

    return !!(ScsiPortReadPortUlong((PULONG)(adaptExt->device_base + VIRTIO_PCI_HOST_FEATURES)) & (1 << uFeature));
}

VOID
VirtIODeviceReset(
    IN PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s\n", __FUNCTION__));

    ScsiPortWritePortUchar((PUCHAR)(adaptExt->device_base + VIRTIO_PCI_STATUS),(UCHAR)0);
}

VOID
VirtIODeviceGet(
    IN PVOID DeviceExtension,
    unsigned offset,
    PVOID buf,
    unsigned len)
{
    unsigned           i;
    ULONG_PTR          ioaddr;
    u8*                ptr = (u8*)buf;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s\n", __FUNCTION__));

    ioaddr = adaptExt->device_base + VIRTIO_PCI_CONFIG(adaptExt->msix_enabled) + offset;

    for (i = 0; i < len; i++) {
        ptr[i] = ScsiPortReadPortUchar((PUCHAR)(ioaddr + i));
    }
}

UCHAR
VirtIODeviceISR(
    IN PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s\n", __FUNCTION__));

    return ScsiPortReadPortUchar((PUCHAR)(adaptExt->device_base + VIRTIO_PCI_ISR));
}


// the notify function used when creating a virt queue /
static
VOID
vp_notify(
    IN struct virtqueue *vq)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)vq->DeviceExtension;
    struct virtio_pci_vq_info *info = vq->priv;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s>> queue %d\n", __FUNCTION__, info->queue_index));

    // we write the queue's selector into the notification register to
    // (*) signal the other end
    ScsiPortWritePortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_NOTIFY),(USHORT)(info->queue_index));
}
*/


u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    return ScsiPortReadPortUlong((PULONG)(ulRegister));
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    ScsiPortWritePortUlong( (PULONG)(ulRegister),(ULONG)(ulValue) );
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    return ScsiPortReadPortUchar((PUCHAR)(ulRegister));
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    ScsiPortWritePortUchar((PUCHAR)(ulRegister),(UCHAR)(bValue));
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    return ScsiPortReadPortUshort((PUSHORT)(ulRegister));
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    ScsiPortWritePortUshort((PUSHORT)(ulRegister),(USHORT)(wValue));
}

ULONG_PTR GetVirtIODeviceAddr( PVOID pVirtIODevice )
{
    return ((PADAPTER_EXTENSION)pVirtIODevice)->device_base ;
}

void SetVirtIODeviceAddr(PVOID pVirtIODevice, ULONG_PTR addr)
{
    ((PADAPTER_EXTENSION)pVirtIODevice)->device_base = addr;
}

int GetPciConfig(PVOID pVirtIODevice)
{
    return VIRTIO_PCI_CONFIG_STOR(((PADAPTER_EXTENSION)pVirtIODevice)->msix_enabled);
}

PVOID drv_alloc_needed_mem(PVOID vdev, PVOID Context,
						   PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
						   ULONG size, pmeminfo pmi)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(allocmem);
    UNREFERENCED_PARAMETER(size);
    UNREFERENCED_PARAMETER(pmi);

    return ((PADAPTER_EXTENSION)vdev)->virtqueue;
}

PHYSICAL_ADDRESS GetPhysicalAddress(PVOID addr)
{
    return MmGetPhysicalAddress(addr);
}


struct
virtqueue*
VirtIODeviceFindVirtualQueue_InDrv(
    IN PVOID DeviceExtension,
    IN unsigned index,
    IN unsigned vector,
    bool (*callback)(struct virtqueue *vq),
    PVOID Context,											                      PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
    VOID (*freemem)(PVOID Context, PVOID Address, pmeminfo pmi),
    BOOLEAN Cached, BOOLEAN bPhysical)
{
    struct virtio_pci_vq_info *info;
    struct virtqueue   *vq;
    u16                num;
    ULONG              dummy;
    PHYSICAL_ADDRESS   pa;
    ULONG              pageNum;
    ULONG              pfns;
    unsigned           res;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    UNREFERENCED_PARAMETER(freemem);
    UNREFERENCED_PARAMETER(Cached);
    UNREFERENCED_PARAMETER(bPhysical);


    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s  index = %d, vector = %d\n", __FUNCTION__, index, vector));

    if(vector) {
        ScsiPortWritePortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_MSI_CONFIG_VECTOR),(USHORT)0);
        res = ScsiPortReadPortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_MSI_CONFIG_VECTOR));
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> VIRTIO_MSI_CONFIG_VECTOR res = 0x%x\n", __FUNCTION__, res));
        if(res == VIRTIO_MSI_NO_VECTOR) {
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> Cannot find config vector res = 0x%x\n", __FUNCTION__, res));
           return NULL;
        }
    }

    // Select the queue we're interested in
    ScsiPortWritePortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL),(USHORT)index);

    // Check if queue is either not available or already active.
    num = ScsiPortReadPortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_NUM));
    pfns = ScsiPortReadPortUlong((PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN));
    if (!num || pfns) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>>> num = 0x%x, pfns= 0x%x\n", __FUNCTION__, num, pfns) );
        return NULL;
    }
    // allocate and fill out our structure the represents an active queue
    info = &adaptExt->pci_vq_info;

    info->queue_index = index;
    info->num = num;

    RhelDbgPrint(TRACE_LEVEL_FATAL, ("[%s] info = %p, info->queue = %p\n", __FUNCTION__, info, info->queue) );
    // create the vring
    memset(info->queue, 0, vring_size(num,PAGE_SIZE));
    vq = vring_new_virtqueue(info->num,
                             DeviceExtension,
                             info->queue,
                             vp_notify,
                             callback,
                             Context, allocmem);

    if (!vq) {
        ScsiPortWritePortUlong((PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(ULONG)0);
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>>> vring_new_virtqueue failed\n", __FUNCTION__) );
        return NULL;
    }

    vq->priv = info;
    info->vq = vq;

    // activate the queue
    pa = ScsiPortGetPhysicalAddress(DeviceExtension, NULL, info->queue, &dummy);
    pageNum = (ULONG)(pa.QuadPart >> PAGE_SHIFT);
    RhelDbgPrint(TRACE_LEVEL_FATAL, ("[%s] queue phys.address %08lx:%08lx, pfn %lx\n", __FUNCTION__, pa.u.HighPart, pa.u.LowPart, pageNum));
    ScsiPortWritePortUlong((PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(ULONG)(pageNum));

    if(vector) {
        ScsiPortWritePortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_MSI_QUEUE_VECTOR),(USHORT)vector);
        res = ScsiPortReadPortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_MSI_QUEUE_VECTOR));
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> VIRTIO_MSI_QUEUE_VECTOR vector = %d, res = 0x%x\n", __FUNCTION__, vector, res));
        if(res == VIRTIO_MSI_NO_VECTOR) {
           ScsiPortWritePortUlong((PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(ULONG)0);
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> Cannot create vq vector\n", __FUNCTION__));
           return NULL;
        }
    }

    return vq;
}

/*
void
VirtIODeviceDeleteVirtualQueue(
    IN struct virtqueue *vq)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)vq->DeviceExtension;
    struct virtio_pci_vq_info *info = (struct virtio_pci_vq_info *)vq->priv;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s\n", __FUNCTION__));

    // Select and deactivate the queue
    ScsiPortWritePortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL),(USHORT)(info->queue_index));

    ScsiPortWritePortUlong((PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(ULONG)0);
}
*/
