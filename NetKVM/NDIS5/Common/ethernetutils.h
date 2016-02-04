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


#define ETH_IS_LOCALLY_ADMINISTERED(Address) \
        (BOOLEAN)(((PUCHAR)(Address))[0] & ((UCHAR)0x02))

#define ETH_IS_EMPTY(Address) \
    ((((PUCHAR)(Address))[0] == ((UCHAR)0x00)) && (((PUCHAR)(Address))[1] == ((UCHAR)0x00)) && (((PUCHAR)(Address))[2] == ((UCHAR)0x00)) && (((PUCHAR)(Address))[3] == ((UCHAR)0x00)) && (((PUCHAR)(Address))[4] == ((UCHAR)0x00)) && (((PUCHAR)(Address))[5] == ((UCHAR)0x00)))

#define ETH_HAS_PRIO_HEADER(Address) \
    (((PUCHAR)(Address))[12] == ((UCHAR)0x81) && ((PUCHAR)(Address))[13] == ((UCHAR)0x00))

#include <pshpack1.h>
typedef struct _ETH_HEADER
{
    UCHAR   DstAddr[ETH_LENGTH_OF_ADDRESS];
    UCHAR   SrcAddr[ETH_LENGTH_OF_ADDRESS];
    USHORT  EthType;
} ETH_HEADER, *PETH_HEADER;
#include <poppack.h>

#define ETH_HEADER_SIZE                     (sizeof(ETH_HEADER))
#define ETH_MIN_PACKET_SIZE                 60
#define ETH_PRIORITY_HEADER_OFFSET          12
#define ETH_PRIORITY_HEADER_SIZE            4


static void FORCEINLINE SetPriorityData(UCHAR *pDest, ULONG priority, ULONG VlanID)
{
    pDest[0] = 0x81;
    pDest[2] = (UCHAR)(priority << 5);
    pDest[2] |= (UCHAR)(VlanID >> 8);
    pDest[3] |= (UCHAR)VlanID;
}

typedef enum _tag_eInspectedPacketType
{
    iptUnicast,
    iptBroadcast,
    iptMilticast,
    iptInvalid
}eInspectedPacketType;

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

static __inline USHORT swap_short(USHORT us)
{
    return (us << 8) | (us >> 8);
}


#endif
