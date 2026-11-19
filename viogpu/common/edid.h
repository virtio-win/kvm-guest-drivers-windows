#pragma once

#include "viogpu.h"

// The EDID the host gives for a scanout (VIRTIO_GPU_CMD_GET_EDID): a VESA E-EDID 1.x base block followed by
// extension blocks. Of the extensions, CTA-861 and DisplayID describe video modes; the others are skipped.

#define EDID_BLOCK_SIZE        EDID_V1_BLOCK_SIZE

// Byte offsets in the base block.
#define EDID_OFFSET_VERSION    18
#define EDID_OFFSET_REVISION   19
#define EDID_OFFSET_H_SIZE_CM  21
#define EDID_OFFSET_V_SIZE_CM  22
#define EDID_OFFSET_FEATURES   24
#define EDID_OFFSET_EXTENSIONS 126
#define EDID_OFFSET_CHECKSUM   127

// The bytes at edid that form a valid EDID: the base block, if its header, version and checksum are right, and the
// extensions it announces, up to the first one that does not fit in size or fails its checksum. 0 when the base
// block is not valid. Always a multiple of EDID_BLOCK_SIZE.
ULONG EdidValidSize(const UCHAR *edid, ULONG size);

// Makes the block's last byte its checksum.
void EdidSetChecksum(UCHAR *block);

// The progressive resolutions an EDID describes, each once, in the order the EDID lists them: established and
// standard timings, detailed timings, and the CTA-861 and DisplayID extensions. size must come from
// EdidValidSize. Returns how many were stored in modes, at most maxModes. preferred receives the timing the
// display prefers (the base block's first detailed timing, or a DisplayID timing marked preferred), or 0x0.
ULONG EdidGetModes(const UCHAR *edid, ULONG size, PVIOGPU_DISP_MODE modes, ULONG maxModes, PVIOGPU_DISP_MODE preferred);

extern UCHAR g_gpu_edid[EDID_V1_BLOCK_SIZE];
