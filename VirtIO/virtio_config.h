#ifndef _LINUX_VIRTIO_CONFIG_H
#define _LINUX_VIRTIO_CONFIG_H

#include "osdep.h"
#include "virtio.h"
#include "linux/virtio_byteorder.h"
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
* @bus_name: return the bus name associated with the device
*    vdev: the virtio_device
*      This returns a pointer to the bus name a la pci_name from which
*      the caller can then copy.
* @set_vq_affinity: set the affinity for a virtqueue.
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
    int (*find_vqs)(virtio_device *, unsigned nvqs,
                    struct virtqueue *vqs[],
                    vq_callback_t *callbacks[],
                    const char * const names[]);
    int (*find_vq)(virtio_device *, unsigned index,
                   struct virtqueue **vq, const char *name);
    void (*del_vqs)(virtio_device *);
    void (*del_vq)(struct virtqueue *);
    u64 (*get_features)(virtio_device *vdev);
    int (*finalize_features)(virtio_device *vdev);
    const char *(*bus_name)(virtio_device *vdev);
    int (*set_vq_affinity)(struct virtqueue *vq, int cpu);
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
    //if (__builtin_constant_p(fbit))
    //    BUILD_BUG_ON(fbit >= 64);
    //else
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

static inline
struct virtqueue *virtio_find_single_vq(virtio_device *vdev,
                                        vq_callback_t *c, const char *n)
{
    vq_callback_t *callbacks[1];
    const char *names[1];
    struct virtqueue *vq;
    int err;

    callbacks[0] = c;
    names[0] = n;

    err = vdev->config->find_vqs(vdev, 1, &vq, callbacks, names);
    if (err < 0)
        return (struct virtqueue *)ERR_PTR(err);
    return vq;
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

static inline
const char *virtio_bus_name(virtio_device *vdev)
{
    if (!vdev->config->bus_name)
        return "virtio";
    return vdev->config->bus_name(vdev);
}

/**
* virtqueue_set_affinity - setting affinity for a virtqueue
* @vq: the virtqueue
* @cpu: the cpu no.
*
* Pay attention the function are best-effort: the affinity hint may not be set
* due to config support, irq type and sharing.
*
*/
static inline
int virtqueue_set_affinity(struct virtqueue *vq, int cpu)
{
    virtio_device *vdev = vq->vdev;
    if (vdev->config->set_vq_affinity)
        return vdev->config->set_vq_affinity(vq, cpu);
    return 0;
}

static inline bool virtio_is_little_endian(virtio_device *vdev)
{
    return virtio_has_feature(vdev, VIRTIO_F_VERSION_1) ||
        virtio_legacy_is_little_endian();
}

/* Memory accessors */
#pragma warning (push)
#pragma warning (disable:4100) // unreferenced formal parameter vdev
static inline u16 virtio16_to_cpu(virtio_device *vdev, __virtio16 val)
{
    return __virtio16_to_cpu(virtio_is_little_endian(vdev), val);
}

static inline __virtio16 cpu_to_virtio16(virtio_device *vdev, u16 val)
{
    return __cpu_to_virtio16(virtio_is_little_endian(vdev), val);
}

static inline u32 virtio32_to_cpu(virtio_device *vdev, __virtio32 val)
{
    return __virtio32_to_cpu(virtio_is_little_endian(vdev), val);
}

static inline __virtio32 cpu_to_virtio32(virtio_device *vdev, u32 val)
{
    return __cpu_to_virtio32(virtio_is_little_endian(vdev), val);
}

static inline u64 virtio64_to_cpu(virtio_device *vdev, __virtio64 val)
{
    return __virtio64_to_cpu(virtio_is_little_endian(vdev), val);
}

static inline __virtio64 cpu_to_virtio64(virtio_device *vdev, u64 val)
{
    return __cpu_to_virtio64(virtio_is_little_endian(vdev), val);
}
#pragma warning (pop)

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
    return virtio16_to_cpu(vdev, (__force __virtio16)ret);
}

static inline void virtio_cwrite16(virtio_device *vdev,
                                   unsigned int offset, u16 val)
{
    val = (__force u16)cpu_to_virtio16(vdev, val);
    vdev->config->set(vdev, offset, &val, sizeof(val));
}

static inline u32 virtio_cread32(virtio_device *vdev,
                                 unsigned int offset)
{
    u32 ret;
    vdev->config->get(vdev, offset, &ret, sizeof(ret));
    return virtio32_to_cpu(vdev, (__force __virtio32)ret);
}

static inline void virtio_cwrite32(virtio_device *vdev,
                                   unsigned int offset, u32 val)
{
    val = (__force u32)cpu_to_virtio32(vdev, val);
    vdev->config->set(vdev, offset, &val, sizeof(val));
}

static inline u64 virtio_cread64(virtio_device *vdev,
                                 unsigned int offset)
{
    u64 ret;
    __virtio_cread_many(vdev, offset, &ret, 1, sizeof(ret));
    return virtio64_to_cpu(vdev, (__force __virtio64)ret);
}

static inline void virtio_cwrite64(virtio_device *vdev,
                                   unsigned int offset, u64 val)
{
    val = (__force u64)cpu_to_virtio64(vdev, val);
    vdev->config->set(vdev, offset, &val, sizeof(val));
}

/* Conditional config space accessors. */
#define virtio_cread_feature(vdev, fbit, structname, member, ptr)    \
    ({                                                               \
        int _r = 0;                                                  \
        if (!virtio_has_feature(vdev, fbit))                         \
            _r = -ENOENT;                                            \
                else                                                 \
            virtio_cread((vdev), structname, member, ptr);           \
        _r;                                                          \
    })

#endif /* _LINUX_VIRTIO_CONFIG_H */
