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
#define VIRTIO_F_INDIRECT                   28
#define VIRTIO_F_PUBLISH_INDICES            29


// if this number is not equal to desc size, queue creation fails
#define SIZE_OF_SINGLE_INDIRECT_DESC        16

/**
 * virtqueue - a queue to register buffers for sending or receiving.
 * @vdev: the virtio device this queue was created for.
 * @vq_ops: the operations for this virtqueue (see below).
 * @ulIndex: queue number defined by the device.
 */
typedef struct virtqueue
{
    VirtIODevice *vdev;
    struct virtqueue_ops *vq_ops;
    unsigned long ulIndex;
    unsigned long ulReserved;
};


typedef struct VirtIOBufferDescriptor
{
    PHYSICAL_ADDRESS physAddr;
    unsigned long ulSize;
};

/**
 * virtqueue_ops - operations for virtqueue abstraction layer
 * @add_buf: expose buffer to other end
 *  vq: the struct virtqueue we're talking about.
 *  sg: the description of the buffer(s).
 *  out_num: the number of sg readable by other side
 *  in_num: the number of sg which are writable (after readable ones)
 *  data: the token identifying the buffer.
 *      Returns 0 or an error.
 * @kick: update after add_buf
 *  vq: the struct virtqueue
 *  After one or more add_buf calls, invoke this to kick the other side.
 * @get_buf: get the next used buffer
 *  vq: the struct virtqueue we're talking about.
 *  len: the length written into the buffer
 *  Returns NULL or the "data" token handed to add_buf.
 * @restart: restart callbacks after callback returned false.
 *  vq: the struct virtqueue we're talking about.
 *  This returns "false" (and doesn't re-enable) if there are pending
 *  buffers in the queue, to avoid a race.
 * @shutdown: "unadd" all buffers.
 *  vq: the struct virtqueue we're talking about.
 *  Remove everything from the queue.
 *
 * Locking rules are straightforward: the driver is responsible for
 * locking.  No two operations may be invoked simultaneously.
 *
 * All operations can be called in any context.
 */
struct virtqueue_ops {
    int (*add_buf)(struct virtqueue *vq,
               struct VirtIOBufferDescriptor sg[],
               unsigned int out_num,
               unsigned int in_num,
               void *data,
               void *va_indirect,
               ULONGLONG phys_indirect);

    void (*kick)(struct virtqueue *vq);

    void (*kick_always)(struct virtqueue *vq);

    void *(*get_buf)(struct virtqueue *vq, unsigned int *len);

    bool (*restart)(struct virtqueue *vq);

    void (*shutdown)(struct virtqueue *vq);

    void (*enable_interrupt)(struct virtqueue *vq);

    void (*disable_interrupt)(struct virtqueue *vq);

    BOOLEAN (*is_interrupt_enabled)(struct virtqueue *vq);

    void (*delay_interrupt)(struct virtqueue *vq);
};

#endif /* _LINUX_VIRTIO_H */
