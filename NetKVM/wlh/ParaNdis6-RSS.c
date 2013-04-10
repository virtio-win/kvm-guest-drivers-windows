#include "ndis56common.h"

#if PARANDIS_SUPPORT_RSS

static VOID ApplySettings(PPARANDIS_RSS_PARAMS RSSParameters,
        PARANDIS_RSS_MODE NewRSSMode,
        PARANDIS_HASHING_SETTINGS *ReceiveHashingSettings,
        PARANDIS_SCALING_SETTINGS *ReceiveScalingSettings)
{
    NdisAcquireSpinLock(&RSSParameters->RSSSettingsLock);

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

    NdisReleaseSpinLock(&RSSParameters->RSSSettingsLock);
}

static VOID InitRSSParameters(PARANDIS_RSS_PARAMS *RSSParameters, CCHAR RSSReceiveQueuesNumber)
{
    NdisZeroMemory(RSSParameters, sizeof(*RSSParameters));
    NdisAllocateSpinLock(&RSSParameters->RSSSettingsLock);
    RSSParameters->ReceiveQueuesNumber = RSSReceiveQueuesNumber;
}

static VOID CleanupRSSParameters(PARANDIS_RSS_PARAMS *RSSParameters)
{
    if(RSSParameters->ActiveRSSScalingSettings.CPUIndexMapping != NULL)
        NdisFreeMemory(RSSParameters->ActiveRSSScalingSettings.CPUIndexMapping, 0, 0);

    if(RSSParameters->RSSScalingSettings.CPUIndexMapping != NULL)
        NdisFreeMemory(RSSParameters->RSSScalingSettings.CPUIndexMapping, 0, 0);

    NdisFreeSpinLock(&RSSParameters->RSSSettingsLock);
}

static VOID InitRSSCapabilities(NDIS_RECEIVE_SCALE_CAPABILITIES *RSSCapabilities, ULONG RSSReceiveQueuesNumber)
{
    RSSCapabilities->Header.Type = NDIS_OBJECT_TYPE_RSS_CAPABILITIES;
    RSSCapabilities->Header.Revision = NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2;
    RSSCapabilities->Header.Size = NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_2;
    RSSCapabilities->CapabilitiesFlags =    NDIS_RSS_CAPS_MESSAGE_SIGNALED_INTERRUPTS |
                                        NDIS_RSS_CAPS_CLASSIFICATION_AT_ISR |
                                        NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4 |
                                        NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6 |
                                        NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6_EX |
                                        NdisHashFunctionToeplitz;
    RSSCapabilities->NumberOfInterruptMessages = 1;
    RSSCapabilities->NumberOfReceiveQueues = RSSReceiveQueuesNumber;
    RSSCapabilities->NumberOfIndirectionTableEntries = NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2 / sizeof(PROCESSOR_NUMBER);
}

NDIS_RECEIVE_SCALE_CAPABILITIES* ParaNdis6_RSSCreateConfiguration(PARANDIS_RSS_PARAMS *RSSParameters,
                                                                  NDIS_RECEIVE_SCALE_CAPABILITIES *RSSCapabilities,
                                                                  CCHAR RSSReceiveQueuesNumber)
{
    InitRSSParameters(RSSParameters, RSSReceiveQueuesNumber);
    InitRSSCapabilities(RSSCapabilities, RSSReceiveQueuesNumber);
    return RSSCapabilities;
}

VOID ParaNdis6_RSSCleanupConfiguration(PARANDIS_RSS_PARAMS *RSSParameters)
{
    CleanupRSSParameters(RSSParameters);
}

static BOOLEAN IsValidHashInfo(ULONG HashInformation)
{
#define HASH_FLAGS_COMBINATION(Type, Flags) ( ((Type) & (Flags)) && !((Type) & ~(Flags)) )

    ULONG ulHashType = NDIS_RSS_HASH_TYPE_FROM_HASH_INFO(HashInformation);
    ULONG ulHashFunction = NDIS_RSS_HASH_FUNC_FROM_HASH_INFO(HashInformation);

    if (HashInformation == 0)
        return TRUE;

    if (HASH_FLAGS_COMBINATION(ulHashType, NDIS_HASH_IPV4 | NDIS_HASH_TCP_IPV4 |
                                           NDIS_HASH_IPV6 | NDIS_HASH_TCP_IPV6 |
                                           NDIS_HASH_IPV6_EX | NDIS_HASH_TCP_IPV6_EX))
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
    PROCESSOR_NUMBER CurrProcNum;
    ULONG CurrProcIdx;

    KeGetCurrentProcessorNumberEx(&CurrProcNum);
    CurrProcIdx = KeGetProcessorIndexFromNumber(&CurrProcNum);

    ASSERT(CurrProcIdx != INVALID_PROCESSOR_INDEX);

    if(CurrProcIdx >= RSSScalingSettings->CPUIndexMappingSize)
        return PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED;

    return RSSScalingSettings->CPUIndexMapping[CurrProcIdx];
}

static
BOOLEAN AllocateCPUMappingArray(NDIS_HANDLE NdisHandle, PPARANDIS_SCALING_SETTINGS RSSScalingSettings)
{
    ULONG i;
    ULONG CPUNumber = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    PCHAR NewCPUMappingArray = NdisAllocateMemoryWithTagPriority(
                                                                NdisHandle,
                                                                sizeof(CCHAR) * CPUNumber,
                                                                PARANDIS_MEMORY_TAG,
                                                                NormalPoolPriority);

    if(!NewCPUMappingArray)
        return FALSE;

    RSSScalingSettings->CPUIndexMapping = NewCPUMappingArray;
    RSSScalingSettings->CPUIndexMappingSize = CPUNumber;

    for(i = 0; i < CPUNumber; i++)
    {
        RSSScalingSettings->CPUIndexMapping[i] = PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED;
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

    for (i = 0; i < RSSScalingSettings->IndirectionTableSize/sizeof(PROCESSOR_NUMBER); i++)
    {
        PPROCESSOR_NUMBER ProcNum = &RSSScalingSettings->IndirectionTable[i];
        ULONG CurrProcIdx = KeGetProcessorIndexFromNumber(ProcNum);

        if(CurrProcIdx != INVALID_PROCESSOR_INDEX)
        {
            if(RSSScalingSettings->CPUIndexMapping[CurrProcIdx] == PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED)
                RSSScalingSettings->CPUIndexMapping[CurrProcIdx] = ReceiveQueue++;

            RSSScalingSettings->QueueIndirectionTable[i] = RSSScalingSettings->CPUIndexMapping[CurrProcIdx];

            if(ReceiveQueue == ReceiveQueuesNumber)
                ReceiveQueue = PARANDIS_FIRST_RSS_RECEIVE_QUEUE;
        }
        else
        {
            RSSScalingSettings->QueueIndirectionTable[i] = PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED;
        }
    }
}

NDIS_STATUS ParaNdis6_RSSSetParameters( PARANDIS_RSS_PARAMS *RSSParameters,
                                        const NDIS_RECEIVE_SCALE_PARAMETERS* Params,
                                        UINT ParamsLength,
                                        PUINT ParamsBytesRead,
                                        NDIS_HANDLE NdisHandle)
{
    ULONG ProcessorMasksSize;
    ULONG IndirectionTableEntries;

    *ParamsBytesRead = 0;

    if((RSSParameters->RSSMode == PARANDIS_RSS_HASHING) &&
        !(Params->Flags & NDIS_RSS_PARAM_FLAG_DISABLE_RSS) &&
        (Params->HashInformation != 0))
        return NDIS_STATUS_NOT_SUPPORTED;

    if (ParamsLength < sizeof(NDIS_RECEIVE_SCALE_PARAMETERS))
        return NDIS_STATUS_INVALID_LENGTH;

    if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED) &&
        !IsValidHashInfo(Params->HashInformation))
        return NDIS_STATUS_INVALID_PARAMETER;

    IndirectionTableEntries = Params->IndirectionTableSize / sizeof(PROCESSOR_NUMBER);

    if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED) &&
        ( (Params->IndirectionTableSize > sizeof(RSSParameters->RSSScalingSettings.IndirectionTable)) ||
          (ParamsLength < (Params->IndirectionTableOffset + Params->IndirectionTableSize)) ||
          !IsPowerOfTwo(IndirectionTableEntries) )
        )
        return NDIS_STATUS_INVALID_LENGTH;

    if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED) &&
        ( (Params->HashSecretKeySize > sizeof(RSSParameters->RSSHashingSettings.HashSecretKey)) ||
          (ParamsLength < (Params->HashSecretKeyOffset + Params->HashSecretKeySize)) )
        )
            return NDIS_STATUS_INVALID_LENGTH;

    ProcessorMasksSize = Params->NumberOfProcessorMasks * Params->ProcessorMasksEntrySize;
    if (ParamsLength < Params->ProcessorMasksOffset + ProcessorMasksSize)
        return NDIS_STATUS_INVALID_LENGTH;

    if(Params->Flags & NDIS_RSS_PARAM_FLAG_DISABLE_RSS || (Params->HashInformation == 0))
    {
        ApplySettings(RSSParameters, PARANDIS_RSS_DISABLED, NULL, NULL);
    }
    else
    {
        if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED))
        {
            if(!AllocateCPUMappingArray(NdisHandle, &RSSParameters->RSSScalingSettings))
                return NDIS_STATUS_RESOURCES;

            RSSParameters->RSSScalingSettings.IndirectionTableSize = Params->IndirectionTableSize;
            NdisMoveMemory(RSSParameters->RSSScalingSettings.IndirectionTable,
                (char*)Params + Params->IndirectionTableOffset, Params->IndirectionTableSize);
            RSSParameters->RSSScalingSettings.RSSHashMask = IndirectionTableEntries - 1;

            *ParamsBytesRead += Params->IndirectionTableSize;
            *ParamsBytesRead += ProcessorMasksSize;

            FillCPUMappingArray(&RSSParameters->RSSScalingSettings, RSSParameters->ReceiveQueuesNumber);
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

    *ParamsBytesRead += sizeof(NDIS_RECEIVE_SCALE_PARAMETERS);
    return NDIS_STATUS_SUCCESS;
}

ULONG ParaNdis6_QueryReceiveHash(const PARANDIS_RSS_PARAMS *RSSParameters,
                                 RSS_HASH_KEY_PARAMETERS *RSSHashKeyParameters)
{
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

NDIS_STATUS ParaNdis6_RSSSetReceiveHash(PARANDIS_RSS_PARAMS *RSSParameters,
                                        const NDIS_RECEIVE_HASH_PARAMETERS* Params,
                                        UINT ParamsLength,
                                        PUINT ParamsBytesRead)
{
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

    return NDIS_STATUS_SUCCESS;
}

typedef struct _tagHASH_CALC_SG_BUF_ENTRY
{
    PCHAR chunkPtr;
    ULONG  chunkLen;
} HASH_CALC_SG_BUF_ENTRY, *PHASH_CALC_SG_BUF_ENTRY;

// Little Endian version ONLY
static
UINT32 ToeplitsHash(const PHASH_CALC_SG_BUF_ENTRY sgBuff, int sgEntriesNum, PCCHAR fullKey)
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
VOID RSSCalcHash(
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

            packetInfo->RSSHash.Value = ToeplitsHash(sgBuff, 2, &RSSParameters->ActiveHashingSettings.HashSecretKey[0]);
            packetInfo->RSSHash.Type = NDIS_HASH_TCP_IPV4;
            packetInfo->RSSHash.Function = NdisHashFunctionToeplitz;
            return;
        }

        if(hashTypes & NDIS_HASH_IPV4)
        {
            sgBuff[0].chunkPtr = RtlOffsetToPointer(dataBuffer, packetInfo->L2HdrLen + FIELD_OFFSET(IPv4Header, ip_src));
            sgBuff[0].chunkLen = RTL_FIELD_SIZE(IPv4Header, ip_src) + RTL_FIELD_SIZE(IPv4Header, ip_dest);

            packetInfo->RSSHash.Value = ToeplitsHash(sgBuff, 1, RSSParameters->ActiveHashingSettings.HashSecretKey);
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

                packetInfo->RSSHash.Value = ToeplitsHash(sgBuff, 3, RSSParameters->ActiveHashingSettings.HashSecretKey);
                packetInfo->RSSHash.Type = (hashTypes & NDIS_HASH_TCP_IPV6_EX) ? NDIS_HASH_TCP_IPV6_EX : NDIS_HASH_TCP_IPV6;
                packetInfo->RSSHash.Function = NdisHashFunctionToeplitz;
                return;
            }
        }

        if(hashTypes & (NDIS_HASH_IPV6 | NDIS_HASH_IPV6_EX))
        {
            sgBuff[0].chunkPtr = (PCHAR) GetIP6SrcAddrForHash(dataBuffer, packetInfo, hashTypes);
            sgBuff[0].chunkLen = RTL_FIELD_SIZE(IPv6Header, ip6_src_address);
            sgBuff[1].chunkPtr = (PCHAR) GetIP6DstAddrForHash(dataBuffer, packetInfo, hashTypes);
            sgBuff[1].chunkLen = RTL_FIELD_SIZE(IPv6Header, ip6_dst_address);

            packetInfo->RSSHash.Value = ToeplitsHash(sgBuff, 2, RSSParameters->ActiveHashingSettings.HashSecretKey);
            packetInfo->RSSHash.Type = (hashTypes & NDIS_HASH_IPV6_EX) ? NDIS_HASH_IPV6_EX : NDIS_HASH_IPV6;
            packetInfo->RSSHash.Function = NdisHashFunctionToeplitz;
            return;
        }

        if(hashTypes & NDIS_HASH_IPV6)
        {
            IPv6Header *pIpHeader = (IPv6Header *) RtlOffsetToPointer(dataBuffer, packetInfo->L2HdrLen);

            sgBuff[0].chunkPtr = RtlOffsetToPointer(pIpHeader, FIELD_OFFSET(IPv6Header, ip6_src_address));
            sgBuff[0].chunkLen = RTL_FIELD_SIZE(IPv6Header, ip6_src_address) + RTL_FIELD_SIZE(IPv6Header, ip6_dst_address);

            packetInfo->RSSHash.Value = ToeplitsHash(sgBuff, 2, RSSParameters->ActiveHashingSettings.HashSecretKey);
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
    NdisDprAcquireSpinLock(&RSSParameters->RSSSettingsLock);
    if(RSSParameters->RSSMode != PARANDIS_RSS_DISABLED)
    {
        RSSCalcHash(RSSParameters, dataBuffer, packetInfo);
    }
    NdisDprReleaseSpinLock(&RSSParameters->RSSSettingsLock);
}

CCHAR ParaNdis6_RSSGetScalingDataForPacket(
    PARANDIS_RSS_PARAMS *RSSParameters,
    PNET_PACKET_INFO packetInfo,
    PPROCESSOR_NUMBER targetProcessor)
{
    CCHAR targetQueue;

    NdisDprAcquireSpinLock(&RSSParameters->RSSSettingsLock);

    if((RSSParameters->RSSMode != PARANDIS_RSS_FULL) || (packetInfo->RSSHash.Type == 0))
    {
        targetQueue = PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED;
    }
    else
    {
        ULONG indirectionIndex = packetInfo->RSSHash.Value & RSSParameters->ActiveRSSScalingSettings.RSSHashMask;
        *targetProcessor = RSSParameters->ActiveRSSScalingSettings.IndirectionTable[indirectionIndex];
        targetQueue = RSSParameters->ActiveRSSScalingSettings.QueueIndirectionTable[indirectionIndex];
    }

    NdisDprReleaseSpinLock(&RSSParameters->RSSSettingsLock);

    return targetQueue;
}

CCHAR ParaNdis6_RSSGetCurrentCpuReceiveQueue(PARANDIS_RSS_PARAMS *RSSParameters)
{
    CCHAR res;

    NdisDprAcquireSpinLock(&RSSParameters->RSSSettingsLock);

    if(RSSParameters->RSSMode != PARANDIS_RSS_FULL)
    {
        res = PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED;
    }
    else
    {
        res = FindReceiveQueueForCurrentCpu(&RSSParameters->ActiveRSSScalingSettings);
    }

    NdisDprReleaseSpinLock(&RSSParameters->RSSSettingsLock);

    return res;
}

#endif
