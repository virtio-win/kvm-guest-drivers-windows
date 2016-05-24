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
#include "VirtIO_PCI.h"
#include "virtio_config.h"
#include "virtio.h"
#include "kdebugprint.h"
#include "VirtIO_Ring.h"
#include "virtio_pci_common.h"
#include "windows\virtio_ring_allocation.h"
#include <stddef.h>

#ifdef WPP_EVENT_TRACING
#include "VirtIOPCIModern.tmh"
#endif

/*
 * Type-safe wrappers for io accesses.
 * Use these to enforce at compile time the following spec requirement:
 *
 * The driver MUST access each field using the “natural” access
 * method, i.e. 32-bit accesses for 32-bit fields, 16-bit accesses
 * for 16-bit fields and 8-bit accesses for 8-bit fields.
 */
static inline u8 vp_ioread8(u8 *addr)
{
    return ioread8(addr);
}
static inline u16 vp_ioread16(u16 *addr)
{
    return ioread16(addr);
}

static inline u32 vp_ioread32(u32 *addr)
{
    return ioread32(addr);
}

static inline void vp_iowrite8(u8 value, u8 *addr)
{
    iowrite8(value, addr);
}

static inline void vp_iowrite16(u16 value, u16 *addr)
{
    iowrite16(value, addr);
}

static inline void vp_iowrite32(u32 value, u32 *addr)
{
    iowrite32(value, addr);
}

static void vp_iowrite64_twopart(u64 val,
                                 __le32 *lo, __le32 *hi)
{
    vp_iowrite32((u32)val, lo);
    vp_iowrite32(val >> 32, hi);
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

/* virtio config->get_features() implementation */
static u64 vp_get_features(virtio_device *vdev)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    u64 features;

    vp_iowrite32(0, &vp_dev->common->device_feature_select);
    features = vp_ioread32(&vp_dev->common->device_feature);
    vp_iowrite32(1, &vp_dev->common->device_feature_select);
    features |= ((u64)vp_ioread32(&vp_dev->common->device_feature) << 32);

    return features;
}

/* virtio config->finalize_features() implementation */
static NTSTATUS vp_finalize_features(virtio_device *vdev)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);

    /* Give virtio_ring a chance to accept features. */
    vring_transport_features(vdev);

    if (!__virtio_test_bit(vdev, VIRTIO_F_VERSION_1)) {
        DPrintf(0, ("virtio: device uses modern interface but does not have VIRTIO_F_VERSION_1\n"));
        return STATUS_INVALID_PARAMETER;
    }

    vp_iowrite32(0, &vp_dev->common->guest_feature_select);
    vp_iowrite32((u32)vdev->features, &vp_dev->common->guest_feature);
    vp_iowrite32(1, &vp_dev->common->guest_feature_select);
    vp_iowrite32(vdev->features >> 32, &vp_dev->common->guest_feature);

    return STATUS_SUCCESS;
}

/* virtio config->set_msi_vector() implementation */
static u16 vp_set_msi_vector(struct virtqueue *vq, u16 vector)
{
    virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
    struct virtio_pci_common_cfg *cfg = vp_dev->common;

    vp_iowrite16((u16)vq->index, &cfg->queue_select);
    vp_iowrite16(vector, &cfg->queue_msix_vector);
    return vp_ioread16(&cfg->queue_msix_vector);
}

/* virtio config->get() implementation */
static void vp_get(virtio_device *vdev, unsigned offset,
                   void *buf, unsigned len)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    u8 b;
    __le16 w;
    __le32 l;

    BUG_ON(offset + len > vp_dev->device_len);

    switch (len) {
    case 1:
        b = ioread8(vp_dev->device + offset);
        memcpy(buf, &b, sizeof b);
        break;
    case 2:
        w = ioread16(vp_dev->device + offset);
        memcpy(buf, &w, sizeof w);
        break;
    case 4:
        l = ioread32(vp_dev->device + offset);
        memcpy(buf, &l, sizeof l);
        break;
    case 8:
        l = ioread32(vp_dev->device + offset);
        memcpy(buf, &l, sizeof l);
        l = ioread32(vp_dev->device + offset + sizeof l);
        memcpy((unsigned char *)buf + sizeof l, &l, sizeof l);
        break;
    default:
        BUG();
    }
}

/* the config->set() implementation.  it's symmetric to the config->get()
 * implementation */
static void vp_set(virtio_device *vdev, unsigned offset,
                   const void *buf, unsigned len)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    u8 b;
    __le16 w;
    __le32 l;

    BUG_ON(offset + len > vp_dev->device_len);

    switch (len) {
    case 1:
        memcpy(&b, buf, sizeof b);
        iowrite8(b, vp_dev->device + offset);
        break;
    case 2:
        memcpy(&w, buf, sizeof w);
        iowrite16(w, vp_dev->device + offset);
        break;
    case 4:
        memcpy(&l, buf, sizeof l);
        iowrite32(l, vp_dev->device + offset);
        break;
    case 8:
        memcpy(&l, buf, sizeof l);
        iowrite32(l, vp_dev->device + offset);
        memcpy(&l, (unsigned char *)buf + sizeof l, sizeof l);
        iowrite32(l, vp_dev->device + offset + sizeof l);
        break;
    default:
        BUG();
    }
}

static u32 vp_generation(virtio_device *vdev)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    return vp_ioread8(&vp_dev->common->config_generation);
}

/* config->{get,set}_status() implementations */
static u8 vp_get_status(virtio_device *vdev)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    return vp_ioread8(&vp_dev->common->device_status);
}

static void vp_set_status(virtio_device *vdev, u8 status)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    /* We should never be setting status to 0. */
    BUG_ON(status == 0);
    vp_iowrite8(status, &vp_dev->common->device_status);
}

static void vp_reset(virtio_device *vdev)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    /* 0 status means a reset. */
    vp_iowrite8(0, &vp_dev->common->device_status);
    /* After writing 0 to device_status, the driver MUST wait for a read of
    * device_status to return 0 before reinitializing the device.
    * This will flush out the status write, and flush in device writes,
    * including MSI-X interrupts, if any.
    */
    while (vp_ioread8(&vp_dev->common->device_status)) {
        vdev_sleep(vp_dev, 1);
    }
}

static u16 vp_config_vector(virtio_pci_device *vp_dev, u16 vector)
{
    /* Setup the vector used for configuration events */
    vp_iowrite16(vector, &vp_dev->common->msix_config);
    /* Verify we had enough resources to assign the vector */
    /* Will also flush the write out to device */
    return vp_ioread16(&vp_dev->common->msix_config);
}

static size_t vring_pci_size(u16 num)
{
    /* We only need a cacheline separation. */
    return (size_t)ROUND_TO_PAGES(vring_size(num, SMP_CACHE_BYTES));
}

static void *alloc_virtqueue_pages(virtio_pci_device *vp_dev, u16 *num)
{
    void *pages;
    
    /* TODO: allocate each queue chunk individually */
    for (; *num && vring_pci_size(*num) > PAGE_SIZE; *num /= 2) {
        pages = mem_alloc_contiguous_pages(vp_dev, vring_pci_size(*num));
        if (pages)
            return pages;
    }
    
    if (!*num)
        return NULL;
    
    /* Try to get a single page. You are my only hope! */
    return mem_alloc_contiguous_pages(vp_dev, vring_pci_size(*num));
}

static NTSTATUS query_vq_alloc(virtio_pci_device *vp_dev,
                          unsigned index,
                          unsigned short *pNumEntries,
                          unsigned long *pAllocationSize,
                          unsigned long *pHeapSize)
{
    struct virtio_pci_common_cfg *cfg = vp_dev->common;
    u16 num;

    if (index >= vp_ioread16(&cfg->num_queues))
        return STATUS_NOT_FOUND;

    /* Select the queue we're interested in */
    vp_iowrite16((u16)index, &cfg->queue_select);

    /* Check if queue is either not available or already active. */
    num = vp_ioread16(&cfg->queue_size);
    /* QEMU has a bug where queues don't revert to inactive on device
     * reset. Skip checking the queue_enable field until it is fixed.
     */
    if (!num /*|| vp_ioread16(&cfg->queue_enable)*/)
        return STATUS_NOT_FOUND;

    if (num & (num - 1)) {
        DPrintf(0, ("%p: bad queue size %u", vp_dev, num));
        return STATUS_INVALID_PARAMETER;
    }

    *pNumEntries = num;
    *pAllocationSize = (unsigned long)vring_pci_size(num);
    *pHeapSize = vring_control_block_size() + sizeof(void *) * num;

    return STATUS_SUCCESS;
}

static NTSTATUS setup_vq(struct virtqueue **queue,
                         virtio_pci_device *vp_dev,
                         virtio_pci_vq_info *info,
                         unsigned index,
                         const char *name,
                         u16 msix_vec)
{
    struct virtio_pci_common_cfg *cfg = vp_dev->common;
    struct virtqueue *vq;
    void *vq_addr;
    u16 off;
    unsigned long size, heap_size;
    NTSTATUS status;

    /* Select the queue and query allocation parameters */
    status = query_vq_alloc(vp_dev, index, &info->num, &size, &heap_size);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* get offset of notification word for this vq */
    off = vp_ioread16(&cfg->queue_notify_off);

    info->queue = alloc_virtqueue_pages(vp_dev, &info->num);
    if (info->queue == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    vq_addr = mem_alloc_nonpaged_block(vp_dev, heap_size);
    if (vq_addr == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* create the vring */
    vq = vring_new_virtqueue(index, info->num,
        SMP_CACHE_BYTES, vp_dev,
        true, info->queue, vp_notify, vq_addr, name);
    if (!vq) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_new_queue;
    }

    /* activate the queue */
    vp_iowrite16(info->num, &cfg->queue_size);
    vp_iowrite64_twopart(mem_get_physical_address(vp_dev, info->queue),
        &cfg->queue_desc_lo, &cfg->queue_desc_hi);
    vp_iowrite64_twopart(mem_get_physical_address(vp_dev, virtqueue_get_avail(vq)),
        &cfg->queue_avail_lo, &cfg->queue_avail_hi);
    vp_iowrite64_twopart(mem_get_physical_address(vp_dev, virtqueue_get_used(vq)),
        &cfg->queue_used_lo, &cfg->queue_used_hi);

    if (vp_dev->notify_base) {
        /* offset should not wrap */
        if ((u64)off * vp_dev->notify_offset_multiplier + 2
            > vp_dev->notify_len) {
            DPrintf(0, (
                "%p: bad notification offset %u (x %u) "
                "for queue %u > %zd",
                vp_dev,
                off, vp_dev->notify_offset_multiplier,
                index, vp_dev->notify_len));
            status = STATUS_INVALID_PARAMETER;
            goto err_map_notify;
        }
        vq->priv = (void *)(vp_dev->notify_base +
            off * vp_dev->notify_offset_multiplier);
    }
    else {
        vq->priv = (void *)map_capability(vp_dev,
            vp_dev->notify_map_cap, 2, 2,
            off * vp_dev->notify_offset_multiplier, 2,
            NULL);
    }

    if (!vq->priv) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_map_notify;
    }

    if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
        vp_iowrite16(msix_vec, &cfg->queue_msix_vector);
        msix_vec = vp_ioread16(&cfg->queue_msix_vector);
        if (msix_vec == VIRTIO_MSI_NO_VECTOR) {
            status = STATUS_DEVICE_BUSY;
            goto err_assign_vector;
        }
    }

    *queue = vq;
    return STATUS_SUCCESS;

err_assign_vector:
    if (!vp_dev->notify_base)
        pci_unmap_address_range(vp_dev, (void *)vq->priv);
err_map_notify:
    virtqueue_shutdown(vq);
err_new_queue:
    mem_free_nonpaged_block(vp_dev, vq_addr);
    mem_free_contiguous_pages(vp_dev, info->queue);
    return status;
}

static NTSTATUS vp_modern_find_vqs(virtio_device *vdev, unsigned nvqs,
                                   struct virtqueue *vqs[],
                                   const char * const names[])
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    struct virtqueue *vq;
    unsigned i;
    NTSTATUS status;
    
    status = vp_find_vqs(vdev, nvqs, vqs, (const char **)names);

    if (NT_SUCCESS(status)) {
        /* Select and activate all queues. Has to be done last: once we do
         * this, there's no way to go back except reset.
         */
        for (i = 0; i < nvqs; i++) {
            if ((vq = vqs[i]) != NULL) {
                vp_iowrite16((u16)vq->index, &vp_dev->common->queue_select);
                vp_iowrite16(1, &vp_dev->common->queue_enable);
            }
        }
    }

    return status;
}

static NTSTATUS vp_modern_find_vq(virtio_device *vdev, unsigned index,
                                  struct virtqueue **vq,
                                  const char *name)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    NTSTATUS status;

    status = vp_find_vq(vdev, index, vq, name);

    if (NT_SUCCESS(status)) {
        /* Select and activate the queue. Has to be done last: once we do
         * this, there's no way to go back except reset.
         */
        vp_iowrite16((u16)index, &vp_dev->common->queue_select);
        vp_iowrite16(1, &vp_dev->common->queue_enable);
    }

    return status;
}

static void del_vq(virtio_pci_vq_info *info)
{
    struct virtqueue *vq = info->vq;
    virtio_pci_device *vp_dev = to_vp_device(vq->vdev);

    vp_iowrite16((u16)vq->index, &vp_dev->common->queue_select);

    if (vp_dev->msix_used) {
        vp_iowrite16(VIRTIO_MSI_NO_VECTOR,
            &vp_dev->common->queue_msix_vector);
        /* Flush the write out to device */
        vp_ioread16(&vp_dev->common->queue_msix_vector);
    }

    if (!vp_dev->notify_base)
        pci_unmap_address_range(vp_dev, (void *)vq->priv);

    virtqueue_shutdown(vq);

    mem_free_nonpaged_block(vp_dev, vq);
    mem_free_contiguous_pages(vp_dev, info->queue);
}

static const struct virtio_config_ops virtio_pci_config_nodev_ops = {
    .get = NULL,
    .set = NULL,
    .generation = vp_generation,
    .get_status = vp_get_status,
    .set_status = vp_set_status,
    .reset = vp_reset,
    .find_vqs = vp_modern_find_vqs,
    .find_vq = vp_modern_find_vq,
    .del_vqs = vp_del_vqs,
    .del_vq = vp_del_vq,
    .get_features = vp_get_features,
    .finalize_features = vp_finalize_features,
    .set_msi_vector = vp_set_msi_vector,
};

static const struct virtio_config_ops virtio_pci_config_ops = {
    .get = vp_get,
    .set = vp_set,
    .generation = vp_generation,
    .get_status = vp_get_status,
    .set_status = vp_set_status,
    .reset = vp_reset,
    .find_vqs = vp_modern_find_vqs,
    .find_vq = vp_modern_find_vq,
    .del_vqs = vp_del_vqs,
    .del_vq = vp_del_vq,
    .get_features = vp_get_features,
    .finalize_features = vp_finalize_features,
    .set_msi_vector = vp_set_msi_vector,
};

/**
 * virtio_pci_find_capability - walk capabilities to find device info.
 * @dev: the pci device
 * @cfg_type: the VIRTIO_PCI_CAP_* value we seek
 *
 * Returns offset of the capability, or 0.
 */
static inline int virtio_pci_find_capability(virtio_pci_device *vp_dev, u8 cfg_type)
{
    int pos;

    for (pos = pci_find_capability(vp_dev, PCI_CAPABILITY_ID_VENDOR_SPECIFIC);
        pos > 0;
        pos = pci_find_next_capability(vp_dev, (u8)pos, PCI_CAPABILITY_ID_VENDOR_SPECIFIC)) {
        u8 type, bar;
        pci_read_config_byte(vp_dev, pos + offsetof(struct virtio_pci_cap,
            cfg_type),
            &type);
        pci_read_config_byte(vp_dev, pos + offsetof(struct virtio_pci_cap,
            bar),
            &bar);
        
        /* Ignore structures with reserved BAR values */
        if (bar > 0x5)
            continue;

        if (type == cfg_type) {
            if (pci_get_resource_len(vp_dev, bar)) {
                return pos;
            }
        }
    }
    return 0;
}

/* This is part of the ABI.  Don't screw with it. */
static inline void check_offsets(void)
{
    /* Note: disk space was harmed in compilation of this function. */
    BUILD_BUG_ON(VIRTIO_PCI_CAP_VNDR !=
        offsetof(struct virtio_pci_cap, cap_vndr));
    BUILD_BUG_ON(VIRTIO_PCI_CAP_NEXT !=
        offsetof(struct virtio_pci_cap, cap_next));
    BUILD_BUG_ON(VIRTIO_PCI_CAP_LEN !=
        offsetof(struct virtio_pci_cap, cap_len));
    BUILD_BUG_ON(VIRTIO_PCI_CAP_CFG_TYPE !=
        offsetof(struct virtio_pci_cap, cfg_type));
    BUILD_BUG_ON(VIRTIO_PCI_CAP_BAR !=
        offsetof(struct virtio_pci_cap, bar));
    BUILD_BUG_ON(VIRTIO_PCI_CAP_OFFSET !=
        offsetof(struct virtio_pci_cap, offset));
    BUILD_BUG_ON(VIRTIO_PCI_CAP_LENGTH !=
        offsetof(struct virtio_pci_cap, length));
    BUILD_BUG_ON(VIRTIO_PCI_NOTIFY_CAP_MULT !=
        offsetof(struct virtio_pci_notify_cap,
        notify_off_multiplier));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_DFSELECT !=
        offsetof(struct virtio_pci_common_cfg,
        device_feature_select));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_DF !=
        offsetof(struct virtio_pci_common_cfg, device_feature));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_GFSELECT !=
        offsetof(struct virtio_pci_common_cfg,
        guest_feature_select));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_GF !=
        offsetof(struct virtio_pci_common_cfg, guest_feature));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_MSIX !=
        offsetof(struct virtio_pci_common_cfg, msix_config));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_NUMQ !=
        offsetof(struct virtio_pci_common_cfg, num_queues));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_STATUS !=
        offsetof(struct virtio_pci_common_cfg, device_status));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_CFGGENERATION !=
        offsetof(struct virtio_pci_common_cfg, config_generation));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_SELECT !=
        offsetof(struct virtio_pci_common_cfg, queue_select));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_SIZE !=
        offsetof(struct virtio_pci_common_cfg, queue_size));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_MSIX !=
        offsetof(struct virtio_pci_common_cfg, queue_msix_vector));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_ENABLE !=
        offsetof(struct virtio_pci_common_cfg, queue_enable));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_NOFF !=
        offsetof(struct virtio_pci_common_cfg, queue_notify_off));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_DESCLO !=
        offsetof(struct virtio_pci_common_cfg, queue_desc_lo));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_DESCHI !=
        offsetof(struct virtio_pci_common_cfg, queue_desc_hi));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_AVAILLO !=
        offsetof(struct virtio_pci_common_cfg, queue_avail_lo));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_AVAILHI !=
        offsetof(struct virtio_pci_common_cfg, queue_avail_hi));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_USEDLO !=
        offsetof(struct virtio_pci_common_cfg, queue_used_lo));
    BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_USEDHI !=
        offsetof(struct virtio_pci_common_cfg, queue_used_hi));
}

/* the PCI probing function */
NTSTATUS virtio_pci_modern_probe(virtio_pci_device *vp_dev)
{
    int common, isr, notify, device;
    u32 notify_length;
    u32 notify_offset;
    NTSTATUS status;

    /* check for a common config: if not, use legacy mode (bar 0). */
    common = virtio_pci_find_capability(vp_dev, VIRTIO_PCI_CAP_COMMON_CFG);
    if (!common) {
        DPrintf(0, ("virtio_pci: %p: leaving for legacy driver\n", vp_dev));
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    /* If common is there, these should be too... */
    isr = virtio_pci_find_capability(vp_dev, VIRTIO_PCI_CAP_ISR_CFG);
    notify = virtio_pci_find_capability(vp_dev, VIRTIO_PCI_CAP_NOTIFY_CFG);
    if (!isr || !notify) {
        DPrintf(0, ("virtio_pci: %p: missing capabilities %i/%i/%i\n",
            vp_dev, common, isr, notify));
        return STATUS_INVALID_PARAMETER;
    }

    /* Device capability is only mandatory for devices that have
     * device-specific configuration.
     */
    device = virtio_pci_find_capability(vp_dev, VIRTIO_PCI_CAP_DEVICE_CFG);

    status = STATUS_INVALID_PARAMETER;
    vp_dev->common = map_capability(vp_dev, common,
        sizeof(struct virtio_pci_common_cfg), 4,
        0, sizeof(struct virtio_pci_common_cfg),
        NULL);
    if (!vp_dev->common)
        goto err_map_common;
    vp_dev->isr = map_capability(vp_dev, isr, sizeof(u8), 1,
        0, 1,
        NULL);
    if (!vp_dev->isr)
        goto err_map_isr;

    /* Read notify_off_multiplier from config space. */
    pci_read_config_dword(vp_dev,
        notify + offsetof(struct virtio_pci_notify_cap,
        notify_off_multiplier),
        &vp_dev->notify_offset_multiplier);
    /* Read notify length and offset from config space. */
    pci_read_config_dword(vp_dev,
        notify + offsetof(struct virtio_pci_notify_cap,
        cap.length),
        &notify_length);

    pci_read_config_dword(vp_dev,
        notify + offsetof(struct virtio_pci_notify_cap,
        cap.offset),
        &notify_offset);

    /* We don't know how many VQs we'll map, ahead of the time.
     * If notify length is small, map it all now.
     * Otherwise, map each VQ individually later.
     */
    if ((u64)notify_length + (notify_offset % PAGE_SIZE) <= PAGE_SIZE) {
        vp_dev->notify_base = map_capability(vp_dev, notify, 2, 2,
            0, notify_length,
            &vp_dev->notify_len);
        if (!vp_dev->notify_base)
            goto err_map_notify;
    }
    else {
        vp_dev->notify_map_cap = notify;
    }

    /* Again, we don't know how much we should map, but PAGE_SIZE
     * is more than enough for all existing devices.
     */
    if (device) {
        vp_dev->device = map_capability(vp_dev, device, 0, 4,
            0, PAGE_SIZE,
            &vp_dev->device_len);
        if (!vp_dev->device)
            goto err_map_device;

        vp_dev->config = &virtio_pci_config_ops;
    }
    else {
        vp_dev->config = &virtio_pci_config_nodev_ops;
    }

    vp_dev->config_vector = vp_config_vector;
    vp_dev->query_vq_alloc = query_vq_alloc;
    vp_dev->setup_vq = setup_vq;
    vp_dev->del_vq = del_vq;

    return STATUS_SUCCESS;

err_map_device:
    if (vp_dev->notify_base)
        pci_unmap_address_range(vp_dev, (void *)vp_dev->notify_base);
err_map_notify:
    pci_unmap_address_range(vp_dev, vp_dev->isr);
err_map_isr:
    pci_unmap_address_range(vp_dev, vp_dev->common);
err_map_common:
    return status;
}

void virtio_pci_modern_remove(virtio_pci_device *vp_dev)
{
    if (vp_dev->device)
        pci_unmap_address_range(vp_dev, vp_dev->device);
    if (vp_dev->notify_base)
        pci_unmap_address_range(vp_dev, (void *)vp_dev->notify_base);
    pci_unmap_address_range(vp_dev, vp_dev->isr);
    pci_unmap_address_range(vp_dev, vp_dev->common);
}
