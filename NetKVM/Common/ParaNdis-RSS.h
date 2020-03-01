#ifndef PARANDIS_RSS_H
#define PARANDIS_RSS_H

#include "osdep.h"

#include "ParaNdis-Util.h"

#if PARANDIS_SUPPORT_RSS

#define PARANDIS_RSS_MAX_RECEIVE_QUEUES (16)

typedef enum _tagPARANDIS_RSS_MODE
{
    PARANDIS_RSS_DISABLED = 0,
    PARANDIS_RSS_HASHING  = 1,
    PARANDIS_RSS_FULL     = 2
} PARANDIS_RSS_MODE, *PPARANDIS_RSS_MODE;

typedef struct _tagPARANDIS_HASHING_SETTINGS
{
    ULONG  HashInformation;
    CCHAR  HashSecretKey[NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2];
    USHORT HashSecretKeySize;
} PARANDIS_HASHING_SETTINGS;


#define INVALID_INDIRECTION_INDEX (-1)

typedef struct _tagPARANDIS_SCALING_SETTINGS
{
    PROCESSOR_NUMBER IndirectionTable[NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2 / sizeof(PROCESSOR_NUMBER)];
    USHORT           IndirectionTableSize;
    CCHAR            QueueIndirectionTable[NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2 / sizeof(PROCESSOR_NUMBER)];

    ULONG            RSSHashMask;

    PCHAR          CPUIndexMapping;
    ULONG          CPUIndexMappingSize;

    LONG           FirstQueueIndirectionIndex;

    PROCESSOR_NUMBER DefaultProcessor;
    CCHAR          DefaultQueue;
} PARANDIS_SCALING_SETTINGS, *PPARANDIS_SCALING_SETTINGS;

typedef struct _tagPARANDIS_RSS_PARAMS
{
    PARANDIS_ADAPTER *pContext;

    CCHAR             ReceiveQueuesNumber;

    PARANDIS_RSS_MODE RSSMode;

    PARANDIS_HASHING_SETTINGS ReceiveHashingSettings;
    PARANDIS_HASHING_SETTINGS RSSHashingSettings;
    PARANDIS_SCALING_SETTINGS RSSScalingSettings;

    PARANDIS_HASHING_SETTINGS ActiveHashingSettings;
    PARANDIS_SCALING_SETTINGS ActiveRSSScalingSettings;

    mutable CNdisRWLock                 rwLock;
} PARANDIS_RSS_PARAMS, *PPARANDIS_RSS_PARAMS;

typedef struct _tagRSS_HASH_KEY_PARAMETERS
{
    NDIS_RECEIVE_HASH_PARAMETERS        ReceiveHashParameters;
    CCHAR                               HashSecretKey[NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2];
} RSS_HASH_KEY_PARAMETERS;

ULONG ParaNdis6_QueryReceiveHash(const PARANDIS_RSS_PARAMS *RSSParameters, RSS_HASH_KEY_PARAMETERS *RSSHashKeyParameters);

NDIS_STATUS ParaNdis6_RSSSetParameters( PARANDIS_ADAPTER *pContext,
                                        const NDIS_RECEIVE_SCALE_PARAMETERS* Params,
                                        UINT ParamsLength,
                                        PUINT ParamsBytesRead);

NDIS_STATUS ParaNdis6_RSSSetReceiveHash(PARANDIS_ADAPTER *pContext,
                                        const NDIS_RECEIVE_HASH_PARAMETERS* Params,
                                        UINT ParamsLength,
                                        PUINT ParamsBytesRead);

void ParaNdis6_CheckDeviceRSSCapabilities(PARANDIS_ADAPTER *pContext, bool& bRss, bool& bHash);
/* for engineering tests only */
void ParaNdis6_EnableDeviceRssSupport(PARANDIS_ADAPTER *pContext, BOOLEAN b);

VOID ParaNdis6_RSSCleanupConfiguration(PARANDIS_RSS_PARAMS *RSSParameters);

NDIS_RECEIVE_SCALE_CAPABILITIES* ParaNdis6_RSSCreateConfiguration(PARANDIS_ADAPTER *pContext);

struct _tagNET_PACKET_INFO;

VOID ParaNdis6_RSSAnalyzeReceivedPacket(
    PARANDIS_RSS_PARAMS *RSSParameters,
    PVOID dataBuffer,
    struct _tagNET_PACKET_INFO *packetInfo);

CCHAR ParaNdis6_RSSGetScalingDataForPacket(
    PARANDIS_RSS_PARAMS *RSSParameters,
    struct _tagNET_PACKET_INFO *packetInfo,
    PPROCESSOR_NUMBER targetProcessor);

CCHAR ParaNdis6_RSSGetCurrentCpuReceiveQueue(PARANDIS_RSS_PARAMS *RSSParameters);

#else

#define PARANDIS_RSS_MAX_RECEIVE_QUEUES (0)

#endif

#endif

