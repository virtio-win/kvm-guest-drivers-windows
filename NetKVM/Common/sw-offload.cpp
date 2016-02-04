/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: sw-offload.c
 *
 * This file contains SW Implementation of checksum computation for IP,TCP,UDP
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "ndis56common.h"
#include "kdebugprint.h"

// till IP header size is 8 bit
#define MAX_SUPPORTED_IPV6_HEADERS  (256 - 4)

// IPv6 Header RFC 2460 (n*8 bytes)
typedef struct _tagIPv6ExtHeader {
    UCHAR       ip6ext_next_header;     // next header type
    UCHAR       ip6ext_hdr_len;         // length of this header in 8 bytes unit, not including first 8 bytes
    USHORT      options;                // 
} IPv6ExtHeader;

// IP Pseudo Header RFC 768
typedef struct _tagIPv4PseudoHeader {
    ULONG       ipph_src;               // Source address
    ULONG       ipph_dest;              // Destination address
    UCHAR       ipph_zero;              // 0
    UCHAR       ipph_protocol;          // TCP/UDP
    USHORT      ipph_length;            // TCP/UDP length
}tIPv4PseudoHeader;

// IPv6 Pseudo Header RFC 2460
typedef struct _tagIPv6PseudoHeader {
    IPV6_ADDRESS ipph_src;              // Source address
    IPV6_ADDRESS ipph_dest;             // Destination address
    ULONG        ipph_length;               // TCP/UDP length
    UCHAR        z1;                // 0
    UCHAR        z2;                // 0
    UCHAR        z3;                // 0
    UCHAR        ipph_protocol;             // TCP/UDP
}tIPv6PseudoHeader;

// IP v6 extension header option
typedef struct _tagIP6_EXT_HDR_OPTION
{
    UCHAR Type;
    UCHAR Length;
} IP6_EXT_HDR_OPTION, *PIP6_EXT_HDR_OPTION;

#define IP6_EXT_HDR_OPTION_PAD1         (0)
#define IP6_EXT_HDR_OPTION_HOME_ADDR    (201)

// IP v6 routing header
typedef struct _tagIP6_TYPE2_ROUTING_HEADER
{
    UCHAR           NextHdr;
    UCHAR           HdrLen;
    UCHAR           RoutingType;
    UCHAR           SegmentsLeft;
    ULONG           Reserved;
    IPV6_ADDRESS    Address;
} IP6_TYPE2_ROUTING_HEADER, *PIP6_TYPE2_ROUTING_HEADER;

#define PROTOCOL_TCP                    6
#define PROTOCOL_UDP                    17

#define IP_HEADER_LENGTH(pHeader)       (((pHeader)->ip_verlen & 0x0F) << 2)
#define IP_HEADER_VERSION(pHeader)      (((pHeader)->ip_verlen & 0xF0) >> 4)
#define IP_HEADER_IS_FRAGMENT(pHeader)  (((pHeader)->ip_offset & ~0xC0) != 0)

#define IP6_HEADER_VERSION(pHeader)     (((pHeader)->ip6_ver_tc & 0xF0) >> 4)

#define ETH_GET_VLAN_HDR(ethHdr)        ((PVLAN_HEADER) RtlOffsetToPointer(ethHdr, ETH_PRIORITY_HEADER_OFFSET))
#define VLAN_GET_USER_PRIORITY(vlanHdr) ( (((PUCHAR)(vlanHdr))[2] & 0xE0) >> 5 )
#define VLAN_GET_VLAN_ID(vlanHdr)       ( ((USHORT) (((PUCHAR)(vlanHdr))[2] & 0x0F) << 8) | ( ((PUCHAR)(vlanHdr))[3] ) )

#define ETH_PROTO_IP4 (0x0800)
#define ETH_PROTO_IP6 (0x86DD)

#define IP6_HDR_HOP_BY_HOP        (0)
#define IP6_HDR_ROUTING           (43)
#define IP6_HDR_FRAGMENT          (44)
#define IP6_HDR_ESP               (50)
#define IP6_HDR_AUTHENTICATION    (51)
#define IP6_HDR_NONE              (59)
#define IP6_HDR_DESTINATON        (60)
#define IP6_HDR_MOBILITY          (135)

#define IP6_EXT_HDR_GRANULARITY   (8)

static UINT32 RawCheckSumCalculator(PVOID buffer, ULONG len)
{
    UINT32 val = 0;
    PUSHORT pus = (PUSHORT)buffer;
    ULONG count = len >> 1;
    while (count--) val += *pus++;
    if (len & 1) val += (USHORT)*(PUCHAR)pus;
    return val;
}

static __inline USHORT RawCheckSumFinalize(UINT32 val)
{
    val = (((val >> 16) | (val << 16)) + val) >> 16;
    return (USHORT)~val;
}

static __inline USHORT CheckSumCalculatorFlat(PVOID buffer, ULONG len)
{
    return RawCheckSumFinalize(RawCheckSumCalculator(buffer, len));
}

static __inline USHORT CheckSumCalculator(tCompletePhysicalAddress *pDataPages, ULONG ulStartOffset, ULONG len)
{
    tCompletePhysicalAddress *pCurrentPage = &pDataPages[0];
    ULONG ulCurrPageOffset = 0;
    UINT32 u32RawCSum = 0;

    while(ulStartOffset > 0)
    {
        ulCurrPageOffset = min(pCurrentPage->size, ulStartOffset);

        if(ulCurrPageOffset < ulStartOffset)
            pCurrentPage++;

        ulStartOffset -= ulCurrPageOffset;
    }

    while(len > 0)
    {
        PVOID pCurrentPageDataStart = RtlOffsetToPointer(pCurrentPage->Virtual, ulCurrPageOffset);
        ULONG ulCurrentPageDataLength = min(len, pCurrentPage->size - ulCurrPageOffset);

        u32RawCSum += RawCheckSumCalculator(pCurrentPageDataStart, ulCurrentPageDataLength);
        pCurrentPage++;
        ulCurrPageOffset = 0;
        len -= ulCurrentPageDataLength;
    }

    return RawCheckSumFinalize(u32RawCSum);
}


/******************************************
    IP header checksum calculator
*******************************************/
static __inline VOID CalculateIpChecksum(IPv4Header *pIpHeader)
{
    pIpHeader->ip_xsum = 0;
    pIpHeader->ip_xsum = CheckSumCalculatorFlat(pIpHeader, IP_HEADER_LENGTH(pIpHeader));
}

static __inline tTcpIpPacketParsingResult
ProcessTCPHeader(tTcpIpPacketParsingResult _res, PVOID pIpHeader, ULONG len, USHORT ipHeaderSize)
{
    ULONG tcpipDataAt;
    tTcpIpPacketParsingResult res = _res;
    tcpipDataAt = ipHeaderSize + sizeof(TCPHeader);
    res.TcpUdp = ppresIsTCP;

    if (len >= tcpipDataAt)
    {
        TCPHeader *pTcpHeader = (TCPHeader *)RtlOffsetToPointer(pIpHeader, ipHeaderSize);
        res.xxpStatus = ppresXxpKnown;
        res.xxpFull = TRUE;
        tcpipDataAt = ipHeaderSize + TCP_HEADER_LENGTH(pTcpHeader);
        res.XxpIpHeaderSize = tcpipDataAt;
    }
    else
    {
        DPrintf(2, ("tcp: %d < min headers %d\n", len, tcpipDataAt));
        res.xxpFull = FALSE;
        res.xxpStatus = ppresXxpIncomplete;
    }
    return res;
}

static __inline tTcpIpPacketParsingResult
ProcessUDPHeader(tTcpIpPacketParsingResult _res, PVOID pIpHeader, ULONG len, USHORT ipHeaderSize)
{
    tTcpIpPacketParsingResult res = _res;
    ULONG udpDataStart = ipHeaderSize + sizeof(UDPHeader);
    res.TcpUdp = ppresIsUDP;
    res.XxpIpHeaderSize = udpDataStart;
    if (len >= udpDataStart)
    {
        UDPHeader *pUdpHeader = (UDPHeader *)RtlOffsetToPointer(pIpHeader, ipHeaderSize);
        USHORT datagramLength = swap_short(pUdpHeader->udp_length);
        res.xxpStatus = ppresXxpKnown;
        res.xxpFull = TRUE;
        // may be full or not, but the datagram length is known
        DPrintf(2, ("udp: len %d, datagramLength %d\n", len, datagramLength));
    }
    else
    {
        res.xxpFull = FALSE;
        res.xxpStatus = ppresXxpIncomplete;
    }
    return res;
}

static __inline tTcpIpPacketParsingResult
QualifyIpPacket(IPHeader *pIpHeader, ULONG len, BOOLEAN verifyLength)
{
    tTcpIpPacketParsingResult res;
    res.value = 0;

    if (len < 4)
    {
        res.ipStatus = ppresNotIP;
        return res;
    }

    UCHAR  ver_len = pIpHeader->v4.ip_verlen;
    UCHAR  ip_version = (ver_len & 0xF0) >> 4;
    USHORT ipHeaderSize = 0;
    USHORT fullLength = 0;
    res.value = 0;

    if (ip_version == 4)
    {
        if (len < sizeof(IPv4Header))
        {
            res.ipStatus = ppresNotIP;
            return res;
        }
        ipHeaderSize = (ver_len & 0xF) << 2;
        fullLength = swap_short(pIpHeader->v4.ip_length);
        DPrintf(3, ("ip_version %d, ipHeaderSize %d, protocol %d, iplen %d, L2 payload length %d\n",
            ip_version, ipHeaderSize, pIpHeader->v4.ip_protocol, fullLength, len));

        res.ipStatus = (ipHeaderSize >= sizeof(IPv4Header)) ? ppresIPV4 : ppresNotIP;
        if (res.ipStatus == ppresNotIP)
        {
            return res;
        }

        if (ipHeaderSize >= fullLength || ( verifyLength && len < fullLength))
        {
            DPrintf(2, ("[%s] - truncated packet - ip_version %d, ipHeaderSize %d, protocol %d, iplen %d, L2 payload length %d, verify = %s\n", __FUNCTION__,
                ip_version, ipHeaderSize, pIpHeader->v4.ip_protocol, fullLength, len, (verifyLength ? "true" : "false")));
            res.ipCheckSum = ppresIPTooShort;
            return res;
        }
    }
    else if (ip_version == 6)
    {
        if (len < sizeof(IPv6Header))
        {
            res.ipStatus = ppresNotIP;
            return res;
        }

        UCHAR nextHeader = pIpHeader->v6.ip6_next_header;
        BOOLEAN bParsingDone = FALSE;
        ipHeaderSize = sizeof(pIpHeader->v6);
        res.ipStatus = ppresIPV6;
        res.ipCheckSum = ppresCSOK;
        fullLength = swap_short(pIpHeader->v6.ip6_payload_len);
        fullLength += ipHeaderSize;

        if (verifyLength && (len < fullLength))
        {
            res.ipStatus = ppresNotIP;
            return res;
        }
        while (nextHeader != 59)
        {
            IPv6ExtHeader *pExt;
            switch (nextHeader)
            {
                case PROTOCOL_TCP:
                    bParsingDone = TRUE;
                    res.xxpStatus = ppresXxpKnown;
                    res.TcpUdp = ppresIsTCP;
                    res.xxpFull = len >= fullLength ? 1 : 0;
                    res = ProcessTCPHeader(res, pIpHeader, len, ipHeaderSize);
                    break;
                case PROTOCOL_UDP:
                    bParsingDone = TRUE;
                    res.xxpStatus = ppresXxpKnown;
                    res.TcpUdp = ppresIsUDP;
                    res.xxpFull = len >= fullLength ? 1 : 0;
                    res = ProcessUDPHeader(res, pIpHeader, len, ipHeaderSize);
                    break;
                    //existing extended headers
                case 0:
                    __fallthrough;
                case 60:
                    __fallthrough;
                case 43:
                    __fallthrough;
                case 44:
                    __fallthrough;
                case 51:
                    __fallthrough;
                case 50:
                    __fallthrough;
                case 135:
                    if (len >= ((ULONG)ipHeaderSize + 8))
                    {
                        pExt = (IPv6ExtHeader *)((PUCHAR)pIpHeader + ipHeaderSize);
                        nextHeader = pExt->ip6ext_next_header;
                        ipHeaderSize += 8;
                        ipHeaderSize += pExt->ip6ext_hdr_len * 8;
                    }
                    else
                    {
                        DPrintf(0, ("[%s] ERROR: Break in the middle of ext. headers(len %d, hdr > %d)\n", __FUNCTION__, len, ipHeaderSize));
                        res.ipStatus = ppresNotIP;
                        bParsingDone = TRUE;
                    }
                    break;
                    //any other protocol
                default:
                    res.xxpStatus = ppresXxpOther;
                    bParsingDone = TRUE;
                    break;
            }
            if (bParsingDone)
                break;
        }
        if (ipHeaderSize <= MAX_SUPPORTED_IPV6_HEADERS)
        {
            DPrintf(3, ("ip_version %d, ipHeaderSize %d, protocol %d, iplen %d\n",
                ip_version, ipHeaderSize, nextHeader, fullLength));
            res.ipHeaderSize = ipHeaderSize;
        }
        else
        {
            DPrintf(0, ("[%s] ERROR: IP chain is too large (%d)\n", __FUNCTION__, ipHeaderSize));
            res.ipStatus = ppresNotIP;
        }
    }
    
    if (res.ipStatus == ppresIPV4)
    {
        res.ipHeaderSize = ipHeaderSize;

        // bit "more fragments" or fragment offset mean the packet is fragmented
        res.IsFragment = (pIpHeader->v4.ip_offset & ~0xC0) != 0;
        switch (pIpHeader->v4.ip_protocol)
        {
            case PROTOCOL_TCP:
            {
                res = ProcessTCPHeader(res, pIpHeader, len, ipHeaderSize);
            }
            break;
        case PROTOCOL_UDP:
            {
                res = ProcessUDPHeader(res, pIpHeader, len, ipHeaderSize);
            }
            break;
        default:
            res.xxpStatus = ppresXxpOther;
            break;
        }
    }
    return res;
}

static __inline USHORT GetXxpHeaderAndPayloadLen(IPHeader *pIpHeader, tTcpIpPacketParsingResult res)
{
    if (res.ipStatus == ppresIPV4)
    {
        USHORT headerLength = IP_HEADER_LENGTH(&pIpHeader->v4);
        USHORT len = swap_short(pIpHeader->v4.ip_length);
        return len - headerLength;          
    }
    if (res.ipStatus == ppresIPV6)
    {
        USHORT fullLength = swap_short(pIpHeader->v6.ip6_payload_len);
        return fullLength + sizeof(pIpHeader->v6) - (USHORT)res.ipHeaderSize;
    }
    return 0;
}

static __inline USHORT CalculateIpv4PseudoHeaderChecksum(IPv4Header *pIpHeader, USHORT headerAndPayloadLen)
{
    tIPv4PseudoHeader ipph;
    USHORT checksum;
    ipph.ipph_src  = pIpHeader->ip_src;
    ipph.ipph_dest = pIpHeader->ip_dest;
    ipph.ipph_zero = 0;
    ipph.ipph_protocol = pIpHeader->ip_protocol;
    ipph.ipph_length = swap_short(headerAndPayloadLen);
    checksum = CheckSumCalculatorFlat(&ipph, sizeof(ipph));
    return ~checksum;
}


static __inline USHORT CalculateIpv6PseudoHeaderChecksum(IPv6Header *pIpHeader, USHORT headerAndPayloadLen)
{
    tIPv6PseudoHeader ipph;
    USHORT checksum;
    ipph.ipph_src[0]  = pIpHeader->ip6_src_address[0];
    ipph.ipph_src[1]  = pIpHeader->ip6_src_address[1];
    ipph.ipph_src[2]  = pIpHeader->ip6_src_address[2];
    ipph.ipph_src[3]  = pIpHeader->ip6_src_address[3];
    ipph.ipph_dest[0] = pIpHeader->ip6_dst_address[0];
    ipph.ipph_dest[1] = pIpHeader->ip6_dst_address[1];
    ipph.ipph_dest[2] = pIpHeader->ip6_dst_address[2];
    ipph.ipph_dest[3] = pIpHeader->ip6_dst_address[3];
    ipph.z1 = ipph.z2 = ipph.z3 = 0;
    ipph.ipph_protocol = pIpHeader->ip6_next_header;
    ipph.ipph_length = swap_short(headerAndPayloadLen);
    checksum = CheckSumCalculatorFlat(&ipph, sizeof(ipph));
    return ~checksum;
}

static __inline USHORT CalculateIpPseudoHeaderChecksum(IPHeader *pIpHeader,
                                                       tTcpIpPacketParsingResult res,
                                                       USHORT headerAndPayloadLen)
{
    if (res.ipStatus == ppresIPV4)
        return CalculateIpv4PseudoHeaderChecksum(&pIpHeader->v4, headerAndPayloadLen);
    if (res.ipStatus == ppresIPV6)
        return CalculateIpv6PseudoHeaderChecksum(&pIpHeader->v6, headerAndPayloadLen);
    return 0;
}

static __inline BOOLEAN
CompareNetCheckSumOnEndSystem(USHORT computedChecksum, USHORT arrivedChecksum)
{
    //According to RFC 1624 sec. 3
    //Checksum verification mechanism should treat 0xFFFF
    //checksum value from received packet as 0x0000
    if(arrivedChecksum == 0xFFFF)
        arrivedChecksum = 0;

    return computedChecksum == arrivedChecksum;
}

/******************************************
  Calculates IP header checksum calculator
  it can be already calculated
  the header must be complete!
*******************************************/
static __inline tTcpIpPacketParsingResult
VerifyIpChecksum(
    IPv4Header *pIpHeader,
    tTcpIpPacketParsingResult known,
    BOOLEAN bFix)
{
    tTcpIpPacketParsingResult res = known;
    if (res.ipCheckSum != ppresIPTooShort)
    {
        USHORT saved = pIpHeader->ip_xsum;
        CalculateIpChecksum(pIpHeader);
        res.ipCheckSum = CompareNetCheckSumOnEndSystem(pIpHeader->ip_xsum, saved) ? ppresCSOK : ppresCSBad;
        if (!bFix)
            pIpHeader->ip_xsum = saved;
        else
            res.fixedIpCS = res.ipCheckSum == ppresCSBad;
    }
    return res;
}

/*********************************************
Calculates UDP checksum, assuming the checksum field
is initialized with pseudoheader checksum
**********************************************/
static __inline VOID CalculateUdpChecksumGivenPseudoCS(UDPHeader *pUdpHeader, tCompletePhysicalAddress *pDataPages, ULONG ulStartOffset, ULONG udpLength)
{
    pUdpHeader->udp_xsum = CheckSumCalculator(pDataPages, ulStartOffset, udpLength);
}

/*********************************************
Calculates TCP checksum, assuming the checksum field
is initialized with pseudoheader checksum
**********************************************/
static __inline VOID CalculateTcpChecksumGivenPseudoCS(TCPHeader *pTcpHeader, tCompletePhysicalAddress *pDataPages, ULONG ulStartOffset, ULONG tcpLength)
{
    pTcpHeader->tcp_xsum = CheckSumCalculator(pDataPages, ulStartOffset, tcpLength);
}

/************************************************
Checks (and fix if required) the TCP checksum
sets flags in result structure according to verification
TcpPseudoOK if valid pseudo CS was found
TcpOK if valid TCP checksum was found
************************************************/
static __inline tTcpIpPacketParsingResult
VerifyTcpChecksum(
                tCompletePhysicalAddress *pDataPages,
                ULONG ulDataLength,
                ULONG ulStartOffset,
                tTcpIpPacketParsingResult known,
                ULONG whatToFix)
{
    USHORT  phcs;
    tTcpIpPacketParsingResult res = known;
    IPHeader *pIpHeader = (IPHeader *)RtlOffsetToPointer(pDataPages[0].Virtual, ulStartOffset);
    TCPHeader *pTcpHeader = (TCPHeader *)RtlOffsetToPointer(pIpHeader, res.ipHeaderSize);
    USHORT saved = pTcpHeader->tcp_xsum;
    USHORT xxpHeaderAndPayloadLen = GetXxpHeaderAndPayloadLen(pIpHeader, res);
    if (ulDataLength >= res.ipHeaderSize)
    {
        phcs = CalculateIpPseudoHeaderChecksum(pIpHeader, res, xxpHeaderAndPayloadLen);
        res.xxpCheckSum = CompareNetCheckSumOnEndSystem(phcs, saved) ?  ppresPCSOK : ppresCSBad;
        if (res.xxpCheckSum != ppresPCSOK || whatToFix)
        {
            if (whatToFix & pcrFixPHChecksum)
            {
                if (ulDataLength >= (ULONG)(res.ipHeaderSize + sizeof(*pTcpHeader)))
                {
                    pTcpHeader->tcp_xsum = phcs;
                    res.fixedXxpCS = res.xxpCheckSum != ppresPCSOK;
                }
                else
                    res.xxpStatus = ppresXxpIncomplete;
            }
            else if (res.xxpFull)
            {
                //USHORT ipFullLength = swap_short(pIpHeader->v4.ip_length);
                pTcpHeader->tcp_xsum = phcs;
                CalculateTcpChecksumGivenPseudoCS(pTcpHeader, pDataPages, ulStartOffset + res.ipHeaderSize, xxpHeaderAndPayloadLen);
                if (CompareNetCheckSumOnEndSystem(pTcpHeader->tcp_xsum, saved))
                    res.xxpCheckSum = ppresCSOK;

                if (!(whatToFix & pcrFixXxpChecksum))
                    pTcpHeader->tcp_xsum = saved;
                else
                    res.fixedXxpCS =
                        res.xxpCheckSum == ppresCSBad || res.xxpCheckSum == ppresPCSOK;
            }
            else if (whatToFix)
            {
                res.xxpStatus = ppresXxpIncomplete;
            }
        }
        else if (res.xxpFull)
        {
            // we have correct PHCS and we do not need to fix anything
            // there is a very small chance that it is also good TCP CS
            // in such rare case we give a priority to TCP CS
            CalculateTcpChecksumGivenPseudoCS(pTcpHeader, pDataPages, ulStartOffset + res.ipHeaderSize, xxpHeaderAndPayloadLen);
            if (CompareNetCheckSumOnEndSystem(pTcpHeader->tcp_xsum, saved))
                res.xxpCheckSum = ppresCSOK;
            pTcpHeader->tcp_xsum = saved;
        }
    }
    else
        res.ipCheckSum = ppresIPTooShort;
    return res;
}

/************************************************
Checks (and fix if required) the UDP checksum
sets flags in result structure according to verification
UdpPseudoOK if valid pseudo CS was found
UdpOK if valid UDP checksum was found
************************************************/
static __inline tTcpIpPacketParsingResult
VerifyUdpChecksum(
                tCompletePhysicalAddress *pDataPages,
                ULONG ulDataLength,
                ULONG ulStartOffset,
                tTcpIpPacketParsingResult known,
                ULONG whatToFix)
{
    USHORT  phcs;
    tTcpIpPacketParsingResult res = known;
    IPHeader *pIpHeader = (IPHeader *)RtlOffsetToPointer(pDataPages[0].Virtual, ulStartOffset);
    UDPHeader *pUdpHeader = (UDPHeader *)RtlOffsetToPointer(pIpHeader, res.ipHeaderSize);
    USHORT saved = pUdpHeader->udp_xsum;
    USHORT xxpHeaderAndPayloadLen = GetXxpHeaderAndPayloadLen(pIpHeader, res);
    if (ulDataLength >= res.ipHeaderSize)
    {
        phcs = CalculateIpPseudoHeaderChecksum(pIpHeader, res, xxpHeaderAndPayloadLen);
        res.xxpCheckSum = CompareNetCheckSumOnEndSystem(phcs, saved) ?  ppresPCSOK : ppresCSBad;
        if (whatToFix & pcrFixPHChecksum)
        {
            if (ulDataLength >= (ULONG)(res.ipHeaderSize + sizeof(UDPHeader)))
            {
                pUdpHeader->udp_xsum = phcs;
                res.fixedXxpCS = res.xxpCheckSum != ppresPCSOK;
            }
            else
                res.xxpStatus = ppresXxpIncomplete;
        }
        else if (res.xxpCheckSum != ppresPCSOK || (whatToFix & pcrFixXxpChecksum))
        {
            if (res.xxpFull)
            {
                pUdpHeader->udp_xsum = phcs;
                CalculateUdpChecksumGivenPseudoCS(pUdpHeader, pDataPages, ulStartOffset + res.ipHeaderSize, xxpHeaderAndPayloadLen);
                if (CompareNetCheckSumOnEndSystem(pUdpHeader->udp_xsum, saved))
                    res.xxpCheckSum = ppresCSOK;

                if (!(whatToFix & pcrFixXxpChecksum))
                    pUdpHeader->udp_xsum = saved;
                else
                    res.fixedXxpCS =
                        res.xxpCheckSum == ppresCSBad || res.xxpCheckSum == ppresPCSOK;
            }
            else
                res.xxpCheckSum = ppresXxpIncomplete;
        }
        else if (res.xxpFull)
        {
            // we have correct PHCS and we do not need to fix anything
            // there is a very small chance that it is also good UDP CS
            // in such rare case we give a priority to UDP CS
            CalculateUdpChecksumGivenPseudoCS(pUdpHeader, pDataPages, ulStartOffset + res.ipHeaderSize, xxpHeaderAndPayloadLen);
            if (CompareNetCheckSumOnEndSystem(pUdpHeader->udp_xsum, saved))
                res.xxpCheckSum = ppresCSOK;
            pUdpHeader->udp_xsum = saved;
        }
    }
    else
        res.ipCheckSum = ppresIPTooShort;

    return res;
}

static LPCSTR __inline GetPacketCase(tTcpIpPacketParsingResult res)
{
    static const char *const IPCaseName[4] = { "not tested", "Non-IP", "IPv4", "IPv6" };
    if (res.xxpStatus == ppresXxpKnown) return res.TcpUdp == ppresIsTCP ? 
        (res.ipStatus == ppresIPV4 ? "TCPv4" : "TCPv6") : 
        (res.ipStatus == ppresIPV4 ? "UDPv4" : "UDPv6");
    if (res.xxpStatus == ppresXxpIncomplete) return res.TcpUdp == ppresIsTCP ? "Incomplete TCP" : "Incomplete UDP";
    if (res.xxpStatus == ppresXxpOther) return "IP";
    return  IPCaseName[res.ipStatus];
}

static LPCSTR __inline GetIPCSCase(tTcpIpPacketParsingResult res)
{
    static const char *const CSCaseName[4] = { "not tested", "(too short)", "OK", "Bad" };
    return CSCaseName[res.ipCheckSum];
}

static LPCSTR __inline GetXxpCSCase(tTcpIpPacketParsingResult res)
{
    static const char *const CSCaseName[4] = { "-", "PCS", "CS", "Bad" };
    return CSCaseName[res.xxpCheckSum];
}

static __inline VOID PrintOutParsingResult(
    tTcpIpPacketParsingResult res,
    int level,
    LPCSTR procname)
{
    DPrintf(level, ("[%s] %s packet IPCS %s%s, checksum %s%s\n", procname,
        GetPacketCase(res),
        GetIPCSCase(res),
        res.fixedIpCS ? "(fixed)" : "",
        GetXxpCSCase(res),
        res.fixedXxpCS ? "(fixed)" : ""));
}

tTcpIpPacketParsingResult ParaNdis_CheckSumVerify(
                                                tCompletePhysicalAddress *pDataPages,
                                                ULONG ulDataLength,
                                                ULONG ulStartOffset,
                                                ULONG flags,
                                                BOOLEAN verifyLength,
                                                LPCSTR caller)
{
    IPHeader *pIpHeader = (IPHeader *) RtlOffsetToPointer(pDataPages[0].Virtual, ulStartOffset);

    tTcpIpPacketParsingResult res = QualifyIpPacket(pIpHeader, ulDataLength, verifyLength);
    if (res.ipStatus == ppresNotIP || res.ipCheckSum == ppresIPTooShort)
        return res;

    if (res.ipStatus == ppresIPV4)
    {
        if (flags & pcrIpChecksum)
            res = VerifyIpChecksum(&pIpHeader->v4, res, (flags & pcrFixIPChecksum) != 0);
        if(res.xxpStatus == ppresXxpKnown)
        {
            if (res.TcpUdp == ppresIsTCP) /* TCP */
            {
                if(flags & pcrTcpV4Checksum)
                {
                    res = VerifyTcpChecksum(pDataPages, ulDataLength, ulStartOffset, res, flags & (pcrFixPHChecksum | pcrFixTcpV4Checksum));
                }
            }
            else /* UDP */
            {
                if (flags & pcrUdpV4Checksum)
                {
                    res = VerifyUdpChecksum(pDataPages, ulDataLength, ulStartOffset, res, flags & (pcrFixPHChecksum | pcrFixUdpV4Checksum));
                }
            }
        }
    }
    else if (res.ipStatus == ppresIPV6)
    {
        if(res.xxpStatus == ppresXxpKnown)
        {
            if (res.TcpUdp == ppresIsTCP) /* TCP */
            {
                if(flags & pcrTcpV6Checksum)
                {
                    res = VerifyTcpChecksum(pDataPages, ulDataLength, ulStartOffset, res, flags & (pcrFixPHChecksum | pcrFixTcpV6Checksum));
                }
            }
            else /* UDP */
            {
                if (flags & pcrUdpV6Checksum)
                {
                    res = VerifyUdpChecksum(pDataPages, ulDataLength, ulStartOffset, res, flags & (pcrFixPHChecksum | pcrFixUdpV6Checksum));
                }
            }
        }
    }
    PrintOutParsingResult(res, 1, caller);
    return res;
}

tTcpIpPacketParsingResult ParaNdis_ReviewIPPacket(PVOID buffer, ULONG size, BOOLEAN verifyLength, LPCSTR caller)
{
    tTcpIpPacketParsingResult res = QualifyIpPacket((IPHeader *) buffer, size, verifyLength);
    PrintOutParsingResult(res, 1, caller);
    return res;
}

static __inline
VOID AnalyzeL3Proto(
    USHORT L3Proto,
    PNET_PACKET_INFO packetInfo)
{
    packetInfo->isIP4 = (L3Proto == RtlUshortByteSwap(ETH_PROTO_IP4));
    packetInfo->isIP6 = (L3Proto == RtlUshortByteSwap(ETH_PROTO_IP6));
}

static
BOOLEAN AnalyzeL2Hdr(
    PNET_PACKET_INFO packetInfo)
{
    PETH_HEADER dataBuffer = (PETH_HEADER) packetInfo->headersBuffer;

    if (packetInfo->dataLength < ETH_HEADER_SIZE)
        return FALSE;

    packetInfo->ethDestAddr = dataBuffer->DstAddr;

    if (ETH_IS_BROADCAST(dataBuffer))
    {
        #pragma warning(suppress: 4463)
        packetInfo->isBroadcast = TRUE;
    }
    else if (ETH_IS_MULTICAST(dataBuffer))
    {
        #pragma warning(suppress: 4463)
        packetInfo->isMulticast = TRUE;
    }
    else
    {
        #pragma warning(suppress: 4463)
        packetInfo->isUnicast = TRUE;
    }

    if(ETH_HAS_PRIO_HEADER(dataBuffer))
    {
        PVLAN_HEADER vlanHdr = ETH_GET_VLAN_HDR(dataBuffer);

        if(packetInfo->dataLength < ETH_HEADER_SIZE + ETH_PRIORITY_HEADER_SIZE)
            return FALSE;

        #pragma warning(suppress: 4463)
        packetInfo->hasVlanHeader     = TRUE;
        packetInfo->Vlan.UserPriority = VLAN_GET_USER_PRIORITY(vlanHdr);
        packetInfo->Vlan.VlanId       = VLAN_GET_VLAN_ID(vlanHdr);
        packetInfo->L2HdrLen          = ETH_HEADER_SIZE + ETH_PRIORITY_HEADER_SIZE;
        AnalyzeL3Proto(vlanHdr->EthType, packetInfo);
    }
    else
    {
        packetInfo->L2HdrLen = ETH_HEADER_SIZE;
        AnalyzeL3Proto(dataBuffer->EthType, packetInfo);
    }

    packetInfo->L2PayloadLen = packetInfo->dataLength - packetInfo->L2HdrLen;

    return TRUE;
}

static __inline
BOOLEAN SkipIP6ExtensionHeader(
    IPv6Header *ip6Hdr,
    ULONG dataLength,
    PULONG ip6HdrLength,
    PUCHAR nextHdr)
{
    IPv6ExtHeader* ip6ExtHdr;

    if (*ip6HdrLength + sizeof(*ip6ExtHdr) > dataLength)
        return FALSE;

    ip6ExtHdr = (IPv6ExtHeader *)RtlOffsetToPointer(ip6Hdr, *ip6HdrLength);
    *nextHdr = ip6ExtHdr->ip6ext_next_header;
    *ip6HdrLength += (ip6ExtHdr->ip6ext_hdr_len + 1) * IP6_EXT_HDR_GRANULARITY;
    return TRUE;
}

static
BOOLEAN AnalyzeIP6RoutingExtension(
    PIP6_TYPE2_ROUTING_HEADER routingHdr,
    ULONG dataLength,
    IPV6_ADDRESS **destAddr)
{
    if(dataLength < sizeof(*routingHdr))
        return FALSE;
    if(routingHdr->RoutingType == 2)
    {
        if((dataLength != sizeof(*routingHdr)) || (routingHdr->SegmentsLeft != 1))
            return FALSE;

        *destAddr = &routingHdr->Address;
    }
    else *destAddr = NULL;

    return TRUE;
}

static
BOOLEAN AnalyzeIP6DestinationExtension(
    PVOID destHdr,
    ULONG dataLength,
    IPV6_ADDRESS **homeAddr)
{
    while(dataLength != 0)
    {
        PIP6_EXT_HDR_OPTION optHdr = (PIP6_EXT_HDR_OPTION) destHdr;
        ULONG optionLen;

        switch(optHdr->Type)
        {
        case IP6_EXT_HDR_OPTION_HOME_ADDR:
            if(dataLength < sizeof(IP6_EXT_HDR_OPTION))
                return FALSE;

            optionLen = optHdr->Length + sizeof(IP6_EXT_HDR_OPTION);
            if(optHdr->Length != sizeof(IPV6_ADDRESS))
                return FALSE;

            *homeAddr = (IPV6_ADDRESS*) RtlOffsetToPointer(optHdr, sizeof(IP6_EXT_HDR_OPTION));
            break;

        case IP6_EXT_HDR_OPTION_PAD1:
            optionLen = RTL_SIZEOF_THROUGH_FIELD(IP6_EXT_HDR_OPTION, Type);
            break;

        default:
            if(dataLength < sizeof(IP6_EXT_HDR_OPTION))
                return FALSE;

            optionLen = optHdr->Length + sizeof(IP6_EXT_HDR_OPTION);
            break;
        }

        destHdr = RtlOffsetToPointer(destHdr, optionLen);
        if(dataLength < optionLen)
            return FALSE;

        dataLength -= optionLen;
    }

    return TRUE;
}

static
BOOLEAN AnalyzeIP6Hdr(
    IPv6Header *ip6Hdr,
    ULONG dataLength,
    PULONG ip6HdrLength,
    PUCHAR nextHdr,
    PULONG homeAddrOffset,
    PULONG destAddrOffset)
{
    *homeAddrOffset = 0;
    *destAddrOffset = 0;

    *ip6HdrLength = sizeof(*ip6Hdr);
    if(dataLength < *ip6HdrLength)
        return FALSE;

    *nextHdr = ip6Hdr->ip6_next_header;
    for(;;)
    {
        switch (*nextHdr)
        {
        default:
        case IP6_HDR_NONE:
            __fallthrough;
        case PROTOCOL_TCP:
            __fallthrough;
        case PROTOCOL_UDP:
            __fallthrough;
        case IP6_HDR_FRAGMENT:
            return TRUE;
        case IP6_HDR_DESTINATON:
            {
                IPV6_ADDRESS *homeAddr = NULL;
                ULONG destHdrOffset = *ip6HdrLength;
                if(!SkipIP6ExtensionHeader(ip6Hdr, dataLength, ip6HdrLength, nextHdr))
                    return FALSE;

                if(!AnalyzeIP6DestinationExtension(RtlOffsetToPointer(ip6Hdr, destHdrOffset),
                    *ip6HdrLength - destHdrOffset, &homeAddr))
                    return FALSE;

                *homeAddrOffset = homeAddr ? RtlPointerToOffset(ip6Hdr, homeAddr) : 0;
            }
            break;
        case IP6_HDR_ROUTING:
            {
                IPV6_ADDRESS *destAddr = NULL;
                ULONG routingHdrOffset = *ip6HdrLength;

                if(!SkipIP6ExtensionHeader(ip6Hdr, dataLength, ip6HdrLength, nextHdr))
                    return FALSE;

                if(!AnalyzeIP6RoutingExtension((PIP6_TYPE2_ROUTING_HEADER) RtlOffsetToPointer(ip6Hdr, routingHdrOffset),
                    *ip6HdrLength - routingHdrOffset, &destAddr))
                    return FALSE;

                *destAddrOffset = destAddr ? RtlPointerToOffset(ip6Hdr, destAddr) : 0;
            }
            break;
        case IP6_HDR_HOP_BY_HOP:
            __fallthrough;
        case IP6_HDR_ESP:
            __fallthrough;
        case IP6_HDR_AUTHENTICATION:
            __fallthrough;
        case IP6_HDR_MOBILITY:
            if(!SkipIP6ExtensionHeader(ip6Hdr, dataLength, ip6HdrLength, nextHdr))
                return FALSE;

            break;
        }
    }
}

static __inline
VOID AnalyzeL4Proto(
    UCHAR l4Protocol,
    PNET_PACKET_INFO packetInfo)
{
    packetInfo->isTCP = (l4Protocol == PROTOCOL_TCP);
    packetInfo->isUDP = (l4Protocol == PROTOCOL_UDP);
}

static
BOOLEAN AnalyzeL3Hdr(
    PNET_PACKET_INFO packetInfo)
{
    if(packetInfo->isIP4)
    {
        IPv4Header *ip4Hdr = (IPv4Header *) RtlOffsetToPointer(packetInfo->headersBuffer, packetInfo->L2HdrLen);

        if(packetInfo->dataLength < packetInfo->L2HdrLen + sizeof(*ip4Hdr))
            return FALSE;

        packetInfo->L3HdrLen = IP_HEADER_LENGTH(ip4Hdr);
        if ((packetInfo->L3HdrLen < sizeof(*ip4Hdr)) ||
            (packetInfo->dataLength < packetInfo->L2HdrLen + packetInfo->L3HdrLen))
            return FALSE;

        if(IP_HEADER_VERSION(ip4Hdr) != 4)
            return FALSE;

        packetInfo->isFragment = IP_HEADER_IS_FRAGMENT(ip4Hdr);

        if(!packetInfo->isFragment)
        {
            AnalyzeL4Proto(ip4Hdr->ip_protocol, packetInfo);
        }
    }
    else if(packetInfo->isIP6)
    {
        ULONG homeAddrOffset, destAddrOffset;
        UCHAR l4Proto;

        IPv6Header *ip6Hdr = (IPv6Header *) RtlOffsetToPointer(packetInfo->headersBuffer, packetInfo->L2HdrLen);

        if(IP6_HEADER_VERSION(ip6Hdr) != 6)
            return FALSE;

        if(!AnalyzeIP6Hdr(ip6Hdr, packetInfo->L2PayloadLen,
            &packetInfo->L3HdrLen, &l4Proto, &homeAddrOffset, &destAddrOffset))
            return FALSE;

        if (packetInfo->L3HdrLen > MAX_SUPPORTED_IPV6_HEADERS)
            return FALSE;

        packetInfo->ip6HomeAddrOffset = (homeAddrOffset) ? packetInfo->L2HdrLen + homeAddrOffset : 0;
        packetInfo->ip6DestAddrOffset = (destAddrOffset) ? packetInfo->L2HdrLen + destAddrOffset : 0;

        packetInfo->isFragment = (l4Proto == IP6_HDR_FRAGMENT);

        if(!packetInfo->isFragment)
        {
            AnalyzeL4Proto(l4Proto, packetInfo);
        }
    }

    return TRUE;
}

BOOLEAN ParaNdis_AnalyzeReceivedPacket(
    PVOID headersBuffer,
    ULONG dataLength,
    PNET_PACKET_INFO packetInfo)
{
    NdisZeroMemory(packetInfo, sizeof(*packetInfo));

    packetInfo->headersBuffer = headersBuffer;
    packetInfo->dataLength = dataLength;

    if(!AnalyzeL2Hdr(packetInfo))
        return FALSE;

    if (!AnalyzeL3Hdr(packetInfo))
        return FALSE;

    return TRUE;
}

ULONG ParaNdis_StripVlanHeaderMoveHead(PNET_PACKET_INFO packetInfo)
{
    PUINT32 pData = (PUINT32) packetInfo->headersBuffer;

    ASSERT(packetInfo->hasVlanHeader);
    ASSERT(packetInfo->L2HdrLen == ETH_HEADER_SIZE + ETH_PRIORITY_HEADER_SIZE);

    pData[3] = pData[2];
    pData[2] = pData[1];
    pData[1] = pData[0];

    packetInfo->headersBuffer = RtlOffsetToPointer(packetInfo->headersBuffer, ETH_PRIORITY_HEADER_SIZE);
    packetInfo->dataLength -= ETH_PRIORITY_HEADER_SIZE;
    packetInfo->L2HdrLen = ETH_HEADER_SIZE;

    packetInfo->ethDestAddr = (PUCHAR) RtlOffsetToPointer(packetInfo->ethDestAddr, ETH_PRIORITY_HEADER_SIZE);
    packetInfo->ip6DestAddrOffset -= ETH_PRIORITY_HEADER_SIZE;
    packetInfo->ip6HomeAddrOffset -= ETH_PRIORITY_HEADER_SIZE;

    return ETH_PRIORITY_HEADER_SIZE;
};

VOID ParaNdis_PadPacketToMinimalLength(PNET_PACKET_INFO packetInfo)
{
    // Ethernet standard declares minimal possible packet size
    // Packets smaller than that must be padded before transfer
    // Ethernet HW pads packets on transmit, however in our case
    // some packets do not travel over Ethernet but being routed
    // guest-to-guest by virtual switch.
    // In this case padding is not performed and we may
    // receive packet smaller than minimal allowed size. This is not
    // a problem for real life scenarios however WHQL/HCK contains
    // tests that check padding of received packets.
    // To make these tests happy we have to pad small packets on receive

    //NOTE: This function assumes that VLAN header has been already stripped out

    if(packetInfo->dataLength < ETH_MIN_PACKET_SIZE)
    {
        RtlZeroMemory(
                        RtlOffsetToPointer(packetInfo->headersBuffer, packetInfo->dataLength),
                        ETH_MIN_PACKET_SIZE - packetInfo->dataLength);
        packetInfo->dataLength = ETH_MIN_PACKET_SIZE;
    }
}
