#ifndef _LINUX_VIRTIO_CONFIG_H
#define _LINUX_VIRTIO_CONFIG_H

#include "osdep.h"
#include "virtio.h"

/**
* virtio_config_ops - operations for configuring a virtio device
* @get: read the value of a configuration field
*    vdev: the virtio_device
*    offset: the offset of the configuration field
*    buf: the buffer to write the field value into.
*    len: the length of the buffer
* @set: write the value of a configuration field
*    vdev: the virtio_device
*    offset: the offset of the configuration field
*    buf: the buffer to read the field value from.
*    len: the length of the buffer
* @generation: config generation counter
*    vdev: the virtio_device
*    Returns the config generation counter
* @get_status: read the status byte
*    vdev: the virtio_device
*    Returns the status byte
* @set_status: write the status byte
*    vdev: the virtio_device
*    status: the new status byte
* @reset: reset the device
*    vdev: the virtio device
*    After this, status and feature negotiation must be done again
*    Device must not be reset from its vq/config callbacks, or in
*    parallel with being added/removed.
* @find_vqs: find virtqueues and instantiate them.
*    vdev: the virtio_device
*    nvqs: the number of virtqueues to find
*    vqs: on success, includes new virtqueues
*    callbacks: array of callbacks, for each virtqueue
*        include a NULL entry for vqs that do not need a callback
*    names: array of virtqueue names (mainly for debugging)
*        include a NULL entry for vqs unused by driver
*    Returns 0 on success or error status
* @get_features: get the array of feature bits for this device.
*    vdev: the virtio_device
*    Returns the first 32 feature bits (all we currently need).
* @finalize_features: confirm what device features we'll be using.
*    vdev: the virtio_device
*    This gives the final feature bits for the device: it can change
*    the dev->feature bits if it wants.
*    Returns 0 on success or error status
*/
typedef void vq_callback_t(struct virtqueue *);
struct virtio_config_ops {
    void (*get)(VirtIODevice *vdev, unsigned offset,
                void *buf, unsigned len);
    void (*set)(VirtIODevice *vdev, unsigned offset,
                const void *buf, unsigned len);
    u32 (*generation)(VirtIODevice *vdev);
    u8 (*get_status)(VirtIODevice *vdev);
    void (*set_status)(VirtIODevice *vdev, u8 status);
    void (*reset)(VirtIODevice *vdev);
    NTSTATUS (*query_vq_alloc)(VirtIODevice *vdev,
                unsigned index, unsigned short *pNumEntries,
                unsigned long *pAllocationSize,
                unsigned long *pHeapSize);
    NTSTATUS (*setup_vq)(struct virtqueue **queue,
                VirtIODevice *vdev,
                VirtIOQueueInfo *info,
                unsigned idx,
                const char *name,
                u16 msix_vec);
    void (*del_vq)(VirtIOQueueInfo *info);
    u16 (*config_vector)(VirtIODevice *vdev, u16 vector);
    NTSTATUS (*find_vqs)(VirtIODevice *, unsigned nvqs,
                struct virtqueue *vqs[],
                const char * const names[]);
    NTSTATUS (*find_vq)(VirtIODevice *, unsigned index,
                struct virtqueue **vq, const char *name);
    u64 (*get_features)(VirtIODevice *vdev);
    NTSTATUS (*finalize_features)(VirtIODevice *vdev);
    u16 (*set_msi_vector)(struct virtqueue *vq, u16 vector);
};

#endif /* _LINUX_VIRTIO_CONFIG_H */
