/*
 * Copyright (C) 2019-2020 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
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

#pragma once
#include "helper.h"

extern "C"
{
#include "virtio_pci.h"
#include "virtio.h"
}

#include "viogpu_pci.h"
#include "viogpu_idr.h"
#include "viogpum.h"

extern VirtIOSystemOps VioGpuSystemOps;

enum virtio_gpu_ctrl_type
{
    VIRTIO_GPU_UNDEFINED = 0,

    /* 2d commands */
    VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
    VIRTIO_GPU_CMD_RESOURCE_UNREF,
    VIRTIO_GPU_CMD_SET_SCANOUT,
    VIRTIO_GPU_CMD_RESOURCE_FLUSH,
    VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
    VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
    VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
    VIRTIO_GPU_CMD_GET_CAPSET_INFO,
    VIRTIO_GPU_CMD_GET_CAPSET,
    VIRTIO_GPU_CMD_GET_EDID,

    /* 3d commands */
    VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
    VIRTIO_GPU_CMD_CTX_DESTROY,
    VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
    VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
    VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
    VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
    VIRTIO_GPU_CMD_SUBMIT_3D,

    /* cursor commands */
    VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
    VIRTIO_GPU_CMD_MOVE_CURSOR,

    /* success responses */
    VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
    VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
    VIRTIO_GPU_RESP_OK_CAPSET_INFO,
    VIRTIO_GPU_RESP_OK_CAPSET,
    VIRTIO_GPU_RESP_OK_EDID,

    /* error responses */
    VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
    VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
    VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

#define VIRTIO_GPU_EVENT_DISPLAY (1 << 0)

enum virtio_gpu_formats
{
    VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM = 1,
    VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM = 2,
    VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM = 3,
    VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM = 4,

    VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM = 67,
    VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM = 68,

    VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM = 121,
    VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM = 134,
};

#pragma pack(1)
typedef struct virtio_gpu_rect
{
    ULONG x;
    ULONG y;
    ULONG width;
    ULONG height;
} GPU_RECT, *PGPU_RECT;
#pragma pack()

#pragma pack(1)
typedef struct virtio_gpu_box
{
    ULONG x;
    ULONG y;
    ULONG z;
    ULONG width;
    ULONG height;
    ULONG depth;
} GPU_BOX, *PGPU_BOX;
#pragma pack()

#define VIRTIO_GPU_FLAG_FENCE (1 << 0)

#pragma pack(1)
typedef struct virtio_gpu_ctrl_hdr
{
    ULONG type;
    ULONG flags;
    ULONGLONG fence_id;
    ULONG ctx_id;
    ULONG padding;
} GPU_CTRL_HDR, *PGPU_CTRL_HDR;
#pragma pack()

#pragma pack(1)
typedef struct virtio_gpu_display_one
{
    GPU_RECT r;
    ULONG enabled;
    ULONG flags;
} GPU_DISP_ONE, *PGPU_DISP_ONE;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_UNREF */
#pragma pack(1)
typedef struct virtio_gpu_resource_unref
{
    GPU_CTRL_HDR hdr;
    ULONG resource_id;
    ULONG padding;
} GPU_RES_UNREF, *PGPU_RES_UNREF;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: create a 2d resource with a format */
#pragma pack(1)
typedef struct virtio_gpu_resource_create_2d
{
    GPU_CTRL_HDR hdr;
    ULONG resource_id;
    ULONG format;
    ULONG width;
    ULONG height;
} GPU_RES_CREATE_2D, *PGPU_RES_CREATE_2D;
#pragma pack()

/* VIRTIO_GPU_CMD_SET_SCANOUT */
#pragma pack(1)
typedef struct virtio_gpu_set_scanout
{
    GPU_CTRL_HDR hdr;
    GPU_RECT r;
    ULONG scanout_id;
    ULONG resource_id;
} GPU_SET_SCANOUT, *PGPU_SET_SCANOUT;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_FLUSH */
#pragma pack(1)
typedef struct virtio_gpu_resource_flush
{
    GPU_CTRL_HDR hdr;
    GPU_RECT r;
    ULONG resource_id;
    ULONG padding;
} GPU_RES_FLUSH, *PGPU_RES_FLUSH;
#pragma pack()

/* VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: simple transfer to_host */
#pragma pack(1)
typedef struct virtio_gpu_transfer_to_host_2d
{
    GPU_CTRL_HDR hdr;
    GPU_RECT r;
    ULONGLONG offset;
    ULONG resource_id;
    ULONG padding;
} GPU_RES_TRANSF_TO_HOST_2D, *PGPU_RES_TRANSF_TO_HOST_2D;
#pragma pack()

#pragma pack(1)
typedef struct virtio_gpu_mem_entry
{
    ULONGLONG addr;
    ULONG length;
    ULONG padding;
} GPU_MEM_ENTRY, *PGPU_MEM_ENTRY;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING */
#pragma pack(1)
typedef struct virtio_gpu_resource_attach_backing
{
    GPU_CTRL_HDR hdr;
    ULONG resource_id;
    ULONG nr_entries;
} GPU_RES_ATTACH_BACKING, *PGPU_RES_ATTACH_BACKING;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING */
#pragma pack(1)
typedef struct virtio_gpu_resource_detach_backing
{
    GPU_CTRL_HDR hdr;
    ULONG resource_id;
    ULONG padding;
} GPU_RES_DETACH_BACKING, *PGPU_RES_DETACH_BACKING;
#pragma pack()

/* VIRTIO_GPU_RESP_OK_DISPLAY_INFO */
#define VIRTIO_GPU_MAX_SCANOUTS 16
#pragma pack(1)
typedef struct virtio_gpu_resp_display_info
{
    GPU_CTRL_HDR hdr;
    GPU_DISP_ONE pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} GPU_RESP_DISP_INFO, *PGPU_RESP_DISP_INFO;
#pragma pack()

#pragma pack(1)
typedef struct virtio_gpu_object
{
    UINT hw_res_handle;
    VirtIOBufferDescriptor *pages;
    PVOID vmap;
    BOOLEAN dumb;
} GPU_OBJECT, *PGPU_OBJECT;
#pragma pack()

#pragma pack(1)
typedef struct virtio_gpu_cursor_pos
{
    ULONG scanout_id;
    ULONG x;
    ULONG y;
    ULONG padding;
} GPU_CURSOR_POS, *PGPU_CURSOR_POS;
#pragma pack()

/* VIRTIO_GPU_CMD_UPDATE_CURSOR, VIRTIO_GPU_CMD_MOVE_CURSOR */
#pragma pack(1)
typedef struct virtio_gpu_update_cursor
{
    GPU_CTRL_HDR hdr;
    GPU_CURSOR_POS pos; /* update & move */
    ULONG resource_id;  /* update only */
    ULONG hot_x;        /* update only */
    ULONG hot_y;        /* update only */
    ULONG padding;
} GPU_UPDATE_CURSOR, *PGPU_UPDATE_CURSOR;
#pragma pack()

/* VIRTIO_GPU_CMD_GET_EDID */
#pragma pack(1)
typedef struct virtio_gpu_cmd_get_edid
{
    GPU_CTRL_HDR hdr;
    ULONG scanout;
    ULONG padding;
} GPU_CMD_GET_EDID, *PGPU_CMD_GET_EDID;
#pragma pack()

/* VIRTIO_GPU_RESP_OK_EDID */
#pragma pack(1)
typedef struct virtio_gpu_resp_edid
{
    GPU_CTRL_HDR hdr;
    ULONG size;
    ULONG padding;
    UCHAR edid[1024];
} GPU_RESP_EDID, *PGPU_RESP_EDID;
#pragma pack()

#define EDID_V1_BLOCK_SIZE       128
#define EDID_RAW_BLOCK_SIZE      256

/* VIRTIO_GPU_CMD_GET_CAPSET_INFO */
#define VIRTIO_GPU_MAX_CAPSET_ID 63
#pragma pack(1)
typedef struct virtio_gpu_cmd_get_capset_info
{
    GPU_CTRL_HDR hdr;
    ULONG capset_index;
    ULONG padding;
} GPU_CMD_GET_CAPSET_INFO, *PGPU_CMD_GET_CASPSET_INFO;
#pragma pack()

/* VIRTIO_GPU_RESP_OK_CAPSET_INFO */
#pragma pack(1)
typedef struct virtio_gpu_resp_capset_info
{
    GPU_CTRL_HDR hdr;
    ULONG capset_id;
    ULONG capset_max_version;
    ULONG capset_max_size;
    ULONG padding;
} GPU_RESP_CAPSET_INFO, *PGPU_RESP_CAPSET_INFO;
#pragma pack()

/* VIRTIO_GPU_CMD_GET_CAPSET */
#pragma pack(1)
typedef struct virtio_gpu_cmd_get_capset
{
    GPU_CTRL_HDR hdr;
    ULONG capset_id;
    ULONG capset_version;
} GPU_CMD_GET_CAPSET, *PGPU_CMD_GET_CASPSET;
#pragma pack()

/* VIRTIO_GPU_RESP_OK_CAPSET_INFO */
#pragma pack(1)
typedef struct virtio_gpu_resp_capset
{
    GPU_CTRL_HDR hdr;
    UCHAR capset_data[1];
} GPU_RESP_CAPSET, *PGPU_RESP_CAPSET;
#pragma pack()

/* VIRTIO_GPU_CMD_CTX_DESTROY  */
#pragma pack(1)
typedef struct virtio_gpu_ctx_create
{
    GPU_CTRL_HDR hdr;
    ULONG nlen;
    ULONG context_init;
    UCHAR debug_name[64];
} GPU_CMD_CTX_CREATE, *PGPU_CMD_CTX_CREATE;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_3D*/
#pragma pack(1)
typedef struct virtio_gpu_resource_create_3d
{
    GPU_CTRL_HDR hdr;
    ULONG res_id;
    ULONG target;
    ULONG format;
    ULONG bind;
    ULONG width;
    ULONG height;
    ULONG depth;
    ULONG array_size;
    ULONG last_level;
    ULONG nr_samples;
    ULONG flags;
    ULONG padding;
} GPU_CMD_RES_CREATE_3D, *PGPU_CMD_RES_CREATE_3D;
#pragma pack()

/* VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D, VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D */
typedef struct virtio_gpu_transfer_host_3d
{
    GPU_CTRL_HDR hdr;
    GPU_BOX box;
    ULONGLONG offset;
    ULONG resource_id;
    ULONG level;
    ULONG stride;
    ULONG layer_stride;
} GPU_CMD_TRANSFER_HOST_3D, *PGPU_CMD_TRANSFER_HOST_3D;

/* VIRTIO_GPU_CMD_CTX_DESTROY  */
#pragma pack(1)
typedef struct virtio_gpu_ctx_destroy
{
    GPU_CTRL_HDR hdr;
} GPU_CMD_CTX_DESTROY, *PGPU_CMD_CTX_DESTROY;
#pragma pack()

#pragma pack(1)
typedef struct virtio_gpu_ctx_resource
{
    GPU_CTRL_HDR hdr;
    ULONG resource_id;
    ULONG padding;
} GPU_CMD_CTX_RESOURCE, *PGPU_CMD_CTX_RESOURCE;
#pragma pack()

#pragma pack(1)
typedef struct virtio_gpu_cmd_submit
{
    GPU_CTRL_HDR hdr;
    ULONG size;
    ULONG padding;
} GPU_CMD_SUBMIT, *PGPU_CMD_SUBMIT;
#pragma pack()

#pragma pack(push)
#pragma pack(1)

typedef struct _FEATURES_SUPPORT
{
    UCHAR DefaultGTF : 1;
    UCHAR PreferredTimingNode : 1;
    UCHAR StandardDefaultColorSpace : 1;
    UCHAR DisplayType : 2;
    UCHAR ActiveOff : 1;
    UCHAR Suspend : 1;
    UCHAR Standby : 1;
} FEATURES_SUPPORT, *PFEATURES_SUPPORT;

typedef struct _COLOR_CHARACTERISTICS
{
    UCHAR GreenYLow : 2;
    UCHAR GreenXLow : 2;
    UCHAR RedYLow : 2;
    UCHAR RedXLow : 2;
    UCHAR WhiteYLow : 2;
    UCHAR WhiteXLow : 2;
    UCHAR BlueYLow : 2;
    UCHAR BlueXLow : 2;
    UCHAR RedX;
    UCHAR RedY;
    UCHAR GreenX;
    UCHAR GreenY;
    UCHAR BlueX;
    UCHAR BlueY;
    UCHAR WhiteX;
    UCHAR WhiteY;
} COLOR_CHARACTERISTICS, *PCOLOR_CHARACTERISTICS;

#pragma pack(pop)

#define VIRTIO_GPU_F_VIRGL 0
#define VIRTIO_GPU_F_EDID  1

#define ISR_REASON_DISPLAY 1
#define ISR_REASON_CURSOR  2
#define ISR_REASON_CHANGE  4
