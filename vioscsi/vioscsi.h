/*
 * Main include file
 * This file contains various routines and globals
 *
 * Copyright (c) 2012-2017 Red Hat, Inc.
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

#ifndef ___VIOSCSI_H__
#define ___VIOSCSI_H__

#include <ntddk.h>
#include <storport.h>
#include "scsiwmi.h"

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio.h"
#include "virtio_ring.h"

typedef struct VirtIOBufferDescriptor VIO_SG, *PVIO_SG;

#define VIRTIO_SCSI_CDB_SIZE   32
#define VIRTIO_SCSI_SENSE_SIZE 96

#define MAX_PHYS_SEGMENTS       64
#define MAX_PHYS_INDIRECT_SEGMENTS 256
#define VIOSCSI_POOL_TAG        'SoiV'
#define VIRTIO_MAX_SG            (3+MAX_PHYS_SEGMENTS)

#define SECTOR_SIZE             512
#define IO_PORT_LENGTH          0x40
#define MAX_CPU                 256

#define MAX_PH_BREAKS           "PhysicalBreaks"


/* Feature Bits */
#define VIRTIO_SCSI_F_INOUT                    0
#define VIRTIO_SCSI_F_HOTPLUG                  1
#define VIRTIO_SCSI_F_CHANGE                   2

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
#define VIRTIO_SCSI_T_PARAM_CHANGE             3

/* Reasons of transport reset event */
#define VIRTIO_SCSI_EVT_RESET_HARD             0
#define VIRTIO_SCSI_EVT_RESET_RESCAN           1
#define VIRTIO_SCSI_EVT_RESET_REMOVED          2

#define VIRTIO_SCSI_S_SIMPLE                   0
#define VIRTIO_SCSI_S_ORDERED                  1
#define VIRTIO_SCSI_S_HEAD                     2
#define VIRTIO_SCSI_S_ACA                      3

#define VIRTIO_SCSI_CONTROL_QUEUE              0
#define VIRTIO_SCSI_EVENTS_QUEUE               1
#define VIRTIO_SCSI_REQUEST_QUEUE_0            2
#define VIRTIO_SCSI_QUEUE_LAST                 VIRTIO_SCSI_REQUEST_QUEUE_0 + MAX_CPU

/* MSI messages and virtqueue indices are offset by 1, MSI 0 is not used */
#define QUEUE_TO_MESSAGE(QueueId)              ((QueueId) + 1)
#define MESSAGE_TO_QUEUE(MessageId)            ((MessageId) - 1)

/* SCSI command request, followed by data-out */
#pragma pack(1)
typedef struct {
    u8 lun[8];        /* Logical Unit Number */
    u64 tag;          /* Command identifier */
    u8 task_attr;     /* Task attribute */
    u8 prio;
    u8 crn;
    u8 cdb[VIRTIO_SCSI_CDB_SIZE];
} VirtIOSCSICmdReq, * PVirtIOSCSICmdReq;
#pragma pack()


/* Response, followed by sense data and data-in */
#pragma pack(1)
typedef struct {
    u32 sense_len;        /* Sense data length */
    u32 resid;            /* Residual bytes in data buffer */
    u16 status_qualifier; /* Status qualifier */
    u8 status;            /* Command completion status */
    u8 response;          /* Response values */
    u8 sense[VIRTIO_SCSI_SENSE_SIZE];
} VirtIOSCSICmdResp, * PVirtIOSCSICmdResp;
#pragma pack()

/* Task Management Request */
#pragma pack(1)
typedef struct {
    u32 type;
    u32 subtype;
    u8 lun[8];
    u64 tag;
} VirtIOSCSICtrlTMFReq, * PVirtIOSCSICtrlTMFReq;
#pragma pack()

#pragma pack(1)
typedef struct {
    u8 response;
} VirtIOSCSICtrlTMFResp, * PVirtIOSCSICtrlTMFResp;
#pragma pack()

/* Asynchronous notification query/subscription */
#pragma pack(1)
typedef struct {
    u32 type;
    u8 lun[8];
    u32 event_requested;
} VirtIOSCSICtrlANReq, *PVirtIOSCSICtrlANReq;
#pragma pack()

#pragma pack(1)
typedef struct {
    u32 event_actual;
    u8 response;
} VirtIOSCSICtrlANResp, * PVirtIOSCSICtrlANResp;
#pragma pack()

#pragma pack(1)
typedef struct {
    u32 event;
    u8 lun[8];
    u32 reason;
} VirtIOSCSIEvent, * PVirtIOSCSIEvent;
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
} VirtIOSCSIConfig, * PVirtIOSCSIConfig;
#pragma pack()

#pragma pack(1)
typedef struct {
    PVOID srb;
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
} VirtIOSCSICmd, * PVirtIOSCSICmd;
#pragma pack()

#pragma pack(1)
typedef struct {
    PVOID           adapter;
    VirtIOSCSIEvent event;
    VIO_SG          sg;
} VirtIOSCSIEventNode, * PVirtIOSCSIEventNode;
#pragma pack()

typedef struct _VRING_DESC_ALIAS
{
    union
    {
        ULONGLONG data[2];
        UCHAR chars[SIZE_OF_SINGLE_INDIRECT_DESC];
    }u;
}VRING_DESC_ALIAS, *PVRING_DESC_ALIAS;

#pragma pack(1)
typedef struct _SRB_EXTENSION {
    LIST_ENTRY            list_entry;
    PSCSI_REQUEST_BLOCK   Srb;
    ULONG                 out;
    ULONG                 in;
    ULONG                 Xfer;
    VirtIOSCSICmd         cmd;
    ULONG                 vq_num;
    ULONG                 allocated;
    PVIO_SG               psgl;
    PVRING_DESC_ALIAS     pdesc;
    VIO_SG                vio_sg[VIRTIO_MAX_SG];
    VRING_DESC_ALIAS      desc_alias[VIRTIO_MAX_SG];
#ifdef USE_CPU_TO_VQ_MAP
    ULONG                 cpu;
#endif // USE_CPU_TO_VQ_MAP
}SRB_EXTENSION, * PSRB_EXTENSION;
#pragma pack()

#pragma pack(1)
typedef struct {
    SCSI_REQUEST_BLOCK    Srb;
    PSRB_EXTENSION        SrbExtension;
}TMF_COMMAND, * PTMF_COMMAND;
#pragma pack()

typedef struct _REQUEST_LIST {
    LIST_ENTRY            srb_list;
    KSPIN_LOCK            srb_list_lock;
} REQUEST_LIST, *PREQUEST_LIST;

typedef struct virtio_bar {
    PHYSICAL_ADDRESS  BasePA;
    ULONG             uLength;
    PVOID             pBase;
    BOOLEAN           bPortSpace;
} VIRTIO_BAR, *PVIRTIO_BAR;

typedef struct _ADAPTER_EXTENSION {
    VirtIODevice          vdev;

    PVOID                 pageAllocationVa;
    ULONG                 pageAllocationSize;
    ULONG                 pageOffset;

    PVOID                 poolAllocationVa;
    ULONG                 poolAllocationSize;
    ULONG                 poolOffset;

    struct virtqueue *    vq[VIRTIO_SCSI_QUEUE_LAST];
    ULONG_PTR             device_base;
    VirtIOSCSIConfig      scsi_config;
    union {
        PCI_COMMON_HEADER pci_config;
        UCHAR             pci_config_buf[sizeof(PCI_COMMON_CONFIG)];
    };
    VIRTIO_BAR            pci_bars[PCI_TYPE0_ADDRESSES];
    ULONG                 system_io_bus_number;
    ULONG                 slot_number;

    ULONG                 queue_depth;
    BOOLEAN               dump_mode;

    ULONGLONG             features;

    ULONG                 msix_vectors;
    BOOLEAN               msix_enabled;
    BOOLEAN               msix_one_vector;
    BOOLEAN               indirect;

    TMF_COMMAND           tmf_cmd;
    BOOLEAN               tmf_infly;

    PVirtIOSCSIEventNode  events;

    ULONG                 num_queues;
#ifdef USE_CPU_TO_VQ_MAP
    UCHAR                 cpu_to_vq_map[MAX_CPU];
#endif
    REQUEST_LIST          pending_list[MAX_CPU];
    ULONG                 perfFlags;
    PGROUP_AFFINITY       pmsg_affinity;
    BOOLEAN               dpc_ok;
    PSTOR_DPC             dpc;
    ULONG                 max_physical_breaks;
    SCSI_WMILIB_CONTEXT   WmiLibContext;
    ULONGLONG             hba_id;
    PUCHAR                ser_num;
    ULONGLONG             wwn;
    ULONGLONG             port_wwn;
    ULONG                 port_idx;
    UCHAR                 ven_id[8 + 1];
    UCHAR                 prod_id[16 + 1];
    UCHAR                 rev_id[4 + 1];
}ADAPTER_EXTENSION, * PADAPTER_EXTENSION;

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

#define SPC3_SCSI_SENSEQ_PARAMETERS_CHANGED                 0x0
#define SPC3_SCSI_SENSEQ_MODE_PARAMETERS_CHANGED            0x01
#define SPC3_SCSI_SENSEQ_CAPACITY_DATA_HAS_CHANGED          0x09

typedef enum VIOSCSI_VPD_CODE_SET {
    VioscsiVpdCodeSetBinary = 1,
    VioscsiVpdCodeSetAscii = 2,
    VioscsiVpdCodeSetSASBinary = 0x61,
} VIOSCSI_VPD_CODE_SET, *PVIOSCSI_VPD_CODE_SET;

typedef enum VIOSCSI_VPD_IDENTIFIER_TYPE {
    VioscsiVpdIdentifierTypeVendorSpecific = 0,
    VioscsiVpdIdentifierTypeVendorId = 1,
    VioscsiVpdIdentifierTypeEUI64 = 2,
    VioscsiVpdIdentifierTypeFCPHName = 3,
    VioscsiVpdIdentifierTypeFCTargetPortPHName = 0x93,
    VioscsiVpdIdentifierTypeFCTargetPortRelativeTargetPort = 0x94,
} VIOSCSI_VPD_IDENTIFIER_TYPE, *PVIOSCSI_VPD_IDENTIFIER_TYPE;

#endif ___VIOSCSI__H__
