#ifndef _LINUX_VIRTIO_RING_H
#define _LINUX_VIRTIO_RING_H
/* An interface for efficient virtio implementation, currently for use by KVM
* and lguest, but hopefully others soon.  Do NOT change this since it will
* break existing servers and clients.
*
* This header is BSD licensed so anyone can use the definitions to implement
* compatible drivers/servers.
*
* Copyright Rusty Russell IBM Corporation 2007. */
#include "osdep.h"

/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT           1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE          2
#define VRING_DESC_F_INDIRECT       4

/* This means don't notify other side when buffer added. */
#define VRING_USED_F_NO_NOTIFY  1
/* This means don't interrupt guest when buffer consumed. */
#define VRING_AVAIL_F_NO_INTERRUPT  1

/* We support indirect buffer descriptors */
#define VIRTIO_RING_F_INDIRECT_DESC    28

/* The Guest publishes the used index for which it expects an interrupt
* at the end of the avail ring. Host should ignore the avail->flags field. */
/* The Host publishes the avail index for which it expects a kick
* at the end of the used ring. Guest should ignore the used->flags field. */
#define VIRTIO_RING_F_EVENT_IDX        29


#pragma warning (push)
#pragma warning (disable:4200)
#pragma pack (push)
#pragma pack (1)

/* Virtio ring descriptors: 16 bytes.  These can chain together via "next". */
struct vring_desc {
    /* Address (guest-physical). */
    __u64 addr;
    /* Length. */
    __u32 len;
    /* The flags as indicated above. */
    __u16 flags;
    /* We chain unused descriptors via this, too */
    __u16 next;
};

struct vring_avail {
    __u16 flags;
    __u16 idx;
    __u16 ring[];
};

/* u32 is used here for ids for padding reasons. */
struct vring_used_elem {
    /* Index of start of used descriptor chain. */
    __u32 id;
    /* Total length of the descriptor chain which was used (written to) */
    __u32 len;
};

struct vring_used {
    __u16 flags;
    __u16 idx;
    struct vring_used_elem ring[];
};

struct vring {
    unsigned int num;

    struct vring_desc *desc;

    struct vring_avail *avail;

    struct vring_used *used;
};
#pragma pack (pop)
#pragma warning (pop)

/* The standard layout for the ring is a continuous chunk of memory which looks
* like this.  We assume num is a power of 2.
*
* struct vring
* {
*    // The actual descriptors (16 bytes each)
*    struct vring_desc desc[num];
*
*    // A ring of available descriptor heads with free-running index.
*    __u16 avail_flags;
*    __u16 avail_idx;
*    __u16 available[num];
*    __u16 used_event_idx;
*
*    // Padding to the next align boundary.
*    char pad[];
*
*    // A ring of used descriptor heads with free-running index.
*    __u16 used_flags;
*    __u16 used_idx;
*    struct vring_used_elem used[num];
*    __u16 avail_event_idx;
* };
*/
/* We publish the used event index at the end of the available ring, and vice
* versa. They are at the end for backwards compatibility. */
#define vring_used_event(vr) ((vr)->avail->ring[(vr)->num])
#define vring_avail_event(vr) (*(__u16 *)&(vr)->used->ring[(vr)->num])

static inline void vring_init(struct vring *vr, unsigned int num, void *p,
    unsigned long align)
{
    vr->num = num;
    vr->desc = p;
    vr->avail = (void *) ((__u8 *)p + num*sizeof(struct vring_desc));
    vr->used = (void *)(((ULONG_PTR)&vr->avail->ring[num] + sizeof(__u16)
        + align - 1) & ~((ULONG_PTR)align - 1));
}

static inline unsigned vring_size(unsigned int num, unsigned long align)
{
    return ((sizeof(struct vring_desc) * num + sizeof(__u16) * (3 + num)
        + align - 1) & ~(align - 1))
        + sizeof(__u16) * 3 + sizeof(struct vring_used_elem) * num;
}

/* The following is used with USED_EVENT_IDX and AVAIL_EVENT_IDX */
/* Assuming a given event_idx value from the other size, if
* we have just incremented index from old to new_idx,
* should we trigger an event? */
static inline int vring_need_event(__u16 event_idx, __u16 new_idx, __u16 old)
{
    /* Note: Xen has similar logic for notification hold-off
    * in include/xen/interface/io/ring.h with req_event and req_prod
    * corresponding to event_idx + 1 and new_idx respectively.
    * Note also that req_event and req_prod in Xen start at 1,
    * event indexes in virtio start at 0. */
    return (__u16)(new_idx - event_idx - 1) < (__u16)(new_idx - old);
}

struct virtqueue *vring_new_virtqueue(unsigned int index,
    unsigned int num,
    unsigned int vring_align,
    virtio_device *vdev,
    bool event,
    void *pages,
    void(*notify)(struct virtqueue *),
    void *control,
    const char *name);

unsigned int vring_control_block_size();

#endif /* _LINUX_VIRTIO_RING_H */
