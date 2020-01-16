/*
 * Contains common Ethernet-related definition, not defined in NDIS
 *
 * Copyright (c) 2008-2017 Red Hat, Inc.
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

// note that in this project VLAN header (for both TX and RX) does not describe
// 802.1Q header, but 4-bytes structure following Ethetnet header, i.e
// 2-bytes TCI field and 2-bytes real protocol information
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
#define ETH_HARDWARE_ADDRESS_SIZE           ETH_ALEN
#define ETH_IPV4_ADDRESS_SIZE               4
#define ETH_IPV4_ADDRESS_SIZE               4
#define ETH_IPV6_USHORT_ADDRESS_SIZE        8
#define ETH_HARDWARE_TYPE                   1
#define ETH_ETHER_TYPE_ARP                  0x0806
#define ETH_IP_PROTOCOL_TYPE                0x0800
#define ETH_ARP_OPERATION_TYPE_REQUEST      1
#define ETH_ARP_OPERATION_TYPE_REPLY        2
#define ETH_ETHER_TYPE_IPV6                 0x86DD
#define ETH_IPV6_ICMPV6_PROTOCOL            0x3A
#define ETH_ICMPV6_TYPE_NSM                 0x87
#define ETH_IPV6_VERSION_TRAFFICCONTROL_FLOWLABEL 0x6E000000

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
    union
    {
        ULONG       ip_src;                // Source IP address
        UCHAR       ip_srca[4];
    };
    union
    {
        ULONG       ip_dest;               // Destination IP address
        UCHAR       ip_desta[4];
    };
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

#include <pshpack1.h>

typedef struct _hardware_address {
    UINT8 address[ETH_HARDWARE_ADDRESS_SIZE];
} hardware_address;

typedef struct _ipv4_address {
    UINT32 address;
} ipv4_address;

typedef struct _ipv6_address {
    USHORT address[ETH_IPV6_USHORT_ADDRESS_SIZE];
} ipv6_address;

typedef struct _EthernetFrame {
    hardware_address target_hardware_address;
    hardware_address sender_hardware_address;
    UINT16 ether_type;
} EthernetFrame;

// Internet Protocol (IPv4) over Ethernet ARP packet
typedef struct _IPv4OverEthernetARPPacket {
    UINT16 hardware_type;
    UINT16 protocol_type;
    UINT8 hardware_address_length;
    UINT8 protocol_address_length;
    UINT16 operation;
    hardware_address sender_hardware_address;
    ipv4_address sender_ipv4_address;
    hardware_address target_hardware_address;
    ipv4_address target_ipv4_address;
} IPv4OverEthernetARPPacket;

typedef struct _EthernetArpFrame {
    EthernetFrame frame;
    IPv4OverEthernetARPPacket data;
} EthernetArpFrame;

// Internet Protocol (IPv6) Neighbor Solicitation Message over Ethernet
typedef struct _ICMPv6NSM {
    UINT8 type;
    UINT8 code;
    UINT16 checksum;
    UINT32 reserved;
    ipv6_address target_address;
} ICMPv6NSM;

typedef struct _IPv6NSMOverEthernetPacket {
    UINT32 version_trafficclass_flowlabel; // Version is 4 bits, Traffic Class is 8 and Flow label is 20
    UINT16 payload_length;
    UINT8 next_header;
    UINT8 hop_limit;
    ipv6_address source_address;
    ipv6_address destination_address;
    ICMPv6NSM nsm;
} IPv6NSMOverEthernetPacket;

// This struct is used for calculating the checksum
typedef struct _ICMPv6PseudoHeader {
    ipv6_address source_address;
    ipv6_address destination_address;
    UINT32 icmpv6_length;
    UINT16 zeropad0 = 0;
    UINT8 zeropad1 = 0;
    UINT8 next_header;
    ICMPv6NSM nsm; //data
} ICMPv6PseudoHeader;

typedef struct _EthernetNSMFrame {
    EthernetFrame frame;
    IPv6NSMOverEthernetPacket data;
} EthernetNSMFrame;

#include <poppack.h>

#define TCP_CHECKSUM_OFFSET     16
#define UDP_CHECKSUM_OFFSET     6
#define MAX_IPV4_HEADER_SIZE    60
#define MAX_TCP_HEADER_SIZE     60
#define MAX_IP4_DATAGRAM_SIZE   65535

static __inline USHORT swap_short(USHORT us)
{
    return (us << 8) | (us >> 8);
}

#if !defined(_ARM64_)
#define ETH_COMPARE_NETWORK_ADDRESSES_EQ_SAFE ETH_COMPARE_NETWORK_ADDRESSES_EQ
#else
#define ETH_COMPARE_NETWORK_ADDRESSES_EQ_SAFE(a1, a2, res) *(res) = RtlCompareMemory((a1), (a2), ETH_ALEN) != ETH_ALEN
#endif

#endif
