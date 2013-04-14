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

#ifdef WPP_EVENT_TRACING
#include "VirtIORing.tmh"
#endif

struct vring_virtqueue
{
    struct virtqueue vq;

    /* Actual memory layout for this queue */
    struct vring vring;

    /* Other side has made a mess, don't try any more. */
    bool broken;

    /* Whether to use published indices mechanism */
    bool use_published_indices;

    /* Number of free buffers */
    unsigned int num_free;
    /* Head of free buffer list. */
    unsigned int free_head;
    /* Number we've added since last sync. */
    unsigned int num_added;
    /* Last used index */
    u16 last_used_idx;

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
                            unsigned int index,
                            bool use_published_indices
                            );


//#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)
#define to_vvq(_vq) (struct vring_virtqueue *)_vq

static
int
vring_add_indirect(
    IN struct virtqueue *_vq,
    IN struct VirtIOBufferDescriptor sg[],
    IN unsigned int out,
    IN unsigned int in,
    IN PVOID va,
    IN ULONGLONG phys
    )
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    struct vring_desc *desc = (struct vring_desc *)va;
    unsigned head;
    unsigned int i;

    if (!phys) {
        return -1;
    }
    /* Transfer entries from the sg list into the indirect page */
    for (i = 0; i < out; i++) {
        desc[i].flags = VRING_DESC_F_NEXT;
        desc[i].addr = sg->physAddr.QuadPart;
        desc[i].len = sg->ulSize;
        desc[i].next = i+1;
        sg++;
    }
    for (; i < (out + in); i++) {
        desc[i].flags = VRING_DESC_F_NEXT|VRING_DESC_F_WRITE;
        desc[i].addr = sg->physAddr.QuadPart;
        desc[i].len = sg->ulSize;
        desc[i].next = i+1;
        sg++;
    }

    /* Last one doesn't continue. */
    desc[i-1].flags &= ~VRING_DESC_F_NEXT;
    desc[i-1].next = 0;

    /* We're about to use a buffer */
    vq->num_free--;

    /* Use a single buffer which doesn't continue */
    head = vq->free_head;
    vq->vring.desc[head].flags = VRING_DESC_F_INDIRECT;
    vq->vring.desc[head].addr = phys;
    vq->vring.desc[head].len = i * sizeof(struct vring_desc);

    /* Update free pointer */
    vq->free_head = vq->vring.desc[head].next;

    return head;
}

static void vring_delay_interrupts(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    u16 buffs_num_before_interrupt = ((u16) (vq->vring.avail->idx - vq->last_used_idx)) * 3 / 4;

    *vq->vring.vring_last_used_ptr = vq->vring.used->idx +
                                    buffs_num_before_interrupt;

    mb();
}

static int vring_add_buf(struct virtqueue *_vq,
             struct VirtIOBufferDescriptor sg[],
             unsigned int out,
             unsigned int in,
             void *data,
             void *va_indirect,
             ULONGLONG phys_indirect
             )
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    unsigned int i, avail, head, prev = 0;

    if(data == NULL) {
        DPrintf(0, ("%s: data is NULL!\n",  __FUNCTION__) );
        return -1;
    }

    if (va_indirect)
    {
        int ret = vring_add_indirect(_vq, sg, out, in, va_indirect, phys_indirect);
        if (ret >= 0)
        {
            head = (unsigned int)ret;
            goto add_head;
        }
        else
        {
            DPrintf(0, ("%s: no physical storage provided!\n",  __FUNCTION__) );
            return -1;
        }
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

add_head:
    /* Set token. */
    vq->data[head] = data;

    /* Put entry in available array (but don't update avail->idx until they
    * do sync).  FIXME: avoid modulus here? */

    avail = vq->vring.avail->idx % vq->vring.num;

    DPrintf(6, ("%s >>> avail %d vq->vring.avail->idx = %d, vq->num_added = %d vq->vring.num = %d\n", __FUNCTION__, avail, vq->vring.avail->idx, vq->num_added, vq->vring.num));
    vq->vring.avail->ring[avail] = (u16) head;

    DPrintf(6, ("Added buffer head %i to %p\n", head, vq) );

    //Flush ring changes before index advancement
    mb();

    vq->vring.avail->idx++;
    vq->num_added++;

    return vq->num_free;
}

static void vring_kick_always(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);

    /* Descriptors and available array need to be set before we expose the
     * new available array entries. */
    wmb();

    DPrintf(4, ("%s>>> vq->vring.avail->idx %d\n", __FUNCTION__, vq->vring.avail->idx));
    vq->num_added = 0;

    /* Need to update avail index before checking if we should notify */
    mb();

    vq->notify(&vq->vq);
}

static void vring_kick(struct virtqueue *_vq)
{
    u16 prev_avail_idx;
    struct vring_virtqueue *vq = to_vvq(_vq);

    /* Descriptors and available array need to be set before we expose the
     * new available array entries. */
    mb();

    prev_avail_idx = vq->vring.avail->idx - (u16) vq->num_added;
    DPrintf(4, ("%s>>> vq->vring.avail->idx %d\n", __FUNCTION__, vq->vring.avail->idx));
    vq->num_added = 0;

    /* Need to update avail index before checking if we should notify */
    mb();

    if(vq->use_published_indices) {
        if(vring_need_event(vring_last_avail(&vq->vring),
                            vq->vring.avail->idx,
                            prev_avail_idx))
            vq->notify(&vq->vq);
    } else {
        if (!(vq->vring.used->flags & VRING_USED_F_NO_NOTIFY))
            /* Prod other side to tell it about changes. */
            vq->notify(&vq->vq);
    }
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

static void vring_enable_interrupts(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);

    vq->vring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
    *vq->vring.vring_last_used_ptr = vq->last_used_idx;

    mb();
}

static void vring_disable_interrupts(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);

    vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;

    mb();
}

static BOOLEAN vring_is_interrupt_enabled(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    return (vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT) ? FALSE : TRUE;
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
    unsigned int index = vq->vq.ulIndex;
    void *pages = vq->vring.desc;
    VirtIODevice * pVirtIODevice = vq->vq.vdev;
    bool use_published_indices = vq->use_published_indices;
    void (*notify)(struct virtqueue *) = vq->notify;

    memset(pages, 0, vring_size(num,PAGE_SIZE));
    initialize_virtqueue(vq, num, pVirtIODevice, pages, notify, index, use_published_indices);
}

static bool more_used(const struct vring_virtqueue *vq)
{
    return vq->last_used_idx != vq->vring.used->idx;
}

static void *vring_get_buf(struct virtqueue *_vq, unsigned int *len)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    void *ret;
    struct vring_used_elem *u;
    unsigned int i;

    if (!more_used(vq)) {
        DPrintf(4, ("No more buffers in queue: last_used_idx %d vring.used->idx %d\n",
            vq->last_used_idx,
            vq->vring.used->idx));
        return NULL;
    }

    /* Only get used array entries after they have been exposed by host. */
    rmb();

    u = &vq->vring.used->ring[vq->last_used_idx % vq->vring.num];
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
    ++vq->last_used_idx;

    if (!(vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
        *vq->vring.vring_last_used_ptr = vq->last_used_idx;
        mb();
    }

    return ret;
}

static bool vring_restart(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);

    DPrintf(6, ("%s\n", __FUNCTION__) );

    //BUG_ON(!(vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT));

    /* We optimistically turn back on interrupts, then check if there was
     * more to do. */
    vring_enable_interrupts(_vq);

    if (more_used(vq)) {
        vring_disable_interrupts(_vq);
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
                                             vring_enable_interrupts,
                                             vring_disable_interrupts,
                                             vring_is_interrupt_enabled,
                                             vring_delay_interrupts};

void initialize_virtqueue(struct vring_virtqueue *vq,
                            unsigned int num,
                            VirtIODevice * pVirtIODevice,
                            void *pages,
                            void (*notify)(struct virtqueue *),
                            unsigned int index,
                            bool use_published_indices)
{
    unsigned int i = num;
    memset(vq, 0, sizeof(*vq) + sizeof(void *)*num);

    vring_init(&vq->vring, num, pages, PAGE_SIZE);
    vq->vq.vdev = pVirtIODevice;
    vq->vq.vq_ops = &vring_vq_ops;
    vq->notify = notify;
    vq->broken = 0;
    vq->vq.ulIndex = index;
    *vq->vring.vring_last_used_ptr = vq->last_used_idx = 0;
    vq->num_added = 0;
    vq->use_published_indices = use_published_indices;

    /* No callback?  Tell other side not to bother us. */
    // TBD
    //if (!callback)
    //  vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;

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
                                      void *control,
                                      unsigned int index,
                                      BOOLEAN use_published_indices
                                      )
{
    struct vring_virtqueue *vq;

    DPrintf(0, ("Creating new virtqueue>>> size %d, pages %p\n", num, pages) );

    /* We assume num is a power of 2. */
    if (num & (num - 1)) {
        DPrintf(0, ("Bad virtqueue length %u\n", num));
        return NULL;
    }

    vq = control;
    if (!vq)
        return NULL;

    initialize_virtqueue(vq, num, pVirtIODevice, pages, notify, index, use_published_indices);

    return &vq->vq;
}

void* vring_detach_unused_buf(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    unsigned int i;
    void *buf;

    for (i = 0; i < vq->vring.num; i++) {
        if (!vq->data[i])
            continue;
        buf = vq->data[i];
        detach_buf(vq, i);
        vq->vring.avail->idx--;
        return buf;
    }
    return NULL;
}

unsigned int vring_control_block_size()
{
    return sizeof(struct vring_virtqueue);
}
