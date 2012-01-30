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
#include "VirtIO_Win.h"
#include "kdebugprint.h"
#include "VirtIO_Ring.h"
#include "PVUtils.h"

#ifdef WPP_EVENT_TRACING
#include "VirtIOPCI.tmh"
#endif


struct virtio_pci_vq_info
{
	/* the actual virtqueue */
	struct virtqueue *vq;

	/* the number of entries in the queue */
	int num;

	/* the index of the queue */
	int queue_index;

	/* the virtual address of the ring queue */
	void *queue;
};

/////////////////////////////////////////////////////////////////////////////////////
//
// VirtIODeviceSetIOAddress - Dump HW registers of the device
//
/////////////////////////////////////////////////////////////////////////////////////
void VirtIODeviceSetIOAddress(VirtIODevice * pVirtIODevice, ULONG_PTR addr)
{
	DPrintf(4, ("%s\n", __FUNCTION__));

	pVirtIODevice->addr = addr;
}

/////////////////////////////////////////////////////////////////////////////////////
//
// VirtIODeviceDumpRegisters - Dump HW registers of the device
//
/////////////////////////////////////////////////////////////////////////////////////
void VirtIODeviceDumpRegisters(VirtIODevice * pVirtIODevice)
{
	DPrintf(4, ("%s\n", __FUNCTION__));

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
// Get\Set features
//
/////////////////////////////////////////////////////////////////////////////////////
bool VirtIODeviceGetHostFeature(VirtIODevice * pVirtIODevice, unsigned uFeature)
{
	DPrintf(4, ("%s\n", __FUNCTION__));

	return !!(ReadVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_HOST_FEATURES) & (1 << uFeature));
}

bool VirtIODeviceEnableGuestFeature(VirtIODevice * pVirtIODevice, unsigned uFeature)
{
	ULONG ulValue = 0;
	DPrintf(4, ("%s\n", __FUNCTION__));

	ulValue = ReadVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_GUEST_FEATURES);
	ulValue	|= (1 << uFeature);
	WriteVirtIODeviceRegister(pVirtIODevice->addr + VIRTIO_PCI_GUEST_FEATURES, ulValue);

	return !!(ulValue & (1 << uFeature));
}

bool VirtIODeviceHasFeature(unsigned uFeature)
{
	if (uFeature == VIRTIO_F_PUBLISH_INDICES) return TRUE;
	return FALSE;
}

/////////////////////////////////////////////////////////////////////////////////////
//
// Reset device
//
/////////////////////////////////////////////////////////////////////////////////////
void VirtIODeviceReset(VirtIODevice * pVirtIODevice)
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

void VirtIODeviceAddStatus(VirtIODevice * pVirtIODevice, u8 status)
{
	DPrintf(4, ("%s>>> %x\n", __FUNCTION__, status));

	VirtIODeviceSetStatus(pVirtIODevice, VirtIODeviceGetStatus(pVirtIODevice) | status);
}

void VirtIODeviceRemoveStatus(VirtIODevice * pVirtIODevice, u8 status)
{
	DPrintf(4, ("%s>>> %x\n", __FUNCTION__, status));

	VirtIODeviceSetStatus(pVirtIODevice ,
						  VirtIODeviceGetStatus(pVirtIODevice) & (~status));
}

/////////////////////////////////////////////////////////////////////////////////////
//
// Get\Set device data
//
/////////////////////////////////////////////////////////////////////////////////////
void VirtIODeviceGet(VirtIODevice * pVirtIODevice,
							unsigned offset,
							void *buf,
							unsigned len)
{
	ULONG_PTR ioaddr = pVirtIODevice->addr + VIRTIO_PCI_CONFIG + offset;
	u8 *ptr = buf;
	unsigned i;

	DPrintf(4, ("%s\n", __FUNCTION__));

	for (i = 0; i < len; i++)
		ptr[i] = ReadVirtIODeviceByte(ioaddr + i);
}

void VirtIODeviceSet(VirtIODevice * pVirtIODevice,
							   unsigned offset,
							   const void *buf,
							   unsigned len)
{
	ULONG_PTR ioaddr = pVirtIODevice->addr + VIRTIO_PCI_CONFIG + offset;
	const u8 *ptr = buf;
	unsigned i;

	DPrintf(4, ("%s\n", __FUNCTION__));

	for (i = 0; i < len; i++)
		WriteVirtIODeviceByte(ioaddr + i, ptr[i]);
}

// the notify function used when creating a virt queue /
static void vp_notify(struct virtqueue *vq)
{
	VirtIODevice *vp_dev = vq->vdev;
	struct virtio_pci_vq_info *info = vq->priv;

	DPrintf(4, ("%s>> queue %d\n", __FUNCTION__, info->queue_index));

	// we write the queue's selector into the notification register to
	// * signal the other end
	WriteVirtIODeviceWord(vp_dev->addr + VIRTIO_PCI_QUEUE_NOTIFY, (u16) info->queue_index);
}


//* A small wrapper to also acknowledge the interrupt when it's handled.
// * I really need an EIO hook for the vring so I can ack the interrupt once we
//* know that we'll be handling the IRQ but before we invoke the callback since
//* the callback may notify the host which results in the host attempting to
// * raise an interrupt that we would then mask once we acknowledged the
// * interrupt.
/* changed: status is a bitmap rather than boolean value */
ULONG VirtIODeviceISR(VirtIODevice * pVirtIODevice)
{
	ULONG status;
	DPrintf(6, ("%s\n", __FUNCTION__));

	status = ReadVirtIODeviceByte(pVirtIODevice->addr + VIRTIO_PCI_ISR);

	return status;
}

//the config->find_vq() implementation
//VirtIODeviceFindVirtualQueue
struct virtqueue *VirtIODeviceFindVirtualQueue(VirtIODevice *vp_dev,
											   unsigned index,
											   bool (*callback)(struct virtqueue *vq))
{
	struct virtio_pci_vq_info *info;
	struct virtqueue *vq;
	u16 num;
	int err;

	DPrintf(4, ("%s\n", __FUNCTION__));

	// Select the queue we're interested in
	WriteVirtIODeviceWord(vp_dev->addr + VIRTIO_PCI_QUEUE_SEL, (u16) index);

	// Check if queue is either not available or already active.
	num = ReadVirtIODeviceWord(vp_dev->addr + VIRTIO_PCI_QUEUE_NUM);

	DPrintf(0, ("%s>>> [vp_dev->addr + VIRTIO_PCI_QUEUE_NUM] = %x\n", __FUNCTION__, num) );
	if (!num || ReadVirtIODeviceRegister(vp_dev->addr + VIRTIO_PCI_QUEUE_PFN))
		return NULL;

	// allocate and fill out our structure the represents an active queue
	info = AllocatePhysical(sizeof(struct virtio_pci_vq_info));
	if (!info)
		return NULL;

	info->queue_index = index;
	info->num = num;

	info->queue = AllocatePhysical(vring_size(num,PAGE_SIZE));
	if (info->queue == NULL) {
		err = -1;
		goto out_info;
	}

	memset(info->queue, 0, vring_size(num,PAGE_SIZE));

	DPrintf(0, ("[%s] info = %p, info->queue = %p\n", __FUNCTION__, info, info->queue) );
	// create the vring
	vq = vring_new_virtqueue(info->num,
							 vp_dev,
							 info->queue,
							 vp_notify,
							 callback);

	if (!vq) {
		err = -1;
		goto out_activate_queue;
	}

	vq->priv = info;
	info->vq = vq;

	// activate the queue
	{
		PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(info->queue);
		ULONG pageNum = (ULONG)(pa.QuadPart >> PAGE_SHIFT);
		DPrintf(0, ("[%s] queue phys.address %08lx:%08lx, pfn %lx\n", __FUNCTION__, pa.u.HighPart, pa.u.LowPart, pageNum));
		WriteVirtIODeviceRegister(vp_dev->addr + VIRTIO_PCI_QUEUE_PFN, pageNum);
	}

	return vq;

out_activate_queue:
	WriteVirtIODeviceRegister(vp_dev->addr + VIRTIO_PCI_QUEUE_PFN, 0);
	if(info->queue) {
		MmFreeContiguousMemory(info->queue);
	}
out_info:
	if(info) {
		MmFreeContiguousMemory(info);
	}

	return NULL;
}


/* the config->del_vq() implementation  */
void VirtIODeviceDeleteVirtualQueue(struct virtqueue *vq)
{
	VirtIODevice *vp_dev = vq->vdev;
	struct virtio_pci_vq_info *info = (struct virtio_pci_vq_info *)vq->priv;

	DPrintf(4, ("%s\n", __FUNCTION__));

	vring_del_virtqueue(vq);

	// Select and deactivate the queue
	WriteVirtIODeviceWord(vp_dev->addr + VIRTIO_PCI_QUEUE_SEL, (u16) info->queue_index);
	WriteVirtIODeviceRegister(vp_dev->addr + VIRTIO_PCI_QUEUE_PFN, 0);

	if(info->queue) {
		MmFreeContiguousMemory(info->queue);
	}

	if(info) {
		MmFreeContiguousMemory(info);
	}
}

/* implementation of queue renew on resume from standby/hibernation */
void VirtIODeviceRenewVirtualQueue(struct virtqueue *vq)
{
	PHYSICAL_ADDRESS pa;
	ULONG pageNum;
	struct virtio_pci_vq_info *info = vq->priv;
	VirtIODevice *vp_dev = vq->vdev;
	pa = MmGetPhysicalAddress(info->queue);
	pageNum = (ULONG)(pa.QuadPart >> PAGE_SHIFT);
	DPrintf(0, ("[%s] devaddr %p, queue %d, pfn %x\n", __FUNCTION__, vp_dev->addr, info->queue_index, pageNum));
	WriteVirtIODeviceWord(vp_dev->addr + VIRTIO_PCI_QUEUE_SEL, (u16)info->queue_index);
	WriteVirtIODeviceRegister(vp_dev->addr + VIRTIO_PCI_QUEUE_PFN, pageNum);
}

u32 VirtIODeviceGetQueueSize(struct virtqueue *vq)
{
	struct virtio_pci_vq_info *info = vq->priv;
	return info->num;
}
