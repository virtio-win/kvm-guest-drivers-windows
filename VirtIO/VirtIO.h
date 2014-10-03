#ifndef _LINUX_VIRTIO_H
#define _LINUX_VIRTIO_H
/* Everything a virtio driver needs to work with any particular virtio
 * implementation. */

/* Status byte for guest to report progress, and synchronize features. */
/* We have seen device and processed generic fields (VIRTIO_CONFIG_F_VIRTIO) */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE         1
/* We have found a driver for the device. */
#define VIRTIO_CONFIG_S_DRIVER              2
/* Driver has used its parts of the config, and is happy */
#define VIRTIO_CONFIG_S_DRIVER_OK           4
/* We've given up on this device. */
#define VIRTIO_CONFIG_S_FAILED              0x80
/* virtio library features bits */
#define VIRTIO_F_ANY_LAYOUT                 27
#define VIRTIO_F_INDIRECT                   28
#define VIRTIO_RING_F_EVENT_IDX             29


// if this number is not equal to desc size, queue creation fails
#define SIZE_OF_SINGLE_INDIRECT_DESC        16

#define virtio_device VirtIODevice
#define scatterlist VirtIOBufferDescriptor

typedef struct TypeVirtIODevice VirtIODevice;
struct VirtIOBufferDescriptor {
    PHYSICAL_ADDRESS physAddr;
    ULONG length;
};

/**
 * virtqueue - a queue to register buffers for sending or receiving.
 * @list: the chain of virtqueues for this device
 * @callback: the function to call when buffers are consumed (can be NULL).
 * @name: the name of this virtqueue (mainly for debugging)
 * @vdev: the virtio device this queue was created for.
 * @priv: a pointer for the virtqueue implementation to use.
 * @index: the zero-based ordinal number for this queue.
 * @num_free: number of elements we expect to be able to fit.
 *
 * A note on @num_free: with indirect buffers, each buffer needs one
 * element in the queue, otherwise a buffer will need one element per
 * sg element.
 */
struct virtqueue {
    // struct list_head list;
    // void (*callback)(struct virtqueue *vq);
    const char *name;
    virtio_device *vdev;
    unsigned int index;
    unsigned int num_free;
    void *priv;
};

int virtqueue_add_buf(struct virtqueue *vq,
                      struct scatterlist sg[],
                      unsigned int out_num,
                      unsigned int in_num,
                      void *data,
                      void *va_indirect,
                      ULONGLONG phys_indirect);

void virtqueue_kick(struct virtqueue *vq);

bool virtqueue_kick_prepare(struct virtqueue *vq);

void virtqueue_notify(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

void virtqueue_disable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb_delayed(struct virtqueue *vq);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);

unsigned int virtqueue_get_vring_size(struct virtqueue *vq);

BOOLEAN virtqueue_is_interrupt_enabled(struct virtqueue *_vq);

void virtqueue_shutdown(struct virtqueue *_vq);

#endif /* _LINUX_VIRTIO_H */
