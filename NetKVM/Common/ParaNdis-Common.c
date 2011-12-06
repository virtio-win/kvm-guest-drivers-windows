/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * File: ParaNdis-Common.c
 *
 * This file contains NDIS driver procedures, common for NDIS5 and NDIS6
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "ndis56common.h"

#ifdef WPP_EVENT_TRACING
#include "ParaNdis-Common.tmh"
#endif

// TODO: remove when the problem solved
void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue);

//#define ROUNDSIZE(sz)	((sz + 15) & ~15)


#if 0
void FORCEINLINE DebugDumpPacket(LPCSTR prefix, PVOID header, int level)
{
	PUCHAR peth = (PUCHAR)header;
	DPrintf(level, ("[%s] %02X%02X%02X%02X%02X%02X => %02X%02X%02X%02X%02X%02X", prefix,
		peth[6], peth[7], peth[8], peth[9], peth[10], peth[11],
		peth[0], peth[1], peth[2], peth[3], peth[4], peth[5]));
}
#else
void FORCEINLINE DebugDumpPacket(LPCSTR prefix, PVOID header, int level)
{
}
#endif



/**********************************************************
Validates MAC address
Valid MAC address is not broadcast, not multicast, not empty
if bLocal is set, it must be LOCAL
if not, is must be non-local or local
Parameters:
	PUCHAR pcMacAddress - MAC address to validate
	BOOLEAN bLocal      - TRUE, if we validate locally administered address
Return value:
	TRUE if valid
***********************************************************/
BOOLEAN ParaNdis_ValidateMacAddress(PUCHAR pcMacAddress, BOOLEAN bLocal)
{
	BOOLEAN bLA = FALSE, bEmpty, bBroadcast, bMulticast = FALSE;
	bBroadcast = ETH_IS_BROADCAST(pcMacAddress);
	bLA = !bBroadcast && ETH_IS_LOCALLY_ADMINISTERED(pcMacAddress);
	bMulticast = !bBroadcast && ETH_IS_MULTICAST(pcMacAddress);
	bEmpty = ETH_IS_EMPTY(pcMacAddress);
	return !bBroadcast && !bEmpty && !bMulticast && (!bLocal || bLA);
}

static eInspectedPacketType QueryPacketType(PVOID data)
{
	if (ETH_IS_BROADCAST(data))
		return iptBroadcast;
	if (ETH_IS_MULTICAST(data))
		return iptMilticast;
	return iptUnicast;
}

typedef struct _tagConfigurationEntry
{
	const char		*Name;
	ULONG			ulValue;
	ULONG			ulMinimal;
	ULONG			ulMaximal;
}tConfigurationEntry;

typedef struct _tagConfigurationEntries
{
	tConfigurationEntry isPromiscuous;
	tConfigurationEntry PrioritySupport;
	tConfigurationEntry ConnectRate;
	tConfigurationEntry isLogEnabled;
	tConfigurationEntry debugLevel;
	tConfigurationEntry connectTimer;
	tConfigurationEntry dpcChecker;
	tConfigurationEntry TxCapacity;
	tConfigurationEntry RxCapacity;
	tConfigurationEntry InterruptRecovery;
	tConfigurationEntry LogStatistics;
	tConfigurationEntry HardReset;
	tConfigurationEntry PacketFiltering;
	tConfigurationEntry ScatterGather;
	tConfigurationEntry BatchReceive;
	tConfigurationEntry OffloadTxIP;
	tConfigurationEntry OffloadTxTCP;
	tConfigurationEntry OffloadTxUDP;
	tConfigurationEntry OffloadTxLSO;
	tConfigurationEntry OffloadRxIP;
	tConfigurationEntry HwOffload;
	tConfigurationEntry IPPacketsCheck;
	tConfigurationEntry IPChecksumFix;
	tConfigurationEntry stdIpcsV4;
	tConfigurationEntry stdTcpcsV4;
	tConfigurationEntry stdTcpcsV6;
	tConfigurationEntry stdUdpcsV4;
	tConfigurationEntry stdUdpcsV6;
	tConfigurationEntry stdLsoV1;
	tConfigurationEntry stdLsoV2ip4;
	tConfigurationEntry stdLsoV2ip6;
	tConfigurationEntry PriorityVlanTagging;
	tConfigurationEntry VlanId;
	tConfigurationEntry UseMergeableBuffers;
	tConfigurationEntry PublishIndices;
	tConfigurationEntry MTU;
	tConfigurationEntry NumberOfHandledRXPackersInDPC;
}tConfigurationEntries;

static const tConfigurationEntries defaultConfiguration =
{
	{ "Promiscuous",	0,	0,	1 },
	{ "Priority",		0,	0,	1 },
	{ "ConnectRate",	100,10,10000 },
	{ "DoLog",			1,	0,	1 },
	{ "DebugLevel",		2,	0,	8 },
	{ "ConnectTimer",	0,	0,	300000 },
	{ "DpcCheck",		0,	0,	2 },
	{ "TxCapacity",		64,	16,	1024 },
	{ "RxCapacity",		256, 32, 1024 },
	{ "InterruptRecovery",	0, 0, 1},
	{ "LogStatistics",	0, 0, 10000},
	{ "HardReset",		0, 0, 1},
	{ "PacketFilter",	0, 0, 1},
	{ "Gather",			0, 0, 1},
	{ "BatchReceive",	1, 0, 1},
	{ "Offload.TxIP",	0, 0, 1},
	{ "Offload.TxTCP",	0, 0, 1},
	{ "Offload.TxUDP",	0, 0, 1},
	{ "Offload.TxLSO",	0, 0, 1},
	{ "Offload.RxIP",	0, 0, 1},
	{ "HwOffload",		0, 0, 1 },
	{ "IPPacketsCheck",	0, 0, 1 },
	{ "IPChecksumFix",	1, 0, 1 },
	{ "*IPChecksumOffloadIPv4",	3, 0, 3 },
	{ "*TCPChecksumOffloadIPv4",3, 0, 3 },
	{ "*TCPChecksumOffloadIPv6",3, 0, 3 },
	{ "*UDPChecksumOffloadIPv4",3, 0, 3 },
	{ "*UDPChecksumOffloadIPv6",3, 0, 3 },
	{ "*LsoV1IPv4",	1, 0, 1 },
	{ "*LsoV2IPv4",	1, 0, 1 },
	{ "*LsoV2IPv6",	1, 0, 1 },
	{ "*PriorityVLANTag", 3, 0, 3},
	{ "VlanId", 0, 0, 4095},
	{ "MergeableBuf", 1, 0, 1},
	{ "PublishIndices", 1, 0, 1},
	{ "MTU", 1500, 500, 65500},
	{ "NumberOfHandledRXPackersInDPC", MAX_RX_LOOPS, 1, 10000},
};

static void ParaNdis_ResetVirtIONetDevice(PARANDIS_ADAPTER *pContext)
{
	VirtIODeviceReset(&pContext->IODevice);
	DPrintf(0, ("[%s] Done", __FUNCTION__));
	/* reset all the features in the device */
	WriteVirtIODeviceRegister(pContext->IODevice.addr + VIRTIO_PCI_GUEST_FEATURES, 0);
#ifdef VIRTIO_RESET_VERIFY
	if (1)
	{
		u8 devStatus;
		devStatus = ReadVirtIODeviceByte(pContext->IODevice.addr + VIRTIO_PCI_STATUS);
		if (devStatus)
		{
			DPrintf(0, ("[%s] Device status is still %02X", __FUNCTION__, (ULONG)devStatus));
			VirtIODeviceReset(&pContext->IODevice);
			devStatus = ReadVirtIODeviceByte(pContext->IODevice.addr + VIRTIO_PCI_STATUS);
			DPrintf(0, ("[%s] Device status on retry %02X", __FUNCTION__, (ULONG)devStatus));
		}
	}
#endif
}

/**********************************************************
Gets integer value for specifies in pEntry->Name name
Parameters:
	NDIS_HANDLE cfg  previously open configuration
	tConfigurationEntry *pEntry - Entry to fill value in
***********************************************************/
static void	GetConfigurationEntry(NDIS_HANDLE cfg, tConfigurationEntry *pEntry)
{
	NDIS_STATUS status;
	const char *statusName;
	NDIS_STRING name;
	PNDIS_CONFIGURATION_PARAMETER pParam = NULL;
	NDIS_PARAMETER_TYPE ParameterType = NdisParameterInteger;
	NdisInitializeString(&name, (PUCHAR)pEntry->Name);
	NdisReadConfiguration(
		&status,
		&pParam,
		cfg,
		&name,
		ParameterType);
	if (status == NDIS_STATUS_SUCCESS)
	{
		ULONG ulValue = pParam->ParameterData.IntegerData;
		if (ulValue >= pEntry->ulMinimal && ulValue <= pEntry->ulMaximal)
		{
			pEntry->ulValue = ulValue;
			statusName = "value";
		}
		else
		{
			statusName = "out of range";
		}
	}
	else
	{
		statusName = "nothing";
	}
	DPrintf(2, ("[%s] %s read for %s - 0x%x",
		__FUNCTION__,
		statusName,
		pEntry->Name,
		pEntry->ulValue));
	NdisFreeString(name);
}

static __inline void DisableLSOPermanently(PARANDIS_ADAPTER *pContext, LPCSTR procname, LPCSTR reason)
{
	if (pContext->Offload.flagsValue & osbT4Lso)
	{
		DPrintf(0, ("[%s] Warning: %s", procname, reason));
		pContext->Offload.flagsValue &= ~osbT4Lso;
		ParaNdis_ResetOffloadSettings(pContext, NULL, NULL);
	}
}

/**********************************************************
Loads NIC parameters from adapter registry key
Parameters:
	context
	PUCHAR *ppNewMACAddress - pointer to hold MAC address if configured from host
***********************************************************/
static void ReadNicConfiguration(PARANDIS_ADAPTER *pContext, PUCHAR *ppNewMACAddress)
{
	NDIS_HANDLE cfg;
	tConfigurationEntries *pConfiguration = ParaNdis_AllocateMemory(pContext, sizeof(tConfigurationEntries));
	if (pConfiguration)
	{
		*pConfiguration = defaultConfiguration;
		cfg = ParaNdis_OpenNICConfiguration(pContext);
		if (cfg)
		{
			GetConfigurationEntry(cfg, &pConfiguration->isLogEnabled);
			GetConfigurationEntry(cfg, &pConfiguration->debugLevel);
			GetConfigurationEntry(cfg, &pConfiguration->ConnectRate);
			GetConfigurationEntry(cfg, &pConfiguration->PrioritySupport);
			GetConfigurationEntry(cfg, &pConfiguration->isPromiscuous);
			GetConfigurationEntry(cfg, &pConfiguration->TxCapacity);
			GetConfigurationEntry(cfg, &pConfiguration->RxCapacity);
			GetConfigurationEntry(cfg, &pConfiguration->connectTimer);
			GetConfigurationEntry(cfg, &pConfiguration->dpcChecker);
			GetConfigurationEntry(cfg, &pConfiguration->InterruptRecovery);
			GetConfigurationEntry(cfg, &pConfiguration->LogStatistics);
			GetConfigurationEntry(cfg, &pConfiguration->HardReset);
			GetConfigurationEntry(cfg, &pConfiguration->PacketFiltering);
			GetConfigurationEntry(cfg, &pConfiguration->ScatterGather);
			GetConfigurationEntry(cfg, &pConfiguration->BatchReceive);
			GetConfigurationEntry(cfg, &pConfiguration->OffloadTxIP);
			GetConfigurationEntry(cfg, &pConfiguration->OffloadTxTCP);
			GetConfigurationEntry(cfg, &pConfiguration->OffloadTxUDP);
			GetConfigurationEntry(cfg, &pConfiguration->OffloadTxLSO);
			GetConfigurationEntry(cfg, &pConfiguration->OffloadRxIP);
			GetConfigurationEntry(cfg, &pConfiguration->HwOffload);
			GetConfigurationEntry(cfg, &pConfiguration->IPPacketsCheck);
			GetConfigurationEntry(cfg, &pConfiguration->IPChecksumFix);
			GetConfigurationEntry(cfg, &pConfiguration->stdIpcsV4);
			GetConfigurationEntry(cfg, &pConfiguration->stdTcpcsV4);
			GetConfigurationEntry(cfg, &pConfiguration->stdTcpcsV6);
			GetConfigurationEntry(cfg, &pConfiguration->stdUdpcsV4);
			GetConfigurationEntry(cfg, &pConfiguration->stdUdpcsV6);
			GetConfigurationEntry(cfg, &pConfiguration->stdLsoV1);
			GetConfigurationEntry(cfg, &pConfiguration->stdLsoV2ip4);
			GetConfigurationEntry(cfg, &pConfiguration->stdLsoV2ip6);
			GetConfigurationEntry(cfg, &pConfiguration->PriorityVlanTagging);
			GetConfigurationEntry(cfg, &pConfiguration->VlanId);
			GetConfigurationEntry(cfg, &pConfiguration->UseMergeableBuffers);
			GetConfigurationEntry(cfg, &pConfiguration->PublishIndices);
			GetConfigurationEntry(cfg, &pConfiguration->MTU);
			GetConfigurationEntry(cfg, &pConfiguration->NumberOfHandledRXPackersInDPC);

	#if !defined(WPP_EVENT_TRACING)
			bDebugPrint = pConfiguration->isLogEnabled.ulValue;
			nDebugLevel = pConfiguration->debugLevel.ulValue;
	#endif
			// ignoring promiscuous setting, nothing to do with it
			pContext->maxFreeTxDescriptors = pConfiguration->TxCapacity.ulValue;
			pContext->NetMaxReceiveBuffers = pConfiguration->RxCapacity.ulValue;
			pContext->ulMilliesToConnect = pConfiguration->connectTimer.ulValue;
			pContext->nEnableDPCChecker = pConfiguration->dpcChecker.ulValue;
			pContext->bDoInterruptRecovery = pConfiguration->InterruptRecovery.ulValue != 0;
			pContext->Limits.nPrintDiagnostic = pConfiguration->LogStatistics.ulValue;
			pContext->uNumberOfHandledRXPacketsInDPC = pConfiguration->NumberOfHandledRXPackersInDPC.ulValue;
			pContext->bDoHardReset = pConfiguration->HardReset.ulValue != 0;
			pContext->bDoSupportPriority = pConfiguration->PrioritySupport.ulValue != 0;
			pContext->ulFormalLinkSpeed  = pConfiguration->ConnectRate.ulValue;
			pContext->ulFormalLinkSpeed *= 1000000;
			pContext->bDoPacketFiltering = pConfiguration->PacketFiltering.ulValue != 0;
			pContext->bUseScatterGather  = pConfiguration->ScatterGather.ulValue != 0;
			pContext->bBatchReceive      = pConfiguration->BatchReceive.ulValue != 0;
			pContext->bDoHardwareOffload = pConfiguration->HwOffload.ulValue != 0;
			pContext->bDoIPCheck = pConfiguration->IPPacketsCheck.ulValue != 0;
			pContext->bFixIPChecksum = pConfiguration->IPChecksumFix.ulValue != 0;
			pContext->Offload.flagsValue = 0;
			if (pConfiguration->OffloadTxIP.ulValue) pContext->Offload.flagsValue |= osbT4IpChecksum | osbT4IpOptionsChecksum;
			if (pConfiguration->OffloadTxTCP.ulValue) pContext->Offload.flagsValue |= osbT4TcpChecksum | osbT4TcpOptionsChecksum;
			if (pConfiguration->OffloadTxUDP.ulValue) pContext->Offload.flagsValue |= osbT4UdpChecksum;
			if (pConfiguration->OffloadTxLSO.ulValue) pContext->Offload.flagsValue |= osbT4Lso | osbT4LsoIp | osbT4LsoTcp;
			if (pConfiguration->OffloadRxIP.ulValue) pContext->Offload.flagsValue |= osbT4IpRxChecksum | osbT4IpOptionsChecksum;
			/* full packet size that can be configured as GSO for VIRTIO is short */
			/* NDIS test fails sometimes fails on segments 50-60K */
			pContext->Offload.maxPacketSize = PARANDIS_MAX_LSO_SIZE;
			pContext->InitialOffloadParameters.IPv4Checksum = (UCHAR)pConfiguration->stdIpcsV4.ulValue;
			pContext->InitialOffloadParameters.TCPIPv4Checksum = (UCHAR)pConfiguration->stdTcpcsV4.ulValue;
			pContext->InitialOffloadParameters.TCPIPv6Checksum = (UCHAR)pConfiguration->stdTcpcsV6.ulValue;
			pContext->InitialOffloadParameters.UDPIPv4Checksum = (UCHAR)pConfiguration->stdUdpcsV4.ulValue;
			pContext->InitialOffloadParameters.UDPIPv6Checksum = (UCHAR)pConfiguration->stdUdpcsV6.ulValue;
			pContext->InitialOffloadParameters.LsoV1 = (UCHAR)pConfiguration->stdLsoV1.ulValue;
			pContext->InitialOffloadParameters.LsoV2IPv4 = (UCHAR)pConfiguration->stdLsoV2ip4.ulValue;
			pContext->InitialOffloadParameters.LsoV2IPv6 = (UCHAR)pConfiguration->stdLsoV2ip6.ulValue;
			pContext->ulPriorityVlanSetting = pConfiguration->PriorityVlanTagging.ulValue;
			pContext->VlanId = pConfiguration->VlanId.ulValue;
			pContext->bUseMergedBuffers = pConfiguration->UseMergeableBuffers.ulValue != 0;
			pContext->bDoPublishIndices = pConfiguration->PublishIndices.ulValue != 0;
			pContext->MaxPacketSize.nMaxDataSize = pConfiguration->MTU.ulValue;
			if (!pContext->bDoSupportPriority)
				pContext->ulPriorityVlanSetting = 0;
			// if Vlan not supported
			if (~pContext->ulPriorityVlanSetting & 2)
				pContext->VlanId = 0;
			if (1)
			{
				NDIS_STATUS status;
				PVOID p;
				UINT  len = 0;
				NdisReadNetworkAddress(&status, &p, &len, cfg);
				if (status == NDIS_STATUS_SUCCESS && len == sizeof(pContext->CurrentMacAddress))
				{
					*ppNewMACAddress = ParaNdis_AllocateMemory(pContext, sizeof(pContext->CurrentMacAddress));
					if (*ppNewMACAddress)
					{
						NdisMoveMemory(*ppNewMACAddress, p, len);
					}
					else
					{
						DPrintf(0, ("[%s] MAC address present, but some problem also...", __FUNCTION__));
					}
				}
				else if (len && len != sizeof(pContext->CurrentMacAddress))
				{
					DPrintf(0, ("[%s] MAC address has wrong length of %d", __FUNCTION__, len));
				}
				else
				{
					DPrintf(4, ("[%s] Nothing read for MAC, error %X", __FUNCTION__, status));
				}
			}
			NdisCloseConfiguration(cfg);
		}
		NdisFreeMemory(pConfiguration, 0, 0);
	}
}

void ParaNdis_ResetOffloadSettings(PARANDIS_ADAPTER *pContext, tOffloadSettingsFlags *pDest, PULONG from)
{
	if (!pDest) pDest = &pContext->Offload.flags;
	if (!from)  from = &pContext->Offload.flagsValue;
	pDest->fTxIPChecksum = !!(*from & osbT4IpChecksum);
	pDest->fTxTCPChecksum = !!(*from & osbT4TcpChecksum);
	pDest->fTxUDPChecksum = !!(*from & osbT4UdpChecksum);
	pDest->fTxTCPOptions = !!(*from & osbT4TcpOptionsChecksum);
	pDest->fTxIPOptions = !!(*from & osbT4IpOptionsChecksum);
	pDest->fTxLso = !!(*from & osbT4Lso);
	pDest->fTxLsoIP = !!(*from & osbT4LsoIp);
	pDest->fTxLsoTCP = !!(*from & osbT4LsoTcp);
	pDest->fRxIPChecksum = !!(*from & osbT4IpRxChecksum);
	pDest->fTxIPOptions = !!(*from & osbT4IpOptionsChecksum);
	pDest->fRxIPOptions = 0;
	pDest->fRxTCPChecksum = 0;
	pDest->fRxTCPOptions = 0;
	pDest->fRxUDPChecksum = 0;
}
/**********************************************************
Enumerates adapter resources and fills the structure holding them
Verifies that IO assigned and has correct size
Verifies that interrupt assigned
Parameters:
	PNDIS_RESOURCE_LIST RList - list of resources, received from NDIS
	tAdapterResources *pResources - structure to fill
Return value:
	TRUE if everything is OK
***********************************************************/
static BOOLEAN GetAdapterResources(PNDIS_RESOURCE_LIST RList, tAdapterResources *pResources)
{
	UINT i;
	NdisZeroMemory(pResources, sizeof(*pResources));
	for (i = 0; i < RList->Count; ++i)
	{
		ULONG type = RList->PartialDescriptors[i].Type;
		if (type == CmResourceTypePort)
		{
			PHYSICAL_ADDRESS Start = RList->PartialDescriptors[i].u.Port.Start;
			ULONG len = RList->PartialDescriptors[i].u.Port.Length;
			DPrintf(0, ("Found IO ports at %08lX(%d)", Start.LowPart, len));
			pResources->ulIOAddress = Start.LowPart;
			pResources->IOLength = len;
		}
		else if (type == CmResourceTypeInterrupt)
		{
			pResources->Vector = RList->PartialDescriptors[i].u.Interrupt.Vector;
			pResources->Level = RList->PartialDescriptors[i].u.Interrupt.Level;
			pResources->Affinity = RList->PartialDescriptors[i].u.Interrupt.Affinity;
			pResources->InterruptFlags = RList->PartialDescriptors[i].Flags;
			DPrintf(0, ("Found Interrupt vector %d, level %d, affinity %X, flags %X",
				pResources->Vector, pResources->Level, (ULONG)pResources->Affinity, pResources->InterruptFlags));
		}
	}
	return pResources->ulIOAddress && pResources->Vector && pResources->IOLength == 32;
}

static void DumpVirtIOFeatures(VirtIODevice *pIO)
{
	const struct {	ULONG bitmask; 	const PCHAR Name; } Features[] =
	{

		{VIRTIO_NET_F_CSUM, "VIRTIO_NET_F_CSUM" },
		{VIRTIO_NET_F_GUEST_CSUM, "VIRTIO_NET_F_GUEST_CSUM" },
		{VIRTIO_NET_F_MAC, "VIRTIO_NET_F_MAC" },
		{VIRTIO_NET_F_GSO, "VIRTIO_NET_F_GSO" },
		{VIRTIO_NET_F_GUEST_TSO4, "VIRTIO_NET_F_GUEST_TSO4"},
		{VIRTIO_NET_F_GUEST_TSO6, "VIRTIO_NET_F_GUEST_TSO6"},
		{VIRTIO_NET_F_GUEST_ECN, "VIRTIO_NET_F_GUEST_ECN"},
		{VIRTIO_NET_F_GUEST_UFO, "VIRTIO_NET_F_GUEST_UFO"},
		{VIRTIO_NET_F_HOST_TSO4, "VIRTIO_NET_F_HOST_TSO4"},
		{VIRTIO_NET_F_HOST_TSO6, "VIRTIO_NET_F_HOST_TSO6"},
		{VIRTIO_NET_F_HOST_ECN, "VIRTIO_NET_F_HOST_ECN"},
		{VIRTIO_NET_F_HOST_UFO, "VIRTIO_NET_F_HOST_UFO"},
		{VIRTIO_NET_F_MRG_RXBUF, "VIRTIO_NET_F_MRG_RXBUF"},
		{VIRTIO_NET_F_STATUS, "VIRTIO_NET_F_STATUS"},
		{VIRTIO_F_INDIRECT, "VIRTIO_F_INDIRECT"},
		{VIRTIO_F_PUBLISH_INDICES, "VIRTIO_F_PUBLISH_INDICES"},
	};
	UINT i;
	for (i = 0; i < sizeof(Features)/sizeof(Features[0]); ++i)
	{
		if (VirtIODeviceGetHostFeature(pIO, Features[i].bitmask))
		{
			DPrintf(0, ("VirtIO Host Feature %s", Features[i].Name));
		}
	}
}

/**********************************************************
	Only for test. Prints out if the interrupt line is ON
Parameters:
Return value:
***********************************************************/
static void JustForCheckClearInterrupt(PARANDIS_ADAPTER *pContext, const char *Label)
{
	if (pContext->bEnableInterruptChecking)
	{
		ULONG ulActive;
		ulActive = VirtIODeviceISR(&pContext->IODevice);
		if (ulActive)
		{
			DPrintf(0,("WARNING: Interrupt Line %d(%s)!", ulActive, Label));
		}
	}
}

/**********************************************************
Prints out statistics
***********************************************************/
static void PrintStatistics(PARANDIS_ADAPTER *pContext)
{
	DPrintf(0, ("[Diag!] RX buffers at VIRTIO %d of %d", pContext->NetNofReceiveBuffers, pContext->NetMaxReceiveBuffers));
	DPrintf(0, ("[Diag!] TX desc available %d/%d, buf %d/min. %d",
		pContext->nofFreeTxDescriptors, pContext->maxFreeTxDescriptors,
		pContext->nofFreeHardwareBuffers, pContext->minFreeHardwareBuffers));
	pContext->minFreeHardwareBuffers = pContext->nofFreeHardwareBuffers;
	DPrintf(0, ("[Diag!] TX packets to return %d", pContext->NetTxPacketsToReturn));
	DPrintf(0, ("[Diag!] Bytes transmitted %I64u, received %I64u", pContext->Statistics.ifHCOutOctets, pContext->Statistics.ifHCInOctets));
}


/**********************************************************
Initializes the context structure
Major variables, received from NDIS on initialization, must be be set before this call
(for ex. pContext->MiniportHandle)

If this procedure fails, no need to call
	ParaNdis_CleanupContext


Parameters:
Return value:
	SUCCESS, if resources are OK
	NDIS_STATUS_RESOURCE_CONFLICT if not
***********************************************************/
NDIS_STATUS ParaNdis_InitializeContext(
	PARANDIS_ADAPTER *pContext,
	PNDIS_RESOURCE_LIST pResourceList)
{
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	PUCHAR pNewMacAddress = NULL;
	DEBUG_ENTRY(0);
	/* read first PCI IO bar*/
	//ulIOAddress = ReadPCIConfiguration(miniportAdapterHandle, 0x10);
	/* check this is IO and assigned */
	ReadNicConfiguration(pContext, &pNewMacAddress);
	if (pNewMacAddress)
	{
		if (ParaNdis_ValidateMacAddress(pNewMacAddress, TRUE))
		{
			DPrintf(0, ("[%s] WARNING: MAC address reloaded", __FUNCTION__));
			NdisMoveMemory(pContext->CurrentMacAddress, pNewMacAddress, sizeof(pContext->CurrentMacAddress));
		}
		else
		{
			DPrintf(0, ("[%s] WARNING: Invalid MAC address ignored", __FUNCTION__));
		}
		NdisFreeMemory(pNewMacAddress, 0, 0);
	}

	pContext->MaxPacketSize.nMaxFullSizeOS = pContext->MaxPacketSize.nMaxDataSize + ETH_HEADER_SIZE;
	pContext->MaxPacketSize.nMaxFullSizeHwTx = pContext->MaxPacketSize.nMaxFullSizeOS;
	pContext->MaxPacketSize.nMaxFullSizeHwRx = pContext->MaxPacketSize.nMaxFullSizeOS + ETH_PRIORITY_HEADER_SIZE;
	if (pContext->ulPriorityVlanSetting)
		pContext->MaxPacketSize.nMaxFullSizeHwTx = pContext->MaxPacketSize.nMaxFullSizeHwRx;

	if (GetAdapterResources(pResourceList, &pContext->AdapterResources) &&
		NDIS_STATUS_SUCCESS == NdisMRegisterIoPortRange(
			&pContext->pIoPortOffset,
			pContext->MiniportHandle,
			pContext->AdapterResources.ulIOAddress,
			pContext->AdapterResources.IOLength)
		)
	{
		if (pContext->AdapterResources.InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE)
		{
			DPrintf(0, ("[%s] Message interrupt assigned", __FUNCTION__));
			pContext->bUsingMSIX = TRUE;
		}

		VirtIODeviceSetIOAddress(&pContext->IODevice, pContext->AdapterResources.ulIOAddress);
		JustForCheckClearInterrupt(pContext, "init 0");
		ParaNdis_ResetVirtIONetDevice(pContext);
		JustForCheckClearInterrupt(pContext, "init 1");
		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
		JustForCheckClearInterrupt(pContext, "init 2");
		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER);
		DumpVirtIOFeatures(&pContext->IODevice);
		JustForCheckClearInterrupt(pContext, "init 3");
		pContext->bLinkDetectSupported = 0 != VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_STATUS);
		pContext->nVirtioHeaderSize = sizeof(virtio_net_hdr_basic);
		if (!pContext->bUseMergedBuffers && VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_MRG_RXBUF))
		{
			DPrintf(0, ("[%s] Not using mergeable buffers", __FUNCTION__));
		}
		else
		{
			pContext->bUseMergedBuffers = VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_MRG_RXBUF) != 0;
			if (pContext->bUseMergedBuffers)
			{
				pContext->nVirtioHeaderSize = sizeof(virtio_net_hdr_ext);
				VirtIODeviceEnableGuestFeature(&pContext->IODevice, VIRTIO_NET_F_MRG_RXBUF);
			}
		}
		if (VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_MAC))
		{
			VirtIODeviceGet(
				&pContext->IODevice,
				pContext->bUsingMSIX ? 4 : 0, // offsetof(struct virtio_net_config, mac)
				&pContext->PermanentMacAddress,
				ETH_LENGTH_OF_ADDRESS);
			if (!ParaNdis_ValidateMacAddress(pContext->PermanentMacAddress, FALSE))
			{
				DPrintf(0,("Invalid device MAC ignored(%02x-%02x-%02x-%02x-%02x-%02x)",
					pContext->PermanentMacAddress[0],
					pContext->PermanentMacAddress[1],
					pContext->PermanentMacAddress[2],
					pContext->PermanentMacAddress[3],
					pContext->PermanentMacAddress[4],
					pContext->PermanentMacAddress[5]));
				NdisZeroMemory(pContext->PermanentMacAddress, sizeof(pContext->PermanentMacAddress));
			}
		}

		if (ETH_IS_EMPTY(pContext->PermanentMacAddress))
		{
			DPrintf(0, ("No device MAC present, use default"));
			pContext->PermanentMacAddress[0] = 0x02;
			pContext->PermanentMacAddress[1] = 0x50;
			pContext->PermanentMacAddress[2] = 0xF2;
			pContext->PermanentMacAddress[3] = 0x00;
			pContext->PermanentMacAddress[4] = 0x01;
			pContext->PermanentMacAddress[5] = 0x80 | (UCHAR)(pContext->ulUniqueID & 0xFF);
		}
		DPrintf(0,("Device MAC = %02x-%02x-%02x-%02x-%02x-%02x",
			pContext->PermanentMacAddress[0],
			pContext->PermanentMacAddress[1],
			pContext->PermanentMacAddress[2],
			pContext->PermanentMacAddress[3],
			pContext->PermanentMacAddress[4],
			pContext->PermanentMacAddress[5]));

		if (ETH_IS_EMPTY(pContext->CurrentMacAddress))
		{
			NdisMoveMemory(
				&pContext->CurrentMacAddress,
				&pContext->PermanentMacAddress,
				ETH_LENGTH_OF_ADDRESS);
		}
		else
		{
			DPrintf(0,("Current MAC = %02x-%02x-%02x-%02x-%02x-%02x",
				pContext->CurrentMacAddress[0],
				pContext->CurrentMacAddress[1],
				pContext->CurrentMacAddress[2],
				pContext->CurrentMacAddress[3],
				pContext->CurrentMacAddress[4],
				pContext->CurrentMacAddress[5]));
		}
		if (pContext->bDoPublishIndices)
			pContext->bDoPublishIndices = VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_F_PUBLISH_INDICES) != 0;
		if (pContext->bDoPublishIndices && VirtIODeviceHasFeature(VIRTIO_F_PUBLISH_INDICES))
		{
			VirtIODeviceEnableGuestFeature(&pContext->IODevice, VIRTIO_F_PUBLISH_INDICES);
		}
		else
		{
			pContext->bDoPublishIndices = FALSE;
		}
	}
	else
	{
		DPrintf(0, ("[%s] Error: Incomplete resources", __FUNCTION__));
		/* avoid deregistering if failed */
		pContext->AdapterResources.ulIOAddress = 0;
		status = NDIS_STATUS_RESOURCE_CONFLICT;
	}


	if (pContext->bDoHardwareOffload)
 	{
		ULONG dependentOptions;
		dependentOptions = osbT4TcpChecksum | osbT4UdpChecksum | osbT4TcpOptionsChecksum;
		if (!VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_CSUM) &&
			(pContext->Offload.flagsValue & dependentOptions))
		{
			DPrintf(0, ("[%s] Host does not support CSUM, disabling CS offload", __FUNCTION__) );
			pContext->Offload.flagsValue &= ~dependentOptions;
		}
		dependentOptions = osbT4IpChecksum | osbT4IpOptionsChecksum;
		if (pContext->Offload.flagsValue & dependentOptions)
		{
			DPrintf(0, ("[%s] Host does not support IPCS, disabling it", __FUNCTION__) );
			pContext->Offload.flagsValue &= ~dependentOptions;
		}
	}

	// now, after we checked the capabilities, we can initialize current
	// configuration of offload tasks
	ParaNdis_ResetOffloadSettings(pContext, NULL, NULL);
	if (pContext->Offload.flags.fTxLso && !pContext->bUseScatterGather)
	{
		DisableLSOPermanently(pContext, __FUNCTION__, "SG is not active");
	}
	if (pContext->Offload.flags.fTxLso &&
		!VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_HOST_TSO4))
	{
		DisableLSOPermanently(pContext, __FUNCTION__, "Host does not support TSO");
	}


	NdisInitializeEvent(&pContext->ResetEvent);
	DEBUG_EXIT_STATUS(0, status);
	return status;
}

/**********************************************************
Free the resources allocated for VirtIO buffer descriptor
Parameters:
	PVOID pParam				pIONetDescriptor to free
	BOOLEAN bRemoveFromList		TRUE, if also remove it from list
***********************************************************/
static void VirtIONetFreeBufferDescriptor(PARANDIS_ADAPTER *pContext, pIONetDescriptor pBufferDescriptor)
{
	if(pBufferDescriptor)
	{
		if (pBufferDescriptor->pHolder)
			ParaNdis_UnbindBufferFromPacket(pContext, pBufferDescriptor);
		if (pBufferDescriptor->DataInfo.Virtual)
			ParaNdis_FreePhysicalMemory(pContext, &pBufferDescriptor->DataInfo);
		if (pBufferDescriptor->HeaderInfo.Virtual)
			ParaNdis_FreePhysicalMemory(pContext, &pBufferDescriptor->HeaderInfo);
		NdisFreeMemory(pBufferDescriptor, 0, 0);
	}
}

/**********************************************************
Free all the buffer descriptors from specified list
Parameters:
	PLIST_ENTRY pListRoot			list containing pIONetDescriptor structures
	PNDIS_SPIN_LOCK pLock			lock to protest this list
Return value:
***********************************************************/
static void FreeDescriptorsFromList(PARANDIS_ADAPTER *pContext, PLIST_ENTRY pListRoot, PNDIS_SPIN_LOCK pLock)
{
	pIONetDescriptor pBufferDescriptor;
	LIST_ENTRY TempList;
	InitializeListHead(&TempList);
	NdisAcquireSpinLock(pLock);
	while(!IsListEmpty(pListRoot))
	{
		pBufferDescriptor = (pIONetDescriptor)RemoveHeadList(pListRoot);
		InsertTailList(&TempList, &pBufferDescriptor->listEntry);
	}
	NdisReleaseSpinLock(pLock);
	while(!IsListEmpty(&TempList))
	{
		pBufferDescriptor = (pIONetDescriptor)RemoveHeadList(&TempList);
		VirtIONetFreeBufferDescriptor(pContext, pBufferDescriptor);
	}
}

static pIONetDescriptor AllocatePairOfBuffersOnInit(
	PARANDIS_ADAPTER *pContext,
	ULONG size1,
	ULONG size2,
	BOOLEAN bForTx)
{
	pIONetDescriptor p;
	p = (pIONetDescriptor)ParaNdis_AllocateMemory(pContext, sizeof(*p));
	if (p)
	{
		BOOLEAN b1 = FALSE, b2 = FALSE;
		NdisZeroMemory(p, sizeof(*p));
		p->HeaderInfo.size = size1;
		p->DataInfo.size   = size2;
		p->HeaderInfo.IsCached = p->DataInfo.IsCached = 1;
		p->HeaderInfo.IsTX = p->DataInfo.IsTX = bForTx;
		p->nofUsedBuffers = 0;
		b1 = ParaNdis_InitialAllocatePhysicalMemory(pContext, &p->HeaderInfo);
		if (b1) b2 = ParaNdis_InitialAllocatePhysicalMemory(pContext, &p->DataInfo);
		if (b1 && b2)
		{
			BOOLEAN b = bForTx || ParaNdis_BindBufferToPacket(pContext, p);
			if (!b)
			{
				DPrintf(0, ("[INITPHYS](%s) Failed to bind memory to net packet", bForTx ? "TX" : "RX"));
				VirtIONetFreeBufferDescriptor(pContext, p);
				p = NULL;
			}
		}
		else
		{
			if (b1) ParaNdis_FreePhysicalMemory(pContext, &p->HeaderInfo);
			if (b2) ParaNdis_FreePhysicalMemory(pContext, &p->DataInfo);
			NdisFreeMemory(p, 0, 0);
			p = NULL;
			DPrintf(0, ("[INITPHYS](%s) Failed to allocate memory block", bForTx ? "TX" : "RX"));
		}
	}
	if (p)
	{
		DPrintf(3, ("[INITPHYS](%s) Header v%p(p%08lX), Data v%p(p%08lX)", bForTx ? "TX" : "RX",
			p->HeaderInfo.Virtual, p->HeaderInfo.Physical.LowPart,
			p->DataInfo.Virtual, p->DataInfo.Physical.LowPart));
	}
	return p;
}

/**********************************************************
Allocates TX buffers according to startup setting (pContext->maxFreeTxDescriptors as got from registry)
Buffers are chained in NetFreeSendBuffers
Parameters:
	context
***********************************************************/
static void PrepareTransmitBuffers(PARANDIS_ADAPTER *pContext)
{
	UINT nBuffers, nMaxBuffers;
	DEBUG_ENTRY(4);
	nMaxBuffers = VirtIODeviceGetQueueSize(pContext->NetSendQueue) / 2;
	if (nMaxBuffers > pContext->maxFreeTxDescriptors) nMaxBuffers = pContext->maxFreeTxDescriptors;

	for (nBuffers = 0; nBuffers < nMaxBuffers; ++nBuffers)
	{
		pIONetDescriptor pBuffersDescriptor =
			AllocatePairOfBuffersOnInit(
				pContext,
				pContext->nVirtioHeaderSize,
				pContext->MaxPacketSize.nMaxFullSizeHwTx,
				TRUE);
		if (!pBuffersDescriptor) break;

		NdisZeroMemory(pBuffersDescriptor->HeaderInfo.Virtual, pBuffersDescriptor->HeaderInfo.size);
		InsertTailList(&pContext->NetFreeSendBuffers, &pBuffersDescriptor->listEntry);
		pContext->nofFreeTxDescriptors++;
	}

	pContext->maxFreeTxDescriptors = pContext->nofFreeTxDescriptors;
	pContext->nofFreeHardwareBuffers = pContext->nofFreeTxDescriptors * 2;
	pContext->maxFreeHardwareBuffers = pContext->minFreeHardwareBuffers = pContext->nofFreeHardwareBuffers;
	DPrintf(0, ("[%s] available %d Tx descriptors, %d hw buffers",
		__FUNCTION__, pContext->nofFreeTxDescriptors, pContext->nofFreeHardwareBuffers));
}

static BOOLEAN AddRxBufferToQueue(PARANDIS_ADAPTER *pContext, pIONetDescriptor pBufferDescriptor)
{
	UINT nBuffersToSubmit = 2;
	struct VirtIOBufferDescriptor sg[2];
	if (!pContext->bUseMergedBuffers)
	{
		sg[0].physAddr = pBufferDescriptor->HeaderInfo.Physical;
		sg[0].ulSize = pBufferDescriptor->HeaderInfo.size;
		sg[1].physAddr = pBufferDescriptor->DataInfo.Physical;
		sg[1].ulSize = pBufferDescriptor->DataInfo.size;
	}
	else
	{
		sg[0].physAddr = pBufferDescriptor->DataInfo.Physical;
		sg[0].ulSize = pBufferDescriptor->DataInfo.size;
		nBuffersToSubmit = 1;
	}
	return 0 == pContext->NetReceiveQueue->vq_ops->add_buf(
		pContext->NetReceiveQueue,
		sg,
		0,
		nBuffersToSubmit,
		pBufferDescriptor);
}


/**********************************************************
Allocates maximum RX buffers for incoming packets
Buffers are chained in NetReceiveBuffers
Parameters:
	context
***********************************************************/
static int PrepareReceiveBuffers(PARANDIS_ADAPTER *pContext)
{
	int nRet = 0;
	UINT i;
	DEBUG_ENTRY(4);

	for (i = 0; i < pContext->NetMaxReceiveBuffers; ++i)
	{
		ULONG size1 = pContext->bUseMergedBuffers ? 4 : pContext->nVirtioHeaderSize;
		ULONG size2 = pContext->MaxPacketSize.nMaxFullSizeHwRx +
			(pContext->bUseMergedBuffers ? pContext->nVirtioHeaderSize : 0);
		pIONetDescriptor pBuffersDescriptor =
			AllocatePairOfBuffersOnInit(pContext, size1, size2,	FALSE);
		if (!pBuffersDescriptor) break;

		if (!AddRxBufferToQueue(pContext, pBuffersDescriptor))
		{
			VirtIONetFreeBufferDescriptor(pContext, pBuffersDescriptor);
			break;
		}

		InsertTailList(&pContext->NetReceiveBuffers, &pBuffersDescriptor->listEntry);

		pContext->NetNofReceiveBuffers++;
	}

	pContext->NetMaxReceiveBuffers = pContext->NetNofReceiveBuffers;
	DPrintf(0, ("[%s] MaxReceiveBuffers %d\n", __FUNCTION__, pContext->NetMaxReceiveBuffers) );

	return nRet;
}


/**********************************************************
Initializes VirtIO buffering and related stuff:
Allocates RX and TX queues and buffers
Parameters:
	context
Return value:
	TRUE if both queues are allocated
***********************************************************/
static NDIS_STATUS ParaNdis_VirtIONetInit(PARANDIS_ADAPTER *pContext)
{
	NDIS_STATUS status = NDIS_STATUS_RESOURCES;
	DEBUG_ENTRY(0);

	// We expect two virtqueues, receive then send.
	pContext->NetReceiveQueue = VirtIODeviceFindVirtualQueue(&pContext->IODevice, 0, NULL); //vp_find_vq(vdev, 0, skb_recv_done);
	pContext->NetSendQueue = VirtIODeviceFindVirtualQueue(&pContext->IODevice, 1, NULL); //vp_find_vq(vdev, 0, skb_recv_done);

	if (pContext->NetReceiveQueue && pContext->NetSendQueue)
	{
		PrepareTransmitBuffers(pContext);
		PrepareReceiveBuffers(pContext);
		if (pContext->nofFreeTxDescriptors &&
			pContext->NetMaxReceiveBuffers &&
			pContext->maxFreeHardwareBuffers)
		{
			pContext->sgTxGatherTable = ParaNdis_AllocateMemory(pContext,
				pContext->maxFreeHardwareBuffers * sizeof(pContext->sgTxGatherTable[0]));
			if (!pContext->sgTxGatherTable)
			{
				DisableLSOPermanently(pContext, __FUNCTION__, "Can not allocate SG table");
			}
			status = NDIS_STATUS_SUCCESS;
		}
	}
	else
	{
		if(pContext->NetSendQueue)
			VirtIODeviceDeleteVirtualQueue(pContext->NetSendQueue);
		if(pContext->NetReceiveQueue)
			VirtIODeviceDeleteVirtualQueue(pContext->NetReceiveQueue);
		pContext->NetSendQueue = pContext->NetReceiveQueue = NULL;
	}
	return status;
}

/**********************************************************
Finishes initialization of context structure, calling also version dependent part
If this procedure failed, ParaNdis_CleanupContext must be called
Parameters:
	context
Return value:
	SUCCESS or some kind of failure
***********************************************************/
NDIS_STATUS ParaNdis_FinishInitialization(PARANDIS_ADAPTER *pContext)
{
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	DEBUG_ENTRY(0);

	NdisAllocateSpinLock(&pContext->SendLock);
#if !defined(UNIFY_LOCKS)
	NdisAllocateSpinLock(&pContext->ReceiveLock);
#endif

	InitializeListHead(&pContext->NetReceiveBuffers);
	InitializeListHead(&pContext->NetReceiveBuffersWaiting);
	InitializeListHead(&pContext->NetSendBuffersInUse);
	InitializeListHead(&pContext->NetFreeSendBuffers);

	status = ParaNdis_FinishSpecificInitialization(pContext);

	if (status == NDIS_STATUS_SUCCESS)
	{
		status = ParaNdis_VirtIONetInit(pContext);
	}

	pContext->Limits.nReusedRxBuffers = pContext->NetMaxReceiveBuffers / 4 + 1;
	pContext->uRXPacketsDPCLimit = pContext->NetMaxReceiveBuffers * 2;

	if (status == NDIS_STATUS_SUCCESS)
	{
		JustForCheckClearInterrupt(pContext, "start 3");
		pContext->bEnableInterruptHandlingDPC = TRUE;
		pContext->powerState = NetDeviceStateD0;
		VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
		JustForCheckClearInterrupt(pContext, "start 4");
		pContext->NetReceiveQueue->vq_ops->kick(pContext->NetReceiveQueue);
	}
	DEBUG_EXIT_STATUS(0, status);
	return status;
}

/**********************************************************
Releases VirtIO related resources - queues and buffers
Parameters:
	context
Return value:
***********************************************************/
static void VirtIONetRelease(PARANDIS_ADAPTER *pContext)
{
	BOOLEAN b;
	DEBUG_ENTRY(0);

	/* list NetReceiveBuffersWaiting must be free */
	do
	{
		NdisAcquireSpinLock(&pContext->ReceiveLock);
		b = !IsListEmpty(&pContext->NetReceiveBuffersWaiting);
		NdisReleaseSpinLock(&pContext->ReceiveLock);
		if (b)
		{
			DPrintf(0, ("[%s] There are waiting buffers", __FUNCTION__));
			PrintStatistics(pContext);
			NdisMSleep(5000000);
		}
	}while (b);

	if(pContext->NetSendQueue)
		pContext->NetSendQueue->vq_ops->shutdown(pContext->NetSendQueue);
	if(pContext->NetReceiveQueue)
		pContext->NetReceiveQueue->vq_ops->shutdown(pContext->NetReceiveQueue);
	if(pContext->NetSendQueue)
		VirtIODeviceDeleteVirtualQueue(pContext->NetSendQueue);
	if(pContext->NetReceiveQueue)
		VirtIODeviceDeleteVirtualQueue(pContext->NetReceiveQueue);
	pContext->NetSendQueue = pContext->NetReceiveQueue = NULL;

	/* intentionally commented out
	FreeDescriptorsFromList(
		pContext,
		&pContext->NetReceiveBuffersWaiting,
		&pContext->ReceiveLock);
	*/

	/* this can be freed, queue shut down */
	FreeDescriptorsFromList(
		pContext,
		&pContext->NetReceiveBuffers,
		&pContext->ReceiveLock);

	/* this can be freed, queue shut down */
	FreeDescriptorsFromList(
		pContext,
		&pContext->NetSendBuffersInUse,
		&pContext->SendLock);

	/* this can be freed, send disabled */
	FreeDescriptorsFromList(
		pContext,
		&pContext->NetFreeSendBuffers,
		&pContext->SendLock);
	PrintStatistics(pContext);
	if (pContext->sgTxGatherTable)
	{
		NdisFreeMemory(pContext->sgTxGatherTable, 0, 0);
	}
}


static void PreventDPCServicing(PARANDIS_ADAPTER *pContext)
{
	LONG inside;;
	pContext->bEnableInterruptHandlingDPC = FALSE;
	do
	{
		inside = InterlockedIncrement(&pContext->counterDPCInside);
		InterlockedDecrement(&pContext->counterDPCInside);
		if (inside > 1)
		{
			DPrintf(0, ("[%s] waiting!", __FUNCTION__));
			NdisMSleep(20000);
		}
	} while (inside > 1);
}

/**********************************************************
Frees all the resources allocated when the context initialized,
	calling also version-dependent part
Parameters:
	context
***********************************************************/
VOID ParaNdis_CleanupContext(PARANDIS_ADAPTER *pContext)
{
	/* disable any interrupt generation */
	if (pContext->IODevice.addr)
	{
		//int nActive;
		//nActive = VirtIODeviceISR(&pContext->IODevice);
		VirtIODeviceRemoveStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
		JustForCheckClearInterrupt(pContext, "exit 1");
		//nActive += VirtIODeviceISR(&pContext->IODevice);
		//nActive += VirtIODeviceISR(&pContext->IODevice);
		//DPrintf(0, ("cleanup %d", nActive));
	}

	PreventDPCServicing(pContext);

	/****************************************
	ensure all the incoming packets returned,
	free all the buffers and their descriptors
	*****************************************/

	if (pContext->IODevice.addr)
	{
		JustForCheckClearInterrupt(pContext, "exit 2");
		ParaNdis_ResetVirtIONetDevice(pContext);
		JustForCheckClearInterrupt(pContext, "exit 3");
	}
	VirtIONetRelease(pContext);

	ParaNdis_FinalizeCleanup(pContext);

	if (pContext->SendLock.SpinLock)
	{
		NdisFreeSpinLock(&pContext->SendLock);
	}

#if !defined(UNIFY_LOCKS)
	if (pContext->ReceiveLock.SpinLock)
	{
		NdisFreeSpinLock(&pContext->ReceiveLock);
	}
#endif

	if (pContext->AdapterResources.ulIOAddress)
	{
		NdisMDeregisterIoPortRange(
			pContext->MiniportHandle,
			pContext->AdapterResources.ulIOAddress,
			pContext->AdapterResources.IOLength,
			pContext->pIoPortOffset);
		pContext->AdapterResources.ulIOAddress = 0;
	}
}


/**********************************************************
System shutdown handler (shutdown, restart, bugcheck)
Parameters:
	context
***********************************************************/
VOID ParaNdis_OnShutdown(PARANDIS_ADAPTER *pContext)
{
	DEBUG_ENTRY(0); // this is only for kdbg :)
	ParaNdis_ResetVirtIONetDevice(pContext);
}

/**********************************************************
Handles hardware interrupt
Parameters:
	context
	ULONG knownInterruptSources - bitmask of
Return value:
	TRUE, if it is our interrupt
	sets *pRunDpc to TRUE if the DPC should be fired
***********************************************************/
BOOLEAN ParaNdis_OnInterrupt(
	PARANDIS_ADAPTER *pContext,
	OUT BOOLEAN *pRunDpc,
	ULONG knownInterruptSources)
{
	ULONG status;
	BOOLEAN b = FALSE;
	if (knownInterruptSources == isAny)
	{
		status = VirtIODeviceISR(&pContext->IODevice);
		// ignore interrupts with invalid status bits
		if (
			status == VIRTIO_NET_INVALID_INTERRUPT_STATUS ||
			pContext->powerState != NetDeviceStateD0
			)
			status = 0;
		if (status)
		{
			*pRunDpc = TRUE;
			b = TRUE;
			NdisGetCurrentSystemTime(&pContext->LastInterruptTimeStamp);
			ParaNdis_VirtIOEnableIrqSynchronized(pContext, isAny, FALSE);
			status = (status & 2) ? isControl : 0;
			status |= isReceive | isTransmit;
			InterlockedOr(&pContext->InterruptStatus, (LONG)status);
		}
	}
	else
	{
		b = TRUE;
		*pRunDpc = TRUE;
		NdisGetCurrentSystemTime(&pContext->LastInterruptTimeStamp);
		InterlockedOr(&pContext->InterruptStatus, (LONG)knownInterruptSources);
		ParaNdis_VirtIOEnableIrqSynchronized(pContext, knownInterruptSources, FALSE);
		status = knownInterruptSources;
	}
	DPrintf(5, ("[%s](src%X)=>st%X", __FUNCTION__, knownInterruptSources, status));
	return b;
}


/**********************************************************
It is called from Rx processing routines.
Returns received buffer back to VirtIO queue, inserting it to NetReceiveBuffers.
If needed, signals end of RX pause operation

Must be called with &pContext->ReceiveLock acquired

Parameters:
	context
	void *pDescriptor - pIONetDescriptor to return
***********************************************************/
void ParaNdis_VirtIONetReuseRecvBuffer(PARANDIS_ADAPTER *pContext, void *pDescriptor)
{
	pIONetDescriptor pBuffersDescriptor = (pIONetDescriptor)pDescriptor;

	DEBUG_ENTRY(4);

	if(!pBuffersDescriptor)
		return;

	RemoveEntryList(&pBuffersDescriptor->listEntry);

	if(AddRxBufferToQueue(pContext, pBuffersDescriptor))
	{
		InsertTailList(&pContext->NetReceiveBuffers, &pBuffersDescriptor->listEntry);

		pContext->NetNofReceiveBuffers++;

		if (pContext->NetNofReceiveBuffers > pContext->NetMaxReceiveBuffers)
		{
			DPrintf(0, (" Error: NetNofReceiveBuffers > NetMaxReceiveBuffers(%d>%d)",
				pContext->NetNofReceiveBuffers, pContext->NetMaxReceiveBuffers));
		}

		if (++pContext->Counters.nReusedRxBuffers >= pContext->Limits.nReusedRxBuffers)
		{
			pContext->Counters.nReusedRxBuffers = 0;
			pContext->NetReceiveQueue->vq_ops->kick_always(pContext->NetReceiveQueue);
		}

		if (IsListEmpty(&pContext->NetReceiveBuffersWaiting))
		{
			if (pContext->ReceiveState == srsPausing || pContext->ReceivePauseCompletionProc)
			{
				ONPAUSECOMPLETEPROC callback = pContext->ReceivePauseCompletionProc;
				pContext->ReceiveState = srsDisabled;
				pContext->ReceivePauseCompletionProc = NULL;
				ParaNdis_DebugHistory(pContext, hopInternalReceivePause, NULL, 0, 0, 0);
				if (callback) callback(pContext);
			}
		}
	}
	else
	{
		DPrintf(0, ("FAILED TO REUSE THE BUFFER!!!!"));
		VirtIONetFreeBufferDescriptor(pContext, pBuffersDescriptor);
		pContext->NetMaxReceiveBuffers--;
	}
}

/**********************************************************
It is called from Tx processing routines
Gets all the fininshed buffer from VirtIO TX path and
returns them to NetFreeSendBuffers

Must be called with &pContext->SendLock acquired

Parameters:
	context
Return value:
	(for reference) number of TX buffers returned
***********************************************************/
UINT ParaNdis_VirtIONetReleaseTransmitBuffers(
	PARANDIS_ADAPTER *pContext)
{
	UINT len, i = 0;
	pIONetDescriptor pBufferDescriptor;

	DEBUG_ENTRY(4);

	while(pBufferDescriptor = pContext->NetSendQueue->vq_ops->get_buf(pContext->NetSendQueue, &len))
	{
		RemoveEntryList(&pBufferDescriptor->listEntry);
		pContext->nofFreeTxDescriptors++;
		if (!pBufferDescriptor->nofUsedBuffers)
		{
			DPrintf(0, ("[%s] ERROR: nofUsedBuffers not set!", __FUNCTION__));
		}
		pContext->nofFreeHardwareBuffers += pBufferDescriptor->nofUsedBuffers;
		ParaNdis_OnTransmitBufferReleased(pContext, pBufferDescriptor);
		InsertTailList(&pContext->NetFreeSendBuffers, &pBufferDescriptor->listEntry);
		DPrintf(3, ("[%s] Free Tx: desc %d, buff %d", __FUNCTION__, pContext->nofFreeTxDescriptors, pContext->nofFreeHardwareBuffers));
		pBufferDescriptor->nofUsedBuffers = 0;
		++i;
	}
	if (i)
	{
		NdisGetCurrentSystemTime(&pContext->LastTxCompletionTimeStamp);
		pContext->bDoKickOnNoBuffer = TRUE;
		pContext->nDetectedStoppedTx = 0;
	}
	DEBUG_EXIT_STATUS((i ? 3 : 5), i);
	return i;
}

/*********************************************************
Called with from ProcessTx routine with TxLock held
Uses pContext->sgTxGatherTable
***********************************************************/
tCopyPacketResult ParaNdis_DoSubmitPacket(PARANDIS_ADAPTER *pContext, tTxOperationParameters *Params)
{
	tCopyPacketResult result;
	tMapperResult mapResult;
	// populating priority tag or LSO MAY require additional SG element
	UINT nRequiredBuffers = Params->nofSGFragments + 1 + ((Params->flags & (pcrPriorityTag | pcrLSO)) ? 1 : 0);
	BOOLEAN bUseCopy = FALSE;
	struct VirtIOBufferDescriptor *sg = pContext->sgTxGatherTable;
	result.size = 0;
	result.error = cpeOK;
	if (!pContext->bUseScatterGather ||			// only copy available
		Params->nofSGFragments == 0 ||			// theoretical case
		!sg ||									// only copy available
		Params->ulDataSize < ETH_MIN_PACKET_SIZE ||		// padding required
		(Params->flags & pcrAnyChecksum) ||				// TCP checksumming required
		((~Params->flags & pcrLSO) && nRequiredBuffers > pContext->maxFreeHardwareBuffers) // to many fragments and normal size of packet
		)
	{
		nRequiredBuffers = 2;
		bUseCopy = TRUE;
	}

	// I do not think this will help, but at least we can try freeing some buffers right now
	if (pContext->nofFreeHardwareBuffers < nRequiredBuffers || !pContext->nofFreeTxDescriptors)
	{
		ParaNdis_VirtIONetReleaseTransmitBuffers(pContext);
	}

	if (nRequiredBuffers > pContext->maxFreeHardwareBuffers)
	{
		result.error = cpeTooLarge;
		DPrintf(0, ("[%s] ERROR: too many fragments(%d required, %d max.avail)!", __FUNCTION__,
			nRequiredBuffers, pContext->maxFreeHardwareBuffers));
	}
	else if (pContext->nofFreeHardwareBuffers < nRequiredBuffers || !pContext->nofFreeTxDescriptors)
	{
		result.error = cpeNoBuffer;
	}
	else if (Params->offloalMss && bUseCopy)
	{
		result.error = cpeInternalError;
		DPrintf(0, ("[%s] ERROR: expecting SG for TSO! (%d buffers, %d bytes)", __FUNCTION__,
			Params->nofSGFragments, Params->ulDataSize));
	}
	else if (bUseCopy)
	{
		result = ParaNdis_DoCopyPacketData(pContext, Params);
	}
	else
	{
		UINT nMappedBuffers;
		pIONetDescriptor pBuffersDescriptor = (pIONetDescriptor)RemoveHeadList(&pContext->NetFreeSendBuffers);
		pContext->nofFreeTxDescriptors--;
		NdisZeroMemory(pBuffersDescriptor->HeaderInfo.Virtual, pBuffersDescriptor->HeaderInfo.size);
		sg[0].physAddr = pBuffersDescriptor->HeaderInfo.Physical;
		sg[0].ulSize = pBuffersDescriptor->HeaderInfo.size;
		mapResult = ParaNdis_PacketMapper(
			pContext,
			Params->packet,
			Params->ReferenceValue,
			sg + 1,
			pBuffersDescriptor);
		nMappedBuffers = mapResult.nBuffersMapped;
		if (nMappedBuffers)
		{
			nMappedBuffers++;
			if (0 == pContext->NetSendQueue->vq_ops->add_buf(pContext->NetSendQueue, sg, nMappedBuffers, 0, pBuffersDescriptor))
			{
				pBuffersDescriptor->nofUsedBuffers = nMappedBuffers;
				pContext->nofFreeHardwareBuffers -= nMappedBuffers;
				if (pContext->minFreeHardwareBuffers > pContext->nofFreeHardwareBuffers)
					pContext->minFreeHardwareBuffers = pContext->nofFreeHardwareBuffers;
				pBuffersDescriptor->ReferenceValue = Params->ReferenceValue;
				result.size = Params->ulDataSize;
				DPrintf(2, ("[%s] Submitted %d buffers (%d bytes), avail %d desc, %d bufs",
					__FUNCTION__, nMappedBuffers, result.size,
					pContext->nofFreeTxDescriptors, pContext->nofFreeHardwareBuffers
				));
			}
			else
			{
				result.error = cpeInternalError;
				DPrintf(0, ("[%s] Unexpected ERROR adding buffer to TX engine!..", __FUNCTION__));
			}
		}
		else
		{
			DPrintf(0, ("[%s] Unexpected ERROR: packet not mapped!", __FUNCTION__));
			result.error = cpeInternalError;
		}

		if (result.error == cpeOK)
		{
			UCHAR ethernetHeader[sizeof(ETH_HEADER)];
			eInspectedPacketType packetType;
			/* get the ethernet header for review */
			ParaNdis_PacketCopier(Params->packet, ethernetHeader, sizeof(ethernetHeader), Params->ReferenceValue, TRUE);
			packetType = QueryPacketType(ethernetHeader);
			DebugDumpPacket("sending", ethernetHeader, 3);
			InsertTailList(&pContext->NetSendBuffersInUse, &pBuffersDescriptor->listEntry);
			pContext->Statistics.ifHCOutOctets += result.size;
			switch (packetType)
			{
				case iptBroadcast:
					pContext->Statistics.ifHCOutBroadcastOctets += result.size;
					pContext->Statistics.ifHCOutBroadcastPkts++;
					break;
				case iptMilticast:
					pContext->Statistics.ifHCOutMulticastOctets += result.size;
					pContext->Statistics.ifHCOutMulticastPkts++;
					break;
				default:
					pContext->Statistics.ifHCOutUcastOctets += result.size;
					pContext->Statistics.ifHCOutUcastPkts++;
					break;
			}
		}
		else
		{
			pContext->nofFreeTxDescriptors++;
			InsertHeadList(&pContext->NetFreeSendBuffers, &pBuffersDescriptor->listEntry);
		}
	}
	if (result.error == cpeNoBuffer && pContext->bDoKickOnNoBuffer)
	{
		pContext->NetSendQueue->vq_ops->kick_always(pContext->NetSendQueue);
		pContext->bDoKickOnNoBuffer = FALSE;
	}
	return result;
}

/**********************************************************
It is called from Tx processing routines
Prepares the VirtIO buffer and copies to it the data from provided packet

Must be called with &pContext->SendLock acquired

Parameters:
	context
	tPacketType packet			specific type is NDIS dependent
	tCopyPacketDataFunction		PacketCopier procedure for NDIS-specific type of packet
Return value:
	(for reference) number of TX buffers returned
***********************************************************/
tCopyPacketResult ParaNdis_DoCopyPacketData(
	PARANDIS_ADAPTER *pContext,
	const tTxOperationParameters *pParams)
{
	tCopyPacketResult result;
	tCopyPacketResult CopierResult;
	struct VirtIOBufferDescriptor sg[2];
	pIONetDescriptor pBuffersDescriptor = NULL;
	ULONG flags = pParams->flags;
	UINT nRequiredHardwareBuffers = 2;
	result.size  = 0;
	result.error = cpeOK;
	if (pContext->nofFreeHardwareBuffers < nRequiredHardwareBuffers ||
		IsListEmpty(&pContext->NetFreeSendBuffers))
	{
		result.error = cpeNoBuffer;
	}
	if(result.error == cpeOK)
	{
		pBuffersDescriptor = (pIONetDescriptor)RemoveHeadList(&pContext->NetFreeSendBuffers);
		NdisZeroMemory(pBuffersDescriptor->HeaderInfo.Virtual, pBuffersDescriptor->HeaderInfo.size);
		sg[0].physAddr = pBuffersDescriptor->HeaderInfo.Physical;
		sg[0].ulSize = pBuffersDescriptor->HeaderInfo.size;
		sg[1].physAddr = pBuffersDescriptor->DataInfo.Physical;
		CopierResult = ParaNdis_PacketCopier(
			pParams->packet,
			pBuffersDescriptor->DataInfo.Virtual,
			pBuffersDescriptor->DataInfo.size,
			pParams->ReferenceValue,
			FALSE);
		sg[1].ulSize = result.size = CopierResult.size;
		// did NDIS ask us to compute CS?
		if ((flags & (pcrIpChecksum | pcrTcpChecksum | pcrUdpChecksum )) != 0)
		{
			// we asked
			tOffloadSettingsFlags f = pContext->Offload.flags;
			PVOID ipPacket = RtlOffsetToPointer(pBuffersDescriptor->DataInfo.Virtual, pContext->Offload.ipHeaderOffset);
			ULONG ipPacketLength = CopierResult.size - pContext->Offload.ipHeaderOffset;
			if (pContext->bDoHardwareOffload)
			{
				// hardware offload
				virtio_net_hdr_basic *pvnh = (virtio_net_hdr_basic *)pBuffersDescriptor->HeaderInfo.Virtual;
				tTcpIpPacketParsingResult ppr = ParaNdis_CheckSumVerify(ipPacket, ipPacketLength, pcrAnyChecksum, __FUNCTION__);
				if (ppr.ipStatus == ppresIPV4 && ppr.xxpStatus == ppresXxpKnown)
				{
					/* attempt to pass Ndis test (offloadmisc) - does not help
					if (ppr.ipCheckSum == ppresCSBad)
					{
						ppr = ParaNdis_CheckSumVerify(ipPacket, ipPacketLength, pcrIpChecksum | pcrFixIPChecksum, __FUNCTION__);
					}
					*/
					pvnh->csum_start = (USHORT)pContext->Offload.ipHeaderOffset + (USHORT)ppr.ipHeaderSize;
					pvnh->csum_offset = ppr.TcpUdp == ppresIsTCP ? TCP_CHECKSUM_OFFSET : UDP_CHECKSUM_OFFSET;
					pvnh->flags |= VIRTIO_NET_HDR_F_NEEDS_CSUM;
				}
				else
				{
					DPrintf(0, ("[%s] ERROR: not valid TCP packet!", __FUNCTION__));
					result.size = 0;
					result.error = cpeInternalError;
				}
			}
			else if (CopierResult.size > pContext->Offload.ipHeaderOffset)
			{
				// software offload
				if (!f.fTxIPChecksum) flags &= ~pcrIpChecksum;
				if (!f.fTxTCPChecksum) flags &= ~pcrTcpChecksum;
				if (!f.fTxUDPChecksum) flags &= ~pcrUdpChecksum;
				ParaNdis_CheckSumVerify(ipPacket, ipPacketLength, flags | pcrFixIPChecksum | pcrFixXxpChecksum, __FUNCTION__);
			}
			else
			{
				DPrintf(0, ("[%s] ERROR: Invalid buffer size for offload!", __FUNCTION__));
				result.size = 0;
				result.error = cpeInternalError;
			}
		}
		else if (pContext->Offload.flags.fTxTCPChecksum && (flags & pcrIsIP))
		{
			if (pContext->bDoIPCheck)
			{
				// we should not do anything there, as required no CS calculation!
				// just check what we send, if it is IP packet
				tOffloadSettingsFlags f = pContext->Offload.flags;
				PVOID ipPacket = RtlOffsetToPointer(pBuffersDescriptor->DataInfo.Virtual, pContext->Offload.ipHeaderOffset);
				ULONG ipPacketLength = CopierResult.size - pContext->Offload.ipHeaderOffset;
				ParaNdis_CheckSumVerify(ipPacket, ipPacketLength,
					pcrIpChecksum /*| pcrFixChecksum*/ | pcrTcpChecksum |  pcrUdpChecksum, __FUNCTION__);
			}
		}
		pContext->nofFreeTxDescriptors--;
		if (result.size)
		{
			eInspectedPacketType packetType;
			if (result.size < ETH_MIN_PACKET_SIZE)
			{
				ULONG padding = ETH_MIN_PACKET_SIZE - result.size;
				PVOID dest  = (PUCHAR)pBuffersDescriptor->DataInfo.Virtual + result.size;
				NdisZeroMemory(dest, padding);
				result.size = ETH_MIN_PACKET_SIZE;
			}
			packetType = QueryPacketType(pBuffersDescriptor->DataInfo.Virtual);
			DebugDumpPacket("sending", pBuffersDescriptor->DataInfo.Virtual, 3);

			pBuffersDescriptor->nofUsedBuffers = nRequiredHardwareBuffers;
			pContext->nofFreeHardwareBuffers -= nRequiredHardwareBuffers;
			if (pContext->minFreeHardwareBuffers > pContext->nofFreeHardwareBuffers)
				pContext->minFreeHardwareBuffers = pContext->nofFreeHardwareBuffers;
			if (pContext->NetSendQueue->vq_ops->add_buf(pContext->NetSendQueue, sg, 2, 0, pBuffersDescriptor))
			{
				pBuffersDescriptor->nofUsedBuffers = 0;
				pContext->nofFreeHardwareBuffers += nRequiredHardwareBuffers;
				result.error = cpeInternalError;
				result.size  = 0;
				DPrintf(0, ("[%s] Unexpected ERROR adding buffer to TX engine!..", __FUNCTION__));
			}
			else
			{
				DPrintf(2, ("[%s] Submitted %d buffers (%d bytes), avail %d desc, %d bufs",
					__FUNCTION__, nRequiredHardwareBuffers, result.size,
					pContext->nofFreeTxDescriptors, pContext->nofFreeHardwareBuffers
				));
			}
			if (result.error != cpeOK)
			{
				InsertTailList(&pContext->NetFreeSendBuffers, &pBuffersDescriptor->listEntry);
				pContext->nofFreeTxDescriptors++;
			}
			else
			{
				ULONG reportedSize = pParams->ulDataSize;
				pBuffersDescriptor->ReferenceValue = pParams->ReferenceValue;
				InsertTailList(&pContext->NetSendBuffersInUse, &pBuffersDescriptor->listEntry);
				pContext->Statistics.ifHCOutOctets += reportedSize;
				switch (packetType)
				{
					case iptBroadcast:
						pContext->Statistics.ifHCOutBroadcastOctets += reportedSize;
						pContext->Statistics.ifHCOutBroadcastPkts++;
						break;
					case iptMilticast:
						pContext->Statistics.ifHCOutMulticastOctets += reportedSize;
						pContext->Statistics.ifHCOutMulticastPkts++;
						break;
					default:
						pContext->Statistics.ifHCOutUcastOctets += reportedSize;
						pContext->Statistics.ifHCOutUcastPkts++;
						break;
				}
			}
		}
		else
		{
			DPrintf(0, ("[%s] Unexpected ERROR in copying packet data! Continue...", __FUNCTION__));
			InsertTailList(&pContext->NetFreeSendBuffers, &pBuffersDescriptor->listEntry);
			pContext->nofFreeTxDescriptors++;
			// the buffer is not copied and the callback will not be called
			result.error = cpeInternalError;
		}
	}

	return result;
}

static ULONG ShallPassPacket(PARANDIS_ADAPTER *pContext, PVOID address, UINT len, eInspectedPacketType *pType)
{
	ULONG b;
	if (len <= sizeof(ETH_HEADER)) return FALSE;
	if (len > pContext->MaxPacketSize.nMaxFullSizeHwRx) return FALSE;
	if (len > pContext->MaxPacketSize.nMaxFullSizeOS && !ETH_HAS_PRIO_HEADER(address)) return FALSE;
	*pType = QueryPacketType(address);
	if (pContext->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)	return TRUE;
	if (!pContext->bDoPacketFiltering) return TRUE;
	switch(*pType)
	{
		case iptBroadcast:
			b = pContext->PacketFilter & NDIS_PACKET_TYPE_BROADCAST;
			break;
		case iptMilticast:
			b = pContext->PacketFilter & NDIS_PACKET_TYPE_ALL_MULTICAST;
			if (!b && (pContext->PacketFilter & NDIS_PACKET_TYPE_MULTICAST))
			{
				UINT i, n = (pContext->MulticastListSize / ETH_LENGTH_OF_ADDRESS) * ETH_LENGTH_OF_ADDRESS;
				b = 1;
				for (i = 0; b && i < n; i += ETH_LENGTH_OF_ADDRESS)
				{
					ETH_COMPARE_NETWORK_ADDRESSES((PUCHAR)address, &pContext->MulticastList[i], &b)
				}
				b = !b;
			}
			break;
		default:
			ETH_COMPARE_NETWORK_ADDRESSES((PUCHAR)address, pContext->CurrentMacAddress, &b);
			b = !b && (pContext->PacketFilter & NDIS_PACKET_TYPE_DIRECTED);
			break;
	}
	return b;
}

/**********************************************************
Manages RX path, calling NDIS-specific procedure for packet indication
Parameters:
	context
***********************************************************/
static UINT ParaNdis_ProcessRxPath(PARANDIS_ADAPTER *pContext)
{
	pIONetDescriptor pBuffersDescriptor;
	UINT uLoopCount = 0;
	UINT len, headerSize = pContext->nVirtioHeaderSize;
	eInspectedPacketType packetType = iptInvalid;
	UINT nReceived = 0, nRetrieved = 0, nReported = 0;
	tPacketIndicationType	*pBatchOfPackets;
	UINT					maxPacketsInBatch = pContext->NetMaxReceiveBuffers;
	pBatchOfPackets = pContext->bBatchReceive ?
		ParaNdis_AllocateMemory(pContext, maxPacketsInBatch * sizeof(tPacketIndicationType)) : NULL;
	NdisAcquireSpinLock(&pContext->ReceiveLock);
	while ((uLoopCount++ < pContext->uRXPacketsDPCLimit) && NULL != (pBuffersDescriptor = pContext->NetReceiveQueue->vq_ops->get_buf(pContext->NetReceiveQueue, &len)))
	{
		PVOID pDataBuffer = RtlOffsetToPointer(pBuffersDescriptor->DataInfo.Virtual, pContext->bUseMergedBuffers ? pContext->nVirtioHeaderSize : 0);
		RemoveEntryList(&pBuffersDescriptor->listEntry);
		InsertTailList(&pContext->NetReceiveBuffersWaiting, &pBuffersDescriptor->listEntry);
		pContext->NetNofReceiveBuffers--;
		nRetrieved++;
		DPrintf(2, ("[%s] retrieved header+%d b.", __FUNCTION__, len - headerSize));
		DebugDumpPacket("receive", pDataBuffer, 3);

		if( !pContext->bSurprizeRemoved &&
			ShallPassPacket(pContext, pDataBuffer, len - headerSize, &packetType) &&
			pContext->ReceiveState == srsEnabled &&
			pContext->bConnected)
		{
			BOOLEAN b = FALSE;
			ULONG length = len - headerSize;
			if (!pBatchOfPackets)
			{
				NdisReleaseSpinLock(&pContext->ReceiveLock);
				b = NULL != ParaNdis_IndicateReceivedPacket(
					pContext,
					pDataBuffer,
					&length,
					FALSE,
					pBuffersDescriptor);
				NdisAcquireSpinLock(&pContext->ReceiveLock);
			}
			else
			{
				tPacketIndicationType packet;
				packet = ParaNdis_IndicateReceivedPacket(
					pContext,
					pDataBuffer,
					&length,
					TRUE,
					pBuffersDescriptor);
				b = packet != NULL;
				if (b) pBatchOfPackets[nReceived] = packet;
			}
			if (!b)
			{
				ParaNdis_VirtIONetReuseRecvBuffer(pContext, pBuffersDescriptor);
				//only possible reason for that is unexpected Vlan tag
				//shall I count it as error?
				pContext->Statistics.ifInErrors++;
				pContext->Statistics.ifInDiscards++;
			}
			else
			{
				nReceived++;
				nReported++;
				pContext->Statistics.ifHCInOctets += length;
				switch(packetType)
				{
					case iptBroadcast:
						pContext->Statistics.ifHCInBroadcastPkts++;
						pContext->Statistics.ifHCInBroadcastOctets += length;
						break;
					case iptMilticast:
						pContext->Statistics.ifHCInMulticastPkts++;
						pContext->Statistics.ifHCInMulticastOctets += length;
						break;
					default:
						pContext->Statistics.ifHCInUcastPkts++;
						pContext->Statistics.ifHCInUcastOctets += length;
						break;
				}
				if (pBatchOfPackets && nReceived == maxPacketsInBatch)
				{
					DPrintf(1, ("[%s] received %d buffers", __FUNCTION__, nReceived));
					NdisReleaseSpinLock(&pContext->ReceiveLock);
					ParaNdis_IndicateReceivedBatch(pContext, pBatchOfPackets, nReceived);
					NdisAcquireSpinLock(&pContext->ReceiveLock);
					nReceived = 0;
				}
			}
		}
		else
		{
			// reuse packet, there is no data or the RX is suppressed
			ParaNdis_VirtIONetReuseRecvBuffer(pContext, pBuffersDescriptor);
		}
	}
	ParaNdis_DebugHistory(pContext, hopReceiveStat, NULL, nRetrieved, nReported, pContext->NetNofReceiveBuffers);
	NdisReleaseSpinLock(&pContext->ReceiveLock);
	if (nReceived && pBatchOfPackets)
	{
		DPrintf(1, ("[%s]%d: received %d buffers", __FUNCTION__, KeGetCurrentProcessorNumber(), nReceived));
		ParaNdis_IndicateReceivedBatch(pContext, pBatchOfPackets, nReceived);
	}
	if (pBatchOfPackets) NdisFreeMemory(pBatchOfPackets, 0, 0);
	return nRetrieved;
}

void ParaNdis_ReportLinkStatus(PARANDIS_ADAPTER *pContext)
{
	BOOLEAN bConnected = TRUE;
	if (pContext->bLinkDetectSupported)
	{
		USHORT linkStatus = 0;
		USHORT offset = (pContext->bUsingMSIX ? 4 : 0) + sizeof(pContext->CurrentMacAddress);
		// link changed
		VirtIODeviceGet(&pContext->IODevice, offset, &linkStatus, sizeof(linkStatus));
		bConnected = (linkStatus & VIRTIO_NET_S_LINK_UP) != 0;
	}
	ParaNdis_IndicateConnect(pContext, bConnected, FALSE);
}


static BOOLEAN RestartQueueSynchronously(tSynchronizedContext *SyncContext)
{
	PARANDIS_ADAPTER *pContext = SyncContext->pContext;
	BOOLEAN b = 0;
	if (SyncContext->Parameter & isReceive)
	{
		b = !pContext->NetReceiveQueue->vq_ops->restart(pContext->NetReceiveQueue);
	}
	if (SyncContext->Parameter & isTransmit)
	{
		b = !pContext->NetSendQueue->vq_ops->restart(pContext->NetSendQueue);
	}
	ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)0x20, SyncContext->Parameter, !b, 0);
	return b;
}
/**********************************************************
DPC implementation, common for both NDIS
Parameters:
	context
***********************************************************/
ULONG ParaNdis_DPCWorkBody(PARANDIS_ADAPTER *pContext)
{
	ULONG stillRequiresProcessing = 0;
	ULONG interruptSources;
	DEBUG_ENTRY(5);
	if (pContext->bEnableInterruptHandlingDPC)
	{
		InterlockedIncrement(&pContext->counterDPCInside);
		if (pContext->bEnableInterruptHandlingDPC)
		{
			InterlockedExchange(&pContext->bDPCInactive, 0);
			interruptSources = InterlockedExchange(&pContext->InterruptStatus, 0);
			ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)1, interruptSources, 0, 0);
			if ((interruptSources & isControl) && pContext->bLinkDetectSupported)
			{
				ParaNdis_ReportLinkStatus(pContext);
			}
			if (interruptSources & isTransmit)
			{
				ParaNdis_ProcessTx(pContext, TRUE);
			}
			if (interruptSources & isReceive)
			{
				int nRestartResult = 0;
				UINT nLoop = 0;
				do
				{
					UINT n;
					LONG rxActive = InterlockedIncrement(&pContext->dpcReceiveActive);
					if (rxActive == 1)
					{
						n = ParaNdis_ProcessRxPath(pContext);
						InterlockedDecrement(&pContext->dpcReceiveActive);
						NdisAcquireSpinLock(&pContext->ReceiveLock);
						nRestartResult = ParaNdis_SynchronizeWithInterrupt(
							pContext, pContext->ulRxMessage, RestartQueueSynchronously, isReceive);
						ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)3, nRestartResult, 0, 0);
						NdisReleaseSpinLock(&pContext->ReceiveLock);
						DPrintf(nRestartResult ? 2 : 6, ("[%s] queue restarted%s", __FUNCTION__, nRestartResult ? "(Rerun)" : "(Done)"));
						++nLoop;
						if (nLoop > pContext->uNumberOfHandledRXPacketsInDPC)
						{
							DPrintf(0, ("[%s] Breaking Rx loop on %d-th operation", __FUNCTION__, nLoop));
							ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)4, nRestartResult, 0, 0);
							break;
						}
					}
					else
					{
						InterlockedDecrement(&pContext->dpcReceiveActive);
						if (!nRestartResult)
						{
							NdisAcquireSpinLock(&pContext->ReceiveLock);
							nRestartResult = ParaNdis_SynchronizeWithInterrupt(
								pContext, pContext->ulRxMessage, RestartQueueSynchronously, isReceive);
							ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)5, nRestartResult, 0, 0);
							NdisReleaseSpinLock(&pContext->ReceiveLock);
						}
						DPrintf(1, ("[%s] Skip Rx processing no.%d", __FUNCTION__, rxActive));
						break;
					}
				} while (nRestartResult);

				if (nRestartResult) stillRequiresProcessing |= isReceive;
			}

			if (interruptSources & isTransmit)
			{
				NdisAcquireSpinLock(&pContext->SendLock);
				if (ParaNdis_SynchronizeWithInterrupt(pContext, pContext->ulTxMessage, RestartQueueSynchronously, isTransmit))
					stillRequiresProcessing |= isTransmit;
				NdisReleaseSpinLock(&pContext->SendLock);
			}
		}
		InterlockedDecrement(&pContext->counterDPCInside);
		ParaNdis_DebugHistory(pContext, hopDPC, NULL, stillRequiresProcessing, pContext->nofFreeHardwareBuffers, pContext->nofFreeTxDescriptors);
	}
	return stillRequiresProcessing;
}

/**********************************************************
Periodically called procedure, checking dpc activity
If DPC are not running, it does exactly the same that the DPC
Parameters:
	context
***********************************************************/
static BOOLEAN CheckRunningDpc(PARANDIS_ADAPTER *pContext)
{
	BOOLEAN bStopped;
	BOOLEAN bReportHang = FALSE;
	bStopped = 0 != InterlockedExchange(&pContext->bDPCInactive, TRUE);

	if (bStopped)
	{
		pContext->nDetectedInactivity++;
		if (pContext->nEnableDPCChecker)
		{
			if (pContext->NetTxPacketsToReturn)
			{
				DPrintf(0, ("[%s] - NO ACTIVITY!", __FUNCTION__));
				if (!pContext->Limits.nPrintDiagnostic) PrintStatistics(pContext);
				if (pContext->nEnableDPCChecker > 1)
				{
					int isrStatus1, isrStatus2;
					isrStatus1 = VirtIODeviceISR(&pContext->IODevice);
					isrStatus2 = VirtIODeviceISR(&pContext->IODevice);
					if (isrStatus1 || isrStatus2)
					{
						DPrintf(0, ("WARNING: Interrupt status %d=>%d", isrStatus1, isrStatus2));
					}
				}
				// simulateDPC
				InterlockedOr(&pContext->InterruptStatus, isAny);
				ParaNdis_DPCWorkBody(pContext);
			}
		}
	}
	else
	{
		pContext->nDetectedInactivity = 0;
	}

	NdisAcquireSpinLock(&pContext->SendLock);
	if (pContext->nofFreeHardwareBuffers != pContext->maxFreeHardwareBuffers)
	{
		if (pContext->nDetectedStoppedTx++ > 1)
		{
			DPrintf(0, ("[%s] - Suspicious Tx inactivity (%d)!", __FUNCTION__, pContext->nofFreeHardwareBuffers));
			//bReportHang = TRUE;
			WriteVirtIODeviceByte(pContext->IODevice.addr + VIRTIO_PCI_ISR, 0);
		}
	}
	NdisReleaseSpinLock(&pContext->SendLock);


	if (pContext->Limits.nPrintDiagnostic &&
		++pContext->Counters.nPrintDiagnostic >= pContext->Limits.nPrintDiagnostic)
	{
		pContext->Counters.nPrintDiagnostic = 0;
		// todo - collect more and put out optionally
		PrintStatistics(pContext);
	}

	if (pContext->Statistics.ifHCInOctets == pContext->Counters.prevIn)
	{
		pContext->Counters.nRxInactivity++;
		if (pContext->Counters.nRxInactivity >= 10)
		{
//#define CRASH_ON_NO_RX
#if defined(CRASH_ON_NO_RX)
			ONPAUSECOMPLETEPROC proc = (ONPAUSECOMPLETEPROC)(PVOID)1;
			proc(pContext);
#endif
		}
	}
	else
	{
		pContext->Counters.nRxInactivity = 0;
		pContext->Counters.prevIn = pContext->Statistics.ifHCInOctets;
	}
	return bReportHang;
}

/**********************************************************
Common implementation of periodic poll
Parameters:
	context
Return:
	TRUE, if reset required
***********************************************************/
BOOLEAN ParaNdis_CheckForHang(PARANDIS_ADAPTER *pContext)
{
	static int nHangOn = 0;
	BOOLEAN b = nHangOn >= 3 && nHangOn < 6;
	DEBUG_ENTRY(3);
	b |= CheckRunningDpc(pContext);
	//uncomment to cause 3 consecutive resets
	//nHangOn++;
	DEBUG_EXIT_STATUS(b ? 0 : 6, b);
	return b;
}


/////////////////////////////////////////////////////////////////////////////////////
//
// ReadVirtIODeviceRegister\WriteVirtIODeviceRegister
// NDIS specific implementation of the IO space read\write
//
/////////////////////////////////////////////////////////////////////////////////////
u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
	ULONG ulValue;

	NdisRawReadPortUlong(ulRegister, &ulValue);

	DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, ulValue) );
	return ulValue;
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
	DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, ulValue) );

	NdisRawWritePortUlong(ulRegister, ulValue);
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
	u8 bValue;

	NdisRawReadPortUchar(ulRegister, &bValue);

	DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, bValue) );

	return bValue;
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
	DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, bValue) );

	NdisRawWritePortUchar(ulRegister, bValue);
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
	u16 wValue;

	NdisRawReadPortUshort(ulRegister, &wValue);

	DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, wValue) );

	return wValue;
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
#if 1
	NdisRawWritePortUshort(ulRegister, wValue);
#else
	// test only to cause long TX waiting queue of NDIS packets
	// to recognize it and request for reset via Hang handler
	static int nCounterToFail = 0;
	static const int StartFail = 200, StopFail = 600;
	BOOLEAN bFail = FALSE;
	DPrintf(6, ("%s> R[%x] = %x\n", __FUNCTION__, (ULONG)ulRegister, wValue) );
	if ((ulRegister & 0x1F) == 0x10)
	{
		nCounterToFail++;
		bFail = nCounterToFail >= StartFail && nCounterToFail < StopFail;
	}
	if (!bFail) NdisRawWritePortUshort(ulRegister, wValue);
	else
	{
		DPrintf(0, ("%s> FAILING R[%x] = %x\n", __FUNCTION__, (ULONG)ulRegister, wValue) );
	}
#endif
}

/**********************************************************
Common handler of multicast address configuration
Parameters:
	PVOID Buffer			array of addresses from NDIS
	ULONG BufferSize		size of incoming buffer
	PUINT pBytesRead		update on success
	PUINT pBytesNeeded		update on wrong buffer size
Return value:
	SUCCESS or kind of failure
***********************************************************/
NDIS_STATUS ParaNdis_SetMulticastList(
	PARANDIS_ADAPTER *pContext,
	PVOID Buffer,
	ULONG BufferSize,
	PUINT pBytesRead,
	PUINT pBytesNeeded)
{
	NDIS_STATUS status;
	ULONG length = BufferSize;
	if (length > sizeof(pContext->MulticastList))
	{
		status = NDIS_STATUS_MULTICAST_FULL;
		*pBytesNeeded = sizeof(pContext->MulticastList);
	}
	else if (length % ETH_LENGTH_OF_ADDRESS)
	{
		status = NDIS_STATUS_INVALID_LENGTH;
		*pBytesNeeded = (length / ETH_LENGTH_OF_ADDRESS) * ETH_LENGTH_OF_ADDRESS;
	}
	else
	{
		NdisZeroMemory(pContext->MulticastList, sizeof(pContext->MulticastList));
		if (length)
			NdisMoveMemory(pContext->MulticastList, Buffer, length);
		pContext->MulticastListSize = length;
		DPrintf(1, ("[%s] New multicast list of %d bytes", __FUNCTION__, length));
		*pBytesRead = length;
		status = NDIS_STATUS_SUCCESS;
	}
	return status;
}

/**********************************************************
Callable from synchronized routine or interrupt handler
to enable or disable Rx and/or Tx interrupt generation
Parameters:
	context
	interruptSource - isReceive, isTransmit
	b - 1/0 enable/disable
***********************************************************/
VOID ParaNdis_VirtIOEnableIrqSynchronized(PARANDIS_ADAPTER *pContext, ULONG interruptSource, BOOLEAN b)
{
	if (interruptSource & isTransmit)
		pContext->NetSendQueue->vq_ops->enable_interrupt(pContext->NetSendQueue, b);
	if (interruptSource & isReceive)
		pContext->NetReceiveQueue->vq_ops->enable_interrupt(pContext->NetReceiveQueue, b);
	ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)0x10, interruptSource, b, 0);
}

/**********************************************************
Common handler of PnP events
Parameters:
Return value:
***********************************************************/
VOID ParaNdis_OnPnPEvent(
	PARANDIS_ADAPTER *pContext,
	NDIS_DEVICE_PNP_EVENT pEvent,
	PVOID	pInfo,
	ULONG	ulSize)
{
	const char *pName = "";
	DEBUG_ENTRY(0);
#undef MAKECASE
#define	MAKECASE(x) case (x): pName = #x; break;
	switch (pEvent)
	{
   		MAKECASE(NdisDevicePnPEventQueryRemoved)
		MAKECASE(NdisDevicePnPEventRemoved)
		MAKECASE(NdisDevicePnPEventSurpriseRemoved)
		MAKECASE(NdisDevicePnPEventQueryStopped)
		MAKECASE(NdisDevicePnPEventStopped)
		MAKECASE(NdisDevicePnPEventPowerProfileChanged)
#if NDIS_SUPPORT_NDIS6
		MAKECASE(NdisDevicePnPEventFilterListChanged)
#endif // NDIS_SUPPORT_NDIS6
		default:
			break;
	}
	ParaNdis_DebugHistory(pContext, hopPnpEvent, NULL, pEvent, 0, 0);
	DPrintf(0, ("[%s] (%s)", __FUNCTION__, pName));
	if (pEvent == NdisDevicePnPEventSurpriseRemoved)
	{
		// on simulated surprise removal (under PnpTest) we need to reset the device
		// to prevent any access of device queues to memory buffers
		pContext->bSurprizeRemoved = TRUE;
		ParaNdis_ResetVirtIONetDevice(pContext);
	}
	pContext->PnpEvents[pContext->nPnpEventIndex++] = pEvent;
	if (pContext->nPnpEventIndex > sizeof(pContext->PnpEvents)/sizeof(pContext->PnpEvents[0]))
		pContext->nPnpEventIndex = 0;
}


VOID ParaNdis_PowerOn(PARANDIS_ADAPTER *pContext)
{
	LIST_ENTRY TempList;
	int Dummy;
	DEBUG_ENTRY(0);
	ParaNdis_DebugHistory(pContext, hopPowerOn, NULL, 1, 0, 0);
	ParaNdis_ResetVirtIONetDevice(pContext);
	VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);
	/* GetHostFeature must be called with any mask once upon device initialization:
	 otherwise the device will not work properly */
	Dummy = VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_MAC);
	if (pContext->bUseMergedBuffers)
		VirtIODeviceEnableGuestFeature(&pContext->IODevice, VIRTIO_NET_F_MRG_RXBUF);
	if (pContext->bDoPublishIndices)
		VirtIODeviceEnableGuestFeature(&pContext->IODevice, VIRTIO_F_PUBLISH_INDICES);

	VirtIODeviceRenewVirtualQueue(pContext->NetReceiveQueue);
	VirtIODeviceRenewVirtualQueue(pContext->NetSendQueue);
	ParaNdis_RestoreDeviceConfigurationAfterReset(pContext);

	InitializeListHead(&TempList);
	/* submit all the receive buffers */
	NdisAcquireSpinLock(&pContext->ReceiveLock);
	while (!IsListEmpty(&pContext->NetReceiveBuffers))
	{
		pIONetDescriptor pBufferDescriptor =
			(pIONetDescriptor)RemoveHeadList(&pContext->NetReceiveBuffers);
		InsertTailList(&TempList, &pBufferDescriptor->listEntry);
	}
	pContext->NetNofReceiveBuffers = 0;
	while (!IsListEmpty(&TempList))
	{
		pIONetDescriptor pBufferDescriptor =
			(pIONetDescriptor)RemoveHeadList(&TempList);
		if (AddRxBufferToQueue(pContext, pBufferDescriptor))
		{
			InsertTailList(&pContext->NetReceiveBuffers, &pBufferDescriptor->listEntry);
			pContext->NetNofReceiveBuffers++;
		}
		else
		{
			DPrintf(0, ("FAILED TO REUSE THE BUFFER!!!!"));
			VirtIONetFreeBufferDescriptor(pContext, pBufferDescriptor);
			pContext->NetMaxReceiveBuffers--;
		}
	}
	pContext->powerState = NetDeviceStateD0;
	pContext->bEnableInterruptHandlingDPC = TRUE;
	VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
	pContext->NetReceiveQueue->vq_ops->kick(pContext->NetReceiveQueue);
	NdisReleaseSpinLock(&pContext->ReceiveLock);

	ParaNdis_Resume(pContext);
	ParaNdis_ReportLinkStatus(pContext);
	ParaNdis_DebugHistory(pContext, hopPowerOn, NULL, 0, 0, 0);
}

VOID ParaNdis_PowerOff(PARANDIS_ADAPTER *pContext)
{
	DEBUG_ENTRY(0);
	ParaNdis_DebugHistory(pContext, hopPowerOff, NULL, 1, 0, 0);
	/* stop processing of interrupts, DPC and Send operations */
	ParaNdis_IndicateConnect(pContext, FALSE, FALSE);
	VirtIODeviceRemoveStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
	// TODO: Synchronize with IRQ
	ParaNdis_Suspend(pContext);
	pContext->powerState = NetDeviceStateD3;

	PreventDPCServicing(pContext);

	/*******************************************************************
		shutdown queues to have all the receive buffers under our control
		all the transmit buffers move to list of free buffers
	********************************************************************/

	NdisAcquireSpinLock(&pContext->SendLock);
	pContext->NetSendQueue->vq_ops->shutdown(pContext->NetSendQueue);
	while (!IsListEmpty(&pContext->NetSendBuffersInUse))
	{
		pIONetDescriptor pBufferDescriptor =
			(pIONetDescriptor)RemoveHeadList(&pContext->NetSendBuffersInUse);
		InsertTailList(&pContext->NetFreeSendBuffers, &pBufferDescriptor->listEntry);
		pContext->nofFreeTxDescriptors++;
		pContext->nofFreeHardwareBuffers += pBufferDescriptor->nofUsedBuffers;
	}
	NdisReleaseSpinLock(&pContext->SendLock);

	NdisAcquireSpinLock(&pContext->ReceiveLock);
	pContext->NetReceiveQueue->vq_ops->shutdown(pContext->NetReceiveQueue);
	NdisReleaseSpinLock(&pContext->ReceiveLock);
	/*
	DPrintf(0, ("WARNING: deleting queues!!!!!!!!!"));
	VirtIODeviceDeleteVirtualQueue(pContext->NetSendQueue);
	VirtIODeviceDeleteVirtualQueue(pContext->NetReceiveQueue);
	pContext->NetSendQueue = pContext->NetReceiveQueue = NULL;
	*/
	ParaNdis_ResetVirtIONetDevice(pContext);
	ParaNdis_DebugHistory(pContext, hopPowerOff, NULL, 0, 0, 0);
}

void ParaNdis_CallOnBugCheck(PARANDIS_ADAPTER *pContext)
{
	if (pContext->AdapterResources.ulIOAddress)
	{
		WriteVirtIODeviceByte(pContext->IODevice.addr + VIRTIO_PCI_ISR, 1);
	}
}


