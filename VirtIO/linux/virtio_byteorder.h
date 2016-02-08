#ifndef _LINUX_VIRTIO_BYTEORDER_H
#define _LINUX_VIRTIO_BYTEORDER_H
#include <linux/types.h>
#include <linux/virtio_types.h>

static inline bool virtio_legacy_is_little_endian(void)
{
#ifdef __LITTLE_ENDIAN
	return true;
#else
	return false;
#endif
}

/* Windows runs on little endian only */
#define __virtio16_to_cpu(little_endian,val) (val)
#define __cpu_to_virtio16(little_endian,val) (val)
#define __virtio32_to_cpu(little_endian,val) (val)
#define __cpu_to_virtio32(little_endian,val) (val)
#define __virtio64_to_cpu(little_endian,val) (val)
#define __cpu_to_virtio64(little_endian,val) (val)

#endif /* _LINUX_VIRTIO_BYTEORDER */
