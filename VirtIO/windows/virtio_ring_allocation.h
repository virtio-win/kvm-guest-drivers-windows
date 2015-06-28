#ifndef _VIRTIO_RING_ALLOCATION_H
#define _VIRTIO_RING_ALLOCATION_H

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

#endif /* _VIRTIO_RING_ALLOCATION_H */
