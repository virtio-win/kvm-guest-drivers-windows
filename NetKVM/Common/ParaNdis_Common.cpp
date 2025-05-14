/*
 * This file contains NDIS driver procedures, common for NDIS5 and NDIS6
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
#include "ndis56common.h"
#include "virtio_net.h"
#include "virtio_ring.h"
#include "kdebugprint.h"
#include "ParaNdis_DebugHistory.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_Common.tmh"
#endif

static VOID ParaNdis_UpdateMAC(PARANDIS_ADAPTER *pContext);

static __inline pRxNetDescriptor ReceiveQueueGetBuffer(PPARANDIS_RECEIVE_QUEUE pQueue);

#define MAX_VLAN_ID 4094

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
static BOOLEAN ParaNdis_ValidateMacAddress(PUCHAR pcMacAddress, BOOLEAN bLocal)
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
    const char *Name;
    ULONG ulValue;
    ULONG ulMinimal;
    ULONG ulMaximal;
} tConfigurationEntry;

typedef struct _tagConfigurationEntries
{
    tConfigurationEntry PhysicalMediaType;
    tConfigurationEntry PrioritySupport;
    tConfigurationEntry isLogEnabled;
    tConfigurationEntry debugLevel;
    tConfigurationEntry TxCapacity;
    tConfigurationEntry RxCapacity;
    tConfigurationEntry RxSeparateTail;
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
    tConfigurationEntry JumboPacket;
    tConfigurationEntry NumberOfHandledRXPacketsInDPC;
    tConfigurationEntry FastInit;
#if PARANDIS_SUPPORT_RSS
    tConfigurationEntry RSSOffloadSupported;
    tConfigurationEntry NumRSSQueues;
#endif
#if PARANDIS_SUPPORT_RSC
    tConfigurationEntry RSCIPv4Supported;
    tConfigurationEntry RSCIPv6Supported;
#endif
#if PARANDIS_SUPPORT_USO
    tConfigurationEntry USOv4Supported;
    tConfigurationEntry USOv6Supported;
#endif
    tConfigurationEntry MinRxBufferPercent;
    tConfigurationEntry PollMode;
} tConfigurationEntries;

// clang-format off
static const tConfigurationEntries defaultConfiguration =
{
    { "*PhysicalMediaType", 0,  0,  0xff },
    { "Priority",       0,  0,  1 },
    { "DoLog",          1,  0,  1 },
    { "DebugLevel",     2,  0,  8 },
    { "TxCapacity",     1024,   16, 1024 },
    { "RxCapacity",     256, 16, 4096 },
    { "SeparateTail",   1,  0,  1 },
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
    { "*JumboPacket", 1514, 590, 65500},
    { "NumberOfHandledRXPacketsInDPC", MAX_RX_LOOPS, 1, 10000},
    { "FastInit", 1, 0, 1},
#if PARANDIS_SUPPORT_RSS
    { "*RSS", 1, 0, 1},
    { "*NumRssQueues", 16, 1, PARANDIS_RSS_MAX_RECEIVE_QUEUES},
#endif
#if PARANDIS_SUPPORT_RSC
    { "*RscIPv4", 1, 0, 1},
    { "*RscIPv6", 1, 0, 1},
#endif
#if PARANDIS_SUPPORT_USO
    { "*UsoIPv4", 1, 0, 1},
    { "*UsoIPv6", 1, 0, 1},
#endif
    { "MinRxBufferPercent", PARANDIS_MIN_RX_BUFFER_PERCENT_DEFAULT, 0, 100},
    { "*NdisPoll", 0, 0, 1},
};

static void ParaNdis_ResetVirtIONetDevice(PARANDIS_ADAPTER *pContext)
{
    virtio_device_reset(&pContext->IODevice);
    DPrintf(0, "Done");

    KeMemoryBarrier();

    pContext->bDeviceInitialized = FALSE;

    /* reset all the features in the device */
    pContext->ulCurrentVlansFilterSet = 0;
}
// clang-format on

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
    NdisReadConfiguration(&status, &pParam, cfg, &name, ParameterType);
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
        DPrintf(0,
                "%s read for %s - current value = 0x%x, input value = 0x%x",
                statusName,
                pEntry->Name,
                pEntry->ulValue,
                ulValue);
    }
    else
    {
        DPrintf(0, "nothing read for %s - current value = 0x%x", pEntry->Name, pEntry->ulValue);
    }
    if (name.Buffer)
    {
        NdisFreeString(name);
    }
}

static void DisableLSOv4Permanently(PARANDIS_ADAPTER *pContext, LPCSTR procname, LPCSTR reason)
{
    if (pContext->Offload.flagsValue & osbT4Lso)
    {
        DPrintf(0, "[%s] Warning: %s", procname, reason);
        pContext->Offload.flagsValue &= ~osbT4Lso;
        ParaNdis_ResetOffloadSettings(pContext, NULL, NULL);
    }
}

static void DisableLSOv6Permanently(PARANDIS_ADAPTER *pContext, LPCSTR procname, LPCSTR reason)
{
    if (pContext->Offload.flagsValue & osbT6Lso)
    {
        DPrintf(0, "[%s] Warning: %s", procname, reason);
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
static bool ReadNicConfiguration(PARANDIS_ADAPTER *pContext, PUCHAR pNewMACAddress)
{
    NDIS_HANDLE cfg;
    bool ret = false;
    tConfigurationEntries *pConfiguration = (tConfigurationEntries *)ParaNdis_AllocateMemory(pContext,
                                                                                             sizeof(tConfigurationEntries));
    if (pConfiguration)
    {
        *pConfiguration = defaultConfiguration;
        cfg = ParaNdis_OpenNICConfiguration(pContext);
        if (cfg)
        {
            ret = true;
            GetConfigurationEntry(cfg, &pConfiguration->PhysicalMediaType);
            GetConfigurationEntry(cfg, &pConfiguration->isLogEnabled);
            GetConfigurationEntry(cfg, &pConfiguration->debugLevel);
            GetConfigurationEntry(cfg, &pConfiguration->PrioritySupport);
            GetConfigurationEntry(cfg, &pConfiguration->TxCapacity);
            GetConfigurationEntry(cfg, &pConfiguration->RxCapacity);
            GetConfigurationEntry(cfg, &pConfiguration->RxSeparateTail);
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
            GetConfigurationEntry(cfg, &pConfiguration->JumboPacket);
            GetConfigurationEntry(cfg, &pConfiguration->NumberOfHandledRXPacketsInDPC);
            GetConfigurationEntry(cfg, &pConfiguration->FastInit);
#if PARANDIS_SUPPORT_RSS
            GetConfigurationEntry(cfg, &pConfiguration->RSSOffloadSupported);
            GetConfigurationEntry(cfg, &pConfiguration->NumRSSQueues);
#endif
#if PARANDIS_SUPPORT_RSC
            GetConfigurationEntry(cfg, &pConfiguration->RSCIPv4Supported);
            GetConfigurationEntry(cfg, &pConfiguration->RSCIPv6Supported);
#endif
#if PARANDIS_SUPPORT_USO
            GetConfigurationEntry(cfg, &pConfiguration->USOv4Supported);
            GetConfigurationEntry(cfg, &pConfiguration->USOv6Supported);
#endif
            GetConfigurationEntry(cfg, &pConfiguration->MinRxBufferPercent);
            GetConfigurationEntry(cfg, &pConfiguration->PollMode);

            bDebugPrint = pConfiguration->isLogEnabled.ulValue;
            virtioDebugLevel = pConfiguration->debugLevel.ulValue;
            pContext->bFastInit = pConfiguration->FastInit.ulValue != 0;
            pContext->physicalMediaType = (NDIS_PHYSICAL_MEDIUM)pConfiguration->PhysicalMediaType.ulValue;
            pContext->maxFreeTxDescriptors = pConfiguration->TxCapacity.ulValue;
            pContext->maxRxBufferPerQueue = pConfiguration->RxCapacity.ulValue;
            pContext->uNumberOfHandledRXPacketsInDPC = pConfiguration->NumberOfHandledRXPacketsInDPC.ulValue;
            pContext->bDoSupportPriority = pConfiguration->PrioritySupport.ulValue != 0;
            pContext->Offload.flagsValue = 0;
            pContext->MinRxBufferPercent = pConfiguration->MinRxBufferPercent.ulValue;
            pContext->bRxSeparateTail = pConfiguration->RxSeparateTail.ulValue != 0;
            // TX caps: 1 - TCP, 2 - UDP, 4 - IP, 8 - TCPv6, 16 - UDPv6
            if (pConfiguration->OffloadTxChecksum.ulValue & 1)
            {
                pContext->Offload.flagsValue |= osbT4TcpChecksum | osbT4TcpOptionsChecksum;
            }
            if (pConfiguration->OffloadTxChecksum.ulValue & 2)
            {
                pContext->Offload.flagsValue |= osbT4UdpChecksum;
            }
            if (pConfiguration->OffloadTxChecksum.ulValue & 4)
            {
                pContext->Offload.flagsValue |= osbT4IpChecksum | osbT4IpOptionsChecksum;
            }
            if (pConfiguration->OffloadTxChecksum.ulValue & 8)
            {
                pContext->Offload.flagsValue |= osbT6TcpChecksum | osbT6TcpOptionsChecksum;
            }
            if (pConfiguration->OffloadTxChecksum.ulValue & 16)
            {
                pContext->Offload.flagsValue |= osbT6UdpChecksum;
            }
            if (pConfiguration->OffloadTxLSO.ulValue)
            {
                pContext->Offload.flagsValue |= osbT4Lso | osbT4LsoIp | osbT4LsoTcp;
            }
            if (pConfiguration->OffloadTxLSO.ulValue > 1)
            {
                pContext->Offload.flagsValue |= osbT6Lso | osbT6LsoTcpOptions;
            }
            // RX caps: 1 - TCP, 2 - UDP, 4 - IP, 8 - TCPv6, 16 - UDPv6
            if (pConfiguration->OffloadRxCS.ulValue & 1)
            {
                pContext->Offload.flagsValue |= osbT4RxTCPChecksum | osbT4RxTCPOptionsChecksum;
            }
            if (pConfiguration->OffloadRxCS.ulValue & 2)
            {
                pContext->Offload.flagsValue |= osbT4RxUDPChecksum;
            }
            if (pConfiguration->OffloadRxCS.ulValue & 4)
            {
                pContext->Offload.flagsValue |= osbT4RxIPChecksum | osbT4RxIPOptionsChecksum;
            }
            if (pConfiguration->OffloadRxCS.ulValue & 8)
            {
                pContext->Offload.flagsValue |= osbT6RxTCPChecksum | osbT6RxTCPOptionsChecksum;
            }
            if (pConfiguration->OffloadRxCS.ulValue & 16)
            {
                pContext->Offload.flagsValue |= osbT6RxUDPChecksum;
            }
            /* InitialOffloadParameters is used only internally */
            pContext->InitialOffloadParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            pContext->InitialOffloadParameters.Header.Revision = NDIS_OFFLOAD_PARAMETERS_REVISION_1;
            pContext->InitialOffloadParameters.Header.Size = NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_1;
            pContext->InitialOffloadParameters.IPv4Checksum = (UCHAR)pConfiguration->stdIpcsV4.ulValue;
            pContext->InitialOffloadParameters.TCPIPv4Checksum = (UCHAR)pConfiguration->stdTcpcsV4.ulValue;
            pContext->InitialOffloadParameters.TCPIPv6Checksum = (UCHAR)pConfiguration->stdTcpcsV6.ulValue;
            pContext->InitialOffloadParameters.UDPIPv4Checksum = (UCHAR)pConfiguration->stdUdpcsV4.ulValue;
            pContext->InitialOffloadParameters.UDPIPv6Checksum = (UCHAR)pConfiguration->stdUdpcsV6.ulValue;
            pContext->InitialOffloadParameters.LsoV1 = (UCHAR)pConfiguration->stdLsoV1.ulValue;
            pContext->InitialOffloadParameters.LsoV2IPv4 = (UCHAR)pConfiguration->stdLsoV2ip4.ulValue;
            pContext->InitialOffloadParameters.LsoV2IPv6 = (UCHAR)pConfiguration->stdLsoV2ip6.ulValue;
#if PARANDIS_SUPPORT_USO
            pContext->InitialOffloadParameters.Header.Revision = NDIS_OFFLOAD_PARAMETERS_REVISION_5;
            pContext->InitialOffloadParameters.Header.Size = NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_5;
            if (pConfiguration->USOv4Supported.ulValue)
            {
                // Uncomment later
                pContext->InitialOffloadParameters.UdpSegmentation.IPv4 = (UCHAR)pConfiguration->USOv4Supported.ulValue;
                pContext->Offload.flagsValue |= osbT4Uso;
            }
            if (pConfiguration->USOv6Supported.ulValue)
            {
                // Uncomment later
                pContext->InitialOffloadParameters.UdpSegmentation.IPv6 = (UCHAR)pConfiguration->USOv6Supported.ulValue;
                pContext->Offload.flagsValue |= osbT6Uso;
            }
#endif
            pContext->ulPriorityVlanSetting = pConfiguration->PriorityVlanTagging.ulValue;
            pContext->VlanId = pConfiguration->VlanId.ulValue & 0xfff;
            pContext->MaxPacketSize.nMaxDataSize = pConfiguration->JumboPacket.ulValue - ETH_HEADER_SIZE;
#if PARANDIS_SUPPORT_RSS
            pContext->bRSSOffloadSupported = pConfiguration->RSSOffloadSupported.ulValue ? TRUE : FALSE;
            pContext->RSSMaxQueuesNumber = (CCHAR)pConfiguration->NumRSSQueues.ulValue;
#endif
#if PARANDIS_SUPPORT_RSC
            pContext->RSC.bIPv4SupportedSW = (UCHAR)pConfiguration->RSCIPv4Supported.ulValue;
            pContext->RSC.bIPv6SupportedSW = (UCHAR)pConfiguration->RSCIPv6Supported.ulValue;
#endif
            pContext->uMaxFragmentsInOneNB = MAX_FRAGMENTS_IN_ONE_NB;
#if PARANDIS_SUPPORT_POLL
            // Win10 build: poll mode keyword is not in the INF, poll mode is disabled by compilation
            // Win11 build: poll mode keyword is in the INF
            // The poll mode keyword setting has effect currently only on Server 2025 (NDIS 6.89)
            // TODO - check Win11 24H2, probably on it the poll mode will work also
            bool bPollModeTestOnWin11 = false;
            pContext->bPollModeTry = pConfiguration->PollMode.ulValue &&
                                     CheckOSNdisVersion(6, bPollModeTestOnWin11 ? 85 : 89);
#endif
            if (!pContext->bDoSupportPriority)
            {
                pContext->ulPriorityVlanSetting = 0;
            }
            // if Vlan not supported
            if (!IsVlanSupported(pContext))
            {
                pContext->VlanId = 0;
            }

            {
                NDIS_STATUS status;
                PVOID p;
                UINT len = 0;
                NdisReadNetworkAddress(&status, &p, &len, cfg);
                if (status == NDIS_STATUS_SUCCESS && len == ETH_ALEN)
                {
                    NdisMoveMemory(pNewMACAddress, p, len);
                }
                else if (len && len != ETH_ALEN)
                {
                    DPrintf(0, "MAC address has wrong length of %d", len);
                }
                else
                {
                    DPrintf(4, "Nothing read for MAC, error %X", status);
                }
            }
            NdisCloseConfiguration(cfg);
        }
        NdisFreeMemory(pConfiguration, 0, 0);
    }
    return ret;
}

void ParaNdis_ResetOffloadSettings(PARANDIS_ADAPTER *pContext, tOffloadSettingsFlags *pDest, PULONG from)
{
    if (!pDest)
    {
        pDest = &pContext->Offload.flags;
    }
    if (!from)
    {
        from = &pContext->Offload.flagsValue;
    }

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

    pDest->fUsov4 = !!(*from & osbT4Uso);
    pDest->fUsov6 = !!(*from & osbT6Uso);
}

// clang-format off
static void DumpVirtIOFeatures(PPARANDIS_ADAPTER pContext)
{
    static const struct {  ULONG bitmask;  PCHAR Name; } Features[] =
    {

        {VIRTIO_NET_F_CSUM, "VIRTIO_NET_F_CSUM" },
        {VIRTIO_NET_F_GUEST_CSUM, "VIRTIO_NET_F_GUEST_CSUM" },
        {VIRTIO_NET_F_MTU, "VIRTIO_NET_F_MTU" },
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
        {VIRTIO_NET_F_GUEST_ANNOUNCE, "VIRTIO_NET_F_GUEST_ANNOUNCE"},
        {VIRTIO_NET_F_CTRL_MAC_ADDR, "VIRTIO_NET_F_CTRL_MAC_ADDR"},
        {VIRTIO_NET_F_MQ, "VIRTIO_NET_F_MQ"},
        {VIRTIO_RING_F_INDIRECT_DESC, "VIRTIO_RING_F_INDIRECT_DESC"},
        {VIRTIO_F_ANY_LAYOUT, "VIRTIO_F_ANY_LAYOUT"},
        {VIRTIO_RING_F_EVENT_IDX, "VIRTIO_RING_F_EVENT_IDX"},
        {VIRTIO_F_VERSION_1, "VIRTIO_F_VERSION_1"},
        {VIRTIO_F_RING_PACKED, "VIRTIO_F_RING_PACKED"},
        {VIRTIO_F_ACCESS_PLATFORM, "VIRTIO_F_ACCESS_PLATFORM"},
        {VIRTIO_NET_F_CTRL_GUEST_OFFLOADS, "VIRTIO_NET_F_CTRL_GUEST_OFFLOADS" },
        {VIRTIO_NET_F_RSC_EXT, "VIRTIO_NET_F_RSC_EXT" },
        {VIRTIO_NET_F_RSS, "VIRTIO_NET_F_RSS" },
        {VIRTIO_NET_F_HASH_REPORT, "VIRTIO_NET_F_HASH_REPORT" },
        {VIRTIO_NET_F_STANDBY, "VIRTIO_NET_F_STANDBY" },
        {VIRTIO_NET_F_HOST_USO, "VIRTIO_NET_F_HOST_USO" },
    };
    UINT i;
    for (i = 0; i < sizeof(Features) / sizeof(Features[0]); ++i)
    {
        if (virtio_is_feature_enabled(pContext->u64HostFeatures, Features[i].bitmask))
        {
            DPrintf(0, "VirtIO Host Feature %s", Features[i].Name);
        }
    }
}
// clang-format on

static BOOLEAN AckFeature(PPARANDIS_ADAPTER pContext, UINT64 Feature)
{
    if (virtio_is_feature_enabled(pContext->u64HostFeatures, Feature))
    {
        virtio_feature_enable(pContext->u64GuestFeatures, Feature);
        return TRUE;
    }
    return FALSE;
}

/**********************************************************
Prints out statistics
***********************************************************/
static void PrintStatistics(PARANDIS_ADAPTER *pContext)
{
    ULONG64 totalTxFrames = pContext->Statistics.ifHCOutBroadcastPkts + pContext->Statistics.ifHCOutMulticastPkts +
                            pContext->Statistics.ifHCOutUcastPkts;
    ULONG64 totalRxFrames = pContext->Statistics.ifHCInBroadcastPkts + pContext->Statistics.ifHCInMulticastPkts +
                            pContext->Statistics.ifHCInUcastPkts;

#if 0 /* TODO - setup accessor functions*/
    DPrintf(0, "[Diag!%X] RX buffers at VIRTIO %d of %d",
        pContext->CurrentMacAddress[5],
        pContext->RXPath.m_NetNofReceiveBuffers,
        pContext->NetMaxReceiveBuffers);
#endif
    DPrintf(0,
            "[Diag!] Bytes transmitted %I64u, received %I64u",
            pContext->Statistics.ifHCOutOctets,
            pContext->Statistics.ifHCInOctets);
    DPrintf(0,
            "[Diag!] Tx frames %I64u, CSO %d, LSO %d, Min TX buffers %d, dropped Tx %d",
            totalTxFrames,
            pContext->extraStatistics.framesCSOffload,
            pContext->extraStatistics.framesLSO,
            pContext->extraStatistics.minFreeTxBuffers,
            pContext->extraStatistics.droppedTxPackets);
    DPrintf(0,
            "[Diag!] Rx frames %I64u, Rx.Pri %d, RxHwCS.OK %d, FiltOut %d",
            totalRxFrames,
            pContext->extraStatistics.framesRxPriority,
            pContext->extraStatistics.framesRxCSHwOK,
            pContext->extraStatistics.framesFilteredOut);
}

static VOID InitializeRSCState(PPARANDIS_ADAPTER pContext)
{
#if PARANDIS_SUPPORT_RSC

    pContext->RSC.bIPv4Enabled = FALSE;
    pContext->RSC.bIPv6Enabled = FALSE;

    if (!pContext->bGuestChecksumSupported)
    {
        DPrintf(0, "Guest TSO cannot be enabled without guest checksum");
        return;
    }

    BOOLEAN bDynamicOffloadsPossible = pContext->bControlQueueSupported &&
                                       AckFeature(pContext, VIRTIO_NET_F_CTRL_GUEST_OFFLOADS);
    BOOLEAN bQemuRscSupport = bDynamicOffloadsPossible && AckFeature(pContext, VIRTIO_NET_F_RSC_EXT);

    if (pContext->RSC.bIPv4SupportedSW)
    {
        pContext->RSC.bIPv4Enabled = pContext->RSC.bIPv4SupportedHW = AckFeature(pContext, VIRTIO_NET_F_GUEST_TSO4);
    }
    else
    {
        pContext->RSC.bIPv4SupportedHW = virtio_is_feature_enabled(pContext->u64HostFeatures, VIRTIO_NET_F_GUEST_TSO4);
    }

    if (pContext->RSC.bIPv6SupportedSW)
    {
        pContext->RSC.bIPv6Enabled = pContext->RSC.bIPv6SupportedHW = AckFeature(pContext, VIRTIO_NET_F_GUEST_TSO6);
    }
    else
    {
        pContext->RSC.bIPv6SupportedHW = virtio_is_feature_enabled(pContext->u64HostFeatures, VIRTIO_NET_F_GUEST_TSO6);
    }

    pContext->RSC.bHasDynamicConfig = bDynamicOffloadsPossible;
    pContext->RSC.bQemuSupported = bQemuRscSupport;

    DPrintf(0,
            "Guest TSO state: IP4=%d, IP6=%d, Dynamic=%d",
            pContext->RSC.bIPv4Enabled,
            pContext->RSC.bIPv6Enabled,
            pContext->RSC.bHasDynamicConfig);

    DPrintf(0, "Guest QEMU RSC support state: %sresent", pContext->RSC.bQemuSupported ? "P" : "Not p");
#else
    UNREFERENCED_PARAMETER(pContext);
#endif
}

static void InitializeMaxMTUConfig(PPARANDIS_ADAPTER pContext)
{
    pContext->bMaxMTUConfigSupported = AckFeature(pContext, VIRTIO_NET_F_MTU);

    if (pContext->bMaxMTUConfigSupported)
    {
        virtio_get_config(&pContext->IODevice,
                          ETH_ALEN + 2 * sizeof(USHORT),
                          &pContext->MaxPacketSize.nMaxDataSize,
                          sizeof(USHORT));
    }
}

static void InitializeLinkPropertiesConfig(PPARANDIS_ADAPTER pContext)
{
    INT32 speed;
    UINT8 duplexState;

    const char *MediaDuplexStates[] = {"unknown", "half", "full"};

    pContext->bLinkPropertiesConfigSupported = AckFeature(pContext, VIRTIO_NET_F_SPEED_DUPLEX);

    // Default link properties
    pContext->LinkProperties.Speed = PARANDIS_DEFAULT_LINK_SPEED;
    pContext->LinkProperties.DuplexState = MediaDuplexStateFull;

    if (pContext->bLinkPropertiesConfigSupported)
    {
        virtio_get_config(&pContext->IODevice, FIELD_OFFSET(virtio_net_config, speed), &speed, sizeof(__u32));
        virtio_get_config(&pContext->IODevice, FIELD_OFFSET(virtio_net_config, duplex), &duplexState, sizeof(__u8));

        DPrintf(0, "Link properties in virtio configuration: %d:%d", speed, duplexState);

        if (speed != VIRTIO_NET_SPEED_UNKNOWN && speed)
        {
            pContext->LinkProperties.Speed = ((ULONGLONG)speed) * 1000000;
        }

        if (duplexState == VIRTIO_NET_DUPLEX_HALF)
        {
            pContext->LinkProperties.DuplexState = MediaDuplexStateHalf;
        }
        else if (duplexState == VIRTIO_NET_DUPLEX_FULL)
        {
            pContext->LinkProperties.DuplexState = MediaDuplexStateFull;
        }
    }

    DPrintf(0,
            "Speed=%llu Bit/s, Duplex=%s",
            pContext->LinkProperties.Speed,
            MediaDuplexStates[pContext->LinkProperties.DuplexState]);
}

static __inline void DumpMac(int dbg_level, const char *calling_function, const char *header_str, UCHAR *mac)
{
    DPrintf(dbg_level,
            "[%s] - %s: %02x-%02x-%02x-%02x-%02x-%02x",
            calling_function,
            header_str,
            mac[0],
            mac[1],
            mac[2],
            mac[3],
            mac[4],
            mac[5]);
}

static __inline void SetDeviceMAC(PPARANDIS_ADAPTER pContext, PUCHAR pDeviceMAC)
{
    if (pContext->bCfgMACAddrSupported && !pContext->bCtrlMACAddrSupported &&
        !virtio_is_feature_enabled(pContext->u64HostFeatures, VIRTIO_F_VERSION_1))
    {
        virtio_set_config(&pContext->IODevice, 0, pDeviceMAC, ETH_ALEN);
    }
}

static void InitializeMAC(PPARANDIS_ADAPTER pContext, PUCHAR pCurrentMAC)
{
    // Acknowledge related features
    pContext->bCfgMACAddrSupported = AckFeature(pContext, VIRTIO_NET_F_MAC);
    pContext->bCtrlMACAddrSupported = pContext->bControlQueueSupported &&
                                      AckFeature(pContext, VIRTIO_NET_F_CTRL_MAC_ADDR);
    DPrintf(0,
            "[%s] - MAC address configuration options: configuration space %d, control queue %d",
            __FUNCTION__,
            pContext->bCfgMACAddrSupported,
            pContext->bCtrlMACAddrSupported);

    // Read and validate permanent MAC address
    if (pContext->bCfgMACAddrSupported)
    {
        virtio_get_config(&pContext->IODevice, 0, &pContext->PermanentMacAddress, ETH_ALEN);

        if (!ParaNdis_ValidateMacAddress(pContext->PermanentMacAddress, FALSE))
        {
            DumpMac(0, __FUNCTION__, "Invalid device MAC ignored", pContext->PermanentMacAddress);
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
        DumpMac(0, __FUNCTION__, "No device MAC present, use default", pContext->PermanentMacAddress);
    }
    DumpMac(0, __FUNCTION__, "Permanent device MAC", pContext->PermanentMacAddress);

    // In virtio 1.0 MAC in configuration space is read only
    if (pContext->bCtrlMACAddrSupported || !virtio_is_feature_enabled(pContext->u64HostFeatures, VIRTIO_F_VERSION_1))
    {
        // Read and validate configured MAC address.
        if (ParaNdis_ValidateMacAddress(pCurrentMAC, TRUE))
        {
            DPrintf(0, "MAC address from configuration used");
            ETH_COPY_NETWORK_ADDRESS(pContext->CurrentMacAddress, pCurrentMAC);
        }
        else
        {
            DPrintf(0, "No valid MAC configured");
            ETH_COPY_NETWORK_ADDRESS(pContext->CurrentMacAddress, pContext->PermanentMacAddress);
        }
    }
    else
    {
        DPrintf(0,
                "MAC address from configuration will not be used. Setting MAC address through control queue is "
                "disabled.");
        ETH_COPY_NETWORK_ADDRESS(pContext->CurrentMacAddress, pContext->PermanentMacAddress);
    }

    // If control channel message for MAC address configuration is not supported
    //   Configure device with actual MAC address via configurations space
    // Else actual MAC address will be configured later via control queue
    SetDeviceMAC(pContext, pContext->CurrentMacAddress);

    DumpMac(0, __FUNCTION__, "Actual MAC", pContext->CurrentMacAddress);
}

static __inline void RestoreMAC(PPARANDIS_ADAPTER pContext)
{
    SetDeviceMAC(pContext, pContext->PermanentMacAddress);
}

static NDIS_STATUS NTStatusToNdisStatus(NTSTATUS nt_status)
{
    switch (nt_status)
    {
        case STATUS_SUCCESS:
            return NDIS_STATUS_SUCCESS;
        case STATUS_NOT_FOUND:
        case STATUS_DEVICE_NOT_CONNECTED:
            return NDIS_STATUS_ADAPTER_NOT_FOUND;
        case STATUS_INSUFFICIENT_RESOURCES:
            return NDIS_STATUS_RESOURCES;
        case STATUS_INVALID_PARAMETER:
            return NDIS_STATUS_INVALID_DEVICE_REQUEST;
        default:
            return NDIS_STATUS_FAILURE;
    }
}

static void ReadLinkState(PARANDIS_ADAPTER *pContext)
{
    if (pContext->bLinkDetectSupported)
    {
        USHORT linkStatus = 0;
        virtio_get_config(&pContext->IODevice, ETH_ALEN, &linkStatus, sizeof(linkStatus));
        pContext->bConnected = !!(linkStatus & VIRTIO_NET_S_LINK_UP);
        pContext->bGuestAnnounced = !!(linkStatus & VIRTIO_NET_S_ANNOUNCE);
    }
    else
    {
        pContext->bConnected = TRUE;
    }
}

/**********************************************************
Initializes the context structure
Major variables, received from NDIS on initialization, must be be set before this call
(for ex. pContext->MiniportHandle)

If this procedure fails, it is safe to call ParaNdis_CleanupContext

Parameters:
Return value:
    SUCCESS, if resources are OK
    NDIS_STATUS_RESOURCE_CONFLICT if not
***********************************************************/
NDIS_STATUS ParaNdis_InitializeContext(PARANDIS_ADAPTER *pContext, PNDIS_RESOURCE_LIST pResourceList)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    UCHAR CurrentMAC[ETH_ALEN] = {0};
    ULONG dependentOptions;

    DEBUG_ENTRY(0);

    if (pContext->RSSParameters.FailedInitialization)
    {
        return NDIS_STATUS_RESOURCES;
    }

    if (!ReadNicConfiguration(pContext, CurrentMAC))
    {
        return NDIS_STATUS_RESOURCES;
    }

    pContext->fCurrentLinkState = MediaConnectStateUnknown;

    if (pContext->PciResources.Init(pContext->MiniportHandle, pResourceList))
    {
        if (pContext->PciResources.GetInterruptFlags() & CM_RESOURCE_INTERRUPT_MESSAGE)
        {
            DPrintf(0, "Message interrupt assigned");
            pContext->bUsingMSIX = TRUE;
        }
        else
        {
            pContext->bSharedVectors = TRUE;
        }

        NTSTATUS nt_status = virtio_device_initialize(&pContext->IODevice,
                                                      &ParaNdisSystemOps,
                                                      pContext,
                                                      pContext->bUsingMSIX ? true : false);
        if (!NT_SUCCESS(nt_status))
        {
            DPrintf(0, "virtio_device_initialize failed with %x", nt_status);
            status = NTStatusToNdisStatus(nt_status);
            DEBUG_EXIT_STATUS(0, status);
            return status;
        }

        pContext->u64HostFeatures = virtio_get_features(&pContext->IODevice);
        DumpVirtIOFeatures(pContext);

        // Enable VIRTIO_F_ACCESS_PLATFORM feature on Windows 10 and Windows Server 2016
#if (WINVER == 0x0A00)
        AckFeature(pContext, VIRTIO_F_ACCESS_PLATFORM);
#endif
        if (AckFeature(pContext, VIRTIO_NET_F_STANDBY))
        {
            pContext->bSuppressLinkUp = true;
            pContext->bPollModeTry = false;
        }

        pContext->bLinkDetectSupported = AckFeature(pContext, VIRTIO_NET_F_STATUS);
        ReadLinkState(pContext);
        DPrintf(0, "Link status on driver startup: %d", pContext->bConnected);
        pContext->bGuestAnnounced = false;
        ParaNdis_SynchronizeLinkState(pContext, false);

        InitializeLinkPropertiesConfig(pContext);
        pContext->bControlQueueSupported = AckFeature(pContext, VIRTIO_NET_F_CTRL_VQ);
        pContext->bGuestAnnounceSupported = pContext->bLinkDetectSupported && pContext->bControlQueueSupported &&
                                            AckFeature(pContext, VIRTIO_NET_F_GUEST_ANNOUNCE);
        InitializeMAC(pContext, CurrentMAC);
        InitializeMaxMTUConfig(pContext);

        pContext->bUseMergedBuffers = AckFeature(pContext, VIRTIO_NET_F_MRG_RXBUF);
        pContext->nVirtioHeaderSize = (pContext->bUseMergedBuffers) ? sizeof(virtio_net_hdr_mrg_rxbuf)
                                                                    : sizeof(virtio_net_hdr);
        AckFeature(pContext, VIRTIO_RING_F_EVENT_IDX);
    }
    else
    {
        DPrintf(0, "Error: Incomplete resources");
        status = NDIS_STATUS_RESOURCE_CONFLICT;
        return status;
    }

    if (pContext->ulPriorityVlanSetting)
    {
        pContext->MaxPacketSize.nMaxFullSizeHwTx = pContext->MaxPacketSize.nMaxFullSizeOS + ETH_PRIORITY_HEADER_SIZE;
    }

#if PARANDIS_SUPPORT_RSS
    pContext->bMultiQueue = pContext->bControlQueueSupported && AckFeature(pContext, VIRTIO_NET_F_MQ);
    bool bRSS = virtio_is_feature_enabled(pContext->u64HostFeatures, VIRTIO_NET_F_RSS);
    bool bHash = virtio_is_feature_enabled(pContext->u64HostFeatures, VIRTIO_NET_F_HASH_REPORT);

    if ((bHash || bRSS) && pContext->bControlQueueSupported &&
        virtio_is_feature_enabled(pContext->u64HostFeatures, VIRTIO_F_VERSION_1))
    {
        struct virtio_net_config cfg = {};
        virtio_get_config(&pContext->IODevice, FIELD_OFFSET(struct virtio_net_config, duplex), &cfg.duplex, 8);
        pContext->DeviceRSSCapabilities.SupportedHashes = cfg.supported_hash_types;
        pContext->DeviceRSSCapabilities.MaxKeySize = cfg.rss_max_key_size;
        pContext->DeviceRSSCapabilities.MaxIndirectEntries = cfg.rss_max_indirection_table_length;

        ParaNdis6_CheckDeviceRSSCapabilities(pContext, bRSS, bHash);

        if (bRSS)
        {
            pContext->bRSSSupportedByDevice = AckFeature(pContext, VIRTIO_NET_F_RSS);
            pContext->bRSSSupportedByDevicePersistent = pContext->bRSSSupportedByDevice;
        }
        if (bHash)
        {
            pContext->bHashReportedByDevice = AckFeature(pContext, VIRTIO_NET_F_HASH_REPORT);
        }
    }
    if (pContext->bRSSSupportedByDevice)
    {
        pContext->bMultiQueue = true;
    }
    if (pContext->bMultiQueue)
    {
        virtio_get_config(&pContext->IODevice,
                          ETH_ALEN + sizeof(USHORT),
                          &pContext->nHardwareQueues,
                          sizeof(pContext->nHardwareQueues));
    }
    else
    {
        pContext->nHardwareQueues = 1;
    }
#else
    pContext->nHardwareQueues = 1;
#endif

    dependentOptions = osbT4TcpChecksum | osbT4UdpChecksum | osbT4TcpOptionsChecksum | osbT6TcpChecksum |
                       osbT6UdpChecksum | osbT6TcpOptionsChecksum | osbT6IpExtChecksum;

    if ((pContext->Offload.flagsValue & dependentOptions) && !AckFeature(pContext, VIRTIO_NET_F_CSUM))
    {
        DPrintf(0, "Host does not support CSUM, disabling CS offload");
        pContext->Offload.flagsValue &= ~dependentOptions;
    }

    pContext->bGuestChecksumSupported = AckFeature(pContext, VIRTIO_NET_F_GUEST_CSUM);

    InitializeRSCState(pContext);

    pContext->MaxPacketSize.nMaxFullSizeOS = pContext->MaxPacketSize.nMaxDataSize + ETH_HEADER_SIZE;
    pContext->MaxPacketSize.nMaxFullSizeHwTx = pContext->MaxPacketSize.nMaxFullSizeOS;

    if (pContext->RSC.bIPv4SupportedHW || pContext->RSC.bIPv6SupportedHW)
    {
        pContext->MaxPacketSize.nMaxDataSizeHwRx = MAX_HW_RX_PACKET_SIZE;
        pContext->MaxPacketSize.nMaxFullSizeOsRx = MAX_OS_RX_PACKET_SIZE;
    }
    else
    {
        pContext->MaxPacketSize.nMaxDataSizeHwRx = pContext->MaxPacketSize.nMaxFullSizeOS + ETH_PRIORITY_HEADER_SIZE;
        pContext->MaxPacketSize.nMaxFullSizeOsRx = pContext->MaxPacketSize.nMaxFullSizeOS;
    }

    // now, after we checked the capabilities, we can initialize current
    // configuration of offload tasks
    ParaNdis_ResetOffloadSettings(pContext, NULL, NULL);

    if (pContext->Offload.flags.fTxLso && !AckFeature(pContext, VIRTIO_NET_F_HOST_TSO4))
    {
        DisableLSOv4Permanently(pContext, __FUNCTION__, "Host does not support TSOv4");
    }

    if (pContext->Offload.flags.fTxLsov6 && !AckFeature(pContext, VIRTIO_NET_F_HOST_TSO6))
    {
        DisableLSOv6Permanently(pContext, __FUNCTION__, "Host does not support TSOv6");
    }

    if (pContext->Offload.flags.fUsov4 || pContext->Offload.flags.fUsov6)
    {
        LPCSTR message = NULL;
        if (!CheckNdisVersion(6, 83))
        {
            message = "NDIS before 1903";
        }
        else if (virtio_is_feature_enabled(pContext->u64GuestFeatures, VIRTIO_NET_F_STANDBY))
        {
            message = "failover configuration";
        }
        else if (!AckFeature(pContext, VIRTIO_NET_F_HOST_USO))
        {
            message = "host without USO support";
        }
        if (message)
        {
            DPrintf(0, "USO is disabled due to %s", message);
            pContext->Offload.flagsValue &= ~osbT4Uso;
            pContext->Offload.flagsValue &= ~osbT6Uso;
            ParaNdis_ResetOffloadSettings(pContext, NULL, NULL);
        }
    }

    pContext->bUseIndirect = AckFeature(pContext, VIRTIO_RING_F_INDIRECT_DESC);
    pContext->bAnyLayout = AckFeature(pContext, VIRTIO_F_ANY_LAYOUT);
    if (AckFeature(pContext, VIRTIO_F_VERSION_1))
    {
        pContext->nVirtioHeaderSize = pContext->bHashReportedByDevice ? sizeof(virtio_net_hdr_v1_hash)
                                                                      : sizeof(virtio_net_hdr_v1);

        pContext->bAnyLayout = true;
        DPrintf(0, "Assuming VIRTIO_F_ANY_LAYOUT for V1 device");
        if (AckFeature(pContext, VIRTIO_F_RING_PACKED))
        {
            DPrintf(0, "Using PACKED ring");
        }

        if (AckFeature(pContext, VIRTIO_F_ORDER_PLATFORM))
        {
            DPrintf(0, "Ack VIRTIO_F_ORDER_PLATFORM to device");
        }
    }

    if (pContext->bControlQueueSupported)
    {
        pContext->bCtrlRXFiltersSupported = AckFeature(pContext, VIRTIO_NET_F_CTRL_RX);
        pContext->bCtrlRXExtraFiltersSupported = AckFeature(pContext, VIRTIO_NET_F_CTRL_RX_EXTRA);
        pContext->bCtrlVLANFiltersSupported = AckFeature(pContext, VIRTIO_NET_F_CTRL_VLAN);
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        NTSTATUS nt_status = virtio_set_features(&pContext->IODevice, pContext->u64GuestFeatures);
        if (!NT_SUCCESS(nt_status))
        {
            DPrintf(0, "virtio_set_features failed with %x", nt_status);
            status = NTStatusToNdisStatus(nt_status);
        }
    }

    DEBUG_EXIT_STATUS(0, status);
    return status;
}

#if PARANDIS_SUPPORT_RSS
static USHORT DetermineQueueNumber(PARANDIS_ADAPTER *pContext)
{
    if (!pContext->bUsingMSIX)
    {
        DPrintf(0, "No MSIX, using 1 queue");
        return 1;
    }

    if (pContext->bMultiQueue)
    {
        DPrintf(0, "Number of hardware queues = %d", pContext->nHardwareQueues);
    }
    else
    {
        DPrintf(0, "CTRL_MQ not acked, # of bundles set to 1");
        return 1;
    }

    USHORT nProcessors = USHORT(ParaNdis_GetSystemCPUCount() & 0xFFFF);

    /* In virtio the type of the queue index is "short", thus this type casting */
    USHORT nBundles = USHORT(((pContext->pMSIXInfoTable->MessageCount - 1) / 2) & 0xFFFF);
    if (!nBundles && (pContext->pMSIXInfoTable->MessageCount == 1 || pContext->pMSIXInfoTable->MessageCount == 2))
    {
        DPrintf(0, "WARNING: %d MSIX message(s), performance will be reduced", pContext->pMSIXInfoTable->MessageCount);
        nBundles = 1;
    }

    DPrintf(0, "%lu CPUs reported", nProcessors);
    DPrintf(0, "%lu MSIs, %u queues' bundles", pContext->pMSIXInfoTable->MessageCount, nBundles);

    USHORT nBundlesLimitByCPU = (pContext->nHardwareQueues < nProcessors) ? pContext->nHardwareQueues : nProcessors;
    nBundles = (nBundles < nBundlesLimitByCPU) ? nBundles : nBundlesLimitByCPU;

    DPrintf(0, "# of path bundles = %u", nBundles);

    return nBundles;
}
#else
static USHORT DetermineQueueNumber(PARANDIS_ADAPTER *pContext)
{
    if (!pContext->bUsingMSIX)
    {
        DPrintf(0, "No MSIX, using 1 queue");
        return 1;
    }

    if (pContext->pMSIXInfoTable->MessageCount == 1 || pContext->pMSIXInfoTable->MessageCount > 2)
    {
        return 1;
    }

    return 0;
}
#endif

static NDIS_STATUS SetupDPCTarget(PARANDIS_ADAPTER *pContext)
{
    ULONG i;
#if NDIS_SUPPORT_NDIS620
    NDIS_STATUS status;
    PROCESSOR_NUMBER procNumber;
#endif

    for (i = 0; i < pContext->nPathBundles; i++)
    {
#if NDIS_SUPPORT_NDIS620
        status = KeGetProcessorNumberFromIndex(i, &procNumber);
        if (status != NDIS_STATUS_SUCCESS)
        {
            DPrintf(0, "KeGetProcessorNumberFromIndex failed for index %lu - %X", i, status);
            return status;
        }
        ParaNdis_ProcessorNumberToGroupAffinity(&pContext->pPathBundles[i].rxPath.DPCAffinity, &procNumber);
        pContext->pPathBundles[i].txPath.DPCAffinity = pContext->pPathBundles[i].rxPath.DPCAffinity;
#elif NDIS_SUPPORT_NDIS6
        pContext->pPathBundles[i].rxPath.DPCTargetProcessor = 1i64 << i;
        pContext->pPathBundles[i].txPath.DPCTargetProcessor = pContext->pPathBundles[i].rxPath.DPCTargetProcessor;
#else
#error not supported
#endif
    }

#if NDIS_SUPPORT_NDIS620
    pContext->CXPath.DPCAffinity = pContext->pPathBundles[0].rxPath.DPCAffinity;
#elif NDIS_SUPPORT_NDIS6
    pContext->CXPath.DPCTargetProcessor = pContext->pPathBundles[0].rxPath.DPCTargetProcessor;
#else
#error not yet defined
#endif
    return NDIS_STATUS_SUCCESS;
}

/**************************************************************************
For each RX packet we need:
0 or more page-sized blocks for data (eth header and up)
header block (4K or less) to accomodate header, indirect are and
the remainder of the packet that is less than a page
***************************************************************************/
static void PrepareRXLayout(PARANDIS_ADAPTER *pContext)
{
// #define RX_LAYOUT_AS_BEFORE
#ifndef RX_LAYOUT_AS_BEFORE
    USHORT alignment = 32;
    ULONG rxPayloadSize;
    bool combineHeaderAndData = pContext->bAnyLayout;
    if (combineHeaderAndData)
    {
        pContext->RxLayout.ReserveForHeader = 0;
        rxPayloadSize = pContext->MaxPacketSize.nMaxDataSizeHwRx + pContext->nVirtioHeaderSize;
    }
    else
    {
        pContext->RxLayout.ReserveForHeader = ALIGN_UP_BY(pContext->nVirtioHeaderSize, alignment);
        rxPayloadSize = pContext->MaxPacketSize.nMaxDataSizeHwRx;
    }
    // include the header
    pContext->RxLayout.TotalAllocationsPerBuffer = USHORT(rxPayloadSize / PAGE_SIZE) + 1;
    USHORT tail = rxPayloadSize % PAGE_SIZE;
    // we need one entry for each data page + header + tail (if any)
    pContext->RxLayout.IndirectEntries = pContext->RxLayout.TotalAllocationsPerBuffer + !!tail;
    pContext->RxLayout.ReserveForIndirectArea = ALIGN_UP_BY(pContext->RxLayout.IndirectEntries * sizeof(VirtIOBufferDescriptor),
                                                            alignment);
    pContext->RxLayout.HeaderPageAllocation = pContext->RxLayout.ReserveForHeader +
                                              pContext->RxLayout.ReserveForIndirectArea;
    if (pContext->RxLayout.HeaderPageAllocation + tail > PAGE_SIZE)
    {
        // packet tail is quite big, placing it in additional page
        pContext->RxLayout.TotalAllocationsPerBuffer++;
    }
    else
    {
        pContext->RxLayout.HeaderPageAllocation += tail;
        pContext->RxLayout.ReserveForPacketTail = tail;
    }
    while (!IsPowerOfTwo(pContext->RxLayout.HeaderPageAllocation))
    {
        pContext->RxLayout.HeaderPageAllocation++;
    }
#else
    USHORT alignment = 8;
    pContext->RxLayout.ReserveForHeader = ALIGN_UP_BY(pContext->nVirtioHeaderSize, alignment);
    pContext->RxLayout.ReserveForIndirectArea = PAGE_SIZE - pContext->RxLayout.ReserveForHeader;
    pContext->RxLayout.ReserveForPacketTail = 0;
    pContext->RxLayout.HeaderPageAllocation = PAGE_SIZE;
    // this includes header
    pContext->RxLayout.TotalAllocationsPerBuffer = USHORT(pContext->MaxPacketSize.nMaxDataSizeHwRx / PAGE_SIZE) + 2;
    pContext->RxLayout.IndirectEntries = pContext->RxLayout.TotalAllocationsPerBuffer;
#endif
    TraceNoPrefix(0,
                  "header: h%d + i%d(%d) + t%d = %d, allocs: %d, separate tail %s",
                  pContext->RxLayout.ReserveForHeader,
                  pContext->RxLayout.IndirectEntries,
                  pContext->RxLayout.ReserveForIndirectArea,
                  pContext->RxLayout.ReserveForPacketTail,
                  pContext->RxLayout.HeaderPageAllocation,
                  pContext->RxLayout.TotalAllocationsPerBuffer,
                  pContext->bRxSeparateTail ? "on" : "off");
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
    UINT i;
    USHORT nVirtIOQueues = pContext->nHardwareQueues * 2 + 2;

    pContext->nPathBundles = DetermineQueueNumber(pContext);
    if (pContext->nPathBundles == 0)
    {
        DPrintf(0, "no I/O paths");
        return NDIS_STATUS_RESOURCES;
    }

    NTSTATUS nt_status = virtio_reserve_queue_memory(&pContext->IODevice, nVirtIOQueues);
    if (!NT_SUCCESS(nt_status))
    {
        DPrintf(0, "failed to reserve %u queues", nVirtIOQueues);
        return NTStatusToNdisStatus(nt_status);
    }

    PrepareRXLayout(pContext);

    if (pContext->bControlQueueSupported && pContext->CXPath.Create(2 * pContext->nHardwareQueues))
    {
        pContext->bCXPathCreated = TRUE;
    }
    else
    {
        DPrintf(0, "The Control vQueue does not work!");
        pContext->bCtrlRXFiltersSupported = pContext->bCtrlRXExtraFiltersSupported = FALSE;
        pContext->bCtrlMACAddrSupported = pContext->bCtrlVLANFiltersSupported = FALSE;
        pContext->bCXPathCreated = FALSE;
    }

    pContext->pPathBundles = (CPUPathBundle *)NdisAllocateMemoryWithTagPriority(pContext->MiniportHandle,
                                                                                pContext->nPathBundles * sizeof(*pContext->pPathBundles),
                                                                                PARANDIS_MEMORY_TAG,
                                                                                NormalPoolPriority);
    if (pContext->pPathBundles == nullptr)
    {
        DPrintf(0, "Path bundles allocation failed");
        return status;
    }

    for (i = 0; i < pContext->nPathBundles; i++)
    {
        new (pContext->pPathBundles + i) CPUPathBundle();
    }

    for (i = 0; i < pContext->nPathBundles; i++)
    {
        if (!pContext->pPathBundles[i].rxPath.Create(pContext, i * 2))
        {
            DPrintf(0, "CParaNdisRX creation failed");
            return status;
        }
        pContext->pPathBundles[i].rxCreated = true;

        if (!pContext->pPathBundles[i].txPath.Create(pContext, i * 2 + 1))
        {
            DPrintf(0, "CParaNdisTX creation failed");
            return status;
        }
        pContext->pPathBundles[i].txCreated = true;
    }

    if (pContext->bCXPathCreated)
    {
        pContext->pPathBundles[0].cxPath = &pContext->CXPath;
    }

    status = NDIS_STATUS_SUCCESS;

    return status;
}

static UINT8 ReadDeviceStatus(PARANDIS_ADAPTER *pContext)
{
    return (UINT8)virtio_get_status(&pContext->IODevice);
}

static VOID ParaNdis_AddDriverOKStatus(PPARANDIS_ADAPTER pContext)
{
    pContext->bDeviceInitialized = TRUE;

    KeMemoryBarrier();

    virtio_device_ready(&pContext->IODevice);
}

void ParaNdis_DeviceConfigureRSC(PARANDIS_ADAPTER *pContext)
{
    DEBUG_ENTRY(0);

#if PARANDIS_SUPPORT_RSC
    UINT64 GuestOffloads;

    GuestOffloads = 1 << VIRTIO_NET_F_GUEST_CSUM | ((pContext->RSC.bIPv4Enabled) ? (1 << VIRTIO_NET_F_GUEST_TSO4) : 0) |
                    ((pContext->RSC.bIPv6Enabled) ? (1 << VIRTIO_NET_F_GUEST_TSO6) : 0) |
                    ((pContext->RSC.bQemuSupported) ? (1LL << VIRTIO_NET_F_RSC_EXT) : 0);

    if (pContext->RSC.bHasDynamicConfig)
    {
        DPrintf(0, "Updating offload settings with %I64x", GuestOffloads);
        pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_GUEST_OFFLOADS,
                                            VIRTIO_NET_CTRL_GUEST_OFFLOADS_SET,
                                            &GuestOffloads,
                                            sizeof(GuestOffloads),
                                            NULL,
                                            0,
                                            2);
    }
    else
    {
        DPrintf(0, "ERROR: Can't update offload settings dynamically!");
    }
#else
    UNREFERENCED_PARAMETER(pContext);
#endif /* PARANDIS_SUPPORT_RSC */
}

static NDIS_STATUS SetInitialDeviceRSS(PARANDIS_ADAPTER *pContext)
{
    NDIS_STATUS status;
    struct virtio_net_rss_config cfg = {};
    max_tx_vq(&cfg) = (USHORT)pContext->nPathBundles;
    DEBUG_ENTRY(0);
    if (!pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_MQ,
                                             VIRTIO_NET_CTRL_MQ_RSS_CONFIG,
                                             &cfg,
                                             sizeof(cfg),
                                             NULL,
                                             0,
                                             2))
    {
        status = NDIS_STATUS_DEVICE_FAILED;
    }
    else
    {
        status = NDIS_STATUS_SUCCESS;
    }
    return status;
}

NDIS_STATUS ParaNdis_DeviceConfigureMultiQueue(PARANDIS_ADAPTER *pContext)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    DEBUG_ENTRY(0);

    if (pContext->nPathBundles > 1)
    {
        bool bHasMQ = pContext->u64GuestFeatures & (1LL << VIRTIO_NET_F_MQ);
        u16 nPaths = u16(pContext->nPathBundles);
        if (pContext->bRSSSupportedByDevice)
        {
            status = SetInitialDeviceRSS(pContext);
        }
        else if (bHasMQ && !pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_MQ,
                                                                VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET,
                                                                &nPaths,
                                                                sizeof(nPaths),
                                                                NULL,
                                                                0,
                                                                2))
        {
            DPrintf(0, "Sending MQ control message failed");
            status = NDIS_STATUS_DEVICE_FAILED;
        }
    }

    DEBUG_EXIT_STATUS(0, status);
    return status;
}

static VOID ParaNdis_KickRX(PARANDIS_ADAPTER *pContext)
{
    UINT i;
    for (i = 0; i < pContext->nPathBundles; i++)
    {
        pContext->pPathBundles[i].rxPath.KickRXRing();
    }
}

NDIS_STATUS ParaNdis_DeviceEnterD0(PARANDIS_ADAPTER *pContext)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    DEBUG_ENTRY(0);

    ReadLinkState(pContext);
    pContext->bEnableInterruptHandlingDPC = TRUE;
    ParaNdis_SynchronizeLinkState(pContext);
    ParaNdis_AddDriverOKStatus(pContext);
    ParaNdis_DeviceConfigureMultiQueue(pContext);
    ParaNdis_DeviceConfigureRSC(pContext);
    ParaNdis_UpdateMAC(pContext);
    ParaNdis_KickRX(pContext);

    DEBUG_EXIT_STATUS(0, status);
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

    status = ParaNdis_FinishSpecificInitialization(pContext);
    DPrintf(0, "ParaNdis_FinishSpecificInitialization passed, status = %X", status);

    if (status == NDIS_STATUS_SUCCESS)
    {
        status = ParaNdis_VirtIONetInit(pContext);
        DPrintf(0, "ParaNdis_VirtIONetInit passed, status = %X", status);
    }

    if (status == NDIS_STATUS_SUCCESS && pContext->bUsingMSIX)
    {
        status = ParaNdis_ConfigureMSIXVectors(pContext);
        DPrintf(0, "ParaNdis_ConfigureMSIXVectors passed, status = %X", status);
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        status = SetupDPCTarget(pContext);
        DPrintf(0, "SetupDPCTarget passed, status = %X", status);
    }

    if (status == NDIS_STATUS_SUCCESS && pContext->bPollModeTry && pContext->RSSMaxQueuesNumber &&
        pContext->bRSSOffloadSupported && (UINT)pContext->RSSMaxQueuesNumber >= pContext->nPathBundles)
    {
        int nPollModeOK = 0;
        for (int i = 0; i < pContext->RSSMaxQueuesNumber; i++)
        {
            nPollModeOK += pContext->PollHandlers[i].Register(pContext, i);
        }
        if (nPollModeOK == pContext->RSSMaxQueuesNumber)
        {
            pContext->bPollModeEnabled = true;
        }
    }

    if (pContext->bPollModeTry)
    {
        DPrintf(0, "Poll mode tried and %sabled", pContext->bPollModeEnabled ? "en" : "dis");
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        ParaNdis_DeviceEnterD0(pContext);
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
    ULONG i;
    DEBUG_ENTRY(0);

    /* list NetReceiveBuffersWaiting must be free */

#ifdef PARANDIS_SUPPORT_RSS
    for (i = 0; i < ARRAYSIZE(pContext->ReceiveQueues); i++)
    {
        pRxNetDescriptor pBufferDescriptor;

        while (NULL != (pBufferDescriptor = ReceiveQueueGetBuffer(pContext->ReceiveQueues + i)))
        {
            pBufferDescriptor->Queue->ReuseReceiveBuffer(pBufferDescriptor);
        }
    }
#endif

    for (i = 0; i < pContext->nPathBundles; i++)
    {
        pRxNetDescriptor pBufferDescriptor;

        while (NULL !=
               (pBufferDescriptor = ReceiveQueueGetBuffer(&pContext->pPathBundles[i].rxPath.UnclassifiedPacketsQueue())))
        {
            pBufferDescriptor->Queue->ReuseReceiveBuffer(pBufferDescriptor);
        }
    }

    RestoreMAC(pContext);

    for (i = 0; i < (UINT)pContext->RSSMaxQueuesNumber; i++)
    {
        pContext->PollHandlers[i].Unregister();
    }

    for (i = 0; i < pContext->nPathBundles; i++)
    {
        if (pContext->pPathBundles[i].txCreated)
        {
            pContext->pPathBundles[i].txPath.Shutdown();
        }

        if (pContext->pPathBundles[i].rxCreated)
        {
            pContext->pPathBundles[i].rxPath.Shutdown();

            /* this can be freed, queue shut down */
            pContext->pPathBundles[i].rxPath.FreeRxDescriptorsFromList();
        }
    }

    if (pContext->bCXPathCreated)
    {
        pContext->CXPath.Shutdown();
    }

    PrintStatistics(pContext);
}

static void PreventDPCServicing(PARANDIS_ADAPTER *pContext)
{
    LONG inside;
    pContext->bEnableInterruptHandlingDPC = FALSE;
    KeMemoryBarrier();
    do
    {
        inside = InterlockedIncrement(&pContext->counterDPCInside);
        InterlockedDecrement(&pContext->counterDPCInside);
        if (inside > 1)
        {
            DPrintf(0, "waiting...");
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
static VOID ParaNdis_CleanupContext(PARANDIS_ADAPTER *pContext)
{
    /* disable any interrupt generation */
    if (pContext->bDeviceInitialized)
    {
        ParaNdis_ResetVirtIONetDevice(pContext);
    }

    PreventDPCServicing(pContext);

    /****************************************
    ensure all the incoming packets returned,
    free all the buffers and their descriptors
    *****************************************/

    ParaNdis_SetLinkState(pContext, MediaConnectStateUnknown);
    VirtIONetRelease(pContext);

    ParaNdis_FinalizeCleanup(pContext);

    pContext->m_StateMachine.NotifyHalted();

    if (pContext->pPathBundles != NULL)
    {
        UINT i;

        for (i = 0; i < pContext->nPathBundles; i++)
        {
            pContext->pPathBundles[i].~CPUPathBundle();
        }
        NdisFreeMemoryWithTagPriority(pContext->MiniportHandle, pContext->pPathBundles, PARANDIS_MEMORY_TAG);
        pContext->pPathBundles = nullptr;
    }

    if (pContext->RSS2QueueMap)
    {
        NdisFreeMemoryWithTagPriority(pContext->MiniportHandle, pContext->RSS2QueueMap, PARANDIS_MEMORY_TAG);
        pContext->RSS2QueueMap = nullptr;
        pContext->RSS2QueueLength = 0;
    }

    virtio_device_shutdown(&pContext->IODevice);
}

/**********************************************************
System shutdown handler (shutdown, restart, bugcheck)
Parameters:
    context
***********************************************************/
VOID ParaNdis_OnShutdown(PARANDIS_ADAPTER *pContext)
{
    DEBUG_ENTRY(0); // this is only for kdbg :)

    pContext->systemThread.Stop();

    ParaNdis_ResetVirtIONetDevice(pContext);

    pContext->m_StateMachine.NotifyShutdown();
}

static __inline CCHAR GetReceiveQueueForCurrentCPU(PARANDIS_ADAPTER *pContext)
{
#if PARANDIS_SUPPORT_RSS
    return ParaNdis6_RSSGetCurrentCpuReceiveQueue(&pContext->RSSParameters);
#else
    UNREFERENCED_PARAMETER(pContext);

    return PARANDIS_RECEIVE_NO_QUEUE;
#endif
}

static __inline pRxNetDescriptor ReceiveQueueGetBuffer(PPARANDIS_RECEIVE_QUEUE pQueue)
{
    PLIST_ENTRY pListEntry = NdisInterlockedRemoveHeadList(&pQueue->BuffersList, &pQueue->Lock);
    return pListEntry ? CONTAINING_RECORD(pListEntry, RxNetDescriptor, ReceiveQueueListEntry) : NULL;
}

static __inline BOOLEAN ReceiveQueueHasBuffers(PPARANDIS_RECEIVE_QUEUE pQueue)
{
    BOOLEAN res;

    NdisAcquireSpinLock(&pQueue->Lock);
    res = !IsListEmpty(&pQueue->BuffersList);
    NdisReleaseSpinLock(&pQueue->Lock);

    return res;
}

static VOID UpdateReceiveSuccessStatistics(PPARANDIS_ADAPTER pContext,
                                           PNET_PACKET_INFO pPacketInfo,
                                           UINT nCoalescedSegmentsCount)
{
    pContext->Statistics.ifHCInOctets += pPacketInfo->dataLength;

    if (pPacketInfo->isUnicast)
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
        NETKVM_ASSERT(FALSE);
    }
}

static __inline VOID UpdateReceiveFailStatistics(PPARANDIS_ADAPTER pContext, UINT nCoalescedSegmentsCount)
{
    pContext->Statistics.ifInErrors++;
    pContext->Statistics.ifInDiscards += nCoalescedSegmentsCount;
}

static BOOLEAN ProcessReceiveQueue(PARANDIS_ADAPTER *pContext,
                                   PULONG pnPacketsToIndicateLeft,
                                   PPARANDIS_RECEIVE_QUEUE pTargetReceiveQueue,
                                   PNET_BUFFER_LIST *indicate,
                                   PNET_BUFFER_LIST *indicateTail,
                                   ULONG *nIndicate)
{
    pRxNetDescriptor pBufferDescriptor;
    BOOLEAN isRxBufferShortage = FALSE;

    while ((*pnPacketsToIndicateLeft > 0) && (NULL != (pBufferDescriptor = ReceiveQueueGetBuffer(pTargetReceiveQueue))))
    {
        PNET_PACKET_INFO pPacketInfo = &pBufferDescriptor->PacketInfo;

        if (ParaNdis_IsTxRxPossible(pContext))
        {
            UINT nCoalescedSegmentsCount;
            PNET_BUFFER_LIST packet = ParaNdis_PrepareReceivedPacket(pContext,
                                                                     pBufferDescriptor,
                                                                     &nCoalescedSegmentsCount);
            if (packet != NULL)
            {
                UpdateReceiveSuccessStatistics(pContext, pPacketInfo, nCoalescedSegmentsCount);
                if (*indicate == nullptr)
                {
                    *indicate = *indicateTail = packet;
                }
                else
                {
                    NET_BUFFER_LIST_NEXT_NBL(*indicateTail) = packet;
                    *indicateTail = packet;
                }

                NET_BUFFER_LIST_NEXT_NBL(*indicateTail) = NULL;
                (*pnPacketsToIndicateLeft)--;
                (*nIndicate)++;

                if (!isRxBufferShortage)
                {
                    isRxBufferShortage = pBufferDescriptor->Queue->IsRxBuffersShortage();
                }
            }
            else
            {
                UpdateReceiveFailStatistics(pContext, nCoalescedSegmentsCount);
                pBufferDescriptor->Queue->ReuseReceiveBuffer(pBufferDescriptor);
            }
        }
        else
        {
            pContext->extraStatistics.framesFilteredOut++;
            pBufferDescriptor->Queue->ReuseReceiveBuffer(pBufferDescriptor);
        }
    }
    return isRxBufferShortage;
}

/* DPC throttling implementation.

The main loop of the function RxDPCWorkBody finishes under light traffic
when there are no more packets in the virtqueue receive queue. Under the
heavy traffic, the situation is more complicated: the ready-to process packets
are fetched from the virtqueue's receive queue, indicated toward the OS upper
layer in the ProcessReceiveQueue and, depending on the relative speed of
a virtual NIC and guest OS, may be returned by the upper layer to the driver
with ParaNdis6_ReturnNetBufferLists and reinserted into the virtqueue for
reading.

Under these conditions, the RxDPCWorkBody's loop terminates because
ProcessReceiveQueue is limited by the nPacketsToIndicate parameter, accepting
the configuration value for DPC throttling. ProcessReceiveQueue decreases
the nPacketsToIndicate's value each time the packet is indicated toward the
OS's upper layer and stops indicating when nPacketsToIndicate drops to zero.

When nPacketsToIndicate reaches zero, the loop operates in the following way:
ProcessRxRing fetches the ready-to-process packet from virtqueue and places
them into receiving queues, but the packets are not indicated by
ProcessReceiveQueue; OS has no packets to be reinserted into the virtqueue,
virtqueue eventually becomes empty and RxDPCWorkBody's loop exits  */

static BOOLEAN RxDPCWorkBody(PARANDIS_ADAPTER *pContext, CPUPathBundle *pathBundle, ULONG nPacketsToIndicate)
{
    BOOLEAN res = FALSE;
    bool rxPathOwner = false;
    PNET_BUFFER_LIST indicate, indicateTail;
    ULONG nIndicate;

    CCHAR CurrCpuReceiveQueue = GetReceiveQueueForCurrentCPU(pContext);
    BOOLEAN isRxBufferShortage = FALSE;

    indicate = nullptr;
    indicateTail = nullptr;
    nIndicate = 0;

    /* pathBundle is passed from ParaNdis_DPCWorkBody and may be NULL
    if case DPC handler is scheduled by RSS to the CPU with
    associated queues */
    if (pathBundle != nullptr)
    {
        rxPathOwner = pathBundle->rxPath.UnclassifiedPacketsQueue().Ownership.Acquire();

        pathBundle->rxPath.ProcessRxRing(CurrCpuReceiveQueue);

        if (rxPathOwner)
        {
            isRxBufferShortage = ProcessReceiveQueue(pContext,
                                                     &nPacketsToIndicate,
                                                     &pathBundle->rxPath.UnclassifiedPacketsQueue(),
                                                     &indicate,
                                                     &indicateTail,
                                                     &nIndicate);
        }
    }

#ifdef PARANDIS_SUPPORT_RSS
    if (CurrCpuReceiveQueue != PARANDIS_RECEIVE_NO_QUEUE)
    {
        isRxBufferShortage = ProcessReceiveQueue(pContext,
                                                 &nPacketsToIndicate,
                                                 &pContext->ReceiveQueues[CurrCpuReceiveQueue],
                                                 &indicate,
                                                 &indicateTail,
                                                 &nIndicate);
        res |= ReceiveQueueHasBuffers(&pContext->ReceiveQueues[CurrCpuReceiveQueue]);
    }
#endif

    if (nIndicate)
    {
        if (pContext->m_RxStateMachine.RegisterOutstandingItems(nIndicate))
        {
            if (!isRxBufferShortage)
            {
                NdisMIndicateReceiveNetBufferLists(pContext->MiniportHandle,
                                                   indicate,
                                                   0,
                                                   nIndicate,
                                                   NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
            }
            else
            {
                /* If the number of available RX buffers is insufficient, we set the
                NDIS_RECEIVE_FLAGS_RESOURCES flag so that the RX buffers can be immediately
                reclaimed once the call to NdisMIndicateReceiveNetBufferLists returns. */

                NdisMIndicateReceiveNetBufferLists(pContext->MiniportHandle,
                                                   indicate,
                                                   0,
                                                   nIndicate,
                                                   NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL | NDIS_RECEIVE_FLAGS_RESOURCES);

                NdisInterlockedAddLargeStatistic(&pContext->extraStatistics.rxIndicatesWithResourcesFlag, nIndicate);
                ParaNdis_ReuseRxNBLs(indicate);
                pContext->m_RxStateMachine.UnregisterOutstandingItems(nIndicate);
            }
            NdisInterlockedAddLargeStatistic(&pContext->extraStatistics.totalRxIndicates, nIndicate);
        }
        else
        {
            ParaNdis_ReuseRxNBLs(indicate);
        }
    }

    if (rxPathOwner)
    {
        pathBundle->rxPath.UnclassifiedPacketsQueue().Ownership.Release();
    }

    // we do not need to make a check of rx queue restart etc. if we already know
    // that we need to respawn the DPC to get more data from the queue
    if (pathBundle != nullptr && res == 0)
    {
        res |= pathBundle->rxPath.RestartQueue() |
               ReceiveQueueHasBuffers(&pathBundle->rxPath.UnclassifiedPacketsQueue());
    }

    return res;
}

void RxPoll(PARANDIS_ADAPTER *pContext, UINT BundleIndex, NDIS_POLL_RECEIVE_DATA &RxData)
{
    ULONG &collected = RxData.NumberOfIndicatedNbls;
    ULONG MaxPacketsToIndicate = RxData.MaxNblsToIndicate;
    BOOLEAN hasMore = FALSE;
    bool rxPathOwner = false;
    PNET_BUFFER_LIST indicateTail = nullptr;
    PNET_BUFFER_LIST &indicate = RxData.IndicatedNblChain;
    CPUPathBundle *pathBundle = BundleIndex < pContext->nPathBundles ? &pContext->pPathBundles[BundleIndex] : NULL;

    collected = 0;

    if (pathBundle != nullptr)
    {
        rxPathOwner = pathBundle->rxPath.UnclassifiedPacketsQueue().Ownership.Acquire();

        pathBundle->rxPath.ProcessRxRing((CCHAR)BundleIndex);

        if (rxPathOwner)
        {
            ProcessReceiveQueue(pContext,
                                &MaxPacketsToIndicate,
                                &pathBundle->rxPath.UnclassifiedPacketsQueue(),
                                &indicate,
                                &indicateTail,
                                &collected);
        }
    }

    ProcessReceiveQueue(pContext,
                        &MaxPacketsToIndicate,
                        &pContext->ReceiveQueues[BundleIndex],
                        &indicate,
                        &indicateTail,
                        &collected);
    hasMore |= ReceiveQueueHasBuffers(&pContext->ReceiveQueues[BundleIndex]);

    if (collected)
    {
        if (!pContext->m_RxStateMachine.RegisterOutstandingItems(collected))
        {
            ParaNdis_ReuseRxNBLs(indicate);
            collected = 0;
            indicate = NULL;
        }
        else
        {
            NdisInterlockedAddLargeStatistic(&pContext->extraStatistics.totalRxIndicates, collected);
        }
    }

    if (rxPathOwner)
    {
        pathBundle->rxPath.UnclassifiedPacketsQueue().Ownership.Release();
    }

    // we do not need to make a check of rx queue restart etc. if we already know
    // that we need to respawn the DPC to get more data from the queue
    if (pathBundle != nullptr && !hasMore)
    {
        hasMore |= pathBundle->rxPath.RestartQueue() |
                   ReceiveQueueHasBuffers(&pathBundle->rxPath.UnclassifiedPacketsQueue());
    }
    if (hasMore)
    {
        RxData.NumberOfRemainingNbls = NDIS_ANY_NUMBER_OF_NBLS;
    }
}

void ParaNdis_ReuseRxNBLs(PNET_BUFFER_LIST pNBL)
{
    while (pNBL)
    {
        PNET_BUFFER_LIST pTemp = pNBL;
        pRxNetDescriptor pBuffersDescriptor = (pRxNetDescriptor)pNBL->MiniportReserved[0];
        DPrintf(3, "Returned NBL of pBuffersDescriptor %p", pBuffersDescriptor);
        pNBL = NET_BUFFER_LIST_NEXT_NBL(pNBL);
        NET_BUFFER_LIST_NEXT_NBL(pTemp) = NULL;
        NdisFreeNetBufferList(pTemp);
        pBuffersDescriptor->Queue->ReuseReceiveBuffer(pBuffersDescriptor);
    }
}

void ParaNdis_CXDPCWorkBody(PARANDIS_ADAPTER *pContext)
{
    InterlockedIncrement(&pContext->counterDPCInside);
    if (pContext->bEnableInterruptHandlingDPC)
    {
        UINT8 status = 0;
        status = ReadDeviceStatus(pContext);

        if (virtio_is_feature_enabled(pContext->u64HostFeatures, VIRTIO_F_VERSION_1) &&
            (status & VIRTIO_CONFIG_S_NEEDS_RESET))
        {
            DPrintf(0, "Received VIRTIO_CONFIG_S_NEEDS_RESET event");
            pContext->m_StateMachine.NotifyDeviceNeedsReset();
            pContext->bDeviceNeedsReset = TRUE;
        }

        ReadLinkState(pContext);
        if (pContext->bLinkDetectSupported)
        {
            ReadLinkState(pContext);
            ParaNdis_SynchronizeLinkState(pContext);
        }
        if (pContext->bGuestAnnounceSupported && pContext->bGuestAnnounced)
        {
            ParaNdis_SendGratuitousArpPacket(pContext);
            pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_ANNOUNCE,
                                                VIRTIO_NET_CTRL_ANNOUNCE_ACK,
                                                NULL,
                                                0,
                                                NULL,
                                                0,
                                                0);
            pContext->bGuestAnnounced = FALSE;
        }
        if (pContext->bCXPathCreated)
        {
            // We call CX to process as many pending commands
            // as we can. If we want in future to limit the
            // Maintain by time, need to take care on restarting
            // the CVQ or CX dpc if needed
            pContext->CXPath.Maintain();
        }
    }
    InterlockedDecrement(&pContext->counterDPCInside);
}

bool ParaNdis_RXTXDPCWorkBody(PARANDIS_ADAPTER *pContext, ULONG ulMaxPacketsToIndicate)
{
    bool stillRequiresProcessing = false;
    UINT numOfPacketsToIndicate = min(ulMaxPacketsToIndicate, pContext->uNumberOfHandledRXPacketsInDPC);

    DEBUG_ENTRY(5);

    InterlockedIncrement(&pContext->counterDPCInside);

    CPUPathBundle *pathBundle = nullptr;

    if (pContext->nPathBundles == 1)
    {
        pathBundle = pContext->pPathBundles;
    }
    else
    {
        ULONG procIndex = ParaNdis_GetCurrentCPUIndex();
        if (procIndex < pContext->nPathBundles)
        {
            pathBundle = pContext->pPathBundles + procIndex;
        }
    }
    /* When DPC is scheduled for RSS processing, it may be assigned to CPU that has no
    correspondent path, so pathBundle may remain null. */

    if (pContext->bEnableInterruptHandlingDPC)
    {
        if (RxDPCWorkBody(pContext, pathBundle, numOfPacketsToIndicate))
        {
            stillRequiresProcessing = true;
        }

        if (pathBundle != nullptr && pathBundle->txPath.DoPendingTasks(nullptr))
        {
            stillRequiresProcessing = true;
        }
    }
    InterlockedDecrement(&pContext->counterDPCInside);

    if (pContext->bSharedVectors && pathBundle)
    {
        ParaNdis_CXDPCWorkBody(pContext);
    }
    return stillRequiresProcessing;
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
NDIS_STATUS ParaNdis_SetMulticastList(PARANDIS_ADAPTER *pContext,
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
    else if (length % ETH_ALEN)
    {
        status = NDIS_STATUS_INVALID_LENGTH;
        *pBytesNeeded = (length / ETH_ALEN) * ETH_ALEN;
    }
    else
    {
        NdisZeroMemory(pContext->MulticastData.MulticastList, sizeof(pContext->MulticastData.MulticastList));
        if (length)
        {
            NdisMoveMemory(pContext->MulticastData.MulticastList, Buffer, length);
        }
        pContext->MulticastData.nofMulticastEntries = length / ETH_ALEN;
        DPrintf(1, "New multicast list of %d bytes", length);
        *pBytesRead = length;
        status = NDIS_STATUS_SUCCESS;
    }
    return status;
}

/**********************************************************
Common handler of PnP events
Parameters:
Return value:
***********************************************************/
VOID ParaNdis_OnPnPEvent(PARANDIS_ADAPTER *pContext, NDIS_DEVICE_PNP_EVENT pEvent, PVOID pInfo, ULONG ulSize)
{
    const char *pName = "";

    UNREFERENCED_PARAMETER(pInfo);
    UNREFERENCED_PARAMETER(ulSize);

    DEBUG_ENTRY(0);
#undef MAKECASE
#define MAKECASE(x)                                                                                                    \
    case (x):                                                                                                          \
        pName = #x;                                                                                                    \
        break;
    switch (pEvent)
    {
        MAKECASE(NdisDevicePnPEventQueryRemoved)
        MAKECASE(NdisDevicePnPEventRemoved)
        MAKECASE(NdisDevicePnPEventSurpriseRemoved)
        MAKECASE(NdisDevicePnPEventQueryStopped)
        MAKECASE(NdisDevicePnPEventStopped)
        MAKECASE(NdisDevicePnPEventPowerProfileChanged)
        MAKECASE(NdisDevicePnPEventFilterListChanged)
        default:
            break;
    }
    ParaNdis_DebugHistory(pContext, _etagHistoryLogOperation::hopPnpEvent, NULL, pEvent, 0, 0);
    DPrintf(0, "(%s)", pName);
    if (pEvent == NdisDevicePnPEventSurpriseRemoved)
    {
        // on simulated surprise removal (under PnpTest) we need to reset the device
        // to prevent any access of device queues to memory buffers

        // sync with lazy alloc thread action
        CMutexLockedContext sync(pContext->systemThread.PowerMutex());

        pContext->bSurprizeRemoved = TRUE;
        pContext->m_StateMachine.NotifySupriseRemoved();
        ParaNdis_ResetVirtIONetDevice(pContext);
    }
    pContext->PnpEvents[pContext->nPnpEventIndex++] = pEvent;
    if (pContext->nPnpEventIndex >= sizeof(pContext->PnpEvents) / sizeof(pContext->PnpEvents[0]))
    {
        pContext->nPnpEventIndex = 0;
    }
}

static VOID ParaNdis_DeviceFiltersUpdateRxMode(PARANDIS_ADAPTER *pContext)
{
    u8 val;
    ULONG f = pContext->PacketFilter;
    val = (f & NDIS_PACKET_TYPE_PROMISCUOUS) ? 1 : 0;
    pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_RX, VIRTIO_NET_CTRL_RX_PROMISC, &val, sizeof(val), NULL, 0, 2);
    val = (f & NDIS_PACKET_TYPE_ALL_MULTICAST) ? 1 : 0;
    pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_RX, VIRTIO_NET_CTRL_RX_ALLMULTI, &val, sizeof(val), NULL, 0, 2);

    if (pContext->bCtrlRXExtraFiltersSupported)
    {
        val = (f & (NDIS_PACKET_TYPE_MULTICAST | NDIS_PACKET_TYPE_ALL_MULTICAST)) ? 0 : 1;
        pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_RX,
                                            VIRTIO_NET_CTRL_RX_NOMULTI,
                                            &val,
                                            sizeof(val),
                                            NULL,
                                            0,
                                            2);
        val = (f & NDIS_PACKET_TYPE_DIRECTED) ? 0 : 1;
        pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_RX,
                                            VIRTIO_NET_CTRL_RX_NOUNI,
                                            &val,
                                            sizeof(val),
                                            NULL,
                                            0,
                                            2);
        val = (f & NDIS_PACKET_TYPE_BROADCAST) ? 0 : 1;
        pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_RX,
                                            VIRTIO_NET_CTRL_RX_NOBCAST,
                                            &val,
                                            sizeof(val),
                                            NULL,
                                            0,
                                            2);
    }
}

static VOID ParaNdis_DeviceFiltersUpdateAddresses(PARANDIS_ADAPTER *pContext)
{
    u32 u32UniCastEntries = 0;
    pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_MAC,
                                        VIRTIO_NET_CTRL_MAC_TABLE_SET,
                                        &u32UniCastEntries,
                                        sizeof(u32UniCastEntries),
                                        &pContext->MulticastData,
                                        sizeof(pContext->MulticastData.nofMulticastEntries) + (ULONGLONG)pContext->MulticastData.nofMulticastEntries * ETH_ALEN,
                                        2);
}

static VOID SetSingleVlanFilter(PARANDIS_ADAPTER *pContext, ULONG vlanId, BOOLEAN bOn, int levelIfOK)
{
    u16 val = vlanId & 0xfff;
    UCHAR cmd = bOn ? VIRTIO_NET_CTRL_VLAN_ADD : VIRTIO_NET_CTRL_VLAN_DEL;
    pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_VLAN, cmd, &val, sizeof(val), NULL, 0, levelIfOK);
}

static VOID SetAllVlanFilters(PARANDIS_ADAPTER *pContext, BOOLEAN bOn)
{
    ULONG i;
    for (i = 0; i <= MAX_VLAN_ID; ++i)
    {
        SetSingleVlanFilter(pContext, i, bOn, 7);
    }
}

/*
    possible values of filter set (pContext->ulCurrentVlansFilterSet):
    0 - all disabled
    1..4094 - one selected enabled
    4095 - all enabled
    Note that only 0th vlan can't be enabled
*/
VOID ParaNdis_DeviceFiltersUpdateVlanId(PARANDIS_ADAPTER *pContext)
{
    if (pContext->bCtrlVLANFiltersSupported)
    {
        ULONG newFilterSet;
        if (IsVlanSupported(pContext))
        {
            newFilterSet = pContext->VlanId ? pContext->VlanId : (MAX_VLAN_ID + 1);
        }
        else
        {
            newFilterSet = IsPrioritySupported(pContext) ? (MAX_VLAN_ID + 1) : 0;
        }
        if (newFilterSet != pContext->ulCurrentVlansFilterSet)
        {
            if (pContext->ulCurrentVlansFilterSet > MAX_VLAN_ID)
            {
                SetAllVlanFilters(pContext, FALSE);
            }
            else if (pContext->ulCurrentVlansFilterSet)
            {
                SetSingleVlanFilter(pContext, pContext->ulCurrentVlansFilterSet, FALSE, 2);
            }

            pContext->ulCurrentVlansFilterSet = newFilterSet;

            if (pContext->ulCurrentVlansFilterSet > MAX_VLAN_ID)
            {
                SetAllVlanFilters(pContext, TRUE);
            }
            else if (pContext->ulCurrentVlansFilterSet)
            {
                SetSingleVlanFilter(pContext, pContext->ulCurrentVlansFilterSet, TRUE, 2);
            }
        }
    }
}

VOID ParaNdis_UpdateDeviceFilters(PARANDIS_ADAPTER *pContext)
{
    if (pContext->bCtrlRXFiltersSupported)
    {
        ParaNdis_DeviceFiltersUpdateRxMode(pContext);
        ParaNdis_DeviceFiltersUpdateAddresses(pContext);
    }

    ParaNdis_DeviceFiltersUpdateVlanId(pContext);
}

static VOID ParaNdis_UpdateMAC(PARANDIS_ADAPTER *pContext)
{
    if (pContext->bCtrlMACAddrSupported)
    {
        pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_MAC,
                                            VIRTIO_NET_CTRL_MAC_ADDR_SET,
                                            pContext->CurrentMacAddress,
                                            ETH_ALEN,
                                            NULL,
                                            0,
                                            4);
    }
}

NDIS_STATUS ParaNdis_PowerOn(PARANDIS_ADAPTER *pContext)
{
    UINT i;
    bool bRenewed = true;
    DEBUG_ENTRY(0);
    ParaNdis_DebugHistory(pContext, _etagHistoryLogOperation::hopPowerOn, NULL, 1, 0, 0);

    CMutexLockedContext sync(pContext->systemThread.PowerMutex());

    pContext->m_StateMachine.NotifyPowerOn();

    ParaNdis_ResetVirtIONetDevice(pContext);
    virtio_add_status(&pContext->IODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);
    /* virtio_get_features must be called with any mask once upon device initialization:
     otherwise the device will not work properly */
    (void)virtio_get_features(&pContext->IODevice);
    NTSTATUS nt_status = virtio_set_features(&pContext->IODevice, pContext->u64GuestFeatures);
    if (!NT_SUCCESS(nt_status))
    {
        DPrintf(0, "virtio_set_features failed with %x", nt_status);
        pContext->m_StateMachine.NotifyResumed();
        return NTStatusToNdisStatus(nt_status);
    }

    for (i = 0; i < pContext->nPathBundles; i++)
    {
        bRenewed = bRenewed && pContext->pPathBundles[i].txPath.Renew();
        bRenewed = bRenewed && pContext->pPathBundles[i].rxPath.Renew();
    }
    if (pContext->bCXPathCreated)
    {
        bRenewed = bRenewed && pContext->CXPath.Renew();
    }

    if (!bRenewed)
    {
        DPrintf(0, "one or more queues failed to renew");
        return NDIS_STATUS_RESOURCES;
    }

    ParaNdis_RestoreDeviceConfigurationAfterReset(pContext);

    for (i = 0; i < pContext->nPathBundles; i++)
    {
        pContext->pPathBundles[i].rxPath.PopulateQueue();
    }

    ParaNdis_DeviceEnterD0(pContext);
    ParaNdis_UpdateDeviceFilters(pContext);

    pContext->m_StateMachine.NotifyResumed();

    ParaNdis_DebugHistory(pContext, _etagHistoryLogOperation::hopPowerOn, NULL, 0, 0, 0);

    return NDIS_STATUS_SUCCESS;
}

VOID ParaNdis_PowerOff(PARANDIS_ADAPTER *pContext)
{
    DEBUG_ENTRY(0);
    ParaNdis_DebugHistory(pContext, _etagHistoryLogOperation::hopPowerOff, NULL, 1, 0, 0);

    CMutexLockedContext sync(pContext->systemThread.PowerMutex());

    pContext->m_StateMachine.NotifySuspended();

    pContext->bConnected = FALSE;

    ParaNdis_ResetVirtIONetDevice(pContext);

#if !NDIS_SUPPORT_NDIS620
    // WLK tests for Windows 2008 require media disconnect indication
    // on power off. HCK tests for newer versions require media state unknown
    // indication only and fail on disconnect indication
    ParaNdis_SetLinkState(pContext, MediaConnectStateDisconnected);
#endif
    ParaNdis_SetLinkState(pContext, MediaConnectStateUnknown);

    PreventDPCServicing(pContext);

    /*******************************************************************
        shutdown queues to have all the receive buffers under our control
        all the transmit buffers move to list of free buffers
    ********************************************************************/

    for (UINT i = 0; i < pContext->nPathBundles; i++)
    {
        pContext->pPathBundles[i].txPath.Shutdown();
        pContext->pPathBundles[i].rxPath.Shutdown();
    }

    if (pContext->bCXPathCreated)
    {
        pContext->CXPath.Shutdown();
    }

    ParaNdis_DebugHistory(pContext, _etagHistoryLogOperation::hopPowerOff, NULL, 0, 0, 0);
}

void ParaNdis_CallOnBugCheck(PARANDIS_ADAPTER *pContext)
{
    if (pContext->bDeviceInitialized)
    {
#ifdef DBG_USE_VIRTIO_PCI_ISR_FOR_HOST_REPORT
        WriteVirtIODeviceByte(pContext->IODevice->isr, 1);
#endif
    }
}

tChecksumCheckResult ParaNdis_CheckRxChecksum(PARANDIS_ADAPTER *pContext,
                                              ULONG virtioFlags,
                                              tCompletePhysicalAddress *pPacketPages,
                                              PNET_PACKET_INFO pPacketInfo,
                                              ULONG ulDataOffset,
                                              BOOLEAN verifyLength)
{
    tOffloadSettingsFlags f = pContext->Offload.flags;
    tChecksumCheckResult res;
    tTcpIpPacketParsingResult ppr;
    ULONG ulPacketLength = pPacketInfo->dataLength;
    ULONG flagsToCalculate = 0;
    res.value = 0;

    // VIRTIO_NET_HDR_F_NEEDS_CSUM - we need to calculate TCP/UDP CS
    // VIRTIO_NET_HDR_F_DATA_VALID - host tells us TCP/UDP CS is OK

    if (f.fRxIPChecksum)
    {
        flagsToCalculate |= tPacketOffloadRequest::pcrIpChecksum; // check only
    }

    if (!(virtioFlags & VIRTIO_NET_HDR_F_DATA_VALID))
    {
        if (virtioFlags & VIRTIO_NET_HDR_F_NEEDS_CSUM)
        {
            flagsToCalculate |= tPacketOffloadRequest::pcrFixXxpChecksum | tPacketOffloadRequest::pcrTcpChecksum |
                                tPacketOffloadRequest::pcrUdpChecksum;
        }
        else
        {
            if (f.fRxTCPChecksum)
            {
                flagsToCalculate |= tPacketOffloadRequest::pcrTcpV4Checksum;
            }
            if (f.fRxUDPChecksum)
            {
                flagsToCalculate |= tPacketOffloadRequest::pcrUdpV4Checksum;
            }
            if (f.fRxTCPv6Checksum)
            {
                flagsToCalculate |= tPacketOffloadRequest::pcrTcpV6Checksum;
            }
            if (f.fRxUDPv6Checksum)
            {
                flagsToCalculate |= tPacketOffloadRequest::pcrUdpV6Checksum;
            }
        }
    }

    if (pPacketInfo->isIP4 || pPacketInfo->isIP6)
    {
        ppr = ParaNdis_CheckSumVerify(pPacketPages,
                                      ulPacketLength - ETH_HEADER_SIZE,
                                      ulDataOffset + ETH_HEADER_SIZE,
                                      flagsToCalculate,
                                      verifyLength,
                                      __FUNCTION__);
    }
    else
    {
        ppr.value = 0;
        ppr.ipStatus = static_cast<ULONG>(ppResult::ppresNotIP);
    }

    if (ppr.ipCheckSum == ppResult::ppresIPTooShort || ppr.xxpStatus == ppResult::ppresXxpIncomplete)
    {
        res.flags.IpOK = FALSE;
        res.flags.IpFailed = TRUE;
        return res;
    }

    if (virtioFlags & VIRTIO_NET_HDR_F_DATA_VALID)
    {
        pContext->extraStatistics.framesRxCSHwOK++;
        ppr.xxpCheckSum = static_cast<ULONG>(ppResult::ppresCSOK);
    }

    if (ppr.ipStatus == ppResult::ppresIPV4 && !ppr.IsFragment)
    {
        if (f.fRxIPChecksum)
        {
            res.flags.IpOK = ppr.ipCheckSum == ppResult::ppresCSOK;
            res.flags.IpFailed = ppr.ipCheckSum == ppResult::ppresCSBad;
        }
        if (ppr.xxpStatus == ppResult::ppresXxpKnown)
        {
            if (ppr.TcpUdp == ppResult::ppresIsTCP) /* TCP */
            {
                if (f.fRxTCPChecksum)
                {
                    res.flags.TcpOK = ppr.xxpCheckSum == ppResult::ppresCSOK || ppr.fixedXxpCS;
                    res.flags.TcpFailed = !res.flags.TcpOK;
                }
            }
            else /* UDP */
            {
                if (f.fRxUDPChecksum)
                {
                    res.flags.UdpOK = ppr.xxpCheckSum == ppResult::ppresCSOK || ppr.fixedXxpCS;
                    res.flags.UdpFailed = !res.flags.UdpOK;
                }
            }
        }
    }
    else if (ppr.ipStatus == ppResult::ppresIPV6)
    {
        if (ppr.xxpStatus == ppResult::ppresXxpKnown)
        {
            if (ppr.TcpUdp == ppResult::ppresIsTCP) /* TCP */
            {
                if (f.fRxTCPv6Checksum)
                {
                    res.flags.TcpOK = ppr.xxpCheckSum == ppResult::ppresCSOK || ppr.fixedXxpCS;
                    res.flags.TcpFailed = !res.flags.TcpOK;
                }
            }
            else /* UDP */
            {
                if (f.fRxUDPv6Checksum)
                {
                    res.flags.UdpOK = ppr.xxpCheckSum == ppResult::ppresCSOK || ppr.fixedXxpCS;
                    res.flags.UdpFailed = !res.flags.UdpOK;
                }
            }
        }
    }

    return res;
}

void ParaNdis_PrintCharArray(int DebugPrintLevel, const CCHAR *data, size_t length)
{
    ParaNdis_PrintTable<80, 10>(DebugPrintLevel, data, length, "%d", [](const CCHAR *p) { return *p; });
}

_PARANDIS_ADAPTER::~_PARANDIS_ADAPTER()
{
    guestAnnouncePackets.Clear();
    ParaNdis_CleanupContext(this);
}
