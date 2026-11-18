/*
 * Device Memory Buffer (DMB) allocator and bounce helpers
 *
 * A device that offers VIRTIO_F_DMB exposes a Device Memory Buffer: a shared
 * memory region, owned by the device, that both the guest and the device
 * backend can read and write. The device reports the region's shared memory
 * id in dmb_shm_id. Once the driver accepts VIRTIO_F_DMB, every address it
 * hands the device is a bus address into that region, and the device
 * accesses no other guest memory, so a guest can keep the rest of its RAM
 * private from the device backend.
 *
 * This module owns the DMB: it validates and maps the region, hands out
 * page-granular runs (offset 0 is never handed out), translates
 * DMB-resident addresses to offsets, and bounces driver buffers in and out.
 *
 * Copyright (C) 2026 Amazon.com, Inc. or its affiliates.
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
#include "osdep.h"
#define VIRTIO_PCI_NO_LEGACY
#include "virtio_pci.h"
#include "virtio.h"
#include "kdebugprint.h"
#include "virtio_pci_common.h"
#include "VirtIODmb.h"
#include <stddef.h>

#define DMB_PAGE_SIZE         PAGE_SIZE
#define DMB_PAGE_SHIFT        PAGE_SHIFT
#define DMB_PAGE_MASK         ((u64)PAGE_SIZE - 1)
#define DMB_SIZE_TO_PAGES(sz) (((size_t)(sz) + DMB_PAGE_SIZE - 1) >> DMB_PAGE_SHIFT)
#define DMB_WORD_BITS         32

/*
 * The page bitmap is lockless: a page is taken with an interlocked
 * bit-test-and-set and given back with an interlocked bit-test-and-reset. An
 * allocation scans for a free run without synchronization, then claims its
 * pages one by one and gives the claimed ones back if another CPU took one
 * first. No lock is held, so allocation and free are safe at any IRQL,
 * including from an ISR that interrupts a CPU in the middle of an allocation.
 */
static __inline bool dmb_page_used(VirtIODmb *dmb, size_t page)
{
    return (dmb->bitmap[page / DMB_WORD_BITS] & (LONG)(1UL << (page % DMB_WORD_BITS))) != 0;
}

static __inline bool dmb_take_page(VirtIODmb *dmb, size_t page)
{
    return !InterlockedBitTestAndSet(&dmb->bitmap[page / DMB_WORD_BITS],
                                     (LONG)(page % DMB_WORD_BITS));
}

static __inline bool dmb_put_page(VirtIODmb *dmb, size_t page)
{
    return InterlockedBitTestAndReset(&dmb->bitmap[page / DMB_WORD_BITS],
                                      (LONG)(page % DMB_WORD_BITS)) != 0;
}

/* Claims [first, first + pages). On a collision with another CPU, gives back
 * the pages already claimed and returns false. */
static bool dmb_claim_run(VirtIODmb *dmb, size_t first, size_t pages)
{
    size_t page;

    for (page = first; page < first + pages; page++) {
        if (!dmb_take_page(dmb, page)) {
            while (page-- > first) {
                dmb_put_page(dmb, page);
            }
            return false;
        }
    }
    return true;
}

void virtio_dmb_set_location(VirtIODevice *vdev, u8 bar, u64 bar_offset, u64 size)
{
    vdev->dmb.bar = bar;
    vdev->dmb.bar_offset = bar_offset;
    vdev->dmb.size = size;
}

void virtio_dmb_map(VirtIODevice *vdev)
{
    VirtIODmb *dmb = &vdev->dmb;
    u64 features;
    u16 mem_type;
    size_t bar_len;
    u64 usable;
    void *base;

    dmb->present = false;
    if (dmb->size == 0) {
        return;
    }

    /* Only touch the DMB if the device actually offers the feature. */
    features = vdev->device->get_features(vdev);
    if (!virtio_is_feature_enabled(features, VIRTIO_F_DMB)) {
        return;
    }

    /* The device serves the memory type behind the common configuration.
     * Without it the region cannot be validated, so it is declined rather
     * than assumed. */
    if (!dmb->cfg_reachable) {
        DPrintf(0, "virtio DMB: common configuration too short for the DMB fields\n");
        return;
    }
    mem_type = ioread16(vdev, (volatile u8 *)vdev->common + VIRTIO_PCI_COMMON_CFG_DMB_MEM_TYPE);
    if (mem_type != VIRTIO_DMB_MEM_TYPE_COHERENT) {
        DPrintf(0, "virtio DMB: unsupported memory type %u\n", mem_type);
        return;
    }

    /* The region must lie inside the BAR and start on a page boundary. Its
     * usable part is rounded down to whole pages, so no page handed out can
     * run past the region. */
    bar_len = pci_get_resource_len(vdev, dmb->bar);
    usable = dmb->size & ~DMB_PAGE_MASK;
    if (usable > VIRTIO_DMB_MAX_SIZE) {
        usable = VIRTIO_DMB_MAX_SIZE;
    }
    if (bar_len == 0 || dmb->bar_offset >= bar_len || dmb->size > bar_len - dmb->bar_offset ||
        (dmb->bar_offset & DMB_PAGE_MASK) != 0 || usable < VIRTIO_DMB_MIN_SIZE) {
        DPrintf(0, "virtio DMB: region rejected (bar %u offset 0x%llx size 0x%llx barlen 0x%Ix)\n",
                dmb->bar, dmb->bar_offset, dmb->size, bar_len);
        return;
    }

    base = pci_map_address_range(vdev, dmb->bar, (size_t)dmb->bar_offset, (size_t)usable);
    if (base == NULL || ((ULONG_PTR)base & DMB_PAGE_MASK) != 0) {
        DPrintf(0, "virtio DMB: failed to map bar %u offset 0x%llx\n", dmb->bar, dmb->bar_offset);
        return;
    }

    dmb->base = (u8 *)base;
    dmb->size = usable;
    dmb->num_pages = (size_t)(usable >> DMB_PAGE_SHIFT);
    dmb->bitmap = NULL;
    dmb->hint = 1;
    dmb->present = true;

    DPrintf(0, "virtio DMB: %llu-byte region at bar %u offset 0x%llx (%Iu pages)\n", dmb->size,
            dmb->bar, dmb->bar_offset, dmb->num_pages);
}

bool virtio_dmb_is_required(VirtIODevice *vdev)
{
    return virtio_is_feature_enabled(vdev->device->get_features(vdev), VIRTIO_F_DMB);
}

NTSTATUS virtio_dmb_negotiate(VirtIODevice *vdev, u64 *features)
{
    VirtIODmb *dmb = &vdev->dmb;
    u64 device_features = vdev->device->get_features(vdev);

    if (!virtio_is_feature_enabled(device_features, VIRTIO_F_DMB)) {
        virtio_feature_disable(*features, VIRTIO_F_DMB);
        if (dmb->negotiated && dmb->active) {
            DPrintf(0, "virtio DMB: device no longer offers VIRTIO_F_DMB\n");
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        return STATUS_SUCCESS;
    }

    if (!dmb->driver_capable) {
        DPrintf(0, "virtio DMB: the device requires the Device Memory Buffer (VIRTIO_F_DMB); "
                   "this driver is not DMB-aware\n");
        return STATUS_NOT_SUPPORTED;
    }
    if (!virtio_is_feature_enabled(*features, VIRTIO_F_VERSION_1)) {
        DPrintf(0, "virtio DMB: VIRTIO_F_DMB needs VIRTIO_F_VERSION_1\n");
        return STATUS_NOT_SUPPORTED;
    }
    if (!dmb->present) {
        DPrintf(0, "virtio DMB: the device offers VIRTIO_F_DMB but its region is unusable\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (dmb->negotiated && !dmb->active) {
        DPrintf(0, "virtio DMB: device offers VIRTIO_F_DMB after a negotiation without it\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    virtio_feature_enable(*features, VIRTIO_F_DMB);
    if (virtio_is_feature_enabled(device_features, VIRTIO_F_ACCESS_PLATFORM)) {
        virtio_feature_enable(*features, VIRTIO_F_ACCESS_PLATFORM);
    }
    /* The packed ring and indirect tables would put driver bookkeeping or
     * guest addresses where the device can reach them. */
    virtio_feature_disable(*features, VIRTIO_F_RING_PACKED);
    virtio_feature_disable(*features, VIRTIO_RING_F_INDIRECT_DESC);
    return STATUS_SUCCESS;
}

NTSTATUS virtio_dmb_features_accepted(VirtIODevice *vdev, u64 features)
{
    vdev->dmb.active = vdev->dmb.present && virtio_is_feature_enabled(features, VIRTIO_F_DMB);
    vdev->dmb.negotiated = true;
    if (!vdev->dmb.active) {
        return STATUS_SUCCESS;
    }
    DPrintf(0, "virtio DMB: VIRTIO_F_DMB accepted, features 0x%llx\n", features);
    return vdev->dmb.defer_bitmap ? STATUS_SUCCESS : virtio_dmb_attach(vdev);
}

NTSTATUS virtio_dmb_check_mode(VirtIODevice *vdev, VirtIODmbMode *mode)
{
    bool active = virtio_dmb_is_active(vdev);
    u64 size = active ? vdev->dmb.size : 0;

    if (!active && virtio_dmb_is_required(vdev)) {
        DPrintf(0, "virtio DMB: the device requires a DMB that is not active\n");
    } else if (!mode->fixed) {
        mode->fixed = true;
        mode->active = active;
        mode->size = size;
        return STATUS_SUCCESS;
    } else if (mode->active == active && mode->size == size) {
        return STATUS_SUCCESS;
    } else {
        DPrintf(0, "virtio DMB: mode changed: active %u -> %u, %llu -> %llu bytes\n", mode->active,
                active, mode->size, size);
    }
    virtio_add_status(vdev, VIRTIO_CONFIG_S_FAILED);
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS virtio_dmb_attach(VirtIODevice *vdev)
{
    VirtIODmb *dmb = &vdev->dmb;
    size_t bytes = virtio_dmb_bitmap_bytes(vdev);
    volatile LONG *bitmap;

    if (!dmb->active || dmb->bitmap != NULL) {
        return STATUS_SUCCESS;
    }
    bitmap = (volatile LONG *)mem_alloc_nonpaged_block(vdev, bytes);
    if (bitmap == NULL) {
        DPrintf(0, "virtio DMB: cannot allocate the %Iu-byte bitmap\n", bytes);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ASSERT(((ULONG_PTR)bitmap & (sizeof(LONG) - 1)) == 0);

    RtlZeroMemory((void *)bitmap, bytes);
    /* Reserve page 0 so that no device offset handed out is ever 0. */
    bitmap[0] = 1;
    dmb->hint = 1;
    dmb->bitmap = bitmap;
    return STATUS_SUCCESS;
}

void virtio_dmb_unmap(VirtIODevice *vdev)
{
    VirtIODmb *dmb = &vdev->dmb;
    volatile LONG *bitmap = dmb->bitmap;

    /* base and size stay, so that virtio_dmb_contains() still recognizes a
     * run freed after this call and the caller does not hand it to a guest
     * RAM free routine. Such a free does nothing. */
    dmb->active = false;
    dmb->bitmap = NULL;
    dmb->hint = 0;
    if (bitmap != NULL) {
        mem_free_nonpaged_block(vdev, (void *)bitmap);
    }
}

size_t virtio_dmb_bitmap_bytes(VirtIODevice *vdev)
{
    if (!vdev->dmb.present) {
        return 0;
    }
    return ((vdev->dmb.num_pages + DMB_WORD_BITS - 1) / DMB_WORD_BITS) * sizeof(LONG);
}

size_t virtio_dmb_max_mapping(VirtIODevice *vdev)
{
    u64 eighth;

    if (!vdev->dmb.present) {
        return 0;
    }
    eighth = vdev->dmb.size / 8;
    if (eighth > (1ULL << 20)) {
        eighth = (1ULL << 20);
    }
    eighth &= ~DMB_PAGE_MASK;
    return (size_t)max(eighth, (u64)DMB_PAGE_SIZE);
}

void *virtio_dmb_alloc(VirtIODevice *vdev, size_t size, u64 *offset)
{
    VirtIODmb *dmb = &vdev->dmb;
    size_t pages = DMB_SIZE_TO_PAGES(size);
    size_t start, pass, page, run;

    *offset = 0;
    if (!dmb->active || dmb->bitmap == NULL || size == 0 || (u64)size >= dmb->size ||
        pages >= dmb->num_pages) {
        return NULL;
    }

    start = dmb->hint;
    if (start == 0 || start >= dmb->num_pages) {
        start = 1;
    }

    /* First fit from the hint to the end, then from page 1. */
    for (pass = 0; pass < 2; pass++) {
        run = 0;
        for (page = (pass == 0) ? start : 1; page < dmb->num_pages; page++) {
            size_t first;

            if (dmb_page_used(dmb, page)) {
                run = 0;
                continue;
            }
            if (++run < pages) {
                continue;
            }
            first = page + 1 - pages;
            if (!dmb_claim_run(dmb, first, pages)) {
                /* Another CPU took a page of this run; keep scanning. */
                run = 0;
                continue;
            }
            dmb->hint = (page + 1 < dmb->num_pages) ? page + 1 : 1;
            *offset = (u64)first << DMB_PAGE_SHIFT;
            return dmb->base + *offset;
        }
    }
    return NULL;
}

virtio_dmb_map_result virtio_dmb_alloc_ex(VirtIODevice *vdev, size_t size, void **va, u64 *offset)
{
    *va = NULL;
    *offset = 0;
    if (!vdev->dmb.active || vdev->dmb.bitmap == NULL || size == 0 ||
        size > virtio_dmb_max_mapping(vdev)) {
        return VIRTIO_DMB_TOO_BIG;
    }
    *va = virtio_dmb_alloc(vdev, size, offset);
    return (*va != NULL) ? VIRTIO_DMB_MAPPED : VIRTIO_DMB_FULL;
}

void virtio_dmb_free(VirtIODevice *vdev, void *va, size_t size)
{
    VirtIODmb *dmb = &vdev->dmb;
    size_t pages = DMB_SIZE_TO_PAGES(size);
    size_t first, page;
    u64 start;
    bool double_free = false;

    if (!dmb->present || dmb->bitmap == NULL || va == NULL || size == 0 || (u64)size >= dmb->size) {
        return;
    }
    if (!virtio_dmb_contains(vdev, va, pages << DMB_PAGE_SHIFT)) {
        DPrintf(0, "virtio DMB: free of %p (+%Iu) outside the region\n", va, size);
        ASSERT(!"virtio DMB: free outside the region");
        return;
    }
    start = (u64)((u8 *)va - dmb->base);
    if ((start & DMB_PAGE_MASK) != 0 || start == 0) {
        DPrintf(0, "virtio DMB: free of unaligned or reserved offset 0x%llx\n", start);
        ASSERT(!"virtio DMB: bad free");
        return;
    }

    first = (size_t)(start >> DMB_PAGE_SHIFT);
    for (page = first; page < first + pages; page++) {
        if (!dmb_put_page(dmb, page)) {
            double_free = true;
        }
    }
    if (double_free) {
        DPrintf(0, "virtio DMB: double free at offset 0x%llx (+%Iu)\n", start, size);
        ASSERT(!"virtio DMB: double free");
    }
}

bool virtio_dmb_contains(VirtIODevice *vdev, const void *va, size_t len)
{
    VirtIODmb *dmb = &vdev->dmb;
    u64 start;

    if (!dmb->present || (const u8 *)va < dmb->base) {
        return false;
    }
    start = (u64)((const u8 *)va - dmb->base);
    return start < dmb->size && (u64)len <= dmb->size - start;
}

bool virtio_dmb_va_to_offset(VirtIODevice *vdev, void *va, size_t len, u64 *offset)
{
    if (!virtio_dmb_contains(vdev, va, len)) {
        return false;
    }
    *offset = (u64)((u8 *)va - vdev->dmb.base);
    return true;
}

bool virtio_dmb_offset_ok(VirtIODevice *vdev, u64 offset, size_t len)
{
    VirtIODmb *dmb = &vdev->dmb;

    if (!dmb->present || offset == 0 || offset >= dmb->size) {
        return false;
    }
    return (u64)len <= dmb->size - offset;
}

void virtio_dmb_copy_in(VirtIODevice *vdev, void *dmb_va, const void *src, size_t len)
{
    RtlCopyMemory(dmb_va, src, len);
    InterlockedExchangeAdd64(&vdev->dmb.bytes_in, (LONG64)len);
}

void virtio_dmb_copy_out(VirtIODevice *vdev, void *dst, const void *dmb_va, size_t len)
{
    RtlCopyMemory(dst, dmb_va, len);
    InterlockedExchangeAdd64(&vdev->dmb.bytes_out, (LONG64)len);
}

/* A request may add one page of headers and status to the mapping limit. */
static size_t dmb_req_limit(VirtIODevice *vdev)
{
    return virtio_dmb_max_mapping(vdev) + DMB_PAGE_SIZE;
}

static unsigned dmb_descs(ULONG len, ULONG split)
{
    if (len == 0) {
        return 0;
    }
    if (split == 0) {
        return 1;
    }
    /* Written so that a split near 2^32 cannot wrap. */
    return (unsigned)(len / split + ((len % split) != 0));
}

virtio_dmb_map_result virtio_dmb_req_map(VirtIODevice *vdev, VirtIODmbReq *req,
                                         const VirtIODmbSeg *segs, unsigned nseg, ULONG split,
                                         struct VirtIOBufferDescriptor *sg, unsigned max_sg,
                                         unsigned *out, unsigned *in)
{
    size_t limit = dmb_req_limit(vdev);
    size_t total = 0;
    unsigned descs = 0, n = 0, i;
    bool writable = false;
    u64 offset;
    u8 *run;

    *out = 0;
    *in = 0;
    if (req->run != NULL) {
        /* Every path that does not post a request releases its run. */
        ASSERT(!"virtio DMB: request already holds a run");
        virtio_dmb_req_release(vdev, req);
    }
    if (!vdev->dmb.active || nseg > VIRTIO_DMB_REQ_MAX_SEGS) {
        return VIRTIO_DMB_TOO_BIG;
    }
    for (i = 0; i < nseg; i++) {
        if (segs[i].len == 0) {
            continue;
        }
        if ((segs[i].to_device && writable) || segs[i].len > limit ||
            (!segs[i].to_device && segs[i].preset_len > segs[i].len)) {
            return VIRTIO_DMB_TOO_BIG;
        }
        writable = writable || !segs[i].to_device;
        total += segs[i].len;
        descs += dmb_descs(segs[i].len, split);
        if (total > limit || descs > max_sg) {
            return VIRTIO_DMB_TOO_BIG;
        }
    }
    if (total == 0) {
        return VIRTIO_DMB_TOO_BIG;
    }

    run = (u8 *)virtio_dmb_alloc(vdev, total, &offset);
    if (run == NULL) {
        return VIRTIO_DMB_FULL;
    }
    req->run = run;
    req->total = total;
    req->nseg = (u8)nseg;

    total = 0;
    for (i = 0; i < nseg; i++) {
        ULONG done = 0;

        req->seg[i].va = segs[i].va;
        req->seg[i].len = segs[i].len;
        req->seg[i].off = (ULONG)total;
        req->seg[i].to_device = segs[i].to_device;
        if (segs[i].len == 0) {
            continue;
        }
        if (segs[i].to_device && segs[i].va != NULL) {
            virtio_dmb_copy_in(vdev, run + total, segs[i].va, segs[i].len);
            *out += dmb_descs(segs[i].len, split);
        } else if (segs[i].to_device) {
            /* The driver fills it with virtio_dmb_req_write(). */
            RtlZeroMemory(run + total, segs[i].len);
            *out += dmb_descs(segs[i].len, split);
        } else {
            /* Hand the driver zeroes, not stale bounce bytes, for any part
             * the device does not write. */
            RtlZeroMemory(run + total, segs[i].len);
            if (segs[i].preset_len != 0) {
                RtlCopyMemory(run + total, segs[i].preset, segs[i].preset_len);
            }
            *in += dmb_descs(segs[i].len, split);
        }
        while (done < segs[i].len) {
            ULONG chunk = segs[i].len - done;

            if (split != 0 && chunk > split) {
                chunk = split;
            }
            sg[n].physAddr.QuadPart = (LONGLONG)(offset + total + done);
            sg[n].length = chunk;
            n++;
            done += chunk;
        }
        total += segs[i].len;
    }
    return VIRTIO_DMB_MAPPED;
}

bool virtio_dmb_req_write(VirtIODevice *vdev, VirtIODmbReq *req, unsigned seg, ULONG offset,
                          const void *src, ULONG len)
{
    if (req->run == NULL || seg >= req->nseg || offset > req->seg[seg].len ||
        len > req->seg[seg].len - offset) {
        return false;
    }
    virtio_dmb_copy_in(vdev, req->run + req->seg[seg].off + offset, src, len);
    return true;
}

ULONG virtio_dmb_req_read(VirtIODevice *vdev, VirtIODmbReq *req, unsigned seg, ULONG offset,
                          void *dst, ULONG len)
{
    if (req->run == NULL || seg >= req->nseg || dst == NULL || offset >= req->seg[seg].len) {
        return 0;
    }
    if (len > req->seg[seg].len - offset) {
        len = req->seg[seg].len - offset;
    }
    virtio_dmb_copy_out(vdev, dst, req->run + req->seg[seg].off + offset, len);
    return len;
}

ULONG virtio_dmb_req_copy_out(VirtIODevice *vdev, VirtIODmbReq *req, unsigned seg, ULONG len)
{
    if (req->run == NULL || seg >= req->nseg || req->seg[seg].to_device) {
        return 0;
    }
    return virtio_dmb_req_read(vdev, req, seg, 0, req->seg[seg].va, len);
}

void virtio_dmb_req_release(VirtIODevice *vdev, VirtIODmbReq *req)
{
    if (req->run == NULL) {
        return;
    }
    virtio_dmb_free(vdev, req->run, req->total);
    req->run = NULL;
    req->total = 0;
    req->nseg = 0;
}

ULONG virtio_dmb_max_transfer(VirtIODevice *vdev, ULONG split, unsigned max_descs)
{
    u64 limit = virtio_dmb_max_mapping(vdev);

    if (!vdev->dmb.active) {
        return 0;
    }
    if (split != 0 && (u64)split * max_descs < limit) {
        limit = (u64)split * max_descs;
    }
    limit &= ~DMB_PAGE_MASK;
    return (ULONG)max(limit, (u64)DMB_PAGE_SIZE);
}
