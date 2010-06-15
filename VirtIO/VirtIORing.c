/* Virtio ring implementation.
 *
 *  Copyright 2007 Rusty Russell IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "osdep.h"
#include "VirtIO_PCI.h"
#include "VirtIO.h"
#include "kdebugprint.h"
#include "virtio_ring.h"
#include "PVUtils.h"

#ifdef WPP_EVENT_TRACING
#include "VirtIORing.tmh"
#endif

struct _declspec(align(PAGE_SIZE)) vring_virtqueue
{
	struct virtqueue vq;

	/* Actual memory layout for this queue */
	struct vring vring;

	/* Other side has made a mess, don't try any more. */
	bool broken;

	/* Number of free buffers */
	unsigned int num_free;
	/* Head of free buffer list. */
	unsigned int free_head;
	/* Number we've added since last sync. */
	unsigned int num_added;

	/* How to notify other side. FIXME: commonalize hcalls! */
	void (*notify)(struct virtqueue *vq);

	/* Tokens for callbacks. */
	void *data[];
};

static void initialize_virtqueue(struct vring_virtqueue *vq,
							unsigned int num,
							VirtIODevice * pVirtIODevice,
							void *pages,
							void (*notify)(struct virtqueue *),
							bool (*callback)(struct virtqueue *));


//#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)
#define to_vvq(_vq) (struct vring_virtqueue *)_vq

static int vring_add_buf(struct virtqueue *_vq,
			 struct VirtIOBufferDescriptor sg[],
			 unsigned int out,
			 unsigned int in,
			 void *data)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	unsigned int i, avail, head, prev;

	if(data == NULL) {
		DPrintf(0, ("%s: data is NULL!\n",  __FUNCTION__) );
		return -1;
	}

	if(out + in > vq->vring.num) {
		DPrintf(0, ("%s: out + in > vq->vring.num!\n",  __FUNCTION__) );
		return -1;
	}

	if(out + in == 0) {
		DPrintf(0, ("%s: out + in == 0!\n",  __FUNCTION__) );
		return -1;
	}

	if (vq->num_free < out + in) {
		DPrintf(0, ("Can't add buf len %i - avail = %i\n",
			 out + in, vq->num_free) );
		/*
		notify the host immediately if we are out of
		descriptors in tx ring
		*/
		if (out)
			vq->notify(&vq->vq);
		return -1;
	}

	/* We're about to use some buffers from the free list. */
	vq->num_free -= out + in;

	head = vq->free_head;
	for (i = vq->free_head; out; i = vq->vring.desc[i].next, out--) {
		vq->vring.desc[i].flags = VRING_DESC_F_NEXT;
		vq->vring.desc[i].addr = sg->physAddr.QuadPart;
		vq->vring.desc[i].len = sg->ulSize;
		prev = i;
		sg++;
	}
	for (; in; i = vq->vring.desc[i].next, in--) {
		vq->vring.desc[i].flags = VRING_DESC_F_NEXT|VRING_DESC_F_WRITE;
		vq->vring.desc[i].addr = sg->physAddr.QuadPart;
		vq->vring.desc[i].len = sg->ulSize;
		prev = i;
		sg++;
	}
	/* Last one doesn't continue. */
	vq->vring.desc[prev].flags &= ~VRING_DESC_F_NEXT;

	/* Update free pointer */
	vq->free_head = i;

	/* Set token. */
	vq->data[head] = data;

	/* Put entry in available array (but don't update avail->idx until they
	* do sync).  FIXME: avoid modulus here? */
	avail = (vq->vring.avail->idx + vq->num_added++) % vq->vring.num;
	DPrintf(6, ("%s >>> avail %d vq->vring.avail->idx = %d, vq->num_added = %d vq->vring.num = %d\n", __FUNCTION__, avail, vq->vring.avail->idx, vq->num_added, vq->vring.num));
	vq->vring.avail->ring[avail] = (u16) head;

	DPrintf(6, ("Added buffer head %i to %p\n", head, vq) );

	return 0;
}

static void vring_kick_always(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	/* Descriptors and available array need to be set before we expose the
	 * new available array entries. */
	wmb();

	vq->vring.avail->idx += (u16) vq->num_added;
	DPrintf(4, ("%s>>> vq->vring.avail->idx %d\n", __FUNCTION__, vq->vring.avail->idx));
	vq->num_added = 0;

	/* Need to update avail index before checking if we should notify */
	mb();

//	if (!(vq->vring.used->flags & VRING_USED_F_NO_NOTIFY))
		/* Prod other side to tell it about changes. */
	// Always kick the ring
	vq->notify(&vq->vq);
}

static void vring_kick(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	/* Descriptors and available array need to be set before we expose the
	 * new available array entries. */
	wmb();

	vq->vring.avail->idx += (u16) vq->num_added;
	DPrintf(4, ("%s>>> vq->vring.avail->idx %d\n", __FUNCTION__, vq->vring.avail->idx));
	vq->num_added = 0;

	/* Need to update avail index before checking if we should notify */
	mb();

	if (!(vq->vring.used->flags & VRING_USED_F_NO_NOTIFY))
		/* Prod other side to tell it about changes. */
		vq->notify(&vq->vq);
}

static void detach_buf(struct vring_virtqueue *vq, unsigned int head)
{
	unsigned int i;

	/* Clear data ptr. */
	vq->data[head] = NULL;

	/* Put back on free list: find end */
	i = head;
	while (vq->vring.desc[i].flags & VRING_DESC_F_NEXT) {
		i = vq->vring.desc[i].next;
		vq->num_free++;
	}

	vq->vring.desc[i].next = (u16) vq->free_head;
	vq->free_head = head;
	/* Plus final descriptor */
	vq->num_free++;
}

static void vring_enable_interrupts(struct virtqueue *_vq, bool enable)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	if(enable)
		vq->vring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
	else
		vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;

	mb();
}

/*
 changed: vring_shutdown brings the queue to initial state, as it was
 upon initialization (for proper power management)
*/
/* FIXME: We need to tell other side about removal, to synchronize. */
static void vring_shutdown(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	unsigned int num = vq->vring.num;
	void *pages = vq->vring.desc;
	VirtIODevice * pVirtIODevice = vq->vq.vdev;
	void (*notify)(struct virtqueue *) = vq->notify;
	bool (*callback)(struct virtqueue *) = vq->vq.callback;
	void *priv = vq->vq.priv;

	memset(pages, 0, vring_size(num,PAGE_SIZE));
	initialize_virtqueue(vq, num, pVirtIODevice, pages, notify, callback);
	vq->vq.priv = priv;
}

static bool more_used(const struct vring_virtqueue *vq)
{
    return vring_last_used(&vq->vring) != vq->vring.used->idx;
}

static void *vring_get_buf(struct virtqueue *_vq, unsigned int *len)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	void *ret;
    struct vring_used_elem *u;
	unsigned int i;

	if (!more_used(vq)) {
		DPrintf(4, ("No more buffers in queue: last_used_idx %d vring.used->idx %d\n", 
			vring_last_used(&vq->vring),
			vq->vring.used->idx));
		return NULL;
	}

	u = &vq->vring.used->ring[vring_last_used(&vq->vring) % vq->vring.num];
	i = u->id;
	*len = u->len;


	DPrintf(4, ("%s>>> id %d, len %d\n", __FUNCTION__, i, *len) );

	if (i >= vq->vring.num) {
		DPrintf(0, ("id %u out of range\n", i) );
		return NULL;
	}
	if (!vq->data[i]) {
		DPrintf(0, ("id %u is not a head!\n", i) );
		return NULL;
	}

	/* detach_buf clears data, so grab it now. */
	ret = vq->data[i];
	detach_buf(vq, i);
    vring_last_used(&vq->vring)++;
	return ret;
}

static bool vring_restart(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	DPrintf(6, ("%s\n", __FUNCTION__) );

	//BUG_ON(!(vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT));

	/* We optimistically turn back on interrupts, then check if there was
	 * more to do. */
	vq->vring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
	mb();
	if (more_used(vq)) {
		vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
		return 0;
	}

	return 1;
}

static struct virtqueue_ops vring_vq_ops = { vring_add_buf,
											 vring_kick,
											 vring_kick_always,
											 vring_get_buf,
											 vring_restart,
											 vring_shutdown,
											 vring_enable_interrupts};


void initialize_virtqueue(struct vring_virtqueue *vq,
							unsigned int num,
							VirtIODevice * pVirtIODevice,
							void *pages,
							void (*notify)(struct virtqueue *),
							bool (*callback)(struct virtqueue *))
{
	unsigned int i = num;
	memset(vq, 0, sizeof(*vq) + sizeof(void *)*num);

	vring_init(&vq->vring, num, pages, PAGE_SIZE);
	vq->vq.callback = callback;
	vq->vq.vdev = pVirtIODevice;
	vq->vq.vq_ops = &vring_vq_ops;
	vq->notify = notify;
	vq->broken = 0;
	vring_last_used(&vq->vring) = 0;
	vq->num_added = 0;

	/* No callback?  Tell other side not to bother us. */
	// TBD
	//if (!callback)
	//	vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;

	/* Put everything in free lists. */
	vq->num_free = num;
	vq->free_head = 0;
	for (i = 0; i < num-1; i++)
		vq->vring.desc[i].next = i+1;

}

struct virtqueue *vring_new_virtqueue(unsigned int num,
									  VirtIODevice * pVirtIODevice,
									  void *pages,
									  void (*notify)(struct virtqueue *),
									  bool (*callback)(struct virtqueue *))
{
	struct vring_virtqueue *vq;
	unsigned int i;

	DPrintf(0, ("Creating new virtqueue>>> size %d, pages %p\n", num, pages) );

	/* We assume num is a power of 2. */
	if (num & (num - 1)) {
		DPrintf(0, ("Bad virtqueue length %u\n", num));
		return NULL;
	}

	vq = AllocatePhysical(sizeof(*vq) + sizeof(void *)*num);
	if (!vq)
		return NULL;

	initialize_virtqueue(vq, num, pVirtIODevice, pages, notify, callback);

	return &vq->vq;
}


void vring_del_virtqueue(struct virtqueue *vq)
{
	if(vq) {
		MmFreeContiguousMemory(to_vvq(vq));
	}
}
