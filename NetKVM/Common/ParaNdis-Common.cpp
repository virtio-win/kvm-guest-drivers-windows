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

static void ReuseReceiveBufferRegular(PARANDIS_ADAPTER *pContext, pRxNetDescriptor pBuffersDescriptor);
static void ReuseReceiveBufferPowerOff(PARANDIS_ADAPTER *pContext, pRxNetDescriptor pBuffersDescriptor);
static VOID ParaNdis_UpdateMAC(PARANDIS_ADAPTER *pContext);

static __inline pRxNetDescriptor ReceiveQueueGetBuffer(PPARANDIS_RECEIVE_QUEUE pQueue);

// TODO: remove when the problem solved
void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue);

//#define ROUNDSIZE(sz) ((sz + 15) & ~15)
#define MAX_VLAN_ID     4095

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

typedef struct _tagConfigurationEntry
{
    const char      *Name;
    ULONG           ulValue;
    ULONG           ulMinimal;
    ULONG           ulMaximal;
}tConfigurationEntry;

typedef struct _tagConfigurationEntries
{
    tConfigurationEntry PrioritySupport;
    tConfigurationEntry ConnectRate;
    tConfigurationEntry isLogEnabled;
    tConfigurationEntry debugLevel;
    tConfigurationEntry TxCapacity;
    tConfigurationEntry RxCapacity;
    tConfigurationEntry LogStatistics;
    tConfigurationEntry OffloadTxChecksum;
    tConfigurationEntry OffloadTxLSO;
    tConfigurationEntry OffloadRxCS;
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
    tConfigurationEntry PublishIndices;
    tConfigurationEntry MTU;
    tConfigurationEntry NumberOfHandledRXPackersInDPC;
#if PARANDIS_SUPPORT_RSS
    tConfigurationEntry RSSOffloadSupported;
    tConfigurationEntry NumRSSQueues;
#endif
#if PARANDIS_SUPPORT_RSC
    tConfigurationEntry RSCIPv4Supported;
    tConfigurationEntry RSCIPv6Supported;
#endif
}tConfigurationEntries;

static const tConfigurationEntries defaultConfiguration =
{
    { "Priority",       0,  0,  1 },
    { "ConnectRate",    100,10,10000 },
    { "DoLog",          1,  0,  1 },
    { "DebugLevel",     2,  0,  8 },
    { "TxCapacity",     1024,   16, 1024 },
    { "RxCapacity",     256, 32, 1024 },
    { "LogStatistics",  0, 0, 10000},
    { "Offload.TxChecksum", 0, 0, 31},
    { "Offload.TxLSO",  0, 0, 2},
    { "Offload.RxCS",   0, 0, 31},
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
    { "PublishIndices", 1, 0, 1},
    { "MTU", 1500, 500, 65500},
    { "NumberOfHandledRXPackersInDPC", MAX_RX_LOOPS, 1, 10000},
#if PARANDIS_SUPPORT_RSS
    { "*RSS", 1, 0, 1},
    { "*NumRssQueues", 8, 1, PARANDIS_RSS_MAX_RECEIVE_QUEUES},
#endif
#if PARANDIS_SUPPORT_RSC
    { "*RscIPv4", 1, 0, 1},
    { "*RscIPv6", 1, 0, 1},
#endif
};

static void ParaNdis_ResetVirtIONetDevice(PARANDIS_ADAPTER *pContext)
{
    VirtIODeviceReset(&pContext->IODevice);
    DPrintf(0, ("[%s] Done\n", __FUNCTION__));
    /* reset all the features in the device */
    pContext->ulCurrentVlansFilterSet = 0;
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
#pragma warning(push)
#pragma warning(disable:6102)
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
#pragma warning(pop)
    DPrintf(2, ("[%s] %s read for %s - 0x%x\n",
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
        DPrintf(0, ("[%s] Warning: %s\n", procname, reason));
        pContext->Offload.flagsValue &= ~osbT6Lso;
        ParaNdis_ResetOffloadSettings(pContext, NULL, NULL);
    }
}

/**********************************************************
Loads NIC parameters from adapter registry key
Parameters:
    context
    PUCHAR *ppNewMACAddress - pointer to hold MAC address if configured from host
***********************************************************/
static void ReadNicConfiguration(PARANDIS_ADAPTER *pContext, PUCHAR pNewMACAddress)
{
    NDIS_HANDLE cfg;
    tConfigurationEntries *pConfiguration = (tConfigurationEntries *) ParaNdis_AllocateMemory(pContext, sizeof(tConfigurationEntries));
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
            GetConfigurationEntry(cfg, &pConfiguration->TxCapacity);
            GetConfigurationEntry(cfg, &pConfiguration->RxCapacity);
            GetConfigurationEntry(cfg, &pConfiguration->LogStatistics);
            GetConfigurationEntry(cfg, &pConfiguration->OffloadTxChecksum);
            GetConfigurationEntry(cfg, &pConfiguration->OffloadTxLSO);
            GetConfigurationEntry(cfg, &pConfiguration->OffloadRxCS);
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
            GetConfigurationEntry(cfg, &pConfiguration->PublishIndices);
            GetConfigurationEntry(cfg, &pConfiguration->MTU);
            GetConfigurationEntry(cfg, &pConfiguration->NumberOfHandledRXPackersInDPC);
#if PARANDIS_SUPPORT_RSS
            GetConfigurationEntry(cfg, &pConfiguration->RSSOffloadSupported);
            GetConfigurationEntry(cfg, &pConfiguration->NumRSSQueues);
#endif
#if PARANDIS_SUPPORT_RSC
            GetConfigurationEntry(cfg, &pConfiguration->RSCIPv4Supported);
            GetConfigurationEntry(cfg, &pConfiguration->RSCIPv6Supported);
#endif

            bDebugPrint = pConfiguration->isLogEnabled.ulValue;
            virtioDebugLevel = pConfiguration->debugLevel.ulValue;
            pContext->maxFreeTxDescriptors = pConfiguration->TxCapacity.ulValue;
            pContext->NetMaxReceiveBuffers = pConfiguration->RxCapacity.ulValue;
            pContext->Limits.nPrintDiagnostic = pConfiguration->LogStatistics.ulValue;
            pContext->uNumberOfHandledRXPacketsInDPC = pConfiguration->NumberOfHandledRXPackersInDPC.ulValue;
            pContext->bDoSupportPriority = pConfiguration->PrioritySupport.ulValue != 0;
            pContext->ulFormalLinkSpeed  = pConfiguration->ConnectRate.ulValue;
            pContext->ulFormalLinkSpeed *= 1000000;
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
            pContext->MaxPacketSize.nMaxDataSize = pConfiguration->MTU.ulValue;
#if PARANDIS_SUPPORT_RSS
            pContext->bRSSOffloadSupported = pConfiguration->RSSOffloadSupported.ulValue ? TRUE : FALSE;
            pContext->RSSMaxQueuesNumber = (CCHAR) pConfiguration->NumRSSQueues.ulValue;
#endif
#if PARANDIS_SUPPORT_RSC
            pContext->RSC.bIPv4SupportedSW = (UCHAR)pConfiguration->RSCIPv4Supported.ulValue;
            pContext->RSC.bIPv6SupportedSW = (UCHAR)pConfiguration->RSCIPv6Supported.ulValue;
#endif
            if (!pContext->bDoSupportPriority)
                pContext->ulPriorityVlanSetting = 0;
            // if Vlan not supported
            if (!IsVlanSupported(pContext)) {
                pContext->VlanId = 0;
            }

            {
                NDIS_STATUS status;
                PVOID p;
                UINT  len = 0;
#pragma warning(push)
#pragma warning(disable:6102)
                NdisReadNetworkAddress(&status, &p, &len, cfg);
                if (status == NDIS_STATUS_SUCCESS && len == ETH_LENGTH_OF_ADDRESS)
                {
                    NdisMoveMemory(pNewMACAddress, p, len);
                }
                else if (len && len != ETH_LENGTH_OF_ADDRESS)
                {
                    DPrintf(0, ("[%s] MAC address has wrong length of %d\n", __FUNCTION__, len));
                }
                else
                {
                    DPrintf(4, ("[%s] Nothing read for MAC, error %X\n", __FUNCTION__, status));
                }
#pragma warning(pop)
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
            DPrintf(0, ("Found IO ports at %08lX(%d)\n", Start.LowPart, len));
            pResources->ulIOAddress = Start.LowPart;
            pResources->IOLength = len;
        }
        else if (type == CmResourceTypeInterrupt)
        {
            pResources->Vector = RList->PartialDescriptors[i].u.Interrupt.Vector;
            pResources->Level = RList->PartialDescriptors[i].u.Interrupt.Level;
            pResources->Affinity = RList->PartialDescriptors[i].u.Interrupt.Affinity;
            pResources->InterruptFlags = RList->PartialDescriptors[i].Flags;
            DPrintf(0, ("Found Interrupt vector %d, level %d, affinity %X, flags %X\n",
                pResources->Vector, pResources->Level, (ULONG)pResources->Affinity, pResources->InterruptFlags));
        }
    }
    return pResources->ulIOAddress && pResources->Vector;
}

static void DumpVirtIOFeatures(PPARANDIS_ADAPTER pContext)
{
    static const struct {  ULONG bitmask;  PCHAR Name; } Features[] =
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
        {VIRTIO_NET_F_CTRL_MAC_ADDR, "VIRTIO_NET_F_CTRL_MAC_ADDR"},
        {VIRTIO_F_INDIRECT, "VIRTIO_F_INDIRECT"},
        {VIRTIO_F_PUBLISH_INDICES, "VIRTIO_F_PUBLISH_INDICES"},
    };
    UINT i;
    for (i = 0; i < sizeof(Features)/sizeof(Features[0]); ++i)
    {
        if (VirtIOIsFeatureEnabled(pContext->u32HostFeatures, Features[i].bitmask))
        {
            DPrintf(0, ("VirtIO Host Feature %s\n", Features[i].Name));
        }
    }
}

static BOOLEAN
AckFeature(PPARANDIS_ADAPTER pContext, UINT32 Feature)
{
    if (VirtIOIsFeatureEnabled(pContext->u32HostFeatures, Feature))
    {
        VirtIOFeatureEnable(pContext->u32GuestFeatures, Feature);
        return TRUE;
    }
    return FALSE;
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

    DPrintf(0, ("[Diag!%X] RX buffers at VIRTIO %d of %d\n",
        pContext->CurrentMacAddress[5],
        pContext->NetNofReceiveBuffers,
        pContext->NetMaxReceiveBuffers));
    DPrintf(0, ("[Diag!] TX desc available %d/%d, buf %d\n",
        pContext->TXPath.GetFreeTXDescriptors(),
        pContext->maxFreeTxDescriptors,
        pContext->TXPath.GetFreeHWBuffers()));
    if (pContext->TXPath.GetTXPacketsToReturn())
    {
        DPrintf(0, ("[Diag!] TX packets to return %d\n", pContext->TXPath.GetTXPacketsToReturn()));
    }
    DPrintf(0, ("[Diag!] Bytes transmitted %I64u, received %I64u\n",
        pContext->Statistics.ifHCOutOctets,
        pContext->Statistics.ifHCInOctets));
    DPrintf(0, ("[Diag!] Tx frames %I64u, CSO %d, LSO %d, indirect %d\n",
        totalTxFrames,
        pContext->extraStatistics.framesCSOffload,
        pContext->extraStatistics.framesLSO,
        pContext->extraStatistics.framesIndirect));
    DPrintf(0, ("[Diag!] Rx frames %I64u, Rx.Pri %d, RxHwCS.OK %d, FiltOut %d\n",
        totalRxFrames, pContext->extraStatistics.framesRxPriority,
        pContext->extraStatistics.framesRxCSHwOK, pContext->extraStatistics.framesFilteredOut));
    if (pContext->extraStatistics.framesRxCSHwMissedBad || pContext->extraStatistics.framesRxCSHwMissedGood)
    {
        DPrintf(0, ("[Diag!] RxHwCS mistakes: missed bad %d, missed good %d\n",
            pContext->extraStatistics.framesRxCSHwMissedBad, pContext->extraStatistics.framesRxCSHwMissedGood));
    }
}

static
VOID InitializeRSCState(PPARANDIS_ADAPTER pContext)
{
#if PARANDIS_SUPPORT_RSC

    pContext->RSC.bIPv4Enabled = FALSE;
    pContext->RSC.bIPv6Enabled = FALSE;

    if(!pContext->bGuestChecksumSupported)
    {
        DPrintf(0, ("[%s] Guest TSO cannot be enabled without guest checksum\n", __FUNCTION__) );
        return;
    }

    if(pContext->RSC.bIPv4SupportedSW)
    {
        pContext->RSC.bIPv4Enabled =
            pContext->RSC.bIPv4SupportedHW =
                AckFeature(pContext, VIRTIO_NET_F_GUEST_TSO4);
    }
    else
    {
        pContext->RSC.bIPv4SupportedHW =
            VirtIOIsFeatureEnabled(pContext->u32HostFeatures, VIRTIO_NET_F_GUEST_TSO4);
    }

    if(pContext->RSC.bIPv6SupportedSW)
    {
        pContext->RSC.bIPv6Enabled =
            pContext->RSC.bIPv6SupportedHW =
                AckFeature(pContext, VIRTIO_NET_F_GUEST_TSO6);
    }
    else
    {
        pContext->RSC.bIPv6SupportedHW =
            VirtIOIsFeatureEnabled(pContext->u32HostFeatures, VIRTIO_NET_F_GUEST_TSO6);
    }

    pContext->RSC.bHasDynamicConfig = (pContext->RSC.bIPv4Enabled || pContext->RSC.bIPv6Enabled) &&
                                      AckFeature(pContext, VIRTIO_NET_F_CTRL_GUEST_OFFLOADS);

    DPrintf(0, ("[%s] Guest TSO state: IP4=%d, IP6=%d, Dynamic=%d\n", __FUNCTION__,
        pContext->RSC.bIPv4Enabled, pContext->RSC.bIPv6Enabled, pContext->RSC.bHasDynamicConfig) );
#else
    UNREFERENCED_PARAMETER(pContext);
#endif
}

static __inline void
DumpMac(int dbg_level, const char* header_str, UCHAR* mac)
{
    DPrintf(dbg_level,("%s: %02x-%02x-%02x-%02x-%02x-%02x\n",
        header_str, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]));

}

static __inline void
SetDeviceMAC(PPARANDIS_ADAPTER pContext, PUCHAR pDeviceMAC)
{
    if(pContext->bCfgMACAddrSupported && !pContext->bCtrlMACAddrSupported)
    {
        VirtIODeviceSet(&pContext->IODevice, 0, pDeviceMAC, ETH_LENGTH_OF_ADDRESS);
    }
}

static void
InitializeMAC(PPARANDIS_ADAPTER pContext, PUCHAR pCurrentMAC)
{
    //Acknowledge related features
    pContext->bCfgMACAddrSupported = AckFeature(pContext, VIRTIO_NET_F_MAC);
    pContext->bCtrlMACAddrSupported = AckFeature(pContext, VIRTIO_NET_F_CTRL_MAC_ADDR);

    //Read and validate permanent MAC address
    if (pContext->bCfgMACAddrSupported)
    {
        VirtIODeviceGet(&pContext->IODevice, 0, &pContext->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
        if (!ParaNdis_ValidateMacAddress(pContext->PermanentMacAddress, FALSE))
        {
            DumpMac(0, "Invalid device MAC ignored", pContext->PermanentMacAddress);
            NdisZeroMemory(pContext->PermanentMacAddress, sizeof(pContext->PermanentMacAddress));
        }
    }

    if (ETH_IS_EMPTY(pContext->PermanentMacAddress))
    {
        pContext->PermanentMacAddress[0] = 0x02;
        pContext->PermanentMacAddress[1] = 0x50;
        pContext->PermanentMacAddress[2] = 0xF2;
        pContext->PermanentMacAddress[3] = 0x00;
        pContext->PermanentMacAddress[4] = 0x01;
        pContext->PermanentMacAddress[5] = 0x80 | (UCHAR)(pContext->ulUniqueID & 0xFF);
        DumpMac(0, "No device MAC present, use default", pContext->PermanentMacAddress);
    }
    DumpMac(0, "Permanent device MAC", pContext->PermanentMacAddress);

    //Read and validate configured MAC address
    if (ParaNdis_ValidateMacAddress(pCurrentMAC, TRUE))
    {
        DPrintf(0, ("[%s] MAC address from configuration used\n", __FUNCTION__));
        ETH_COPY_NETWORK_ADDRESS(pContext->CurrentMacAddress, pCurrentMAC);
    }
    else
    {
        DPrintf(0, ("No valid MAC configured\n", __FUNCTION__));
        ETH_COPY_NETWORK_ADDRESS(pContext->CurrentMacAddress, pContext->PermanentMacAddress);
    }

    //If control channel message for MAC address configuration is not supported
    //  Configure device with actual MAC address via configurations space
    //Else actual MAC address will be configured later via control queue
    SetDeviceMAC(pContext, pContext->CurrentMacAddress);

    DumpMac(0, "Actual MAC", pContext->CurrentMacAddress);
}

static __inline void
RestoreMAC(PPARANDIS_ADAPTER pContext)
{
    SetDeviceMAC(pContext, pContext->PermanentMacAddress);
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
    USHORT linkStatus = 0;
    UCHAR CurrentMAC[ETH_LENGTH_OF_ADDRESS] = {0};
    ULONG dependentOptions;

    DEBUG_ENTRY(0);

    ReadNicConfiguration(pContext, CurrentMAC);

    pContext->fCurrentLinkState = MediaConnectStateUnknown;
    pContext->powerState = NdisDeviceStateUnspecified;

    pContext->MaxPacketSize.nMaxFullSizeOS = pContext->MaxPacketSize.nMaxDataSize + ETH_HEADER_SIZE;
    pContext->MaxPacketSize.nMaxFullSizeHwTx = pContext->MaxPacketSize.nMaxFullSizeOS;
#if PARANDIS_SUPPORT_RSC
    pContext->MaxPacketSize.nMaxDataSizeHwRx = MAX_HW_RX_PACKET_SIZE;
    pContext->MaxPacketSize.nMaxFullSizeOsRx = MAX_OS_RX_PACKET_SIZE;
#else
    pContext->MaxPacketSize.nMaxDataSizeHwRx = pContext->MaxPacketSize.nMaxFullSizeOS + ETH_PRIORITY_HEADER_SIZE;
    pContext->MaxPacketSize.nMaxFullSizeOsRx = pContext->MaxPacketSize.nMaxFullSizeOS;
#endif
    if (pContext->ulPriorityVlanSetting)
        pContext->MaxPacketSize.nMaxFullSizeHwTx = pContext->MaxPacketSize.nMaxFullSizeOS + ETH_PRIORITY_HEADER_SIZE;

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
            DPrintf(0, ("[%s] Message interrupt assigned\n", __FUNCTION__));
            pContext->bUsingMSIX = TRUE;
        }

        VirtIODeviceInitialize(&pContext->IODevice, pContext->AdapterResources.ulIOAddress, sizeof(pContext->IODevice));
        VirtIODeviceSetMSIXUsed(&pContext->IODevice, pContext->bUsingMSIX ? true : false);
        ParaNdis_ResetVirtIONetDevice(pContext);
        VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
        VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER);
        pContext->u32HostFeatures = VirtIODeviceReadHostFeatures(&pContext->IODevice);
        DumpVirtIOFeatures(pContext);

        pContext->bLinkDetectSupported = AckFeature(pContext, VIRTIO_NET_F_STATUS);
        if(pContext->bLinkDetectSupported) {
            VirtIODeviceGet(&pContext->IODevice, ETH_LENGTH_OF_ADDRESS, &linkStatus, sizeof(linkStatus));
            pContext->bConnected = (linkStatus & VIRTIO_NET_S_LINK_UP) != 0;
            DPrintf(0, ("[%s] Link status on driver startup: %d\n", __FUNCTION__, pContext->bConnected));
        }

        InitializeMAC(pContext, CurrentMAC);

        pContext->bUseMergedBuffers = AckFeature(pContext, VIRTIO_NET_F_MRG_RXBUF);
        pContext->nVirtioHeaderSize = (pContext->bUseMergedBuffers) ? sizeof(virtio_net_hdr_ext) : sizeof(virtio_net_hdr_basic);
        pContext->bDoPublishIndices = AckFeature(pContext, VIRTIO_F_PUBLISH_INDICES);
    }
    else
    {
        DPrintf(0, ("[%s] Error: Incomplete resources\n", __FUNCTION__));
        /* avoid deregistering if failed */
        pContext->AdapterResources.ulIOAddress = 0;
        status = NDIS_STATUS_RESOURCE_CONFLICT;
    }

    dependentOptions = osbT4TcpChecksum | osbT4UdpChecksum | osbT4TcpOptionsChecksum;

    if((pContext->Offload.flagsValue & dependentOptions) && !AckFeature(pContext, VIRTIO_NET_F_CSUM))
    {
        DPrintf(0, ("[%s] Host does not support CSUM, disabling CS offload\n", __FUNCTION__) );
        pContext->Offload.flagsValue &= ~dependentOptions;
    }

    pContext->bGuestChecksumSupported = AckFeature(pContext, VIRTIO_NET_F_GUEST_CSUM);
    AckFeature(pContext, VIRTIO_NET_F_CTRL_VQ);

    InitializeRSCState(pContext);

    // now, after we checked the capabilities, we can initialize current
    // configuration of offload tasks
    ParaNdis_ResetOffloadSettings(pContext, NULL, NULL);

    if (pContext->Offload.flags.fTxLso && !AckFeature(pContext, VIRTIO_NET_F_HOST_TSO4))
    {
        DisableLSOv4Permanently(pContext, __FUNCTION__, "Host does not support TSOv4\n");
    }

    if (pContext->Offload.flags.fTxLsov6 && !AckFeature(pContext, VIRTIO_NET_F_HOST_TSO6))
    {
        DisableLSOv6Permanently(pContext, __FUNCTION__, "Host does not support TSOv6");
    }

    pContext->bUseIndirect = AckFeature(pContext, VIRTIO_F_INDIRECT);

    pContext->bHasHardwareFilters = AckFeature(pContext, VIRTIO_NET_F_CTRL_RX_EXTRA);

    pContext->ReuseBufferProc = ReuseReceiveBufferRegular;

    VirtIODeviceWriteGuestFeatures(&pContext->IODevice, pContext->u32GuestFeatures);
    NdisInitializeEvent(&pContext->ResetEvent);
    DEBUG_EXIT_STATUS(0, status);
    return status;
}

static void FreeRxBufferDescriptor(PARANDIS_ADAPTER *pContext, pRxNetDescriptor p)
{
    ULONG i;
    for(i = 0; i < p->PagesAllocated; i++)
    {
        ParaNdis_FreePhysicalMemory(pContext, &p->PhysicalPages[i]);
    }

    if(p->BufferSGArray) NdisFreeMemory(p->BufferSGArray, 0, 0);
    if(p->PhysicalPages) NdisFreeMemory(p->PhysicalPages, 0, 0);
    NdisFreeMemory(p, 0, 0);
}

static void FreeRxDescriptorsFromList(PARANDIS_ADAPTER *pContext, PLIST_ENTRY pListRoot)
{
    while(!IsListEmpty(pListRoot))
    {
        pRxNetDescriptor pBufferDescriptor = (pRxNetDescriptor)RemoveHeadList(pListRoot);
        FreeRxBufferDescriptor(pContext, pBufferDescriptor);
    }
}

static pRxNetDescriptor CreateRxDescriptorOnInit(
    PARANDIS_ADAPTER *pContext)
{
    //For RX packets we allocate following pages
    //  1 page for virtio header and indirect buffers array
    //  X pages needed to fit maximal length buffer of data
    //  The assumption is virtio header and indirect buffers array fit 1 page
    ULONG ulNumPages = pContext->MaxPacketSize.nMaxDataSizeHwRx / PAGE_SIZE + 2;

    pRxNetDescriptor p = (pRxNetDescriptor)ParaNdis_AllocateMemory(pContext, sizeof(*p));
    if(p == NULL) return NULL;

    NdisZeroMemory(p, sizeof(*p));

    p->BufferSGArray = (struct VirtIOBufferDescriptor *)
        ParaNdis_AllocateMemory(pContext, sizeof(*p->BufferSGArray) * ulNumPages);
    if(p->BufferSGArray == NULL) goto error_exit;

    p->PhysicalPages = (tCompletePhysicalAddress *)
        ParaNdis_AllocateMemory(pContext, sizeof(*p->PhysicalPages) * ulNumPages);
    if(p->PhysicalPages == NULL) goto error_exit;

    for(p->PagesAllocated = 0; p->PagesAllocated < ulNumPages; p->PagesAllocated++)
    {
        p->PhysicalPages[p->PagesAllocated].size = PAGE_SIZE;
        if(!ParaNdis_InitialAllocatePhysicalMemory(pContext, &p->PhysicalPages[p->PagesAllocated]))
            goto error_exit;

        p->BufferSGArray[p->PagesAllocated].physAddr = p->PhysicalPages[p->PagesAllocated].Physical;
        p->BufferSGArray[p->PagesAllocated].ulSize = PAGE_SIZE;
    }

    //First page is for virtio header, size needs to be adjusted correspondingly
    p->BufferSGArray[0].ulSize = pContext->nVirtioHeaderSize;

    //Pre-cache indirect area addresses
    p->IndirectArea.Physical.QuadPart = p->PhysicalPages[0].Physical.QuadPart + pContext->nVirtioHeaderSize;
    p->IndirectArea.Virtual = RtlOffsetToPointer(p->PhysicalPages[0].Virtual, pContext->nVirtioHeaderSize);
    p->IndirectArea.size = PAGE_SIZE - pContext->nVirtioHeaderSize;

    if (!ParaNdis_BindRxBufferToPacket(pContext, p))
        goto error_exit;

    return p;

error_exit:
    FreeRxBufferDescriptor(pContext, p);
    return NULL;
}

static BOOLEAN AddRxBufferToQueue(PARANDIS_ADAPTER *pContext, pRxNetDescriptor pBufferDescriptor)
{
    return 0 <= pContext->NetReceiveQueue->vq_ops->add_buf(
        pContext->NetReceiveQueue,
        pBufferDescriptor->BufferSGArray,
        0,
        pBufferDescriptor->PagesAllocated,
        pBufferDescriptor,
        pContext->bUseIndirect ? pBufferDescriptor->IndirectArea.Virtual           : NULL,
        pContext->bUseIndirect ? pBufferDescriptor->IndirectArea.Physical.QuadPart : 0);
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
        pRxNetDescriptor pBuffersDescriptor = CreateRxDescriptorOnInit(pContext);
        if (!pBuffersDescriptor) break;

        if (!AddRxBufferToQueue(pContext, pBuffersDescriptor))
        {
            FreeRxBufferDescriptor(pContext, pBuffersDescriptor);
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
    DeleteQueue(pContext, &pContext->NetControlQueue, &pContext->ControlQueueRing);
    DeleteQueue(pContext, &pContext->NetReceiveQueue, &pContext->ReceiveQueueRing);
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

    pContext->ControlData.size = 512;

    new (&pContext->TXPath, PLACEMENT_NEW) CParaNdisTX(pContext, 1);
    pContext->bTXPathAllocated = TRUE;

    if(!pContext->TXPath.Create())
    {
        return status;
    }
    pContext->bTXPathCreated = TRUE;

    // We work with two virtqueues, 0 - receive and 1 - send.
    VirtIODeviceQueryQueueAllocation(&pContext->IODevice, 0, &size, &pContext->ReceiveQueueRing.size);
    VirtIODeviceQueryQueueAllocation(&pContext->IODevice, 2, &size, &pContext->ControlQueueRing.size);
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

    if (pContext->NetReceiveQueue)
    {
        PrepareReceiveBuffers(pContext);

        if (pContext->NetControlQueue)
            ParaNdis_InitialAllocatePhysicalMemory(pContext, &pContext->ControlData);
        if (!pContext->NetControlQueue || !pContext->ControlData.Virtual)
        {
            DPrintf(0, ("[%s] The Control vQueue does not work!\n", __FUNCTION__) );
            pContext->bHasHardwareFilters = FALSE;
            pContext->bCtrlMACAddrSupported = FALSE;
        }
        if (pContext->NetMaxReceiveBuffers)
        {
            status = NDIS_STATUS_SUCCESS;
        }
    }
    else
    {
        DeleteNetQueues(pContext);
    }
    return status;
}

static void ReadLinkState(PARANDIS_ADAPTER *pContext)
{
    if (pContext->bLinkDetectSupported)
    {
        USHORT linkStatus = 0;
        VirtIODeviceGet(&pContext->IODevice, ETH_LENGTH_OF_ADDRESS, &linkStatus, sizeof(linkStatus));
        pContext->bConnected = !!(linkStatus & VIRTIO_NET_S_LINK_UP);
    }
    else
    {
        pContext->bConnected = TRUE;
    }
}

static BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) ParaNdis_RemoveDriverOKStatus(tSynchronizedContext *SyncContext)
{
    PPARANDIS_ADAPTER pContext = (PPARANDIS_ADAPTER) SyncContext->Parameter;
    VirtIODeviceRemoveStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);

    KeMemoryBarrier();

    pContext->bDeviceInitialized = FALSE;
    return TRUE;
}

static VOID ParaNdis_AddDriverOKStatus(PPARANDIS_ADAPTER pContext)
{
    pContext->bDeviceInitialized = TRUE;

    KeMemoryBarrier();

    VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_DRIVER_OK);
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

    NdisAllocateSpinLock(&pContext->ReceiveLock);

    InitializeListHead(&pContext->NetReceiveBuffers);
    InitializeListHead(&pContext->NetReceiveBuffersWaiting);

    status = ParaNdis_FinishSpecificInitialization(pContext);

    if (status == NDIS_STATUS_SUCCESS)
    {
        status = ParaNdis_VirtIONetInit(pContext);
    }

    pContext->Limits.nReusedRxBuffers = pContext->NetMaxReceiveBuffers / 4 + 1;

    if (status == NDIS_STATUS_SUCCESS)
    {
        ReadLinkState(pContext);
        pContext->bEnableInterruptHandlingDPC = TRUE;
        ParaNdis_SetPowerState(pContext, NdisDeviceStateD0);
        ParaNdis_SynchronizeLinkState(pContext);
        ParaNdis_AddDriverOKStatus(pContext);
        ParaNdis_UpdateMAC(pContext);
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
    ULONG i;
    DEBUG_ENTRY(0);

    /* list NetReceiveBuffersWaiting must be free */

    for (i = 0; i < ARRAYSIZE(pContext->ReceiveQueues); i++)
    {
	pRxNetDescriptor pBufferDescriptor;

	while (NULL != (pBufferDescriptor = ReceiveQueueGetBuffer(pContext->ReceiveQueues + i)))
	    ReuseReceiveBufferPowerOff(pContext, pBufferDescriptor);
    }

    do
    {
        NdisAcquireSpinLock(&pContext->ReceiveLock);
        b = !IsListEmpty(&pContext->NetReceiveBuffersWaiting);
        NdisReleaseSpinLock(&pContext->ReceiveLock);
        if (b)
        {
            DPrintf(0, ("[%s] There are waiting buffers\n", __FUNCTION__));
            PrintStatistics(pContext);
            NdisMSleep(5000000);
        }
    }while (b);

    RestoreMAC(pContext);

    if(pContext->bTXPathCreated)
        pContext->TXPath.Shutdown();
    if(pContext->NetReceiveQueue)
        pContext->NetReceiveQueue->vq_ops->shutdown(pContext->NetReceiveQueue);
    if(pContext->NetControlQueue)
        pContext->NetControlQueue->vq_ops->shutdown(pContext->NetControlQueue);

    DeleteNetQueues(pContext);

    /* this can be freed, queue shut down */
    FreeRxDescriptorsFromList(pContext, &pContext->NetReceiveBuffers);

    if (pContext->ControlData.Virtual)
        ParaNdis_FreePhysicalMemory(pContext, &pContext->ControlData);

    PrintStatistics(pContext);
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
            DPrintf(0, ("[%s] waiting!\n", __FUNCTION__));
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
        if(pContext->bDeviceInitialized) {
            ParaNdis_SynchronizeWithInterrupt(pContext,
                                              pContext->ulRxMessage,
                                              ParaNdis_RemoveDriverOKStatus,
                                              pContext);
        }
    }

    PreventDPCServicing(pContext);

    /****************************************
    ensure all the incoming packets returned,
    free all the buffers and their descriptors
    *****************************************/

    if (pContext->IODevice.addr)
    {
        ParaNdis_ResetVirtIONetDevice(pContext);
    }

    ParaNdis_SetPowerState(pContext, NdisDeviceStateD3);
    ParaNdis_SetLinkState(pContext, MediaConnectStateUnknown);
    VirtIONetRelease(pContext);

    ParaNdis_FinalizeCleanup(pContext);

    if (pContext->ReceiveLock.SpinLock)
    {
        NdisFreeSpinLock(&pContext->ReceiveLock);
    }

    if (pContext->ReceiveQueuesInitialized)
    {
        ULONG i;

        for(i = 0; i < ARRAYSIZE(pContext->ReceiveQueues); i++)
        {
            NdisFreeSpinLock(&pContext->ReceiveQueues[i].Lock);
            if(pContext->ReceiveQueues[i].BatchReceiveArray != &pContext->ReceiveQueues[i].BatchReceiveEmergencyItem)
                NdisFreeMemory(pContext->ReceiveQueues[i].BatchReceiveArray, 0, 0);
        }
    }

#if PARANDIS_SUPPORT_RSS
    if (pContext->bRSSInitialized)
    {
        ParaNdis6_RSSCleanupConfiguration(&pContext->RSSParameters);
    }
#endif

    if (pContext->bTXPathAllocated)
    {
        pContext->TXPath.~CParaNdisTX();
    }

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
       (status == VIRTIO_NET_INVALID_INTERRUPT_STATUS))
    {
        *pRunDpc = FALSE;
        return FALSE;
    }

    PARADNIS_STORE_LAST_INTERRUPT_TIMESTAMP(pContext);

    if(!pContext->bDeviceInitialized) {
        *pRunDpc = FALSE;
        return TRUE;
    }

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
    /* If interrupts for this queue (or globally) disabled do nothing */
    if( !ParaNdis_IsInterruptSourceEnabled(pContext, knownInterruptSources) || !pContext->bDeviceInitialized )
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

    return TRUE;
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
void ReuseReceiveBufferRegular(PARANDIS_ADAPTER *pContext, pRxNetDescriptor pBuffersDescriptor)
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
            DPrintf(0, (" Error: NetNofReceiveBuffers > NetMaxReceiveBuffers(%d>%d)\n",
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
        DPrintf(0, ("FAILED TO REUSE THE BUFFER!!!!\n"));
        FreeRxBufferDescriptor(pContext, pBuffersDescriptor);
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
static void ReuseReceiveBufferPowerOff(PARANDIS_ADAPTER *pContext, pRxNetDescriptor pBuffersDescriptor)
{
    RemoveEntryList(&pBuffersDescriptor->listEntry);
    InsertTailList(&pContext->NetReceiveBuffers, &pBuffersDescriptor->listEntry);
}

static ULONG ShallPassPacket(PARANDIS_ADAPTER *pContext, PNET_PACKET_INFO pPacketInfo)
{
    ULONG i;

    if (pPacketInfo->dataLength > pContext->MaxPacketSize.nMaxFullSizeOsRx + ETH_PRIORITY_HEADER_SIZE)
        return FALSE;

    if ((pPacketInfo->dataLength > pContext->MaxPacketSize.nMaxFullSizeOsRx) && !pPacketInfo->hasVlanHeader)
        return FALSE;

    if (IsVlanSupported(pContext) && pPacketInfo->hasVlanHeader)
    {
        if (pContext->VlanId && pContext->VlanId != pPacketInfo->Vlan.VlanId)
        {
            return FALSE;
        }
    }

    if (pContext->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
        return TRUE;

    if(pPacketInfo->isUnicast)
    {
        ULONG Res;

        if(!(pContext->PacketFilter & NDIS_PACKET_TYPE_DIRECTED))
            return FALSE;

        ETH_COMPARE_NETWORK_ADDRESSES_EQ(pPacketInfo->ethDestAddr, pContext->CurrentMacAddress, &Res);
        return !Res;
    }

    if(pPacketInfo->isBroadcast)
        return (pContext->PacketFilter & NDIS_PACKET_TYPE_BROADCAST);

    // Multi-cast

    if(pContext->PacketFilter & NDIS_PACKET_TYPE_ALL_MULTICAST)
        return TRUE;

    if(!(pContext->PacketFilter & NDIS_PACKET_TYPE_MULTICAST))
        return FALSE;

    for (i = 0; i < pContext->MulticastData.nofMulticastEntries; i++)
    {
        ULONG Res;
        PUCHAR CurrMcastAddr = &pContext->MulticastData.MulticastList[i*ETH_LENGTH_OF_ADDRESS];

        ETH_COMPARE_NETWORK_ADDRESSES_EQ(pPacketInfo->ethDestAddr, CurrMcastAddr, &Res);

        if(!Res)
            return TRUE;
    }

    return FALSE;
}

static __inline
BOOLEAN PerformPacketAnalyzis(
#if PARANDIS_SUPPORT_RSS
                            PPARANDIS_RSS_PARAMS RSSParameters,
#endif
                            PNET_PACKET_INFO PacketInfo,
                            PVOID HeadersBuffer,
                            ULONG DataLength)
{
    if(!ParaNdis_AnalyzeReceivedPacket(HeadersBuffer, DataLength, PacketInfo))
        return FALSE;

#if PARANDIS_SUPPORT_RSS
    if(RSSParameters->RSSMode != PARANDIS_RSS_DISABLED)
    {
        ParaNdis6_RSSAnalyzeReceivedPacket(RSSParameters, HeadersBuffer, PacketInfo);
    }
#endif
    return TRUE;
}

static BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) RestartQueueSynchronously(tSynchronizedContext *SyncContext)
{
    struct virtqueue * _vq = (struct virtqueue *) SyncContext->Parameter;
    bool res = _vq->vq_ops->restart(_vq);

    ParaNdis_DebugHistory(SyncContext->pContext, hopDPC, (PVOID)SyncContext->Parameter, 0x20, res, 0);
    return !res;
}

static __inline
VOID ProcessorNumberToGroupAffinity(PGROUP_AFFINITY Affinity, const PPROCESSOR_NUMBER Processor)
{
    Affinity->Group = Processor->Group;
    Affinity->Mask = 1;
    Affinity->Mask <<= Processor->Number;
}

static __inline
CCHAR GetScalingDataForPacket(PARANDIS_ADAPTER *pContext, PNET_PACKET_INFO pPacketInfo, PPROCESSOR_NUMBER pTargetProcessor)
{
#if PARANDIS_SUPPORT_RSS
    return ParaNdis6_RSSGetScalingDataForPacket(&pContext->RSSParameters, pPacketInfo, pTargetProcessor);
#else
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(pPacketInfo);
    UNREFERENCED_PARAMETER(pTargetProcessor);

    return PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED;
#endif
}

static __inline
CCHAR GetReceiveQueueForCurrentCPU(PARANDIS_ADAPTER *pContext)
{
#if PARANDIS_SUPPORT_RSS
    return ParaNdis6_RSSGetCurrentCpuReceiveQueue(&pContext->RSSParameters);
#else
    UNREFERENCED_PARAMETER(pContext);

    return PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED;
#endif
}

static __inline
VOID QueueRSSDpc(PARANDIS_ADAPTER *pContext, PGROUP_AFFINITY pTargetAffinity)
{
#if PARANDIS_SUPPORT_RSS
    NdisMQueueDpcEx(pContext->InterruptHandle, pContext->ulRxMessage, pTargetAffinity, NULL);
#else
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(pTargetAffinity);

    ASSERT(FALSE);
#endif
}

static __inline
VOID ReceiveQueueAddBuffer(PPARANDIS_RECEIVE_QUEUE pQueue, pRxNetDescriptor pBuffer)
{
    NdisInterlockedInsertTailList(  &pQueue->BuffersList,
                                    &pBuffer->ReceiveQueueListEntry,
                                    &pQueue->Lock);
}

static __inline
pRxNetDescriptor ReceiveQueueGetBuffer(PPARANDIS_RECEIVE_QUEUE pQueue)
{
    PLIST_ENTRY pListEntry = NdisInterlockedRemoveHeadList(&pQueue->BuffersList, &pQueue->Lock);
    return pListEntry ? CONTAINING_RECORD(pListEntry, RxNetDescriptor, ReceiveQueueListEntry) : NULL;
}

static __inline
BOOLEAN ReceiveQueueHasBuffers(PPARANDIS_RECEIVE_QUEUE pQueue)
{
    BOOLEAN res;

    NdisAcquireSpinLock(&pQueue->Lock);
    res = !IsListEmpty(&pQueue->BuffersList);
    NdisReleaseSpinLock(&pQueue->Lock);

    return res;
}

static VOID ProcessRxRing(PARANDIS_ADAPTER *pContext, CCHAR nCurrCpuReceiveQueue)
{
    pRxNetDescriptor pBufferDescriptor;
    unsigned int nFullLength;

    NdisAcquireSpinLock(&pContext->ReceiveLock);

    while (NULL != (pBufferDescriptor = (pRxNetDescriptor) pContext->NetReceiveQueue->vq_ops->get_buf(pContext->NetReceiveQueue, &nFullLength)))
    {
        CCHAR nTargetReceiveQueueNum;
        GROUP_AFFINITY TargetAffinity;
        PROCESSOR_NUMBER TargetProcessor;

        RemoveEntryList(&pBufferDescriptor->listEntry);
        InsertTailList(&pContext->NetReceiveBuffersWaiting, &pBufferDescriptor->listEntry);
        pContext->NetNofReceiveBuffers--;

        if(!PerformPacketAnalyzis(
#if PARANDIS_SUPPORT_RSS
                                &pContext->RSSParameters,
#endif
                                &pBufferDescriptor->PacketInfo,
                                pBufferDescriptor->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE].Virtual,
                                nFullLength - pContext->nVirtioHeaderSize))
        {
            pContext->ReuseBufferProc(pContext, pBufferDescriptor);
            pContext->Statistics.ifInErrors++;
            pContext->Statistics.ifInDiscards++;
            continue;
        }

        nTargetReceiveQueueNum = GetScalingDataForPacket(
                                                            pContext,
                                                            &pBufferDescriptor->PacketInfo,
                                                            &TargetProcessor);

        ReceiveQueueAddBuffer(&pContext->ReceiveQueues[nTargetReceiveQueueNum], pBufferDescriptor);
        ProcessorNumberToGroupAffinity(&TargetAffinity, &TargetProcessor);

        if ((nTargetReceiveQueueNum != PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED) &&
            (nTargetReceiveQueueNum != nCurrCpuReceiveQueue))
            QueueRSSDpc(pContext, &TargetAffinity);
    }

    NdisReleaseSpinLock(&pContext->ReceiveLock);
}

static VOID
UpdateReceiveSuccessStatistics(PPARANDIS_ADAPTER pContext,
                               PNET_PACKET_INFO pPacketInfo,
                               UINT nCoalescedSegmentsCount)
{
    pContext->Statistics.ifHCInOctets += pPacketInfo->dataLength;

    if(pPacketInfo->isUnicast)
    {
        pContext->Statistics.ifHCInUcastPkts += nCoalescedSegmentsCount;
        pContext->Statistics.ifHCInUcastOctets += pPacketInfo->dataLength;
    }
    else if (pPacketInfo->isBroadcast)
    {
        pContext->Statistics.ifHCInBroadcastPkts += nCoalescedSegmentsCount;
        pContext->Statistics.ifHCInBroadcastOctets += pPacketInfo->dataLength;
    }
    else if (pPacketInfo->isMulticast)
    {
        pContext->Statistics.ifHCInMulticastPkts += nCoalescedSegmentsCount;
        pContext->Statistics.ifHCInMulticastOctets += pPacketInfo->dataLength;
    }
    else
    {
        ASSERT(FALSE);
    }
}

static __inline VOID
UpdateReceiveFailStatistics(PPARANDIS_ADAPTER pContext, UINT nCoalescedSegmentsCount)
{
    pContext->Statistics.ifInErrors++;
    pContext->Statistics.ifInDiscards += nCoalescedSegmentsCount;
}

static BOOLEAN ProcessReceiveQueue(
                                    PARANDIS_ADAPTER *pContext,
                                    PULONG pnPacketsToIndicateLeft,
                                    CCHAR nQueueIndex)
{
    UINT nReceived = 0;
    pRxNetDescriptor pBufferDescriptor;
    PPARANDIS_RECEIVE_QUEUE pTargetReceiveQueue = &pContext->ReceiveQueues[nQueueIndex];

    if(NdisInterlockedIncrement(&pTargetReceiveQueue->ActiveProcessorsCount) == 1)
    {
        while( (*pnPacketsToIndicateLeft > 0) &&
               (NULL != (pBufferDescriptor = ReceiveQueueGetBuffer(pTargetReceiveQueue))) )
        {
            PNET_PACKET_INFO pPacketInfo = &pBufferDescriptor->PacketInfo;

            if( !pContext->bSurprizeRemoved &&
                pContext->ReceiveState == srsEnabled &&
                pContext->bConnected &&
                ShallPassPacket(pContext, pPacketInfo))
            {
                UINT nCoalescedSegmentsCount;
                tPacketIndicationType packet = ParaNdis_PrepareReceivedPacket(pContext, pBufferDescriptor, &nCoalescedSegmentsCount);
                if(packet != NULL)
                {
                    UpdateReceiveSuccessStatistics(pContext, pPacketInfo, nCoalescedSegmentsCount);
                    pTargetReceiveQueue->BatchReceiveArray[nReceived] = packet;

                    nReceived++;
                    (*pnPacketsToIndicateLeft)--;

                    if (nReceived == pTargetReceiveQueue->BatchReceiveArraySize)
                    {
                        ParaNdis_IndicateReceivedBatch(pContext, pTargetReceiveQueue->BatchReceiveArray, nReceived);
                        nReceived = 0;
                    }
                }
                else
                {
                    UpdateReceiveFailStatistics(pContext, nCoalescedSegmentsCount);
                    NdisAcquireSpinLock(&pContext->ReceiveLock);
                    pContext->ReuseBufferProc(pContext, pBufferDescriptor);
                    NdisReleaseSpinLock(&pContext->ReceiveLock);
                }
            }
            else
            {
                pContext->extraStatistics.framesFilteredOut++;
                NdisAcquireSpinLock(&pContext->ReceiveLock);
                pContext->ReuseBufferProc(pContext, pBufferDescriptor);
                NdisReleaseSpinLock(&pContext->ReceiveLock);
            }
        }

        if(nReceived > 0)
        {
            ParaNdis_IndicateReceivedBatch(pContext, pTargetReceiveQueue->BatchReceiveArray, nReceived);
        }
    }

    NdisInterlockedDecrement(&pTargetReceiveQueue->ActiveProcessorsCount);
    return ReceiveQueueHasBuffers(pTargetReceiveQueue);
}

static
BOOLEAN RxDPCWorkBody(PARANDIS_ADAPTER *pContext, ULONG nPacketsToIndicate)
{
    BOOLEAN res = FALSE;
    BOOLEAN bMoreDataInRing;

    CCHAR CurrCpuReceiveQueue = GetReceiveQueueForCurrentCPU(pContext);

    do
    {
        ProcessRxRing(pContext, CurrCpuReceiveQueue);

        res |= ProcessReceiveQueue(pContext, &nPacketsToIndicate, PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED);

        if(CurrCpuReceiveQueue != PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED)
        {
            res |= ProcessReceiveQueue(pContext, &nPacketsToIndicate, CurrCpuReceiveQueue);
        }

        bMoreDataInRing = ParaNdis_SynchronizeWithInterrupt(pContext,
                                                            pContext->ulRxMessage,
                                                            RestartQueueSynchronously,
                                                            pContext->NetReceiveQueue);
    } while(bMoreDataInRing);

    return res;
}

ULONG ParaNdis_DPCWorkBody(PARANDIS_ADAPTER *pContext, ULONG ulMaxPacketsToIndicate)
{
    ULONG stillRequiresProcessing = 0;
    ULONG interruptSources;
    UINT numOfPacketsToIndicate = min(ulMaxPacketsToIndicate, pContext->uNumberOfHandledRXPacketsInDPC);

    DEBUG_ENTRY(5);
    if (pContext->bEnableInterruptHandlingDPC)
    {
        bool bDoKick = false;

        InterlockedIncrement(&pContext->counterDPCInside);
        InterlockedExchange(&pContext->bDPCInactive, 0);
        interruptSources = InterlockedExchange(&pContext->InterruptStatus, 0);

        if (RxDPCWorkBody(  pContext,
                            numOfPacketsToIndicate))
        {
            stillRequiresProcessing |= isReceive;
        }

        ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)1, interruptSources, 0, 0);
        if ((interruptSources & isControl) && pContext->bLinkDetectSupported)
        {
            ReadLinkState(pContext);
            ParaNdis_SynchronizeLinkState(pContext);
        }
        if (interruptSources & isTransmit)
        {
            bDoKick = pContext->TXPath.DoPendingTasks(true);
            if(pContext->TXPath.RestartQueue(bDoKick))
            {
                stillRequiresProcessing |= isTransmit;
            }
        }

        InterlockedDecrement(&pContext->counterDPCInside);
    }
    return stillRequiresProcessing;
}

VOID ParaNdis_ResetRxClassification(PARANDIS_ADAPTER *pContext)
{
    ULONG i;
    PPARANDIS_RECEIVE_QUEUE pUnclassified = &pContext->ReceiveQueues[PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED];

    NdisAcquireSpinLock(&pUnclassified->Lock);

    for(i = PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED + 1; i < ARRAYSIZE(pContext->ReceiveQueues); i++)
    {
        PPARANDIS_RECEIVE_QUEUE pCurrQueue = &pContext->ReceiveQueues[i];
        NdisAcquireSpinLock(&pCurrQueue->Lock);

        while(!IsListEmpty(&pCurrQueue->BuffersList))
        {
            PLIST_ENTRY pListEntry = RemoveHeadList(&pCurrQueue->BuffersList);
            InsertTailList(&pUnclassified->BuffersList, pListEntry);
        }

        NdisReleaseSpinLock(&pCurrQueue->Lock);
    }

    NdisReleaseSpinLock(&pUnclassified->Lock);
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
    }
    else
    {
        pContext->nDetectedInactivity = 0;
    }

    if (pContext->TXPath.HasHWBuffersIsUse())
    {
        if (pContext->nDetectedStoppedTx++ > 1)
        {
            DPrintf(0, ("[%s] - Suspicious Tx inactivity (%d)!\n", __FUNCTION__, pContext->TXPath.GetFreeHWBuffers()));
            //bReportHang = TRUE;
#ifdef DBG_USE_VIRTIO_PCI_ISR_FOR_HOST_REPORT
            WriteVirtIODeviceByte(pContext->IODevice.addr + VIRTIO_PCI_ISR, 0);
#endif
        }
    }

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

    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, ulValue) );
    return ulValue;
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, ulValue) );

    NdisRawWritePortUlong(ulRegister, ulValue);
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    u8 bValue;

    NdisRawReadPortUchar(ulRegister, &bValue);

    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, bValue) );

    return bValue;
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, bValue) );

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
        DPrintf(1, ("[%s] New multicast list of %d bytes\n", __FUNCTION__, length));
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
        pContext->TXPath.EnableInterrupts();
    if (interruptSource & isReceive)
        pContext->NetReceiveQueue->vq_ops->enable_interrupt(pContext->NetReceiveQueue);
    ParaNdis_DebugHistory(pContext, hopDPC, (PVOID)0x10, interruptSource, TRUE, 0);
}

VOID ParaNdis_VirtIODisableIrqSynchronized(PARANDIS_ADAPTER *pContext, ULONG interruptSource)
{
    if (interruptSource & isTransmit)
        pContext->TXPath.DisableInterrupts();
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

    UNREFERENCED_PARAMETER(pInfo);
    UNREFERENCED_PARAMETER(ulSize);

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
    DPrintf(0, ("[%s] (%s)\n", __FUNCTION__, pName));
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
                DPrintf(0, ("%s - ERROR: get_buf failed\n", __FUNCTION__));
            }
            else if (len != sizeof(virtio_net_ctrl_ack))
            {
                DPrintf(0, ("%s - ERROR: wrong len %d\n", __FUNCTION__, len));
            }
            else if (*(virtio_net_ctrl_ack *)(pBase + offset) != VIRTIO_NET_OK)
            {
                DPrintf(0, ("%s - ERROR: error %d returned for class %d\n", __FUNCTION__, *(virtio_net_ctrl_ack *)(pBase + offset), cls));
            }
            else
            {
                // everything is OK
                DPrintf(levelIfOK, ("%s OK(%d.%d,buffers of %d and %d) \n", __FUNCTION__, cls, cmd, size1, size2));
                bOK = TRUE;
            }
        }
        else
        {
            DPrintf(0, ("%s - ERROR: add_buf failed\n", __FUNCTION__));
        }
    }
    else
    {
        DPrintf(0, ("%s (buffer %d,%d) - ERROR: message too LARGE\n", __FUNCTION__, size1, size2));
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
    u32 u32UniCastEntries = 0;
    SendControlMessage(pContext, VIRTIO_NET_CTRL_MAC, VIRTIO_NET_CTRL_MAC_TABLE_SET,
                        &u32UniCastEntries,
                        sizeof(u32UniCastEntries),
                        &pContext->MulticastData,
                        sizeof(pContext->MulticastData.nofMulticastEntries) + pContext->MulticastData.nofMulticastEntries * ETH_LENGTH_OF_ADDRESS,
                        2);
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

static VOID
ParaNdis_UpdateMAC(PARANDIS_ADAPTER *pContext)
{
    if (pContext->bCtrlMACAddrSupported)
    {
        SendControlMessage(pContext, VIRTIO_NET_CTRL_MAC, VIRTIO_NET_CTRL_MAC_ADDR_SET,
                           pContext->CurrentMacAddress,
                           ETH_LENGTH_OF_ADDRESS,
                           NULL, 0, 4);
    }
}

#if PARANDIS_SUPPORT_RSC
VOID
ParaNdis_UpdateGuestOffloads(PARANDIS_ADAPTER *pContext, UINT64 Offloads)
{
    if (pContext->RSC.bHasDynamicConfig)
    {
        SendControlMessage(pContext, VIRTIO_NET_CTRL_GUEST_OFFLOADS, VIRTIO_NET_CTRL_GUEST_OFFLOADS_SET,
                           &Offloads,
                           sizeof(Offloads),
                           NULL, 0, 2);
    }
}
#endif

VOID ParaNdis_PowerOn(PARANDIS_ADAPTER *pContext)
{
    LIST_ENTRY TempList;
    DEBUG_ENTRY(0);
    ParaNdis_DebugHistory(pContext, hopPowerOn, NULL, 1, 0, 0);
    ParaNdis_ResetVirtIONetDevice(pContext);
    VirtIODeviceAddStatus(&pContext->IODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);
    /* GetHostFeature must be called with any mask once upon device initialization:
     otherwise the device will not work properly */
    VirtIODeviceReadHostFeatures(&pContext->IODevice);
    VirtIODeviceWriteGuestFeatures(&pContext->IODevice, pContext->u32GuestFeatures);

    VirtIODeviceRenewQueue(pContext->NetReceiveQueue);
    pContext->TXPath.Renew();

    if(pContext->NetControlQueue)
        VirtIODeviceRenewQueue(pContext->NetControlQueue);

    ParaNdis_RestoreDeviceConfigurationAfterReset(pContext);

    ParaNdis_UpdateDeviceFilters(pContext);
    ParaNdis_UpdateMAC(pContext);

    InitializeListHead(&TempList);
    
    /* submit all the receive buffers */
    NdisAcquireSpinLock(&pContext->ReceiveLock);
    
    pContext->ReuseBufferProc = ReuseReceiveBufferRegular;
    
    while (!IsListEmpty(&pContext->NetReceiveBuffers))
    {
        pRxNetDescriptor pBufferDescriptor =
            (pRxNetDescriptor)RemoveHeadList(&pContext->NetReceiveBuffers);
        InsertTailList(&TempList, &pBufferDescriptor->listEntry);
    }
    pContext->NetNofReceiveBuffers = 0;
    while (!IsListEmpty(&TempList))
    {
        pRxNetDescriptor pBufferDescriptor =
            (pRxNetDescriptor)RemoveHeadList(&TempList);
        if (AddRxBufferToQueue(pContext, pBufferDescriptor))
        {
            InsertTailList(&pContext->NetReceiveBuffers, &pBufferDescriptor->listEntry);
            pContext->NetNofReceiveBuffers++;
        }
        else
        {
            DPrintf(0, ("FAILED TO REUSE THE BUFFER!!!!\n"));
            FreeRxBufferDescriptor(pContext, pBufferDescriptor);
            pContext->NetMaxReceiveBuffers--;
        }
    }
    pContext->NetReceiveQueue->vq_ops->kick(pContext->NetReceiveQueue);
    ReadLinkState(pContext);
    ParaNdis_SetPowerState(pContext, NdisDeviceStateD0);
    ParaNdis_SynchronizeLinkState(pContext);
    pContext->bEnableInterruptHandlingDPC = TRUE;
    ParaNdis_AddDriverOKStatus(pContext);

    NdisReleaseSpinLock(&pContext->ReceiveLock);

    // if bFastSuspendInProcess is set by Win8 power-off procedure,
    // the ParaNdis_Resume enables Tx and RX
    // otherwise it does not do anything in Vista+ (Tx and RX are enabled after power-on by Restart)
    ParaNdis_Resume(pContext);
    pContext->bFastSuspendInProcess = FALSE;

    ParaNdis_DebugHistory(pContext, hopPowerOn, NULL, 0, 0, 0);
}

VOID ParaNdis_PowerOff(PARANDIS_ADAPTER *pContext)
{
    DEBUG_ENTRY(0);
    ParaNdis_DebugHistory(pContext, hopPowerOff, NULL, 1, 0, 0);

    pContext->bConnected = FALSE;

    // if bFastSuspendInProcess is set by Win8 power-off procedure
    // the ParaNdis_Suspend does fast Rx stop without waiting (=>srsPausing, if there are some RX packets in Ndis)
    pContext->bFastSuspendInProcess = pContext->bNoPauseOnSuspend && pContext->ReceiveState == srsEnabled;
    ParaNdis_Suspend(pContext);

    ParaNdis_SynchronizeWithInterrupt(pContext,
        pContext->ulRxMessage,
        ParaNdis_RemoveDriverOKStatus,
        pContext);
    
    if (pContext->bFastSuspendInProcess)
    {
        NdisAcquireSpinLock(&pContext->ReceiveLock);
        pContext->ReuseBufferProc = ReuseReceiveBufferPowerOff;
        NdisReleaseSpinLock(&pContext->ReceiveLock);
    }
    
#if !NDIS_SUPPORT_NDIS620
    // WLK tests for Windows 2008 require media disconnect indication
    // on power off. HCK tests for newer versions require media state unknown
    // indication only and fail on disconnect indication
    ParaNdis_SetLinkState(pContext, MediaConnectStateDisconnected);
#endif
    ParaNdis_SetPowerState(pContext, NdisDeviceStateD3);
    ParaNdis_SetLinkState(pContext, MediaConnectStateUnknown);

    PreventDPCServicing(pContext);

    /*******************************************************************
        shutdown queues to have all the receive buffers under our control
        all the transmit buffers move to list of free buffers
    ********************************************************************/

    pContext->TXPath.Shutdown();

    NdisAcquireSpinLock(&pContext->ReceiveLock);
    pContext->NetReceiveQueue->vq_ops->shutdown(pContext->NetReceiveQueue);
    NdisReleaseSpinLock(&pContext->ReceiveLock);

    if(pContext->NetControlQueue)
        pContext->NetControlQueue->vq_ops->shutdown(pContext->NetControlQueue);

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

tChecksumCheckResult ParaNdis_CheckRxChecksum(
                                            PARANDIS_ADAPTER *pContext,
                                            ULONG virtioFlags,
                                            tCompletePhysicalAddress *pPacketPages,
                                            ULONG ulPacketLength,
                                            ULONG ulDataOffset)
{
    tOffloadSettingsFlags f = pContext->Offload.flags;
    tChecksumCheckResult res, resIp;
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

    ppr = ParaNdis_CheckSumVerify(pPacketPages, ulPacketLength - ETH_HEADER_SIZE, ulDataOffset + ETH_HEADER_SIZE, flagsToCalculate, __FUNCTION__);

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

    return res;
}
