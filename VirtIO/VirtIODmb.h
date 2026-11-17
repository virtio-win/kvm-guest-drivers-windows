/*
 * Device Memory Buffer (DMB) allocator and bounce helpers
 *
 * Copyright (C) 2026 Amazon.com, Inc. or its affiliates.
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _VIRTIO_DMB_H
#define _VIRTIO_DMB_H

/* Smallest DMB the guest accepts: the rings plus a few requests must fit. */
#define VIRTIO_DMB_MIN_SIZE (64 * 1024)
/* Largest part of a DMB the guest uses. A larger region is truncated, which
 * bounds the bitmap and the allocator scan (16384 pages), which can run in
 * an ISR. */
#define VIRTIO_DMB_MAX_SIZE (64ULL << 20)

/* Called by a DMB-aware driver after virtio_device_initialize() and before
 * virtio_set_features(), on every initialization: the driver keeps every
 * device address inside the DMB when the device requires it. Without this
 * call, a device that offers VIRTIO_F_DMB fails feature negotiation, because
 * the driver would hand it guest physical addresses it cannot reach. */
static __inline void virtio_dmb_opt_in(VirtIODevice *vdev)
{
    vdev->dmb.driver_capable = true;
}

/* Called by virtio_set_features() before the features are written: decides
 * whether VIRTIO_F_DMB is accepted and adjusts *features to suit (adds
 * VIRTIO_F_DMB and VIRTIO_F_ACCESS_PLATFORM, drops the packed ring and
 * indirect descriptors). Fails when the device offers the DMB and the driver
 * did not opt in, the region is unusable, or the outcome differs from an
 * earlier negotiation since virtio_device_initialize(). */
NTSTATUS virtio_dmb_negotiate(VirtIODevice *vdev, u64 *features);

/* Called by a DMB-aware driver whose mem_alloc_nonpaged_block cannot allocate
 * at feature negotiation, such as a StorPort miniport whose pool is set up
 * and reset in HwInitialize, after that negotiation. The bitmap is then
 * allocated at the first queue setup instead of when the device accepts the
 * features, and the driver allocates nothing from the DMB before its first
 * queue. Call it with virtio_dmb_opt_in(). */
static __inline void virtio_dmb_defer_bitmap(VirtIODevice *vdev)
{
    vdev->dmb.defer_bitmap = true;
}

/* True when the device offers VIRTIO_F_DMB, so it cannot be driven without
 * the DMB. */
bool virtio_dmb_is_required(VirtIODevice *vdev);

/* Called by virtio_set_features() once the device has accepted @features.
 * Allocates the bitmap unless the driver deferred it. */
NTSTATUS virtio_dmb_features_accepted(VirtIODevice *vdev, u64 features);

/* The DMB mode a driver sized itself for: whether the DMB is active, and its
 * size. The driver keeps it outside VirtIODevice, which every
 * virtio_device_initialize() clears, and zeroes it once, before the first
 * negotiation. */
typedef struct virtio_dmb_mode {
    bool fixed;
    bool active;
    u64 size;
} VirtIODmbMode;

/* Called by a DMB-aware driver after every feature negotiation. The first
 * call records the mode in @mode; a later call fails when the mode differs,
 * because the driver sized its transfers, pools or buffers from the first.
 * Every call fails when the device requires the DMB and it is not active.
 * On failure the device is marked FAILED. */
NTSTATUS virtio_dmb_check_mode(VirtIODevice *vdev, VirtIODmbMode *mode);

/* Called by the transport during device initialization. */

/* Record the region a shared memory capability describes, before the BAR is
 * mapped. Does not validate; virtio_dmb_map() does. */
void virtio_dmb_set_location(VirtIODevice *vdev, u8 bar, u64 bar_offset, u64 size);

/* Map and validate the DMB: the device must offer VIRTIO_F_DMB, the common
 * configuration must be mapped far enough to read the region ID and memory
 * type (vdev->dmb.cfg_reachable), the type must be coherent, the region
 * must start page aligned inside the BAR and be at
 * least VIRTIO_DMB_MIN_SIZE. The usable size is rounded down to whole pages.
 * On success vdev->dmb.present becomes true. */
void virtio_dmb_map(VirtIODevice *vdev);

/* Allocate the page bitmap through the driver's mem_alloc_nonpaged_block, so
 * it is driver-private guest RAM and never the DMB itself, which the device
 * can write. Page 0 is reserved, so no device offset handed out is ever 0.
 * Does nothing if the bitmap is already there or the DMB is not active, so
 * each call site may run more than once. Callable where the driver's
 * mem_alloc_nonpaged_block is. */
NTSTATUS virtio_dmb_attach(VirtIODevice *vdev);

/* Stop allocating and free the bitmap through mem_free_nonpaged_block.
 * virtio_device_shutdown() calls this. Afterwards virtio_dmb_alloc() fails
 * and virtio_dmb_free() does nothing, so the driver may still drop runs it
 * holds. The BAR mapping is owned by the caller and is not unmapped here. */
void virtio_dmb_unmap(VirtIODevice *vdev);

/* Bytes of bitmap storage virtio_dmb_attach() allocates (one bit per DMB
 * page, in LONG words), for a driver that must size its pool up front. 0 if
 * there is no usable DMB. */
size_t virtio_dmb_bitmap_bytes(VirtIODevice *vdev);

/* True when the device offers VIRTIO_F_DMB and its region checked out. Valid
 * before feature negotiation; device drivers use it to pick DMB-compatible
 * features. */
static __inline bool virtio_dmb_is_present(VirtIODevice *vdev)
{
    return vdev->dmb.present;
}

/* True once the device has accepted VIRTIO_F_DMB. */
static __inline bool virtio_dmb_is_active(VirtIODevice *vdev)
{
    return vdev->dmb.active;
}

/* Usable size of the region, in bytes (0 if there is no DMB). */
static __inline u64 virtio_dmb_size(VirtIODevice *vdev)
{
    return vdev->dmb.present ? vdev->dmb.size : 0;
}

/* The largest single bounce the caller should map, so several large requests
 * fit in the DMB at once: min(size / 8, 1 MiB), rounded down to whole
 * pages. */
size_t virtio_dmb_max_mapping(VirtIODevice *vdev);

/* Page/slot allocator. Allocates a run covering @size bytes, returns its
 * kernel virtual address and sets *offset to the device offset (a byte offset
 * into the DMB, never 0). Returns NULL and leaves *offset 0 on exhaustion.
 * Lockless: callable at any IRQL, concurrently with virtio_dmb_free(). Use it
 * for long-lived allocations (rings, buffers posted for the device's
 * lifetime), which may exceed virtio_dmb_max_mapping(). */
void *virtio_dmb_alloc(VirtIODevice *vdev, size_t size, u64 *offset);

/* Outcome of mapping a transient run. */
typedef enum virtio_dmb_map_result {
    VIRTIO_DMB_MAPPED,  /* the run is allocated */
    VIRTIO_DMB_FULL,    /* no free run now; a later free may make room */
    VIRTIO_DMB_TOO_BIG, /* above the mapping limit, or no DMB: never fits */
} virtio_dmb_map_result;

/* virtio_dmb_alloc() for a transient run, such as the bounce of one request:
 * fails with VIRTIO_DMB_TOO_BIG above virtio_dmb_max_mapping(), so that a
 * caller tells a full DMB from a run that never fits. */
virtio_dmb_map_result virtio_dmb_alloc_ex(VirtIODevice *vdev, size_t size, void **va, u64 *offset);

/* Free a run returned by virtio_dmb_alloc() or virtio_dmb_alloc_ex(). @size
 * must match the alloc. Callable at any IRQL. */
void virtio_dmb_free(VirtIODevice *vdev, void *va, size_t size);

/* True if [va, va + len) lies inside the mapped DMB. */
bool virtio_dmb_contains(VirtIODevice *vdev, const void *va, size_t len);

/* Translate a DMB-resident kernel VA to its device offset. Bounds-checks the
 * [va, va + len) range against the DMB. Returns false if it is not inside. */
bool virtio_dmb_va_to_offset(VirtIODevice *vdev, void *va, size_t len, u64 *offset);

/* Bounds-check a device-supplied offset and length against the DMB before the
 * driver trusts it. */
bool virtio_dmb_offset_ok(VirtIODevice *vdev, u64 offset, size_t len);

/* Copy @len bytes into the DMB at kernel VA @dmb_va from @src, for data the
 * device will read. Accounts the bytes for diagnostics. */
void virtio_dmb_copy_in(VirtIODevice *vdev, void *dmb_va, const void *src, size_t len);

/* Copy @len bytes out of the DMB at kernel VA @dmb_va into @dst, for data the
 * device has written. The copy is a snapshot: parse @dst, never @dmb_va.
 * Accounts the bytes for diagnostics. */
void virtio_dmb_copy_out(VirtIODevice *vdev, void *dst, const void *dmb_va, size_t len);

#endif /* _VIRTIO_DMB_H */
