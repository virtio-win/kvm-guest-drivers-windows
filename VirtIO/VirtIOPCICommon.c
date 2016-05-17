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
#include "VirtIO_PCI.h"
#include "virtio.h"
#include "virtio_config.h"
#include "kdebugprint.h"
#include <stddef.h>

#include "virtio_pci_common.h"

#define PCI_CAP_LIST_NEXT       1
#define PCI_FIND_CAP_TTL        48

static int __pci_find_next_cap_ttl(virtio_device *vdev, u8 pos, int cap, int *ttl)
{
    u8 id;
    u16 ent;

    pci_read_config_byte(vdev, pos, &pos);

    while ((*ttl)--) {
        if (pos < 0x40)
            break;
        pos &= ~3;
        pci_read_config_word(vdev, pos, &ent);

        id = ent & 0xff;
        if (id == 0xff)
            break;
        if (id == cap)
            return pos;
        pos = (ent >> 8);
    }
    return 0;
}

static int __pci_find_next_cap(virtio_device *vdev, u8 pos, int cap)
{
    int ttl = PCI_FIND_CAP_TTL;

    return __pci_find_next_cap_ttl(vdev, pos, cap, &ttl);
}

static int __pci_bus_find_cap_start(virtio_device *vdev, u8 hdr_type)
{
    u16 status;

    pci_read_config_word(vdev, offsetof(PCI_COMMON_HEADER, Status), &status);
    if (!(status & PCI_STATUS_CAPABILITIES_LIST))
        return 0;

    switch (hdr_type) {
    case PCI_BRIDGE_TYPE:
        return offsetof(PCI_COMMON_HEADER, u.type1.CapabilitiesPtr);
    case PCI_CARDBUS_BRIDGE_TYPE:
        return offsetof(PCI_COMMON_HEADER, u.type2.CapabilitiesPtr);
    default:
        return offsetof(PCI_COMMON_HEADER, u.type0.CapabilitiesPtr);
    }
}

/**
 * pci_find_capability - query for devices' capabilities
 * @dev: PCI device to query
 * @cap: capability code
 *
 * Tell if a device supports a given PCI capability.
 * Returns the address of the requested capability structure within the
 * device's PCI configuration space or 0 in case the device does not
 * support it.  Possible values for @cap:
 *
 *  %PCI_CAP_ID_PM           Power Management
 *  %PCI_CAP_ID_AGP          Accelerated Graphics Port
 *  %PCI_CAP_ID_VPD          Vital Product Data
 *  %PCI_CAP_ID_SLOTID       Slot Identification
 *  %PCI_CAP_ID_MSI          Message Signalled Interrupts
 *  %PCI_CAP_ID_CHSWP        CompactPCI HotSwap
 *  %PCI_CAP_ID_PCIX         PCI-X
 *  %PCI_CAP_ID_EXP          PCI Express
 */
int pci_find_capability(virtio_device *vdev, int cap)
{
    int pos, res;
    u8 hdr_type;

    res = pci_read_config_byte(vdev, offsetof(PCI_COMMON_HEADER, HeaderType), &hdr_type);
    if (res != 0) {
        return 0;
    }
    pos = __pci_bus_find_cap_start(vdev, hdr_type & ~PCI_MULTIFUNCTION);
    if (pos)
        pos = __pci_find_next_cap(vdev, (u8)pos, cap);

    return pos;
}

int pci_find_next_capability(virtio_device *vdev, u8 pos, int cap)
{
    return __pci_find_next_cap(vdev,
        pos + PCI_CAP_LIST_NEXT, cap);
}

/* the config->del_vqs() implementation */
void vp_del_vqs(virtio_device *vdev)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    struct virtqueue *vq;
    unsigned i;

    for (i = 0; i < vdev->maxQueues; i++) {
        vq = vdev->info[i].vq;
        if (vq != NULL) {
            vp_dev->del_vq(&vdev->info[i]);
            vdev->info[i].vq = NULL;
        }
    }
}

/* the config->del_vq() implementation */
void vp_del_vq(struct virtqueue *vq)
{
    virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
    unsigned i = vq->index;

    vp_dev->del_vq(&vp_dev->info[i]);
    vp_dev->info[i].vq = NULL;
}

static struct virtqueue *vp_setup_vq(virtio_device *vdev, unsigned index,
                                     void(*callback)(struct virtqueue *vq),
                                     const char *name,
                                     u16 msix_vec)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    virtio_pci_vq_info *info = &vp_dev->info[index];
    struct virtqueue *vq;

    vq = vp_dev->setup_vq(vp_dev, info, index, callback, name, msix_vec);
    if (!IS_ERR(vq)) {
        info->vq = vq;
    }

    return vq;
}

/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq)
{
    /* we write the queue's selector into the notification register to
     * signal the other end */
    iowrite16((unsigned short)vq->index, (void __iomem *)vq->priv);
    return true;
}

/* the config->find_vqs() implementation */
int vp_find_vqs(virtio_device *vdev, unsigned nvqs,
                struct virtqueue *vqs[],
                vq_callback_t *callbacks[],
                const char * const names[])
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);
    const char *name;
    unsigned i;
    int err;
    u16 msix_vec;

    UNREFERENCED_PARAMETER(callbacks);

    /* set up the config interrupt */
    msix_vec = pci_get_msix_vector(vp_dev, -1);

    if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
        msix_vec = vp_dev->config_vector(vp_dev, msix_vec);
        /* Verify we had enough resources to assign the vector */
        if (msix_vec == VIRTIO_MSI_NO_VECTOR) {
            err = -EBUSY;
            goto error_find;
        }
        vp_dev->msix_used = 1;
    }

    /* set up queue interrupts */
    for (i = 0; i < nvqs; i++) {
        msix_vec = pci_get_msix_vector(vp_dev, i);
        if (names && names[i]) {
            name = names[i];
        } else {
            name = "_unnamed_queue";
        }
        vqs[i] = vp_setup_vq(
            vdev,
            i,
            NULL /*callbacks[i]*/,
            name,
            msix_vec);
        if (IS_ERR(vqs[i])) {
            err = (int)PTR_ERR(vqs[i]);
            goto error_find;
        }
        if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
            vp_dev->msix_used |= 1;
        }
    }
    return 0;

error_find:
    vp_del_vqs(vdev);
    return err;
}

/* the config->find_vq() implementation */
int vp_find_vq(virtio_device *vdev, unsigned index,
               struct virtqueue **vq,
               const char *name)
{
    virtio_pci_device *vp_dev = to_vp_device(vdev);

    struct virtqueue *queue = vp_setup_vq(
        vp_dev,
        index,
        NULL,
        name,
        VIRTIO_MSI_NO_VECTOR);
    if (IS_ERR(queue)) {
        return (int)PTR_ERR(queue);
    }

    *vq = queue;
    return 0;
}

u8 virtio_read_isr_status(VirtIODevice *vdev)
{
    return ioread8(vdev->isr);
}

u8 virtio_get_status(VirtIODevice *vdev)
{
    return vdev->config->get_status(vdev);
}

void virtio_add_status(VirtIODevice *vdev, u8 status)
{
    vdev->config->set_status(vdev, (u8)(vdev->config->get_status(vdev) | status));
}

static void register_virtio_device(virtio_device *dev)
{
    /* We always start by resetting the device, in case a previous
     * driver messed it up.  This also tests that code path a little. */
    dev->config->reset(dev);

    /* Acknowledge that we've seen the device. */
    virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);
}

int virtio_finalize_features(VirtIODevice *dev)
{
    unsigned status;
    int ret = dev->config->finalize_features(dev);

    if (ret)
        return ret;

    if (!virtio_has_feature(dev, VIRTIO_F_VERSION_1))
        return 0;

    virtio_add_status(dev, VIRTIO_CONFIG_S_FEATURES_OK);
    status = dev->config->get_status(dev);
    if (!(status & VIRTIO_CONFIG_S_FEATURES_OK)) {
        DPrintf(0, ("virtio: device refuses features: %x\n", status));
        return -ENODEV;
    }
    return 0;
}

int virtio_device_initialize(VirtIODevice *pVirtIODevice,
                             const VirtIOSystemOps *pSystemOps,
                             PVOID DeviceContext,
                             ULONG allocatedSize)
{
    int err;

    memset(pVirtIODevice, 0, allocatedSize);
    pVirtIODevice->DeviceContext = DeviceContext;
    pVirtIODevice->system = pSystemOps;
    pVirtIODevice->info = pVirtIODevice->inline_info;

    ASSERT(allocatedSize > offsetof(VirtIODevice, info));
    pVirtIODevice->maxQueues =
        (allocatedSize - offsetof(VirtIODevice, info)) / sizeof(tVirtIOPerQueueInfo);

    err = virtio_pci_modern_probe(pVirtIODevice);
    if (err == -ENODEV) {
        /* legacy virtio device */
        err = virtio_pci_legacy_probe(pVirtIODevice);
    }
    if (!err) {
        register_virtio_device(pVirtIODevice);

        /* If we are here, we must have found a driver for the device */
        virtio_add_status(pVirtIODevice, VIRTIO_CONFIG_S_DRIVER);
    }

    return err;
}

void virtio_device_shutdown(VirtIODevice *pVirtIODevice)
{
    if (pVirtIODevice->addr) {
        virtio_pci_legacy_remove(pVirtIODevice);
    }
    else {
        virtio_pci_modern_remove(pVirtIODevice);
    }

    if (pVirtIODevice->info &&
        pVirtIODevice->info != pVirtIODevice->inline_info) {
        kfree(pVirtIODevice, pVirtIODevice->info);
        pVirtIODevice->info = NULL;
    }
}

int virtio_query_queue_allocation(VirtIODevice *vdev,
                                  unsigned index,
                                  unsigned short *pNumEntries,
                                  unsigned long *pAllocationSize,
                                  unsigned long *pHeapSize)
{
    return vdev->query_vq_alloc(vdev, index, pNumEntries, pAllocationSize, pHeapSize);
}

int virtio_reserve_queue_memory(VirtIODevice *vdev, unsigned nvqs)
{
    if (nvqs > vdev->maxQueues) {
        /* allocate new space for queue infos */
        void *new_info = kmalloc(vdev, nvqs * virtio_queue_descriptor_size(), GFP_KERNEL);
        if (!new_info) {
            return -ENOSPC;
        }

        if (vdev->info && vdev->info != vdev->inline_info) {
            kfree(vdev, vdev->info);
        }
        vdev->info = new_info;
        vdev->maxQueues = nvqs;
    }
    return 0;
}

int virtio_find_queues(VirtIODevice *vdev,
                       unsigned nvqs,
                       struct virtqueue *vqs[],
                       const char *const names[])
{
    int err = virtio_reserve_queue_memory(vdev, nvqs);
    if (err == 0)
    {
        err = vdev->config->find_vqs(
            vdev,
            nvqs,
            vqs,
            NULL,
            names);
    }
    return err;
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
        }
        else if ((BAR.LowPart & 0x06) == 0x04) {
            /* memory space 64-bit */
            BAR.LowPart &= 0xFFFFFFF0;
            BAR.HighPart = pPCIHeader->u.type0.BaseAddresses[++i];
        }
        else {
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

NTSTATUS virtio_error_to_ntstatus(int error)
{
    switch (error) {
        case 0:
            return STATUS_SUCCESS;
        case -ENOENT:
            return STATUS_NOT_FOUND;
        case -ENOMEM:
            return STATUS_INSUFFICIENT_RESOURCES;
        case -EBUSY:
            return STATUS_DEVICE_BUSY;
        case -ENODEV:
            return STATUS_DEVICE_NOT_CONNECTED;
        case -EINVAL:
            return STATUS_INVALID_PARAMETER;
        default:
            return STATUS_UNSUCCESSFUL;
    }
}
