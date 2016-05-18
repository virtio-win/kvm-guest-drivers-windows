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
#include "VirtIO_PCI.h"
#include "virtio_config.h"
#include "virtio.h"
#include "kdebugprint.h"
#include "VirtIO_Ring.h"
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

    DPrintf(0, ("[VIRTIO_PCI_HOST_FEATURES] = %x\n", ReadVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_HOST_FEATURES)));
    DPrintf(0, ("[VIRTIO_PCI_GUEST_FEATURES] = %x\n", ReadVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_GUEST_FEATURES)));
    DPrintf(0, ("[VIRTIO_PCI_QUEUE_PFN] = %x\n", ReadVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_QUEUE_PFN)));
    DPrintf(0, ("[VIRTIO_PCI_QUEUE_NUM] = %x\n", ReadVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_QUEUE_NUM)));
    DPrintf(0, ("[VIRTIO_PCI_QUEUE_SEL] = %x\n", ReadVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_QUEUE_SEL)));
    DPrintf(0, ("[VIRTIO_PCI_QUEUE_NOTIFY] = %x\n", ReadVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_QUEUE_NOTIFY)));
    DPrintf(0, ("[VIRTIO_PCI_STATUS] = %x\n", ReadVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_STATUS)));
    DPrintf(0, ("[VIRTIO_PCI_ISR] = %x\n", ReadVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_ISR)));
}


/////////////////////////////////////////////////////////////////////////////////////
//
// Reset device
//
/////////////////////////////////////////////////////////////////////////////////////
static void VirtIODeviceReset(VirtIODevice * pVirtIODevice)
{
    /* 0 status means a reset. */
    WriteVirtIODeviceByte(pVirtIODevice->addr + VIRTIO_PCI_STATUS, 0);
}

/////////////////////////////////////////////////////////////////////////////////////
//
// Get\Set status
//
/////////////////////////////////////////////////////////////////////////////////////
static u8 VirtIODeviceGetStatus(VirtIODevice * pVirtIODevice)
{
    DPrintf(6, ("%s\n", __FUNCTION__));

    return ReadVirtIODeviceByte(pVirtIODevice->addr + VIRTIO_PCI_STATUS);
}

static void VirtIODeviceSetStatus(VirtIODevice * pVirtIODevice, u8 status)
{
    DPrintf(6, ("%s>>> %x\n", __FUNCTION__, status));
    WriteVirtIODeviceByte(pVirtIODevice->addr + VIRTIO_PCI_STATUS, status);
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
        ptr[i] = ReadVirtIODeviceByte(ioaddr + i);
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
        WriteVirtIODeviceByte(ioaddr + i, ptr[i]);
}

u32 VirtIODeviceGetQueueSize(struct virtqueue *vq)
{
    return vq->vdev->info[vq->index].num;
}

u32 VirtIODeviceIndirectPageCapacity()
{
    return PAGE_SIZE / sizeof(struct vring_desc);
}

static u16 vp_config_vector(virtio_pci_device *vp_dev, u16 vector)
{
    /* Setup the vector used for configuration events */
    iowrite16(vector, vp_dev->addr + VIRTIO_MSI_CONFIG_VECTOR);
    /* Verify we had enough resources to assign the vector */
    /* Will also flush the write out to device */
    return ioread16(vp_dev->addr + VIRTIO_MSI_CONFIG_VECTOR);
}

static int query_vq_alloc(virtio_pci_device *vp_dev,
                          unsigned index,
                          unsigned short *pNumEntries,
                          unsigned long *pAllocationSize,
                          unsigned long *pHeapSize)
{
    unsigned long ring_size, data_size;
    u16 num;

    /* Select the queue we're interested in */
    iowrite16((u16)index, vp_dev->addr + VIRTIO_PCI_QUEUE_SEL);

    /* Check if queue is either not available or already active. */
    num = ioread16(vp_dev->addr + VIRTIO_PCI_QUEUE_NUM);
    if (!num || ioread32(vp_dev->addr + VIRTIO_PCI_QUEUE_PFN))
        return -ENOENT;

    ring_size = PAGE_ALIGN(vring_size(num, VIRTIO_PCI_VRING_ALIGN));
    data_size = PAGE_ALIGN(sizeof(void *) * num + vring_control_block_size());

    *pNumEntries = num;
    *pAllocationSize = ring_size + data_size;
    *pHeapSize = 0;

    return 0;
}

static struct virtqueue *setup_vq(virtio_pci_device *vp_dev,
                                  virtio_pci_vq_info *info,
                                  unsigned index,
                                  void(*callback)(struct virtqueue *vq),
                                  const char *name,
                                  u16 msix_vec)
{
    struct virtqueue *vq;
    unsigned long size, ring_size, heap_size;
    int err;

    UNREFERENCED_PARAMETER(callback);

    /* Select the queue and query allocation parameters */
    err = query_vq_alloc(vp_dev, index, &info->num, &size, &heap_size);
    if (err) {
        return ERR_PTR(err);
    }

    ring_size = PAGE_ALIGN(vring_size(info->num, VIRTIO_PCI_VRING_ALIGN));

    info->queue = alloc_pages_exact(vp_dev, size, GFP_KERNEL | __GFP_ZERO);
    if (info->queue == NULL)
        return ERR_PTR(-ENOMEM);

    /* activate the queue */
    iowrite32((u32)(virt_to_phys(vp_dev, info->queue) >> VIRTIO_PCI_QUEUE_ADDR_SHIFT),
        vp_dev->addr + VIRTIO_PCI_QUEUE_PFN);

    /* create the vring */
    vq = vring_new_virtqueue(index, info->num,
        VIRTIO_PCI_VRING_ALIGN, vp_dev,
        true, info->queue, vp_notify, (u8 *)info->queue + ring_size, name);
    if (!vq) {
        err = -ENOMEM;
        goto out_activate_queue;
    }

    vq->priv = (void __force *)(vp_dev->addr + VIRTIO_PCI_QUEUE_NOTIFY);

    if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
        iowrite16(msix_vec, vp_dev->addr + VIRTIO_MSI_QUEUE_VECTOR);
        msix_vec = ioread16(vp_dev->addr + VIRTIO_MSI_QUEUE_VECTOR);
        if (msix_vec == VIRTIO_MSI_NO_VECTOR) {
            err = -EBUSY;
            goto out_assign;
        }
    }

    return vq;

out_assign:
out_activate_queue:
    iowrite32(0, vp_dev->addr + VIRTIO_PCI_QUEUE_PFN);
    free_pages_exact(vp_dev, info->queue, size);
    return ERR_PTR(err);
}

static void del_vq(virtio_pci_vq_info *info)
{
    struct virtqueue *vq = info->vq;
    virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
    unsigned long size;

    iowrite16((u16)vq->index, vp_dev->addr + VIRTIO_PCI_QUEUE_SEL);

    if (vp_dev->msix_used) {
        iowrite16(VIRTIO_MSI_NO_VECTOR,
            vp_dev->addr + VIRTIO_MSI_QUEUE_VECTOR);
        /* Flush the write out to device */
        ioread8(vp_dev->addr + VIRTIO_PCI_ISR);
    }

    /* Select and deactivate the queue */
    iowrite32(0, vp_dev->addr + VIRTIO_PCI_QUEUE_PFN);

    size = PAGE_ALIGN(vring_size(info->num, VIRTIO_PCI_VRING_ALIGN));
    free_pages_exact(vp_dev, info->queue, size);
}

/* virtio config->get_features() implementation */
static u64 vp_get_features(virtio_device *vdev)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);

    /* When someone needs more than 32 feature bits, we'll need to
    * steal a bit to indicate that the rest are somewhere else. */
    return ioread32(vp_dev->addr + VIRTIO_PCI_HOST_FEATURES);
}

/* virtio config->finalize_features() implementation */
static int vp_finalize_features(virtio_device *vdev)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);

    /* Give virtio_ring a chance to accept features. */
    vring_transport_features(vdev);

    /* Make sure we don't have any features > 32 bits! */
    BUG_ON((u32)vdev->features != vdev->features);

    /* We only support 32 feature bits. */
    iowrite32((u32)vp_dev->features, vp_dev->addr + VIRTIO_PCI_GUEST_FEATURES);

    return 0;
}

/* virtio config->set_msi_vector() implementation */
static u16 vp_set_msi_vector(struct virtqueue *vq, u16 vector)
{
    virtio_pci_device *vp_dev = to_vp_device(vq->vdev);

    iowrite16((u16)vq->index, vp_dev->addr + VIRTIO_PCI_QUEUE_SEL);
    iowrite16(vector, vp_dev->addr + VIRTIO_MSI_QUEUE_VECTOR);
    return ioread16(vp_dev->addr + VIRTIO_MSI_QUEUE_VECTOR);
}

static const struct virtio_config_ops virtio_pci_config_ops = {
    .get = VirtIODeviceGet,
    .set = VirtIODeviceSet,
    .generation = NULL,
    .get_status = VirtIODeviceGetStatus,
    .set_status = VirtIODeviceSetStatus,
    .reset = VirtIODeviceReset,
    .find_vqs = vp_find_vqs,
    .find_vq = vp_find_vq,
    .del_vqs = vp_del_vqs,
    .del_vq = vp_del_vq,
    .get_features = vp_get_features,
    .finalize_features = vp_finalize_features,
    .set_msi_vector = vp_set_msi_vector,
};

/* the PCI probing function */
int virtio_pci_legacy_probe(virtio_pci_device *vp_dev)
{
    size_t length = pci_resource_len(vp_dev, 0);
    vp_dev->addr = (ULONG_PTR)pci_iomap_range(vp_dev, 0, 0, length);

    if (!vp_dev->addr)
        return -ENOMEM;

    vp_dev->isr = (u8 *)vp_dev->addr + VIRTIO_PCI_ISR;

    vp_dev->config = &virtio_pci_config_ops;

    vp_dev->config_vector = vp_config_vector;
    vp_dev->query_vq_alloc = query_vq_alloc;
    vp_dev->setup_vq = setup_vq;
    vp_dev->del_vq = del_vq;

    return 0;
}

void virtio_pci_legacy_remove(virtio_pci_device *vp_dev)
{
    pci_iounmap(vp_dev, (void *)vp_dev->addr);
}
