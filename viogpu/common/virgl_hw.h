// Based on virglrenderer/src/virgl_hw.h
#pragma once

// Resource bind flags for kernel allocations
#define VIRGL_BIND_RENDER_TARGET (1 << 1)
#define VIRGL_BIND_SAMPLER_VIEW  (1 << 3)
#define VIRGL_BIND_DISPLAY_TARGET (1 << 7)
#define VIRGL_BIND_DISPLAY_TARGET (1 << 7)
#define VIRGL_BIND_SCANOUT       (1 << 18)

// Blt command specification for kernel BLTs
#define VIRGL_CMD0(cmd, obj, len) ((cmd) | ((obj) << 8) | ((len) << 16))
#define VIRGL_CCMD_RESOURCE_COPY_REGION 17
#define VIRGL_CMD_RESOURCE_COPY_REGION_SIZE 13

// Flags for the driver about resource behaviour
#define VIRGL_RESOURCE_Y_0_TOP (1 << 0)
#define VIRGL_RESOURCE_FLAG_MAP_PERSISTENT (1 << 1)
#define VIRGL_RESOURCE_FLAG_MAP_COHERENT   (1 << 2)

