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
#define VIRTIO_PCI_CONFIG		20

/* MSI-X registers: only enabled if MSI-X is enabled. */
/* A 16-bit vector for configuration changes. */
#define VIRTIO_MSI_CONFIG_VECTOR        20
/* A 16-bit vector for selected queue notifications. */
#define VIRTIO_MSI_QUEUE_VECTOR         22
/* Vector value used to disable MSI for queue */
#define VIRTIO_MSI_NO_VECTOR            0xffff

#define MAX_QUEUES_PER_DEVICE	16

typedef struct _tVirtIOPerQueueInfo
{
	/* the actual virtqueue */
	struct virtqueue *vq;
	/* the number of entries in the queue */
	int num;
	/* the index of the queue */
	int queue_index;
	/* the virtual address of the ring queue */
	void *queue;
	/* physical address of the ring queue */
	PHYSICAL_ADDRESS phys;
	/* owner per-queue context */
	void *pOwnerContext;
}tVirtIOPerQueueInfo;

typedef struct TypeVirtIODevice
{
	ULONG_PTR addr;
	tVirtIOPerQueueInfo info[MAX_QUEUES_PER_DEVICE];
} VirtIODevice;

void VirtIODeviceInitialize(VirtIODevice * pVirtIODevice, ULONG_PTR addr);
void VirtIODeviceReset(VirtIODevice * pVirtIODevice);
void VirtIODeviceDumpRegisters(VirtIODevice * pVirtIODevice);
bool VirtIODeviceGetHostFeature(VirtIODevice * pVirtIODevice, unsigned uFeature);
bool VirtIODeviceEnableGuestFeature(VirtIODevice * pVirtIODevice, unsigned uFeature);
bool VirtIODeviceHasFeature(unsigned uFeature);
void VirtIODeviceGet(VirtIODevice * pVirtIODevice,
					 unsigned offset,
					 void *buf,
					 unsigned len);
void VirtIODeviceSet(VirtIODevice * pVirtIODevice,
					 unsigned offset,
					 const void *buf,
					 unsigned len);
ULONG VirtIODeviceISR(VirtIODevice * pVirtIODevice);
void VirtIODeviceAddStatus(VirtIODevice * pVirtIODevice, u8 status);
void VirtIODeviceRemoveStatus(VirtIODevice * pVirtIODevice, u8 status);

void VirtIODeviceQueryQueueAllocation(VirtIODevice *vp_dev, unsigned index, unsigned long *pNumEntries, unsigned long *pAllocationSize);
struct virtqueue *VirtIODevicePrepareQueue(
					VirtIODevice *vp_dev, 
					unsigned index, 
					PHYSICAL_ADDRESS pa, 
					void *va, 
					unsigned long size,
					void *ownerContext);
void VirtIODeviceDeleteQueue(struct virtqueue *vq, /* optional*/ void **pOwnerContext);
u32  VirtIODeviceGetQueueSize(struct virtqueue *vq);
void VirtIODeviceRenewQueue(struct virtqueue *vq);
void* VirtIODeviceDetachUnusedBuf(struct virtqueue *vq);


/////////////////////////////////////////////////////////////////////////////////////
//
// IO space read\write functions
//
// ReadVirtIODeviceRegister
// WriteVirtIODeviceRegister
// ReadVirtIODeviceByte
// WriteVirtIODeviceByte
//
// Must be implemented in device specific module
//
/////////////////////////////////////////////////////////////////////////////////////
extern u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister);
extern void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue);
extern u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister);
extern void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue);
extern u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister);
extern void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 bValue);

#endif
