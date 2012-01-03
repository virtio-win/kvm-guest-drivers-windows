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


/* The remaining space is defined by each driver as the per-driver
 * configuration space */
#define VIRTIO_PCI_CONFIG_STOR(msix_enabled)		(msix_enabled ? 24 : 20)

/* MSI-X registers: only enabled if MSI-X is enabled. */
/* A 16-bit vector for configuration changes. */
#define VIRTIO_MSI_CONFIG_VECTOR        20
/* A 16-bit vector for selected queue notifications. */
#define VIRTIO_MSI_QUEUE_VECTOR         22
/* Vector value used to disable MSI for queue */
#define VIRTIO_MSI_NO_VECTOR            0xffff

/* Moved to VirtIO.h to remove dependency of virtioring.c from virtio_pci.h
 * That mean that VirtIO.h have to stand before other virto H files and not after as before

typedef struct _meminfo
{
	PVOID            Addr;
	PHYSICAL_ADDRESS physAddr;
	ULONG            size;
	BOOLEAN          Cached;

    ULONG            alignment;
	PVOID            Reserved;

}meminfo, *pmeminfo;

typedef struct TypeVirtIODevice
{
	ULONG_PTR addr;
} VirtIODevice;
*/
typedef struct _Header
{
	struct _Header *pHead;
	unsigned size;
#ifdef _WIN64
	unsigned Dummy ; // That make header to be 2 LONG64 and not 1.5 without it
#endif
}Header, *pHeader;


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

	/* memory info for queue */
	meminfo mi;

	/*Additional memory info*/
	Header base;
	Header *pFree;
	void *Add_alloc;
	meminfo mi_add;
};

void VirtIODeviceReset(PVOID pVirtIODevice);

void VirtIODeviceSetIOAddress(PVOID pVirtIODevice, ULONG_PTR addr);
void VirtIODeviceDumpRegisters(PVOID pVirtIODevice);

bool VirtIODeviceGetHostFeature(PVOID pVirtIODevice, unsigned uFeature);
bool VirtIODeviceEnableGuestFeature(PVOID pVirtIODevice, unsigned uFeature);
bool VirtIODeviceHasFeature(unsigned uFeature);
void VirtIODeviceGet(PVOID pVirtIODevice,
					 unsigned offset,
					 void *buf,
					 unsigned len);
void VirtIODeviceSet(PVOID pVirtIODevice,
					 unsigned offset,
					 const void *buf,
					 unsigned len);

ULONG VirtIODeviceISR(PVOID pVirtIODevice);
struct virtqueue *VirtIODeviceFindVirtualQueue(PVOID vp_dev,
											   unsigned index,
											   unsigned vector,
											   bool (*callback)(struct virtqueue *vq),
											   PVOID Context,
											   PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
											   VOID (*freemem)(PVOID Context, PVOID Address, pmeminfo pmi ),
											   BOOLEAN Cached, BOOLEAN bPhysical, BOOLEAN bLocal);
void VirtIODeviceDeleteVirtualQueue(struct virtqueue *vq,
									PVOID Context,
									VOID (*freemem)(PVOID Context, PVOID Address, pmeminfo pmi ), BOOLEAN bLocal);
u32  VirtIODeviceGetQueueSize(struct virtqueue *vq);
void VirtIODeviceRenewVirtualQueue(struct virtqueue *vq);
void* VirtIODeviceDetachUnusedBuf(struct virtqueue *vq);

void VirtIODeviceAddStatus(PVOID pVirtIODevice, u8 status);
void VirtIODeviceRemoveStatus(PVOID pVirtIODevice, u8 status);

PVOID VirtIODeviceAllocVirtualQueueAddMem( struct virtqueue *vq,
										   PVOID Context, PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
										   int size, BOOLEAN Cached, BOOLEAN bPhysical);
void VirtIODeviceDeleteVirtualQueueAddMem(struct virtqueue *vq, PVOID Context,
										  VOID (*freemem)(PVOID Context, PVOID Address, pmeminfo pmi ));
void *VirtIODevicemallocVirtualQueueAddMem(struct virtqueue *vq, unsigned nbytes);
void VirtIODevicefreeVirtualQueueAddMem(struct virtqueue *vq, void *ap);

PVOID alloc_needed_mem(PVOID Context,
	                   PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
					   ULONG size, pmeminfo pmi);

void free_needed_mem(PVOID Context, VOID (*freemem)(PVOID Context, PVOID Address, pmeminfo pmi), PVOID Address, pmeminfo pmi);

void vp_notify(struct virtqueue *vq);

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

/////////////////////////////////////////////////////////////////////////////////////
//
// GetVirtIODeviceAddr supply device address and must be implemented in device specific module
//
/////////////////////////////////////////////////////////////////////////////////////
extern ULONG_PTR GetVirtIODeviceAddr(PVOID pVirtIODevice);

/////////////////////////////////////////////////////////////////////////////////////
//
// SetVirtIODeviceAddr set device address and must be implemented in device specific module
//
/////////////////////////////////////////////////////////////////////////////////////
extern void SetVirtIODeviceAddr(PVOID pVirtIODevice, ULONG_PTR addr);

/////////////////////////////////////////////////////////////////////////////////////
//
// VirtIODeviceFindVirtualQueue_InDrv is driver specific VirtIODeviceFindVirtualQueue
//
/////////////////////////////////////////////////////////////////////////////////////
extern struct virtqueue *VirtIODeviceFindVirtualQueue_InDrv(PVOID vp_dev,
															unsigned index,
															unsigned vector,
															bool (*callback)(struct virtqueue *vq),
															PVOID Context,
															PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
															VOID (*freemem)(PVOID Context, PVOID Address, pmeminfo pmi ),
															BOOLEAN Cached, BOOLEAN bPhysical);

/////////////////////////////////////////////////////////////////////////////////////
//
// GetPciConfig is driver specific. It return VIRTIO_PCI_CONFIG specific
//
/////////////////////////////////////////////////////////////////////////////////////
extern int GetPciConfig(PVOID pVirtIODevice);

/////////////////////////////////////////////////////////////////////////////////////
//
// drv_alloc_needed_mem is driver specific.
//
/////////////////////////////////////////////////////////////////////////////////////
extern PVOID drv_alloc_needed_mem(PVOID vdev, PVOID Context,
						   PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
						   ULONG size, pmeminfo pmi);

/////////////////////////////////////////////////////////////////////////////////////
//
// GetPhysicalAddress is driver specific.
//
/////////////////////////////////////////////////////////////////////////////////////
extern  PHYSICAL_ADDRESS GetPhysicalAddress(PVOID addr);

#endif
