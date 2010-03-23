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
/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "VirtIO.h"

/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT            1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE           2
#define VRING_DESC_F_INDIRECT        4

/* This means don't notify other side when buffer added. */
#define VRING_USED_F_NO_NOTIFY       1
/* This means don't interrupt guest when buffer consumed. */
#define VRING_AVAIL_F_NO_INTERRUPT   1

#define VIRTIO_RING_F_INDIRECT_DESC  28

#pragma warning(disable:4200)

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
};
#pragma pack (pop)

typedef struct _declspec(align(PAGE_SIZE)) vring_virtqueue
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

    /* Last used index we've seen. */
    u16 last_used_idx;

    /* How to notify other side. FIXME: commonalize hcalls! */
    void (*notify)(struct virtqueue *vq);

    /* Tokens for callbacks. */
    void *data[];
}vring_virtqueue, *pvring_virtqueue;

static
VOID
vring_init(struct vring *vr,
           unsigned int num,
           IN PVOID p,
           unsigned long pagesize)
{
    vr->num = num;
    vr->desc = p;
    vr->avail = (void *) ((u8 *)p + num*sizeof(struct vring_desc));
    vr->used = (void *)(((ULONG_PTR)&vr->avail->ring[num] + pagesize-1)
                        & ~((ULONG_PTR)pagesize - 1));
}

static  unsigned vring_size(unsigned int num, unsigned long pagesize)
{
    return ((sizeof(struct vring_desc) * num + sizeof(u16) * (2 + num)
        + pagesize - 1) & ~((ULONG_PTR)pagesize - 1))
        + sizeof(u16) * 2 + sizeof(struct vring_used_elem) * num;
}

struct
virtqueue*
vring_new_virtqueue(
    unsigned int num,
    IN PVOID DeviceExtension,
    IN PVOID pages,
    IN VOID  (*notify)(struct virtqueue *vq));

#endif /* _LINUX_VIRTIO_RING_H */
