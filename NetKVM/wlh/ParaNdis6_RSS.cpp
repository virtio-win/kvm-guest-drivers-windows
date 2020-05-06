#include "ndis56common.h"
#include "kdebugprint.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis6_RSS.tmh"
#endif

#if PARANDIS_SUPPORT_RSS

#define RSS_PRINT_LEVEL 0

static void PrintIndirectionTable(const NDIS_RECEIVE_SCALE_PARAMETERS* Params);
static void PrintIndirectionTable(const PARANDIS_SCALING_SETTINGS *RSSScalingSetting);

static void PrintRSSSettings(PPARANDIS_RSS_PARAMS RSSParameters);
static NDIS_STATUS ParaNdis_SetupRSSQueueMap(PARANDIS_ADAPTER *pContext);

static VOID ApplySettings(PPARANDIS_RSS_PARAMS RSSParameters,
        PARANDIS_RSS_MODE NewRSSMode,
        PARANDIS_HASHING_SETTINGS *ReceiveHashingSettings,
        PARANDIS_SCALING_SETTINGS *ReceiveScalingSettings)
{
    CNdisPassiveWriteAutoLock autoLock(RSSParameters->rwLock);

    RSSParameters->RSSMode = NewRSSMode;

    if(NewRSSMode != PARANDIS_RSS_DISABLED)
    {
        RSSParameters->ActiveHashingSettings = *ReceiveHashingSettings;

        if(NewRSSMode == PARANDIS_RSS_FULL)
        {
            if(RSSParameters->ActiveRSSScalingSettings.CPUIndexMapping != NULL)
                NdisFreeMemory(RSSParameters->ActiveRSSScalingSettings.CPUIndexMapping, 0, 0);

            RSSParameters->ActiveRSSScalingSettings = *ReceiveScalingSettings;

            ReceiveScalingSettings->CPUIndexMapping = NULL;
        }
    }
}

static VOID InitRSSParameters(PARANDIS_ADAPTER *pContext)
{
    PARANDIS_RSS_PARAMS *RSSParameters = &pContext->RSSParameters;
    NdisZeroMemory(RSSParameters, sizeof(*RSSParameters));
    RSSParameters->pContext = pContext;
    RSSParameters->ReceiveQueuesNumber = pContext->RSSMaxQueuesNumber;
    RSSParameters->RSSScalingSettings.DefaultQueue = PARANDIS_RECEIVE_UNCLASSIFIED_PACKET;
    RSSParameters->ActiveRSSScalingSettings.DefaultQueue = PARANDIS_RECEIVE_UNCLASSIFIED_PACKET;
}

static VOID CleanupRSSParameters(PARANDIS_RSS_PARAMS *RSSParameters)
{
    if(RSSParameters->ActiveRSSScalingSettings.CPUIndexMapping != NULL)
        NdisFreeMemory(RSSParameters->ActiveRSSScalingSettings.CPUIndexMapping, 0, 0);

    if(RSSParameters->RSSScalingSettings.CPUIndexMapping != NULL)
        NdisFreeMemory(RSSParameters->RSSScalingSettings.CPUIndexMapping, 0, 0);

}

static VOID InitRSSCapabilities(PARANDIS_ADAPTER *pContext)
{
    NDIS_RECEIVE_SCALE_CAPABILITIES *RSSCapabilities = &pContext->RSSCapabilities;
    RSSCapabilities->Header.Type = NDIS_OBJECT_TYPE_RSS_CAPABILITIES;
    RSSCapabilities->Header.Revision = NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_1;
    RSSCapabilities->Header.Size = NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_1;
    RSSCapabilities->CapabilitiesFlags =    NDIS_RSS_CAPS_MESSAGE_SIGNALED_INTERRUPTS |
                                        NDIS_RSS_CAPS_CLASSIFICATION_AT_ISR |
                                        NdisHashFunctionToeplitz;
    if (pContext->bRSSSupportedByDevice)
    {
        ULONG flags = 0;
        flags |= (pContext->DeviceRSSCapabilities.SupportedHashes & VIRTIO_NET_RSS_HASH_TYPE_TCPv4) ?
            NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4 : 0;
        flags |= (pContext->DeviceRSSCapabilities.SupportedHashes & VIRTIO_NET_RSS_HASH_TYPE_TCPv6) ?
            NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6 : 0;
        flags |= (pContext->DeviceRSSCapabilities.SupportedHashes & VIRTIO_NET_RSS_HASH_TYPE_TCP_EX) ?
            NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6_EX : 0;
        RSSCapabilities->CapabilitiesFlags |= flags;
    }
    else
    {
        RSSCapabilities->CapabilitiesFlags |= NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4 |
                                              NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6 |
                                              NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6_EX;
    }
    RSSCapabilities->NumberOfInterruptMessages = 1;
    RSSCapabilities->NumberOfReceiveQueues = pContext->RSSMaxQueuesNumber;
#if (NDIS_SUPPORT_NDIS630)
    RSSCapabilities->Header.Revision = NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2;
    RSSCapabilities->Header.Size = NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_2;
    RSSCapabilities->NumberOfIndirectionTableEntries = NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2 / sizeof(PROCESSOR_NUMBER);
#endif
#if (NDIS_SUPPORT_NDIS680)
    if (CheckNdisVersion(6, 80))
    {
        RSSCapabilities->Header.Revision = NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_3;
        RSSCapabilities->Header.Size = NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_3;
        if (pContext->bRSSSupportedByDevice)
        {
            ULONG flags = 0;
            flags |= (pContext->DeviceRSSCapabilities.SupportedHashes & VIRTIO_NET_RSS_HASH_TYPE_UDPv4) ?
                NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV4 : 0;
            flags |= (pContext->DeviceRSSCapabilities.SupportedHashes & VIRTIO_NET_RSS_HASH_TYPE_UDPv6) ?
                NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV6 : 0;
            flags |= (pContext->DeviceRSSCapabilities.SupportedHashes & VIRTIO_NET_RSS_HASH_TYPE_UDP_EX) ?
                NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV6_EX : 0;
            RSSCapabilities->CapabilitiesFlags |= flags;
        }
        else
        {
            RSSCapabilities->CapabilitiesFlags |= NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV4 |
                                                  NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV6 |
                                                  NDIS_RSS_CAPS_HASH_TYPE_UDP_IPV6_EX;
        }
    }
#endif
}

void ParaNdis6_CheckDeviceRSSCapabilities(PARANDIS_ADAPTER *pContext, bool& bRss, bool& bHash)
{
    if (bHash || bRss)
    {
        DPrintf(0, "[%s] Device supports %s %s: key of %d, table of %d, hashes %X\n", __FUNCTION__,
            bHash ? "Hash" : " ", bRss ? "RSS" : " ",
            pContext->DeviceRSSCapabilities.MaxKeySize,
            pContext->DeviceRSSCapabilities.MaxIndirectEntries,
            pContext->DeviceRSSCapabilities.SupportedHashes);
    }
    BOOLEAN bResult = (pContext->DeviceRSSCapabilities.SupportedHashes & VIRTIO_NET_RSS_HASH_TYPE_IPv4) &&
        pContext->DeviceRSSCapabilities.MaxKeySize >= NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2;

    if (!bResult)
    {
        bRss = false;
        bHash = false;
    }

    bResult = bResult && pContext->DeviceRSSCapabilities.MaxIndirectEntries >= NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2 / sizeof(PROCESSOR_NUMBER);
    if (!bResult)
    {
        bRss = false;
    }
    DPrintf(0, "[%s] Driver will use: %s %s\n", __FUNCTION__, bHash ? "Hash" : " ", bRss ? "RSS" : " ");
}

NDIS_RECEIVE_SCALE_CAPABILITIES* ParaNdis6_RSSCreateConfiguration(PARANDIS_ADAPTER *pContext)
{
    InitRSSParameters(pContext);
    InitRSSCapabilities(pContext);
    return &pContext->RSSCapabilities;
}

VOID ParaNdis6_RSSCleanupConfiguration(PARANDIS_RSS_PARAMS *RSSParameters)
{
    CleanupRSSParameters(RSSParameters);
}

static ULONG TranslateHashTypes(ULONG hashSettings)
{
    ULONG val = 0;
    val |= (hashSettings & NDIS_HASH_IPV4) ? VIRTIO_NET_RSS_HASH_TYPE_IPv4 : 0;
    val |= (hashSettings & NDIS_HASH_TCP_IPV4) ? VIRTIO_NET_RSS_HASH_TYPE_TCPv4 : 0;
    val |= (hashSettings & NDIS_HASH_IPV6) ? VIRTIO_NET_RSS_HASH_TYPE_IPv6 : 0;
    val |= (hashSettings & NDIS_HASH_IPV6_EX) ? VIRTIO_NET_RSS_HASH_TYPE_IP_EX : 0;
    val |= (hashSettings & NDIS_HASH_TCP_IPV6) ? VIRTIO_NET_RSS_HASH_TYPE_TCPv6 : 0;
    val |= (hashSettings & NDIS_HASH_TCP_IPV6_EX) ? VIRTIO_NET_RSS_HASH_TYPE_TCP_EX : 0;
#if (NDIS_SUPPORT_NDIS680)
    val |= (hashSettings & NDIS_HASH_UDP_IPV4) ? VIRTIO_NET_RSS_HASH_TYPE_UDPv4 : 0;
    val |= (hashSettings & NDIS_HASH_UDP_IPV6) ? VIRTIO_NET_RSS_HASH_TYPE_UDPv6 : 0;
    val |= (hashSettings & NDIS_HASH_UDP_IPV6_EX) ? VIRTIO_NET_RSS_HASH_TYPE_UDP_EX : 0;
#endif
    TraceNoPrefix(0, "[%s] 0x%X -> 0x%X", __FUNCTION__, hashSettings, val);
    return val;
}

static USHORT ResolveQueue(PARANDIS_ADAPTER *pContext, PPROCESSOR_NUMBER proc, USHORT *fallback)
{
    GROUP_AFFINITY a;
    ParaNdis_ProcessorNumberToGroupAffinity(&a, proc);
    USHORT n;
    for (n = 0; n < pContext->nPathBundles; ++n)
    {
        const GROUP_AFFINITY& b = pContext->pPathBundles[n].rxPath.DPCAffinity;
        if (a.Group == b.Group && a.Mask == b.Mask)
        {
            return n;
        }
    }
    // the CPU is not used by any queue
    n = (*fallback)++;
    if (*fallback >= pContext->nPathBundles)
    {
        *fallback = 0;
    }
    TraceNoPrefix(0, "[%s] fallback CPU %d.%d -> Q%d", __FUNCTION__, proc->Group, proc->Number, n);
    return n;
}

static void SetDeviceRSSSettings(PARANDIS_ADAPTER *pContext, bool bForceOff = false)
{
    if (!pContext->bRSSSupportedByDevice && !pContext->bHashReportedByDevice)
    {
        return;
    }
    UCHAR command = pContext->bRSSSupportedByDevice ?
        VIRTIO_NET_CTRL_MQ_RSS_CONFIG : VIRTIO_NET_CTRL_MQ_HASH_CONFIG;

    if (pContext->RSSParameters.RSSMode == PARANDIS_RSS_DISABLED || bForceOff)
    {
        virtio_net_rss_config cfg = {};
        cfg.max_tx_vq = (USHORT)pContext->nPathBundles;
        pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_MQ, command, &cfg, sizeof(cfg), NULL, 0, 2);
    }
    else
    {
        virtio_net_rss_config *cfg;
        USHORT fallbackQueue = 0;
        USHORT indirection_table_len = pContext->RSSParameters.ActiveRSSScalingSettings.IndirectionTableSize / sizeof(PROCESSOR_NUMBER);
        UCHAR hash_key_len = (UCHAR)pContext->RSSParameters.ActiveHashingSettings.HashSecretKeySize;
        ULONG config_size;
        if (!pContext->bRSSSupportedByDevice)
        {
            indirection_table_len = 1;
        }
        config_size = virtio_net_rss_config_size(indirection_table_len, hash_key_len);
        cfg = (virtio_net_rss_config *)ParaNdis_AllocateMemory(pContext, config_size);
        if (!cfg)
        {
            return;
        }
        cfg->indirection_table_mask = indirection_table_len - 1;
        cfg->unclassified_queue = ResolveQueue(pContext, &pContext->RSSParameters.ActiveRSSScalingSettings.DefaultProcessor, &fallbackQueue);
        for (USHORT i = 0; i < indirection_table_len; ++i)
        {
            cfg->indirection_table[i] = ResolveQueue(pContext,
                pContext->RSSParameters.ActiveRSSScalingSettings.IndirectionTable + i,
                &fallbackQueue);
        }
        TraceNoPrefix(0, "[%s] Translated indirections: (len = %d)\n", __FUNCTION__, indirection_table_len);
        ParaNdis_PrintTable<80, 10>(0, cfg->indirection_table, indirection_table_len, "%d", [](const __u16 *p) {  return *p; });
        max_tx_vq(cfg) = (USHORT)pContext->nPathBundles;
        hash_key_length(cfg) = hash_key_len;
        for (USHORT i = 0; i < hash_key_len; ++i)
        {
            hash_key_data(cfg, i) = pContext->RSSParameters.ActiveHashingSettings.HashSecretKey[i];
        }
        TraceNoPrefix(0, "[%s] RSS key: (len = %d)\n", __FUNCTION__, hash_key_len);
        ParaNdis_PrintTable<80, 10>(0, (hash_key_length_ptr(cfg) + 1), hash_key_len, "%X", [](const __u8 *p) {  return *p; });

        cfg->hash_types = TranslateHashTypes(pContext->RSSParameters.ActiveHashingSettings.HashInformation);

        pContext->CXPath.SendControlMessage(VIRTIO_NET_CTRL_MQ, command, cfg, config_size, NULL, 0, 2);

        NdisFreeMemory(cfg, NULL, 0);
    }
}

static BOOLEAN IsValidHashInfo(ULONG HashInformation)
{
#define HASH_FLAGS_COMBINATION(Type, Flags) ( ((Type) & (Flags)) && !((Type) & ~(Flags)) )

    ULONG ulHashType = NDIS_RSS_HASH_TYPE_FROM_HASH_INFO(HashInformation);
    ULONG ulHashFunction = NDIS_RSS_HASH_FUNC_FROM_HASH_INFO(HashInformation);
    ULONG ulAllowedHashTypes = NDIS_HASH_IPV4 | NDIS_HASH_TCP_IPV4 | NDIS_HASH_IPV6 | NDIS_HASH_TCP_IPV6 |
                                 NDIS_HASH_IPV6_EX | NDIS_HASH_TCP_IPV6_EX;
#if (NDIS_SUPPORT_NDIS680)
    ulAllowedHashTypes |= NDIS_HASH_UDP_IPV4 | NDIS_HASH_UDP_IPV6 | NDIS_HASH_UDP_IPV6_EX;
#endif
    if (HashInformation == 0)
        return TRUE;

    if (HASH_FLAGS_COMBINATION(ulHashType, ulAllowedHashTypes))
        return ulHashFunction == NdisHashFunctionToeplitz;

    return FALSE;
}

static __inline
BOOLEAN IsPowerOfTwo(ULONG n)
{
    return ((n != 0) && ((n & (~n + 1)) == n));
}

static __inline
BOOLEAN IsCompatibleAffinities(PGROUP_AFFINITY a1, PGROUP_AFFINITY a2)
{
    return (a1->Group == a2->Group) && (a1->Mask & a2->Mask);
}

static
CCHAR FindReceiveQueueForCurrentCpu(PPARANDIS_SCALING_SETTINGS RSSScalingSettings)
{
    ULONG CurrProcIdx;

    CurrProcIdx = ParaNdis_GetCurrentCPUIndex();

    if(CurrProcIdx >= RSSScalingSettings->CPUIndexMappingSize)
        return PARANDIS_RECEIVE_NO_QUEUE;

    return RSSScalingSettings->CPUIndexMapping[CurrProcIdx];
}

static
void SetDefaultQueue(PPARANDIS_SCALING_SETTINGS RSSScalingSettings, ULONG idx)
{
    if (idx < RSSScalingSettings->CPUIndexMappingSize)
    {
        NTSTATUS status;
        status = KeGetProcessorNumberFromIndex(idx, &RSSScalingSettings->DefaultProcessor);
        if (NT_SUCCESS(status))
        {
            RSSScalingSettings->DefaultQueue = RSSScalingSettings->CPUIndexMapping[idx];
            return;
        }
    }
    RSSScalingSettings->DefaultQueue = PARANDIS_RECEIVE_UNCLASSIFIED_PACKET;
}

static
BOOLEAN AllocateCPUMappingArray(PARANDIS_ADAPTER *pContext, PPARANDIS_SCALING_SETTINGS RSSScalingSettings)
{
    ULONG i;
    ULONG CPUNumber = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    PCHAR NewCPUMappingArray = (PCHAR)ParaNdis_AllocateMemory(pContext, sizeof(CCHAR) * CPUNumber);

    if(!NewCPUMappingArray)
        return FALSE;

    RSSScalingSettings->CPUIndexMapping = NewCPUMappingArray;
    RSSScalingSettings->CPUIndexMappingSize = CPUNumber;

    for(i = 0; i < CPUNumber; i++)
    {
        RSSScalingSettings->CPUIndexMapping[i] = PARANDIS_RECEIVE_NO_QUEUE;
    }

    return TRUE;
}

static
VOID FillCPUMappingArray(
                            PPARANDIS_SCALING_SETTINGS RSSScalingSettings,
                            CCHAR ReceiveQueuesNumber)
{
    ULONG i;
    CCHAR ReceiveQueue = PARANDIS_FIRST_RSS_RECEIVE_QUEUE;
    auto IndirectionTableChanged = false;

    RSSScalingSettings->FirstQueueIndirectionIndex = INVALID_INDIRECTION_INDEX;

    for (i = 0; i < RSSScalingSettings->IndirectionTableSize/sizeof(PROCESSOR_NUMBER); i++)
    {
        PPROCESSOR_NUMBER ProcNum = &RSSScalingSettings->IndirectionTable[i];
        ULONG CurrProcIdx = KeGetProcessorIndexFromNumber(ProcNum);

        if(CurrProcIdx != INVALID_PROCESSOR_INDEX)
        {
            if (RSSScalingSettings->CPUIndexMapping[CurrProcIdx] == PARANDIS_RECEIVE_NO_QUEUE)
            {
                if (ReceiveQueue == PARANDIS_FIRST_RSS_RECEIVE_QUEUE)
                    RSSScalingSettings->FirstQueueIndirectionIndex = i;

                if (ReceiveQueue != ReceiveQueuesNumber)
                {
                    RSSScalingSettings->CPUIndexMapping[CurrProcIdx] = ReceiveQueue++;
                }
            }

            RSSScalingSettings->QueueIndirectionTable[i] = RSSScalingSettings->CPUIndexMapping[CurrProcIdx];

        }
        else
        {
            RSSScalingSettings->QueueIndirectionTable[i] = PARANDIS_RECEIVE_NO_QUEUE;
        }
    }

    if (RSSScalingSettings->FirstQueueIndirectionIndex == INVALID_INDIRECTION_INDEX)
    {
        DPrintf(0, "[%s] - CPU <-> queue assignment failed!", __FUNCTION__);
        return;
    }

    for (i = 0; i < RSSScalingSettings->IndirectionTableSize / sizeof(PROCESSOR_NUMBER); i++)
    {
        if (RSSScalingSettings->QueueIndirectionTable[i] == PARANDIS_RECEIVE_NO_QUEUE)
        {
            /* If some hash values remains unassigned after the first pass, either because
            mapping processor number to index failed or there are not enough queues,
            reassign the hash values to the first queue*/
            RSSScalingSettings->QueueIndirectionTable[i] = PARANDIS_FIRST_RSS_RECEIVE_QUEUE;
            RSSScalingSettings->IndirectionTable[i] = RSSScalingSettings->IndirectionTable[RSSScalingSettings->FirstQueueIndirectionIndex];
            IndirectionTableChanged = true;
        }
    }

    if (IndirectionTableChanged)
    {
        DPrintf(0, "[%s] Indirection table changed\n", __FUNCTION__);
    }
}

static ULONG MinimalRssParametersLength(const NDIS_RECEIVE_SCALE_PARAMETERS* Params)
{
    if (Params->Header.Revision == NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2)
    {
        return NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2;
    }
#if (NDIS_SUPPORT_NDIS660)
    // we can receive structure rev.3
    if (Params->Header.Revision == NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_3)
    {
        return NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_3;
    }
#endif
    return sizeof(*Params);
}

NDIS_STATUS ParaNdis6_RSSSetParameters( PARANDIS_ADAPTER *pContext,
                                        const NDIS_RECEIVE_SCALE_PARAMETERS* Params,
                                        UINT ParamsLength,
                                        PUINT ParamsBytesRead)
{
    PARANDIS_RSS_PARAMS *RSSParameters = &pContext->RSSParameters;
    ULONG ProcessorMasksSize;
    ULONG IndirectionTableEntries;
    ULONG minimalLength;

    *ParamsBytesRead = 0;

    if (ParamsLength < sizeof(Params->Header))
    {
        DPrintf(0, "[%s] invalid length %d!\n", __FUNCTION__, ParamsLength);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    minimalLength = MinimalRssParametersLength(Params);

    if (ParamsLength < minimalLength)
    {
        DPrintf(0, "[%s] invalid length (1) %d < %d!\n", __FUNCTION__, ParamsLength, minimalLength);
        *ParamsBytesRead = sizeof(Params->Header);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    if ((RSSParameters->RSSMode == PARANDIS_RSS_HASHING) &&
        !(Params->Flags & NDIS_RSS_PARAM_FLAG_DISABLE_RSS) &&
        (Params->HashInformation != 0))
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED) &&
        !IsValidHashInfo(Params->HashInformation))
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    CNdisPassiveWriteAutoLock autoLock(RSSParameters->rwLock);

    if(Params->Flags & NDIS_RSS_PARAM_FLAG_DISABLE_RSS || (Params->HashInformation == 0))
    {
        ApplySettings(RSSParameters, PARANDIS_RSS_DISABLED, NULL, NULL);
    }
    else
    {
        BOOLEAN bHasDefaultCPU = false;
        ULONG defaultCPUIndex = INVALID_PROCESSOR_INDEX;
#if (NDIS_SUPPORT_NDIS660)
        // we can receive structure rev.3
        if (Params->Header.Revision >= NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_3 &&
           !(Params->Flags & NDIS_RSS_PARAM_FLAG_DEFAULT_PROCESSOR_UNCHANGED))
        {
            PROCESSOR_NUMBER num = Params->DefaultProcessorNumber;
            // due to unknown reason the Reserved field might be not zero
            // (we see it on Win10.18362), this causes KeGetProcessorIndexFromNumber
            // to return INVALID_PROCESSOR_INDEX
            num.Reserved = 0;
            bHasDefaultCPU = true;
            defaultCPUIndex = KeGetProcessorIndexFromNumber(&num);
            TraceNoPrefix(0, "[%s] has default CPU idx %d\n", __FUNCTION__, defaultCPUIndex);
        }
#endif

        DPrintf(0, "[%s] RSS Params: flags 0x%4.4x, hash information 0x%4.4lx\n",
            __FUNCTION__, Params->Flags, Params->HashInformation);

        IndirectionTableEntries = Params->IndirectionTableSize / sizeof(PROCESSOR_NUMBER);

        if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED) &&
            ((Params->IndirectionTableSize > sizeof(RSSParameters->RSSScalingSettings.IndirectionTable)) ||
            (ParamsLength < (Params->IndirectionTableOffset + Params->IndirectionTableSize)) ||
                !IsPowerOfTwo(IndirectionTableEntries))
            )
        {
            DPrintf(0, "[%s] invalid length (2), flags %x\n", __FUNCTION__, Params->Flags);
            return NDIS_STATUS_INVALID_LENGTH;
        }

        if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED) &&
            ((Params->HashSecretKeySize > sizeof(RSSParameters->RSSHashingSettings.HashSecretKey)) ||
            (ParamsLength < (Params->HashSecretKeyOffset + Params->HashSecretKeySize)))
            )
        {
            DPrintf(0, "[%s] invalid length (3), flags %x\n", __FUNCTION__, Params->Flags);
            return NDIS_STATUS_INVALID_LENGTH;
        }

        ProcessorMasksSize = Params->NumberOfProcessorMasks * Params->ProcessorMasksEntrySize;
        if (ParamsLength < Params->ProcessorMasksOffset + ProcessorMasksSize)
        {
            DPrintf(0, "[%s] invalid length (4), flags %x\n", __FUNCTION__, Params->Flags);
            return NDIS_STATUS_INVALID_LENGTH;
        }

        if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED))
        {
            PrintIndirectionTable(Params);

            if(!AllocateCPUMappingArray(pContext, &RSSParameters->RSSScalingSettings))
                return NDIS_STATUS_RESOURCES;

            RSSParameters->RSSScalingSettings.IndirectionTableSize = Params->IndirectionTableSize;
            NdisMoveMemory(RSSParameters->RSSScalingSettings.IndirectionTable,
                (char*)Params + Params->IndirectionTableOffset, Params->IndirectionTableSize);
            RSSParameters->RSSScalingSettings.RSSHashMask = IndirectionTableEntries - 1;

            *ParamsBytesRead += Params->IndirectionTableSize;
            *ParamsBytesRead += ProcessorMasksSize;

            FillCPUMappingArray(&RSSParameters->RSSScalingSettings, RSSParameters->ReceiveQueuesNumber);

            if (bHasDefaultCPU)
            {
                SetDefaultQueue(&RSSParameters->RSSScalingSettings, defaultCPUIndex);
            }
            PrintRSSSettings(RSSParameters);
        }
        else if (bHasDefaultCPU)
        {
            SetDefaultQueue(&RSSParameters->ActiveRSSScalingSettings, defaultCPUIndex);
            RSSParameters->RSSScalingSettings.DefaultQueue = RSSParameters->ActiveRSSScalingSettings.DefaultQueue;
            RSSParameters->RSSScalingSettings.DefaultProcessor = RSSParameters->ActiveRSSScalingSettings.DefaultProcessor;
            TraceNoPrefix(0, "[%s] default queue -> %d\n", __FUNCTION__, RSSParameters->ActiveRSSScalingSettings.DefaultQueue);
        }

        if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED))
            RSSParameters->RSSHashingSettings.HashInformation = Params->HashInformation;

        if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED))
        {
            RSSParameters->RSSHashingSettings.HashSecretKeySize = Params->HashSecretKeySize;

            NdisMoveMemory(RSSParameters->RSSHashingSettings.HashSecretKey,
                (char*)Params + Params->HashSecretKeyOffset, Params->HashSecretKeySize);

            *ParamsBytesRead += Params->HashSecretKeySize;
        }

        ApplySettings(RSSParameters,
                    PARANDIS_RSS_FULL,
                    &RSSParameters->RSSHashingSettings,
                    &RSSParameters->RSSScalingSettings);
    }

    *ParamsBytesRead += minimalLength;
#if (NDIS_SUPPORT_NDIS680)
    if (CheckNdisVersion(6, 80))
    {
        // simplify this on Win10
        *ParamsBytesRead = ParamsLength;
    }
#endif
    ParaNdis_ResetRxClassification(pContext);

    NDIS_STATUS status = ParaNdis_SetupRSSQueueMap(pContext);

    if (NT_SUCCESS(status))
    {
        SetDeviceRSSSettings(pContext);
    }

    return status;
}

ULONG ParaNdis6_QueryReceiveHash(const PARANDIS_RSS_PARAMS *RSSParameters,
                                 RSS_HASH_KEY_PARAMETERS *RSSHashKeyParameters)
{
    CNdisPassiveReadAutoLock autoLock(RSSParameters->rwLock);

    NdisZeroMemory(RSSHashKeyParameters, sizeof(*RSSHashKeyParameters));
    RSSHashKeyParameters->ReceiveHashParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    RSSHashKeyParameters->ReceiveHashParameters.Header.Revision = NDIS_RECEIVE_HASH_PARAMETERS_REVISION_1;
    RSSHashKeyParameters->ReceiveHashParameters.Header.Size  = NDIS_SIZEOF_RECEIVE_HASH_PARAMETERS_REVISION_1;
    RSSHashKeyParameters->ReceiveHashParameters.HashInformation = RSSParameters->ReceiveHashingSettings.HashInformation;

    if(RSSParameters->RSSMode == PARANDIS_RSS_HASHING)
        RSSHashKeyParameters->ReceiveHashParameters.Flags = NDIS_RECEIVE_HASH_FLAG_ENABLE_HASH;

    RSSHashKeyParameters->ReceiveHashParameters.HashSecretKeySize = RSSParameters->ReceiveHashingSettings.HashSecretKeySize;

    NdisMoveMemory(RSSHashKeyParameters->HashSecretKey,
        RSSParameters->ReceiveHashingSettings.HashSecretKey,
        RSSHashKeyParameters->ReceiveHashParameters.HashSecretKeySize);

    RSSHashKeyParameters->ReceiveHashParameters.HashSecretKeyOffset = FIELD_OFFSET(RSS_HASH_KEY_PARAMETERS, HashSecretKey);

    return sizeof(RSSHashKeyParameters->ReceiveHashParameters) + RSSHashKeyParameters->ReceiveHashParameters.HashSecretKeySize;
}

NDIS_STATUS ParaNdis6_RSSSetReceiveHash(PARANDIS_ADAPTER *pContext,
                                        const NDIS_RECEIVE_HASH_PARAMETERS* Params,
                                        UINT ParamsLength,
                                        PUINT ParamsBytesRead)
{
    PARANDIS_RSS_PARAMS *RSSParameters = &pContext->RSSParameters;
    CNdisPassiveWriteAutoLock autoLock(RSSParameters->rwLock);

    if (ParamsLength < sizeof(NDIS_RECEIVE_HASH_PARAMETERS))
        return NDIS_STATUS_INVALID_LENGTH;

    *ParamsBytesRead += sizeof(NDIS_RECEIVE_HASH_PARAMETERS);

    if (RSSParameters->RSSMode == PARANDIS_RSS_FULL)
    {
        //Here we check that originator doesn't try to enable hashing while full RSS is on.
        //Disable hashing abd clear parameters is legitimate operation hovewer
        if(Params->Flags & NDIS_RECEIVE_HASH_FLAG_ENABLE_HASH)
        {
            if(!(Params->Flags & NDIS_RECEIVE_HASH_FLAG_HASH_INFO_UNCHANGED) && (Params->HashInformation != 0))
                return NDIS_STATUS_NOT_SUPPORTED;
            if((Params->Flags & NDIS_RECEIVE_HASH_FLAG_HASH_INFO_UNCHANGED) &&
                (RSSParameters->ReceiveHashingSettings.HashInformation != 0))
                return NDIS_STATUS_NOT_SUPPORTED;
        }
    }

    if (!(Params->Flags & NDIS_RECEIVE_HASH_FLAG_HASH_INFO_UNCHANGED) &&
        (!IsValidHashInfo(Params->HashInformation)))
        return NDIS_STATUS_INVALID_PARAMETER;

    if ( (!(Params->Flags & NDIS_RECEIVE_HASH_FLAG_HASH_KEY_UNCHANGED)) &&
         ( (Params->HashSecretKeySize > NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2) ||
           (ParamsLength < (Params->HashSecretKeyOffset + Params->HashSecretKeySize)) )
        )
        return NDIS_STATUS_INVALID_LENGTH;

    if (!(Params->Flags & NDIS_RECEIVE_HASH_FLAG_HASH_INFO_UNCHANGED))
    {
        RSSParameters->ReceiveHashingSettings.HashInformation = Params->HashInformation;
    }

    if (!(Params->Flags & NDIS_RECEIVE_HASH_FLAG_HASH_KEY_UNCHANGED))
    {
        RSSParameters->ReceiveHashingSettings.HashSecretKeySize = Params->HashSecretKeySize;

        NdisMoveMemory(RSSParameters->ReceiveHashingSettings.HashSecretKey,
            (char*)Params + Params->HashSecretKeyOffset, Params->HashSecretKeySize);

        *ParamsBytesRead += Params->HashSecretKeySize;
    }

    if(RSSParameters->RSSMode != PARANDIS_RSS_FULL)
    {
        ApplySettings(RSSParameters,
                ((Params->Flags & NDIS_RECEIVE_HASH_FLAG_ENABLE_HASH) && (Params->HashInformation != 0))
                    ? PARANDIS_RSS_HASHING : PARANDIS_RSS_DISABLED,
                &RSSParameters->ReceiveHashingSettings, NULL);
    }

    ParaNdis_ResetRxClassification(pContext);

    SetDeviceRSSSettings(pContext);

    return NDIS_STATUS_SUCCESS;
}

typedef struct _tagHASH_CALC_SG_BUF_ENTRY
{
    PCHAR chunkPtr;
    ULONG  chunkLen;
} HASH_CALC_SG_BUF_ENTRY, *PHASH_CALC_SG_BUF_ENTRY;

// Little Endian version ONLY
static
UINT32 ToeplitzHash(const PHASH_CALC_SG_BUF_ENTRY sgBuff, int sgEntriesNum, PCCHAR fullKey)
{
#define TOEPLITZ_MAX_BIT_NUM (7)
#define TOEPLITZ_BYTE_HAS_BIT(byte, bit) ((byte) & (1 << (TOEPLITZ_MAX_BIT_NUM - (bit))))
#define TOEPLITZ_BYTE_BIT_STATE(byte, bit) (((byte) >> (TOEPLITZ_MAX_BIT_NUM - (bit))) & 1)

    UINT32 firstKeyWord, res = 0;
    UINT byte, bit;
    PHASH_CALC_SG_BUF_ENTRY sgEntry;
    PCCHAR next_key_byte = fullKey + sizeof(firstKeyWord);
    firstKeyWord = RtlUlongByteSwap(*(UINT32*)fullKey);

    for(sgEntry = sgBuff; sgEntry < sgBuff + sgEntriesNum; ++sgEntry)
    {
        for (byte = 0; byte < sgEntry->chunkLen; ++byte)
        {
            for (bit = 0; bit <= TOEPLITZ_MAX_BIT_NUM; ++bit)
            {
                if (TOEPLITZ_BYTE_HAS_BIT(sgEntry->chunkPtr[byte], bit))
                {
                    res ^= firstKeyWord;
                }
                firstKeyWord = (firstKeyWord << 1) | TOEPLITZ_BYTE_BIT_STATE(*next_key_byte, bit);
            }
            ++next_key_byte;
        }
    }
    return res;

#undef TOEPLITZ_BYTE_HAS_BIT
#undef TOEPLITZ_BYTE_BIT_STATE
#undef TOEPLITZ_MAX_BIT_NUM
}

static __inline
IPV6_ADDRESS* GetIP6SrcAddrForHash(
                            PVOID dataBuffer,
                            PNET_PACKET_INFO packetInfo,
                            ULONG hashTypes)
{
    return ((hashTypes & (NDIS_HASH_TCP_IPV6_EX | NDIS_HASH_IPV6_EX)) && packetInfo->ip6HomeAddrOffset)
        ? (IPV6_ADDRESS*) RtlOffsetToPointer(dataBuffer, packetInfo->ip6HomeAddrOffset)
        : (IPV6_ADDRESS*) RtlOffsetToPointer(dataBuffer, packetInfo->L2HdrLen + FIELD_OFFSET(IPv6Header, ip6_src_address));
}

static __inline
IPV6_ADDRESS* GetIP6DstAddrForHash(
                            PVOID dataBuffer,
                            PNET_PACKET_INFO packetInfo,
                            ULONG hashTypes)
{
    return ((hashTypes & (NDIS_HASH_TCP_IPV6_EX | NDIS_HASH_IPV6_EX)) && packetInfo->ip6DestAddrOffset)
        ? (IPV6_ADDRESS*) RtlOffsetToPointer(dataBuffer, packetInfo->ip6DestAddrOffset)
        : (IPV6_ADDRESS*) RtlOffsetToPointer(dataBuffer, packetInfo->L2HdrLen + FIELD_OFFSET(IPv6Header, ip6_dst_address));
}

static
VOID RSSCalcHash_Unsafe(
                PARANDIS_RSS_PARAMS *RSSParameters,
                PVOID dataBuffer,
                PNET_PACKET_INFO packetInfo)
{
    HASH_CALC_SG_BUF_ENTRY sgBuff[3];
    ULONG hashTypes = NDIS_RSS_HASH_TYPE_FROM_HASH_INFO(RSSParameters->ActiveHashingSettings.HashInformation);

    if(packetInfo->isIP4)
    {
        if(packetInfo->isTCP && (hashTypes & NDIS_HASH_TCP_IPV4))
        {
            IPv4Header *pIpHeader = (IPv4Header *) RtlOffsetToPointer(dataBuffer, packetInfo->L2HdrLen);
            TCPHeader *pTCPHeader = (TCPHeader *) RtlOffsetToPointer(pIpHeader, packetInfo->L3HdrLen);

            sgBuff[0].chunkPtr = RtlOffsetToPointer(pIpHeader, FIELD_OFFSET(IPv4Header, ip_src));
            sgBuff[0].chunkLen = RTL_FIELD_SIZE(IPv4Header, ip_src) + RTL_FIELD_SIZE(IPv4Header, ip_dest);
            sgBuff[1].chunkPtr = RtlOffsetToPointer(pTCPHeader, FIELD_OFFSET(TCPHeader, tcp_src));
            sgBuff[1].chunkLen = RTL_FIELD_SIZE(TCPHeader, tcp_src) + RTL_FIELD_SIZE(TCPHeader, tcp_dest);

            packetInfo->RSSHash.Value = ToeplitzHash(sgBuff, 2, &RSSParameters->ActiveHashingSettings.HashSecretKey[0]);
            packetInfo->RSSHash.Type = NDIS_HASH_TCP_IPV4;
            packetInfo->RSSHash.Function = NdisHashFunctionToeplitz;
            return;
        }

#if (NDIS_SUPPORT_NDIS680)
        if (packetInfo->isUDP && (hashTypes & NDIS_HASH_UDP_IPV4))
        {
            IPv4Header *pIpHeader = (IPv4Header *)RtlOffsetToPointer(dataBuffer, packetInfo->L2HdrLen);
            UDPHeader *pUDPHeader = (UDPHeader *)RtlOffsetToPointer(pIpHeader, packetInfo->L3HdrLen);

            sgBuff[0].chunkPtr = RtlOffsetToPointer(pIpHeader, FIELD_OFFSET(IPv4Header, ip_src));
            sgBuff[0].chunkLen = RTL_FIELD_SIZE(IPv4Header, ip_src) + RTL_FIELD_SIZE(IPv4Header, ip_dest);
            sgBuff[1].chunkPtr = RtlOffsetToPointer(pUDPHeader, FIELD_OFFSET(UDPHeader, udp_src));
            sgBuff[1].chunkLen = RTL_FIELD_SIZE(UDPHeader, udp_src) + RTL_FIELD_SIZE(UDPHeader, udp_dest);

            packetInfo->RSSHash.Value = ToeplitzHash(sgBuff, 2, RSSParameters->ActiveHashingSettings.HashSecretKey);
            packetInfo->RSSHash.Type = NDIS_HASH_UDP_IPV4;
            packetInfo->RSSHash.Function = NdisHashFunctionToeplitz;
            return;
        }
#endif

        if(hashTypes & NDIS_HASH_IPV4)
        {
            sgBuff[0].chunkPtr = RtlOffsetToPointer(dataBuffer, packetInfo->L2HdrLen + FIELD_OFFSET(IPv4Header, ip_src));
            sgBuff[0].chunkLen = RTL_FIELD_SIZE(IPv4Header, ip_src) + RTL_FIELD_SIZE(IPv4Header, ip_dest);

            packetInfo->RSSHash.Value = ToeplitzHash(sgBuff, 1, RSSParameters->ActiveHashingSettings.HashSecretKey);
            packetInfo->RSSHash.Type = NDIS_HASH_IPV4;
            packetInfo->RSSHash.Function = NdisHashFunctionToeplitz;
            return;
        }
    }
    else if(packetInfo->isIP6)
    {
        if(packetInfo->isTCP)
        {
            if(hashTypes & (NDIS_HASH_TCP_IPV6 | NDIS_HASH_TCP_IPV6_EX))
            {
                IPv6Header *pIpHeader =  (IPv6Header *) RtlOffsetToPointer(dataBuffer, packetInfo->L2HdrLen);
                TCPHeader  *pTCPHeader = (TCPHeader *) RtlOffsetToPointer(pIpHeader, packetInfo->L3HdrLen);

                sgBuff[0].chunkPtr = (PCHAR) GetIP6SrcAddrForHash(dataBuffer, packetInfo, hashTypes);
                sgBuff[0].chunkLen = RTL_FIELD_SIZE(IPv6Header, ip6_src_address);
                sgBuff[1].chunkPtr = (PCHAR) GetIP6DstAddrForHash(dataBuffer, packetInfo, hashTypes);
                sgBuff[1].chunkLen = RTL_FIELD_SIZE(IPv6Header, ip6_dst_address);
                sgBuff[2].chunkPtr = RtlOffsetToPointer(pTCPHeader, FIELD_OFFSET(TCPHeader, tcp_src));
                sgBuff[2].chunkLen = RTL_FIELD_SIZE(TCPHeader, tcp_src) + RTL_FIELD_SIZE(TCPHeader, tcp_dest);

                packetInfo->RSSHash.Value = ToeplitzHash(sgBuff, 3, RSSParameters->ActiveHashingSettings.HashSecretKey);
                packetInfo->RSSHash.Type = (hashTypes & NDIS_HASH_TCP_IPV6_EX) ? NDIS_HASH_TCP_IPV6_EX : NDIS_HASH_TCP_IPV6;
                packetInfo->RSSHash.Function = NdisHashFunctionToeplitz;
                return;
            }
        }

#if (NDIS_SUPPORT_NDIS680)
        if (packetInfo->isUDP && (hashTypes & (NDIS_HASH_UDP_IPV6 | NDIS_HASH_UDP_IPV6_EX)))
        {
            IPv6Header *pIpHeader = (IPv6Header *)RtlOffsetToPointer(dataBuffer, packetInfo->L2HdrLen);
            UDPHeader  *pUDPHeader = (UDPHeader *)RtlOffsetToPointer(pIpHeader, packetInfo->L3HdrLen);

            sgBuff[0].chunkPtr = (PCHAR)GetIP6SrcAddrForHash(dataBuffer, packetInfo, hashTypes);
            sgBuff[0].chunkLen = RTL_FIELD_SIZE(IPv6Header, ip6_src_address);
            sgBuff[1].chunkPtr = (PCHAR)GetIP6DstAddrForHash(dataBuffer, packetInfo, hashTypes);
            sgBuff[1].chunkLen = RTL_FIELD_SIZE(IPv6Header, ip6_dst_address);
            sgBuff[2].chunkPtr = RtlOffsetToPointer(pUDPHeader, FIELD_OFFSET(UDPHeader, udp_src));
            sgBuff[2].chunkLen = RTL_FIELD_SIZE(UDPHeader, udp_src) + RTL_FIELD_SIZE(UDPHeader, udp_dest);

            packetInfo->RSSHash.Value = ToeplitzHash(sgBuff, 3, RSSParameters->ActiveHashingSettings.HashSecretKey);
            packetInfo->RSSHash.Type = (hashTypes & NDIS_HASH_UDP_IPV6_EX) ? NDIS_HASH_UDP_IPV6_EX : NDIS_HASH_UDP_IPV6;
            packetInfo->RSSHash.Function = NdisHashFunctionToeplitz;
            return;
        }
#endif

        if(hashTypes & (NDIS_HASH_IPV6 | NDIS_HASH_IPV6_EX))
        {
            sgBuff[0].chunkPtr = (PCHAR) GetIP6SrcAddrForHash(dataBuffer, packetInfo, hashTypes);
            sgBuff[0].chunkLen = RTL_FIELD_SIZE(IPv6Header, ip6_src_address);
            sgBuff[1].chunkPtr = (PCHAR) GetIP6DstAddrForHash(dataBuffer, packetInfo, hashTypes);
            sgBuff[1].chunkLen = RTL_FIELD_SIZE(IPv6Header, ip6_dst_address);

            packetInfo->RSSHash.Value = ToeplitzHash(sgBuff, 2, RSSParameters->ActiveHashingSettings.HashSecretKey);
            packetInfo->RSSHash.Type = (hashTypes & NDIS_HASH_IPV6_EX) ? NDIS_HASH_IPV6_EX : NDIS_HASH_IPV6;
            packetInfo->RSSHash.Function = NdisHashFunctionToeplitz;
            return;
        }

        if(hashTypes & NDIS_HASH_IPV6)
        {
            IPv6Header *pIpHeader = (IPv6Header *) RtlOffsetToPointer(dataBuffer, packetInfo->L2HdrLen);

            sgBuff[0].chunkPtr = RtlOffsetToPointer(pIpHeader, FIELD_OFFSET(IPv6Header, ip6_src_address));
            sgBuff[0].chunkLen = RTL_FIELD_SIZE(IPv6Header, ip6_src_address) + RTL_FIELD_SIZE(IPv6Header, ip6_dst_address);

            packetInfo->RSSHash.Value = ToeplitzHash(sgBuff, 2, RSSParameters->ActiveHashingSettings.HashSecretKey);
            packetInfo->RSSHash.Type = NDIS_HASH_IPV6;
            packetInfo->RSSHash.Function = NdisHashFunctionToeplitz;
            return;
        }
    }

    packetInfo->RSSHash.Value = 0;
    packetInfo->RSSHash.Type = 0;
    packetInfo->RSSHash.Function = 0;
}

VOID ParaNdis6_RSSAnalyzeReceivedPacket(
    PARANDIS_RSS_PARAMS *RSSParameters,
    PVOID dataBuffer,
    PNET_PACKET_INFO packetInfo)
{
    CNdisDispatchReadAutoLock autoLock(RSSParameters->rwLock);

    if(RSSParameters->RSSMode != PARANDIS_RSS_DISABLED)
    {
        RSSCalcHash_Unsafe(RSSParameters, dataBuffer, packetInfo);
    }
}

CCHAR ParaNdis6_RSSGetScalingDataForPacket(
    PARANDIS_RSS_PARAMS *RSSParameters,
    PNET_PACKET_INFO packetInfo,
    PPROCESSOR_NUMBER targetProcessor)
{
    CCHAR targetQueue;
    CNdisDispatchReadAutoLock autoLock(RSSParameters->rwLock);

    if (RSSParameters->RSSMode != PARANDIS_RSS_FULL || 
        RSSParameters->ActiveRSSScalingSettings.FirstQueueIndirectionIndex == INVALID_INDIRECTION_INDEX)
    {
        targetQueue = PARANDIS_RECEIVE_UNCLASSIFIED_PACKET;
    }
    else if (packetInfo->RSSHash.Type == 0)
    {
        targetQueue = RSSParameters->ActiveRSSScalingSettings.DefaultQueue;
        *targetProcessor = RSSParameters->ActiveRSSScalingSettings.DefaultProcessor;
    }
    else
    {
        ULONG indirectionIndex = packetInfo->RSSHash.Value & RSSParameters->ActiveRSSScalingSettings.RSSHashMask;

        targetQueue = RSSParameters->ActiveRSSScalingSettings.QueueIndirectionTable[indirectionIndex];

        if (targetQueue == PARANDIS_RECEIVE_NO_QUEUE)
        {
            targetQueue = PARANDIS_RECEIVE_UNCLASSIFIED_PACKET;
        }
        else
        {
            *targetProcessor = RSSParameters->ActiveRSSScalingSettings.IndirectionTable[indirectionIndex];
        }
    }

    return targetQueue;
}

CCHAR ParaNdis6_RSSGetCurrentCpuReceiveQueue(PARANDIS_RSS_PARAMS *RSSParameters)
{
    CCHAR res;
    CNdisDispatchReadAutoLock autoLock(RSSParameters->rwLock);

    if(RSSParameters->RSSMode != PARANDIS_RSS_FULL)
    {
        res = PARANDIS_RECEIVE_NO_QUEUE;
    }
    else
    {
        res = FindReceiveQueueForCurrentCpu(&RSSParameters->ActiveRSSScalingSettings);
    }

    return res;
}

static void PrintIndirectionTable(const NDIS_RECEIVE_SCALE_PARAMETERS* Params)
{
    ULONG IndirectionTableEntries = Params->IndirectionTableSize / sizeof(PROCESSOR_NUMBER);
    
    DPrintf(RSS_PRINT_LEVEL, "Params: flags 0x%4.4x, hash information 0x%4.4lx\n",
        Params->Flags, Params->HashInformation);

    DPrintf(RSS_PRINT_LEVEL, "NDIS IndirectionTable[%lu]\n", IndirectionTableEntries);
    ParaNdis_PrintTable<80, 20>(RSS_PRINT_LEVEL, (const PROCESSOR_NUMBER *)((char *)Params + Params->IndirectionTableOffset), IndirectionTableEntries,
        "%u/%u", [](const PROCESSOR_NUMBER *proc) { return proc->Group; }, [](const PROCESSOR_NUMBER *proc) { return proc->Number; });
}

static void PrintIndirectionTable(const PARANDIS_SCALING_SETTINGS *RSSScalingSetting)
{
    ULONG IndirectionTableEntries = RSSScalingSetting->IndirectionTableSize/ sizeof(PROCESSOR_NUMBER);

    DPrintf(RSS_PRINT_LEVEL, "Driver IndirectionTable[%lu]\n", IndirectionTableEntries);
    ParaNdis_PrintTable<80, 20>(RSS_PRINT_LEVEL, RSSScalingSetting->IndirectionTable, IndirectionTableEntries,
        "%u/%u", [](const PROCESSOR_NUMBER *proc) { return proc->Group; }, [](const PROCESSOR_NUMBER *proc) { return proc->Number; });
}


static void PrintRSSSettings(const PPARANDIS_RSS_PARAMS RSSParameters)
{
    ULONG CPUNumber = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    DPrintf(RSS_PRINT_LEVEL, "%lu cpus, %d queues, first queue CPU index %ld, default queue %d\n",
        CPUNumber, RSSParameters->ReceiveQueuesNumber,
        RSSParameters->RSSScalingSettings.FirstQueueIndirectionIndex,
        RSSParameters->RSSScalingSettings.DefaultQueue);

    PrintIndirectionTable(&RSSParameters->ActiveRSSScalingSettings);

    DPrintf(RSS_PRINT_LEVEL, "CPU mapping table[%u]:\n", RSSParameters->ActiveRSSScalingSettings.CPUIndexMappingSize);
    ParaNdis_PrintCharArray(RSS_PRINT_LEVEL, RSSParameters->ActiveRSSScalingSettings.CPUIndexMapping, RSSParameters->ActiveRSSScalingSettings.CPUIndexMappingSize);

    DPrintf(RSS_PRINT_LEVEL, "Queue indirection table[%u]:\n", RSSParameters->ReceiveQueuesNumber);
    ParaNdis_PrintCharArray(RSS_PRINT_LEVEL, RSSParameters->ActiveRSSScalingSettings.QueueIndirectionTable, RSSParameters->ReceiveQueuesNumber);
}

NDIS_STATUS ParaNdis_SetupRSSQueueMap(PARANDIS_ADAPTER *pContext)
{
    USHORT rssIndex, bundleIndex;
    ULONG cpuIndex;
    ULONG rssTableSize = pContext->RSSParameters.RSSScalingSettings.IndirectionTableSize / sizeof(PROCESSOR_NUMBER);

    rssIndex = 0;
    bundleIndex = 0;
    USHORT *cpuIndexTable;
    ULONG cpuNumbers;

    cpuNumbers = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    cpuIndexTable = (USHORT *)NdisAllocateMemoryWithTagPriority(pContext->MiniportHandle, cpuNumbers * sizeof(*cpuIndexTable),
        PARANDIS_MEMORY_TAG, NormalPoolPriority);
    if (cpuIndexTable == nullptr)
    {
        DPrintf(0, "[%s] cpu index table allocation failed\n", __FUNCTION__);
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory(cpuIndexTable, sizeof(*cpuIndexTable) * cpuNumbers);

    for (bundleIndex = 0; bundleIndex < pContext->nPathBundles; ++bundleIndex)
    {
        cpuIndex = pContext->pPathBundles[bundleIndex].rxPath.getCPUIndex();
        if (cpuIndex == INVALID_PROCESSOR_INDEX)
        {
            DPrintf(0, "[%s]  Invalid CPU index for path %u\n", __FUNCTION__, bundleIndex);
            NdisFreeMemoryWithTagPriority(pContext->MiniportHandle, cpuIndexTable, PARANDIS_MEMORY_TAG);
            return NDIS_STATUS_SOFT_ERRORS;
        }
        else if (cpuIndex >= cpuNumbers)
        {
            DPrintf(0, "[%s]  CPU index %lu exceeds CPU range %lu\n", __FUNCTION__, cpuIndex, cpuNumbers);
            NdisFreeMemoryWithTagPriority(pContext->MiniportHandle, cpuIndexTable, PARANDIS_MEMORY_TAG);
            return NDIS_STATUS_SOFT_ERRORS;
        }
        else
        {
            cpuIndexTable[cpuIndex] = bundleIndex;
        }
    }

    DPrintf(0, "[%s] Entering, RSS table size = %lu, # of path bundles = %u. RSS2QueueLength = %u, RSS2QueueMap =0x%p\n",
        __FUNCTION__, rssTableSize, pContext->nPathBundles,
        pContext->RSS2QueueLength, pContext->RSS2QueueMap);

    if (pContext->RSS2QueueLength && pContext->RSS2QueueLength < rssTableSize)
    {
        DPrintf(0, "[%s] Freeing RSS2Queue Map\n", __FUNCTION__);
        NdisFreeMemoryWithTagPriority(pContext->MiniportHandle, pContext->RSS2QueueMap, PARANDIS_MEMORY_TAG);
        pContext->RSS2QueueLength = 0;
    }

    if (!pContext->RSS2QueueLength)
    {
        pContext->RSS2QueueLength = USHORT(rssTableSize);
        pContext->RSS2QueueMap = (CPUPathBundle **)NdisAllocateMemoryWithTagPriority(pContext->MiniportHandle, rssTableSize * sizeof(*pContext->RSS2QueueMap),
            PARANDIS_MEMORY_TAG, NormalPoolPriority);
        if (pContext->RSS2QueueMap == nullptr)
        {
            DPrintf(0, "[%s] - Allocating RSS to queue mapping failed\n", __FUNCTION__);
            NdisFreeMemoryWithTagPriority(pContext->MiniportHandle, cpuIndexTable, PARANDIS_MEMORY_TAG);
            return NDIS_STATUS_RESOURCES;
        }

        NdisZeroMemory(pContext->RSS2QueueMap, sizeof(*pContext->RSS2QueueMap) * pContext->RSS2QueueLength);
    }

    for (rssIndex = 0; rssIndex < rssTableSize; rssIndex++)
    {
        pContext->RSS2QueueMap[rssIndex] = pContext->pPathBundles;
    }

    for (rssIndex = 0; rssIndex < rssTableSize; rssIndex++)
    {
        cpuIndex = NdisProcessorNumberToIndex(pContext->RSSParameters.RSSScalingSettings.IndirectionTable[rssIndex]);
        bundleIndex = cpuIndexTable[cpuIndex];

        DPrintf(3, "[%s] filling the relationship, rssIndex = %u, bundleIndex = %u\n", __FUNCTION__, rssIndex, bundleIndex);
        DPrintf(3, "[%s] RSS proc number %u/%u, bundle affinity %u/%llu\n", __FUNCTION__,
            pContext->RSSParameters.RSSScalingSettings.IndirectionTable[rssIndex].Group,
            pContext->RSSParameters.RSSScalingSettings.IndirectionTable[rssIndex].Number,
            pContext->pPathBundles[bundleIndex].txPath.DPCAffinity.Group,
            pContext->pPathBundles[bundleIndex].txPath.DPCAffinity.Mask);

        pContext->RSS2QueueMap[rssIndex] = pContext->pPathBundles + bundleIndex;
    }

    NdisFreeMemoryWithTagPriority(pContext->MiniportHandle, cpuIndexTable, PARANDIS_MEMORY_TAG);
    return NDIS_STATUS_SUCCESS;
}

void ParaNdis6_EnableDeviceRssSupport(PARANDIS_ADAPTER *pContext, BOOLEAN b)
{
    if (!b)
    {
        SetDeviceRSSSettings(pContext, true);
        pContext->bRSSSupportedByDevice = false;
    }
    else
    {
        pContext->bRSSSupportedByDevice = pContext->bRSSSupportedByDevicePersistent;
        SetDeviceRSSSettings(pContext);
    }
}

#endif
