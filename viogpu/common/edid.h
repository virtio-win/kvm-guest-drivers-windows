/*
 * Copyright (C) 2024 Red Hat, Inc.
 * Copyright (C) 2026 Zededa, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
 *             Mikhail Malyshev <mike.malyshev@gmail.com>
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
// display prefers (the base block's first detailed timing when it is progressive and the EDID is 1.4, or 1.3 with the
// preferred-timing feature bit; else a DisplayID timing marked preferred), or 0x0.
ULONG EdidGetModes(const UCHAR *edid, ULONG size, PVIOGPU_DISP_MODE modes, ULONG maxModes, PVIOGPU_DISP_MODE preferred);

extern VIOGPU_DISP_MODE gpu_disp_modes[64];
extern UCHAR g_gpu_edid[EDID_V1_BLOCK_SIZE];
