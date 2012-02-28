/* Virtio ring implementation.
 *
 *  Copyright 2007 Rusty Russell IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "VirtIO.h"
#include "VirtIO_PCI.h"
#include "virtio_ring.h"
#include "virtio_stor_utils.h"
#include "virtio_stor.h"

//#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)
#define to_vvq(_vq) (struct vring_virtqueue *)_vq

#if (INDIRECT_SUPPORTED)
/* Set up an indirect table of descriptors and add it to the queue. */
static
int
vring_add_indirect(
    IN struct vring_virtqueue *vq,
    IN struct VirtIOBufferDescriptor sg[],
    IN unsigned int out,
    IN unsigned int in,
    IN PVOID va)
{
    struct vring_desc *desc = (struct vring_desc *)va;
    unsigned head;
    unsigned int i;
    SCSI_PHYSICAL_ADDRESS addr;
    ULONG len;

    addr = ScsiPortGetPhysicalAddress(vq->vq.DeviceExtension, NULL, desc, &len);
    if (!addr.QuadPart) {
        return vq->vring.num;
    }
    /* Transfer entries from the sg list into the indirect page */
    for (i = 0; i < out; i++) {
        desc[i].flags = VRING_DESC_F_NEXT;
        desc[i].addr = sg->physAddr.QuadPart;
        desc[i].len = sg->ulSize;
        desc[i].next = i+1;
        sg++;
    }
    for (; i < (out + in); i++) {
        desc[i].flags = VRING_DESC_F_NEXT|VRING_DESC_F_WRITE;
        desc[i].addr = sg->physAddr.QuadPart;
        desc[i].len = sg->ulSize;
        desc[i].next = i+1;
        sg++;
    }

    /* Last one doesn't continue. */
    desc[i-1].flags &= ~VRING_DESC_F_NEXT;
    desc[i-1].next = 0;

    /* We're about to use a buffer */
    vq->num_free--;

    /* Use a single buffer which doesn't continue */
    head = vq->free_head;
    vq->vring.desc[head].flags = VRING_DESC_F_INDIRECT;
    vq->vring.desc[head].addr = addr.QuadPart;
    vq->vring.desc[head].len = i * sizeof(struct vring_desc);

    /* Update free pointer */
    vq->free_head = vq->vring.desc[head].next;

    return head;
}
#endif

int
vring_add_buf_stor(
    IN struct virtqueue *_vq,
    IN struct VirtIOBufferDescriptor sg[],
    IN unsigned int out,
    IN unsigned int in,
    IN PVOID data)
{
    struct vring_virtqueue *vq = to_vvq(_vq);
    unsigned int i, avail, head, prev;
#if (INDIRECT_SUPPORTED)
    PSCSI_REQUEST_BLOCK Srb;
    PRHEL_SRB_EXTENSION srbExt;
    PADAPTER_EXTENSION adaptExt;
    pblk_req vbr;
#endif

    if(data == NULL) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s: data is NULL!\n",  __FUNCTION__) );
        return -1;
    }
#if (INDIRECT_SUPPORTED)
    adaptExt = (PADAPTER_EXTENSION)vq->vq.DeviceExtension;
    vbr = (pblk_req) data;
    Srb      = (PSCSI_REQUEST_BLOCK)vbr->req;
    srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
    if ((out + in) > 1 && vq->num_free) {
        head = vring_add_indirect(vq, sg, out, in, srbExt->desc);
        if (head != vq->vring.num)
            goto add_head;
    }
#endif
    if(out + in > vq->vring.num) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s: out + in > vq->vring.num!\n",  __FUNCTION__) );
        return -1;
    }

    if(out + in == 0) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s: out + in == 0!\n",  __FUNCTION__) );
        return -1;
    }

    if (vq->num_free < out + in) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s: can't add buf len %i - avail = %i\n",
           __FUNCTION__, out + in, vq->num_free) );
        /*
        notify the host immediately if we are out of
        descriptors in tx ring
        */
        if (out)
           vq->notify(&vq->vq);
        return -1;
    }

    /* We're about to use some buffers from the free list. */
    vq->num_free -= out + in;
    prev = 0;
    head = vq->free_head;
    for (i = vq->free_head; out; i = vq->vring.desc[i].next, out--) {
        vq->vring.desc[i].flags = VRING_DESC_F_NEXT;
        vq->vring.desc[i].addr = sg->physAddr.QuadPart;
        vq->vring.desc[i].len = sg->ulSize;
        prev = i;
        sg++;
    }
    for (; in; i = vq->vring.desc[i].next, in--) {
        vq->vring.desc[i].flags = VRING_DESC_F_NEXT|VRING_DESC_F_WRITE;
        vq->vring.desc[i].addr = sg->physAddr.QuadPart;
        vq->vring.desc[i].len = sg->ulSize;
        prev = i;
        sg++;
    }
    /* Last one doesn't continue. */
    vq->vring.desc[prev].flags &= ~VRING_DESC_F_NEXT;

    /* Update free pointer */
    vq->free_head = i;
#if (INDIRECT_SUPPORTED)
add_head:
#endif
    /* Set token. */
    vq->data[head] = data;

    /* Put entry in available array (but don't update avail->idx until they
    * do sync).  FIXME: avoid modulus here? */
    avail = (vq->vring.avail->idx + vq->num_added++) % vq->vring.num;
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s >>> avail %d vq->vring.avail->idx = %d, vq->num_added = %d vq->vring.num = %d\n",
        __FUNCTION__, avail, vq->vring.avail->idx, vq->num_added, vq->vring.num));

    vq->vring.avail->ring[avail] = (u16) head;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s: Added buffer head %i to %p\n",
        __FUNCTION__, head, vq) );
#if (INDIRECT_SUPPORTED)
    if (adaptExt->indirect)
        return vq->num_free ? vq->vring.num : 0;
#endif
    return vq->num_free;
}

