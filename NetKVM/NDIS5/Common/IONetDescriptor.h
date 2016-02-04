/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: IONetDescriptor.h
 *
 * This file contains common guest/host definition, related
 * to VirtIO network adapter
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef IONETDESCRIPTOR_H
#define IONETDESCRIPTOR_H

#pragma pack (push)
#pragma pack (1)
/* This is the first element of the scatter-gather list.  If you don't
 * specify GSO or CSUM features, you can simply ignore the header. */
typedef struct _tagvirtio_net_hdr
{
#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1   // Use csum_start, csum_offset
#define VIRTIO_NET_HDR_F_DATA_VALID 2   // Host checked checksum, no need to recheck
    u8 flags;
#define VIRTIO_NET_HDR_GSO_NONE     0   // Not a GSO frame
#define VIRTIO_NET_HDR_GSO_TCPV4    1   // GSO frame, IPv4 TCP (TSO)
#define VIRTIO_NET_HDR_GSO_UDP      3   // GSO frame, IPv4 UDP (UFO)
#define VIRTIO_NET_HDR_GSO_TCPV6    4   // GSO frame, IPv6 TCP
#define VIRTIO_NET_HDR_GSO_ECN      0x80    // TCP has ECN set
    u8 gso_type;
    u16 hdr_len;                        // Ethernet + IP + tcp/udp hdrs
    u16 gso_size;                       // Bytes to append to gso_hdr_len per frame
    u16 csum_start;                     // Position to start checksumming from
    u16 csum_offset;                    // Offset after that to place checksum
}virtio_net_hdr_basic;

typedef struct _tagvirtio_net_hdr_ext
{
    virtio_net_hdr_basic BasicHeader;
    u16 nBuffers;
}virtio_net_hdr_ext;

/*
 * Control virtqueue data structures
 *
 * The control virtqueue expects a header in the first sg entry
 * and an ack/status response in the last entry.  Data for the
 * command goes in between.
 */
typedef struct tag_virtio_net_ctrl_hdr {
    u8 class_of_command;
    u8 cmd;
}virtio_net_ctrl_hdr;

typedef u8 virtio_net_ctrl_ack;

#define VIRTIO_NET_OK     0
#define VIRTIO_NET_ERR    1

/*
 * Control the RX mode, ie. promisucous, allmulti, etc...
 * All commands require an "out" sg entry containing a 1 byte
 * state value, zero = disable, non-zero = enable.  Commands
 * 0 and 1 are supported with the VIRTIO_NET_F_CTRL_RX feature.
 * Commands 2-5 are added with VIRTIO_NET_F_CTRL_RX_EXTRA.
 */
#define VIRTIO_NET_CTRL_RX_MODE    0
 #define VIRTIO_NET_CTRL_RX_MODE_PROMISC      0
 #define VIRTIO_NET_CTRL_RX_MODE_ALLMULTI     1
 #define VIRTIO_NET_CTRL_RX_MODE_ALLUNI       2
 #define VIRTIO_NET_CTRL_RX_MODE_NOMULTI      3
 #define VIRTIO_NET_CTRL_RX_MODE_NOUNI        4
 #define VIRTIO_NET_CTRL_RX_MODE_NOBCAST      5

/*
 * Control the MAC filter table.
 *
 * The MAC filter table is managed by the hypervisor, the guest should
 * assume the size is infinite.  Filtering should be considered
 * non-perfect, ie. based on hypervisor resources, the guest may
 * received packets from sources not specified in the filter list.
 *
 * In addition to the class/cmd header, the TABLE_SET command requires
 * two out scatterlists.  Each contains a 4 byte count of entries followed
 * by a concatenated byte stream of the ETH_ALEN MAC addresses.  The
 * first sg list contains unicast addresses, the second is for multicast.
 * This functionality is present if the VIRTIO_NET_F_CTRL_RX feature
 * is available.
 */
#define ETH_ALEN    6

struct virtio_net_ctrl_mac {
    u32 entries;
    // follows
    //u8 macs[][ETH_ALEN];
};
#define VIRTIO_NET_CTRL_MAC                  1
  #define VIRTIO_NET_CTRL_MAC_TABLE_SET        0

/*
 * Control VLAN filtering
 *
 * The VLAN filter table is controlled via a simple ADD/DEL interface.
 * VLAN IDs not added may be filterd by the hypervisor.  Del is the
 * opposite of add.  Both commands expect an out entry containing a 2
 * byte VLAN ID.  VLAN filterting is available with the
 * VIRTIO_NET_F_CTRL_VLAN feature bit.
 */
#define VIRTIO_NET_CTRL_VLAN                 2
  #define VIRTIO_NET_CTRL_VLAN_ADD             0
  #define VIRTIO_NET_CTRL_VLAN_DEL             1


#pragma pack (pop)

#endif
