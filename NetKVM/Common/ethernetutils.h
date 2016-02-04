/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: ethernetutils.h
 *
 * Contains common Ethernet-related definition, not defined in NDIS
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef _ETHERNET_UTILS_H
#define _ETHERNET_UTILS_H

// assuming <ndis.h> included
#include <linux/if_ether.h>

#define ETH_IS_LOCALLY_ADMINISTERED(Address) \
        (BOOLEAN)(((PUCHAR)(Address))[0] & ((UCHAR)0x02))

#define ETH_IS_EMPTY(Address) \
    ((((PUCHAR)(Address))[0] == ((UCHAR)0x00)) && (((PUCHAR)(Address))[1] == ((UCHAR)0x00)) && (((PUCHAR)(Address))[2] == ((UCHAR)0x00)) && (((PUCHAR)(Address))[3] == ((UCHAR)0x00)) && (((PUCHAR)(Address))[4] == ((UCHAR)0x00)) && (((PUCHAR)(Address))[5] == ((UCHAR)0x00)))

#define ETH_HAS_PRIO_HEADER(Address) \
    (((PUCHAR)(Address))[12] == ((UCHAR)0x81) && ((PUCHAR)(Address))[13] == ((UCHAR)0x00))

#define PRIO_HEADER_ETH_TYPE (0x8100)

#include <pshpack1.h>
typedef struct _ETH_HEADER
{
    UCHAR   DstAddr[ETH_ALEN];
    UCHAR   SrcAddr[ETH_ALEN];
    USHORT  EthType;
} ETH_HEADER, *PETH_HEADER;

typedef struct _VLAN_HEADER
{
    USHORT  TCI;
    USHORT  EthType;
} VLAN_HEADER, *PVLAN_HEADER;
#include <poppack.h>

typedef ULONG IPV6_ADDRESS[4];

#define ETH_HEADER_SIZE                     (sizeof(ETH_HEADER))
#define ETH_MIN_PACKET_SIZE                 60
#define ETH_PRIORITY_HEADER_OFFSET          12
#define ETH_PRIORITY_HEADER_SIZE            4

#define TCP_HEADER_LENGTH(Header) ((Header->tcp_flags & 0xF0) >> 2)

// IP Header RFC 791
typedef struct _tagIPv4Header {
    UCHAR       ip_verlen;             // length in 32-bit units(low nibble), version (high nibble)
    UCHAR       ip_tos;                // Type of service
    USHORT      ip_length;             // Total length
    USHORT      ip_id;                 // Identification
    USHORT      ip_offset;             // fragment offset and flags
    UCHAR       ip_ttl;                // Time to live
    UCHAR       ip_protocol;           // Protocol
    USHORT      ip_xsum;               // Header checksum
    ULONG       ip_src;                // Source IP address
    ULONG       ip_dest;               // Destination IP address
} IPv4Header;

// IPv6 Header RFC 2460 (40 bytes)
typedef struct _tagIPv6Header {
    UCHAR    ip6_ver_tc;            // traffic class(low nibble), version (high nibble)
    UCHAR    ip6_tc_fl;             // traffic class(high nibble), flow label
    USHORT   ip6_fl;                // flow label, the rest
    USHORT   ip6_payload_len;       // length of following headers and payload
    UCHAR    ip6_next_header;       // next header type
    UCHAR    ip6_hoplimit;          // hop limit
    IPV6_ADDRESS ip6_src_address;
    IPV6_ADDRESS ip6_dst_address;
} IPv6Header;

typedef union
{
    IPv6Header v6;
    IPv4Header v4;
} IPHeader;

#define IPV6_HEADER_MIN_SIZE                (sizeof(IPv6Header))

// TCP header RFC 793
typedef struct _tagTCPHeader {
    USHORT      tcp_src;                // Source port
    USHORT      tcp_dest;               // Destination port
    ULONG       tcp_seq;                // Sequence number
    ULONG       tcp_ack;                // Ack number
    USHORT      tcp_flags;              // header length and flags
    USHORT      tcp_window;             // Window size
    USHORT      tcp_xsum;               // Checksum
    USHORT      tcp_urgent;             // Urgent
}TCPHeader;


// UDP Header RFC 768
typedef struct _tagUDPHeader {
    USHORT      udp_src;                // Source port
    USHORT      udp_dest;               // Destination port
    USHORT      udp_length;             // length of datagram
    USHORT      udp_xsum;               // checksum
}UDPHeader;



#define TCP_CHECKSUM_OFFSET     16
#define UDP_CHECKSUM_OFFSET     6
#define MAX_IPV4_HEADER_SIZE    60
#define MAX_TCP_HEADER_SIZE     60
#define MAX_IP4_DATAGRAM_SIZE   65535

static __inline USHORT swap_short(USHORT us)
{
    return (us << 8) | (us >> 8);
}


#endif
