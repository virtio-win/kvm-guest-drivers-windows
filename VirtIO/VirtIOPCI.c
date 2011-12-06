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
#include "VirtIO.h"
#include "VirtIO_PCI.h"
#include "kdebugprint.h"
#include "VirtIO_Ring.h"

#ifdef WPP_EVENT_TRACING
#include "VirtIOPCI.tmh"
#endif

/////////////////////////////////////////////////////////////////////////////////////
//
// VirtIODeviceSetIOAddress - Dump HW registers of the device
//
/////////////////////////////////////////////////////////////////////////////////////
void VirtIODeviceSetIOAddress(PVOID pVirtIODevice, ULONG_PTR addr)
{
	DPrintf(4, ("%s\n", __FUNCTION__));

	SetVirtIODeviceAddr(pVirtIODevice, addr);
}

/////////////////////////////////////////////////////////////////////////////////////
//
// VirtIODeviceDumpRegisters - Dump HW registers of the device
//
/////////////////////////////////////////////////////////////////////////////////////
void VirtIODeviceDumpRegisters(PVOID pVirtIODevice)
{
	DPrintf(4, ("%s\n", __FUNCTION__));

	DPrintf(0, ("[VIRTIO_PCI_HOST_FEATURES] = %x\n", ReadVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_HOST_FEATURES)));
	DPrintf(0, ("[VIRTIO_PCI_GUEST_FEATURES] = %x\n", ReadVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_GUEST_FEATURES)));
	DPrintf(0, ("[VIRTIO_PCI_QUEUE_PFN] = %x\n", ReadVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_QUEUE_PFN)));
	DPrintf(0, ("[VIRTIO_PCI_QUEUE_NUM] = %x\n", ReadVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_QUEUE_NUM)));
	DPrintf(0, ("[VIRTIO_PCI_QUEUE_SEL] = %x\n", ReadVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_QUEUE_SEL)));
	DPrintf(0, ("[VIRTIO_PCI_QUEUE_NOTIFY] = %x\n", ReadVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_QUEUE_NOTIFY)));
	DPrintf(0, ("[VIRTIO_PCI_STATUS] = %x\n", ReadVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_STATUS)));
	DPrintf(0, ("[VIRTIO_PCI_ISR] = %x\n", ReadVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_ISR)));
}


/////////////////////////////////////////////////////////////////////////////////////
//
// Get\Set features
//
/////////////////////////////////////////////////////////////////////////////////////
bool VirtIODeviceGetHostFeature(PVOID pVirtIODevice, unsigned uFeature)
{
	DPrintf(4, ("%s\n", __FUNCTION__));

	return !!(ReadVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_HOST_FEATURES) & (1 << uFeature));
}

bool VirtIODeviceEnableGuestFeature(PVOID pVirtIODevice, unsigned uFeature)
{
	ULONG ulValue = 0;
	DPrintf(4, ("%s\n", __FUNCTION__));

	ulValue = ReadVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_GUEST_FEATURES);
	ulValue	|= (1 << uFeature);
	WriteVirtIODeviceRegister(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_GUEST_FEATURES, ulValue);

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
void VirtIODeviceReset(PVOID pVirtIODevice)
{
	/* 0 status means a reset. */
	WriteVirtIODeviceByte(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_STATUS, 0);
}

/////////////////////////////////////////////////////////////////////////////////////
//
// Get\Set status
//
/////////////////////////////////////////////////////////////////////////////////////
static u8 VirtIODeviceGetStatus(PVOID pVirtIODevice)
{
	DPrintf(6, ("%s\n", __FUNCTION__));

	return ReadVirtIODeviceByte(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_STATUS);
}

static void VirtIODeviceSetStatus(PVOID pVirtIODevice, u8 status)
{
	DPrintf(6, ("%s>>> %x\n", __FUNCTION__, status));
	WriteVirtIODeviceByte(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_STATUS, status);
}

void VirtIODeviceAddStatus(PVOID pVirtIODevice, u8 status)
{
	DPrintf(4, ("%s>>> %x\n", __FUNCTION__, status));

	VirtIODeviceSetStatus(pVirtIODevice, VirtIODeviceGetStatus(pVirtIODevice) | status);
}

void VirtIODeviceRemoveStatus(PVOID pVirtIODevice, u8 status)
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
void VirtIODeviceGet(PVOID pVirtIODevice,
							unsigned offset,
							void *buf,
							unsigned len)
{
	ULONG_PTR ioaddr = GetVirtIODeviceAddr(pVirtIODevice) + GetPciConfig(pVirtIODevice)/*VIRTIO_PCI_CONFIG*/ + offset;
	u8 *ptr = buf;
	unsigned i;

	DPrintf(4, ("%s\n", __FUNCTION__));

	for (i = 0; i < len; i++)
		ptr[i] = ReadVirtIODeviceByte(ioaddr + i);
}

void VirtIODeviceSet(PVOID pVirtIODevice,
							   unsigned offset,
							   const void *buf,
							   unsigned len)
{
	ULONG_PTR ioaddr = GetVirtIODeviceAddr(pVirtIODevice) + GetPciConfig(pVirtIODevice)/*VIRTIO_PCI_CONFIG*/ + offset;
	const u8 *ptr = buf;
	unsigned i;

	DPrintf(4, ("%s\n", __FUNCTION__));

	for (i = 0; i < len; i++)
		WriteVirtIODeviceByte(ioaddr + i, ptr[i]);
}

// the notify function used when creating a virt queue /
void vp_notify(struct virtqueue *vq)
{
	struct virtio_pci_vq_info *info = vq->priv;

	DPrintf(4, ("%s>> queue %d\n", __FUNCTION__, info->queue_index));

	// we write the queue's selector into the notification register to
	// * signal the other end
	WriteVirtIODeviceWord(GetVirtIODeviceAddr(vq->vdev) + VIRTIO_PCI_QUEUE_NOTIFY, (u16) info->queue_index);
}

//* A small wrapper to also acknowledge the interrupt when it's handled.
//* I really need an EIO hook for the vring so I can ack the interrupt once we
//* know that we'll be handling the IRQ but before we invoke the callback since
//* the callback may notify the host which results in the host attempting to
//* raise an interrupt that we would then mask once we acknowledged the
//* interrupt.
/* changed: status is a bitmap rather than boolean value */
ULONG VirtIODeviceISR(PVOID pVirtIODevice)
{
	ULONG status;
	DPrintf(4, ("%s\n", __FUNCTION__));

	status = ReadVirtIODeviceByte(GetVirtIODeviceAddr(pVirtIODevice) + VIRTIO_PCI_ISR);

	return status;
}

//the config->find_vq() implementation
//VirtIODeviceFindVirtualQueue
struct virtqueue *VirtIODeviceFindVirtualQueue(PVOID vp_dev,
											   unsigned index,
											   unsigned vector,
											   bool (*callback)(struct virtqueue *vq),
											   PVOID Context, PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
											   VOID (*freemem)(PVOID Context, PVOID Address, pmeminfo pmi ),
											   BOOLEAN Cached, BOOLEAN bPhysical, BOOLEAN bLocal)
{
	struct virtio_pci_vq_info *info;
	struct virtqueue *vq;
	u16 num;
	int err;

	if( bLocal )
		return VirtIODeviceFindVirtualQueue_InDrv(vp_dev, index, vector, callback, Context, allocmem, freemem, Cached, bPhysical );

	DPrintf(4, ("%s\n", __FUNCTION__));

	// Select the queue we're interested in
	WriteVirtIODeviceWord(GetVirtIODeviceAddr(vp_dev) + VIRTIO_PCI_QUEUE_SEL, (u16) index);

	// Check if queue is either not available or already active.
	num = ReadVirtIODeviceWord(GetVirtIODeviceAddr(vp_dev) + VIRTIO_PCI_QUEUE_NUM);

	DPrintf(0, ("%s>>> [vp_dev->addr + VIRTIO_PCI_QUEUE_NUM] = %x\n", __FUNCTION__, num) );
	if (!num || ReadVirtIODeviceWord(GetVirtIODeviceAddr(vp_dev) + VIRTIO_PCI_QUEUE_PFN))
		return NULL;

	// allocate and fill out our structure the represents an active queue
	info = alloc_needed_mem(Context, allocmem, sizeof(struct virtio_pci_vq_info), NULL);

	if (!info)
		return NULL;

	info->queue_index = index;
	info->num = num;

	info->mi.Addr = NULL;
	info->mi.Cached = Cached;
	info->mi.size = vring_size(num,PAGE_SIZE);
    info->pFree = NULL;

	info->queue = alloc_needed_mem(Context, allocmem, bPhysical ? 0 : info->mi.size/*vring_size(num,PAGE_SIZE)*/,
		                                              bPhysical ? &info->mi : NULL );

	if (info->queue == NULL) {
		err = -1;
		goto out_info;
	}

	memset(info->queue, 0, info->mi.size/*vring_size(num,PAGE_SIZE)*/);

	DPrintf(0, ("[%s] info = %p, info->queue = %p\n", __FUNCTION__, info, info->queue) );
	// create the vring
	vq = vring_new_virtqueue(info->num,
							 vp_dev,
							 info->queue,
							 vp_notify,
							 callback,
							 Context, allocmem);

	if (!vq) {
		err = -1;
		goto out_activate_queue;
	}

	vq->priv = info;
	info->vq = vq;

	// activate the queue
	{
		PHYSICAL_ADDRESS pa;
		ULONG pageNum;

		if( info->mi.Addr )
			pa = info->mi.physAddr;
		else
			pa = MmGetPhysicalAddress(info->queue);

		pageNum = (ULONG)(pa.QuadPart >> PAGE_SHIFT);

		DPrintf(0, ("[%s] queue phys.address %08lx:%08lx, pfn %lx\n", __FUNCTION__, pa.u.HighPart, pa.u.LowPart, pageNum));
		WriteVirtIODeviceRegister(GetVirtIODeviceAddr(vp_dev) + VIRTIO_PCI_QUEUE_PFN, pageNum);
	}

	return vq;

out_activate_queue:
	WriteVirtIODeviceRegister(GetVirtIODeviceAddr(vp_dev) + VIRTIO_PCI_QUEUE_PFN, 0);
	if(info->queue) {
		free_needed_mem(Context, freemem, !info->mi.Addr ? info->queue: NULL,
											!info->mi.Addr ? NULL :&info->mi);
	}
out_info:
	if(info) {
		free_needed_mem(Context, freemem, info, NULL);
		}

	return NULL;
}

/* the config->del_vq() implementation  */
void VirtIODeviceDeleteVirtualQueue(struct virtqueue *vq, PVOID Context,
									VOID (*freemem)(PVOID Context, PVOID Address, pmeminfo pmi ), BOOLEAN bLocal)
{
	struct virtio_pci_vq_info *info = (struct virtio_pci_vq_info *)vq->priv;

	DPrintf(4, ("%s\n", __FUNCTION__));

	if( !bLocal )
	    vring_del_virtqueue(vq, Context, freemem);

	// Select and deactivate the queue
	WriteVirtIODeviceWord(GetVirtIODeviceAddr(vq->vdev) + VIRTIO_PCI_QUEUE_SEL, (u16) info->queue_index);
	WriteVirtIODeviceRegister(GetVirtIODeviceAddr(vq->vdev) + VIRTIO_PCI_QUEUE_PFN, 0);

	if( bLocal )
		return;

	if(info->queue) {
		free_needed_mem(Context, freemem, !info->mi.Addr ? info->queue: NULL,
											!info->mi.Addr ? NULL :&info->mi);
	}

	if(info) {
		free_needed_mem(Context, freemem, info, NULL);
	}
}

/* implementation of queue renew on resume from standby/hibernation */
void VirtIODeviceRenewVirtualQueue(struct virtqueue *vq)
{
	PHYSICAL_ADDRESS pa;
	ULONG pageNum;
	struct virtio_pci_vq_info *info = vq->priv;

	if( info->mi.Addr )
	    pa = info->mi.physAddr;
	else
	    pa = GetPhysicalAddress(info->queue);

	pageNum = (ULONG)(pa.QuadPart >> PAGE_SHIFT);
	DPrintf(0, ("[%s] devaddr %p, queue %d, pfn %x\n", __FUNCTION__, GetVirtIODeviceAddr(vq->vdev), info->queue_index, pageNum));
	WriteVirtIODeviceWord(GetVirtIODeviceAddr(vq->vdev) + VIRTIO_PCI_QUEUE_SEL, (u16)info->queue_index);
	WriteVirtIODeviceRegister(GetVirtIODeviceAddr(vq->vdev) + VIRTIO_PCI_QUEUE_PFN, pageNum);
}

u32 VirtIODeviceGetQueueSize(struct virtqueue *vq)
{
	struct virtio_pci_vq_info *info = vq->priv;
	return info->num;
}

void* VirtIODeviceDetachUnusedBuf(struct virtqueue *vq)
{
    return vring_detach_unused_buf(vq);
}

PVOID VirtIODeviceAllocVirtualQueueAddMem( struct virtqueue *vq,
										   PVOID Context, PVOID (*allocmem)(PVOID Context, ULONG size, pmeminfo pmi),
										   int size, BOOLEAN Cached, BOOLEAN bPhysical)
{
	bool ret = FALSE;
	struct virtio_pci_vq_info *info = (struct virtio_pci_vq_info *)vq->priv;
	void *pAddAllocWork = NULL;
	unsigned nunits = (size+sizeof(Header)-1)/sizeof(Header) + 1; //size in header units
	unsigned allocsize = nunits*sizeof(Header);

	if( bPhysical )
	{
		info->Add_alloc = NULL;
		info->mi_add.size = allocsize;
		info->mi_add.Cached = Cached;
	}

	pAddAllocWork = alloc_needed_mem( Context, allocmem, !bPhysical ? allocsize : 0,
		                                                 !bPhysical ? NULL : &info->mi_add );

	if( !bPhysical )
		info->Add_alloc = pAddAllocWork;

	info->base.pHead = info->pFree = &info->base;
	info->base.size = 0;

	info->pFree->pHead = pAddAllocWork;
	info->pFree->pHead->size  = nunits;
	info->pFree->pHead->pHead = &info->base;

	return pAddAllocWork;
}

void VirtIODeviceDeleteVirtualQueueAddMem(struct virtqueue *vq, PVOID Context, VOID (*freemem)(PVOID Context, PVOID Address, pmeminfo pmi ))
{
	struct virtio_pci_vq_info *info = (struct virtio_pci_vq_info *)vq->priv;

	free_needed_mem(Context, freemem,  info->Add_alloc ?  info->Add_alloc : NULL,
									   info->Add_alloc ? NULL : &info->mi_add);
	info->pFree = NULL;
}


void *VirtIODevicemallocVirtualQueueAddMem(struct virtqueue *vq, unsigned nbytes)
{
    Header *p, *prevp;
    unsigned nunits;

	struct virtio_pci_vq_info *info = (struct virtio_pci_vq_info *)vq->priv;

	if( !info->pFree )
		return NULL;

    nunits = (nbytes+sizeof(Header)-1)/sizeof(Header) + 1;

	prevp = info->pFree ;

    for (p = prevp->pHead; ; prevp = p, p = p->pHead)
    {
        if (p->size >= (int)nunits)
        {
            if (p->size == (int)nunits)
                prevp->pHead = p->pHead;
             else
             {
                 p->size -= nunits;
                 p += p->size;
                 p->size = nunits;
             }
             info->pFree = prevp;
             return (void *)(p+1);
         }
         if (p == info->pFree)
             return NULL;
    }
	return NULL ;
}


void VirtIODevicefreeVirtualQueueAddMem(struct virtqueue *vq, void *ap)
{
    Header *bp, *p;

	struct virtio_pci_vq_info *info = (struct virtio_pci_vq_info *)vq->priv;

	if( !info->pFree )
		return;

    bp = (Header *)ap - 1;

    for (p = info->pFree; !(bp > p && bp < p->pHead); p = p->pHead)
        if (p >= p->pHead && (bp > p || bp < p->pHead))
            break;

    if (bp + bp->size == p->pHead)
	{
        bp->size += p->pHead->size;
        bp->pHead = p->pHead->pHead;
    } else
        bp->pHead = p->pHead;

    if (p + p->size == bp)
	{
        p->size += bp->size;
        p->pHead = bp->pHead;
    } else
        p->pHead = bp;

    info->pFree = p;
}
