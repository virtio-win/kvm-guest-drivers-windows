/*
 * Main include file
 * This file contains various routines and globals
 *
 * Copyright (c) 2008-2017 Red Hat, Inc.
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

#ifndef ___VIOSTOR_H__
#define ___VIOSTOR_H__

#include <ntddk.h>
#include <storport.h>
#include <ntddscsi.h>

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio.h"
#include "virtio_ring.h"
#include "virtio_stor_utils.h"
#include "virtio_stor_hw_helper.h"

typedef struct VirtIOBufferDescriptor VIO_SG, *PVIO_SG;

/* Feature bits */
#define VIRTIO_BLK_F_BARRIER    0       /* Does host support barriers? */
#define VIRTIO_BLK_F_SIZE_MAX   1       /* Indicates maximum segment size */
#define VIRTIO_BLK_F_SEG_MAX    2       /* Indicates maximum # of segments */
#define VIRTIO_BLK_F_GEOMETRY   4       /* Legacy geometry available  */
#define VIRTIO_BLK_F_RO         5       /* Disk is read-only */
#define VIRTIO_BLK_F_BLK_SIZE   6       /* Block size of disk is available*/
#define VIRTIO_BLK_F_SCSI       7       /* Supports scsi command passthru */
#define VIRTIO_BLK_F_FLUSH      9       /* Flush command supported */
#define VIRTIO_BLK_F_TOPOLOGY   10      /* Topology information is available */
#define VIRTIO_BLK_F_CONFIG_WCE 11      /* Writeback mode available in config */
#define VIRTIO_BLK_F_MQ         12      /* support more than one vq */
#define VIRTIO_BLK_F_DISCARD    13      /* DISCARD is supported */
#define VIRTIO_BLK_F_WRITE_ZEROES 14    /* WRITE ZEROES is supported */

/* These two define direction. */
#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1

#define VIRTIO_BLK_T_SCSI_CMD   2
#define VIRTIO_BLK_T_FLUSH      4
#define VIRTIO_BLK_T_GET_ID     8
#define VIRTIO_BLK_T_DISCARD    11
#define VIRTIO_BLK_T_WRITE_ZEROES   13

#define VIRTIO_BLK_WRITE_ZEROES_FLAG_UNMAP   0x00000001

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

#define SECTOR_SIZE             512
#define SECTOR_SHIFT            9
#define IO_PORT_LENGTH          0x40
#define MAX_CPU                 256u
#define MAX_DISCARD_SEGMENTS    256u

#define VIRTIO_BLK_QUEUE_LAST   MAX_CPU

#define VIRTIO_BLK_MSIX_CONFIG_VECTOR   0

#define BLOCK_SERIAL_STRLEN     20

#define MAX_PHYS_SEGMENTS       512
#define VIRTIO_MAX_SG           (3+MAX_PHYS_SEGMENTS)

#define VIOBLK_POOL_TAG        'BoiV'

#ifndef NTDDI_WINTHRESHOLD
#define NTDDI_WINTHRESHOLD                  0x0A000000  /* ABRACADABRA_THRESHOLD */
#endif

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
    u32 opt_io_size;
    /* writeback mode (if VIRTIO_BLK_F_CONFIG_WCE) */
    u8 wce;
    u8 unused;
    /* number of vqs, only available when VIRTIO_BLK_F_MQ is set */
    u16 num_queues;

    /* the next 3 entries are guarded by VIRTIO_BLK_F_DISCARD */
    /*
     * The maximum discard sectors (in 512-byte sectors) for
     * one segment.
     */
    u32 max_discard_sectors;
    /*
     * The maximum number of discard segments in a
     * discard command.
     */
    u32 max_discard_seg;
    /* Discard commands must be aligned to this number of sectors. */
    u32 discard_sector_alignment;

    /* the next 3 entries are guarded by VIRTIO_BLK_F_WRITE_ZEROES */
    /*
     * The maximum number of write zeroes sectors (in 512-byte sectors) in
     * one segment.
     */
    u32 max_write_zeroes_sectors;
    /*
     * The maximum number of segments in a write zeroes
     * command.
     */
    u32 max_write_zeroes_seg;
    /*
     * Set if a VIRTIO_BLK_T_WRITE_ZEROES request may result in the
     * deallocation of one or more of the sectors.
     */
    u8 write_zeroes_may_unmap;

    u8 unused1[3];
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

/* Discard/write zeroes range for each request. */
typedef struct virtio_blk_discard_write_zeroes {
    /* discard/write zeroes start sector */
    u64 sector;
    /* number of discard/write zeroes sectors */
    u32 num_sectors;
    /* flags for this range */
    u32 flags;
}blk_discard_write_zeroes, *pblk_discard_write_zeroes;

typedef struct virtio_blk_req {
    LIST_ENTRY list_entry;
    PVOID      req;
    blk_outhdr out_hdr;
    u8         status;
}blk_req, *pblk_req;

typedef struct virtio_bar {
    PHYSICAL_ADDRESS  BasePA;
    ULONG             uLength;
    PVOID             pBase;
    BOOLEAN           bPortSpace;
} VIRTIO_BAR, *PVIRTIO_BAR;

typedef struct _SENSE_INFO {
    UCHAR senseKey;
    UCHAR additionalSenseCode;
    UCHAR additionalSenseCodeQualifier;
} SENSE_INFO, *PSENSE_INFO;

typedef struct _REQUEST_LIST {
    LIST_ENTRY            srb_list;
    ULONG                 srb_cnt;
} REQUEST_LIST, *PREQUEST_LIST;

typedef struct _ADAPTER_EXTENSION {
    VirtIODevice          vdev;

    PVOID                 pageAllocationVa;
    ULONG                 pageAllocationSize;
    ULONG                 pageOffset;

    PVOID                 poolAllocationVa;
    ULONG                 poolAllocationSize;
    ULONG                 poolOffset;

    struct virtqueue *    vq[VIRTIO_BLK_QUEUE_LAST];
    USHORT                num_queues;
    INQUIRYDATA           inquiry_data;
    blk_config            info;
    ULONG                 queue_depth;
    BOOLEAN               dump_mode;
    ULONG                 msix_vectors;
    BOOLEAN               msix_enabled;
    BOOLEAN               msix_one_vector;
    ULONGLONG             features;
    CHAR                  sn[BLOCK_SERIAL_STRLEN];
    BOOLEAN               sn_ok;
    blk_req               vbr;
    BOOLEAN               indirect;
    ULONGLONG             lastLBA;

    union {
        PCI_COMMON_HEADER pci_config;
        UCHAR             pci_config_buf[sizeof(PCI_COMMON_CONFIG)];
    };
    VIRTIO_BAR            pci_bars[PCI_TYPE0_ADDRESSES];
    ULONG                 system_io_bus_number;
    ULONG                 slot_number;
    ULONG                 perfFlags;
    PSTOR_DPC             dpc;
    BOOLEAN               dpc_ok;
    BOOLEAN               check_condition;
    SENSE_INFO            sense_info;
    BOOLEAN               removed;
    ULONG                 max_tx_length;
    PGROUP_AFFINITY       pmsg_affinity;
    STOR_ADDR_BTL8        device_address;
    blk_discard_write_zeroes blk_discard[16];
    REQUEST_LIST          processing_srbs[MAX_CPU];
    BOOLEAN               reset_in_progress;
#if (NTDDI_VERSION >= NTDDI_WINTHRESHOLD)
    ULONGLONG             fw_ver;
#endif
#ifdef DBG
    LONG                  srb_cnt;
    LONG                  inqueue_cnt;
#endif
}ADAPTER_EXTENSION, *PADAPTER_EXTENSION;

typedef struct _VRING_DESC_ALIAS
{
    union
    {
        ULONGLONG data[2];
        UCHAR chars[SIZE_OF_SINGLE_INDIRECT_DESC];
    }u;
}VRING_DESC_ALIAS;

typedef struct _SRB_EXTENSION {
    blk_req               vbr;
    ULONG                 out;
    ULONG                 in;
    ULONG                 MessageID;
    BOOLEAN               fua;
    VIO_SG                sg[VIRTIO_MAX_SG];
    VRING_DESC_ALIAS      desc[VIRTIO_MAX_SG];
}SRB_EXTENSION, *PSRB_EXTENSION;

BOOLEAN
VirtIoInterrupt(
    IN PVOID DeviceExtension
    );

#ifndef PCIX_TABLE_POINTER
typedef struct {
  union {
    struct {
      ULONG BaseIndexRegister :3;
      ULONG Reserved          :29;
    };
    ULONG TableOffset;
  };
} PCIX_TABLE_POINTER, *PPCIX_TABLE_POINTER;
#endif

#ifndef PCI_MSIX_CAPABILITY
typedef struct {
  PCI_CAPABILITIES_HEADER Header;
  struct {
    USHORT TableSize      :11;
    USHORT Reserved       :3;
    USHORT FunctionMask   :1;
    USHORT MSIXEnable     :1;
  } MessageControl;
  PCIX_TABLE_POINTER      MessageTable;
  PCIX_TABLE_POINTER      PBATable;
} PCI_MSIX_CAPABILITY, *PPCI_MSIX_CAPABILITY;
#endif

#endif ___VIOSTOR__H__
