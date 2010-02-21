/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
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

#ifdef WPP_EVENT_TRACING
#include "sw-offload.tmh"
#endif


// IP Pseudo Header RFC 768
typedef struct _tagIPPseudoHeader {
	ULONG		ipph_src;				// Source address
	ULONG		ipph_dest;				// Destination address
	UCHAR		ipph_zero;				// 0
	UCHAR		ipph_protocol;			// TCP/UDP
	USHORT		ipph_length;			// TCP/UDP length
}tIPPseudoHeader;


#define PROTOCOL_TCP					6
#define PROTOCOL_UDP					17


static __inline USHORT swap_short(USHORT us)
{
	return (us << 8) | (us >> 8);
}

#define IP_HEADER_LENGTH(pHeader)  ((pHeader->ip_verlen & 0x0F) << 2)
#define TCP_HEADER_LENGTH(pHeader) ((pHeader->tcp_flags & 0xF0) >> 2)



static __inline USHORT CheckSumCalculator(ULONG val, PVOID buffer, ULONG len)
{
	PUSHORT pus = (PUSHORT)buffer;
	ULONG count = len >> 1;
	while (count--) val += *pus++;
	if (len & 1) val += (USHORT)*(PUCHAR)pus;
	val = (((val >> 16) | (val << 16)) + val) >> 16;
	return (USHORT)~val;
}


/******************************************
    IP header checksum calculator
*******************************************/
static __inline VOID CalculateIpChecksum(IPHeader *pIpHeader)
{
	ULONG xSum = 0;
	ULONG len = IP_HEADER_LENGTH(pIpHeader);

	pIpHeader->ip_xsum = 0;
	pIpHeader->ip_xsum = CheckSumCalculator(0, pIpHeader, len);
}


static __inline tTcpIpPacketParsingResult
QualifyIpPacket(IPHeader *pIpHeader, ULONG len)
{
	tTcpIpPacketParsingResult res;
	UCHAR  ver_len = pIpHeader->ip_verlen;
	UCHAR  ip_version = (ver_len & 0xF0) >> 4;
	USHORT ipHeaderSize = (ver_len & 0xF) << 2;
	USHORT fullLength = swap_short(pIpHeader->ip_length);
	res.value = 0;
	DPrintf(3, ("ip_version %d, ipHeaderSize %d, protocol %d, iplen %d",
		ip_version, ipHeaderSize, pIpHeader->ip_protocol, fullLength));
	res.ipStatus = (ip_version == 4 && ipHeaderSize >= sizeof(IPHeader))
		? ppresIPV4 : ppresNotIP;
	if (len < ipHeaderSize) res.ipCheckSum = ppresIPTooShort;
	if (res.ipStatus == ppresIPV4)
	{
		res.ipHeaderSize = ipHeaderSize;
		res.xxpFull = len >= fullLength ? 1 : 0;
		switch (pIpHeader->ip_protocol)
		{
			case PROTOCOL_TCP:
			{
				USHORT tcpipDataAt = ipHeaderSize + sizeof(TCPHeader);
				res.xxpStatus = ppresXxpIncomplete;
				res.TcpUdp = ppresIsTCP;

				if ((len >= tcpipDataAt))
				{
					TCPHeader *pTcpHeader = (TCPHeader *)RtlOffsetToPointer(pIpHeader, ipHeaderSize);
					res.xxpStatus = ppresXxpKnown;
					tcpipDataAt = ipHeaderSize + TCP_HEADER_LENGTH(pTcpHeader);
					res.XxpIpHeaderSize = tcpipDataAt;
				}
				else
				{
					DPrintf(2, ("tcp: %d < min headers %d", len, tcpipDataAt));
				}
			}
			break;
		case PROTOCOL_UDP:
			{
				USHORT udpDataStart = ipHeaderSize + sizeof(UDPHeader);
				res.xxpStatus = ppresXxpIncomplete;
				res.TcpUdp = ppresIsUDP;
				res.XxpIpHeaderSize = udpDataStart;
				if (len >= udpDataStart)
				{
					UDPHeader *pUdpHeader = (UDPHeader *)RtlOffsetToPointer(pIpHeader, ipHeaderSize);
					USHORT datagramLength = swap_short(pUdpHeader->udp_length);
					res.xxpStatus = ppresXxpKnown;
					if (((USHORT)len < (ipHeaderSize + datagramLength)) || (datagramLength < sizeof(UDPHeader)))
					{
						DPrintf(2, ("udp: len %d, datagramLength %d, IP len %d", len, datagramLength, fullLength));
					}
				}
				else
				{
					DPrintf(2, ("udp: len %d, udpDataStart %d", len, udpDataStart));
				}
			}
			break;
		default:
			res.xxpStatus = ppresXxpOther;
			break;
		}
	}
	return res;
}

static __inline USHORT CalculateIpPseudoHeaderChecksum(IPHeader *pIpHeader)
{
	tIPPseudoHeader ipph;
    ULONG headerLength = IP_HEADER_LENGTH(pIpHeader);
	USHORT checksum;
	USHORT len = swap_short(pIpHeader->ip_length);
	ipph.ipph_src  = pIpHeader->ip_src;
	ipph.ipph_dest = pIpHeader->ip_dest;
	ipph.ipph_zero = 0;
	ipph.ipph_protocol = pIpHeader->ip_protocol;
	ipph.ipph_length = swap_short((USHORT)(len - headerLength));
	checksum = CheckSumCalculator(0, &ipph, sizeof(ipph));
	return ~checksum;
}

/******************************************
  Calculates IP header checksum calculator
  it can be already calculated
  the header must be complete!
*******************************************/
static __inline tTcpIpPacketParsingResult
VerifyIpChecksum(
	IPHeader *pIpHeader,
	tTcpIpPacketParsingResult known,
	BOOLEAN bFix)
{
	tTcpIpPacketParsingResult res = known;
	if (res.ipCheckSum != ppresIPTooShort)
	{
		USHORT saved = pIpHeader->ip_xsum;
		CalculateIpChecksum(pIpHeader);
		res.ipCheckSum = (pIpHeader->ip_xsum == saved) ? ppresCSOK : ppresCSBad;
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
static VOID CalculateUdpChecksumGivenPseudoCS(UDPHeader *pUdpHeader, ULONG udpLength)
{
	pUdpHeader->udp_xsum = CheckSumCalculator(0, pUdpHeader, udpLength);
}

/*********************************************
Calculates TCP checksum, assuming the checksum field
is initialized with pseudoheader checksum
**********************************************/
static __inline VOID CalculateTcpChecksumGivenPseudoCS(TCPHeader *pTcpHeader, ULONG tcpLength)
{
	pTcpHeader->tcp_xsum = CheckSumCalculator(0, pTcpHeader, tcpLength);
}

/************************************************
Checks (and fix if required) the TCP checksum
sets flags in result structure according to verification
TcpPseudoOK if valid pseudo CS was found
TcpOK if valid TCP checksum was found
************************************************/
static __inline tTcpIpPacketParsingResult
VerifyTcpChecksum( IPHeader *pIpHeader, ULONG len, tTcpIpPacketParsingResult known, ULONG whatToFix)
{
	USHORT  phcs;
	tTcpIpPacketParsingResult res = known;
	UCHAR  ver_len = pIpHeader->ip_verlen;
	USHORT ipHeaderSize = (ver_len & 0xF) << 2;
	TCPHeader *pTcpHeader = (TCPHeader *)RtlOffsetToPointer(pIpHeader, ipHeaderSize);
	USHORT saved = pTcpHeader->tcp_xsum;
	if (len >= ipHeaderSize)
	{
		phcs = CalculateIpPseudoHeaderChecksum(pIpHeader);
		res.xxpCheckSum = (saved == phcs) ?  ppresPCSOK : ppresCSBad;
		if (res.xxpCheckSum != ppresPCSOK || whatToFix)
		{
			if (whatToFix & pcrFixPHChecksum)
			{
				if (len >= (ULONG)(ipHeaderSize + sizeof(*pTcpHeader)))
				{
					pTcpHeader->tcp_xsum = phcs;
					res.fixedXxpCS = res.xxpCheckSum != ppresPCSOK;
				}
				else
					res.xxpStatus = ppresXxpIncomplete;
			}
			else if (res.xxpFull)
			{
				USHORT ipFullLength = swap_short(pIpHeader->ip_length);
				pTcpHeader->tcp_xsum = phcs;
				CalculateTcpChecksumGivenPseudoCS(pTcpHeader, ipFullLength - ipHeaderSize);
				if (saved == pTcpHeader->tcp_xsum)
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
VerifyUdpChecksum( IPHeader *pIpHeader, ULONG len, tTcpIpPacketParsingResult known, ULONG whatToFix)
{
	USHORT  phcs;
	tTcpIpPacketParsingResult res = known;
	UCHAR  ver_len = pIpHeader->ip_verlen;
	USHORT ipHeaderSize = (ver_len & 0xF) << 2;
	UDPHeader *pUdpHeader = (UDPHeader *)RtlOffsetToPointer(pIpHeader, ipHeaderSize);
	USHORT saved = pUdpHeader->udp_xsum;

	if (len >= ipHeaderSize)
	{
		phcs = CalculateIpPseudoHeaderChecksum(pIpHeader);
		res.xxpCheckSum = (saved == phcs) ?  ppresPCSOK : ppresCSBad;
		if (whatToFix & pcrFixPHChecksum)
		{
			if (len >= (ULONG)(ipHeaderSize + sizeof(UDPHeader)))
			{
				pUdpHeader->udp_xsum = phcs;
				res.fixedXxpCS = res.xxpCheckSum != ppresPCSOK;
			}
			else
				res.xxpStatus = ppresXxpIncomplete;
		}
		else if (res.xxpCheckSum != ppresPCSOK || (whatToFix & pcrFixXxpChecksum))
		{
			USHORT ipFullLength = swap_short(pIpHeader->ip_length);
			if (len >= ipFullLength)
			{
				pUdpHeader->udp_xsum = phcs;
				CalculateUdpChecksumGivenPseudoCS(pUdpHeader, ipFullLength - ipHeaderSize);
				if (saved == pUdpHeader->udp_xsum)
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
	}
	else
		res.ipCheckSum = ppresIPTooShort;

	return res;
}

static LPCSTR __inline GetPacketCase(tTcpIpPacketParsingResult res)
{
	static const char *const IPCaseName[4] = { "not tested", "Non-IP", "IP", "???" };
	if (res.xxpStatus == ppresXxpKnown) return res.TcpUdp == ppresIsTCP ? "TCP" : "UDP";
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
	DPrintf(level, ("[%s] %s packet IPCS %s%s, checksum %s%s", procname,
		GetPacketCase(res),
		GetIPCSCase(res),
		res.fixedIpCS ? "(fixed)" : "",
		GetXxpCSCase(res),
		res.fixedXxpCS ? "(fixed)" : ""));
}

tTcpIpPacketParsingResult ParaNdis_CheckSumVerify(PVOID buffer, ULONG size, ULONG flags, LPCSTR caller)
{
	tTcpIpPacketParsingResult res = QualifyIpPacket(buffer, size);
	if (res.ipStatus == ppresIPV4 && (flags & pcrIpChecksum))
	{
		res = VerifyIpChecksum(buffer, res, (flags & pcrFixIPChecksum) != 0);
	}
	if (res.xxpStatus == ppresXxpKnown
		&& res.TcpUdp == ppresIsTCP
		&& (flags & pcrTcpChecksum))
	{
		res = VerifyTcpChecksum(buffer, size, res, flags & (pcrFixPHChecksum | pcrFixXxpChecksum));
	}
	if (res.xxpStatus == ppresXxpKnown
		&& res.TcpUdp == ppresIsUDP
		&& (flags & pcrUdpChecksum))
	{
		res = VerifyUdpChecksum(buffer, size, res, flags & (pcrFixPHChecksum | pcrFixXxpChecksum));
	}
	PrintOutParsingResult(res, 1, caller);
	return res;
}


void ParaNdis_CheckSumCalculate(PVOID buffer, ULONG size, ULONG flags)
{
	tTcpIpPacketParsingResult res = QualifyIpPacket(buffer, size);
	if (res.ipStatus == ppresIPV4 && (flags & pcrIpChecksum))
	{
		res = VerifyIpChecksum(buffer, res, (flags & pcrFixIPChecksum) != 0);
	}
	if (res.xxpStatus == ppresXxpKnown && res.TcpUdp == ppresIsTCP && (flags & pcrTcpChecksum))
	{
		if (flags & (pcrFixPHChecksum | pcrFixXxpChecksum))
			res = VerifyTcpChecksum(buffer, size, res, flags & (pcrFixPHChecksum | pcrFixXxpChecksum));
		else
		{
			IPHeader *pIpHeader = buffer;
			UCHAR  ver_len = pIpHeader->ip_verlen;
			USHORT ipHeaderSize = (ver_len & 0xF) << 2;
			TCPHeader *pTcpHeader = (TCPHeader *)RtlOffsetToPointer(pIpHeader, ipHeaderSize);
			CalculateTcpChecksumGivenPseudoCS(pTcpHeader, size - ipHeaderSize);
		}
	}
	if (res.xxpStatus == ppresXxpKnown && res.TcpUdp == ppresIsUDP && (flags & pcrUdpChecksum))
	{
		if (flags & (pcrFixPHChecksum | pcrFixXxpChecksum))
			res = VerifyUdpChecksum(buffer, size, res, flags & (pcrFixPHChecksum | pcrFixXxpChecksum));
		else
		{
			IPHeader *pIpHeader = buffer;
			UCHAR  ver_len = pIpHeader->ip_verlen;
			USHORT ipHeaderSize = (ver_len & 0xF) << 2;
			UDPHeader *pUdpHeader = (UDPHeader *)RtlOffsetToPointer(pIpHeader, ipHeaderSize);
			CalculateUdpChecksumGivenPseudoCS(pUdpHeader, size - ipHeaderSize);
		}
	}
}

tTcpIpPacketParsingResult ParaNdis_ReviewIPPacket(PVOID buffer, ULONG size, LPCSTR caller)
{
	tTcpIpPacketParsingResult res = QualifyIpPacket(buffer, size);
	PrintOutParsingResult(res, 1, caller);
	return res;
}
