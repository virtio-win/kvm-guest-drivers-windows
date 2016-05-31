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

#define ioread8(vdev, addr) \
    vdev->system->vdev_read_byte((ULONG_PTR)(addr))
#define ioread16(vdev, addr) \
    vdev->system->vdev_read_word((ULONG_PTR)(addr))
#define ioread32(vdev, addr) \
    vdev->system->vdev_read_dword((ULONG_PTR)(addr))
#define iowrite8(vdev, val, addr) \
    vdev->system->vdev_write_byte((ULONG_PTR)(addr), val)
#define iowrite16(vdev, val, addr) \
    vdev->system->vdev_write_word((ULONG_PTR)(addr), val)
#define iowrite32(vdev, val, addr) \
    vdev->system->vdev_write_dword((ULONG_PTR)(addr), val)

#define mem_alloc_contiguous_pages(vdev, size) \
    vdev->system->mem_alloc_contiguous_pages(vdev->DeviceContext, size)
#define mem_free_contiguous_pages(vdev, virt) \
    vdev->system->mem_free_contiguous_pages(vdev->DeviceContext, virt)
#define mem_get_physical_address(vdev, virt) \
    vdev->system->mem_get_physical_address(vdev->DeviceContext, virt)
#define mem_alloc_nonpaged_block(vdev, size) \
    vdev->system->mem_alloc_nonpaged_block(vdev->DeviceContext, size)
#define mem_free_nonpaged_block(vdev, addr) \
    vdev->system->mem_free_nonpaged_block(vdev->DeviceContext, addr)

#define pci_read_config_byte(vdev, where, bVal) \
    vdev->system->pci_read_config_byte(vdev->DeviceContext, where, bVal)
#define pci_read_config_word(vdev, where, wVal) \
    vdev->system->pci_read_config_word(vdev->DeviceContext, where, wVal)
#define pci_read_config_dword(vdev, where, dwVal) \
    vdev->system->pci_read_config_dword(vdev->DeviceContext, where, dwVal)

#define pci_get_resource_len(vdev, bar) \
    vdev->system->pci_get_resource_len(vdev->DeviceContext, bar)
#define pci_map_address_range(vdev, bar, offset, maxlen) \
    vdev->system->pci_map_address_range(vdev->DeviceContext, bar, offset, maxlen)
#define pci_unmap_address_range(vdev, address) \
    vdev->system->pci_unmap_address_range(vdev->DeviceContext, address)

#define vdev_get_msix_vector(vdev, queue) \
    vdev->system->vdev_get_msix_vector(vdev->DeviceContext, queue)
#define vdev_sleep(vdev, msecs) \
    vdev->system->vdev_sleep(vdev->DeviceContext, msecs)

 /**
 * virtio_has_feature - helper to determine if this device has this feature.
 * @vdev: the device
 * @fbit: the feature bit
 */
static inline bool virtio_has_feature(const VirtIODevice *vdev,
    unsigned int fbit)
{
    BUG_ON(fbit >= 64);
    return virtio_is_feature_enabled(vdev->features, fbit);
}

/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq);

NTSTATUS virtio_pci_legacy_probe(VirtIODevice *vdev);
void virtio_pci_legacy_remove(VirtIODevice *vdev);

NTSTATUS virtio_pci_modern_probe(VirtIODevice *vdev);
void virtio_pci_modern_remove(VirtIODevice *vdev);

#endif