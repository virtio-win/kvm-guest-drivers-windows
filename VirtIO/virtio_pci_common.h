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

#define ioread8(addr) ReadVirtIODeviceByte((ULONG_PTR)(addr))
#define ioread16(addr) ReadVirtIODeviceWord((ULONG_PTR)(addr))
#define ioread32(addr) ReadVirtIODeviceRegister((ULONG_PTR)(addr))
#define iowrite8(val, addr) WriteVirtIODeviceByte((ULONG_PTR)(addr), val)
#define iowrite16(val, addr) WriteVirtIODeviceWord((ULONG_PTR)(addr), val)
#define iowrite32(val, addr) WriteVirtIODeviceRegister((ULONG_PTR)(addr), val)

#define alloc_pages_exact(vp_dev, size, gfp_mask) \
    vp_dev->system->alloc_contiguous_pages(vp_dev->DeviceContext, size)
#define free_pages_exact(vp_dev, addr, size) \
    vp_dev->system->free_contiguous_pages(vp_dev->DeviceContext, addr, size)
#define virt_to_phys(vp_dev, addr) \
    vp_dev->system->virt_to_phys(vp_dev->DeviceContext, addr)
#define kmalloc(vp_dev, size, gfp_mask) \
    vp_dev->system->kmalloc(vp_dev->DeviceContext, size)
#define kfree(vp_dev, addr) \
    vp_dev->system->kfree(vp_dev->DeviceContext, addr)
#define msleep(vp_dev, msecs) \
    vp_dev->system->msleep(vp_dev->DeviceContext, msecs)

#define pci_read_config_byte(vp_dev, where, bVal) \
    vp_dev->system->pci_read_config_byte(vp_dev->DeviceContext, where, bVal)
#define pci_read_config_word(vp_dev, where, wVal) \
    vp_dev->system->pci_read_config_word(vp_dev->DeviceContext, where, wVal)
#define pci_read_config_dword(vp_dev, where, dwVal) \
    vp_dev->system->pci_read_config_dword(vp_dev->DeviceContext, where, dwVal)

#define pci_resource_len(vp_dev, bar) \
    vp_dev->system->pci_get_resource_len(vp_dev->DeviceContext, bar)
#define pci_resource_flags(vp_dev, bar) \
    vp_dev->system->pci_get_resource_flags(vp_dev->DeviceContext, bar)
#define pci_get_msix_vector(vp_dev, queue) \
    vp_dev->system->pci_get_msix_vector(vp_dev->DeviceContext, queue)
#define pci_iomap_range(vp_dev, bar, offset, maxlen) \
    vp_dev->system->pci_iomap_range(vp_dev->DeviceContext, bar, offset, maxlen)
#define pci_iounmap(vp_dev, address) \
    vp_dev->system->pci_iounmap(vp_dev->DeviceContext, address)

#define to_vp_device(x) x

/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq);
/* the config->del_vqs() implementation */
void vp_del_vqs(virtio_device *vdev);
/* the config->find_vqs() implementation */
int vp_find_vqs(virtio_device *vdev, unsigned nvqs,
                struct virtqueue *vqs[],
                vq_callback_t *callbacks[],
                const char * const names[]);

int pci_find_capability(virtio_device *vdev, int cap);
int pci_find_next_capability(virtio_device *vdev, u8 pos, int cap);

int virtio_pci_legacy_probe(virtio_pci_device *vp_dev);
void virtio_pci_legacy_remove(virtio_pci_device *vp_dev);

int virtio_pci_modern_probe(virtio_pci_device *vp_dev);
void virtio_pci_modern_remove(virtio_pci_device *vp_dev);

#endif