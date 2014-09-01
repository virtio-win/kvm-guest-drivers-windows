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

#ifdef DEBUG
#define START_USE(_vq)                                          \
__pragma(warning(push))                                          \
__pragma(warning(disable:4127))                                 \
    do {                                                        \
        if ((_vq)->in_use)                                      \
            DPrintf(0, ("%s:in_use = %i\n",                     \
                           (_vq)->vq.name, (_vq)->in_use));     \
        (_vq)->in_use = __LINE__;                               \
    } while (0) \
__pragma(warning(pop))

#define END_USE(_vq) \
__pragma(warning(push))                                          \
__pragma(warning(disable:4127))                                 \
        do { BUG_ON(!(_vq)->in_use); (_vq)->in_use = 0; } while(0) \
__pragma(warning(pop))
#else
#define START_USE(vq)
#define END_USE(vq)
#endif
#define virtio_mb(vq) mb()
#define virtio_wmb(vq) wmb()
#define virtio_rmb(vq) rmb()
#define BAD_RING(vq, e) DPrintf(0, e)

#pragma warning (push)
#pragma warning (disable:4200)
struct vring_virtqueue
{
    struct virtqueue vq;

    /* Actual memory layout for this queue */
    struct vring vring;

    /* Can we use weak barriers? */
    // bool weak_barriers;

    /* Other side has made a mess, don't try any more. */
    bool broken;

    /* Host supports indirect buffers */
    // bool indirect;

    /* Host publishes avail event idx */
    bool event;

    /* Head of free buffer list. */
    unsigned int free_head;
    /* Number we've added since last sync. */
    unsigned int num_added;

    /* Last used index we've seen. */
    u16 last_used_idx;

    /* How to notify other side. FIXME: commonalize hcalls! */
    void (*notify)(struct virtqueue *vq);

#ifdef DEBUG
    /* They're supposed to lock for us. */
    unsigned int in_use;

    /* Figure out if their kicks are too delayed. */
    bool last_add_time_valid;
    ktime_t last_add_time;
#endif

    /* Tokens for callbacks. */
    void *data[];
};
#pragma warning(pop)

static void initialize_virtqueue(struct vring_virtqueue* vq,
                                 unsigned int index,
                                 unsigned int num,
                                 unsigned int vring_align,
                                 virtio_device *vdev,
                                 bool event,
                                 void *pages,
                                 void (*notify)(struct virtqueue *),
                                 // void (*callback)(struct virtqueue *),
                                 const char *name);


//#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)
#define to_vvq(_vq) (struct vring_virtqueue *)_vq
#define sg_phys(sg) sg->physAddr.QuadPart

/* Set up an indirect table of descriptors and add it to the queue. */
static int vring_add_indirect(struct vring_virtqueue *vq,
                             struct scatterlist sg[],
                             unsigned int out,
                             unsigned int in,
                             PVOID va,
                             ULONGLONG phys)
{
    struct vring_desc *desc = (struct vring_desc *)va;
    unsigned head;
    unsigned int i;

    /* Transfer entries from the sg list into the indirect page */
    for (i = 0; i < out; i++) {
        desc[i].flags = VRING_DESC_F_NEXT;
        desc[i].addr = sg_phys(sg);
        desc[i].len = sg->length;
        desc[i].next = (u16) i+1;
        sg++;
    }
    for (; i < (out + in); i++) {
        desc[i].flags = VRING_DESC_F_NEXT|VRING_DESC_F_WRITE;
        desc[i].addr = sg_phys(sg);
        desc[i].len = sg->length;
        desc[i].next = (u16) i + 1;
        sg++;
    }

    /* Last one doesn't continue. */
    desc[i-1].flags &= ~VRING_DESC_F_NEXT;
    desc[i-1].next = 0;

    /* We're about to use a buffer */
    vq->vq.num_free--;

    /* Use a single buffer which doesn't continue */
    head = vq->free_head;
    vq->vring.desc[head].flags = VRING_DESC_F_INDIRECT;
    vq->vring.desc[head].addr = phys;
    vq->vring.desc[head].len = i * sizeof(struct vring_desc);

    /* Update free pointer */
    vq->free_head = vq->vring.desc[head].next;

    return head;
}

/**
 * virtqueue_add_buf - expose buffer to other end
 * @vq: the struct virtqueue we're talking about.
 * @sg: the description of the buffer(s).
 * @out_num: the number of sg readable by other side
 * @in_num: the number of sg which are writable (after readable ones)
 * @data: the token identifying the buffer.
 *
 * Caller must ensure we don't call this with other virtqueue operations
 * at the same time (except where noted).
 *
 * Returns zero or a negative error (ie. ENOSPC, ENOMEM).
 */
int virtqueue_add_buf(struct virtqueue *_vq,
                      struct scatterlist sg[],
                      unsigned int out,
                      unsigned int in,
                      void *data,
                      void *va_indirect,
                      ULONGLONG phys_indirect)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    unsigned int i, avail, prev = 0;
    unsigned int head;

    START_USE(vq);

    BUG_ON(data == NULL);

#ifdef DEBUG
    {
        ktime_t now = ktime_get();

        /* No kick or get, with .1 second between?  Warn. */
        if (vq->last_add_time_valid)
            WARN_ON(ktime_to_ms(ktime_sub(now, vq->last_add_time))
                        > 100);
        vq->last_add_time = now;
        vq->last_add_time_valid = true;
    }
#endif

    /* If the host supports indirect descriptor tables, and we have multiple
     * buffers, then go indirect. FIXME: tune this threshold */
    if (va_indirect && (out + in) > 1 && vq->vq.num_free) {
        int ret = vring_add_indirect(vq, sg, out, in, va_indirect, phys_indirect);
        if (likely(ret >= 0))
        {
            head = (unsigned int)ret;
            goto add_head;
        }
    }

    BUG_ON(out + in > vq->vring.num);
    BUG_ON(out + in == 0);

    if (vq->vq.num_free < out + in) {
        DPrintf(0, ("Can't add buf len %i - avail = %i\n",
             out + in, vq->vq.num_free) );
        /* FIXME: for historical reasons, we force a notify here if
         * there are outgoing parts to the buffer.  Presumably the
         * host should service the ring ASAP. */
        if (out)
            vq->notify(&vq->vq);
        END_USE(vq);
        return -ENOSPC;
    }

    /* We're about to use some buffers from the free list. */
    vq->vq.num_free -= out + in;

    head = vq->free_head;
    for (i = vq->free_head; out; i = vq->vring.desc[i].next, out--) {
        vq->vring.desc[i].flags = VRING_DESC_F_NEXT;
        vq->vring.desc[i].addr = sg_phys(sg);
        vq->vring.desc[i].len = sg->length;
        prev = i;
        sg++;
    }
    for (; in; i = vq->vring.desc[i].next, in--) {
        vq->vring.desc[i].flags = VRING_DESC_F_NEXT|VRING_DESC_F_WRITE;
        vq->vring.desc[i].addr = sg_phys(sg);
        vq->vring.desc[i].len = sg->length;
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
     * do sync). */
    avail = (vq->vring.avail->idx & (vq->vring.num-1));
    vq->vring.avail->ring[avail] = (u16) head;

    /* Descriptors and available array need to be set before we expose the
     * new available array entries. */
    virtio_wmb(vq);
    vq->vring.avail->idx++;
    vq->num_added++;

    /* This is very unlikely, but theoretically possible.  Kick
     * just in case. */
    if (unlikely(vq->num_added == (1 << 16) - 1))
        virtqueue_kick(_vq);

    DPrintf(6, ("Added buffer head %i to %p\n", head, vq) );
    END_USE(vq);

    return 0;
}

/**
 * virtqueue_kick_prepare - first half of split virtqueue_kick call.
 * @vq: the struct virtqueue
 *
 * Instead of virtqueue_kick(), you can do:
 *    if (virtqueue_kick_prepare(vq))
 *        virtqueue_notify(vq);
 *
 * This is sometimes useful because the virtqueue_kick_prepare() needs
 * to be serialized, but the actual virtqueue_notify() call does not.
 */
bool virtqueue_kick_prepare(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    u16 new, old;
    bool needs_kick;

    START_USE(vq);
    /* We need to expose available array entries before checking avail
     * event. */
    virtio_mb(vq);

    old = (u16) (vq->vring.avail->idx - vq->num_added);
    new = vq->vring.avail->idx;
    vq->num_added = 0;

#ifdef DEBUG
    if (vq->last_add_time_valid) {
        WARN_ON(ktime_to_ms(ktime_sub(ktime_get(),
                          vq->last_add_time)) > 100);
    }
    vq->last_add_time_valid = false;
#endif

    if (vq->event) {
        needs_kick = vring_need_event(vring_avail_event(&vq->vring),
                          new, old);
    } else {
        needs_kick = !(vq->vring.used->flags & VRING_USED_F_NO_NOTIFY);
    }
    END_USE(vq);
    return needs_kick;
}

/**
 * virtqueue_notify - second half of split virtqueue_kick call.
 * @vq: the struct virtqueue
 *
 * This does not need to be serialized.
 */
void virtqueue_notify(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);

    /* Prod other side to tell it about changes. */
    vq->notify(_vq);
}

/**
 * virtqueue_kick - update after add_buf
 * @vq: the struct virtqueue
 *
 * After one or more virtqueue_add_buf calls, invoke this to kick
 * the other side.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 */
void virtqueue_kick(struct virtqueue *vq)
{
    if (virtqueue_kick_prepare(vq))
        virtqueue_notify(vq);
}

static void detach_buf(struct vring_virtqueue *vq, unsigned int head)
{
    unsigned int i;

    /* Clear data ptr. */
    vq->data[head] = NULL;

    /* Put back on free list: find end */
    i = head;

    /* Free the indirect table */
    /*if (vq->vring.desc[i].flags & VRING_DESC_F_INDIRECT)
        kfree(phys_to_virt(vq->vring.desc[i].addr));*/

    while (vq->vring.desc[i].flags & VRING_DESC_F_NEXT) {
        i = vq->vring.desc[i].next;
        vq->vq.num_free++;
    }

    vq->vring.desc[i].next = (u16) vq->free_head;
    vq->free_head = head;
    /* Plus final descriptor */
    vq->vq.num_free++;
}

static inline bool more_used(const struct vring_virtqueue *vq)
{
    return vq->last_used_idx != vq->vring.used->idx;
}

/**
 * virtqueue_enable_cb - restart callbacks after disable_cb.
 * @vq: the struct virtqueue we're talking about.
 *
 * This re-enables callbacks; it returns "false" if there are pending
 * buffers in the queue, to detect a possible race between the driver
 * checking for more work, and enabling callbacks.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 */
bool virtqueue_enable_cb(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);

    START_USE(vq);

    /* We optimistically turn back on interrupts, then check if there was
     * more to do. */
    /* Depending on the VIRTIO_RING_F_EVENT_IDX feature, we need to
     * either clear the flags bit or point the event index at the next
     * entry. Always do both to keep code simple. */
    vq->vring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
    vring_used_event(&vq->vring) = vq->last_used_idx;
    virtio_mb(vq);
    if (unlikely(more_used(vq))) {
        END_USE(vq);
        return false;
    }

    END_USE(vq);
    return true;
}

/**
 * virtqueue_disable_cb - disable callbacks
 * @vq: the struct virtqueue we're talking about.
 *
 * Note that this is not necessarily synchronous, hence unreliable and only
 * useful as an optimization.
 *
 * Unlike other operations, this need not be serialized.
 */
void virtqueue_disable_cb(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);

    vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
}

BOOLEAN virtqueue_is_interrupt_enabled(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    return (vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT) ? FALSE : TRUE;
}

/*
 changed: vring_shutdown brings the queue to initial state, as it was
 upon initialization (for proper power management)
*/
/* FIXME: We need to tell other side about removal, to synchronize. */
void virtqueue_shutdown(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    unsigned int num = vq->vring.num;
    unsigned int index = vq->vq.index;
    void *pages = vq->vring.desc;
    virtio_device *vdev = vq->vq.vdev;
    bool event = vq->event;
    void (*notify)(struct virtqueue *) = vq->notify;
    const char* name = vq->vq.name;

    memset(pages, 0, vring_size(num,PAGE_SIZE));
    initialize_virtqueue(vq, index, num, PAGE_SIZE, vdev, event, pages, notify, name);
}

/**
 * virtqueue_get_buf - get the next used buffer
 * @vq: the struct virtqueue we're talking about.
 * @len: the length written into the buffer
 *
 * If the driver wrote data into the buffer, @len will be set to the
 * amount written.  This means you don't need to clear the buffer
 * beforehand to ensure there's no data leakage in the case of short
 * writes.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 *
 * Returns NULL if there are no used buffers, or the "data" token
 * handed to virtqueue_add_buf().
 */
void *virtqueue_get_buf(struct virtqueue *_vq, unsigned int *len)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    void *ret;
    unsigned int i;
    u16 last_used;

    START_USE(vq);

    if (unlikely(vq->broken)) {
        END_USE(vq);
        return NULL;
    }

    if (!more_used(vq)) {
        DPrintf(6, ("No more buffers in queue\n") );
        END_USE(vq);
        return NULL;
    }

    /* Only get used array entries after they have been exposed by host. */
    virtio_rmb(vq);

    last_used = (vq->last_used_idx & (vq->vring.num - 1));
    i = vq->vring.used->ring[last_used].id;
    *len = vq->vring.used->ring[last_used].len;

    if (unlikely(i >= vq->vring.num)) {
        BAD_RING(vq, ("id %u out of range\n", i) );
        return NULL;
    }
    if (unlikely(!vq->data[i])) {
        BAD_RING(vq, ("id %u is not a head!\n", i) );
        return NULL;
    }

    /* detach_buf clears data, so grab it now. */
    ret = vq->data[i];
    detach_buf(vq, i);
    vq->last_used_idx++;
    /* If we expect an interrupt for the next entry, tell host
     * by writing event index and flush out the write before
     * the read in the next get_buf call. */
    if (!(vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
        vring_used_event(&vq->vring) = vq->last_used_idx;
        virtio_mb(vq);
    }

#ifdef DEBUG
    vq->last_add_time_valid = false;
#endif

    END_USE(vq);
    return ret;
}

/**
 * virtqueue_enable_cb_delayed - restart callbacks after disable_cb.
 * @vq: the struct virtqueue we're talking about.
 *
 * This re-enables callbacks but hints to the other side to delay
 * interrupts until most of the available buffers have been processed;
 * it returns "false" if there are many pending buffers in the queue,
 * to detect a possible race between the driver checking for more work,
 * and enabling callbacks.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 */
bool virtqueue_enable_cb_delayed(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    u16 bufs;

    START_USE(vq);

    /* We optimistically turn back on interrupts, then check if there was
     * more to do. */
    /* Depending on the VIRTIO_RING_F_USED_EVENT_IDX feature, we need to
     * either clear the flags bit or point the event index at the next
     * entry. Always do both to keep code simple. */
    vq->vring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
    /* TODO: tune this threshold */
    bufs = (u16)(vq->vring.avail->idx - vq->last_used_idx) * 3 / 4;
    vring_used_event(&vq->vring) = vq->last_used_idx + bufs;
    virtio_mb(vq);
    if (unlikely((u16)(vq->vring.used->idx - vq->last_used_idx) > bufs)) {
        END_USE(vq);
        return false;
    }

    END_USE(vq);
    return true;
}


void initialize_virtqueue(struct vring_virtqueue* vq,
                          unsigned int index,
                          unsigned int num,
                          unsigned int vring_align,
                          virtio_device *vdev,
                          bool event,
                          void *pages,
                          void (*notify)(struct virtqueue *),
                          // void (*callback)(struct virtqueue *),
                          const char *name)
{
    unsigned short i = (unsigned short) num;
    memset(vq, 0, sizeof(*vq) + sizeof(void *)*num);

    vring_init(&vq->vring, num, pages, vring_align);
    vq->vq.vdev = vdev;
    vq->vq.name = name;
    vq->notify = notify;
    // vq->callback = callback;
    //vq->weak_barriers = weak_barriers;
    vq->broken = 0;
    vq->vq.index = index;
    vq->last_used_idx = 0;
    vq->num_added = 0;
#ifdef DEBUG
    vq->in_use = 0;
    vq->last_add_time_valid = 0;
#endif
    //vq->indirect = virtio_has_feature(vdev, VIRTIO_RING_F_INDIRECT_DESC);
    vq->event = event; // virtio_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX);

    /* No callback?  Tell other side not to bother us. */
    // TBD
    //if (!callback)
    //  vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;

    /* Put everything in free lists. */
    vq->vq.num_free = num;
    vq->free_head = 0;
    for (i = 0; i < num-1; i++) {
        vq->vring.desc[i].next = i+1;
        vq->data[i] = NULL;
    }
    vq->data[i] = NULL;
}

struct virtqueue *vring_new_virtqueue(unsigned int index,
                                      unsigned int num,
                                      unsigned int vring_align,
                                      virtio_device *vdev,
                                      bool event,
                                      void *pages,
                                      void (*notify)(struct virtqueue *),
                                      void *control,
                                      const char *name)
{
    struct vring_virtqueue *vq;

    /* We assume num is a power of 2. */
    if (num & (num - 1)) {
        DPrintf(0, ("Bad virtqueue length %u\n", num));
        return NULL;
    }

    vq = control;
    if (!vq)
        return NULL;

    initialize_virtqueue(vq, index, num, vring_align, vdev, event, pages, notify, name);

    return &vq->vq;
}

/**
 * virtqueue_detach_unused_buf - detach first unused buffer
 * @vq: the struct virtqueue we're talking about.
 *
 * Returns NULL or the "data" token handed to virtqueue_add_buf().
 * This is not valid on an active queue; it is useful only for device
 * shutdown.
 */
void *virtqueue_detach_unused_buf(struct virtqueue *_vq)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    unsigned int i;
    void *buf;

    START_USE(vq);

    for (i = 0; i < vq->vring.num; i++) {
        if (!vq->data[i])
            continue;
        /* detach_buf clears data, so grab it now. */
        buf = vq->data[i];
        detach_buf(vq, i);
        vq->vring.avail->idx--;
        END_USE(vq);
        return buf;
    }
    /* That should have freed everything. */
    BUG_ON(vq->vq.num_free != vq->vring.num);

    END_USE(vq);
    return NULL;
}

unsigned int vring_control_block_size()
{
    return sizeof(struct vring_virtqueue);
}

/**
 * virtqueue_get_vring_size - return the size of the virtqueue's vring
 * @vq: the struct virtqueue containing the vring of interest.
 *
 * Returns the size of the vring.  This is mainly used for boasting to
 * userspace.  Unlike other operations, this need not be serialized.
 */
unsigned int virtqueue_get_vring_size(struct virtqueue *_vq)
{

    struct vring_virtqueue *vq = to_vvq(_vq);

    return vq->vring.num;
}
