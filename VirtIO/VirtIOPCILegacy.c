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
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#include "osdep.h"
#include "virtio_pci.h"
#include "virtio.h"
#include "kdebugprint.h"
#include "virtio_ring.h"
#include "virtio_pci_common.h"
#include "windows\virtio_ring_allocation.h"

#ifdef WPP_EVENT_TRACING
#include "VirtIOPCILegacy.tmh"
#endif

void VirtIODeviceSetMSIXUsed(VirtIODevice * pVirtIODevice, bool used)
{
    pVirtIODevice->msix_used = used != 0;
}

/////////////////////////////////////////////////////////////////////////////////////
//
// VirtIODeviceDumpRegisters - Dump HW registers of the device
//
/////////////////////////////////////////////////////////////////////////////////////
void VirtIODeviceDumpRegisters(VirtIODevice * pVirtIODevice)
{
    DPrintf(5, ("%s\n", __FUNCTION__));

    DPrintf(0, ("[VIRTIO_PCI_HOST_FEATURES] = %x\n", ioread32(pVirtIODevice, pVirtIODevice->addr + VIRTIO_PCI_HOST_FEATURES)));
    DPrintf(0, ("[VIRTIO_PCI_GUEST_FEATURES] = %x\n", ioread32(pVirtIODevice, pVirtIODevice->addr + VIRTIO_PCI_GUEST_FEATURES)));
    DPrintf(0, ("[VIRTIO_PCI_QUEUE_PFN] = %x\n", ioread32(pVirtIODevice, pVirtIODevice->addr + VIRTIO_PCI_QUEUE_PFN)));
    DPrintf(0, ("[VIRTIO_PCI_QUEUE_NUM] = %x\n", ioread32(pVirtIODevice, pVirtIODevice->addr + VIRTIO_PCI_QUEUE_NUM)));
    DPrintf(0, ("[VIRTIO_PCI_QUEUE_SEL] = %x\n", ioread32(pVirtIODevice, pVirtIODevice->addr + VIRTIO_PCI_QUEUE_SEL)));
    DPrintf(0, ("[VIRTIO_PCI_QUEUE_NOTIFY] = %x\n", ioread32(pVirtIODevice, pVirtIODevice->addr + VIRTIO_PCI_QUEUE_NOTIFY)));
    DPrintf(0, ("[VIRTIO_PCI_STATUS] = %x\n", ioread32(pVirtIODevice, pVirtIODevice->addr + VIRTIO_PCI_STATUS)));
    DPrintf(0, ("[VIRTIO_PCI_ISR] = %x\n", ioread32(pVirtIODevice, pVirtIODevice->addr + VIRTIO_PCI_ISR)));
}


/////////////////////////////////////////////////////////////////////////////////////
//
// Reset device
//
/////////////////////////////////////////////////////////////////////////////////////
static void VirtIODeviceReset(VirtIODevice * pVirtIODevice)
{
    /* 0 status means a reset. */
    iowrite8(pVirtIODevice, 0, pVirtIODevice->addr + VIRTIO_PCI_STATUS);
}

/////////////////////////////////////////////////////////////////////////////////////
//
// Get\Set status
//
/////////////////////////////////////////////////////////////////////////////////////
static u8 VirtIODeviceGetStatus(VirtIODevice * pVirtIODevice)
{
    DPrintf(6, ("%s\n", __FUNCTION__));

    return ioread8(pVirtIODevice, pVirtIODevice->addr + VIRTIO_PCI_STATUS);
}

static void VirtIODeviceSetStatus(VirtIODevice * pVirtIODevice, u8 status)
{
    DPrintf(6, ("%s>>> %x\n", __FUNCTION__, status));
    iowrite8(pVirtIODevice, status, pVirtIODevice->addr + VIRTIO_PCI_STATUS);
}

/////////////////////////////////////////////////////////////////////////////////////
//
// Get\Set device data
//
/////////////////////////////////////////////////////////////////////////////////////
static void VirtIODeviceGet(VirtIODevice * pVirtIODevice,
                            unsigned offset,
                            void *buf,
                            unsigned len)
{
    ULONG_PTR ioaddr = pVirtIODevice->addr + VIRTIO_PCI_CONFIG(pVirtIODevice->msix_used) + offset;
    u8 *ptr = buf;
    unsigned i;

    DPrintf(5, ("%s\n", __FUNCTION__));

    for (i = 0; i < len; i++)
        ptr[i] = ioread8(pVirtIODevice, ioaddr + i);
}

static void VirtIODeviceSet(VirtIODevice * pVirtIODevice,
                               unsigned offset,
                               const void *buf,
                               unsigned len)
{
    ULONG_PTR ioaddr = pVirtIODevice->addr + VIRTIO_PCI_CONFIG(pVirtIODevice->msix_used) + offset;
    const u8 *ptr = buf;
    unsigned i;

    DPrintf(5, ("%s\n", __FUNCTION__));

    for (i = 0; i < len; i++)
        iowrite8(pVirtIODevice, ptr[i], ioaddr + i);
}

u32 VirtIODeviceGetQueueSize(struct virtqueue *vq)
{
    return vq->vdev->info[vq->index].num;
}

u32 VirtIODeviceIndirectPageCapacity()
{
    return PAGE_SIZE / sizeof(struct vring_desc);
}

static u16 vp_config_vector(VirtIODevice *vdev, u16 vector)
{
    /* Setup the vector used for configuration events */
    iowrite16(vdev, vector, vdev->addr + VIRTIO_MSI_CONFIG_VECTOR);
    /* Verify we had enough resources to assign the vector */
    /* Will also flush the write out to device */
    return ioread16(vdev, vdev->addr + VIRTIO_MSI_CONFIG_VECTOR);
}

static NTSTATUS query_vq_alloc(VirtIODevice *vdev,
                          unsigned index,
                          unsigned short *pNumEntries,
                          unsigned long *pAllocationSize,
                          unsigned long *pHeapSize)
{
    unsigned long ring_size, data_size;
    u16 num;

    /* Select the queue we're interested in */
    iowrite16(vdev, (u16)index, vdev->addr + VIRTIO_PCI_QUEUE_SEL);

    /* Check if queue is either not available or already active. */
    num = ioread16(vdev, vdev->addr + VIRTIO_PCI_QUEUE_NUM);
    if (!num || ioread32(vdev, vdev->addr + VIRTIO_PCI_QUEUE_PFN))
        return STATUS_NOT_FOUND;

    ring_size = ROUND_TO_PAGES(vring_size(num, VIRTIO_PCI_VRING_ALIGN));
    data_size = ROUND_TO_PAGES(sizeof(void *) * num + vring_control_block_size());

    *pNumEntries = num;
    *pAllocationSize = ring_size + data_size;
    *pHeapSize = 0;

    return STATUS_SUCCESS;
}

static NTSTATUS setup_vq(struct virtqueue **queue,
                         VirtIODevice *vdev,
                         VirtIOQueueInfo *info,
                         unsigned index,
                         const char *name,
                         u16 msix_vec)
{
    struct virtqueue *vq;
    unsigned long size, ring_size, heap_size;
    NTSTATUS status;

    /* Select the queue and query allocation parameters */
    status = query_vq_alloc(vdev, index, &info->num, &size, &heap_size);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ring_size = ROUND_TO_PAGES(vring_size(info->num, VIRTIO_PCI_VRING_ALIGN));

    info->queue = mem_alloc_contiguous_pages(vdev, size);
    if (info->queue == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* activate the queue */
    iowrite32(vdev, (u32)(mem_get_physical_address(vdev, info->queue) >> VIRTIO_PCI_QUEUE_ADDR_SHIFT),
        vdev->addr + VIRTIO_PCI_QUEUE_PFN);

    /* create the vring */
    vq = vring_new_virtqueue(index, info->num,
        VIRTIO_PCI_VRING_ALIGN, vdev,
        true, info->queue, vp_notify, (u8 *)info->queue + ring_size, name);
    if (!vq) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto out_activate_queue;
    }

    vq->priv = (void *)(vdev->addr + VIRTIO_PCI_QUEUE_NOTIFY);

    if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
        iowrite16(vdev, msix_vec, vdev->addr + VIRTIO_MSI_QUEUE_VECTOR);
        msix_vec = ioread16(vdev, vdev->addr + VIRTIO_MSI_QUEUE_VECTOR);
        if (msix_vec == VIRTIO_MSI_NO_VECTOR) {
            status = STATUS_DEVICE_BUSY;
            goto out_assign;
        }
    }

    *queue = vq;
    return STATUS_SUCCESS;

out_assign:
out_activate_queue:
    iowrite32(vdev, 0, vdev->addr + VIRTIO_PCI_QUEUE_PFN);
    mem_free_contiguous_pages(vdev, info->queue);
    return status;
}

static void del_vq(VirtIOQueueInfo *info)
{
    struct virtqueue *vq = info->vq;
    VirtIODevice *vdev = vq->vdev;
    unsigned long size;

    iowrite16(vdev, (u16)vq->index, vdev->addr + VIRTIO_PCI_QUEUE_SEL);

    if (vdev->msix_used) {
        iowrite16(vdev, VIRTIO_MSI_NO_VECTOR,
            vdev->addr + VIRTIO_MSI_QUEUE_VECTOR);
        /* Flush the write out to device */
        ioread8(vdev, vdev->addr + VIRTIO_PCI_ISR);
    }

    /* Select and deactivate the queue */
    iowrite32(vdev, 0, vdev->addr + VIRTIO_PCI_QUEUE_PFN);

    size = ROUND_TO_PAGES(vring_size(info->num, VIRTIO_PCI_VRING_ALIGN));
    mem_free_contiguous_pages(vdev, info->queue);
}

/* virtio config->get_features() implementation */
static u64 vp_get_features(VirtIODevice *vdev)
{
    /* When someone needs more than 32 feature bits, we'll need to
    * steal a bit to indicate that the rest are somewhere else. */
    return ioread32(vdev, vdev->addr + VIRTIO_PCI_HOST_FEATURES);
}

/* virtio config->finalize_features() implementation */
static NTSTATUS vp_finalize_features(VirtIODevice *vdev)
{
    /* Give virtio_ring a chance to accept features. */
    vring_transport_features(vdev);

    /* Make sure we don't have any features > 32 bits! */
    BUG_ON((u32)vdev->features != vdev->features);

    /* We only support 32 feature bits. */
    iowrite32(vdev, (u32)vdev->features, vdev->addr + VIRTIO_PCI_GUEST_FEATURES);

    return STATUS_SUCCESS;
}

/* virtio config->set_msi_vector() implementation */
static u16 vp_set_msi_vector(struct virtqueue *vq, u16 vector)
{
    VirtIODevice *vdev = vq->vdev;

    iowrite16(vdev, (u16)vq->index, vdev->addr + VIRTIO_PCI_QUEUE_SEL);
    iowrite16(vdev, vector, vdev->addr + VIRTIO_MSI_QUEUE_VECTOR);
    return ioread16(vdev, vdev->addr + VIRTIO_MSI_QUEUE_VECTOR);
}

static const struct virtio_config_ops virtio_pci_config_ops = {
    .get = VirtIODeviceGet,
    .set = VirtIODeviceSet,
    .generation = NULL,
    .get_status = VirtIODeviceGetStatus,
    .set_status = VirtIODeviceSetStatus,
    .reset = VirtIODeviceReset,
    .config_vector = vp_config_vector,
    .query_vq_alloc = query_vq_alloc,
    .setup_vq = setup_vq,
    .del_vq = del_vq,
    .find_vqs = vp_find_vqs,
    .find_vq = vp_find_vq,
    .get_features = vp_get_features,
    .finalize_features = vp_finalize_features,
    .set_msi_vector = vp_set_msi_vector,
};

/* the PCI probing function */
NTSTATUS virtio_pci_legacy_probe(VirtIODevice *vdev)
{
    size_t length = pci_get_resource_len(vdev, 0);
    vdev->addr = (ULONG_PTR)pci_map_address_range(vdev, 0, 0, length);

    if (!vdev->addr)
        return STATUS_INSUFFICIENT_RESOURCES;

    vdev->isr = (u8 *)vdev->addr + VIRTIO_PCI_ISR;

    vdev->config = &virtio_pci_config_ops;

    return STATUS_SUCCESS;
}

void virtio_pci_legacy_remove(VirtIODevice *vdev)
{
    pci_unmap_address_range(vdev, (void *)vdev->addr);
}
