/**********************************************************************
 * Copyright (c) 2012  Red Hat, Inc.
 *
 * File: vioscsi.h
 *
 * Main include file
 * This file contains vrious routines and globals
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#ifndef ___VIOSCSI_H__
#define ___VIOSCSI_H__

#include <ntddk.h>
#include <storport.h>

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio_ring.h"

typedef struct VirtIOBufferDescriptor VIO_SG, *PVIO_SG;


#define VIRTIO_SCSI_CDB_SIZE   32
#define VIRTIO_SCSI_SENSE_SIZE 96

#define MAX_PHYS_SEGMENTS       16
#define SECTOR_SIZE             512
#define IO_PORT_LENGTH          0x40

/* Response codes */
#define VIRTIO_SCSI_S_OK                       0
#define VIRTIO_SCSI_S_UNDERRUN                 1
#define VIRTIO_SCSI_S_ABORTED                  2
#define VIRTIO_SCSI_S_BAD_TARGET               3
#define VIRTIO_SCSI_S_RESET                    4
#define VIRTIO_SCSI_S_BUSY                     5
#define VIRTIO_SCSI_S_TRANSPORT_FAILURE        6
#define VIRTIO_SCSI_S_TARGET_FAILURE           7
#define VIRTIO_SCSI_S_NEXUS_FAILURE            8
#define VIRTIO_SCSI_S_FAILURE                  9
#define VIRTIO_SCSI_S_FUNCTION_SUCCEEDED       10
#define VIRTIO_SCSI_S_FUNCTION_REJECTED        11
#define VIRTIO_SCSI_S_INCORRECT_LUN            12

/* Controlq type codes.  */
#define VIRTIO_SCSI_T_TMF                      0
#define VIRTIO_SCSI_T_AN_QUERY                 1
#define VIRTIO_SCSI_T_AN_SUBSCRIBE             2

/* Valid TMF subtypes.  */
#define VIRTIO_SCSI_T_TMF_ABORT_TASK           0
#define VIRTIO_SCSI_T_TMF_ABORT_TASK_SET       1
#define VIRTIO_SCSI_T_TMF_CLEAR_ACA            2
#define VIRTIO_SCSI_T_TMF_CLEAR_TASK_SET       3
#define VIRTIO_SCSI_T_TMF_I_T_NEXUS_RESET      4
#define VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET   5
#define VIRTIO_SCSI_T_TMF_QUERY_TASK           6
#define VIRTIO_SCSI_T_TMF_QUERY_TASK_SET       7

/* Events.  */
#define VIRTIO_SCSI_T_EVENTS_MISSED            0x80000000
#define VIRTIO_SCSI_T_NO_EVENT                 0
#define VIRTIO_SCSI_T_TRANSPORT_RESET          1
#define VIRTIO_SCSI_T_ASYNC_NOTIFY             2

#define VIRTIO_SCSI_S_SIMPLE                   0
#define VIRTIO_SCSI_S_ORDERED                  1
#define VIRTIO_SCSI_S_HEAD                     2
#define VIRTIO_SCSI_S_ACA                      3


/* SCSI command request, followed by data-out */
#pragma pack(1)
typedef struct {
	u8 lun[8];		/* Logical Unit Number */
	u64 tag;		/* Command identifier */
	u8 task_attr;		/* Task attribute */
	u8 prio;
	u8 crn;
	u8 cdb[VIRTIO_SCSI_CDB_SIZE];
} VirtIOSCSICmdReq;
#pragma pack()


/* Response, followed by sense data and data-in */
#pragma pack(1)
typedef struct {
	u32 sense_len;		/* Sense data length */
	u32 resid;		/* Residual bytes in data buffer */
	u16 status_qualifier;	/* Status qualifier */
	u8 status;		/* Command completion status */
	u8 response;		/* Response values */
	u8 sense[VIRTIO_SCSI_SENSE_SIZE];
} VirtIOSCSICmdResp;
#pragma pack()

/* Task Management Request */
#pragma pack(1)
typedef struct {
	u32 type;
	u32 subtype;
	u8 lun[8];
	u64 tag;
} VirtIOSCSICtrlTMFReq;
#pragma pack()

#pragma pack(1)
typedef struct {
	u8 response;
} VirtIOSCSICtrlTMFResp;
#pragma pack()

/* Asynchronous notification query/subscription */
#pragma pack(1)
typedef struct {
	u32 type;
	u8 lun[8];
	u32 event_requested;
} VirtIOSCSICtrlANReq;
#pragma pack()

#pragma pack(1)
typedef struct {
	u32 event_actual;
	u8 response;
} VirtIOSCSICtrlANResp;
#pragma pack()

#pragma pack(1)
typedef struct {
	u32 event;
	u8 lun[8];
	u32 reason;
} VirtIOSCSIEvent;
#pragma pack()

#pragma pack(1)
typedef struct {
	u32 num_queues;
	u32 seg_max;
	u32 max_sectors;
	u32 cmd_per_lun;
	u32 event_info_size;
	u32 sense_size;
	u32 cdb_size;
	u16 max_channel;
	u16 max_target;
	u32 max_lun;
} VirtIOSCSIConfig;
#pragma pack()

#pragma pack(1)
typedef struct {
    PVOID sc;
    PVOID comp;
    union {
        VirtIOSCSICmdReq      cmd;
        VirtIOSCSICtrlTMFReq  tmf;
        VirtIOSCSICtrlANReq   an;
    } req;
    union {
        VirtIOSCSICmdResp     cmd;
        VirtIOSCSICtrlTMFResp tmf;
        VirtIOSCSICtrlANResp  an;
        VirtIOSCSIEvent       event;
    } resp;
} VirtIOSCSICmd;
#pragma pack()

typedef struct _ADAPTER_EXTENSION {
    ULONG_PTR             device_base;
    VirtIOSCSIConfig      scsi_config;

    ULONG                 queue_depth;
    BOOLEAN               dump_mode;

    ULONG                 features;


    virtio_pci_vq_info    pci_vq_info[3];
    vring_virtqueue*      virtqueue[3];
    BOOLEAN               msix_enabled;

}ADAPTER_EXTENSION, *PADAPTER_EXTENSION;

typedef struct _SRB_EXTENSION {
    ULONG                 out;
    ULONG                 in;
    ULONG                 Xfer;
    VirtIOSCSICmd         cmd;
    VIO_SG                sg[128];

}SRB_EXTENSION, *PSRB_EXTENSION;

BOOLEAN
VioScsiInterrupt(
    IN PVOID DeviceExtension
    );

#endif ___VIOSCSI__H__
