/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
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

static void ReuseReceiveBufferRegular(PARANDIS_ADAPTER *pContext, pIONetDescriptor pBuffersDescriptor);
static void ReuseReceiveBufferPowerOff(PARANDIS_ADAPTER *pContext, pIONetDescriptor pBuffersDescriptor);

// TODO: remove when the problem solved
void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue);

//#define ROUNDSIZE(sz) ((sz + 15) & ~15)
#define MAX_VLAN_ID     4095

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
    const char      *Name;
    ULONG           ulValue;
    ULONG           ulMinimal;
    ULONG           ulMaximal;
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
    tConfigurationEntry PacketFiltering;
    tConfigurationEntry ScatterGather;
    tConfigurationEntry BatchReceive;
    tConfigurationEntry OffloadTxChecksum;
    tConfigurationEntry OffloadTxLSO;
    tConfigurationEntry OffloadRxCS;
    tConfigurationEntry OffloadGuestCS;
    tConfigurationEntry UseSwTxChecksum;
    tConfigurationEntry IPPacketsCheck;
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
    tConfigurationEntry Indirect;
}tConfigurationEntries;

static const tConfigurationEntries defaultConfiguration =
{
    { "Promiscuous",    0,  0,  1 },
    { "Priority",       0,  0,  1 },
    { "ConnectRate",    100,10,10000 },
    { "DoLog",          1,  0,  1 },
    { "DebugLevel",     2,  0,  8 },
    { "ConnectTimer",   0,  0,  300000 },
    { "DpcCheck",       0,  0,  2 },
    { "TxCapacity",     1024,   16, 1024 },
    { "RxCapacity",     256, 32, 1024 },
    { "InterruptRecovery",  0, 0, 1},
    { "LogStatistics",  0, 0, 10000},
    { "PacketFilter",   1, 0, 1},
    { "Gather",         1, 0, 1},
    { "BatchReceive",   1, 0, 1},
    { "Offload.TxChecksum", 0, 0, 31},
    { "Offload.TxLSO",  0, 0, 2},
    { "Offload.RxCS",   0, 0, 31},
    { "Offload.GuestCS", 0, 0, 1},
    { "UseSwTxChecksum", 0, 0, 1 },
    { "IPPacketsCheck", 0, 0, 3 },
    { "*IPChecksumOffloadIPv4", 3, 0, 3 },
    { "*TCPChecksumOffloadIPv4",3, 0, 3 },
    { "*TCPChecksumOffloadIPv6",3, 0, 3 },
    { "*UDPChecksumOffloadIPv4",3, 0, 3 },
    { "*UDPChecksumOffloadIPv6",3, 0, 3 },
    { "*LsoV1IPv4", 1, 0, 1 },
    { "*LsoV2IPv4", 1, 0, 1 },
    { "*LsoV2IPv6", 1, 0, 1 },
    { "*PriorityVLANTag", 3, 0, 3},
    { "VlanId", 0, 0, MAX_VLAN_ID},
    { "MergeableBuf", 1, 0, 1},
    { "PublishIndices", 1, 0, 1},
    { "MTU", 1500, 500, 65500},
    { "NumberOfHandledRXPackersInDPC", MAX_RX_LOOPS, 1, 10000},
    { "Indirect", 0, 0, 2},
};

static void ParaNdis_ResetVirtIONetDevice(PARANDIS_ADAPTER *pContext)
{
    VirtIODeviceReset(&pContext->IODevice);
    DPrintf(0, ("[%s] Done", __FUNCTION__));
    /* reset all the features in the device */
    pContext->ulCurrentVlansFilterSet = 0;
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
static void GetConfigurationEntry(NDIS_HANDLE cfg, tConfigurationEntry *pEntry)
{
    NDIS_STATUS status;
    const char *statusName;
    NDIS_STRING name = {0};
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
    if (name.Buffer) NdisFreeString(name);
}

static void DisableLSOv4Permanently(PARANDIS_ADAPTER *pContext, LPCSTR procname, LPCSTR reason)
{
    if (pContext->Offload.flagsValue & osbT4Lso)
    {
        DPrintf(0, ("[%s] Warning: %s", procname, reason));
        pContext->Offload.flagsValue &= ~osbT4Lso;
        ParaNdis_ResetOffloadSettings(pContext, NULL, NULL);
    }
}

static void DisableLSOv6Permanently(PARANDIS_ADAPTER *pContext, LPCSTR procname, LPCSTR reason)
{
    if (pContext->Offload.flagsValue & osbT6Lso)
    {
        DPrintf(0, ("[%s] Warning: %s", procname, reason));
        pContext->Offload.flagsValue &= ~osbT6Lso;
        ParaNdis_ResetOffloadSettings(pContext, NULL, NULL);
    }
}

static void DisableBothLSOPermanently(PARANDIS_ADAPTER *pContext, LPCSTR procname, LPCSTR reason)
{
    if (pContext->Offload.flagsValue & (osbT4Lso | osbT6Lso))
    {
        DPrintf(0, ("[%s] Warning: %s", procname, reason));
        pContext->Offload.flagsValue &= ~(osbT6Lso | osbT4Lso);
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
            GetConfigurationEntry(cfg, &pConfiguration->PacketFiltering);
            GetConfigurationEntry(cfg, &pConfiguration->ScatterGather);
            GetConfigurationEntry(cfg, &pConfiguration->BatchReceive);
            GetConfigurationEntry(cfg, &pConfiguration->OffloadTxChecksum);
            GetConfigurationEntry(cfg, &pConfiguration->OffloadTxLSO);
            GetConfigurationEntry(cfg, &pConfiguration->OffloadRxCS);
            GetConfigurationEntry(cfg, &pConfiguration->OffloadGuestCS);
            GetConfigurationEntry(cfg, &pConfiguration->UseSwTxChecksum);
            GetConfigurationEntry(cfg, &pConfiguration->IPPacketsCheck);
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
            GetConfigurationEntry(cfg, &pConfiguration->Indirect);

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
            pContext->bDoSupportPriority = pConfiguration->PrioritySupport.ulValue != 0;
            pContext->ulFormalLinkSpeed  = pConfiguration->ConnectRate.ulValue;
            pContext->ulFormalLinkSpeed *= 1000000;
            pContext->bDoHwPacketFiltering = pConfiguration->PacketFiltering.ulValue != 0;
            pContext->bUseScatterGather  = pConfiguration->ScatterGather.ulValue != 0;
            pContext->bBatchReceive      = pConfiguration->BatchReceive.ulValue != 0;
            pContext->bDoHardwareChecksum = pConfiguration->UseSwTxChecksum.ulValue == 0;
            pContext->bDoGuestChecksumOnReceive = pConfiguration->OffloadGuestCS.ulValue != 0;
            pContext->bDoIPCheckTx = pConfiguration->IPPacketsCheck.ulValue & 1;
            pContext->bDoIPCheckRx = pConfiguration->IPPacketsCheck.ulValue & 2;
            pContext->Offload.flagsValue = 0;
            // TX caps: 1 - TCP, 2 - UDP, 4 - IP, 8 - TCPv6, 16 - UDPv6
            if (pConfiguration->OffloadTxChecksum.ulValue & 1) pContext->Offload.flagsValue |= osbT4TcpChecksum | osbT4TcpOptionsChecksum;
            if (pConfiguration->OffloadTxChecksum.ulValue & 2) pContext->Offload.flagsValue |= osbT4UdpChecksum;
            if (pConfiguration->OffloadTxChecksum.ulValue & 4) pContext->Offload.flagsValue |= osbT4IpChecksum | osbT4IpOptionsChecksum;
            if (pConfiguration->OffloadTxChecksum.ulValue & 8) pContext->Offload.flagsValue |= osbT6TcpChecksum | osbT6TcpOptionsChecksum;
            if (pConfiguration->OffloadTxChecksum.ulValue & 16) pContext->Offload.flagsValue |= osbT6UdpChecksum;
            if (pConfiguration->OffloadTxLSO.ulValue) pContext->Offload.flagsValue |= osbT4Lso | osbT4LsoIp | osbT4LsoTcp;
            if (pConfiguration->OffloadTxLSO.ulValue > 1) pContext->Offload.flagsValue |= osbT6Lso | osbT6LsoTcpOptions;
            // RX caps: 1 - TCP, 2 - UDP, 4 - IP, 8 - TCPv6, 16 - UDPv6
            if (pConfiguration->OffloadRxCS.ulValue & 1) pContext->Offload.flagsValue |= osbT4RxTCPChecksum | osbT4RxTCPOptionsChecksum;
            if (pConfiguration->OffloadRxCS.ulValue & 2) pContext->Offload.flagsValue |= osbT4RxUDPChecksum;
            if (pConfiguration->OffloadRxCS.ulValue & 4) pContext->Offload.flagsValue |= osbT4RxIPChecksum | osbT4RxIPOptionsChecksum;
            if (pConfiguration->OffloadRxCS.ulValue & 8) pContext->Offload.flagsValue |= osbT6RxTCPChecksum | osbT6RxTCPOptionsChecksum;
            if (pConfiguration->OffloadRxCS.ulValue & 16) pContext->Offload.flagsValue |= osbT6RxUDPChecksum;
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
            pContext->VlanId = pConfiguration->VlanId.ulValue & 0xfff;
            pContext->bUseMergedBuffers = pConfiguration->UseMergeableBuffers.ulValue != 0;
            pContext->bDoPublishIndices = pConfiguration->PublishIndices.ulValue != 0;
            pContext->MaxPacketSize.nMaxDataSize = pConfiguration->MTU.ulValue;
            pContext->bUseIndirect = pConfiguration->Indirect.ulValue != 0;
            if (!pContext->bDoSupportPriority)
                pContext->ulPriorityVlanSetting = 0;
            // if Vlan not supported
            if (!IsVlanSupported(pContext))
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

    pDest->fRxIPChecksum = !!(*from & osbT4RxIPChecksum);
    pDest->fRxIPOptions = !!(*from & osbT4RxIPOptionsChecksum);
    pDest->fRxTCPChecksum = !!(*from & osbT4RxTCPChecksum);
    pDest->fRxTCPOptions = !!(*from & osbT4RxTCPOptionsChecksum);
    pDest->fRxUDPChecksum = !!(*from & osbT4RxUDPChecksum);

    pDest->fTxTCPv6Checksum = !!(*from & osbT6TcpChecksum);
    pDest->fTxTCPv6Options = !!(*from & osbT6TcpOptionsChecksum);
    pDest->fTxUDPv6Checksum = !!(*from & osbT6UdpChecksum);
    pDest->fTxIPv6Ext = !!(*from & osbT6IpExtChecksum);

    pDest->fTxLsov6 = !!(*from & osbT6Lso);
    pDest->fTxLsov6IP = !!(*from & osbT6LsoIpExt);
    pDest->fTxLsov6TCP = !!(*from & osbT6LsoTcpOptions);

    pDest->fRxTCPv6Checksum = !!(*from & osbT6RxTCPChecksum);
    pDest->fRxTCPv6Options = !!(*from & osbT6RxTCPOptionsChecksum);
    pDest->fRxUDPv6Checksum = !!(*from & osbT6RxUDPChecksum);
    pDest->fRxIPv6Ext = !!(*from & osbT6RxIpExtChecksum);
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
    return pResources->ulIOAddress && pResources->Vector;
}

static void DumpVirtIOFeatures(VirtIODevice *pIO)
{
    const struct {  ULONG bitmask;  const PCHAR Name; } Features[] =
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
        {VIRTIO_NET_F_CTRL_VQ, "VIRTIO_NET_F_CTRL_VQ"},
        {VIRTIO_NET_F_CTRL_RX, "VIRTIO_NET_F_CTRL_RX"},
        {VIRTIO_NET_F_CTRL_VLAN, "VIRTIO_NET_F_CTRL_VLAN"},
        {VIRTIO_NET_F_CTRL_RX_EXTRA, "VIRTIO_NET_F_CTRL_RX_EXTRA"},
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
    ULONG64 totalTxFrames =
        pContext->Statistics.ifHCOutBroadcastPkts +
        pContext->Statistics.ifHCOutMulticastPkts +
        pContext->Statistics.ifHCOutUcastPkts;
    ULONG64 totalRxFrames =
        pContext->Statistics.ifHCInBroadcastPkts +
        pContext->Statistics.ifHCInMulticastPkts +
        pContext->Statistics.ifHCInUcastPkts;

    DPrintf(0, ("[Diag!%X] RX buffers at VIRTIO %d of %d",
        pContext->CurrentMacAddress[5],
        pContext->NetNofReceiveBuffers,
        pContext->NetMaxReceiveBuffers));
    DPrintf(0, ("[Diag!] TX desc available %d/%d, buf %d/min. %d",
        pContext->nofFreeTxDescriptors,
        pContext->maxFreeTxDescriptors,
        pContext->nofFreeHardwareBuffers,
        pContext->minFreeHardwareBuffers));
    pContext->minFreeHardwareBuffers = pContext->nofFreeHardwareBuffers;
    if (pContext->NetTxPacketsToReturn)
    {
        DPrintf(0, ("[Diag!] TX packets to return %d", pContext->NetTxPacketsToReturn));
    }
    DPrintf(0, ("[Diag!] Bytes transmitted %I64u, received %I64u",
        pContext->Statistics.ifHCOutOctets,
        pContext->Statistics.ifHCInOctets));
    DPrintf(0, ("[Diag!] Tx frames %I64u, CSO %d, LSO %d, indirect %d",
        totalTxFrames,
        pContext->extraStatistics.framesCSOffload,
        pContext->extraStatistics.framesLSO,
        pContext->extraStatistics.framesIndirect));
    DPrintf(0, ("[Diag!] Rx frames %I64u, Rx.Pri %d, RxHwCS.OK %d, FiltOut %d",
        totalRxFrames, pContext->extraStatistics.framesRxPriority,
        pContext->extraStatistics.framesRxCSHwOK, pContext->extraStatistics.framesFilteredOut));
    if (pContext->extraStatistics.framesRxCSHwMissedBad || pContext->extraStatistics.framesRxCSHwMissedGood)
    {
        DPrintf(0, ("[Diag!] RxHwCS mistakes: missed bad %d, missed good %d",
            pContext->extraStatistics.framesRxCSHwMissedBad, pContext->extraStatistics.framesRxCSHwMissedGood));
    }
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
    USHORT linkStatus = 0;

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

        VirtIODeviceInitialize(&pContext->IODevice, pContext->AdapterResources.ulIOAddress, sizeof(pContext->IODevice));
        VirtIODeviceSetMSIXUsed(&pContext->IODevice, pContext->bUsingMSIX);
        JustForCheckClearInterrupt(pContext, "init 0");
        ParaNdis_ResetVirtIONetDevice(pContext);
        JustForCheckClearInterrupt(pContext, "init 1");
        VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
        JustForCheckClearInterrupt(pContext, "init 2");
        VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER);
        DumpVirtIOFeatures(&pContext->IODevice);
        JustForCheckClearInterrupt(pContext, "init 3");
        pContext->bLinkDetectSupported = 0 != VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_STATUS);

        if(pContext->bLinkDetectSupported) {
            VirtIODeviceGet(&pContext->IODevice, sizeof(pContext->CurrentMacAddress), &linkStatus, sizeof(linkStatus));
            pContext->bConnected = (linkStatus & VIRTIO_NET_S_LINK_UP) != 0;
            DPrintf(0, ("[%s] Link status on driver startup: %d", __FUNCTION__, pContext->bConnected));
        }

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
                0, // offsetof(struct virtio_net_config, mac)
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


    if (pContext->bDoHardwareChecksum)
    {
        ULONG dependentOptions;
        dependentOptions = osbT4TcpChecksum | osbT4UdpChecksum | osbT4TcpOptionsChecksum;
        if (!VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_CSUM) &&
            (pContext->Offload.flagsValue & dependentOptions))
        {
            DPrintf(0, ("[%s] Host does not support CSUM, disabling CS offload", __FUNCTION__) );
            pContext->Offload.flagsValue &= ~dependentOptions;
        }
    }

    if (pContext->bDoGuestChecksumOnReceive && VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_GUEST_CSUM))
    {
        DPrintf(0, ("[%s] Enabling guest checksum", __FUNCTION__) );
        VirtIODeviceEnableGuestFeature(&pContext->IODevice, VIRTIO_NET_F_GUEST_CSUM);
    }
    else
    {
        pContext->bDoGuestChecksumOnReceive = FALSE;
    }

    // now, after we checked the capabilities, we can initialize current
    // configuration of offload tasks
    ParaNdis_ResetOffloadSettings(pContext, NULL, NULL);
    if (pContext->Offload.flags.fTxLso && !pContext->bUseScatterGather)
    {
        DisableBothLSOPermanently(pContext, __FUNCTION__, "SG is not active");
    }
    if (pContext->Offload.flags.fTxLso &&
        !VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_HOST_TSO4))
    {
        DisableLSOv4Permanently(pContext, __FUNCTION__, "Host does not support TSOv4");
    }
    if (pContext->Offload.flags.fTxLsov6 &&
        !VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_HOST_TSO6))
    {
        DisableLSOv6Permanently(pContext, __FUNCTION__, "Host does not support TSOv6");
    }
    if (pContext->bUseIndirect)
    {
        const char *reason = "";
        if (!VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_F_INDIRECT))
        {
            pContext->bUseIndirect = FALSE;
            reason = "Host support";
        }
        else if (!pContext->bUseScatterGather)
        {
            pContext->bUseIndirect = FALSE;
            reason = "SG";
        }
        DPrintf(0, ("[%s] %sable indirect Tx(!%s)", __FUNCTION__, pContext->bUseIndirect ? "En" : "Dis", reason) );
    }

    if (VirtIODeviceGetHostFeature(&pContext->IODevice, VIRTIO_NET_F_CTRL_RX_EXTRA) &&
        pContext->bDoHwPacketFiltering)
    {
        DPrintf(0, ("[%s] Using hardware packet filtering", __FUNCTION__));
        pContext->bHasHardwareFilters = TRUE;
    }

    pContext->ReuseBufferProc = ReuseReceiveBufferRegular;

    
    NdisInitializeEvent(&pContext->ResetEvent);
    DEBUG_EXIT_STATUS(0, status);
    return status;
}

/**********************************************************
Free the resources allocated for VirtIO buffer descriptor
Parameters:
    PVOID pParam                pIONetDescriptor to free
    BOOLEAN bRemoveFromList     TRUE, if also remove it from list
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
    PLIST_ENTRY pListRoot           list containing pIONetDescriptor structures
    PNDIS_SPIN_LOCK pLock           lock to protest this list
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
    return 0 <= pContext->NetReceiveQueue->vq_ops->add_buf(
        pContext->NetReceiveQueue,
        sg,
        0,
        nBuffersToSubmit,
        pBufferDescriptor,
        NULL,
        0);
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
            AllocatePairOfBuffersOnInit(pContext, size1, size2, FALSE);
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

    pContext->NetReceiveQueue->vq_ops->kick(pContext->NetReceiveQueue);

    return nRet;
}

static void DeleteQueue(PARANDIS_ADAPTER *pContext, struct virtqueue **ppq, tCompletePhysicalAddress *ppa)
{
    if (*ppq) VirtIODeviceDeleteQueue(*ppq, NULL);
    *ppq = NULL;
    if (ppa->Virtual) ParaNdis_FreePhysicalMemory(pContext, ppa);
    RtlZeroMemory(ppa, sizeof(*ppa));
}

// called on PASSIVE upon unsuccessful Init or upon Halt
static void DeleteNetQueues(PARANDIS_ADAPTER *pContext)
{
    if (pContext->NetControlQueue) {
        DeleteQueue(pContext, &pContext->NetControlQueue, &pContext->ControlQueueRing);
    }

    if (pContext->NetSendQueue) {
        DeleteQueue(pContext, &pContext->NetSendQueue, &pContext->SendQueueRing);
    }

    if (pContext->NetReceiveQueue) {
        DeleteQueue(pContext, &pContext->NetReceiveQueue, &pContext->ReceiveQueueRing);
    }
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
    ULONG size;
    DEBUG_ENTRY(0);

    pContext->ReceiveQueueRing.IsCached = 1;
    pContext->SendQueueRing.IsCached = 1;
    pContext->ControlQueueRing.IsCached = 1;
    pContext->ControlData.IsCached = 1;
    pContext->ControlData.size = 512;

    // We work with two virtqueues, 0 - receive and 1 - send.
    VirtIODeviceQueryQueueAllocation(&pContext->IODevice, 0, &size, &pContext->ReceiveQueueRing.size);
    VirtIODeviceQueryQueueAllocation(&pContext->IODevice, 1, &size, &pContext->SendQueueRing.size);
    VirtIODeviceQueryQueueAllocation(&pContext->IODevice, 1, &size, &pContext->ControlQueueRing.size);
    if (pContext->ReceiveQueueRing.size && ParaNdis_InitialAllocatePhysicalMemory(pContext, &pContext->ReceiveQueueRing))
    {
        pContext->NetReceiveQueue = VirtIODevicePrepareQueue(
            &pContext->IODevice,
            0,
            pContext->ReceiveQueueRing.Physical,
            pContext->ReceiveQueueRing.Virtual,
            pContext->ReceiveQueueRing.size,
            NULL,
            pContext->bDoPublishIndices);
    }
    if (pContext->SendQueueRing.size && ParaNdis_InitialAllocatePhysicalMemory(pContext, &pContext->SendQueueRing))
    {
        pContext->NetSendQueue = VirtIODevicePrepareQueue(
            &pContext->IODevice,
            1,
            pContext->SendQueueRing.Physical,
            pContext->SendQueueRing.Virtual,
            pContext->SendQueueRing.size,
            NULL,
            pContext->bDoPublishIndices);
    }
    if (pContext->ControlQueueRing.size && ParaNdis_InitialAllocatePhysicalMemory(pContext, &pContext->ControlQueueRing))
    {
        pContext->NetControlQueue = VirtIODevicePrepareQueue(
            &pContext->IODevice,
            2,
            pContext->ControlQueueRing.Physical,
            pContext->ControlQueueRing.Virtual,
            pContext->ControlQueueRing.size,
            NULL,
            pContext->bDoPublishIndices);
    }

    if (pContext->NetReceiveQueue && pContext->NetSendQueue)
    {
        PrepareTransmitBuffers(pContext);
        PrepareReceiveBuffers(pContext);

        if (pContext->NetControlQueue)
            ParaNdis_InitialAllocatePhysicalMemory(pContext, &pContext->ControlData);
        if (!pContext->NetControlQueue || !pContext->ControlData.Virtual)
        {
            DPrintf(0, ("[%s] The Control vQueue does not work!\n", __FUNCTION__) );
            pContext->bHasHardwareFilters = FALSE;
        }
        if (pContext->nofFreeTxDescriptors &&
            pContext->NetMaxReceiveBuffers &&
            pContext->maxFreeHardwareBuffers)
        {
            pContext->sgTxGatherTable = ParaNdis_AllocateMemory(pContext,
                pContext->maxFreeHardwareBuffers * sizeof(pContext->sgTxGatherTable[0]));
            if (!pContext->sgTxGatherTable)
            {
                DisableBothLSOPermanently(pContext, __FUNCTION__, "Can not allocate SG table");
            }
            status = NDIS_STATUS_SUCCESS;
        }
    }
    else
    {
        DeleteNetQueues(pContext);
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

    if (status == NDIS_STATUS_SUCCESS)
    {
        JustForCheckClearInterrupt(pContext, "start 3");
        pContext->bEnableInterruptHandlingDPC = TRUE;
        ParaNdis_SetPowerState(pContext, NdisDeviceStateD0);
        VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
        JustForCheckClearInterrupt(pContext, "start 4");
        ParaNdis_UpdateDeviceFilters(pContext);
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
    if(pContext->NetControlQueue)
        pContext->NetControlQueue->vq_ops->shutdown(pContext->NetControlQueue);

    DeleteNetQueues(pContext);

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

    if (pContext->ControlData.Virtual)
        ParaNdis_FreePhysicalMemory(pContext, &pContext->ControlData);

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

    ParaNdis_SetPowerState(pContext, NdisDeviceStateD3);
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
BOOLEAN ParaNdis_OnLegacyInterrupt(
    PARANDIS_ADAPTER *pContext,
    OUT BOOLEAN *pRunDpc)
{
    ULONG status = VirtIODeviceISR(&pContext->IODevice);

    if((status == 0)                                   ||
       (status == VIRTIO_NET_INVALID_INTERRUPT_STATUS) ||
       (pContext->powerState != NdisDeviceStateD0))
    {
        *pRunDpc = FALSE;
        return FALSE;
    }

    PARADNIS_STORE_LAST_INTERRUPT_TIMESTAMP(pContext);
    ParaNdis_VirtIODisableIrqSynchronized(pContext, isAny);
    InterlockedOr(&pContext->InterruptStatus, (LONG) ((status & isControl) | isReceive | isTransmit));
    *pRunDpc = TRUE;
    return TRUE;
}

BOOLEAN ParaNdis_OnQueuedInterrupt(
    PARANDIS_ADAPTER *pContext,
    OUT BOOLEAN *pRunDpc,
    ULONG knownInterruptSources)
{
    struct virtqueue* _vq = ParaNdis_GetQueueForInterrupt(pContext, knownInterruptSources);

    /* If interrupts for this queue disabled do nothing */
    if((_vq != NULL) && !ParaNDIS_IsQueueInterruptEnabled(_vq))
    {
        *pRunDpc = FALSE;
    }
    else
    {
        PARADNIS_STORE_LAST_INTERRUPT_TIMESTAMP(pContext);
        InterlockedOr(&pContext->InterruptStatus, (LONG)knownInterruptSources);
        ParaNdis_VirtIODisableIrqSynchronized(pContext, knownInterruptSources);
        *pRunDpc = TRUE;
    }

    return *pRunDpc;
}


/**********************************************************
It is called from Rx processing routines in regular mode of operation.
Returns received buffer back to VirtIO queue, inserting it to NetReceiveBuffers.
If needed, signals end of RX pause operation

Must be called with &pContext->ReceiveLock acquired

Parameters:
    context
    void *pDescriptor - pIONetDescriptor to return
***********************************************************/
void ReuseReceiveBufferRegular(PARANDIS_ADAPTER *pContext, pIONetDescriptor pBuffersDescriptor)
{
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
It is called from Rx processing routines between power off and power on in non-paused mode (Win8).
Returns received buffer to NetReceiveBuffers. 
All the buffers will be placed into Virtio queue during power-on procedure

Must be called with &pContext->ReceiveLock acquired

Parameters:
    context
    void *pDescriptor - pIONetDescriptor to return
***********************************************************/
static void ReuseReceiveBufferPowerOff(PARANDIS_ADAPTER *pContext, pIONetDescriptor pBuffersDescriptor)
{
    RemoveEntryList(&pBuffersDescriptor->listEntry);
    InsertTailList(&pContext->NetReceiveBuffers, &pBuffersDescriptor->listEntry);
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

    while(NULL != (pBufferDescriptor = pContext->NetSendQueue->vq_ops->get_buf(pContext->NetSendQueue, &len)))
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

static ULONG FORCEINLINE QueryTcpHeaderOffset(PVOID packetData, ULONG ipHeaderOffset, ULONG ipPacketLength)
{
    ULONG res;
    tTcpIpPacketParsingResult ppr = ParaNdis_ReviewIPPacket(
        (PUCHAR)packetData + ipHeaderOffset,
        ipPacketLength,
        __FUNCTION__);
    if (ppr.xxpStatus == ppresXxpKnown)
    {
        res = ipHeaderOffset + ppr.ipHeaderSize;
    }
    else
    {
        DPrintf(0, ("[%s] ERROR: NOT a TCP or UDP packet - expected troubles!", __FUNCTION__));
        res = 0;
    }
    return res;
}


/*********************************************************
Called with from ProcessTx routine with TxLock held
Uses pContext->sgTxGatherTable
***********************************************************/
tCopyPacketResult ParaNdis_DoSubmitPacket(PARANDIS_ADAPTER *pContext, tTxOperationParameters *Params)
{
    tCopyPacketResult result;
    tMapperResult mapResult = {0,0,0};
    // populating priority tag or LSO MAY require additional SG element
    UINT nRequiredBuffers;
    BOOLEAN bUseCopy = FALSE;
    struct VirtIOBufferDescriptor *sg = pContext->sgTxGatherTable;

    nRequiredBuffers = Params->nofSGFragments + 1 + ((Params->flags & (pcrPriorityTag | pcrLSO)) ? 1 : 0);

    result.size = 0;
    result.error = cpeOK;
    if (!pContext->bUseScatterGather ||         // only copy available
        Params->nofSGFragments == 0 ||          // theoretical case
        !sg ||                                  // only copy available
        ((~Params->flags & pcrLSO) && nRequiredBuffers > pContext->maxFreeHardwareBuffers) // to many fragments and normal size of packet
        )
    {
        nRequiredBuffers = 2;
        bUseCopy = TRUE;
    }
    else if (pContext->bUseIndirect && !(Params->flags & pcrNoIndirect))
    {
        nRequiredBuffers = 1;
    }

    // I do not think this will help, but at least we can try freeing some buffers right now
    if (pContext->nofFreeHardwareBuffers < nRequiredBuffers || !pContext->nofFreeTxDescriptors)
    {
        ParaNdis_VirtIONetReleaseTransmitBuffers(pContext);
    }

    if (nRequiredBuffers > pContext->maxFreeHardwareBuffers)
    {
        // LSO and too many buffers, impossible to send
        result.error = cpeTooLarge;
        DPrintf(0, ("[%s] ERROR: too many fragments(%d required, %d max.avail)!", __FUNCTION__,
            nRequiredBuffers, pContext->maxFreeHardwareBuffers));
    }
    else if (pContext->nofFreeHardwareBuffers < nRequiredBuffers || !pContext->nofFreeTxDescriptors)
    {
        pContext->NetSendQueue->vq_ops->delay_interrupt(pContext->NetSendQueue);
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
        ULONGLONG paOfIndirectArea = 0;
        PVOID vaOfIndirectArea = NULL;
        pIONetDescriptor pBuffersDescriptor = (pIONetDescriptor)RemoveHeadList(&pContext->NetFreeSendBuffers);
        pContext->nofFreeTxDescriptors--;
        NdisZeroMemory(pBuffersDescriptor->HeaderInfo.Virtual, pBuffersDescriptor->HeaderInfo.size);
        sg[0].physAddr = pBuffersDescriptor->HeaderInfo.Physical;
        sg[0].ulSize = pBuffersDescriptor->HeaderInfo.size;
        ParaNdis_PacketMapper(
            pContext,
            Params->packet,
            Params->ReferenceValue,
            sg + 1,
            pBuffersDescriptor,
            &mapResult);
        nMappedBuffers = mapResult.usBuffersMapped;
        if (nMappedBuffers)
        {
            nMappedBuffers++;
            if (pContext->bUseIndirect && !(Params->flags & pcrNoIndirect))
            {
                ULONG space1 = (mapResult.usBufferSpaceUsed + 7) & ~7;
                ULONG space2 = nMappedBuffers * SIZE_OF_SINGLE_INDIRECT_DESC;
                if (pBuffersDescriptor->DataInfo.size >= (space1 + space2))
                {
                    vaOfIndirectArea = RtlOffsetToPointer(pBuffersDescriptor->DataInfo.Virtual, space1);
                    paOfIndirectArea = pBuffersDescriptor->DataInfo.Physical.QuadPart + space1;
                    pContext->extraStatistics.framesIndirect++;
                }
                else if (nMappedBuffers <= pContext->nofFreeHardwareBuffers)
                {
                    // send as is, no indirect
                }
                else
                {
                    result.error = cpeNoIndirect;
                    DPrintf(0, ("[%s] Unexpected ERROR of placement!", __FUNCTION__));
                }
            }
            if (result.error == cpeOK)
            {
                if (Params->flags & (pcrTcpChecksum | pcrUdpChecksum))
                {
                    unsigned short addPriorityLen = (Params->flags & pcrPriorityTag) ? ETH_PRIORITY_HEADER_SIZE : 0;
                    if (pContext->bDoHardwareChecksum)
                    {
                        virtio_net_hdr_basic *pheader = pBuffersDescriptor->HeaderInfo.Virtual;
                        pheader->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
                        if (!Params->tcpHeaderOffset)
                        {
                            Params->tcpHeaderOffset = QueryTcpHeaderOffset(
                                pBuffersDescriptor->DataInfo.Virtual,
                                pContext->Offload.ipHeaderOffset + addPriorityLen,
                                mapResult.usBufferSpaceUsed - pContext->Offload.ipHeaderOffset - addPriorityLen);
                        }
                        else
                        {
                            Params->tcpHeaderOffset += addPriorityLen;
                        }
                        pheader->csum_start = (USHORT)Params->tcpHeaderOffset;
                        pheader->csum_offset = (Params->flags & pcrTcpChecksum) ? TCP_CHECKSUM_OFFSET : UDP_CHECKSUM_OFFSET;
                    }
                    else
                    {
                        // emulation mode - it is slow and intended only for test of flows
                        // and debugging of WLK test cases
                        PVOID pCopy = ParaNdis_AllocateMemory(pContext, Params->ulDataSize);
                        if (pCopy)
                        {
                            tTcpIpPacketParsingResult ppr;
                            // duplicate entire packet
                            ParaNdis_PacketCopier(Params->packet, pCopy, Params->ulDataSize, Params->ReferenceValue, FALSE);
                            // calculate complete TCP/UDP checksum
                            ppr = ParaNdis_CheckSumVerify(
                                RtlOffsetToPointer(pCopy, pContext->Offload.ipHeaderOffset + addPriorityLen),
                                Params->ulDataSize - pContext->Offload.ipHeaderOffset - addPriorityLen,
                                pcrAnyChecksum | pcrFixXxpChecksum,
                                __FUNCTION__);
                            // data portion in aside buffer contains complete IP+TCP header
                            // rewrite copy of original buffer by one new with calculated data
                            NdisMoveMemory(
                                pBuffersDescriptor->DataInfo.Virtual,
                                pCopy,
                                mapResult.usBufferSpaceUsed);
                            NdisFreeMemory(pCopy, 0, 0);
                        }
                    }
                }

                if (0 <= pContext->NetSendQueue->vq_ops->add_buf(
                    pContext->NetSendQueue,
                    sg,
                    nMappedBuffers,
                    0,
                    pBuffersDescriptor,
                    vaOfIndirectArea,
                    paOfIndirectArea))
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

            if (Params->flags & pcrLSO)
                pContext->extraStatistics.framesLSO++;
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
    if (result.error == cpeOK)
    {
        if (Params->flags & (pcrTcpChecksum | pcrUdpChecksum))
            pContext->extraStatistics.framesCSOffload++;
    }
    return result;
}


/**********************************************************
It is called from Tx processing routines
Prepares the VirtIO buffer and copies to it the data from provided packet

Must be called with &pContext->SendLock acquired

Parameters:
    context
    tPacketType packet          specific type is NDIS dependent
    tCopyPacketDataFunction     PacketCopier procedure for NDIS-specific type of packet
Return value:
    (for reference) number of TX buffers returned
***********************************************************/
tCopyPacketResult ParaNdis_DoCopyPacketData(
    PARANDIS_ADAPTER *pContext,
    tTxOperationParameters *pParams)
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
        if ((flags & (pcrTcpChecksum | pcrUdpChecksum | pcrIpChecksum)) != 0)
        {
            // we asked
            unsigned short addPriorityLen = (pParams->flags & pcrPriorityTag) ? ETH_PRIORITY_HEADER_SIZE : 0;
            tOffloadSettingsFlags f = pContext->Offload.flags;
            PVOID ipPacket = RtlOffsetToPointer(
                pBuffersDescriptor->DataInfo.Virtual, pContext->Offload.ipHeaderOffset + addPriorityLen);
            ULONG ipPacketLength = CopierResult.size - pContext->Offload.ipHeaderOffset - addPriorityLen;
            if (!pParams->tcpHeaderOffset &&
                (flags & (pcrTcpChecksum | pcrUdpChecksum)) )
            {
                pParams->tcpHeaderOffset = QueryTcpHeaderOffset(
                    pBuffersDescriptor->DataInfo.Virtual,
                    pContext->Offload.ipHeaderOffset + addPriorityLen,
                    ipPacketLength);
            }
            else
            {
                pParams->tcpHeaderOffset += addPriorityLen;
            }

            if (pContext->bDoHardwareChecksum)
            {
                if (flags & (pcrTcpChecksum | pcrUdpChecksum))
                {
                    // hardware offload
                    virtio_net_hdr_basic *pvnh = (virtio_net_hdr_basic *)pBuffersDescriptor->HeaderInfo.Virtual;
                    pvnh->csum_start = (USHORT)pParams->tcpHeaderOffset;
                    pvnh->csum_offset = (flags & pcrTcpChecksum) ? TCP_CHECKSUM_OFFSET : UDP_CHECKSUM_OFFSET;
                    pvnh->flags |= VIRTIO_NET_HDR_F_NEEDS_CSUM;
                }
                if (flags & (pcrIpChecksum))
                {
                    ParaNdis_CheckSumVerify(
                        ipPacket,
                        ipPacketLength,
                        pcrIpChecksum | pcrFixIPChecksum,
                        __FUNCTION__);
                }
            }
            else if (CopierResult.size > pContext->Offload.ipHeaderOffset)
            {
                ULONG csFlags = 0;
                if (flags & pcrIpChecksum) csFlags |= pcrIpChecksum | pcrFixIPChecksum;
                if (flags & (pcrTcpChecksum | pcrUdpChecksum)) csFlags |= pcrTcpChecksum | pcrUdpChecksum| pcrFixXxpChecksum;
                // software offload
                ParaNdis_CheckSumVerify(
                    ipPacket,
                    ipPacketLength,
                    csFlags,
                    __FUNCTION__);
            }
            else
            {
                DPrintf(0, ("[%s] ERROR: Invalid buffer size for offload!", __FUNCTION__));
                result.size = 0;
                result.error = cpeInternalError;
            }
        }
        pContext->nofFreeTxDescriptors--;
        if (result.size)
        {
            eInspectedPacketType packetType;
            packetType = QueryPacketType(pBuffersDescriptor->DataInfo.Virtual);
            DebugDumpPacket("sending", pBuffersDescriptor->DataInfo.Virtual, 3);

            pBuffersDescriptor->nofUsedBuffers = nRequiredHardwareBuffers;
            pContext->nofFreeHardwareBuffers -= nRequiredHardwareBuffers;
            if (pContext->minFreeHardwareBuffers > pContext->nofFreeHardwareBuffers)
                pContext->minFreeHardwareBuffers = pContext->nofFreeHardwareBuffers;
            if (0 > pContext->NetSendQueue->vq_ops->add_buf(
                pContext->NetSendQueue,
                sg,
                2,
                0,
                pBuffersDescriptor,
                NULL,
                0
                ))
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
    if (pContext->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)  return TRUE;

    switch(*pType)
    {
        case iptBroadcast:
            b = pContext->PacketFilter & NDIS_PACKET_TYPE_BROADCAST;
            break;
        case iptMilticast:
            b = pContext->PacketFilter & NDIS_PACKET_TYPE_ALL_MULTICAST;
            if (!b && (pContext->PacketFilter & NDIS_PACKET_TYPE_MULTICAST))
            {
                UINT i, n = pContext->MulticastData.nofMulticastEntries * ETH_LENGTH_OF_ADDRESS;
                b = 1;
                for (i = 0; b && i < n; i += ETH_LENGTH_OF_ADDRESS)
                {
                    ETH_COMPARE_NETWORK_ADDRESSES((PUCHAR)address, &pContext->MulticastData.MulticastList[i], &b)
                }
                b = !b;
            }
            break;
        default:
            ETH_COMPARE_NETWORK_ADDRESSES((PUCHAR)address, pContext->CurrentMacAddress, &b);
            b = !b && (pContext->PacketFilter & NDIS_PACKET_TYPE_DIRECTED);
            break;
    }
    if (!b)
    {
        pContext->extraStatistics.framesFilteredOut++;
    }
    return b;
}

void
ParaNdis_PadPacketReceived(PVOID pDataBuffer, PULONG pLength)
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

    if(*pLength < ETH_MIN_PACKET_SIZE)
    {
        RtlZeroMemory(RtlOffsetToPointer(pDataBuffer, *pLength), ETH_MIN_PACKET_SIZE - *pLength);
        *pLength = ETH_MIN_PACKET_SIZE;
    }
}

/**********************************************************
Manages RX path, calling NDIS-specific procedure for packet indication
Parameters:
    context
***********************************************************/
static UINT ParaNdis_ProcessRxPath(PARANDIS_ADAPTER *pContext, ULONG ulMaxPacketsToIndicate)
{
    pIONetDescriptor pBuffersDescriptor;
    UINT len, headerSize = pContext->nVirtioHeaderSize;
    eInspectedPacketType packetType = iptInvalid;
    UINT nReceived = 0, nRetrieved = 0, nReported = 0;
    tPacketIndicationType   *pBatchOfPackets;
    UINT                    maxPacketsInBatch = pContext->NetMaxReceiveBuffers;
    pBatchOfPackets = pContext->bBatchReceive ?
        ParaNdis_AllocateMemory(pContext, maxPacketsInBatch * sizeof(tPacketIndicationType)) : NULL;
    NdisAcquireSpinLock(&pContext->ReceiveLock);
    while ((nReported < ulMaxPacketsToIndicate) && NULL != (pBuffersDescriptor = pContext->NetReceiveQueue->vq_ops->get_buf(pContext->NetReceiveQueue, &len)))
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
                pContext->ReuseBufferProc(pContext, pBuffersDescriptor);
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
                    DPrintf(1, ("[%s] received %d buffers of max %d", __FUNCTION__, nReceived, ulMaxPacketsToIndicate));
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
            pContext->ReuseBufferProc(pContext, pBuffersDescriptor);
        }
    }
    ParaNdis_DebugHistory(pContext, hopReceiveStat, NULL, nRetrieved, nReported, pContext->NetNofReceiveBuffers);
    NdisReleaseSpinLock(&pContext->ReceiveLock);
    if (nReceived && pBatchOfPackets)
    {
        DPrintf(1, ("[%s]%d: received %d buffers of max %d", __FUNCTION__, KeGetCurrentProcessorNumber(), nReceived, ulMaxPacketsToIndicate));
        ParaNdis_IndicateReceivedBatch(pContext, pBatchOfPackets, nReceived);
    }
    if (pBatchOfPackets) NdisFreeMemory(pBatchOfPackets, 0, 0);
    return nReported;
}

void ParaNdis_ReportLinkStatus(PARANDIS_ADAPTER *pContext, BOOLEAN bForce)
{
    BOOLEAN bConnected = TRUE;
    if (pContext->bLinkDetectSupported)
    {
        USHORT linkStatus = 0;
        USHORT offset = sizeof(pContext->CurrentMacAddress);
        // link changed
        VirtIODeviceGet(&pContext->IODevice, offset, &linkStatus, sizeof(linkStatus));
        bConnected = (linkStatus & VIRTIO_NET_S_LINK_UP) != 0;
    }
    ParaNdis_IndicateConnect(pContext, bConnected, bForce);
}

static BOOLEAN RestartQueueSynchronously(tSynchronizedContext *SyncContext)
{
    struct virtqueue * _vq = (struct virtqueue *) SyncContext->Parameter;
    bool res = _vq->vq_ops->restart(_vq);

    ParaNdis_DebugHistory(SyncContext->pContext, hopDPC, (PVOID)SyncContext->Parameter, 0x20, res, 0);
    return !res;
}
/**********************************************************
DPC implementation, common for both NDIS
Parameters:
    context
***********************************************************/
ULONG ParaNdis_DPCWorkBody(PARANDIS_ADAPTER *pContext, ULONG ulMaxPacketsToIndicate)
{
    ULONG stillRequiresProcessing = 0;
    ULONG interruptSources;
    UINT uIndicatedRXPackets = 0;
    UINT numOfPacketsToIndicate = min(ulMaxPacketsToIndicate, pContext->uNumberOfHandledRXPacketsInDPC);

    DEBUG_ENTRY(5);
    if (pContext->bEnableInterruptHandlingDPC)
    {
        InterlockedIncrement(&pContext->counterDPCInside);
        if (pContext->bEnableInterruptHandlingDPC)
        {
            BOOLEAN bDoKick = FALSE;

            InterlockedExchange(&pContext->bDPCInactive, 0);
            interruptSources = InterlockedExchange(&pContext->InterruptStatus, 0);
            ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)1, interruptSources, 0, 0);
            if ((interruptSources & isControl) && pContext->bLinkDetectSupported)
            {
                ParaNdis_ReportLinkStatus(pContext, FALSE);
            }
            if (interruptSources & isTransmit)
            {
                bDoKick = ParaNdis_ProcessTx(pContext, TRUE, TRUE);
            }
            if (interruptSources & isReceive)
            {
                int nRestartResult = 0;

                do
                {
                    LONG rxActive = InterlockedIncrement(&pContext->dpcReceiveActive);
                    if (rxActive == 1)
                    {
                        uIndicatedRXPackets += ParaNdis_ProcessRxPath(pContext, numOfPacketsToIndicate - uIndicatedRXPackets);
                        InterlockedDecrement(&pContext->dpcReceiveActive);
                        NdisAcquireSpinLock(&pContext->ReceiveLock);
                        nRestartResult = ParaNdis_SynchronizeWithInterrupt(
                            pContext, pContext->ulRxMessage, RestartQueueSynchronously, pContext->NetReceiveQueue);
                        ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)3, nRestartResult, 0, 0);
                        NdisReleaseSpinLock(&pContext->ReceiveLock);
                        DPrintf(nRestartResult ? 2 : 6, ("[%s] queue restarted%s", __FUNCTION__, nRestartResult ? "(Rerun)" : "(Done)"));

                        if (uIndicatedRXPackets < numOfPacketsToIndicate)
                        {

                        }
                        else if (uIndicatedRXPackets == numOfPacketsToIndicate)
                        {
                            DPrintf(1, ("[%s] Breaking Rx loop after %d indications", __FUNCTION__, uIndicatedRXPackets));
                            ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)4, nRestartResult, 0, 0);
                            break;
                        }
                        else
                        {
                            DPrintf(0, ("[%s] Glitch found: %d allowed, %d indicated", __FUNCTION__, numOfPacketsToIndicate, uIndicatedRXPackets));
                            ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)6, nRestartResult, 0, 0);
                        }
                    }
                    else
                    {
                        InterlockedDecrement(&pContext->dpcReceiveActive);
                        if (!nRestartResult)
                        {
                            NdisAcquireSpinLock(&pContext->ReceiveLock);
                            nRestartResult = ParaNdis_SynchronizeWithInterrupt(
                                pContext, pContext->ulRxMessage, RestartQueueSynchronously, pContext->NetReceiveQueue);
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
                if (ParaNdis_SynchronizeWithInterrupt(pContext, pContext->ulTxMessage, RestartQueueSynchronously, pContext->NetSendQueue))
                    stillRequiresProcessing |= isTransmit;
                if(bDoKick)
                {
#ifdef PARANDIS_TEST_TX_KICK_ALWAYS
                    pContext->NetSendQueue->vq_ops->kick_always(pContext->NetSendQueue);
#else
                    pContext->NetSendQueue->vq_ops->kick(pContext->NetSendQueue);
#endif
                }
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
                ParaNdis_DPCWorkBody(pContext, PARANDIS_UNLIMITED_PACKETS_TO_INDICATE);
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
#ifdef DBG_USE_VIRTIO_PCI_ISR_FOR_HOST_REPORT
            WriteVirtIODeviceByte(pContext->IODevice.addr + VIRTIO_PCI_ISR, 0);
#endif
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
    PVOID Buffer            array of addresses from NDIS
    ULONG BufferSize        size of incoming buffer
    PUINT pBytesRead        update on success
    PUINT pBytesNeeded      update on wrong buffer size
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
    if (length > sizeof(pContext->MulticastData.MulticastList))
    {
        status = NDIS_STATUS_MULTICAST_FULL;
        *pBytesNeeded = sizeof(pContext->MulticastData.MulticastList);
    }
    else if (length % ETH_LENGTH_OF_ADDRESS)
    {
        status = NDIS_STATUS_INVALID_LENGTH;
        *pBytesNeeded = (length / ETH_LENGTH_OF_ADDRESS) * ETH_LENGTH_OF_ADDRESS;
    }
    else
    {
        NdisZeroMemory(pContext->MulticastData.MulticastList, sizeof(pContext->MulticastData.MulticastList));
        if (length)
            NdisMoveMemory(pContext->MulticastData.MulticastList, Buffer, length);
        pContext->MulticastData.nofMulticastEntries = length / ETH_LENGTH_OF_ADDRESS;
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
VOID ParaNdis_VirtIOEnableIrqSynchronized(PARANDIS_ADAPTER *pContext, ULONG interruptSource)
{
    if (interruptSource & isTransmit)
        pContext->NetSendQueue->vq_ops->enable_interrupt(pContext->NetSendQueue);
    if (interruptSource & isReceive)
        pContext->NetReceiveQueue->vq_ops->enable_interrupt(pContext->NetReceiveQueue);
    ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)0x10, interruptSource, TRUE, 0);
}

VOID ParaNdis_VirtIODisableIrqSynchronized(PARANDIS_ADAPTER *pContext, ULONG interruptSource)
{
    if (interruptSource & isTransmit)
        pContext->NetSendQueue->vq_ops->disable_interrupt(pContext->NetSendQueue);
    if (interruptSource & isReceive)
        pContext->NetReceiveQueue->vq_ops->disable_interrupt(pContext->NetReceiveQueue);
    ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)0x10, interruptSource, FALSE, 0);
}

/**********************************************************
Common handler of PnP events
Parameters:
Return value:
***********************************************************/
VOID ParaNdis_OnPnPEvent(
    PARANDIS_ADAPTER *pContext,
    NDIS_DEVICE_PNP_EVENT pEvent,
    PVOID   pInfo,
    ULONG   ulSize)
{
    const char *pName = "";
    DEBUG_ENTRY(0);
#undef MAKECASE
#define MAKECASE(x) case (x): pName = #x; break;
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

static BOOLEAN SendControlMessage(
    PARANDIS_ADAPTER *pContext,
    UCHAR cls,
    UCHAR cmd,
    PVOID buffer1,
    ULONG size1,
    PVOID buffer2,
    ULONG size2,
    int levelIfOK
    )
{
    BOOLEAN bOK = FALSE;
    NdisAcquireSpinLock(&pContext->ReceiveLock);
    if (pContext->ControlData.Virtual && pContext->ControlData.size > (size1 + size2 + 16))
    {
        struct VirtIOBufferDescriptor sg[4];
        PUCHAR pBase = (PUCHAR)pContext->ControlData.Virtual;
        PHYSICAL_ADDRESS phBase = pContext->ControlData.Physical;
        ULONG offset = 0;
        UINT nOut = 1;

        ((virtio_net_ctrl_hdr *)pBase)->class_of_command = cls;
        ((virtio_net_ctrl_hdr *)pBase)->cmd = cmd;
        sg[0].physAddr = phBase;
        sg[0].ulSize = sizeof(virtio_net_ctrl_hdr);
        offset += sg[0].ulSize;
        offset = (offset + 3) & ~3;
        if (size1)
        {
            NdisMoveMemory(pBase + offset, buffer1, size1);
            sg[nOut].physAddr = phBase;
            sg[nOut].physAddr.QuadPart += offset;
            sg[nOut].ulSize = size1;
            offset += size1;
            offset = (offset + 3) & ~3;
            nOut++;
        }
        if (size2)
        {
            NdisMoveMemory(pBase + offset, buffer2, size2);
            sg[nOut].physAddr = phBase;
            sg[nOut].physAddr.QuadPart += offset;
            sg[nOut].ulSize = size2;
            offset += size2;
            offset = (offset + 3) & ~3;
            nOut++;
        }
        sg[nOut].physAddr = phBase;
        sg[nOut].physAddr.QuadPart += offset;
        sg[nOut].ulSize = sizeof(virtio_net_ctrl_ack);
        *(virtio_net_ctrl_ack *)(pBase + offset) = VIRTIO_NET_ERR;

        if (0 <= pContext->NetControlQueue->vq_ops->add_buf(pContext->NetControlQueue, sg, nOut, 1, (PVOID)1, NULL, 0))
        {
            UINT len;
            void *p;
            pContext->NetControlQueue->vq_ops->kick_always(pContext->NetControlQueue);
            p = pContext->NetControlQueue->vq_ops->get_buf(pContext->NetControlQueue, &len);
            if (!p)
            {
                DPrintf(0, ("%s - ERROR: get_buf failed", __FUNCTION__));
            }
            else if (len != sizeof(virtio_net_ctrl_ack))
            {
                DPrintf(0, ("%s - ERROR: wrong len %d", __FUNCTION__, len));
            }
            else if (*(virtio_net_ctrl_ack *)(pBase + offset) != VIRTIO_NET_OK)
            {
                DPrintf(0, ("%s - ERROR: error %d returned", __FUNCTION__, *(virtio_net_ctrl_ack *)(pBase + offset)));
            }
            else
            {
                // everything is OK
                DPrintf(levelIfOK, ("%s OK(%d.%d,buffers of %d and %d) ", __FUNCTION__, cls, cmd, size1, size2));
                bOK = TRUE;
            }
        }
        else
        {
            DPrintf(0, ("%s - ERROR: add_buf failed", __FUNCTION__));
        }
    }
    else
    {
        DPrintf(0, ("%s (buffer %d,%d) - ERROR: message too LARGE", __FUNCTION__, size1, size2));
    }
    NdisReleaseSpinLock(&pContext->ReceiveLock);
    return bOK;
}

static VOID ParaNdis_DeviceFiltersUpdateRxMode(PARANDIS_ADAPTER *pContext)
{
    u8 val;
    ULONG f = pContext->PacketFilter;
    val = (f & NDIS_PACKET_TYPE_ALL_MULTICAST) ? 1 : 0;
    SendControlMessage(pContext, VIRTIO_NET_CTRL_RX_MODE, VIRTIO_NET_CTRL_RX_MODE_ALLMULTI, &val, sizeof(val), NULL, 0, 2);
    //SendControlMessage(pContext, VIRTIO_NET_CTRL_RX_MODE, VIRTIO_NET_CTRL_RX_MODE_ALLUNI, &val, sizeof(val), NULL, 0, 2);
    val = (f & (NDIS_PACKET_TYPE_MULTICAST | NDIS_PACKET_TYPE_ALL_MULTICAST)) ? 0 : 1;
    SendControlMessage(pContext, VIRTIO_NET_CTRL_RX_MODE, VIRTIO_NET_CTRL_RX_MODE_NOMULTI, &val, sizeof(val), NULL, 0, 2);
    val = (f & NDIS_PACKET_TYPE_DIRECTED) ? 0 : 1;
    SendControlMessage(pContext, VIRTIO_NET_CTRL_RX_MODE, VIRTIO_NET_CTRL_RX_MODE_NOUNI, &val, sizeof(val), NULL, 0, 2);
    val = (f & NDIS_PACKET_TYPE_BROADCAST) ? 0 : 1;
    SendControlMessage(pContext, VIRTIO_NET_CTRL_RX_MODE, VIRTIO_NET_CTRL_RX_MODE_NOBCAST, &val, sizeof(val), NULL, 0, 2);
    val = (f & NDIS_PACKET_TYPE_PROMISCUOUS) ? 1 : 0;
    SendControlMessage(pContext, VIRTIO_NET_CTRL_RX_MODE, VIRTIO_NET_CTRL_RX_MODE_PROMISC, &val, sizeof(val), NULL, 0, 2);
}

static VOID ParaNdis_DeviceFiltersUpdateAddresses(PARANDIS_ADAPTER *pContext)
{
    struct
    {
        struct virtio_net_ctrl_mac header;
        UCHAR addr[ETH_LENGTH_OF_ADDRESS];
    } uCast;
    uCast.header.entries = 1;
    NdisMoveMemory(uCast.addr, pContext->CurrentMacAddress, sizeof(uCast.addr));
    SendControlMessage(pContext, VIRTIO_NET_CTRL_MAC, VIRTIO_NET_CTRL_MAC_TABLE_SET,
        &uCast, sizeof(uCast), &pContext->MulticastData,sizeof(pContext->MulticastData.nofMulticastEntries) + pContext->MulticastData.nofMulticastEntries * ETH_ALEN, 2);
}

static VOID SetSingleVlanFilter(PARANDIS_ADAPTER *pContext, ULONG vlanId, BOOLEAN bOn, int levelIfOK)
{
    u16 val = vlanId & 0xfff;
    UCHAR cmd = bOn ? VIRTIO_NET_CTRL_VLAN_ADD : VIRTIO_NET_CTRL_VLAN_DEL;
    SendControlMessage(pContext, VIRTIO_NET_CTRL_VLAN, cmd, &val, sizeof(val), NULL, 0, levelIfOK);
}

static VOID SetAllVlanFilters(PARANDIS_ADAPTER *pContext, BOOLEAN bOn)
{
    ULONG i;
    for (i = 0; i <= MAX_VLAN_ID; ++i)
        SetSingleVlanFilter(pContext, i, bOn, 7);
}

/*
    possible values of filter set (pContext->ulCurrentVlansFilterSet):
    0 - all disabled
    1..4095 - one selected enabled
    4096 - all enabled
    Note that only 0th vlan can't be enabled
*/
VOID ParaNdis_DeviceFiltersUpdateVlanId(PARANDIS_ADAPTER *pContext)
{
    if (pContext->bHasHardwareFilters)
    {
        ULONG newFilterSet;
        if (IsVlanSupported(pContext))
            newFilterSet = pContext->VlanId ? pContext->VlanId : (MAX_VLAN_ID + 1);
        else
            newFilterSet = IsPrioritySupported(pContext) ? (MAX_VLAN_ID + 1) : 0;
        if (newFilterSet != pContext->ulCurrentVlansFilterSet)
        {
            if (pContext->ulCurrentVlansFilterSet > MAX_VLAN_ID)
                SetAllVlanFilters(pContext, FALSE);
            else if (pContext->ulCurrentVlansFilterSet)
                SetSingleVlanFilter(pContext, pContext->ulCurrentVlansFilterSet, FALSE, 2);

            pContext->ulCurrentVlansFilterSet = newFilterSet;

            if (pContext->ulCurrentVlansFilterSet > MAX_VLAN_ID)
                SetAllVlanFilters(pContext, TRUE);
            else if (pContext->ulCurrentVlansFilterSet)
                SetSingleVlanFilter(pContext, pContext->ulCurrentVlansFilterSet, TRUE, 2);
        }
    }
}

VOID ParaNdis_UpdateDeviceFilters(PARANDIS_ADAPTER *pContext)
{
    if (pContext->bHasHardwareFilters)
    {
        ParaNdis_DeviceFiltersUpdateRxMode(pContext);
        ParaNdis_DeviceFiltersUpdateAddresses(pContext);
        ParaNdis_DeviceFiltersUpdateVlanId(pContext);
    }
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
    if (pContext->bDoGuestChecksumOnReceive)
        VirtIODeviceEnableGuestFeature(&pContext->IODevice, VIRTIO_NET_F_GUEST_CSUM);

    VirtIODeviceRenewQueue(pContext->NetReceiveQueue);
    VirtIODeviceRenewQueue(pContext->NetSendQueue);

    if (pContext->NetControlQueue) {
        VirtIODeviceRenewQueue(pContext->NetControlQueue);
    }

    ParaNdis_RestoreDeviceConfigurationAfterReset(pContext);

    ParaNdis_UpdateDeviceFilters(pContext);

    InitializeListHead(&TempList);
    
    /* submit all the receive buffers */
    NdisAcquireSpinLock(&pContext->ReceiveLock);
    
    pContext->ReuseBufferProc = ReuseReceiveBufferRegular;
    
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
    pContext->NetReceiveQueue->vq_ops->kick(pContext->NetReceiveQueue);
    ParaNdis_SetPowerState(pContext, NdisDeviceStateD0);
    pContext->bEnableInterruptHandlingDPC = TRUE;
    VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
    
    NdisReleaseSpinLock(&pContext->ReceiveLock);

    // if bFastSuspendInProcess is set by Win8 power-off procedure,
    // the ParaNdis_Resume enables Tx and RX
    // otherwise it does not do anything in Vista+ (Tx and RX are enabled after power-on by Restart)
    ParaNdis_Resume(pContext);
    pContext->bFastSuspendInProcess = FALSE;
    
    ParaNdis_ReportLinkStatus(pContext, TRUE);
    ParaNdis_DebugHistory(pContext, hopPowerOn, NULL, 0, 0, 0);
}

VOID ParaNdis_PowerOff(PARANDIS_ADAPTER *pContext)
{
    DEBUG_ENTRY(0);
    ParaNdis_DebugHistory(pContext, hopPowerOff, NULL, 1, 0, 0);

    ParaNdis_IndicateConnect(pContext, FALSE, FALSE);

    // if bFastSuspendInProcess is set by Win8 power-off procedure
    // the ParaNdis_Suspend does fast Rx stop without waiting (=>srsPausing, if there are some RX packets in Ndis)
    pContext->bFastSuspendInProcess = pContext->bNoPauseOnSuspend && pContext->ReceiveState == srsEnabled;
    ParaNdis_Suspend(pContext);
    VirtIODeviceRemoveStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
    
    if (pContext->bFastSuspendInProcess)
    {
        NdisAcquireSpinLock(&pContext->ReceiveLock);
        pContext->ReuseBufferProc = ReuseReceiveBufferPowerOff;
        NdisReleaseSpinLock(&pContext->ReceiveLock);
    }
    
    ParaNdis_SetPowerState(pContext, NdisDeviceStateD3);

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
    if (pContext->NetControlQueue) {
        pContext->NetControlQueue->vq_ops->shutdown(pContext->NetControlQueue);
    }

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
#ifdef DBG_USE_VIRTIO_PCI_ISR_FOR_HOST_REPORT
        WriteVirtIODeviceByte(pContext->IODevice.addr + VIRTIO_PCI_ISR, 1);
#endif
    }
}

tChecksumCheckResult ParaNdis_CheckRxChecksum(PARANDIS_ADAPTER *pContext, ULONG virtioFlags, PVOID pRxPacket, ULONG len)
{
    tOffloadSettingsFlags f = pContext->Offload.flags;
    tChecksumCheckResult res, resIp;
    PVOID pIpHeader = RtlOffsetToPointer(pRxPacket, ETH_HEADER_SIZE);
    tTcpIpPacketParsingResult ppr;
    ULONG flagsToCalculate = 0;
    res.value = 0;
    resIp.value = 0;

    //VIRTIO_NET_HDR_F_NEEDS_CSUM - we need to calculate TCP/UDP CS
    //VIRTIO_NET_HDR_F_DATA_VALID - host tells us TCP/UDP CS is OK

    if (f.fRxIPChecksum) flagsToCalculate |= pcrIpChecksum; // check only

    if (!(virtioFlags & VIRTIO_NET_HDR_F_DATA_VALID))
    {
        if (virtioFlags & VIRTIO_NET_HDR_F_NEEDS_CSUM)
        {
            flagsToCalculate |= pcrFixXxpChecksum | pcrTcpChecksum | pcrUdpChecksum;
        }
        else
        {
            if (f.fRxTCPChecksum) flagsToCalculate |= pcrTcpV4Checksum;
            if (f.fRxUDPChecksum) flagsToCalculate |= pcrUdpV4Checksum;
            if (f.fRxTCPv6Checksum) flagsToCalculate |= pcrTcpV6Checksum;
            if (f.fRxUDPv6Checksum) flagsToCalculate |= pcrUdpV6Checksum;
        }
    }

    ppr = ParaNdis_CheckSumVerify(pIpHeader, len - ETH_HEADER_SIZE, flagsToCalculate, __FUNCTION__);

    if (virtioFlags & VIRTIO_NET_HDR_F_DATA_VALID)
    {
        pContext->extraStatistics.framesRxCSHwOK++;
        ppr.xxpCheckSum = ppresCSOK;
    }

    if (ppr.ipStatus == ppresIPV4 && !ppr.IsFragment)
    {
        if (f.fRxIPChecksum)
        {
            res.flags.IpOK =  ppr.ipCheckSum == ppresCSOK;
            res.flags.IpFailed = ppr.ipCheckSum == ppresCSBad;
        }
        if(ppr.xxpStatus == ppresXxpKnown)
        {
            if(ppr.TcpUdp == ppresIsTCP) /* TCP */
            {
                if (f.fRxTCPChecksum)
                {
                    res.flags.TcpOK = ppr.xxpCheckSum == ppresCSOK || ppr.fixedXxpCS;
                    res.flags.TcpFailed = !res.flags.TcpOK;
                }
            }
            else /* UDP */
            {
                if (f.fRxUDPChecksum)
                {
                    res.flags.UdpOK = ppr.xxpCheckSum == ppresCSOK || ppr.fixedXxpCS;
                    res.flags.UdpFailed = !res.flags.UdpOK;
                }
            }
        }
    }
    else if (ppr.ipStatus == ppresIPV6)
    {
        if(ppr.xxpStatus == ppresXxpKnown)
        {
            if(ppr.TcpUdp == ppresIsTCP) /* TCP */
            {
                if (f.fRxTCPv6Checksum)
                {
                    res.flags.TcpOK = ppr.xxpCheckSum == ppresCSOK || ppr.fixedXxpCS;
                    res.flags.TcpFailed = !res.flags.TcpOK;
                }
            }
            else /* UDP */
            {
                if (f.fRxUDPv6Checksum)
                {
                    res.flags.UdpOK = ppr.xxpCheckSum == ppresCSOK || ppr.fixedXxpCS;
                    res.flags.UdpFailed = !res.flags.UdpOK;
                }
            }
        }
    }

    if (pContext->bDoIPCheckRx &&
        (f.fRxIPChecksum || f.fRxTCPChecksum || f.fRxUDPChecksum || f.fRxTCPv6Checksum || f.fRxUDPv6Checksum))
    {
        ppr = ParaNdis_CheckSumVerify(pIpHeader, len - ETH_HEADER_SIZE, pcrAnyChecksum, __FUNCTION__"(2)");
        if (ppr.ipStatus == ppresIPV4 && !ppr.IsFragment)
        {
            resIp.flags.IpOK = !!f.fRxIPChecksum && ppr.ipCheckSum == ppresCSOK;
            resIp.flags.IpFailed = !!f.fRxIPChecksum && ppr.ipCheckSum == ppresCSBad;
            if (f.fRxTCPChecksum && ppr.xxpStatus == ppresXxpKnown && ppr.TcpUdp == ppresIsTCP)
            {
                resIp.flags.TcpOK = ppr.xxpCheckSum == ppresCSOK;
                resIp.flags.TcpFailed = ppr.xxpCheckSum == ppresCSBad;
            }
            if (f.fRxUDPChecksum && ppr.xxpStatus == ppresXxpKnown && ppr.TcpUdp == ppresIsUDP)
            {
                resIp.flags.UdpOK = ppr.xxpCheckSum == ppresCSOK;
                resIp.flags.UdpFailed = ppr.xxpCheckSum == ppresCSBad;
            }
        }
        else if (ppr.ipStatus == ppresIPV6)
        {
            if (f.fRxTCPv6Checksum && ppr.xxpStatus == ppresXxpKnown && ppr.TcpUdp == ppresIsTCP)
            {
                resIp.flags.TcpOK = ppr.xxpCheckSum == ppresCSOK;
                resIp.flags.TcpFailed = ppr.xxpCheckSum == ppresCSBad;
            }
            if (f.fRxUDPv6Checksum && ppr.xxpStatus == ppresXxpKnown && ppr.TcpUdp == ppresIsUDP)
            {
                resIp.flags.UdpOK = ppr.xxpCheckSum == ppresCSOK;
                resIp.flags.UdpFailed = ppr.xxpCheckSum == ppresCSBad;
            }
        }

        if (res.value != resIp.value)
        {
            // if HW did not set some bits that IP checker set, it is a mistake:
            // or GOOD CS is not labeled, or BAD checksum is not labeled
            tChecksumCheckResult diff;
            diff.value = resIp.value & ~res.value;
            if (diff.flags.IpFailed || diff.flags.TcpFailed || diff.flags.UdpFailed)
                pContext->extraStatistics.framesRxCSHwMissedBad++;
            if (diff.flags.IpOK || diff.flags.TcpOK || diff.flags.UdpOK)
                pContext->extraStatistics.framesRxCSHwMissedGood++;
            if (diff.value)
            {
                DPrintf(0, ("[%s] real %X <> %X (virtio %X)", __FUNCTION__, resIp.value, res.value, virtioFlags));
            }
            res.value = resIp.value;
        }
    }

    return res;
}
