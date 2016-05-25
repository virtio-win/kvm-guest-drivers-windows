#ifndef _DRIVERS_VIRTIO_VIRTIO_PCI_COMMON_H
#define _DRIVERS_VIRTIO_VIRTIO_PCI_COMMON_H
/*
 * Virtio PCI driver - APIs for common functionality for all device versions
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

#include "virtio_config.h"

#define ioread8(vp_dev, addr) \
    vp_dev->system->vdev_read_byte((ULONG_PTR)(addr))
#define ioread16(vp_dev, addr) \
    vp_dev->system->vdev_read_word((ULONG_PTR)(addr))
#define ioread32(vp_dev, addr) \
    vp_dev->system->vdev_read_dword((ULONG_PTR)(addr))
#define iowrite8(vp_dev, val, addr) \
    vp_dev->system->vdev_write_byte((ULONG_PTR)(addr), val)
#define iowrite16(vp_dev, val, addr) \
    vp_dev->system->vdev_write_word((ULONG_PTR)(addr), val)
#define iowrite32(vp_dev, val, addr) \
    vp_dev->system->vdev_write_dword((ULONG_PTR)(addr), val)

#define mem_alloc_contiguous_pages(vp_dev, size) \
    vp_dev->system->mem_alloc_contiguous_pages(vp_dev->DeviceContext, size)
#define mem_free_contiguous_pages(vp_dev, virt) \
    vp_dev->system->mem_free_contiguous_pages(vp_dev->DeviceContext, virt)
#define mem_get_physical_address(vp_dev, virt) \
    vp_dev->system->mem_get_physical_address(vp_dev->DeviceContext, virt)
#define mem_alloc_nonpaged_block(vp_dev, size) \
    vp_dev->system->mem_alloc_nonpaged_block(vp_dev->DeviceContext, size)
#define mem_free_nonpaged_block(vp_dev, addr) \
    vp_dev->system->mem_free_nonpaged_block(vp_dev->DeviceContext, addr)

#define pci_read_config_byte(vp_dev, where, bVal) \
    vp_dev->system->pci_read_config_byte(vp_dev->DeviceContext, where, bVal)
#define pci_read_config_word(vp_dev, where, wVal) \
    vp_dev->system->pci_read_config_word(vp_dev->DeviceContext, where, wVal)
#define pci_read_config_dword(vp_dev, where, dwVal) \
    vp_dev->system->pci_read_config_dword(vp_dev->DeviceContext, where, dwVal)

#define pci_get_resource_len(vp_dev, bar) \
    vp_dev->system->pci_get_resource_len(vp_dev->DeviceContext, bar)
#define pci_map_address_range(vp_dev, bar, offset, maxlen) \
    vp_dev->system->pci_map_address_range(vp_dev->DeviceContext, bar, offset, maxlen)
#define pci_unmap_address_range(vp_dev, address) \
    vp_dev->system->pci_unmap_address_range(vp_dev->DeviceContext, address)

#define vdev_get_msix_vector(vp_dev, queue) \
    vp_dev->system->vdev_get_msix_vector(vp_dev->DeviceContext, queue)
#define vdev_sleep(vp_dev, msecs) \
    vp_dev->system->vdev_sleep(vp_dev->DeviceContext, msecs)

#define to_vp_device(x) x

/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq);
/* the config->del_vqs() implementation */
void vp_del_vqs(virtio_device *vdev);
/* the config->del_vq() implementation */
void vp_del_vq(struct virtqueue *vq);
/* the config->find_vqs() implementation */
NTSTATUS vp_find_vqs(virtio_device *vdev, unsigned nvqs,
                     struct virtqueue *vqs[],
                     const char * const names[]);
/* the config->find_vq() implementation */
NTSTATUS vp_find_vq(virtio_device *vdev, unsigned index,
                    struct virtqueue **vq,
                    const char *name);

int pci_find_capability(virtio_device *vdev, int cap);
int pci_find_next_capability(virtio_device *vdev, u8 pos, int cap);

NTSTATUS virtio_pci_legacy_probe(virtio_pci_device *vp_dev);
void virtio_pci_legacy_remove(virtio_pci_device *vp_dev);

NTSTATUS virtio_pci_modern_probe(virtio_pci_device *vp_dev);
void virtio_pci_modern_remove(virtio_pci_device *vp_dev);

#endif