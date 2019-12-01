#ifndef _LINUX_VIRTIO_H
#define _LINUX_VIRTIO_H

#include "virtio_ring.h"

#define scatterlist VirtIOBufferDescriptor

struct VirtIOBufferDescriptor {
    PHYSICAL_ADDRESS physAddr;
    ULONG length;
};

/* Represents one virtqueue; only data pointed to by the vring structure is exposed to the host */
struct virtqueue {
    VirtIODevice *vdev;
    unsigned int index;
    void         (*notification_cb)(struct virtqueue *vq);
    void         *notification_addr;
    void         *avail_va;
    void         *used_va;
};

int virtqueue_add_buf(struct virtqueue *vq,
                      struct scatterlist sg[],
                      unsigned int out_num,
                      unsigned int in_num,
                      void *opaque,
                      void *va_indirect,
                      ULONGLONG phys_indirect);

void virtqueue_kick(struct virtqueue *vq);

bool virtqueue_kick_prepare(struct virtqueue *vq);

void virtqueue_kick_always(struct virtqueue *vq);

void virtqueue_notify(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

void virtqueue_disable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb_delayed(struct virtqueue *vq);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);

BOOLEAN virtqueue_is_interrupt_enabled(struct virtqueue *_vq);

BOOLEAN virtqueue_has_buf(struct virtqueue *_vq);

void virtqueue_shutdown(struct virtqueue *_vq);

#endif /* _LINUX_VIRTIO_H */
