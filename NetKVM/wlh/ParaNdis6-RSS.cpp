#include "ndis56common.h"
#include "kdebugprint.h"

#if PARANDIS_SUPPORT_RSS

#define RSS_PRINT_LEVEL 0

static void PrintIndirectionTable(const NDIS_RECEIVE_SCALE_PARAMETERS* Params);
static void PrintIndirectionTable(const PARANDIS_SCALING_SETTINGS *RSSScalingSetting);

static void PrintRSSSettings(PPARANDIS_RSS_PARAMS RSSParameters);

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

static VOID InitRSSParameters(PARANDIS_RSS_PARAMS *RSSParameters, CCHAR RSSReceiveQueuesNumber)
{
    NdisZeroMemory(RSSParameters, sizeof(*RSSParameters));
    RSSParameters->ReceiveQueuesNumber = RSSReceiveQueuesNumber;
}

static VOID CleanupRSSParameters(PARANDIS_RSS_PARAMS *RSSParameters)
{
    if(RSSParameters->ActiveRSSScalingSettings.CPUIndexMapping != NULL)
        NdisFreeMemory(RSSParameters->ActiveRSSScalingSettings.CPUIndexMapping, 0, 0);

    if(RSSParameters->RSSScalingSettings.CPUIndexMapping != NULL)
        NdisFreeMemory(RSSParameters->RSSScalingSettings.CPUIndexMapping, 0, 0);

}

static VOID InitRSSCapabilities(NDIS_RECEIVE_SCALE_CAPABILITIES *RSSCapabilities, ULONG RSSReceiveQueuesNumber)
{
    RSSCapabilities->Header.Type = NDIS_OBJECT_TYPE_RSS_CAPABILITIES;
#if (NDIS_SUPPORT_NDIS630)
    RSSCapabilities->Header.Revision = NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2;
    RSSCapabilities->Header.Size = NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_2;
#else
    RSSCapabilities->Header.Revision = NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_1;
    RSSCapabilities->Header.Size = NDIS_SIZEOF_RECEIVE_SCALE_CAPABILITIES_REVISION_1;
#endif
    RSSCapabilities->CapabilitiesFlags =    NDIS_RSS_CAPS_MESSAGE_SIGNALED_INTERRUPTS |
                                        NDIS_RSS_CAPS_CLASSIFICATION_AT_ISR |
                                        NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4 |
                                        NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6 |
                                        NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6_EX |
                                        NdisHashFunctionToeplitz;
    RSSCapabilities->NumberOfInterruptMessages = 1;
    RSSCapabilities->NumberOfReceiveQueues = RSSReceiveQueuesNumber;
#if (NDIS_SUPPORT_NDIS630)
    RSSCapabilities->NumberOfIndirectionTableEntries = NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2 / sizeof(PROCESSOR_NUMBER);
#endif
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
        return PARANDIS_RECEIVE_NO_QUEUE;

    return RSSScalingSettings->CPUIndexMapping[CurrProcIdx];
}

static
BOOLEAN AllocateCPUMappingArray(NDIS_HANDLE NdisHandle, PPARANDIS_SCALING_SETTINGS RSSScalingSettings)
{
    ULONG i;
    ULONG CPUNumber = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    PCHAR NewCPUMappingArray = (PCHAR) NdisAllocateMemoryWithTagPriority(
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
        DPrintf(0, ("[%s] - CPU <-> queue assignment failed!", __FUNCTION__));
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
        DPrintf(0, ("[%s] Indirection table changed\n", __FUNCTION__));
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
    CNdisPassiveWriteAutoLock autoLock(RSSParameters->rwLock);

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
        DPrintf(0, ("[%s] RSS Params: flags 0x%4.4x, hash information 0x%4.4lx\n",
            __FUNCTION__, Params->Flags, Params->HashInformation));

        if (!(Params->Flags & NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED))
        {
            PrintIndirectionTable(Params);

            if(!AllocateCPUMappingArray(NdisHandle, &RSSParameters->RSSScalingSettings))
                return NDIS_STATUS_RESOURCES;

            RSSParameters->RSSScalingSettings.IndirectionTableSize = Params->IndirectionTableSize;
            NdisMoveMemory(RSSParameters->RSSScalingSettings.IndirectionTable,
                (char*)Params + Params->IndirectionTableOffset, Params->IndirectionTableSize);
            RSSParameters->RSSScalingSettings.RSSHashMask = IndirectionTableEntries - 1;

            *ParamsBytesRead += Params->IndirectionTableSize;
            *ParamsBytesRead += ProcessorMasksSize;

            FillCPUMappingArray(&RSSParameters->RSSScalingSettings, RSSParameters->ReceiveQueuesNumber);
            PrintRSSSettings(RSSParameters);
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

NDIS_STATUS ParaNdis6_RSSSetReceiveHash(PARANDIS_RSS_PARAMS *RSSParameters,
                                        const NDIS_RECEIVE_HASH_PARAMETERS* Params,
                                        UINT ParamsLength,
                                        PUINT ParamsBytesRead)
{
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
        targetQueue = PARANDIS_RECEIVE_UNCLASSIFIED_PACKET;
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
    
    DPrintf(RSS_PRINT_LEVEL, ("Params: flags 0x%4.4x, hash information 0x%4.4lx\n",
        Params->Flags, Params->HashInformation));

    DPrintf(RSS_PRINT_LEVEL, ("NDIS IndirectionTable[%lu]\n", IndirectionTableEntries));
    ParaNdis_PrintTable<80, 20>(RSS_PRINT_LEVEL, (const PROCESSOR_NUMBER *)((char *)Params + Params->IndirectionTableOffset), IndirectionTableEntries,
        "%u/%u", [](const PROCESSOR_NUMBER *proc) { return proc->Group; }, [](const PROCESSOR_NUMBER *proc) { return proc->Number; });
}

static void PrintIndirectionTable(const PARANDIS_SCALING_SETTINGS *RSSScalingSetting)
{
    ULONG IndirectionTableEntries = RSSScalingSetting->IndirectionTableSize/ sizeof(PROCESSOR_NUMBER);

    DPrintf(RSS_PRINT_LEVEL, ("Driver IndirectionTable[%lu]\n", IndirectionTableEntries));
    ParaNdis_PrintTable<80, 20>(RSS_PRINT_LEVEL, RSSScalingSetting->IndirectionTable, IndirectionTableEntries,
        "%u/%u", [](const PROCESSOR_NUMBER *proc) { return proc->Group; }, [](const PROCESSOR_NUMBER *proc) { return proc->Number; });
}


static void PrintRSSSettings(const PPARANDIS_RSS_PARAMS RSSParameters)
{
    ULONG CPUNumber = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    DPrintf(RSS_PRINT_LEVEL, ("%lu cpus, %d queues, first queue CPU index %ld",
        CPUNumber, RSSParameters->ReceiveQueuesNumber,
        RSSParameters->RSSScalingSettings.FirstQueueIndirectionIndex));

    PrintIndirectionTable(&RSSParameters->ActiveRSSScalingSettings);

    DPrintf(RSS_PRINT_LEVEL, ("CPU mapping table[%u]: ", RSSParameters->ActiveRSSScalingSettings.CPUIndexMappingSize));
    ParaNdis_PrintCharArray(RSS_PRINT_LEVEL, RSSParameters->ActiveRSSScalingSettings.CPUIndexMapping, RSSParameters->ActiveRSSScalingSettings.CPUIndexMappingSize);

    DPrintf(RSS_PRINT_LEVEL, ("Queue indirection table[%u]: ", RSSParameters->ReceiveQueuesNumber));
    ParaNdis_PrintCharArray(RSS_PRINT_LEVEL, RSSParameters->ActiveRSSScalingSettings.QueueIndirectionTable, RSSParameters->ReceiveQueuesNumber);
}

#endif
