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

enum virtio_gpu_ctrl_type {
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

enum virtio_gpu_formats {
    VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM  = 1,
    VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM  = 2,
    VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM  = 3,
    VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM  = 4,

    VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM  = 67,
    VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM  = 68,

    VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM  = 121,
    VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM  = 134,
};

#pragma pack(1)
typedef struct virtio_gpu_rect {
    ULONG x;
    ULONG y;
    ULONG width;
    ULONG height;
}GPU_RECT, *PGPU_RECT;
#pragma pack()

#define VIRTIO_GPU_FLAG_FENCE (1 << 0)

#pragma pack(1)
typedef struct virtio_gpu_ctrl_hdr {
    ULONG type;
    ULONG flags;
    ULONGLONG fence_id;
    ULONG ctx_id;
    ULONG padding;
}GPU_CTRL_HDR, *PGPU_CTRL_HDR;
#pragma pack()

#pragma pack(1)
typedef struct virtio_gpu_display_one {
    GPU_RECT r;
    ULONG enabled;
    ULONG flags;
}GPU_DISP_ONE, *PGPU_DISP_ONE;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_UNREF */
#pragma pack(1)
typedef struct virtio_gpu_resource_unref {
    GPU_CTRL_HDR hdr;
    ULONG resource_id;
    ULONG padding;
}GPU_RES_UNREF, *PGPU_RES_UNREF;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: create a 2d resource with a format */
#pragma pack(1)
typedef struct virtio_gpu_resource_create_2d {
    GPU_CTRL_HDR hdr;
    ULONG resource_id;
    ULONG format;
    ULONG width;
    ULONG height;
}GPU_RES_CREATE_2D, *PGPU_RES_CREATE_2D;
#pragma pack()

/* VIRTIO_GPU_CMD_SET_SCANOUT */
#pragma pack(1)
typedef struct virtio_gpu_set_scanout {
    GPU_CTRL_HDR hdr;
    GPU_RECT r;
    ULONG scanout_id;
    ULONG resource_id;
}GPU_SET_SCANOUT, *PGPU_SET_SCANOUT;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_FLUSH */
#pragma pack(1)
typedef struct virtio_gpu_resource_flush {
    GPU_CTRL_HDR hdr;
    GPU_RECT r;
    ULONG resource_id;
    ULONG padding;
}GPU_RES_FLUSH, *PGPU_RES_FLUSH;
#pragma pack()

/* VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: simple transfer to_host */
#pragma pack(1)
typedef struct virtio_gpu_transfer_to_host_2d {
    GPU_CTRL_HDR hdr;
    GPU_RECT r;
    ULONGLONG offset;
    ULONG resource_id;
    ULONG padding;
}GPU_RES_TRANSF_TO_HOST_2D, *PGPU_RES_TRANSF_TO_HOST_2D;
#pragma pack()

#pragma pack(1)
typedef struct virtio_gpu_mem_entry {
    ULONGLONG addr;
    ULONG length;
    ULONG padding;
}GPU_MEM_ENTRY, *PGPU_MEM_ENTRY;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING */
#pragma pack(1)
typedef struct virtio_gpu_resource_attach_backing {
    GPU_CTRL_HDR hdr;
    ULONG resource_id;
    ULONG nr_entries;
}GPU_RES_ATTACH_BACKING, *PGPU_RES_ATTACH_BACKING;
#pragma pack()

/* VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING */
#pragma pack(1)
typedef struct virtio_gpu_resource_detach_backing {
    GPU_CTRL_HDR hdr;
    ULONG resource_id;
    ULONG padding;
}GPU_RES_DETACH_BACKING, *PGPU_RES_DETACH_BACKING;
#pragma pack()

/* VIRTIO_GPU_RESP_OK_DISPLAY_INFO */
#define VIRTIO_GPU_MAX_SCANOUTS 16
#pragma pack(1)
typedef struct virtio_gpu_resp_display_info {
    GPU_CTRL_HDR hdr;
    GPU_DISP_ONE pmodes[VIRTIO_GPU_MAX_SCANOUTS];
}GPU_RESP_DISP_INFO, *PGPU_RESP_DISP_INFO;
#pragma pack()

#pragma pack(1)
typedef struct virtio_gpu_object {
    UINT hw_res_handle;
    VirtIOBufferDescriptor* pages;
    PVOID vmap;
    BOOLEAN dumb;
}GPU_OBJECT, *PGPU_OBJECT;
#pragma pack()


#pragma pack(1)
typedef struct virtio_gpu_cursor_pos {
    ULONG scanout_id;
    ULONG x;
    ULONG y;
    ULONG padding;
}GPU_CURSOR_POS, *PGPU_CURSOR_POS;
#pragma pack()

/* VIRTIO_GPU_CMD_UPDATE_CURSOR, VIRTIO_GPU_CMD_MOVE_CURSOR */
#pragma pack(1)
typedef struct virtio_gpu_update_cursor {
    GPU_CTRL_HDR hdr;
    GPU_CURSOR_POS pos;  /* update & move */
    ULONG resource_id;  /* update only */
    ULONG hot_x;        /* update only */
    ULONG hot_y;        /* update only */
    ULONG padding;
}GPU_UPDATE_CURSOR, *PGPU_UPDATE_CURSOR;
#pragma pack()

/* VIRTIO_GPU_CMD_GET_EDID */
#pragma pack(1)
typedef struct virtio_gpu_cmd_get_edid {
    GPU_CTRL_HDR hdr;
    ULONG scanout;
    ULONG padding;
}GPU_CMD_GET_EDID, *PGPU_CMD_GET_EDID;
#pragma pack()

/* VIRTIO_GPU_RESP_OK_EDID */
#pragma pack(1)
typedef struct virtio_gpu_resp_edid {
    GPU_CTRL_HDR hdr;
    ULONG size;
    ULONG padding;
    UCHAR edid[1024];
}GPU_RESP_EDID, *PGPU_RESP_EDID;
#pragma pack()

#define EDID_V1_BLOCK_SIZE         128


#pragma pack(push)
#pragma pack(1)

typedef struct _FEATURES_SUPPORT {
    UCHAR DefaultGTF                   : 1;
    UCHAR PreferredTimingNode          : 1;
    UCHAR StandardDefaultColorSpace    : 1;
    UCHAR DisplayType                  : 2;
    UCHAR ActiveOff                    : 1;
    UCHAR Suspend                      : 1;
    UCHAR Standby                      : 1;
} FEATURES_SUPPORT, *PFEATURES_SUPPORT;

typedef struct _COLOR_CHARACTERISTICS {
    UCHAR GreenYLow                    : 2;
    UCHAR GreenXLow                    : 2;
    UCHAR RedYLow                      : 2;
    UCHAR RedXLow                      : 2;
    UCHAR WhiteYLow                    : 2;
    UCHAR WhiteXLow                    : 2;
    UCHAR BlueYLow                     : 2;
    UCHAR BlueXLow                     : 2;
    UCHAR RedX;
    UCHAR RedY;
    UCHAR GreenX;
    UCHAR GreenY;
    UCHAR BlueX;
    UCHAR BlueY;
    UCHAR WhiteX;
    UCHAR WhiteY;
} COLOR_CHARACTERISTICS, *PCOLOR_CHARACTERISTICS;

typedef struct _ESTABLISHED_TIMINGS {
    UCHAR Timing_800x600_60            : 1;
    UCHAR Timing_800x600_56            : 1;
    UCHAR Timing_640x480_75            : 1;
    UCHAR Timing_640x480_72            : 1;
    UCHAR Timing_640x480_67            : 1;
    UCHAR Timing_640x480_60            : 1;
    UCHAR Timing_720x400_88            : 1;
    UCHAR Timing_720x400_70            : 1;

    UCHAR Timing_1280x1024_75          : 1;
    UCHAR Timing_1024x768_75           : 1;
    UCHAR Timing_1024x768_70           : 1;
    UCHAR Timing_1024x768_60           : 1;
    UCHAR Timing_1024x768_87           : 1;
    UCHAR Timing_832x624_75            : 1;
    UCHAR Timing_800x600_75            : 1;
    UCHAR Timing_800x600_72            : 1;
} ESTABLISHED_TIMINGS, *PESTABLISHED_TIMINGS;

typedef union _VIDEO_INPUT_DEFINITION {
    typedef struct _DIGITAL {
        UCHAR DviStandard              : 4;
        UCHAR ColorBitDepth            : 3;
        UCHAR Digital                  : 1;
    } DIGITAL;
    typedef struct _ANALOG {
        UCHAR VSyncSerration           : 1;
        UCHAR GreenVideoSync           : 1;
        UCHAR VompositeSync            : 1;
        UCHAR SeparateSync             : 1;
        UCHAR BlankToBlackSetup        : 1;
        UCHAR SignalLevelStandard      : 2;
        UCHAR Digital                  : 1;
    } ANALOG;
} VIDEO_INPUT_DEFINITION, * PVIDEO_INPUT_DEFINITION;

typedef struct _MANUFACTURER_TIMINGS {
    UCHAR Reserved                     : 7;
    UCHAR Timing_1152x870_75           : 1;
} MANUFACTURER_TIMINGS, * PMANUFACTURER_TIMINGS;

typedef struct _STANDARD_TIMING_DESCRIPTOR {
    UCHAR HorizontalActivePixels;
    UCHAR RefreshRate                  : 6;
    UCHAR ImageAspectRatio             : 2;
} STANDARD_TIMING_DESCRIPTOR, *PSTANDARD_TIMING_DESCRIPTOR;

typedef struct _EDID_DETAILED_DESCRIPTOR {
    USHORT PixelClock;
    UCHAR HorizontalActiveLow;
    UCHAR HorizontalBlankingLow;
    UCHAR HorizontalBlankingHigh       : 4;
    UCHAR horizontalActiveHigh         : 4;
    UCHAR VerticalActiveLow;
    UCHAR VerticalBlankingLow;
    UCHAR VerticalBlankingHigh         : 4;
    UCHAR VerticalActiveHigh           : 4;
    UCHAR HorizontalSyncOffsetLow;
    UCHAR HorizontalSyncPulseWidthLow;
    UCHAR VerticalSyncPulseWidthLow    : 4;
    UCHAR VerticalSyncOffsetLow        : 4;
    UCHAR VerticalSyncPulseWidthHigh   : 2;
    UCHAR VerticalSyncOffsetHigh       : 2;
    UCHAR HorizontalSyncPulseWidthHigh : 2;
    UCHAR HorizontalSyncOffsetHigh     : 2;
    UCHAR HorizontalImageSizeLow;
    UCHAR VerticalImageSizeLow;
    UCHAR VerticalImageSizeHigh        : 4;
    UCHAR HorizontalImageSizeHigh      : 4;
    UCHAR HorizontalBorder;
    UCHAR VerticalBorder;
    UCHAR StereoModeLow                : 1;
    UCHAR SignalPulsePolarity          : 1;
    UCHAR SignalSerrationPolarity      : 1;
    UCHAR SignalSync                   : 2;
    UCHAR StereoModeHigh               : 2;
    UCHAR Interlaced                   : 1;
}EDID_DETAILED_DESCRIPTOR, *PEDID_DETAILED_DESCRIPTOR;
#pragma pack(pop)


typedef struct _EDID_DATA_V1 {
    UCHAR Header [8];
    UCHAR VendorID[2];
    UCHAR ProductID[2];
    UCHAR SerialNumber[4];
    UCHAR WeekYearMFG[2];
    UCHAR Version[1];
    UCHAR Revision[1];
    VIDEO_INPUT_DEFINITION VideoInputDefinition[1];
    UCHAR  MaximumHorizontalImageSize[1];
    UCHAR  MaximumVerticallImageSize[1];
    UCHAR  DisplayTransferCharacteristics[1];
    FEATURES_SUPPORT FeaturesSupport;
    COLOR_CHARACTERISTICS ColorCharacteristics;
    ESTABLISHED_TIMINGS EstablishedTimings;
    MANUFACTURER_TIMINGS ManufacturerTimings;
    STANDARD_TIMING_DESCRIPTOR StandardTimings [8];
    EDID_DETAILED_DESCRIPTOR EDIDDetailedTimings [4];
    UCHAR ExtensionFlag[1];
    UCHAR Checksum[1];
} EDID_DATA_V1, * PEDID_DATA_V1;

#define VIRTIO_GPU_F_VIRGL 0
#define VIRTIO_GPU_F_EDID  1

#define ISR_REASON_DISPLAY 1
#define ISR_REASON_CURSOR  2
#define ISR_REASON_CHANGE  4

class IVioGpuAdapter;
class VioGpuAdapter;
