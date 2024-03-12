/*
 * This file contains virtio-mem types.
 *
 * Copyright (c) 2022 Red Hat, Inc.
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
#include "virtio.h"

// 
// Types and defines taken directly from the Linux header file.
//

/* --- virtio-mem: configuration --- */

#pragma pack(push, 1)
typedef struct _virtio_mem_config {
	/* Block size and alignment. Cannot change. */
	__u64 block_size;
	/* Valid with VIRTIO_MEM_F_ACPI_PXM. Cannot change. */
	__u16 node_id;
	__u8 padding[6];
	/* Start address of the memory region. Cannot change. */
	__u64 addr;
	/* Region size (maximum). Cannot change. */
	__u64 region_size;
	/*
	 * Currently usable region size. Can grow up to region_size. Can
	 * shrink due to VIRTIO_MEM_REQ_UNPLUG_ALL (in which case no config
	 * update will be sent).
	 */
	__u64 usable_region_size;
	/*
	 * Currently used size. Changes due to plug/unplug requests, but no
	 * config updates will be sent.
	 */
	__u64 plugged_size;
	/* Requested size. New plug requests cannot exceed it. Can change. */
	__u64 requested_size;
} virtio_mem_config;
#pragma pack(pop)

#pragma pack(push, 1)
struct virtio_mem_req_plug {
	__virtio64 addr;
	__virtio16 nb_blocks;
	__virtio16 padding[3];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct virtio_mem_req_unplug {
	__virtio64 addr;
	__virtio16 nb_blocks;
	__virtio16 padding[3];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct virtio_mem_req_state {
	__virtio64 addr;
	__virtio16 nb_blocks;
	__virtio16 padding[3];
};
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _virtio_mem_req {
	__virtio16 type;
	__virtio16 padding[3];

	union {
		struct virtio_mem_req_plug plug;
		struct virtio_mem_req_unplug unplug;
		struct virtio_mem_req_state state;
	} u;
}virtio_mem_req;
#pragma pack(pop)

#pragma pack(push, 1)
struct virtio_mem_resp_state {
	__virtio16 state;
};
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _virtio_mem_resp {
	__virtio16 type;
	__virtio16 padding[3];

	union {
		struct virtio_mem_resp_state state;
	} u;
}virtio_mem_resp;
#pragma pack(pop)

/*-- - virtio-mem: feature bits-- - */

/* node_id is an ACPI PXM and is valid */
#define VIRTIO_MEM_F_ACPI_PXM		0

#define VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE 1

/* --- virtio-mem: guest -> host requests --- */

/* request to plug memory blocks */
#define VIRTIO_MEM_REQ_PLUG			0
/* request to unplug memory blocks */
#define VIRTIO_MEM_REQ_UNPLUG			1
/* request to unplug all blocks and shrink the usable size */
#define VIRTIO_MEM_REQ_UNPLUG_ALL		2
/* request information about the plugged state of memory blocks */
#define VIRTIO_MEM_REQ_STATE			3

/* --- virtio-mem: host -> guest response --- */

/*
 * Request processed successfully, applicable for
 * - VIRTIO_MEM_REQ_PLUG
 * - VIRTIO_MEM_REQ_UNPLUG
 * - VIRTIO_MEM_REQ_UNPLUG_ALL
 * - VIRTIO_MEM_REQ_STATE
 */
#define VIRTIO_MEM_RESP_ACK			0
 /*
  * Request denied - e.g. trying to plug more than requested, applicable for
  * - VIRTIO_MEM_REQ_PLUG
  */
#define VIRTIO_MEM_RESP_NACK			1
  /*
   * Request cannot be processed right now, try again later, applicable for
   * - VIRTIO_MEM_REQ_PLUG
   * - VIRTIO_MEM_REQ_UNPLUG
   * - VIRTIO_MEM_REQ_UNPLUG_ALL
   */
#define VIRTIO_MEM_RESP_BUSY			2
   /*
	* Error in request (e.g. addresses/alignment), applicable for
	* - VIRTIO_MEM_REQ_PLUG
	* - VIRTIO_MEM_REQ_UNPLUG
	* - VIRTIO_MEM_REQ_STATE
	*/
#define VIRTIO_MEM_RESP_ERROR			3


	/* State of memory blocks is "plugged" */
#define VIRTIO_MEM_STATE_PLUGGED		0
/* State of memory blocks is "unplugged" */
#define VIRTIO_MEM_STATE_UNPLUGGED		1
/* State of memory blocks is "mixed" */
#define VIRTIO_MEM_STATE_MIXED			2

