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
/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "VirtIO_PCI.h"
#include "virtio_ring.h"
#include "virtio_stor_utils.h"
#include "virtio_stor.h"

static
VOID
initialize_virtqueue(
    IN struct vring_virtqueue *vq,
    IN unsigned int num,
    IN PVOID DeviceExtension,
    IN PVOID pages,
    IN VOID (*notify)(struct virtqueue *));


//#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)
#define to_vvq(_vq) (struct vring_virtqueue *)_vq

static
int
vring_add_buf(
    IN struct virtqueue *_vq,
    IN struct VirtIOBufferDescriptor sg[],
    IN unsigned int out,
    IN unsigned int in,
    IN PVOID data)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    unsigned int i, avail, head, prev;

    if(data == NULL) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s: data is NULL!\n",  __FUNCTION__) );
        return -1;
    }

    if(out + in > vq->vring.num) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s: out + in > vq->vring.num!\n",  __FUNCTION__) );
        return -1;
    }

    if(out + in == 0) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s: out + in == 0!\n",  __FUNCTION__) );
        return -1;
    }

    if (vq->num_free < out + in) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s: can't add buf len %i - avail = %i\n",
           __FUNCTION__, out + in, vq->num_free) );
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
    prev = 0;
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
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s >>> avail %d vq->vring.avail->idx = %d, vq->num_added = %d vq->vring.num = %d\n",
        __FUNCTION__, avail, vq->vring.avail->idx, vq->num_added, vq->vring.num));

    vq->vring.avail->ring[avail] = (u16) head;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s: Added buffer head %i to %p\n",
        __FUNCTION__, head, vq) );

    return vq->num_free;
}

static
VOID
vring_kick_always(
    struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);

    /* Descriptors and available array need to be set before we expose the
    * new available array entries. */
    wmb();

    vq->vring.avail->idx += (u16) vq->num_added;
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s>>> vq->vring.avail->idx %d\n", __FUNCTION__, vq->vring.avail->idx));
    vq->num_added = 0;

    /* Need to update avail index before checking if we should notify */
    mb();

    vq->notify(&vq->vq);
}

static
VOID
vring_kick(
    struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);

	/* Descriptors and available array need to be set before we expose the
	 * new available array entries. */
    wmb();

    vq->vring.avail->idx += (u16) vq->num_added;
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s>>> vq->vring.avail->idx %d\n", __FUNCTION__, vq->vring.avail->idx));
    vq->num_added = 0;

	/* Need to update avail index before checking if we should notify */
    mb();

    if (!(vq->vring.used->flags & VRING_USED_F_NO_NOTIFY)) {
        /* Prod other side to tell it about changes. */
        vq->notify(&vq->vq);
    }
}

static
VOID
detach_buf(
    struct vring_virtqueue *vq,
    unsigned int head)
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


/*
 changed: vring_shutdown brings the queue to initial state, as it was
 upon initialization (for proper power management)
*/
/* FIXME: We need to tell other side about removal, to synchronize. */
static
VOID
vring_shutdown(
    struct virtqueue *_vq)
{
    PVOID DeviceExtension = _vq->DeviceExtension;
    struct vring_virtqueue *vq = to_vvq(_vq);
    unsigned int num = vq->vring.num;
    void *pages = vq->vring.desc;
    void (*notify)(struct virtqueue *) = vq->notify;
    void *priv = vq->vq.priv;

    memset(pages, 0, vring_size(num,PAGE_SIZE));
    initialize_virtqueue(vq, num, DeviceExtension, pages, notify);
    vq->vq.priv = priv;
}

static
bool
more_used(
    const struct vring_virtqueue *vq)
{
    return vq->last_used_idx != vq->vring.used->idx;
}

static
PVOID
vring_get_buf(
    struct virtqueue *_vq,
    unsigned int *len)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    void *ret;
    unsigned int i;

    if (!more_used(vq)) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("No more buffers in queue: last_used_idx %d vring.used->idx %d\n", vq->last_used_idx, vq->vring.used->idx));
        return NULL;
    }

    i = vq->vring.used->ring[vq->last_used_idx%vq->vring.num].id;
    *len = vq->vring.used->ring[vq->last_used_idx%vq->vring.num].len;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s>>> id %d, len %d\n", __FUNCTION__, i, *len) );

    if (i >= vq->vring.num) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("id %u out of range\n", i) );
        return NULL;
    }
    if (!vq->data[i]) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("id %u is not a head!\n", i) );
        return NULL;
    }

	/* detach_buf clears data, so grab it now. */
    ret = vq->data[i];
    detach_buf(vq, i);
    vq->last_used_idx++;
    return ret;
}

static
bool
vring_restart(
    struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s\n", __FUNCTION__) );

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

static struct virtqueue_ops vring_vq_ops = {
    vring_add_buf,
    vring_kick,
    vring_kick_always,
    vring_get_buf,
    vring_restart,
    vring_shutdown
};


VOID
initialize_virtqueue(
    struct vring_virtqueue *vq,
    unsigned int num,
    IN PVOID DeviceExtension,
    IN PVOID pages,
    void (*notify)(struct virtqueue *))
{
    unsigned int i = num;
    memset(vq, 0, sizeof(*vq) + sizeof(void *)*num);

    vring_init(&vq->vring, num, pages, PAGE_SIZE);
    vq->vq.DeviceExtension = DeviceExtension;
    vq->vq.vq_ops = &vring_vq_ops;
    vq->notify = notify;
    vq->broken = 0;
    vq->last_used_idx = 0;
    vq->num_added = 0;

    /* Put everything in free lists. */
    vq->num_free = num;
    vq->free_head = 0;
    for (i = 0; i < num-1; i++)
        vq->vring.desc[i].next = (u16)(i+1);

}

struct
virtqueue*
vring_new_virtqueue(
    unsigned int num,
    IN PVOID DeviceExtension,
    IN PVOID pages,
    IN VOID (*notify)(struct virtqueue *))
{
    struct vring_virtqueue *vq;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s: Creating new virtqueue>>> size %d, pages %p\n", __FUNCTION__, num, pages) );

    /* We assume num is a power of 2. */
    if (num & (num - 1)) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s: Bad virtqueue length %u\n", __FUNCTION__, num));
        return NULL;
    }

    vq = adaptExt->virtqueue;

    initialize_virtqueue(vq, num, DeviceExtension, pages, notify);

    return &vq->vq;
}
