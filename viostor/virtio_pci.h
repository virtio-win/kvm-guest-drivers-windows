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

#ifndef _LINUX_VIRTIO_PCI_H
#define _LINUX_VIRTIO_PCI_H

/* A 32-bit r/o bitmask of the features supported by the host */
#define VIRTIO_PCI_HOST_FEATURES	0

/* A 32-bit r/w bitmask of features activated by the guest */
#define VIRTIO_PCI_GUEST_FEATURES	4

/* A 32-bit r/w PFN for the currently selected queue */
#define VIRTIO_PCI_QUEUE_PFN		8

/* A 16-bit r/o queue size for the currently selected queue */
#define VIRTIO_PCI_QUEUE_NUM		12

/* A 16-bit r/w queue selector */
#define VIRTIO_PCI_QUEUE_SEL		14

/* A 16-bit r/w queue notifier */
#define VIRTIO_PCI_QUEUE_NOTIFY		16

/* An 8-bit device status register.  */
#define VIRTIO_PCI_STATUS		18

/* An 8-bit r/o interrupt status register.  Reading the value will return the
 * current contents of the ISR and will also clear it.  This is effectively
 * a read-and-acknowledge. */
#define VIRTIO_PCI_ISR			19

/* The remaining space is defined by each driver as the per-driver
 * configuration space */
#define VIRTIO_PCI_CONFIG(msix_enabled)		(msix_enabled ? 24 : 20)

/* MSI-X registers: only enabled if MSI-X is enabled. */
/* A 16-bit vector for configuration changes. */
#define VIRTIO_MSI_CONFIG_VECTOR        20
/* A 16-bit vector for selected queue notifications. */
#define VIRTIO_MSI_QUEUE_VECTOR         22
/* Vector value used to disable MSI for queue */
#define VIRTIO_MSI_NO_VECTOR            0xffff

typedef struct virtio_pci_vq_info
{
    /* the actual virtqueue */
    struct virtqueue *vq;

    /* the number of entries in the queue */
    int num;

    /* the index of the queue */
    int queue_index;

    /* the virtual address of the ring queue */
    void *queue;
}virtio_pci_vq_info;


VOID
VirtIODeviceReset(
    IN PVOID DeviceExtension);

bool
VirtIODeviceGetHostFeature(
    IN PVOID DeviceExtension,
    IN unsigned uFeature);

void
VirtIODeviceGet(
    IN PVOID DeviceExtension,
    unsigned offset,
    PVOID buf,
    unsigned len);

UCHAR
VirtIODeviceISR(
    IN PVOID DeviceExtension);

struct
virtqueue*
VirtIODeviceFindVirtualQueue(
    IN PVOID DeviceExtension,
    IN unsigned index,
    IN unsigned vector);

void
VirtIODeviceDeleteVirtualQueue(
    IN struct virtqueue *vq);

#endif //_LINUX_VIRTIO_PCI_H
