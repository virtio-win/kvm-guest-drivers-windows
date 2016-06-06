/*
 * Virtio PCI driver - common functionality for all device versions
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 * Copyright Red Hat, Inc. 2014
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Rusty Russell <rusty@rustcorp.com.au>
 *  Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio.h"
#include "kdebugprint.h"
#include <stddef.h>

#include "virtio_pci_common.h"

void virtio_delete_queues(VirtIODevice *vdev)
{
    struct virtqueue *vq;
    unsigned i;

    for (i = 0; i < vdev->maxQueues; i++) {
        vq = vdev->info[i].vq;
        if (vq != NULL) {
            vdev->device->delete_queue(&vdev->info[i]);
            vdev->info[i].vq = NULL;
        }
    }
}

void virtio_delete_queue(struct virtqueue *vq)
{
    VirtIODevice *vdev = vq->vdev;
    unsigned i = vq->index;

    vdev->device->delete_queue(&vdev->info[i]);
    vdev->info[i].vq = NULL;
}

u16 virtio_set_config_vector(VirtIODevice *vdev, u16 vector)
{
    return vdev->device->set_config_vector(vdev, vector);
}

u16 virtio_set_queue_vector(struct virtqueue *vq, u16 vector)
{
    return vq->vdev->device->set_queue_vector(vq, vector);
}

static NTSTATUS vp_setup_vq(struct virtqueue **queue,
                            VirtIODevice *vdev, unsigned index,
                            u16 msix_vec)
{
    VirtIOQueueInfo *info = &vdev->info[index];

    NTSTATUS status = vdev->device->setup_queue(queue, vdev, info, index, msix_vec);
    if (NT_SUCCESS(status)) {
        info->vq = *queue;
    }

    return status;
}

/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq)
{
    /* we write the queue's selector into the notification register to
     * signal the other end */
    iowrite16(vq->vdev, (unsigned short)vq->index, (void *)vq->priv);
    return true;
}

NTSTATUS virtio_find_queue(VirtIODevice *vdev, unsigned index,
                           struct virtqueue **vq)
{
    return vp_setup_vq(
        vq,
        vdev,
        index,
        VIRTIO_MSI_NO_VECTOR);
}

u8 virtio_read_isr_status(VirtIODevice *vdev)
{
    return ioread8(vdev, vdev->isr);
}

u8 virtio_get_status(VirtIODevice *vdev)
{
    return vdev->device->get_status(vdev);
}

void virtio_set_status(VirtIODevice *vdev, u8 status)
{
    vdev->device->set_status(vdev, status);
}

void virtio_add_status(VirtIODevice *vdev, u8 status)
{
    vdev->device->set_status(vdev, (u8)(vdev->device->get_status(vdev) | status));
}

NTSTATUS virtio_finalize_features(VirtIODevice *dev)
{
    unsigned char dev_status;
    NTSTATUS status = dev->device->set_features(dev);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    if (!virtio_has_feature(dev, VIRTIO_F_VERSION_1)) {
        return status;
    }

    virtio_add_status(dev, VIRTIO_CONFIG_S_FEATURES_OK);
    dev_status = dev->device->get_status(dev);
    if (!(dev_status & VIRTIO_CONFIG_S_FEATURES_OK)) {
        DPrintf(0, ("virtio: device refuses features: %x\n", dev_status));
        status = STATUS_INVALID_PARAMETER;
    }
    return status;
}

NTSTATUS virtio_device_initialize(VirtIODevice *pVirtIODevice,
                                  const VirtIOSystemOps *pSystemOps,
                                  PVOID DeviceContext,
                                  ULONG allocatedSize)
{
    NTSTATUS status;

    memset(pVirtIODevice, 0, allocatedSize);
    pVirtIODevice->DeviceContext = DeviceContext;
    pVirtIODevice->system = pSystemOps;
    pVirtIODevice->info = pVirtIODevice->inline_info;

    ASSERT(allocatedSize > offsetof(VirtIODevice, info));
    pVirtIODevice->maxQueues =
        (allocatedSize - offsetof(VirtIODevice, info)) / sizeof(VirtIOQueueInfo);

    status = vio_modern_initialize(pVirtIODevice);
    if (status == STATUS_DEVICE_NOT_CONNECTED) {
        /* fall back to legacy virtio device */
        status = vio_legacy_initialize(pVirtIODevice);
    }
    if (NT_SUCCESS(status)) {
        /* Always start by resetting the device */
        virtio_device_reset(pVirtIODevice);

        /* Acknowledge that we've seen the device. */
        virtio_add_status(pVirtIODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);

        /* If we are here, we must have found a driver for the device */
        virtio_add_status(pVirtIODevice, VIRTIO_CONFIG_S_DRIVER);
    }

    return status;
}

void virtio_device_reset(VirtIODevice *pVirtIODevice)
{
    pVirtIODevice->device->reset(pVirtIODevice);
}

void virtio_device_shutdown(VirtIODevice *pVirtIODevice)
{
    pVirtIODevice->device->release(pVirtIODevice);

    if (pVirtIODevice->info &&
        pVirtIODevice->info != pVirtIODevice->inline_info) {
        mem_free_nonpaged_block(pVirtIODevice, pVirtIODevice->info);
        pVirtIODevice->info = NULL;
    }
}

NTSTATUS virtio_query_queue_allocation(VirtIODevice *vdev,
                                       unsigned index,
                                       unsigned short *pNumEntries,
                                       unsigned long *pAllocationSize,
                                       unsigned long *pHeapSize)
{
    return vdev->device->query_queue_alloc(vdev, index, pNumEntries, pAllocationSize, pHeapSize);
}

NTSTATUS virtio_reserve_queue_memory(VirtIODevice *vdev, unsigned nvqs)
{
    if (nvqs > vdev->maxQueues) {
        /* allocate new space for queue infos */
        void *new_info = mem_alloc_nonpaged_block(vdev, nvqs * virtio_queue_descriptor_size());
        if (!new_info) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (vdev->info && vdev->info != vdev->inline_info) {
            mem_free_nonpaged_block(vdev, vdev->info);
        }
        vdev->info = new_info;
        vdev->maxQueues = nvqs;
    }
    return STATUS_SUCCESS;
}

NTSTATUS virtio_find_queues(VirtIODevice *vdev,
                            unsigned nvqs,
                            struct virtqueue *vqs[])
{
    unsigned i;
    NTSTATUS status;
    u16 msix_vec;

    status = virtio_reserve_queue_memory(vdev, nvqs);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* set up the device config interrupt */
    msix_vec = vdev_get_msix_vector(vdev, -1);

    if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
        msix_vec = vdev->device->set_config_vector(vdev, msix_vec);
        /* Verify we had enough resources to assign the vector */
        if (msix_vec == VIRTIO_MSI_NO_VECTOR) {
            status = STATUS_DEVICE_BUSY;
            goto error_find;
        }
        vdev->msix_used = 1;
    }

    /* set up queue interrupts */
    for (i = 0; i < nvqs; i++) {
        msix_vec = vdev_get_msix_vector(vdev, i);
        status = vp_setup_vq(
            &vqs[i],
            vdev,
            i,
            msix_vec);
        if (!NT_SUCCESS(status)) {
            goto error_find;
        }
        if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
            vdev->msix_used |= 1;
        }
    }
    return STATUS_SUCCESS;

error_find:
    virtio_delete_queues(vdev);
    return status;
}

int virtio_get_bar_index(PPCI_COMMON_HEADER pPCIHeader, PHYSICAL_ADDRESS BasePA)
{
    int iBar, i;

    /* no point in supporting PCI and CardBus bridges */
    ASSERT(pPCIHeader->HeaderType & ~PCI_MULTIFUNCTION == PCI_DEVICE_TYPE);

    for (i = 0; i < PCI_TYPE0_ADDRESSES; i++) {
        PHYSICAL_ADDRESS BAR;
        BAR.LowPart = pPCIHeader->u.type0.BaseAddresses[i];

        iBar = i;
        if (BAR.LowPart & 0x01) {
            /* I/O space */
            BAR.LowPart &= 0xFFFFFFFC;
            BAR.HighPart = 0;
        } else if ((BAR.LowPart & 0x06) == 0x04) {
            /* memory space 64-bit */
            BAR.LowPart &= 0xFFFFFFF0;
            BAR.HighPart = pPCIHeader->u.type0.BaseAddresses[++i];
        } else {
            /* memory space 32-bit */
            BAR.LowPart &= 0xFFFFFFF0;
            BAR.HighPart = 0;
        }

        if (BAR.QuadPart == BasePA.QuadPart) {
            return iBar;
        }
    }
    return -1;
}

/* Read @count fields, @bytes each. */
static void virtio_cread_many(VirtIODevice *vdev,
    unsigned int offset,
    void *buf, size_t count, size_t bytes)
{
    u32 old, gen = vdev->device->get_config_generation ?
        vdev->device->get_config_generation(vdev) : 0;
    size_t i;

    do {
        old = gen;

        for (i = 0; i < count; i++) {
            vdev->device->get_config(vdev, (unsigned)(offset + bytes * i),
                (char *)buf + i * bytes, (unsigned)bytes);
        }

        gen = vdev->device->get_config_generation ?
            vdev->device->get_config_generation(vdev) : 0;
    } while (gen != old);
}

/* Write @count fields, @bytes each. */
static void virtio_cwrite_many(VirtIODevice *vdev,
    unsigned int offset,
    void *buf, size_t count, size_t bytes)
{
    size_t i;
    for (i = 0; i < count; i++) {
        vdev->device->set_config(vdev, (unsigned)(offset + bytes * i),
            (char *)buf + i * bytes, (unsigned)bytes);
    }
}

/* Config space accessors. */
void virtio_get_config(VirtIODevice *vdev, unsigned offset,
    void *buf, unsigned len)
{
    switch (len) {
    case 1:
    case 2:
    case 4:
        vdev->device->get_config(vdev, offset, buf, len);
        break;
    case 8:
        virtio_cread_many(vdev, offset, buf, 2, sizeof(u32));
        break;
    default:
        virtio_cread_many(vdev, offset, buf, len, 1);
    }
}

void virtio_set_config(VirtIODevice *vdev, unsigned offset,
    void *buf, unsigned len)
{
    switch (len) {
    case 1:
    case 2:
    case 4:
        vdev->device->set_config(vdev, offset, buf, len);
        break;
    case 8:
        virtio_cwrite_many(vdev, offset, buf, 2, sizeof(u32));
        break;
    default:
        virtio_cwrite_many(vdev, offset, buf, len, 1);
    }
}

void virtio_device_ready(VirtIODevice *dev)
{
    unsigned status = dev->device->get_status(dev);

    BUG_ON(status & VIRTIO_CONFIG_S_DRIVER_OK);
    dev->device->set_status(dev, (u8)(status | VIRTIO_CONFIG_S_DRIVER_OK));
}

u64 virtio_get_features(VirtIODevice *dev)
{
    dev->features = dev->device->get_features(dev);
    return dev->features;
}
