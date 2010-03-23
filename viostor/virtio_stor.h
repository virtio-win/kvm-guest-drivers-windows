/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * File: virtio_stor.h
 *
 * Main include file
 * This file contains vrious routines and globals
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#ifndef ___VIOSTOR_H__
#define ___VIOSTOR_H__

#include <ntddk.h>
#ifdef USE_STORPORT
#define STOR_USE_SCSI_ALIASES
#include <storport.h>
#else
#include <scsi.h>
#endif

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio_ring.h"

typedef struct VirtIOBufferDescriptor VIO_SG, *PVIO_SG;


/* Feature bits */
#define VIRTIO_BLK_F_BARRIER    0       /* Does host support barriers? */
#define VIRTIO_BLK_F_SIZE_MAX   1       /* Indicates maximum segment size */
#define VIRTIO_BLK_F_SEG_MAX    2       /* Indicates maximum # of segments */
#define VIRTIO_BLK_F_GEOMETRY   4       /* Legacy geometry available  */
#define VIRTIO_BLK_F_RO	        5       /* Disk is read-only */
#define VIRTIO_BLK_F_BLK_SIZE   6       /* Block size of disk is available*/
#define VIRTIO_BLK_F_SCSI       7       /* Supports scsi command passthru */
#define VIRTIO_BLK_F_WCACHE     9       /* write cache enabled */
#define VIRTIO_BLK_F_TOPOLOGY   10      /* Topology information is available */

/* These two define direction. */
#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1

#define VIRTIO_BLK_T_SCSI_CMD   2
#define VIRTIO_BLK_T_FLUSH      4

#define VIRTIO_BLK_S_OK	        0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

#define SECTOR_SIZE             512
#define MAX_PHYS_SEGMENTS       16
#define VIRTIO_MAX_SG	        (3+MAX_PHYS_SEGMENTS)
#define IO_PORT_LENGTH          0x40

#pragma pack(1)
typedef struct virtio_blk_config {
    /* The capacity (in 512-byte sectors). */
    u64 capacity;
    /* The maximum segment size (if VIRTIO_BLK_F_SIZE_MAX) */
    u32 size_max;
    /* The maximum number of segments (if VIRTIO_BLK_F_SEG_MAX) */
    u32 seg_max;
    /* geometry the device (if VIRTIO_BLK_F_GEOMETRY) */
    struct virtio_blk_geometry {
        u16 cylinders;
        u8 heads;
        u8 sectors;
    } geometry;
    /* block size of device (if VIRTIO_BLK_F_BLK_SIZE) */
    u32 blk_size;
    u8  physical_block_exp;
    u8  alignment_offset;
    u16 min_io_size;
    u16 opt_io_size;
}blk_config, *pblk_config;
#pragma pack()

typedef struct virtio_blk_outhdr {
    /* VIRTIO_BLK_T* */
    u32 type;
    /* io priority. */
    u32 ioprio;
    /* Sector (ie. 512 byte offset) */
    u64 sector;
}blk_outhdr, *pblk_outhdr;

typedef struct virtio_blk_req {
    LIST_ENTRY list_entry;
    struct request *req;
    blk_outhdr out_hdr;
    u8     status;
    VIO_SG sg[VIRTIO_MAX_SG];
}blk_req, *pblk_req;

typedef struct _ADAPTER_EXTENSION {
    ULONG_PTR             device_base;
    virtio_pci_vq_info    pci_vq_info;
    vring_virtqueue*      virtqueue;
    INQUIRYDATA           inquiry_data;
    blk_config            info;
    ULONG                 breaks_number;
    ULONG                 transfer_size;
    ULONG                 queue_depth;
    BOOLEAN               dump_mode;
    LIST_ENTRY            list_head;
    ULONG                 msix_vectors;
    ULONG                 features;
#ifdef USE_STORPORT
    LIST_ENTRY            complete_list;
    STOR_DPC              completion_dpc;
#if (NTDDI_VERSION >= NTDDI_VISTA)
    BOOLEAN               indirect;
#endif
#endif
}ADAPTER_EXTENSION, *PADAPTER_EXTENSION;

typedef struct _RHEL_SRB_EXTENSION {
    blk_req               vbr;
    ULONG                 out;
    ULONG                 in;
#ifndef USE_STORPORT
    BOOLEAN               call_next;
#endif
#if (NTDDI_VERSION >= NTDDI_VISTA)
    PVOID                 addr; 
#endif
}RHEL_SRB_EXTENSION, *PRHEL_SRB_EXTENSION;

#endif ___VIOSTOR__H__
