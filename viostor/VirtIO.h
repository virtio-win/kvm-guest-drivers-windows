#ifndef _LINUX_VIRTIO_H
#define _LINUX_VIRTIO_H

/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
/**
 * virtqueue - a queue to register buffers for sending or receiving.
 * @callback: the function to call when buffers are consumed (can be NULL).
 *    If this returns false, callbacks are suppressed until vq_ops->restart
 *    is called.
 * @vdev: the virtio device this queue was created for.
 * @vq_ops: the operations for this virtqueue (see below).
 * @priv: a pointer for the virtqueue implementation to use.
 */
struct virtqueue
{
    bool (*callback)(struct virtqueue *vq);
    PVOID  DeviceExtension;
    struct virtqueue_ops *vq_ops;
    PVOID  priv;
};

struct VirtIOBufferDescriptor
{
	PHYSICAL_ADDRESS physAddr;
	unsigned long ulSize;
};

/**
 * virtqueue_ops - operations for virtqueue abstraction layer
 * @add_buf: expose buffer to other end
 *	vq: the struct virtqueue we're talking about.
 *	sg: the description of the buffer(s).
 *	out_num: the number of sg readable by other side
 *	in_num: the number of sg which are writable (after readable ones)
 *	data: the token identifying the buffer.
 *      Returns 0 or an error.
 * @kick: update after add_buf
 *	vq: the struct virtqueue
 *	After one or more add_buf calls, invoke this to kick the other side.
 * @get_buf: get the next used buffer
 *	vq: the struct virtqueue we're talking about.
 *	len: the length written into the buffer
 *	Returns NULL or the "data" token handed to add_buf.
 * @restart: restart callbacks after callback returned false.
 *	vq: the struct virtqueue we're talking about.
 *	This returns "false" (and doesn't re-enable) if there are pending
 *	buffers in the queue, to avoid a race.
 * @shutdown: "unadd" all buffers.
 *	vq: the struct virtqueue we're talking about.
 *	Remove everything from the queue.
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
        void *data);

    void (*kick)(struct virtqueue *vq);

    void (*kick_always)(struct virtqueue *vq);

    void *(*get_buf)(struct virtqueue *vq, unsigned int *len);

    bool (*restart)(struct virtqueue *vq);

    void (*shutdown)(struct virtqueue *vq);
};

#endif /* _LINUX_VIRTIO_H */

