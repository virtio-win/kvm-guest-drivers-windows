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

#pragma pack (push)
#pragma pack (1)

/* Virtio ring descriptors: 16 bytes.  These can chain together via "next". */
struct vring_desc
{
    /* Address (guest-physical). */
    u64 addr;
    /* Length. */
    u32 len;
    /* The flags as indicated above. */
    u16 flags;
    /* We chain unused descriptors via this, too */
    u16 next;
};

struct vring_avail
{
    u16 flags;
    u16 idx;
    u16 ring[];
};

/* u32 is used here for ids for padding reasons. */
struct vring_used_elem
{
    /* Index of start of used descriptor chain. */
    u32 id;
    /* Total length of the descriptor chain which was used (written to) */
    u32 len;
};

struct vring_used
{
    u16 flags;
    u16 idx;
    struct vring_used_elem ring[];
};

struct vring {
    unsigned int num;

    struct vring_desc *desc;

    struct vring_avail *avail;

    struct vring_used *used;

    u16 *vring_last_used_ptr;
};
#pragma pack (pop)

/* The standard layout for the ring is a continuous chunk of memory which looks
 * like this.  We assume num is a power of 2.
 *
 * struct vring
 * {
 *  // The actual descriptors (16 bytes each)
 *  struct vring_desc desc[num];
 *
 *  // A ring of available descriptor heads with free-running index.
 *  __u16 avail_flags;
 *  __u16 avail_idx;
 *  __u16 available[num];
 *  __u16 last_used_idx;
 *
 *  // Padding to the next page boundary.
 *  char pad[];
 *
 *  // A ring of used descriptor heads with free-running index.
 *  __u16 used_flags;
 *  __u16 used_idx;
 *  struct vring_used_elem used[num];
 *  __u16 last_avail_idx;
 * };
 */

#define vring_last_used(vr) ((vr)->avail->ring[(vr)->num])
#define vring_last_avail(vr) (*(u16 *)&(vr)->used->ring[(vr)->num])

static void vring_init(struct vring *vr, unsigned int num, void *p,
                  unsigned long pagesize)
{
    vr->num = num;
    vr->desc = p;
    vr->avail = (void *) ((u8 *)p + num*sizeof(struct vring_desc));
    vr->used = (void *)(((ULONG_PTR)&vr->avail->ring[num] + pagesize-1)
                & ~((ULONG_PTR)pagesize - 1));
    vr->vring_last_used_ptr = &vring_last_used(vr);
}

static  unsigned vring_size(unsigned int num, unsigned long pagesize)
{
    return ((sizeof(struct vring_desc) * num + sizeof(u16) * (3 + num)
         + pagesize - 1) & ~((ULONG_PTR)pagesize - 1))
        + sizeof(u16) * 3 + sizeof(struct vring_used_elem) * num;
}

static __inline int vring_need_event(u16 event_idx, u16 new_idx, u16 old)
{
    return (u16)(new_idx - event_idx - 1) < (u16)(new_idx - old);
}

struct virtqueue *vring_new_virtqueue(unsigned int num,
                      VirtIODevice * pVirtIODevice,
                      void *pages,
                      void (*notify)(struct virtqueue *vq),
                      void *control,
                      unsigned int index,
                      BOOLEAN use_published_indices);

void* vring_detach_unused_buf(struct virtqueue *_vq);

unsigned int vring_control_block_size();

#endif /* _LINUX_VIRTIO_RING_H */
