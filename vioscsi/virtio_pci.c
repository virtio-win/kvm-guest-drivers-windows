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
#include "VirtIO_PCI.h"
#include "VirtIO.h"
#include "VirtIO_Ring.h"
#include "utils.h"
#include "vioscsi.h"


bool
VirtIODeviceGetHostFeature(
    IN PVOID DeviceExtension,
    unsigned uFeature)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

//    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s\n", __FUNCTION__));

    return !!(StorPortReadPortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_HOST_FEATURES)) & (1 << uFeature));
}

VOID
VirtIODeviceReset(
    IN PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s\n", __FUNCTION__));

    StorPortWritePortUchar(DeviceExtension, (PUCHAR)(adaptExt->device_base + VIRTIO_PCI_STATUS),(UCHAR)0);
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
        ptr[i] = StorPortReadPortUchar(DeviceExtension, (PUCHAR)(ioaddr + i));
    }
}

UCHAR
VirtIODeviceISR(
    IN PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s\n", __FUNCTION__));

    return StorPortReadPortUchar(DeviceExtension, (PUCHAR)(adaptExt->device_base + VIRTIO_PCI_ISR));
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
    // * signal the other end
    StorPortWritePortUshort(vq->DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_NOTIFY),(USHORT)(info->queue_index));
}

struct
virtqueue*
VirtIODeviceFindVirtualQueue(
    IN PVOID DeviceExtension,
    IN unsigned index,
    IN unsigned vector)
{
    virtio_pci_vq_info *info;
    struct virtqueue   *vq;
    u16                num;
    ULONG              dummy;
    PHYSICAL_ADDRESS   pa;
    ULONG              pageNum;
    ULONG              pfns;
    unsigned           res;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s  index = %d, vector = %d\n", __FUNCTION__, index, vector));

    if(vector) {
        StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_MSI_CONFIG_VECTOR),(USHORT)0);
        res = StorPortReadPortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_MSI_CONFIG_VECTOR));
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> VIRTIO_MSI_CONFIG_VECTOR res = 0x%x\n", __FUNCTION__, res));
        if(res == VIRTIO_MSI_NO_VECTOR) {
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> Cannot find config vector res = 0x%x\n", __FUNCTION__, res));
           return NULL;
        }
    }

    // Select the queue we're interested in
    StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL),(USHORT)index);

    // Check if queue is either not available or already active.
    num = StorPortReadPortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_NUM));
    pfns = StorPortReadPortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN));
    if (!num || pfns) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>>> num = 0x%x, pfns= 0x%x\n", __FUNCTION__, num, pfns) );
        return NULL;
    }
    // allocate and fill out our structure the represents an active queue
    info = &adaptExt->pci_vq_info[index];

    info->queue_index = index;
    info->num = num;

    RhelDbgPrint(TRACE_LEVEL_FATAL, ("[%s] info = %p, info->queue = %p\n", __FUNCTION__, info, info->queue) );
    // create the vring
    memset(info->queue, 0, vring_size(num,PAGE_SIZE));
    vq = vring_new_virtqueue(info->num,
                             DeviceExtension,
                             info->queue,
                             vp_notify,
                             index);

    if (!vq) {
        StorPortWritePortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(ULONG)0);
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>>> vring_new_virtqueue failed\n", __FUNCTION__) );
        return NULL;
    }

    vq->priv = info;
    info->vq = vq;

    // activate the queue
    pa = StorPortGetPhysicalAddress(DeviceExtension, NULL, info->queue, &dummy);
    pageNum = (ULONG)(pa.QuadPart >> PAGE_SHIFT);
    RhelDbgPrint(TRACE_LEVEL_FATAL, ("[%s] queue phys.address %08lx:%08lx, pfn %lx\n", __FUNCTION__, pa.u.HighPart, pa.u.LowPart, pageNum));
    StorPortWritePortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(ULONG)(pageNum));

    if(vector) {
        StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_MSI_QUEUE_VECTOR),(USHORT)vector);
        res = StorPortReadPortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_MSI_QUEUE_VECTOR));
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> VIRTIO_MSI_QUEUE_VECTOR vector = %d, res = 0x%x\n", __FUNCTION__, vector, res));
        if(res == VIRTIO_MSI_NO_VECTOR) {
           StorPortWritePortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(ULONG)0);
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> Cannot create vq vector\n", __FUNCTION__));
           return NULL;
        }
    }

    return vq;
}


void
VirtIODeviceDeleteVirtualQueue(
    IN struct virtqueue *vq)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)vq->DeviceExtension;
    struct virtio_pci_vq_info *info = (struct virtio_pci_vq_info *)vq->priv;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s\n", __FUNCTION__));

    // Select and deactivate the queue
    StorPortWritePortUshort(vq->DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL),(USHORT)(info->queue_index));

    StorPortWritePortUlong(vq->DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(ULONG)0);
}
