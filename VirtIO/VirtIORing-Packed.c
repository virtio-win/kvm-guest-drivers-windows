/*
 * Packed virtio ring manipulation routines
 *
 * Copyright 2019 Red Hat, Inc.
 *
 * Authors:
 *  Yuri Benditovich <ybendito@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio.h"
#include "kdebugprint.h"
#include "virtio_ring.h"
#include "windows\virtio_ring_allocation.h"

unsigned int vring_control_block_size_packed(u16 qsize)
{
    return sizeof(struct virtqueue);
}

unsigned long vring_size_packed(unsigned int num, unsigned long align)
{
    return 4096;
}

/* Initializes a new virtqueue using already allocated memory */
struct virtqueue *vring_new_virtqueue_packed(
    unsigned int index,                 /* virtqueue index */
    unsigned int num,                   /* virtqueue size (always a power of 2) */
    unsigned int vring_align,           /* vring alignment requirement */
    VirtIODevice *vdev,                 /* the virtio device owning the queue */
    void *pages,                        /* vring memory */
    void(*notify)(struct virtqueue *), /* notification callback */
    void *control)                      /* virtqueue memory */
{
    return NULL;
}
