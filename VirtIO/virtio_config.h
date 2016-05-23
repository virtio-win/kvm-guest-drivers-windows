#ifndef _LINUX_VIRTIO_CONFIG_H
#define _LINUX_VIRTIO_CONFIG_H

#include "osdep.h"
#include "virtio.h"
#include "linux/virtio_config.h"

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
* @del_vqs: free virtqueues found by find_vqs().
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
    void (*get)(virtio_device *vdev, unsigned offset,
                void *buf, unsigned len);
    void (*set)(virtio_device *vdev, unsigned offset,
                const void *buf, unsigned len);
    u32 (*generation)(virtio_device *vdev);
    u8 (*get_status)(virtio_device *vdev);
    void (*set_status)(virtio_device *vdev, u8 status);
    void (*reset)(virtio_device *vdev);
    NTSTATUS (*find_vqs)(virtio_device *, unsigned nvqs,
                         struct virtqueue *vqs[],
                         const char * const names[]);
    NTSTATUS (*find_vq)(virtio_device *, unsigned index,
                   struct virtqueue **vq, const char *name);
    void (*del_vqs)(virtio_device *);
    void (*del_vq)(struct virtqueue *);
    u64 (*get_features)(virtio_device *vdev);
    NTSTATUS (*finalize_features)(virtio_device *vdev);
    u16 (*set_msi_vector)(struct virtqueue *vq, u16 vector);
};

/**
* __virtio_test_bit - helper to test feature bits. For use by transports.
*                     Devices should normally use virtio_has_feature,
*                     which includes more checks.
* @vdev: the device
* @fbit: the feature bit
*/
static inline bool __virtio_test_bit(const virtio_device *vdev,
                                     unsigned int fbit)
{
    /* Did you forget to fix assumptions on max features? */
    BUG_ON(fbit >= 64);

    return !!(vdev->features & BIT_ULL(fbit));
}

/**
* __virtio_set_bit - helper to set feature bits. For use by transports.
* @vdev: the device
* @fbit: the feature bit
*/
static inline void __virtio_set_bit(virtio_device *vdev,
                                    unsigned int fbit)
{
    /* Did you forget to fix assumptions on max features? */
    BUG_ON(fbit >= 64);

    vdev->features |= BIT_ULL(fbit);
}

/**
* __virtio_clear_bit - helper to clear feature bits. For use by transports.
* @vdev: the device
* @fbit: the feature bit
*/
static inline void __virtio_clear_bit(virtio_device *vdev,
                                      unsigned int fbit)
{
    /* Did you forget to fix assumptions on max features? */
    BUG_ON(fbit >= 64);

    vdev->features &= ~BIT_ULL(fbit);
}

/**
* virtio_has_feature - helper to determine if this device has this feature.
* @vdev: the device
* @fbit: the feature bit
*/
static inline bool virtio_has_feature(const virtio_device *vdev,
                                      unsigned int fbit)
{
    return __virtio_test_bit(vdev, fbit);
}

/**
* virtio_device_ready - enable vq use in probe function
* @vdev: the device
*
* Driver must call this to use vqs in the probe function.
*
* Note: vqs are enabled automatically after probe returns.
*/
static inline
void virtio_device_ready(virtio_device *dev)
{
    unsigned status = dev->config->get_status(dev);

    BUG_ON(status & VIRTIO_CONFIG_S_DRIVER_OK);
    dev->config->set_status(dev, (u8)(status | VIRTIO_CONFIG_S_DRIVER_OK));
}

static inline
u64 virtio_get_features(virtio_device *dev)
{
    dev->features = dev->config->get_features(dev);
    return dev->features;
}

/* Config space accessors. */
#define virtio_cread(vdev, structname, member, ptr)         \
    do {                                                    \
        /* Must match the member's type, and be integer */  \
        if (!typecheck(typeof((((structname*)0)->member)), *(ptr))) \
            (*ptr) = 1;                                     \
                                                            \
        switch (sizeof(*ptr)) {                             \
        case 1:                                             \
            *(ptr) = virtio_cread8(vdev,                    \
                           offsetof(structname, member));   \
            break;                                          \
        case 2:                                             \
            *(ptr) = virtio_cread16(vdev,                   \
                        offsetof(structname, member));      \
            break;                                          \
        case 4:                                             \
            *(ptr) = virtio_cread32(vdev,                   \
                        offsetof(structname, member));      \
            break;                                          \
        case 8:                                             \
            *(ptr) = virtio_cread64(vdev,                   \
                        offsetof(structname, member));      \
            break;                                          \
        default:                                            \
            BUG();                                          \
        }                                                   \
    } while(0)

/* Config space accessors. */
#define virtio_cwrite(vdev, structname, member, ptr)        \
    do {                                                    \
        /* Must match the member's type, and be integer */  \
        if (!typecheck(typeof((((structname*)0)->member)), *(ptr))) \
            BUG_ON((*ptr) == 1);                            \
                                                            \
        switch (sizeof(*ptr)) {                             \
        case 1:                                             \
            virtio_cwrite8(vdev,                            \
                       offsetof(structname, member),        \
                       *(ptr));                             \
            break;                                          \
        case 2:                                             \
            virtio_cwrite16(vdev,                           \
                    offsetof(structname, member),           \
                    *(ptr));                                \
            break;                                          \
        case 4:                                             \
            virtio_cwrite32(vdev,                           \
                    offsetof(structname, member),           \
                    *(ptr));                                \
            break;                                          \
        case 8:                                             \
            virtio_cwrite64(vdev,                           \
                    offsetof(structname, member),           \
                    *(ptr));                                \
            break;                                          \
        default:                                            \
            BUG();                                          \
        }                                                   \
    } while(0)

/* Read @count fields, @bytes each. */
static inline void __virtio_cread_many(virtio_device *vdev,
                                       unsigned int offset,
                                       void *buf, size_t count, size_t bytes)
{
    u32 old, gen = vdev->config->generation ?
        vdev->config->generation(vdev) : 0;
    size_t i;

    do {
        old = gen;

        for (i = 0; i < count; i++)
            vdev->config->get(vdev, (unsigned)(offset + bytes * i),
            (char *)buf + i * bytes, (unsigned)bytes);

        gen = vdev->config->generation ?
            vdev->config->generation(vdev) : 0;
    } while (gen != old);
}

static inline void virtio_cread_bytes(virtio_device *vdev,
                                      unsigned int offset,
                                      void *buf, size_t len)
{
    __virtio_cread_many(vdev, offset, buf, len, 1);
}

static inline u8 virtio_cread8(virtio_device *vdev, unsigned int offset)
{
    u8 ret;
    vdev->config->get(vdev, offset, &ret, sizeof(ret));
    return ret;
}

static inline void virtio_cwrite8(virtio_device *vdev,
                                  unsigned int offset, u8 val)
{
    vdev->config->set(vdev, offset, &val, sizeof(val));
}

static inline u16 virtio_cread16(virtio_device *vdev,
                                 unsigned int offset)
{
    u16 ret;
    vdev->config->get(vdev, offset, &ret, sizeof(ret));
    return ret;
}

static inline void virtio_cwrite16(virtio_device *vdev,
                                   unsigned int offset, u16 val)
{
    vdev->config->set(vdev, offset, &val, sizeof(val));
}

static inline u32 virtio_cread32(virtio_device *vdev,
                                 unsigned int offset)
{
    u32 ret;
    vdev->config->get(vdev, offset, &ret, sizeof(ret));
    return ret;
}

static inline void virtio_cwrite32(virtio_device *vdev,
                                   unsigned int offset, u32 val)
{
    vdev->config->set(vdev, offset, &val, sizeof(val));
}

static inline u64 virtio_cread64(virtio_device *vdev,
                                 unsigned int offset)
{
    u64 ret;
    __virtio_cread_many(vdev, offset, &ret, 1, sizeof(ret));
    return ret;
}

static inline void virtio_cwrite64(virtio_device *vdev,
                                   unsigned int offset, u64 val)
{
    vdev->config->set(vdev, offset, &val, sizeof(val));
}

#endif /* _LINUX_VIRTIO_CONFIG_H */
