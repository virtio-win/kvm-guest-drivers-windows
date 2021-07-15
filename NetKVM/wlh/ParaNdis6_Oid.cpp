/*
 * This file contains implementation of NDIS6 OID-related procedures.
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


#include "ParaNdis-Oid.h"
#include "ParaNdis6.h"
#include "kdebugprint.h"
#include "ParaNdis_DebugHistory.h"
#include "netkvmmof.h"
#include "virtio_pci.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis6_Oid.tmh"
#endif

static NDIS_IO_WORKITEM_FUNCTION OnSetPowerWorkItem;

#define OIDENTRY(oid, el, xfl, xokl, flags) \
{ oid, el, xfl, xokl, flags, NULL }
#define OIDENTRYPROC(oid, el, xfl, xokl, flags, setproc) \
{ oid, el, xfl, xokl, flags, setproc }

/**********************************************************
Just fail the request for unsupported OID
Parameters:
    context
    tOidDesc *pOid      descriptor of OID request
Return value:
    whatever
***********************************************************/
static NDIS_STATUS OnSetInterruptModeration(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(pOid);

    return  NDIS_STATUS_INVALID_DATA;
}


static NDIS_STATUS OnSetOffloadParameters(PARANDIS_ADAPTER *pContext, tOidDesc *pOid);
static NDIS_STATUS OnSetOffloadEncapsulation(PARANDIS_ADAPTER *pContext, tOidDesc *pOid);
static NDIS_STATUS OnSetLinkParameters(PARANDIS_ADAPTER *pContext, tOidDesc *pOid);
static NDIS_STATUS OnSetVendorSpecific1(PARANDIS_ADAPTER *pContext, tOidDesc *pOid);
static NDIS_STATUS OnSetVendorSpecific2(PARANDIS_ADAPTER *pContext, tOidDesc *pOid);
static NDIS_STATUS OnSetVendorSpecific3(PARANDIS_ADAPTER *pContext, tOidDesc *pOid);
static NDIS_STATUS OnSetVendorSpecific4(PARANDIS_ADAPTER *pContext, tOidDesc *pOid);

#define OID_VENDOR_1                    0xff010201
#define OID_VENDOR_2                    0xff010202
#define OID_VENDOR_3                    0xff010203
#define OID_VENDOR_4                    0xff010204

#if PARANDIS_SUPPORT_RSS

static NDIS_STATUS RSSSetParameters(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS status;

    if (!pContext->bRSSOffloadSupported)
        return NDIS_STATUS_NOT_SUPPORTED;

    status = ParaNdis6_RSSSetParameters(pContext,
                                        (NDIS_RECEIVE_SCALE_PARAMETERS*) pOid->InformationBuffer,
                                        pOid->InformationBufferLength,
                                        pOid->pBytesRead);
    if (status != NDIS_STATUS_SUCCESS)
    {
        DPrintf(0, "[%s] - RSS parameters setting failed\n", __FUNCTION__);
    }

    return status;
}

static NDIS_STATUS RSSSetReceiveHash(   PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS status;

    if (!pContext->bRSSOffloadSupported)
        return NDIS_STATUS_NOT_SUPPORTED;

    status = ParaNdis6_RSSSetReceiveHash(pContext,
                                        (NDIS_RECEIVE_HASH_PARAMETERS*) pOid->InformationBuffer,
                                        pOid->InformationBufferLength,
                                        pOid->pBytesRead);

    return status;
}

#endif

/**********************************************************
Structure defining how to support each OID
***********************************************************/
static const tOidWhatToDo OidsDB[] =
{
//                                              i f ok flags        set proc
OIDENTRY(OID_GEN_SUPPORTED_LIST,                2,2,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_HARDWARE_STATUS,               2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_MEDIA_SUPPORTED,               2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_MEDIA_IN_USE,                  2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_MAXIMUM_LOOKAHEAD,             2,0,4, ohfQuery         ),
OIDENTRY(OID_GEN_MAXIMUM_FRAME_SIZE,            2,0,4, ohfQuery         ),
OIDENTRY(OID_GEN_TRANSMIT_BUFFER_SPACE,         2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_RECEIVE_BUFFER_SPACE,          2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_TRANSMIT_BLOCK_SIZE,           2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_RECEIVE_BLOCK_SIZE,            2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_VENDOR_ID,                     2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_VENDOR_DESCRIPTION,            2,2,4, ohfQueryStat     ),
OIDENTRYPROC(OID_GEN_CURRENT_PACKET_FILTER,     2,0,4, ohfQuerySet | ohfSetPropagatePre, ParaNdis_OnSetPacketFilter),
OIDENTRYPROC(OID_GEN_CURRENT_LOOKAHEAD,         2,0,4, ohfQuerySet, ParaNdis_OnSetLookahead),
OIDENTRY(OID_GEN_DRIVER_VERSION,                2,0,4, ohfQuery         ),
OIDENTRY(OID_GEN_MAXIMUM_TOTAL_SIZE,            2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_PROTOCOL_OPTIONS,              2,0,4, 0                ),
OIDENTRY(OID_GEN_MAC_OPTIONS,                   2,0,4, ohfQuery         ),
OIDENTRY(OID_GEN_MAXIMUM_SEND_PACKETS,          2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_VENDOR_DRIVER_VERSION,         2,0,4, ohfQueryStat     ),
OIDENTRY(OID_GEN_SUPPORTED_GUIDS,               2,4,4, ohfQueryStat     ),
OIDENTRYPROC(OID_GEN_NETWORK_LAYER_ADDRESSES,   2,2,4, ohfSet,      ParaNdis_OnOidSetNetworkAddresses),
OIDENTRY(OID_GEN_TRANSPORT_HEADER_OFFSET,       2,4,4, 0                ),
OIDENTRY(OID_GEN_MEDIA_CAPABILITIES,            2,4,4, 0                ),
OIDENTRY(OID_GEN_PHYSICAL_MEDIUM,               2,4,4, 0                ),
OIDENTRY(OID_GEN_XMIT_OK,                       3,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_RCV_OK,                        3,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_XMIT_ERROR,                    2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_RCV_ERROR,                     2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_RCV_NO_BUFFER,                 2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_DIRECTED_BYTES_XMIT,           2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_DIRECTED_FRAMES_XMIT,          2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_MULTICAST_BYTES_XMIT,          2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_MULTICAST_FRAMES_XMIT,         2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_BROADCAST_BYTES_XMIT,          2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_BROADCAST_FRAMES_XMIT,         2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_DIRECTED_BYTES_RCV,            2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_DIRECTED_FRAMES_RCV,           2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_MULTICAST_BYTES_RCV,           2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_MULTICAST_FRAMES_RCV,          2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_BROADCAST_BYTES_RCV,           2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_BROADCAST_FRAMES_RCV,          2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_GEN_RCV_CRC_ERROR,                 2,4,4, 0                ),
OIDENTRY(OID_GEN_TRANSMIT_QUEUE_LENGTH,         2,0,4, ohfQuery         ),
OIDENTRY(OID_GEN_GET_TIME_CAPS,                 2,4,4, 0                ),
OIDENTRY(OID_GEN_GET_NETCARD_TIME,              2,4,4, 0                ),
OIDENTRY(OID_GEN_NETCARD_LOAD,                  2,4,4, 0                ),
OIDENTRY(OID_GEN_DEVICE_PROFILE,                2,4,4, 0                ),
OIDENTRY(OID_GEN_INIT_TIME_MS,                  2,4,4, 0                ),
OIDENTRY(OID_GEN_RESET_COUNTS,                  2,4,4, 0                ),
OIDENTRY(OID_GEN_MEDIA_SENSE_COUNTS,            2,4,4, 0                ),
OIDENTRY(OID_PNP_CAPABILITIES,                  2,0,4, ohfQuery         ),
OIDENTRYPROC(OID_PNP_SET_POWER,                 0,0,0, ohfSet | ohfSetMoreOK, ParaNdis_OnSetPower),
OIDENTRY(OID_PNP_QUERY_POWER,                   2,0,4, ohfQuery         ),
OIDENTRYPROC(OID_PNP_ADD_WAKE_UP_PATTERN,       2,0,4, ohfSet,          ParaNdis_OnAddWakeupPattern),
OIDENTRYPROC(OID_PNP_REMOVE_WAKE_UP_PATTERN,    2,0,4, ohfSet,          ParaNdis_OnRemoveWakeupPattern),
OIDENTRYPROC(OID_PNP_ENABLE_WAKE_UP,            2,0,4, ohfQuerySet,     ParaNdis_OnEnableWakeup),
OIDENTRY(OID_802_3_PERMANENT_ADDRESS,           2,0,4, ohfQueryStat     ),
OIDENTRY(OID_802_3_CURRENT_ADDRESS,             2,0,4, ohfQueryStat     ),
OIDENTRYPROC(OID_802_3_MULTICAST_LIST,          2,0,4, ohfQuerySet | ohfSetPropagatePre, ParaNdis_OnOidSetMulticastList),
OIDENTRY(OID_802_3_MAXIMUM_LIST_SIZE,           2,0,4, ohfQueryStat     ),
OIDENTRY(OID_802_3_MAC_OPTIONS,                 2,4,4, 0                ),
OIDENTRY(OID_802_3_RCV_ERROR_ALIGNMENT,         2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_802_3_XMIT_ONE_COLLISION,          2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_802_3_XMIT_MORE_COLLISIONS,        2,4,4, ohfQueryStat3264 ),
OIDENTRY(OID_802_3_XMIT_DEFERRED,               2,4,4, 0                ),
OIDENTRY(OID_802_3_XMIT_MAX_COLLISIONS,         2,4,4, 0                ),
OIDENTRY(OID_802_3_RCV_OVERRUN,                 2,4,4, 0                ),
OIDENTRY(OID_802_3_XMIT_UNDERRUN,               2,4,4, 0                ),
OIDENTRY(OID_802_3_XMIT_HEARTBEAT_FAILURE,      2,4,4, 0                ),
OIDENTRY(OID_802_3_XMIT_TIMES_CRS_LOST,         2,4,4, 0                ),
OIDENTRY(OID_802_3_XMIT_LATE_COLLISIONS,        2,4,4, 0                ),
OIDENTRY(OID_GEN_MACHINE_NAME,                  2,4,4, 0                ),
OIDENTRY(OID_GEN_STATISTICS,                    3,4,4, ohfQueryStat     ),
OIDENTRYPROC(OID_GEN_VLAN_ID,                   2,4,4, ohfQueryStat | ohfSet, ParaNdis_OnSetVlanId),
OIDENTRYPROC(OID_GEN_INTERRUPT_MODERATION,      2,4,4, ohfQueryStat | ohfSet, OnSetInterruptModeration),
//Win8 NDIS 6.0 fails without it (Mini6OidsNdisRequests)
OIDENTRYPROC(OID_GEN_LINK_PARAMETERS,           2,0,4, ohfSet, OnSetLinkParameters),
OIDENTRY(OID_IP4_OFFLOAD_STATS,                 4,4,4, 0),
OIDENTRY(OID_IP6_OFFLOAD_STATS,                 4,4,4, 0),
OIDENTRYPROC(OID_TCP_OFFLOAD_PARAMETERS,        0,0,0, ohfSet | ohfSetMoreOK | ohfSetLessOK | ohfSetPropagatePost, OnSetOffloadParameters),
OIDENTRYPROC(OID_OFFLOAD_ENCAPSULATION,         0,0,0, ohfQuerySet | ohfSetPropagatePost, OnSetOffloadEncapsulation),
OIDENTRYPROC(OID_VENDOR_1,                      0,0,0, ohfQueryStat | ohfSet | ohfSetMoreOK, OnSetVendorSpecific1),
OIDENTRYPROC(OID_VENDOR_2,                      0,0,0, ohfQueryStat | ohfSet | ohfSetMoreOK, OnSetVendorSpecific2),
OIDENTRYPROC(OID_VENDOR_3,                      0,0,0, ohfQueryStat | ohfSet | ohfSetMoreOK, OnSetVendorSpecific3),
OIDENTRYPROC(OID_VENDOR_4,                      0,0,0, ohfQueryStat | ohfSet | ohfSetMoreOK, OnSetVendorSpecific4),

#if PARANDIS_SUPPORT_RSS
    OIDENTRYPROC(OID_GEN_RECEIVE_SCALE_PARAMETERS,  0,0,0, ohfSet | ohfSetPropagatePost | ohfSetMoreOK, RSSSetParameters),
    OIDENTRYPROC(OID_GEN_RECEIVE_HASH,              0,0,0, ohfQuerySet | ohfSetMoreOK, RSSSetReceiveHash),
#endif
#if PARANDIS_SUPPORT_RSC
    OIDENTRY(OID_TCP_RSC_STATISTICS,            3,4,4, ohfQueryStat     ),
#endif

#if NDIS_SUPPORT_NDIS620
// here should be NDIS 6.20 specific OIDs (mostly power management related)
// OID_PM_CURRENT_CAPABILITIES - not required, supported by NDIS
// OID_PM_PARAMETERS - mandatory
// OID_PM_ADD_WOL_PATTERN - mandatory
// OID_PM_REMOVE_WOL_PATTERN - mandatory
// OID_PM_WOL_PATTERN_LIST - not required, supported by NDIS
// OID_PM_ADD_PROTOCOL_OFFLOAD - mandatory
// OID_PM_GET_PROTOCOL_OFFLOAD - mandatory
// OID_PM_REMOVE_PROTOCOL_OFFLOAD - mandatory
// OID_PM_PROTOCOL_OFFLOAD_LIST - not required, supported by NDIS
#endif
// last entry, do not remove
OIDENTRY(0,                                     4,4,4, 0),
};


static NDIS_OID SupportedOids[] =
{
        OID_GEN_SUPPORTED_LIST,
        OID_GEN_HARDWARE_STATUS,
        OID_GEN_MEDIA_SUPPORTED,
        OID_GEN_MEDIA_IN_USE,
        OID_GEN_MAXIMUM_LOOKAHEAD,
        OID_GEN_MAXIMUM_FRAME_SIZE,
        OID_GEN_TRANSMIT_BUFFER_SPACE,
        OID_GEN_RECEIVE_BUFFER_SPACE,
        OID_GEN_TRANSMIT_BLOCK_SIZE,
        OID_GEN_RECEIVE_BLOCK_SIZE,
        OID_GEN_VENDOR_ID,
        OID_GEN_VENDOR_DESCRIPTION,
        OID_GEN_VENDOR_DRIVER_VERSION,
        OID_GEN_CURRENT_PACKET_FILTER,
        OID_GEN_CURRENT_LOOKAHEAD,
        OID_GEN_DRIVER_VERSION,
        OID_GEN_MAXIMUM_TOTAL_SIZE,
        OID_GEN_MAC_OPTIONS,
        OID_GEN_MAXIMUM_SEND_PACKETS,
        OID_GEN_LINK_PARAMETERS,
        OID_GEN_NETWORK_LAYER_ADDRESSES,
        OID_GEN_INTERRUPT_MODERATION,
        OID_GEN_XMIT_ERROR,
        OID_GEN_RCV_ERROR,
        OID_GEN_RCV_NO_BUFFER,
        OID_802_3_PERMANENT_ADDRESS,
        OID_802_3_CURRENT_ADDRESS,
        OID_802_3_MULTICAST_LIST,
        OID_802_3_MAXIMUM_LIST_SIZE,
        OID_802_3_RCV_ERROR_ALIGNMENT,
        OID_802_3_XMIT_ONE_COLLISION,
        OID_802_3_XMIT_MORE_COLLISIONS,
        OID_GEN_STATISTICS,
        OID_PNP_CAPABILITIES,
        OID_PNP_SET_POWER,
        OID_PNP_QUERY_POWER,
        OID_GEN_XMIT_OK,
        OID_GEN_RCV_OK,
        OID_GEN_VLAN_ID,
#if NDIS_SUPPORT_NDIS61
// disable WMI custom command on 2008 due to non-filtered NDIS test failure
        OID_GEN_SUPPORTED_GUIDS,
        OID_VENDOR_1,
        OID_VENDOR_2,
        OID_VENDOR_3,
        OID_VENDOR_4,
#endif
        OID_OFFLOAD_ENCAPSULATION,
        OID_TCP_OFFLOAD_PARAMETERS,
#if PARANDIS_SUPPORT_RSS
        OID_GEN_RECEIVE_SCALE_PARAMETERS,
        OID_GEN_RECEIVE_HASH,
#endif
#if PARANDIS_SUPPORT_RSC
        OID_TCP_RSC_STATISTICS
#endif
};

static const NDIS_GUID supportedGUIDs[] =
{
    { NetKvm_LoggingGuid,    OID_VENDOR_1, NetKvm_Logging_SIZE, fNDIS_GUID_TO_OID | fNDIS_GUID_ALLOW_READ | fNDIS_GUID_ALLOW_WRITE },
    { NetKvm_StatisticsGuid, OID_VENDOR_2, NetKvm_Statistics_SIZE, fNDIS_GUID_TO_OID | fNDIS_GUID_ALLOW_READ | fNDIS_GUID_ALLOW_WRITE },
    { NetKvm_RssDiagnosticsGuid, OID_VENDOR_3, NetKvm_RssDiagnostics_SIZE, fNDIS_GUID_TO_OID | fNDIS_GUID_ALLOW_READ | fNDIS_GUID_ALLOW_WRITE },
    { NetKvm_StandbyGuid, OID_VENDOR_4, NetKvm_Standby_SIZE, fNDIS_GUID_TO_OID | fNDIS_GUID_ALLOW_READ | fNDIS_GUID_ALLOW_WRITE },
};

/**********************************************************
        For statistics header
***********************************************************/
static const ULONG SupportedStatisticsFlags =
    NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
    NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
    NDIS_STATISTICS_FLAGS_VALID_DIRECTED_BYTES_RCV |
    NDIS_STATISTICS_FLAGS_VALID_DIRECTED_BYTES_XMIT |
    NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
    NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
    NDIS_STATISTICS_FLAGS_VALID_MULTICAST_BYTES_RCV |
    NDIS_STATISTICS_FLAGS_VALID_BROADCAST_BYTES_RCV |
    NDIS_STATISTICS_FLAGS_VALID_MULTICAST_FRAMES_RCV |
    NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_RCV |
    NDIS_STATISTICS_FLAGS_VALID_MULTICAST_FRAMES_XMIT |
    NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_XMIT |
    NDIS_STATISTICS_FLAGS_VALID_MULTICAST_BYTES_RCV |
    NDIS_STATISTICS_FLAGS_VALID_BROADCAST_BYTES_RCV |
    NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
    NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
    NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR |
    NDIS_STATISTICS_FLAGS_VALID_XMIT_DISCARDS |
    NDIS_STATISTICS_FLAGS_VALID_MULTICAST_BYTES_XMIT |
    NDIS_STATISTICS_FLAGS_VALID_BROADCAST_BYTES_XMIT |
    0;

/**********************************************************
        For miniport registration
***********************************************************/
static const ULONG SupportedStatistics =
    NDIS_STATISTICS_XMIT_OK_SUPPORTED                   |
    NDIS_STATISTICS_RCV_OK_SUPPORTED                    |
    NDIS_STATISTICS_XMIT_ERROR_SUPPORTED                    |
    NDIS_STATISTICS_RCV_ERROR_SUPPORTED                     |
    NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED                 |
    NDIS_STATISTICS_DIRECTED_BYTES_XMIT_SUPPORTED           |
    NDIS_STATISTICS_DIRECTED_FRAMES_XMIT_SUPPORTED          |
    NDIS_STATISTICS_MULTICAST_BYTES_XMIT_SUPPORTED          |
    NDIS_STATISTICS_MULTICAST_FRAMES_XMIT_SUPPORTED         |
    NDIS_STATISTICS_BROADCAST_BYTES_XMIT_SUPPORTED          |
    NDIS_STATISTICS_BROADCAST_FRAMES_XMIT_SUPPORTED         |
    NDIS_STATISTICS_DIRECTED_BYTES_RCV_SUPPORTED            |
    NDIS_STATISTICS_DIRECTED_FRAMES_RCV_SUPPORTED           |
    NDIS_STATISTICS_MULTICAST_BYTES_RCV_SUPPORTED           |
    NDIS_STATISTICS_MULTICAST_FRAMES_RCV_SUPPORTED          |
    NDIS_STATISTICS_BROADCAST_BYTES_RCV_SUPPORTED           |
    NDIS_STATISTICS_BROADCAST_FRAMES_RCV_SUPPORTED          |
    //NDIS_STATISTICS_RCV_CRC_ERROR_SUPPORTED               |
    NDIS_STATISTICS_TRANSMIT_QUEUE_LENGTH_SUPPORTED         |
    //NDIS_STATISTICS_BYTES_RCV_SUPPORTED                     |
    //NDIS_STATISTICS_BYTES_XMIT_SUPPORTED                    |
    NDIS_STATISTICS_RCV_DISCARDS_SUPPORTED                  |
    NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED                |
    NDIS_STATISTICS_XMIT_DISCARDS_SUPPORTED                 |
    0;


/**********************************************************
For common query provides array of supported OID
***********************************************************/
void ParaNdis_GetSupportedOid(PVOID *pOidsArray, PULONG pLength)
{
    *pOidsArray     = SupportedOids;
    *pLength        = sizeof(SupportedOids);
}

/**********************************************************
statistics support information for statistics structure
(it is different from bitmask provided at miniport registration)
***********************************************************/
ULONG ParaNdis6_GetSupportedStatisticsFlags()
{
    return SupportedStatisticsFlags;
}

static void ResetRssStatistics(PARANDIS_ADAPTER *pContext)
{
    pContext->extraStatistics.framesRSSHits = 0;
    pContext->extraStatistics.framesRSSMisses = 0;
    pContext->extraStatistics.framesRSSUnclassified = 0;
    pContext->extraStatistics.framesRSSError = 0;
}

/**********************************************************
OID support information for miniport registration
***********************************************************/
void ParaNdis6_GetSupportedOid(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES *pGenAttributes)
{
    pGenAttributes->SupportedOidList = SupportedOids;
    pGenAttributes->SupportedOidListLength = sizeof(SupportedOids);
    pGenAttributes->SupportedStatistics = SupportedStatistics;
}

// WMI properties (set operation)
static NDIS_STATUS OnSetVendorSpecific1(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(pContext);
    status = ParaNdis_OidSetCopy(pOid, &virtioDebugLevel, sizeof(virtioDebugLevel));
    DPrintf(0, "DebugLevel => %d\n", virtioDebugLevel);
    return status;
}

static NDIS_STATUS OnSetVendorSpecific2(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    ULONG dummy = 0;
    NDIS_STATUS status;
    UNREFERENCED_PARAMETER(pContext);
    status = ParaNdis_OidSetCopy(pOid, &dummy, sizeof(dummy));
    RtlZeroMemory(&pContext->extraStatistics, sizeof(pContext->extraStatistics));
    return status;
}

static NDIS_STATUS OnSetVendorSpecific3(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    ULONG temp = 0;
    NDIS_STATUS status;
    UNREFERENCED_PARAMETER(pContext);
    status = ParaNdis_OidSetCopy(pOid, &temp, sizeof(temp));
#if PARANDIS_SUPPORT_RSS
    switch (temp)
    {
        case 0:
            ParaNdis6_EnableDeviceRssSupport(pContext, false);
            break;
        case 1:
            ParaNdis6_EnableDeviceRssSupport(pContext, true);
            break;
        default:
            break;
    }
#endif
    ResetRssStatistics(pContext);
    return status;
}

static NDIS_STATUS OnSetVendorSpecific4(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    ULONG temp = 0;
    NDIS_STATUS status;
    status = ParaNdis_OidSetCopy(pOid, &temp, sizeof(temp));
    switch (temp)
    {
        case 0:
            // inform VIOPROT uninstalled
            if (virtio_is_feature_enabled(pContext->u64GuestFeatures, VIRTIO_NET_F_STANDBY))
            {
                pContext->bSuppressLinkUp = true;
                ParaNdis_SynchronizeLinkState(pContext);
            }
            break;
        default:
            break;
    }
    return status;
}

/*****************************************************************
Handles NDIS6 specific OID, all the rest handled by common handler
*****************************************************************/
static NDIS_STATUS ParaNdis_OidQuery(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    union _tagtemp
    {
        ULONG                                   StandbySupported;
        NDIS_LINK_SPEED                         LinkSpeed;
        NDIS_INTERRUPT_MODERATION_PARAMETERS    InterruptModeration;
        NDIS_LINK_PARAMETERS                    LinkParameters;
#if PARANDIS_SUPPORT_RSS
        RSS_HASH_KEY_PARAMETERS                 RSSHashKeyParameters;
#endif
#if PARANDIS_SUPPORT_RSC
        NDIS_RSC_STATISTICS_INFO                RSCStatistics;
#endif
    } u;
    NDIS_STATUS  status = NDIS_STATUS_SUCCESS;
    PVOID pInfo  = NULL;
    ULONG ulSize = 0;
    BOOLEAN bFreeInfo = FALSE;
    NetKvm_Statistics wmiStatistics;
    NetKvm_RssDiagnostics rssDiag;

#define SETINFO(field, value) pInfo = &u.##field; ulSize = sizeof(u.##field); u.##field = (value)
    switch(pOid->Oid)
    {
        case OID_GEN_STATISTICS:
            pInfo  = &pContext->Statistics;
            ulSize = sizeof(pContext->Statistics);
            break;
        case OID_GEN_SUPPORTED_GUIDS:
#if NDIS_SUPPORT_NDIS61
            pInfo = (PVOID)&supportedGUIDs;
            ulSize = sizeof(supportedGUIDs);
#endif
            break;
        case OID_VENDOR_1:
            pInfo = &virtioDebugLevel;
            ulSize = sizeof(virtioDebugLevel);
            break;
        case OID_VENDOR_2:
            pInfo = &wmiStatistics;
            ulSize = sizeof(wmiStatistics);
            wmiStatistics.txChecksumOffload = pContext->extraStatistics.framesCSOffload;
            wmiStatistics.txLargeOffload = pContext->extraStatistics.framesLSO;
            wmiStatistics.rxPriority = pContext->extraStatistics.framesRxPriority;
            wmiStatistics.rxChecksumOK = pContext->extraStatistics.framesRxCSHwOK;
            wmiStatistics.rxCoalescedWin = pContext->extraStatistics.framesCoalescedWindows;
            wmiStatistics.rxCoalescedHost = pContext->extraStatistics.framesCoalescedHost;
            break;
        case OID_VENDOR_3:
            pInfo = &rssDiag;
            ulSize = sizeof(rssDiag);
            // bit 0 - RSS supported, bit 1 - Hash supported
            rssDiag.DeviceSupport = pContext->bRSSSupportedByDevice;
            rssDiag.DeviceSupport += 2 * pContext->bHashReportedByDevice;
            rssDiag.rxUnclassified = pContext->extraStatistics.framesRSSUnclassified;
            rssDiag.rxMissed = pContext->extraStatistics.framesRSSMisses;
            rssDiag.rxHits = pContext->extraStatistics.framesRSSHits;
            rssDiag.rxErrors = pContext->extraStatistics.framesRSSError;
            ResetRssStatistics(pContext);
            break;
        case OID_VENDOR_4:
            u.StandbySupported = virtio_is_feature_enabled(pContext->u64GuestFeatures, VIRTIO_NET_F_STANDBY);
            pInfo = &u.StandbySupported;
            ulSize = sizeof(u.StandbySupported);
            break;
        case OID_GEN_INTERRUPT_MODERATION:
            u.InterruptModeration.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            u.InterruptModeration.Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            u.InterruptModeration.Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            u.InterruptModeration.Flags = 0;
            u.InterruptModeration.InterruptModeration = NdisInterruptModerationNotSupported;
            pInfo = &u.InterruptModeration;
            ulSize = sizeof(u.InterruptModeration);
            break;
#if PARANDIS_SUPPORT_RSS
        case OID_GEN_RECEIVE_HASH:
            pInfo = &u.RSSHashKeyParameters;
            ulSize = ParaNdis6_QueryReceiveHash(&pContext->RSSParameters, &u.RSSHashKeyParameters);
            break;
#endif
#if PARANDIS_SUPPORT_RSC
        case OID_TCP_RSC_STATISTICS:
            u.RSCStatistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            u.RSCStatistics.Header.Revision = NDIS_RSC_STATISTICS_REVISION_1;
            u.RSCStatistics.Header.Size = NDIS_SIZEOF_RSC_STATISTICS_REVISION_1;
            u.RSCStatistics.CoalescedOctets = pContext->RSC.Statistics.CoalescedOctets.QuadPart;
            u.RSCStatistics.CoalescedPkts = pContext->RSC.Statistics.CoalescedPkts.QuadPart;
            u.RSCStatistics.CoalesceEvents = pContext->RSC.Statistics.CoalesceEvents.QuadPart;
            u.RSCStatistics.Aborts = 0;

            pInfo = &u.RSCStatistics;
            ulSize = sizeof(u.RSCStatistics);
            break;
#endif
        default:
            return ParaNdis_OidQueryCommon(pContext, pOid);
    }
    if (status == NDIS_STATUS_SUCCESS)
    {
        status = ParaNdis_OidQueryCopy(pOid, pInfo, ulSize, bFreeInfo);
    }
    return status;
}

/**********************************************************
NDIS required procedure for OID support
Parameters:
    context
    PNDIS_OID_REQUEST  pNdisRequest         NDIS oid request
Return value:
    NDIS status, as returned from supporting procedure
    NDIS_STATUS_NOT_SUPPORTED if support for get/set is not defined in the table
***********************************************************/
NDIS_STATUS ParaNdis6_OidRequest(
    NDIS_HANDLE miniportAdapterContext,
    PNDIS_OID_REQUEST  pNdisRequest)
{
    NDIS_STATUS  status = NDIS_STATUS_NOT_SUPPORTED;
    tOidWhatToDo Rules;
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)miniportAdapterContext;
    tOidDesc _oid;
    _oid.Reserved = pNdisRequest;
    ParaNdis_GetOidSupportRules(pNdisRequest->DATA.SET_INFORMATION.Oid, &Rules, OidsDB);
    _oid.ulToDoFlags = Rules.Flags;

    ParaNdis_DebugHistory(pContext, hopOidRequest, NULL, pNdisRequest->DATA.SET_INFORMATION.Oid, pNdisRequest->RequestType, 1);
    DPrintf(Rules.nEntryLevel, "[%s] OID type %d, id 0x%X(%s) of %d\n", __FUNCTION__,
                pNdisRequest->RequestType,
                pNdisRequest->DATA.SET_INFORMATION.Oid,
                Rules.name,
                pNdisRequest->DATA.SET_INFORMATION.InformationBufferLength);

    if (pContext->bSurprizeRemoved) status = NDIS_STATUS_NOT_ACCEPTED;
    else switch(pNdisRequest->RequestType)
    {
        case NdisRequestQueryStatistics:
            if (Rules.Flags & ohfQueryStatOnly)
            {
                // fall to query;
            }
            else
            {
                status = NDIS_STATUS_NOT_SUPPORTED;
                break;
            }
            __fallthrough;
        case NdisRequestQueryInformation:
            if (Rules.Flags & ohfQuery)
            {
                _oid.InformationBuffer = pNdisRequest->DATA.QUERY_INFORMATION.InformationBuffer;
                _oid.InformationBufferLength = pNdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
                _oid.Oid = pNdisRequest->DATA.QUERY_INFORMATION.Oid;
                _oid.pBytesWritten = &pNdisRequest->DATA.QUERY_INFORMATION.BytesWritten;
                _oid.pBytesNeeded = &pNdisRequest->DATA.QUERY_INFORMATION.BytesNeeded;
                _oid.pBytesRead = &pNdisRequest->DATA.QUERY_INFORMATION.BytesWritten;
                status = ParaNdis_OidQuery(pContext, &_oid);
            }
            break;
        case NdisRequestSetInformation:
            if (Rules.Flags & ohfSet)
            {
                if (Rules.OidSetProc)
                {
                    _oid.InformationBuffer = pNdisRequest->DATA.SET_INFORMATION.InformationBuffer;
                    _oid.InformationBufferLength = pNdisRequest->DATA.SET_INFORMATION.InformationBufferLength;
                    _oid.Oid = pNdisRequest->DATA.SET_INFORMATION.Oid;
                    _oid.pBytesWritten = &pNdisRequest->DATA.SET_INFORMATION.BytesRead;
                    _oid.pBytesNeeded = &pNdisRequest->DATA.SET_INFORMATION.BytesNeeded;
                    _oid.pBytesRead = &pNdisRequest->DATA.SET_INFORMATION.BytesRead;
                    // if we need to propagate the OID we need to do that before we
                    // call original handler to be sure the original request is still alive
                    if (Rules.Flags & ohfSetPropagatePre)
                    {
                        ParaNdis_PropagateOid(pContext, _oid.Oid, _oid.InformationBuffer, _oid.InformationBufferLength);
                    }
                    status = Rules.OidSetProc(pContext, &_oid);
                    if (Rules.Flags & ohfSetPropagatePost && status == STATUS_SUCCESS)
                    {
                        ParaNdis_PropagateOid(pContext, _oid.Oid, NULL, 0);
                    }
                }
                else
                {
                    DPrintf(0, "Error: Inconsistent OIDDB, oid %s\n", Rules.name);
                }
            }
            break;
        default:
            DPrintf(Rules.nExitFailLevel, "Error: Unsupported OID type %d, id 0x%X(%s)\n",
                pNdisRequest->RequestType, Rules.oid, Rules.name);
            status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }
    ParaNdis_DebugHistory(pContext, hopOidRequest, NULL, pNdisRequest->DATA.SET_INFORMATION.Oid, status, 0);
    if (status != NDIS_STATUS_PENDING)
    {
        DPrintf(((status != NDIS_STATUS_SUCCESS) ? Rules.nExitFailLevel : Rules.nExitOKLevel),
            "[%s] OID type %d, id 0x%X(%s) (%X)\n", __FUNCTION__,
            pNdisRequest->RequestType, Rules.oid, Rules.name, status);
    }
    return status;
}

/**********************************************************
NDIS required procedure for OID cancel
May be used only for OID returning PENDING
Parameters:
    irrelevant
***********************************************************/
VOID ParaNdis6_OidCancelRequest(
        NDIS_HANDLE hMiniportAdapterContext,
        PVOID pRequestId)
{
    UNREFERENCED_PARAMETER(hMiniportAdapterContext);
    UNREFERENCED_PARAMETER(pRequestId);
}

static void OnSetPowerWorkItem(PVOID  WorkItemContext, NDIS_HANDLE  NdisIoWorkItemHandle)
{
    if (WorkItemContext)
    {
        tPowerWorkItem *pwi = (tPowerWorkItem *)WorkItemContext;
        PARANDIS_ADAPTER *pContext = pwi->pContext;
        PNDIS_OID_REQUEST pRequest = (PNDIS_OID_REQUEST)pwi->request;
        NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    #ifdef DEBUG_TIMING
        LARGE_INTEGER TickCount;
        LARGE_INTEGER SysTime;

        KeQueryTickCount(&TickCount);
        NdisGetCurrentSystemTime(&SysTime);
        DPrintf(0, "\n%s>> CPU #%d, perf-counter %I64d, tick count %I64d, NDIS_sys_time %I64d\n", __FUNCTION__, KeGetCurrentProcessorNumber(), KeQueryPerformanceCounter(NULL).QuadPart,TickCount.QuadPart, SysTime.QuadPart);
    #endif

        if (pwi->state == NetDeviceStateD0)
        {
            status = ParaNdis_PowerOn(pContext);
        }
        else
        {
            ParaNdis_PowerOff(pContext);
        }
        NdisFreeMemory(pwi, 0, 0);
        NdisFreeIoWorkItem(NdisIoWorkItemHandle);
        ParaNdis_DebugHistory(pContext, hopOidRequest, NULL, pRequest->DATA.SET_INFORMATION.Oid, status, 2);
        NdisMOidRequestComplete(pContext->MiniportHandle, pRequest, status);
    }
}


/**********************************************************
NDIS6.X handler of power management
***********************************************************/
NDIS_STATUS ParaNdis_OnSetPower(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS status;
    NDIS_DEVICE_POWER_STATE newState;
#ifdef DEBUG_TIMING
    LARGE_INTEGER TickCount;
    LARGE_INTEGER SysTime;

    KeQueryTickCount(&TickCount);
    NdisGetCurrentSystemTime(&SysTime);
    DPrintf(0, "\n%s>> CPU #%d, perf-counter %I64d, tick count %I64d, NDIS_sys_time %I64d\n", __FUNCTION__, KeGetCurrentProcessorNumber(), KeQueryPerformanceCounter(NULL).QuadPart,TickCount.QuadPart, SysTime.QuadPart);
#endif

    DEBUG_ENTRY(0);

    status = ParaNdis_OidSetCopy(pOid, &newState, sizeof(newState));
    if (status == NDIS_STATUS_SUCCESS)
    {
        NDIS_HANDLE hwo = NdisAllocateIoWorkItem(pContext->MiniportHandle);
        tPowerWorkItem *pwi = (tPowerWorkItem *) ParaNdis_AllocateMemory(pContext, sizeof(tPowerWorkItem));
        status = NDIS_STATUS_FAILURE;
        if (pwi && hwo)
        {
            pwi->pContext = pContext;
            pwi->state    = newState;
            pwi->WorkItem = hwo;
            pwi->request = (PNDIS_OID_REQUEST)pOid->Reserved;
            NdisQueueIoWorkItem(hwo, OnSetPowerWorkItem, pwi);
            status = NDIS_STATUS_PENDING;
        }
        else
        {
            if (pwi) NdisFreeMemory(pwi, 0, 0);
            if (hwo) NdisFreeIoWorkItem(hwo);
        }
    }
    return status;
}

static void DumpOffloadStructure(NDIS_OFFLOAD *po, LPCSTR message)
{
    int level = 1;
    ULONG *pul;
    DPrintf(level, "[%s](%s)\n", __FUNCTION__, message);
    pul = (ULONG *)&po->Checksum.IPv4Transmit;
    DPrintf(level, "CSV4TX:(%d,%d)\n", pul[0], pul[1]);
    pul = (ULONG *)&po->Checksum.IPv4Receive;
    DPrintf(level, "CSV4RX:(%d,%d)\n", pul[0], pul[1]);
    pul = (ULONG *)&po->Checksum.IPv6Transmit;
    DPrintf(level, "CSV6TX:(%d,%d,%d,%d,%d)\n", pul[0], pul[1], pul[2], pul[3], pul[4]);
    pul = (ULONG *)&po->Checksum.IPv6Receive;
    DPrintf(level, "CSV6RX:(%d,%d,%d,%d,%d)\n", pul[0], pul[1], pul[2], pul[3], pul[4]);
    pul = (ULONG *)&po->LsoV1;
    DPrintf(level, "LSOV1 :(%d,%d,%d,%d)\n", pul[0], pul[1], pul[2], pul[3]);
    pul = (ULONG *)&po->LsoV2.IPv4;
    DPrintf(level, "LSO4V2:(%d,%d,%d)\n", pul[0], pul[1], pul[2]);
    pul = (ULONG *)&po->LsoV2.IPv6;
    DPrintf(level, "LSO6V2:(%d,%d,%d,%d)\n", pul[0], pul[1], pul[2], pul[3]);
#ifdef PARANDIS_SUPPORT_RSC
    DPrintf(level, "RSC:(IPv4: Enabled=%ul, IPv6: Enabled=%ul)\n", po->Rsc.IPv4.Enabled, po->Rsc.IPv6.Enabled);
#endif
#ifdef PARANDIS_SUPPORT_USO
    if (po->Header.Revision >= NDIS_OFFLOAD_REVISION_6)
    {
        pul = (ULONG *)&po->UdpSegmentation.IPv4;
        DPrintf(level, "USO4:(%d,%d,%d)\n", pul[0], pul[1], pul[2]);
        pul = (ULONG *)&po->UdpSegmentation.IPv6;
        DPrintf(level, "USO6:(%d,%d,%d,%d)\n", pul[0], pul[1], pul[2], pul[3]);
    }
#endif
}

#define OFFLOAD_FEATURE_SUPPORT(flag) (flag) ? NDIS_OFFLOAD_SUPPORTED : NDIS_OFFLOAD_NOT_SUPPORTED

static void FillOffloadStructure(NDIS_OFFLOAD *po, tOffloadSettingsFlags f)
{
    NDIS_TCP_IP_CHECKSUM_OFFLOAD *pcso = &po->Checksum;
    NDIS_TCP_LARGE_SEND_OFFLOAD_V1 *plso = &po->LsoV1;
    NDIS_TCP_LARGE_SEND_OFFLOAD_V2 *plso2 = &po->LsoV2;
    NdisZeroMemory(po, sizeof(*po));
    po->Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
#if (NDIS_SUPPORT_NDIS630)
    po->Header.Revision = NDIS_OFFLOAD_REVISION_3;
    po->Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_3;
#if (NDIS_SUPPORT_NDIS683)
    if (CheckNdisVersion(6, 83))
    {
        po->Header.Revision = NDIS_OFFLOAD_REVISION_6;
        po->Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_6;
    }
#endif
#else
    po->Header.Revision = NDIS_OFFLOAD_REVISION_1;
    po->Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1;
#endif
    pcso->IPv4Transmit.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    pcso->IPv4Transmit.IpChecksum = OFFLOAD_FEATURE_SUPPORT(f.fTxIPChecksum);
    pcso->IPv4Transmit.IpOptionsSupported = OFFLOAD_FEATURE_SUPPORT(f.fTxIPOptions);
    pcso->IPv4Transmit.TcpChecksum = OFFLOAD_FEATURE_SUPPORT(f.fTxTCPChecksum);
    pcso->IPv4Transmit.TcpOptionsSupported = OFFLOAD_FEATURE_SUPPORT(f.fTxTCPOptions);
    pcso->IPv4Transmit.UdpChecksum = OFFLOAD_FEATURE_SUPPORT(f.fTxUDPChecksum);
    
    pcso->IPv6Transmit.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    pcso->IPv6Transmit.IpExtensionHeadersSupported = OFFLOAD_FEATURE_SUPPORT(f.fTxIPv6Ext);
    pcso->IPv6Transmit.TcpChecksum = OFFLOAD_FEATURE_SUPPORT(f.fTxTCPv6Checksum);
    pcso->IPv6Transmit.TcpOptionsSupported = OFFLOAD_FEATURE_SUPPORT(f.fTxTCPv6Options);
    pcso->IPv6Transmit.UdpChecksum = OFFLOAD_FEATURE_SUPPORT(f.fTxUDPv6Checksum);

    pcso->IPv4Receive.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    pcso->IPv4Receive.IpChecksum = OFFLOAD_FEATURE_SUPPORT(f.fRxIPChecksum);
    pcso->IPv4Receive.IpOptionsSupported = OFFLOAD_FEATURE_SUPPORT(f.fRxIPOptions);
    pcso->IPv4Receive.TcpChecksum = OFFLOAD_FEATURE_SUPPORT(f.fRxTCPChecksum);
    pcso->IPv4Receive.TcpOptionsSupported = OFFLOAD_FEATURE_SUPPORT(f.fRxTCPOptions);
    pcso->IPv4Receive.UdpChecksum = OFFLOAD_FEATURE_SUPPORT(f.fRxUDPChecksum);
    
    pcso->IPv6Receive.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    pcso->IPv6Receive.IpExtensionHeadersSupported = OFFLOAD_FEATURE_SUPPORT(f.fRxIPv6Ext);
    pcso->IPv6Receive.TcpChecksum = OFFLOAD_FEATURE_SUPPORT(f.fRxTCPv6Checksum);
    pcso->IPv6Receive.TcpOptionsSupported = OFFLOAD_FEATURE_SUPPORT(f.fRxTCPv6Options);
    pcso->IPv6Receive.UdpChecksum = OFFLOAD_FEATURE_SUPPORT(f.fRxUDPv6Checksum);

    plso->IPv4.Encapsulation = f.fTxLso ? NDIS_ENCAPSULATION_IEEE_802_3 : NDIS_ENCAPSULATION_NOT_SUPPORTED;
    plso->IPv4.TcpOptions = (f.fTxLsoTCP && f.fTxLso) ? NDIS_OFFLOAD_SUPPORTED : NDIS_OFFLOAD_NOT_SUPPORTED;
    plso->IPv4.IpOptions  = (f.fTxLsoIP  && f.fTxLso) ? NDIS_OFFLOAD_SUPPORTED : NDIS_OFFLOAD_NOT_SUPPORTED;
    plso->IPv4.MaxOffLoadSize = f.fTxLso ? PARANDIS_MAX_LSO_SIZE : 0;
    plso->IPv4.MinSegmentCount = f.fTxLso ? PARANDIS_MIN_LSO_SEGMENTS : 0;
    plso2->IPv4.Encapsulation = plso->IPv4.Encapsulation;
    plso2->IPv4.MaxOffLoadSize = plso->IPv4.MaxOffLoadSize;
    plso2->IPv4.MinSegmentCount = plso->IPv4.MinSegmentCount;
    
    plso2->IPv6.Encapsulation = f.fTxLsov6 ? NDIS_ENCAPSULATION_IEEE_802_3 : NDIS_ENCAPSULATION_NOT_SUPPORTED;
    plso2->IPv6.IpExtensionHeadersSupported = f.fTxLsov6IP ? NDIS_OFFLOAD_SUPPORTED : NDIS_OFFLOAD_NOT_SUPPORTED;
    plso2->IPv6.MaxOffLoadSize = f.fTxLsov6 ? PARANDIS_MAX_LSO_SIZE : 0;
    plso2->IPv6.MinSegmentCount = f.fTxLsov6 ? PARANDIS_MIN_LSO_SEGMENTS : 0;
    plso2->IPv6.TcpOptionsSupported = f.fTxLsov6TCP ? NDIS_OFFLOAD_SUPPORTED : NDIS_OFFLOAD_NOT_SUPPORTED;

#if (NDIS_SUPPORT_NDIS683)
    if (CheckNdisVersion(6, 83))
    {
        po->UdpSegmentation.IPv4.Encapsulation = f.fUsov4 ? NDIS_ENCAPSULATION_IEEE_802_3 : NDIS_ENCAPSULATION_NOT_SUPPORTED;
        po->UdpSegmentation.IPv4.MaxOffLoadSize = f.fUsov4 ? PARANDIS_MAX_LSO_SIZE : 0;
        po->UdpSegmentation.IPv4.MinSegmentCount = f.fUsov4 ? PARANDIS_MIN_LSO_SEGMENTS : 0;

        po->UdpSegmentation.IPv6.Encapsulation = f.fUsov6 ? NDIS_ENCAPSULATION_IEEE_802_3 : NDIS_ENCAPSULATION_NOT_SUPPORTED;
        po->UdpSegmentation.IPv6.MaxOffLoadSize = f.fUsov6 ? PARANDIS_MAX_LSO_SIZE : 0;
        po->UdpSegmentation.IPv6.MinSegmentCount = f.fUsov6 ? PARANDIS_MIN_LSO_SEGMENTS : 0;
    }
#endif
}

void ParaNdis6_FillOffloadConfiguration(PARANDIS_ADAPTER *pContext)
{
    NDIS_OFFLOAD *po = &pContext->ReportedOffloadConfiguration;
    FillOffloadStructure(po, pContext->Offload.flags);
#if PARANDIS_SUPPORT_RSC
    po->Rsc.IPv4.Enabled = pContext->RSC.bIPv4Enabled;
    po->Rsc.IPv6.Enabled = pContext->RSC.bIPv6Enabled;
#endif
}

void ParaNdis6_FillOffloadCapabilities(PARANDIS_ADAPTER *pContext)
{
    tOffloadSettingsFlags f;
    NDIS_OFFLOAD *po = &pContext->ReportedOffloadCapabilities;
    ParaNdis_ResetOffloadSettings(pContext, &f, NULL);
    FillOffloadStructure(po, f);
#if PARANDIS_SUPPORT_RSC
    po->Rsc.IPv4.Enabled = pContext->RSC.bIPv4SupportedHW;
    po->Rsc.IPv6.Enabled = pContext->RSC.bIPv6SupportedHW;
#endif
}

#if 0
static const NDIS_TCP_IP_CHECKSUM_OFFLOAD CheckSumAllOff =
{
    {
        //IPV4Tx
        NDIS_ENCAPSULATION_IEEE_802_3,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE
    },
    {
        //IPV4Rx
        NDIS_ENCAPSULATION_NOT_SUPPORTED,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE
    },
    {
        //IPV6Tx
        NDIS_ENCAPSULATION_NOT_SUPPORTED,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE
    },
    {
        //IPV6Rx
        NDIS_ENCAPSULATION_NOT_SUPPORTED,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE,
        NDIS_OFFLOAD_SET_NO_CHANGE
    }
};


static void BuildOffloadStatusReport(
    const NDIS_TCP_IP_CHECKSUM_OFFLOAD *Current,
    NDIS_TCP_IP_CHECKSUM_OFFLOAD *update)
{
    // see
#if 1
#define SYNC_STRUCT(_struct, field) update->##_struct.##field = (Current->##_struct.##field == NDIS_OFFLOAD_SUPPORTED) ? NDIS_OFFLOAD_SET_ON : NDIS_OFFLOAD_SET_OFF
#define SYNC_FIELD_TX4(field) SYNC_STRUCT(IPv4Transmit,field)
#define SYNC_FIELD_RX4(field) SYNC_STRUCT(IPv4Receive,field)
#define SYNC_FIELD_TX6(field) SYNC_STRUCT(IPv6Transmit,field)
#define SYNC_FIELD_RX6(field) SYNC_STRUCT(IPv6Receive,field)

    SYNC_FIELD_TX4(IpChecksum);
    SYNC_FIELD_TX4(IpOptionsSupported);
    SYNC_FIELD_TX4(TcpChecksum);
    SYNC_FIELD_TX4(TcpOptionsSupported);
    SYNC_FIELD_TX4(UdpChecksum);
    SYNC_FIELD_RX4(IpChecksum);
    SYNC_FIELD_RX4(IpOptionsSupported);
    SYNC_FIELD_RX4(TcpChecksum);
    SYNC_FIELD_RX4(TcpOptionsSupported);
    SYNC_FIELD_RX4(UdpChecksum);
    SYNC_FIELD_TX6(IpExtensionHeadersSupported);
    SYNC_FIELD_TX6(TcpOptionsSupported);
    SYNC_FIELD_TX6(TcpChecksum);
    SYNC_FIELD_TX6(UdpChecksum);
    SYNC_FIELD_RX6(IpExtensionHeadersSupported);
    SYNC_FIELD_RX6(TcpOptionsSupported);
    SYNC_FIELD_RX6(TcpChecksum);
    SYNC_FIELD_RX6(UdpChecksum);
#else
    RtlCopyMemory(&diff->Checksum, &CheckSumAllOff, sizeof(diff->Checksum));
#endif
}
#endif //0

static const char *MakeTxRxString(ULONG bTx, ULONG bRx)
{
    const char *const titles[] = {"None", "Tx", "Rx", "TxRx"};
    ULONG index = !!bTx + 2 * !!bRx;
    return titles[index];
}

static const char *MakeOffloadParameterString(UCHAR val)
{
    switch(val)
    {
        case NDIS_OFFLOAD_PARAMETERS_NO_CHANGE:
            return "NoChange";
        case NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED:
            return "None";
        case NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED:
            return "Tx";
        case NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED:
            return "Rx";
        case NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED:
            return "TxRx";
        default:
            break;
    }
    return "OUT OF RANGE";
}

static void SendOffloadStatusIndication(PARANDIS_ADAPTER *pContext)
{
    NDIS_STATUS_INDICATION  indication;
    PNDIS_OID_REQUEST pRequest = NULL;
    NDIS_OFFLOAD offload = pContext->ReportedOffloadConfiguration;
    NdisZeroMemory(&indication, sizeof(indication));
    indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    indication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    indication.SourceHandle = pContext->MiniportHandle;
    indication.StatusCode = NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG;
    indication.StatusBuffer = &offload;
    indication.StatusBufferSize = sizeof(offload);
    // we shall not do that for offload indications,
    // it is only for case we return NDIS_STATUS_INDICATION_REQUIRED from OID Handler
    // if (pOid) pRequest = (PNDIS_OID_REQUEST)pOid->Reserved;
    if (pRequest)
    {
        indication.RequestId = pRequest->RequestId;
        indication.DestinationHandle = pRequest->RequestHandle;
    }
    DPrintf(0, "Indicating offload change");
    NdisMIndicateStatusEx(pContext->MiniportHandle , &indication);
}

static ULONG SetOffloadField(
    const char *name,
    BOOLEAN isTx,
    ULONG current,
    ULONG isSupportedTx,
    ULONG isSupportedRx,
    UCHAR TxRxValue,
    BOOLEAN *pbFailed)
{
    if (!*pbFailed)
    {
        DPrintf(0, "[%s] IN %s %s: current=%d Supported %s Requested %s\n",
                __FUNCTION__, name, isTx? "TX" : "RX", current,
                MakeTxRxString(isSupportedTx, isSupportedRx),
                MakeOffloadParameterString(TxRxValue));

        switch(TxRxValue)
        {
        case NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED:
            current = 0;
            break;
        case NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED:
            if (!isSupportedTx || !isSupportedRx)
            {
                *pbFailed = TRUE;
                current   = isTx ? isSupportedTx : isSupportedRx;
            }
            else
            {
                current = 1;
            }
            break;
        case NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED:
            if (isTx)
            {
                if (!isSupportedTx) *pbFailed = TRUE;
                current = isSupportedTx;
            }
            else
            {
                current = 0;
            }
            break;
        case NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED:
            if (!isTx)
            {
                if (!isSupportedRx) *pbFailed = TRUE;
                current = isSupportedRx;
            }
            else
            {
                current = 0;
            }
            break;
        case NDIS_OFFLOAD_PARAMETERS_NO_CHANGE:
            __fallthrough;
        default:
            break;
        }

        DPrintf(0, "[%s] OUT %s %s: new=%d (%saccepted)\n", __FUNCTION__, name, isTx? "TX" : "RX", current, *pbFailed ? "NOT " : "");
    }
    return current;
}

static NDIS_STATUS ApplyOffloadConfiguration(PARANDIS_ADAPTER *pContext,
    NDIS_OFFLOAD_PARAMETERS *pop, tOidDesc *pOid)
{
    BOOLEAN bFailed = FALSE;
    tOffloadSettingsFlags fSupported;
    tOffloadSettingsFlags fPresent = pContext->Offload.flags;
    tOffloadSettingsFlags *pf = &fPresent;

    DPrintf(0, "[%s] Requested v%d: V4:IPCS=%s,TCPCS=%s,UDPCS=%s V6:TCPCS=%s,UDPCS=%s\n",
                __FUNCTION__, pop->Header.Revision,
                MakeOffloadParameterString(pop->IPv4Checksum),
                MakeOffloadParameterString(pop->TCPIPv4Checksum),
                MakeOffloadParameterString(pop->UDPIPv4Checksum),
                MakeOffloadParameterString(pop->TCPIPv6Checksum),
                MakeOffloadParameterString(pop->UDPIPv6Checksum));

    ParaNdis_ResetOffloadSettings(pContext, &fSupported, NULL);
    pf->fTxIPChecksum = SetOffloadField("TxIPChecksum", TRUE,
        pf->fTxIPChecksum, fSupported.fTxIPChecksum,
        fSupported.fRxIPChecksum, pop->IPv4Checksum, &bFailed);
    pf->fTxIPOptions = pf->fTxIPChecksum && fSupported.fTxIPOptions;
    pf->fTxTCPChecksum = SetOffloadField("TxTCPChecksum", TRUE,
        pf->fTxTCPChecksum, fSupported.fTxTCPChecksum,
        fSupported.fRxTCPChecksum, pop->TCPIPv4Checksum, &bFailed);
    pf->fTxTCPOptions = pf->fTxTCPChecksum && fSupported.fTxTCPOptions;
    pf->fTxUDPChecksum = SetOffloadField("TxUDPChecksum", TRUE,
        pf->fTxUDPChecksum, fSupported.fTxUDPChecksum,
        fSupported.fRxUDPChecksum, pop->UDPIPv4Checksum, &bFailed);
    pf->fRxIPChecksum = SetOffloadField("RxIPChecksum", FALSE,
        pf->fRxIPChecksum, fSupported.fTxIPChecksum,
        fSupported.fRxIPChecksum, pop->IPv4Checksum, &bFailed);
    pf->fRxIPOptions = pf->fRxIPChecksum && fSupported.fRxIPOptions;
    pf->fRxTCPChecksum = SetOffloadField("RxTCPChecksum", FALSE,
        pf->fRxTCPChecksum, fSupported.fTxTCPChecksum,
        fSupported.fRxTCPChecksum, pop->TCPIPv4Checksum, &bFailed);
    pf->fRxTCPOptions = pf->fRxTCPChecksum && fSupported.fRxTCPOptions;
    pf->fRxUDPChecksum = SetOffloadField("RxUDPChecksum", FALSE,
        pf->fRxUDPChecksum, fSupported.fTxUDPChecksum,
        fSupported.fRxUDPChecksum, pop->UDPIPv4Checksum, &bFailed);
    pf->fTxTCPv6Checksum = SetOffloadField("TxTCPv6Checksum", TRUE,
        pf->fTxTCPv6Checksum, fSupported.fTxTCPv6Checksum,
        fSupported.fRxTCPv6Checksum, pop->TCPIPv6Checksum, &bFailed);
    pf->fTxTCPv6Options = pf->fTxTCPv6Checksum && fSupported.fTxTCPv6Options;
    pf->fTxIPv6Ext = pf->fTxTCPv6Checksum && fSupported.fTxIPv6Ext;
    pf->fTxUDPv6Checksum = SetOffloadField("TxUDPv6Checksum", TRUE,
        pf->fTxUDPv6Checksum, fSupported.fTxUDPv6Checksum,
        fSupported.fRxUDPv6Checksum, pop->UDPIPv6Checksum, &bFailed);
    pf->fRxTCPv6Checksum = SetOffloadField("RxTCPv6Checksum", FALSE,
        pf->fRxTCPv6Checksum, fSupported.fTxTCPv6Checksum,
        fSupported.fRxTCPv6Checksum, pop->TCPIPv6Checksum, &bFailed);
    pf->fRxIPv6Ext = pf->fRxTCPv6Checksum && fSupported.fRxIPv6Ext;
    pf->fRxTCPv6Options = pf->fRxTCPv6Checksum && fSupported.fRxTCPv6Options;
    pf->fRxUDPv6Checksum = SetOffloadField("RxUDPv6Checksum", FALSE,
        pf->fRxUDPv6Checksum, fSupported.fTxUDPv6Checksum,
        fSupported.fRxUDPv6Checksum, pop->UDPIPv6Checksum, &bFailed);


    DPrintf(0, "[%s] Result: TCPv4:%s,UDPv4:%s,IPCS:%s TCPv6:%s,UDPv6:%s\n", __FUNCTION__,
        MakeTxRxString(pf->fTxTCPChecksum, pf->fRxTCPChecksum),
        MakeTxRxString(pf->fTxUDPChecksum, pf->fRxUDPChecksum),
        MakeTxRxString(pf->fTxIPChecksum, pf->fRxIPChecksum),
        MakeTxRxString(pf->fTxTCPv6Checksum, pf->fRxTCPv6Checksum),
        MakeTxRxString(pf->fTxUDPv6Checksum, pf->fRxUDPv6Checksum)
        );

    if (NDIS_OFFLOAD_PARAMETERS_LSOV1_DISABLED == pop->LsoV1 || NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED == pop->LsoV2IPv4)
    {
        pf->fTxLso = 0;
    }
    else if (NDIS_OFFLOAD_PARAMETERS_LSOV1_ENABLED == pop->LsoV1 || NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED  == pop->LsoV2IPv4)
    {
        if (fSupported.fTxLso) pf->fTxLso = 1;
        else
            bFailed = TRUE;
    }

    if (NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED == pop->LsoV2IPv6)
    {
        pf->fTxLsov6 = 0;
    }
    else if (NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED  == pop->LsoV2IPv6)
    {
        if (fSupported.fTxLsov6) pf->fTxLsov6 = 1;
        else
            bFailed = TRUE;
    }

    pf->fTxLsoIP = pf->fTxLso && fSupported.fTxLsoIP;
    pf->fTxLsoTCP = pf->fTxLso && fSupported.fTxLsoTCP;

    if ((NDIS_OFFLOAD_PARAMETERS_LSOV1_DISABLED == pop->LsoV1 && NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED  == pop->LsoV2IPv4) ||
        (NDIS_OFFLOAD_PARAMETERS_LSOV1_ENABLED == pop->LsoV1 && NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED  == pop->LsoV2IPv4))
    {
        bFailed = TRUE;
    }

    DPrintf(0, "[%s] Result(%s): LSO: v4 %d, v6 %d\n", __FUNCTION__,
        bFailed ? "Bad" : "OK",
        pf->fTxLso, pf->fTxLsov6);

#if NDIS_SUPPORT_NDIS683
    if (pop->Header.Revision >= NDIS_OFFLOAD_PARAMETERS_REVISION_5 &&
        pop->Header.Size >= NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_5)
    {
        if (NDIS_OFFLOAD_PARAMETERS_USO_DISABLED == pop->UdpSegmentation.IPv4)
        {
            pf->fUsov4 = 0;
        }
        else if (NDIS_OFFLOAD_PARAMETERS_USO_ENABLED == pop->UdpSegmentation.IPv4)
        {
            if (fSupported.fUsov4) pf->fUsov4 = 1;
            else
                bFailed = TRUE;
        }

        if (NDIS_OFFLOAD_PARAMETERS_USO_DISABLED == pop->UdpSegmentation.IPv6)
        {
            pf->fUsov6 = 0;
        }
        else if (NDIS_OFFLOAD_PARAMETERS_USO_ENABLED == pop->UdpSegmentation.IPv6)
        {
            if (fSupported.fUsov6) pf->fUsov6 = 1;
            else
                bFailed = TRUE;
        }
    }
#endif

    DPrintf(0, "[%s] Result (%s): USO: v4 %d, v6 %d\n", __FUNCTION__,
        bFailed ? "Bad" : "OK", pf->fUsov4, pf->fUsov6);

    if (bFailed && pOid)
        return NDIS_STATUS_INVALID_PARAMETER;

    // at initialization time due to absence of pOid
    // we initialize the ReportedOffloadConfiguration
    // according to our capabilities
    pContext->Offload.flags = fPresent;
    ParaNdis6_FillOffloadConfiguration(pContext);

    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS OnSetRSCParameters(PPARANDIS_ADAPTER pContext, PNDIS_OFFLOAD_PARAMETERS op)
{
#if PARANDIS_SUPPORT_RSC

    if(op->Header.Revision < NDIS_OFFLOAD_PARAMETERS_REVISION_3)
        return NDIS_STATUS_SUCCESS;

    if((op->RscIPv4 != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE) &&
        (!pContext->RSC.bIPv4SupportedSW || !pContext->RSC.bIPv4SupportedHW))
        return NDIS_STATUS_NOT_SUPPORTED;

    if((op->RscIPv6 != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE) &&
        (!pContext->RSC.bIPv6SupportedSW || !pContext->RSC.bIPv6SupportedHW))
        return NDIS_STATUS_NOT_SUPPORTED;

    if(op->RscIPv4 != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE)
        pContext->RSC.bIPv4Enabled = (op->RscIPv4 == NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED);

    if(op->RscIPv6 != NDIS_OFFLOAD_PARAMETERS_NO_CHANGE)
        pContext->RSC.bIPv6Enabled = (op->RscIPv6 == NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED);

    ParaNdis_DeviceConfigureRSC(pContext);
#else
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(op);
#endif /* PARANDIS_SUPPORT_RSC */

    return STATUS_SUCCESS;
}

NDIS_STATUS OnSetOffloadParameters(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS status;
    NDIS_OFFLOAD_PARAMETERS op = {0};

    if(pOid->InformationBufferLength < NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_1)
        return NDIS_STATUS_BUFFER_TOO_SHORT;

    status = ParaNdis_OidSetCopy(pOid, &op, sizeof(op));

    if(status != NDIS_STATUS_SUCCESS)
        return status;

    status = OnSetRSCParameters(pContext, &op);

    if(status != NDIS_STATUS_SUCCESS)
        return status;

    status = ApplyOffloadConfiguration(pContext, &op, pOid);

    if(status != NDIS_STATUS_SUCCESS)
        return status;

    DumpOffloadStructure(&pContext->ReportedOffloadConfiguration, "Updated");
    SendOffloadStatusIndication(pContext);

    return NDIS_STATUS_SUCCESS;
}

void ParaNdis6_ApplyOffloadPersistentConfiguration(PARANDIS_ADAPTER *pContext)
{
    // see NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED and below
    // this unobvious code translates setting from registry conventions
    // (where 0 = disable) to NDIS offload parameters (where 1 = disable)
    pContext->InitialOffloadParameters.LsoV1++;
    pContext->InitialOffloadParameters.LsoV2IPv4++;
    pContext->InitialOffloadParameters.LsoV2IPv6++;
    pContext->InitialOffloadParameters.IPv4Checksum++;
    pContext->InitialOffloadParameters.TCPIPv4Checksum++;
    pContext->InitialOffloadParameters.TCPIPv6Checksum++;
    pContext->InitialOffloadParameters.UDPIPv4Checksum++;
    pContext->InitialOffloadParameters.UDPIPv6Checksum++;
#if (NDIS_SUPPORT_NDIS683)
    pContext->InitialOffloadParameters.UdpSegmentation.IPv4++;
    pContext->InitialOffloadParameters.UdpSegmentation.IPv6++;
#endif

    ApplyOffloadConfiguration(pContext,&pContext->InitialOffloadParameters, NULL);
    ParaNdis6_FillOffloadCapabilities(pContext);
}


NDIS_STATUS OnSetOffloadEncapsulation(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_OFFLOAD_ENCAPSULATION encaps;
    NDIS_STATUS status;
    status = ParaNdis_OidSetCopy(pOid, &encaps, sizeof(encaps));
    if (status == NDIS_STATUS_SUCCESS)
    {
        ULONG *pul = (ULONG *)&encaps;
        status = NDIS_STATUS_INVALID_PARAMETER;
        DPrintf(1, "[%s] %08lX, %08lX, %08lX, %08lX, %08lX, %08lX, %08lX\n", __FUNCTION__,
                pul[0],pul[1],pul[2],pul[3],pul[4],pul[5],pul[6]);
        if (encaps.Header.Size == sizeof(encaps) &&
            encaps.Header.Revision == NDIS_OFFLOAD_ENCAPSULATION_REVISION_1 &&
            encaps.Header.Type == NDIS_OBJECT_TYPE_OFFLOAD_ENCAPSULATION &&
            (encaps.IPv4.Enabled != NDIS_OFFLOAD_SET_ON || (encaps.IPv4.EncapsulationType & NDIS_ENCAPSULATION_IEEE_802_3)) &&
            (encaps.IPv6.Enabled != NDIS_OFFLOAD_SET_ON || (encaps.IPv6.EncapsulationType & NDIS_ENCAPSULATION_IEEE_802_3))
            )
         {
            DPrintf(0, "[%s] V4 types 0x%lX, header %d, enabled %d\n", __FUNCTION__,
                encaps.IPv4.EncapsulationType,
                encaps.IPv4.HeaderSize,
                encaps.IPv4.Enabled);
            DPrintf(0, "[%s] V6 types 0x%lX, header %d, enabled %d\n", __FUNCTION__,
                encaps.IPv6.EncapsulationType,
                encaps.IPv6.HeaderSize,
                encaps.IPv6.Enabled);

            if (encaps.IPv4.Enabled == NDIS_OFFLOAD_SET_ON && encaps.IPv6.Enabled == NDIS_OFFLOAD_SET_ON && 
                encaps.IPv6.HeaderSize == encaps.IPv4.HeaderSize)
            {
                pContext->Offload.ipHeaderOffset = encaps.IPv4.HeaderSize;
                pContext->bOffloadv4Enabled = TRUE;
                pContext->bOffloadv6Enabled = TRUE;
                status = NDIS_STATUS_SUCCESS;
            }
            else if (encaps.IPv4.Enabled == NDIS_OFFLOAD_SET_OFF && encaps.IPv6.Enabled == NDIS_OFFLOAD_SET_ON)
            {
                pContext->bOffloadv4Enabled = FALSE;
                pContext->bOffloadv6Enabled = TRUE;
                pContext->Offload.ipHeaderOffset = encaps.IPv6.HeaderSize;
                status = NDIS_STATUS_SUCCESS;
            }
            else if (encaps.IPv6.Enabled == NDIS_OFFLOAD_SET_OFF && encaps.IPv4.Enabled == NDIS_OFFLOAD_SET_ON)
            {
                pContext->bOffloadv4Enabled = TRUE;
                pContext->bOffloadv6Enabled = FALSE;
                pContext->Offload.ipHeaderOffset = encaps.IPv4.HeaderSize;
                status = NDIS_STATUS_SUCCESS;
            }
            else if (encaps.IPv4.Enabled == NDIS_OFFLOAD_SET_OFF && encaps.IPv6.Enabled == NDIS_OFFLOAD_SET_OFF)
            {
                pContext->bOffloadv4Enabled = FALSE;
                pContext->bOffloadv6Enabled = FALSE;
                status = NDIS_STATUS_SUCCESS;
            }
         }
    }
    return status;
}


/*******************************************************************
Provides startup information about offload capabilities and settings
********************************************************************/
NDIS_STATUS ParaNdis6_GetRegistrationOffloadInfo(
        PARANDIS_ADAPTER *pContext,
        NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES *poa)
{
    NDIS_STATUS  status = NDIS_STATUS_NOT_SUPPORTED;
    NdisZeroMemory(poa, sizeof(*poa));
    /* something supported? */
    if (pContext->Offload.flagsValue)
    {
        poa->Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES;
        poa->Header.Revision = NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1;
        poa->Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1;
        poa->DefaultOffloadConfiguration = &pContext->ReportedOffloadConfiguration;
        poa->HardwareOffloadCapabilities = &pContext->ReportedOffloadCapabilities;
        DumpOffloadStructure(poa->HardwareOffloadCapabilities, "Initial Capabilities");
        DumpOffloadStructure(poa->DefaultOffloadConfiguration, "Initial Configuration");
        status = NDIS_STATUS_SUCCESS;
    }
    return status;
}

#if NDIS_SUPPORT_NDIS620

void ParaNdis6_Fill620PowerCapabilities(PNDIS_PM_CAPABILITIES pPower620Caps)
{
    NdisZeroMemory(pPower620Caps, sizeof(*pPower620Caps));
    pPower620Caps->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    pPower620Caps->Header.Revision = NDIS_PM_CAPABILITIES_REVISION_1;
    pPower620Caps->Header.Size = NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_1;
#if NDIS_SUPPORT_NDIS650
    // Rev 2 can be used also for 6.30, but we keep the code for <= Win 8.1 backward compatible
    // for all the Win 10 and up we use Rev 2
    pPower620Caps->Header.Revision = NDIS_PM_CAPABILITIES_REVISION_2;
    pPower620Caps->Header.Size = NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_2;
#endif
    // part of WOL support
    pPower620Caps->SupportedWoLPacketPatterns = 0;
    pPower620Caps->NumTotalWoLPatterns = 0;
    pPower620Caps->MaxWoLPatternSize = 0;
    pPower620Caps->MaxWoLPatternOffset = 0;
    pPower620Caps->MaxWoLPacketSaveBuffer = 0;

    // part of protocol offload support (ARP, NS IPv6)
    pPower620Caps->SupportedProtocolOffloads = 0;
    pPower620Caps->NumArpOffloadIPv4Addresses = 0;
    pPower620Caps->NumNSOffloadIPv6Addresses = 0;

    // if wake on magic packet supported
    pPower620Caps->MinMagicPacketWakeUp =
        (pPower620Caps->SupportedWoLPacketPatterns & NDIS_PM_WOL_MAGIC_PACKET_SUPPORTED) ?
        NdisDeviceStateD3 : NdisDeviceStateUnspecified;

    // if wake on link change supported
    pPower620Caps->MinLinkChangeWakeUp = NdisDeviceStateUnspecified;

    // if wake on pattern supported
    pPower620Caps->MinPatternWakeUp =
        (pPower620Caps->SupportedWoLPacketPatterns & ~NDIS_PM_WOL_MAGIC_PACKET_SUPPORTED) ?
        NdisDeviceStateD3 : NdisDeviceStateUnspecified;
}
#endif

NDIS_STATUS OnSetLinkParameters(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_LINK_PARAMETERS params;
    NDIS_STATUS status;

    UNREFERENCED_PARAMETER(pContext);

    status = ParaNdis_OidSetCopy(pOid, &params, sizeof(params));
    if (status == NDIS_STATUS_SUCCESS)
    {
        status = NDIS_STATUS_NOT_ACCEPTED;
        DPrintf(0, "[%s] requested:\n", __FUNCTION__);
        DPrintf(0, "Tx speed 0x%llx, Rx speed 0x%llx\n", params.XmitLinkSpeed, params.RcvLinkSpeed);
        DPrintf(0, "Duplex %d, PauseFn %d, AutoNeg 0x%X\n",
            params.MediaDuplexState, params.PauseFunctions, params.AutoNegotiationFlags);
    }
    return status;
}
