/*
 * Virtio PCI driver - modern (virtio 1.0) device support
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
#define VIRTIO_PCI_NO_LEGACY
#include "virtio_pci.h"
#include "virtio.h"
#include "kdebugprint.h"
#include "virtio_ring.h"
#include "virtio_pci_common.h"
#include "windows\virtio_ring_allocation.h"
#include <stddef.h>

#ifdef WPP_EVENT_TRACING
#include "VirtIOPCIModern.tmh"
#endif

static void iowrite64_twopart(VirtIODevice *vdev,
                              u64 val,
                              __le32 *lo, __le32 *hi)
{
    iowrite32(vdev, (u32)val, lo);
    iowrite32(vdev, val >> 32, hi);
}

static void *map_capability(VirtIODevice *dev, int off,
                            size_t minlen,
                            u32 align,
                            u32 start, u32 size,
                            size_t *len)
{
    u8 bar;
    u32 offset, length;
    void *p;

    pci_read_config_byte(dev, off + offsetof(struct virtio_pci_cap,
                                             bar),
                         &bar);
    pci_read_config_dword(dev, off + offsetof(struct virtio_pci_cap, offset),
                          &offset);
    pci_read_config_dword(dev, off + offsetof(struct virtio_pci_cap, length),
                          &length);

    if (length <= start) {
        DPrintf(0, ("virtio_pci: bad capability len %u (>%u expected)\n", length, start));
        return NULL;
    }

    if (length - start < minlen) {
        DPrintf(0, ("virtio_pci: bad capability len %u (>=%zu expected)\n", length, minlen));
        return NULL;
    }

    length -= start;

    if (start + offset < offset) {
        DPrintf(0, ("virtio_pci: map wrap-around %u+%u\n", start, offset));
        return NULL;
    }

    offset += start;

    if (offset & (align - 1)) {
        DPrintf(0, ("virtio_pci: offset %u not aligned to %u\n", offset, align));
        return NULL;
    }

    if (length > size)
        length = size;

    if (len)
        *len = length;

    if (minlen + offset < minlen ||
        minlen + offset > pci_get_resource_len(dev, bar)) {
        DPrintf(0, ("virtio_pci: map virtio %zu@%u out of range on bar %i length %lu\n",
            minlen, offset,
            bar, (unsigned long)pci_get_resource_len(dev, bar)));
        return NULL;
    }

    p = pci_map_address_range(dev, bar, offset, length);
    if (!p)
        DPrintf(0, ("virtio_pci: unable to map virtio %u@%u on bar %i\n", length, offset, bar));
    return p;
}

/* virtio device->get_features() implementation */
static u64 vp_get_features(VirtIODevice *vdev)
{
    u64 features;

    iowrite32(vdev, 0, &vdev->common->device_feature_select);
    features = ioread32(vdev, &vdev->common->device_feature);
    iowrite32(vdev, 1, &vdev->common->device_feature_select);
    features |= ((u64)ioread32(vdev, &vdev->common->device_feature) << 32);

    return features;
}

/* virtio device->set_features() implementation */
static NTSTATUS vp_set_features(VirtIODevice *vdev)
{
    /* Give virtio_ring a chance to accept features. */
    vring_transport_features(vdev);

    if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        DPrintf(0, ("virtio: device uses modern interface but does not have VIRTIO_F_VERSION_1\n"));
        return STATUS_INVALID_PARAMETER;
    }

    iowrite32(vdev, 0, &vdev->common->guest_feature_select);
    iowrite32(vdev, (u32)vdev->features, &vdev->common->guest_feature);
    iowrite32(vdev, 1, &vdev->common->guest_feature_select);
    iowrite32(vdev, vdev->features >> 32, &vdev->common->guest_feature);

    return STATUS_SUCCESS;
}

/* virtio device->set_queue_vector() implementation */
static u16 vp_queue_vector(struct virtqueue *vq, u16 vector)
{
    VirtIODevice *vdev = vq->vdev;
    struct virtio_pci_common_cfg *cfg = vdev->common;

    iowrite16(vdev, (u16)vq->index, &cfg->queue_select);
    iowrite16(vdev, vector, &cfg->queue_msix_vector);
    return ioread16(vdev, &cfg->queue_msix_vector);
}

/* virtio device->get_config() implementation */
static void vp_get(VirtIODevice *vdev, unsigned offset,
                   void *buf, unsigned len)
{
    u8 b;
    __le16 w;
    __le32 l;

    if (!vdev->config) {
        BUG();
        return;
    }

    BUG_ON(offset + len > vdev->config_len);

    switch (len) {
    case 1:
        b = ioread8(vdev, vdev->config + offset);
        memcpy(buf, &b, sizeof b);
        break;
    case 2:
        w = ioread16(vdev, vdev->config + offset);
        memcpy(buf, &w, sizeof w);
        break;
    case 4:
        l = ioread32(vdev, vdev->config + offset);
        memcpy(buf, &l, sizeof l);
        break;
    case 8:
        l = ioread32(vdev, vdev->config + offset);
        memcpy(buf, &l, sizeof l);
        l = ioread32(vdev, vdev->config + offset + sizeof l);
        memcpy((unsigned char *)buf + sizeof l, &l, sizeof l);
        break;
    default:
        BUG();
    }
}

/* the device->set_config() implementation.  it's symmetric to the device->get_config()
 * implementation */
static void vp_set(VirtIODevice *vdev, unsigned offset,
                   const void *buf, unsigned len)
{
    u8 b;
    __le16 w;
    __le32 l;

    if (!vdev->config) {
        BUG();
        return;
    }

    BUG_ON(offset + len > vdev->config_len);

    switch (len) {
    case 1:
        memcpy(&b, buf, sizeof b);
        iowrite8(vdev, b, vdev->config + offset);
        break;
    case 2:
        memcpy(&w, buf, sizeof w);
        iowrite16(vdev, w, vdev->config + offset);
        break;
    case 4:
        memcpy(&l, buf, sizeof l);
        iowrite32(vdev, l, vdev->config + offset);
        break;
    case 8:
        memcpy(&l, buf, sizeof l);
        iowrite32(vdev, l, vdev->config + offset);
        memcpy(&l, (unsigned char *)buf + sizeof l, sizeof l);
        iowrite32(vdev, l, vdev->config + offset + sizeof l);
        break;
    default:
        BUG();
    }
}

static u32 vp_generation(VirtIODevice *vdev)
{
    return ioread8(vdev, &vdev->common->config_generation);
}

/* device->{get,set}_status() implementations */
static u8 vp_get_status(VirtIODevice *vdev)
{
    return ioread8(vdev, &vdev->common->device_status);
}

static void vp_set_status(VirtIODevice *vdev, u8 status)
{
    /* We should never be setting status to 0. */
    BUG_ON(status == 0);
    iowrite8(vdev, status, &vdev->common->device_status);
}

static void vp_reset(VirtIODevice *vdev)
{
    /* 0 status means a reset. */
    iowrite8(vdev, 0, &vdev->common->device_status);
    /* After writing 0 to device_status, the driver MUST wait for a read of
    * device_status to return 0 before reinitializing the device.
    * This will flush out the status write, and flush in device writes,
    * including MSI-X interrupts, if any.
    */
    while (ioread8(vdev, &vdev->common->device_status)) {
        vdev_sleep(vdev, 1);
    }
}

static u16 vp_config_vector(VirtIODevice *vdev, u16 vector)
{
    /* Setup the vector used for configuration events */
    iowrite16(vdev, vector, &vdev->common->msix_config);
    /* Verify we had enough resources to assign the vector */
    /* Will also flush the write out to device */
    return ioread16(vdev, &vdev->common->msix_config);
}

static size_t vring_pci_size(u16 num)
{
    /* We only need a cacheline separation. */
    return (size_t)ROUND_TO_PAGES(vring_size(num, SMP_CACHE_BYTES));
}

static void *alloc_virtqueue_pages(VirtIODevice *vdev, u16 *num)
{
    void *pages;
    
    /* TODO: allocate each queue chunk individually */
    for (; *num && vring_pci_size(*num) > PAGE_SIZE; *num /= 2) {
        pages = mem_alloc_contiguous_pages(vdev, vring_pci_size(*num));
        if (pages)
            return pages;
    }
    
    if (!*num)
        return NULL;
    
    /* Try to get a single page. You are my only hope! */
    return mem_alloc_contiguous_pages(vdev, vring_pci_size(*num));
}

static NTSTATUS query_vq_alloc(VirtIODevice *vdev,
                               unsigned index,
                               unsigned short *pNumEntries,
                               unsigned long *pAllocationSize,
                               unsigned long *pHeapSize)
{
    struct virtio_pci_common_cfg *cfg = vdev->common;
    u16 num;

    if (index >= ioread16(vdev, &cfg->num_queues))
        return STATUS_NOT_FOUND;

    /* Select the queue we're interested in */
    iowrite16(vdev, (u16)index, &cfg->queue_select);

    /* Check if queue is either not available or already active. */
    num = ioread16(vdev, &cfg->queue_size);
    /* QEMU has a bug where queues don't revert to inactive on device
     * reset. Skip checking the queue_enable field until it is fixed.
     */
    if (!num /*|| ioread16(vdev, &cfg->queue_enable)*/)
        return STATUS_NOT_FOUND;

    if (num & (num - 1)) {
        DPrintf(0, ("%p: bad queue size %u", vdev, num));
        return STATUS_INVALID_PARAMETER;
    }

    *pNumEntries = num;
    *pAllocationSize = (unsigned long)vring_pci_size(num);
    *pHeapSize = vring_control_block_size() + sizeof(void *) * num;

    return STATUS_SUCCESS;
}

static NTSTATUS setup_vq(struct virtqueue **queue,
                         VirtIODevice *vdev,
                         VirtIOQueueInfo *info,
                         unsigned index,
                         u16 msix_vec)
{
    struct virtio_pci_common_cfg *cfg = vdev->common;
    struct virtqueue *vq;
    void *vq_addr;
    u16 off;
    unsigned long size, heap_size;
    NTSTATUS status;

    /* Select the queue and query allocation parameters */
    status = query_vq_alloc(vdev, index, &info->num, &size, &heap_size);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* get offset of notification word for this vq */
    off = ioread16(vdev, &cfg->queue_notify_off);

    info->queue = alloc_virtqueue_pages(vdev, &info->num);
    if (info->queue == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    vq_addr = mem_alloc_nonpaged_block(vdev, heap_size);
    if (vq_addr == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* create the vring */
    vq = vring_new_virtqueue(index, info->num,
        SMP_CACHE_BYTES, vdev,
        true, info->queue, vp_notify, vq_addr);
    if (!vq) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_new_queue;
    }

    /* activate the queue */
    iowrite16(vdev, info->num, &cfg->queue_size);
    iowrite64_twopart(vdev, mem_get_physical_address(vdev, info->queue),
        &cfg->queue_desc_lo, &cfg->queue_desc_hi);
    iowrite64_twopart(vdev, mem_get_physical_address(vdev, virtqueue_get_avail(vq)),
        &cfg->queue_avail_lo, &cfg->queue_avail_hi);
    iowrite64_twopart(vdev, mem_get_physical_address(vdev, virtqueue_get_used(vq)),
        &cfg->queue_used_lo, &cfg->queue_used_hi);

    if (vdev->notify_base) {
        /* offset should not wrap */
        if ((u64)off * vdev->notify_offset_multiplier + 2
            > vdev->notify_len) {
            DPrintf(0, (
                "%p: bad notification offset %u (x %u) "
                "for queue %u > %zd",
                vdev,
                off, vdev->notify_offset_multiplier,
                index, vdev->notify_len));
            status = STATUS_INVALID_PARAMETER;
            goto err_map_notify;
        }
        vq->priv = (void *)(vdev->notify_base +
            off * vdev->notify_offset_multiplier);
    }
    else {
        vq->priv = (void *)map_capability(vdev,
            vdev->notify_map_cap, 2, 2,
            off * vdev->notify_offset_multiplier, 2,
            NULL);
    }

    if (!vq->priv) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_map_notify;
    }

    if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
        msix_vec = vdev->device->set_queue_vector(vq, msix_vec);
        if (msix_vec == VIRTIO_MSI_NO_VECTOR) {
            status = STATUS_DEVICE_BUSY;
            goto err_assign_vector;
        }
    }

    /* enable the queue */
    iowrite16(vdev, 1, &vdev->common->queue_enable);

    *queue = vq;
    return STATUS_SUCCESS;

err_assign_vector:
    if (!vdev->notify_base)
        pci_unmap_address_range(vdev, (void *)vq->priv);
err_map_notify:
    virtqueue_shutdown(vq);
err_new_queue:
    mem_free_nonpaged_block(vdev, vq_addr);
    mem_free_contiguous_pages(vdev, info->queue);
    return status;
}

static void del_vq(VirtIOQueueInfo *info)
{
    struct virtqueue *vq = info->vq;
    VirtIODevice *vdev = vq->vdev;

    iowrite16(vdev, (u16)vq->index, &vdev->common->queue_select);

    if (vdev->msix_used) {
        iowrite16(vdev, VIRTIO_MSI_NO_VECTOR,
            &vdev->common->queue_msix_vector);
        /* Flush the write out to device */
        ioread16(vdev, &vdev->common->queue_msix_vector);
    }

    if (!vdev->notify_base)
        pci_unmap_address_range(vdev, (void *)vq->priv);

    virtqueue_shutdown(vq);

    mem_free_nonpaged_block(vdev, vq);
    mem_free_contiguous_pages(vdev, info->queue);
}

static const struct virtio_device_ops virtio_pci_device_ops = {
    .get_config = vp_get,
    .set_config = vp_set,
    .get_config_generation = vp_generation,
    .get_status = vp_get_status,
    .set_status = vp_set_status,
    .reset = vp_reset,
    .get_features = vp_get_features,
    .set_features = vp_set_features,
    .set_config_vector = vp_config_vector,
    .set_queue_vector = vp_queue_vector,
    .query_queue_alloc = query_vq_alloc,
    .setup_queue = setup_vq,
    .delete_queue = del_vq,
};

static u8 find_next_pci_vendor_capability(VirtIODevice *vdev, u8 offset)
{
    u8 id = 0;
    int iterations = 48;

    if (pci_read_config_byte(vdev, offset, &offset) != 0) {
        return 0;
    }

    while (iterations-- && offset >= 0x40) {
        offset &= ~3;
        if (pci_read_config_byte(vdev, offset + offsetof(PCI_CAPABILITIES_HEADER,
                CapabilityID), &id) != 0) {
            break;
        }
        if (id == 0xFF) {
            break;
        }
        if (id == PCI_CAPABILITY_ID_VENDOR_SPECIFIC) {
            return offset;
        }
        if (pci_read_config_byte(vdev, offset + offsetof(PCI_CAPABILITIES_HEADER,
                Next), &offset) != 0) {
            break;
        }
    }
    return 0;
}

static u8 find_first_pci_vendor_capability(VirtIODevice *vdev)
{
    u8 hdr_type, offset;
    u16 status;

    if (pci_read_config_byte(vdev, offsetof(PCI_COMMON_HEADER, HeaderType), &hdr_type) != 0) {
        return 0;
    }
    if (pci_read_config_word(vdev, offsetof(PCI_COMMON_HEADER, Status), &status) != 0) {
        return 0;
    }
    if ((status & PCI_STATUS_CAPABILITIES_LIST) == 0) {
        return 0;
    }

    switch (hdr_type & ~PCI_MULTIFUNCTION) {
    case PCI_BRIDGE_TYPE:
        offset = offsetof(PCI_COMMON_HEADER, u.type1.CapabilitiesPtr);
        break;
    case PCI_CARDBUS_BRIDGE_TYPE:
        offset = offsetof(PCI_COMMON_HEADER, u.type2.CapabilitiesPtr);
        break;
    default:
        offset = offsetof(PCI_COMMON_HEADER, u.type0.CapabilitiesPtr);
        break;
    }

    if (offset != 0) {
        offset = find_next_pci_vendor_capability(vdev, offset);
    }
    return offset;
}

static u8 find_pci_vendor_capability(VirtIODevice *vdev, u8 capability_type)
{
    u8 offset = find_first_pci_vendor_capability(vdev);
    while (offset > 0) {
        u8 cfg_type, bar;
        pci_read_config_byte(vdev, offset + offsetof(struct virtio_pci_cap, cfg_type), &cfg_type);
        pci_read_config_byte(vdev, offset + offsetof(struct virtio_pci_cap, bar), &bar);

        if (bar < PCI_TYPE0_ADDRESSES &&
            cfg_type == capability_type &&
            pci_get_resource_len(vdev, bar) > 0) {
            return offset;
        }

        offset = find_next_pci_vendor_capability(vdev, offset + offsetof(PCI_CAPABILITIES_HEADER, Next));
    }
    return 0;
}

/* the PCI probing function */
NTSTATUS virtio_pci_modern_probe(VirtIODevice *vdev)
{
    int common, isr, notify, config;
    u32 notify_length;
    u32 notify_offset;
    NTSTATUS status;

    /* check for a common config: if not, use legacy mode (bar 0). */
    common = find_pci_vendor_capability(vdev, VIRTIO_PCI_CAP_COMMON_CFG);
    if (!common) {
        DPrintf(0, ("virtio_pci: %p: leaving for legacy driver\n", vdev));
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    /* If common is there, these should be too... */
    isr = find_pci_vendor_capability(vdev, VIRTIO_PCI_CAP_ISR_CFG);
    notify = find_pci_vendor_capability(vdev, VIRTIO_PCI_CAP_NOTIFY_CFG);
    if (!isr || !notify) {
        DPrintf(0, ("virtio_pci: %p: missing capabilities %i/%i/%i\n",
            vdev, common, isr, notify));
        return STATUS_INVALID_PARAMETER;
    }

    /* Device capability is only mandatory for devices that have
     * device-specific configuration.
     */
    config = find_pci_vendor_capability(vdev, VIRTIO_PCI_CAP_DEVICE_CFG);

    status = STATUS_INVALID_PARAMETER;
    vdev->common = map_capability(vdev, common,
        sizeof(struct virtio_pci_common_cfg), 4,
        0, sizeof(struct virtio_pci_common_cfg),
        NULL);
    if (!vdev->common)
        goto err_map_common;
    vdev->isr = map_capability(vdev, isr, sizeof(u8), 1,
        0, 1,
        NULL);
    if (!vdev->isr)
        goto err_map_isr;

    /* Read notify_off_multiplier from config space. */
    pci_read_config_dword(vdev,
        notify + offsetof(struct virtio_pci_notify_cap,
        notify_off_multiplier),
        &vdev->notify_offset_multiplier);
    /* Read notify length and offset from config space. */
    pci_read_config_dword(vdev,
        notify + offsetof(struct virtio_pci_notify_cap,
        cap.length),
        &notify_length);

    pci_read_config_dword(vdev,
        notify + offsetof(struct virtio_pci_notify_cap,
        cap.offset),
        &notify_offset);

    /* We don't know how many VQs we'll map, ahead of the time.
     * If notify length is small, map it all now.
     * Otherwise, map each VQ individually later.
     */
    if ((u64)notify_length + (notify_offset % PAGE_SIZE) <= PAGE_SIZE) {
        vdev->notify_base = map_capability(vdev, notify, 2, 2,
            0, notify_length,
            &vdev->notify_len);
        if (!vdev->notify_base)
            goto err_map_notify;
    }
    else {
        vdev->notify_map_cap = notify;
    }

    /* Again, we don't know how much we should map, but PAGE_SIZE
     * is more than enough for all existing devices.
     */
    if (config) {
        vdev->config = map_capability(vdev, config, 0, 4,
            0, PAGE_SIZE,
            &vdev->config_len);
        if (!vdev->config)
            goto err_map_config;

    }

    vdev->device = &virtio_pci_device_ops;

    return STATUS_SUCCESS;

err_map_config:
    if (vdev->notify_base)
        pci_unmap_address_range(vdev, (void *)vdev->notify_base);
err_map_notify:
    pci_unmap_address_range(vdev, vdev->isr);
err_map_isr:
    pci_unmap_address_range(vdev, vdev->common);
err_map_common:
    return status;
}

void virtio_pci_modern_remove(VirtIODevice *vdev)
{
    if (vdev->config)
        pci_unmap_address_range(vdev, vdev->config);
    if (vdev->notify_base)
        pci_unmap_address_range(vdev, (void *)vdev->notify_base);
    pci_unmap_address_range(vdev, vdev->isr);
    pci_unmap_address_range(vdev, vdev->common);
}
