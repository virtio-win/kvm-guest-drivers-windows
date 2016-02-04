/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: ParaNdis6-Oid.c
 *
 * This file contains NDIS OID support procedures, common for NDIS5 and NDIS6
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "ParaNdis-Oid.h"
#include "kdebugprint.h"

static const char VendorName[] = "Red Hat";

static UCHAR FORCEINLINE hexdigit(UCHAR nibble)
{
    UCHAR c = nibble & 0xf;
    c += (c <= 9) ? 0 : 7;
    c += '0';
    return c;
}

/**********************************************************
Common implementation of copy operation when OID is set
pOid->Flags (if used) controls when the source data may be truncated or padded on copy
Parameters:
        tOidDesc *pOid - descriptor of OID
        PVOID pDest    - buffer to receive data sent by NDIS
        ULONG ulSize   - size of data to copy
Return value:
    SUCCESS or NDIS error code if target buffer size is wrong
Rules:

PDEST       <>OK        SIZE    PAYLOAD SZ
NULL        any         n/a         any             fail
BUFF        any         0           any             success, none copied
BUFF        any         SZ          ==SZ            success, copied SZ
BUFF        !lessok     SZ          <SZ             fail (small), none copied
BUFF        !moreok     SZ          >SZ             fail (overflow), none copied
BUFF        lessok      SZ          <SZ             success, SZ cleared, payload sz copied
BUFF        moreok      SZ          >SZ             success, copied SZ
***************************************************/
NDIS_STATUS ParaNdis_OidSetCopy(
        tOidDesc *pOid,
        PVOID pDest,
        ULONG ulSize)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    if (!pDest)
    {
        status = NDIS_STATUS_INVALID_OID;
        *(pOid->pBytesRead) = 0;
        *(pOid->pBytesNeeded) = 0;
    }
    else if (ulSize)
    {
        if (pOid->InformationBufferLength < ulSize)
        {
            if (pOid->ulToDoFlags & ohfSetLessOK)
            {
                *(pOid->pBytesRead) = pOid->InformationBufferLength;
                NdisZeroMemory(pDest, ulSize);
                NdisMoveMemory(pDest, pOid->InformationBuffer, pOid->InformationBufferLength);
            }
            else
            {
                status = NDIS_STATUS_BUFFER_TOO_SHORT;
                *(pOid->pBytesRead) = 0;
                *(pOid->pBytesNeeded)  = ulSize;
            }
        }
        else if (pOid->InformationBufferLength == ulSize || (pOid->ulToDoFlags & ohfSetMoreOK))
        {
            *(pOid->pBytesRead) = ulSize;
            NdisMoveMemory(pDest, pOid->InformationBuffer, ulSize);
        }
        else
        {
            status = NDIS_STATUS_BUFFER_OVERFLOW;
            *(pOid->pBytesNeeded)  = ulSize;
            *(pOid->pBytesRead) = 0;
        }
    }
    else
    {
        *(pOid->pBytesRead) = pOid->InformationBufferLength;
    }
    return status;
}


/**********************************************************
Common handler of setting packet filter
***********************************************************/
NDIS_STATUS ParaNdis_OnSetPacketFilter(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    ULONG newValue;
    NDIS_STATUS status = ParaNdis_OidSetCopy(
        pOid,
        &newValue,
        sizeof(newValue));

    if (newValue & ~PARANDIS_PACKET_FILTERS)
        status = NDIS_STATUS_INVALID_DATA;

    if (status == NDIS_STATUS_SUCCESS)
    {
        pContext->PacketFilter = newValue;
        DPrintf(1, ("[%s] PACKET FILTER SET TO %x\n", __FUNCTION__, pContext->PacketFilter));
        ParaNdis_UpdateDeviceFilters(pContext);
    }
    return status;
}

void ParaNdis_FillPowerCapabilities(PNDIS_PNP_CAPABILITIES pCaps)
{
    NdisZeroMemory(pCaps, sizeof(*pCaps));
    pCaps->WakeUpCapabilities.MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
    pCaps->WakeUpCapabilities.MinPatternWakeUp = NdisDeviceStateUnspecified;
    pCaps->WakeUpCapabilities.MinLinkChangeWakeUp = NdisDeviceStateUnspecified;
}


/**********************************************************
Common handler of setting multicast list
***********************************************************/
NDIS_STATUS ParaNdis_OnOidSetMulticastList(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS status;
    status = ParaNdis_SetMulticastList(
        pContext,
        pOid->InformationBuffer,
        pOid->InformationBufferLength,
        pOid->pBytesRead,
        pOid->pBytesNeeded);
    ParaNdis_UpdateDeviceFilters(pContext);
    return status;
}

/**********************************************************
Common helper of copy operation on GET OID
Copies data from specified location to NDIS buffer
64-bit variable will be casted to 32-bit, if specified on pOid->Flags

Parameters:
        tOidDesc *pOid      - descriptor of OID
        PVOID pInfo         - source to copy from
        ULONG ulSize        - source info size
Return value:
    SUCCESS or kind of failure when the dest buffer size is wrong
Comments:
pInfo   must be non-NULL, otherwise error returned
ulSize  may be 0, then SUCCESS returned without copy
***********************************************************/
NDIS_STATUS ParaNdis_OidQueryCopy(
        tOidDesc *pOid,
        PVOID pInfo,
        ULONG ulSize,
        BOOLEAN bFreeInfo)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    *(pOid->pBytesNeeded)  = ulSize;
    if (!pInfo)
    {
        status = NDIS_STATUS_INVALID_OID;
        *(pOid->pBytesWritten) = 0;
        *(pOid->pBytesNeeded)  = 0;
    }
    else if (pOid->InformationBufferLength >= ulSize)
    {
        if (ulSize) NdisMoveMemory(pOid->InformationBuffer, pInfo, ulSize);
        *(pOid->pBytesWritten) = ulSize;
        *(pOid->pBytesNeeded) = 0;
    }
    else if ((pOid->ulToDoFlags & ohfQuery3264) && pOid->InformationBufferLength == sizeof(ULONG) && ulSize == sizeof(ULONG64))
    {
        ULONG64 u64 = *(ULONG64 *)pInfo;
        ULONG   ul  = (ULONG)u64;
        NdisMoveMemory(pOid->InformationBuffer, &ul, sizeof(ul));
        *(pOid->pBytesWritten) = sizeof(ul);
    }
    else
    {
        status = NDIS_STATUS_BUFFER_TOO_SHORT;
        *(pOid->pBytesWritten) = 0;
    }
    if (bFreeInfo && pInfo)
    {
        NdisFreeMemory(pInfo, 0, 0);
    }
    return status;
}

/**********************************************************
Common handler of Oid queries
Parameters:
    context
    tOidDesc *pOid - filled descriptor of OID operation
Return value:
    SUCCESS or kind of failure
***********************************************************/
NDIS_STATUS ParaNdis_OidQueryCommon(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS  status = NDIS_STATUS_SUCCESS;
    PVOID pInfo  = NULL;
    ULONG ulSize = 0;
    BOOLEAN bFreeInfo = FALSE;
    union _tagtemp
    {
        NDIS_MEDIUM                             Medium;
        ULONG64                                 ul64;
        ULONG                                   ul;
        USHORT                                  us;
        NDIS_PNP_CAPABILITIES                   PMCaps;
    } u;
#define SETINFO(field, value) pInfo = &u.##field; ulSize = sizeof(u.##field); u.##field = (value)
    switch (pOid->Oid)
    {
    case OID_GEN_SUPPORTED_LIST:
        ParaNdis_GetSupportedOid(&pInfo, &ulSize);
        break;
    case OID_GEN_HARDWARE_STATUS:
        SETINFO(ul, NdisHardwareStatusReady);
        break;
    case OID_GEN_MEDIA_SUPPORTED:
        __fallthrough;
    case OID_GEN_MEDIA_IN_USE:
        SETINFO(Medium, NdisMedium802_3);
        break;
    case OID_GEN_MAXIMUM_LOOKAHEAD:
        SETINFO(ul, pContext->MaxPacketSize.nMaxFullSizeOsRx);
        break;
    case OID_GEN_CURRENT_LOOKAHEAD:
        if (!pContext->DummyLookAhead) pContext->DummyLookAhead = pContext->MaxPacketSize.nMaxFullSizeOsRx;
        pInfo  = &pContext->DummyLookAhead;
        ulSize = sizeof(pContext->DummyLookAhead);
        break;
    case OID_GEN_MAXIMUM_FRAME_SIZE:
        SETINFO(ul, pContext->MaxPacketSize.nMaxDataSize);
        break;
    case OID_GEN_TRANSMIT_BUFFER_SPACE:
    {
        ULONG totalFreeTxDescriptors = 0;

        for (UINT i = 0; i < pContext->nPathBundles; i++)
        {
            totalFreeTxDescriptors += pContext->pPathBundles[i].txPath.GetFreeTXDescriptors();
        }
        SETINFO(ul, pContext->MaxPacketSize.nMaxFullSizeOS * totalFreeTxDescriptors);
        break;
    }
    case OID_GEN_RECEIVE_BUFFER_SPACE:
        SETINFO(ul, pContext->MaxPacketSize.nMaxFullSizeOsRx * pContext->NetMaxReceiveBuffers);
        break;
    case OID_GEN_RECEIVE_BLOCK_SIZE:
        SETINFO(ul, pContext->MaxPacketSize.nMaxFullSizeOsRx);
        break;
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
        __fallthrough;
    case OID_GEN_MAXIMUM_TOTAL_SIZE:
        SETINFO(ul, pContext->MaxPacketSize.nMaxFullSizeOS);
        break;
    case OID_GEN_TRANSMIT_QUEUE_LENGTH:
    {
        // TODO: this is not completely correct, but only if
        // the TX queue is not full
        ULONG totalFreeTxDescriptors = 0;

        for (UINT i = 0; i < pContext->nPathBundles; i++)
        {
            totalFreeTxDescriptors += pContext->pPathBundles[i].txPath.GetFreeTXDescriptors();
        }
        SETINFO(ul, pContext->maxFreeTxDescriptors - totalFreeTxDescriptors);
        break;
    }
    case OID_GEN_VENDOR_ID:
        SETINFO(ul, 0x00ffffff);
        break;
    case OID_GEN_VENDOR_DESCRIPTION:
        pInfo = (PVOID)VendorName;
        ulSize = sizeof(VendorName);
        break;

    case OID_GEN_VENDOR_DRIVER_VERSION:
        SETINFO(ul, (NDIS_MINIPORT_MAJOR_VERSION << 16) | NDIS_MINIPORT_MINOR_VERSION);
        break;
    case OID_GEN_CURRENT_PACKET_FILTER:
        pInfo = &pContext->PacketFilter;
        ulSize = sizeof(pContext->PacketFilter);
        break;
    case OID_GEN_DRIVER_VERSION:
        SETINFO(us, ((NDIS_MINIPORT_MAJOR_VERSION << 8) | NDIS_MINIPORT_MINOR_VERSION));
        break;
    case OID_GEN_MAC_OPTIONS:
        {
            ULONG options = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                NDIS_MAC_OPTION_NO_LOOPBACK;
            if (IsPrioritySupported(pContext))
                options |= NDIS_MAC_OPTION_8021P_PRIORITY;
            if (IsVlanSupported(pContext))
                options |= NDIS_MAC_OPTION_8021Q_VLAN;
            SETINFO(ul, options);
        }
        break;
    case OID_GEN_MAXIMUM_SEND_PACKETS:
        // This OID is obsolete according to the documentation
        // but HCK test suite fails if driver doesn't support it
        // HCK test that fails: 1c_OidsDeviceIoControl
    {
        ULONG totalFreeTxDescriptors = 0;

        for (UINT i = 0; i < pContext->nPathBundles; i++)
        {
            totalFreeTxDescriptors += pContext->pPathBundles[i].txPath.GetFreeTXDescriptors();
        }

        SETINFO(ul, totalFreeTxDescriptors);
        break;
    }
    case OID_802_3_PERMANENT_ADDRESS:
        pInfo = pContext->PermanentMacAddress;
        ulSize = sizeof(pContext->PermanentMacAddress);
        break;
    case OID_802_3_CURRENT_ADDRESS:
        pInfo = pContext->CurrentMacAddress;
        ulSize = sizeof(pContext->CurrentMacAddress);
        break;
    case OID_PNP_QUERY_POWER:
        // size if 0, just to indicate success
        pInfo = &status;
        break;
    case OID_GEN_DIRECTED_BYTES_XMIT:
        SETINFO(ul64, pContext->Statistics.ifHCOutUcastOctets);
        break;
    case OID_GEN_DIRECTED_FRAMES_XMIT:
        SETINFO(ul64, pContext->Statistics.ifHCOutUcastPkts);
        break;
    case OID_GEN_MULTICAST_BYTES_XMIT:
        SETINFO(ul64, pContext->Statistics.ifHCOutMulticastOctets);
        break;
    case OID_GEN_MULTICAST_FRAMES_XMIT:
        SETINFO(ul64, pContext->Statistics.ifHCOutMulticastPkts);
        break;
    case OID_GEN_BROADCAST_BYTES_XMIT:
        SETINFO(ul64, pContext->Statistics.ifHCOutBroadcastOctets);
        break;
    case OID_GEN_BROADCAST_FRAMES_XMIT:
        SETINFO(ul64, pContext->Statistics.ifHCOutBroadcastPkts);
        break;
    case OID_GEN_DIRECTED_BYTES_RCV:
        SETINFO(ul64, pContext->Statistics.ifHCInUcastOctets);
        break;
    case OID_GEN_DIRECTED_FRAMES_RCV:
        SETINFO(ul64, pContext->Statistics.ifHCInUcastPkts);
        break;
    case OID_GEN_MULTICAST_BYTES_RCV:
        SETINFO(ul64, pContext->Statistics.ifHCInMulticastOctets);
        break;
    case OID_GEN_MULTICAST_FRAMES_RCV:
        SETINFO(ul64, pContext->Statistics.ifHCInMulticastPkts);
        break;
    case OID_GEN_BROADCAST_BYTES_RCV:
        SETINFO(ul64, pContext->Statistics.ifHCInBroadcastOctets);
        break;
    case OID_GEN_BROADCAST_FRAMES_RCV:
        SETINFO(ul64, pContext->Statistics.ifHCInBroadcastPkts);
        break;
    case OID_GEN_XMIT_OK:
        SETINFO(ul64,
            pContext->Statistics.ifHCOutUcastPkts +
            pContext->Statistics.ifHCOutMulticastPkts +
            pContext->Statistics.ifHCOutBroadcastPkts);
        break;
    case OID_GEN_RCV_OK:
        SETINFO(ul64,
            pContext->Statistics.ifHCInUcastPkts +
            pContext->Statistics.ifHCInMulticastPkts +
            pContext->Statistics.ifHCInBroadcastPkts);
        DPrintf(4, ("[%s] Total frames %I64u\n", __FUNCTION__, u.ul64));
        break;
    case OID_GEN_XMIT_ERROR:
        SETINFO(ul64, pContext->Statistics.ifOutErrors );
        break;
    case OID_GEN_RCV_ERROR:
        __fallthrough;
    case OID_GEN_RCV_NO_BUFFER:
        __fallthrough;
    case OID_802_3_RCV_OVERRUN:
        __fallthrough;
    case OID_GEN_RCV_CRC_ERROR:
        __fallthrough;
    case OID_802_3_RCV_ERROR_ALIGNMENT:
        __fallthrough;
    case OID_802_3_XMIT_UNDERRUN:
        __fallthrough;
    case OID_802_3_XMIT_ONE_COLLISION:
        __fallthrough;
    case OID_802_3_XMIT_DEFERRED:
        __fallthrough;
    case OID_802_3_XMIT_MAX_COLLISIONS:
        __fallthrough;
    case OID_802_3_XMIT_MORE_COLLISIONS:
        __fallthrough;
    case OID_802_3_XMIT_HEARTBEAT_FAILURE:
        __fallthrough;
    case OID_802_3_XMIT_TIMES_CRS_LOST:
        __fallthrough;
    case OID_802_3_XMIT_LATE_COLLISIONS:
        SETINFO(ul64, 0);
        break;
    case OID_802_3_MULTICAST_LIST:
        pInfo = pContext->MulticastData.MulticastList;
        ulSize = pContext->MulticastData.nofMulticastEntries * ETH_ALEN;
        break;
    case OID_802_3_MAXIMUM_LIST_SIZE:
        SETINFO(ul, PARANDIS_MULTICAST_LIST_SIZE);
        break;
    case OID_PNP_CAPABILITIES:
        pInfo  = &u.PMCaps;
        ulSize = sizeof(u.PMCaps);
        ParaNdis_FillPowerCapabilities(&u.PMCaps);
        break;
    case OID_802_3_MAC_OPTIONS:
        SETINFO(ul, 0);
        break;
    case OID_GEN_VLAN_ID:
        SETINFO(ul, pContext->VlanId);
        if (!IsVlanSupported(pContext))
            status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    case OID_PNP_ENABLE_WAKE_UP:
        SETINFO(ul, pContext->ulEnableWakeup);
        break;
    default:
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        status = ParaNdis_OidQueryCopy(pOid, pInfo, ulSize, bFreeInfo);
    }

    return status;
}


/**********************************************************
    Just gets OID name
***********************************************************/
const char *ParaNdis_OidName(NDIS_OID oid)
{
#undef MAKECASE
#define MAKECASE(id) case id: return #id;
    switch (oid)
    {
    MAKECASE(OID_GEN_SUPPORTED_LIST)
    MAKECASE(OID_GEN_HARDWARE_STATUS)
    MAKECASE(OID_GEN_MEDIA_SUPPORTED)
    MAKECASE(OID_GEN_MEDIA_IN_USE)
    MAKECASE(OID_GEN_MAXIMUM_LOOKAHEAD)
    MAKECASE(OID_GEN_MAXIMUM_FRAME_SIZE)
    MAKECASE(OID_GEN_LINK_SPEED)
    MAKECASE(OID_GEN_TRANSMIT_BUFFER_SPACE)
    MAKECASE(OID_GEN_RECEIVE_BUFFER_SPACE)
    MAKECASE(OID_GEN_TRANSMIT_BLOCK_SIZE)
    MAKECASE(OID_GEN_RECEIVE_BLOCK_SIZE)
    MAKECASE(OID_GEN_VENDOR_ID)
    MAKECASE(OID_GEN_VENDOR_DESCRIPTION)
    MAKECASE(OID_GEN_CURRENT_PACKET_FILTER)
    MAKECASE(OID_GEN_CURRENT_LOOKAHEAD)
    MAKECASE(OID_GEN_DRIVER_VERSION)
    MAKECASE(OID_GEN_MAXIMUM_TOTAL_SIZE)
    MAKECASE(OID_GEN_PROTOCOL_OPTIONS)
    MAKECASE(OID_GEN_MAC_OPTIONS)
    MAKECASE(OID_GEN_MAXIMUM_SEND_PACKETS)
    MAKECASE(OID_GEN_VENDOR_DRIVER_VERSION)
    MAKECASE(OID_GEN_SUPPORTED_GUIDS)
    MAKECASE(OID_GEN_TRANSPORT_HEADER_OFFSET)
    MAKECASE(OID_GEN_MEDIA_CAPABILITIES)
    MAKECASE(OID_GEN_PHYSICAL_MEDIUM)
    MAKECASE(OID_GEN_XMIT_OK)
    MAKECASE(OID_GEN_RCV_OK)
    MAKECASE(OID_GEN_XMIT_ERROR)
    MAKECASE(OID_GEN_RCV_ERROR)
    MAKECASE(OID_GEN_RCV_NO_BUFFER)
    MAKECASE(OID_GEN_DIRECTED_BYTES_XMIT)
    MAKECASE(OID_GEN_DIRECTED_FRAMES_XMIT)
    MAKECASE(OID_GEN_MULTICAST_BYTES_XMIT)
    MAKECASE(OID_GEN_MULTICAST_FRAMES_XMIT)
    MAKECASE(OID_GEN_BROADCAST_BYTES_XMIT)
    MAKECASE(OID_GEN_BROADCAST_FRAMES_XMIT)
    MAKECASE(OID_GEN_DIRECTED_BYTES_RCV)
    MAKECASE(OID_GEN_DIRECTED_FRAMES_RCV)
    MAKECASE(OID_GEN_MULTICAST_BYTES_RCV)
    MAKECASE(OID_GEN_MULTICAST_FRAMES_RCV)
    MAKECASE(OID_GEN_BROADCAST_BYTES_RCV)
    MAKECASE(OID_GEN_BROADCAST_FRAMES_RCV)
    MAKECASE(OID_GEN_RCV_CRC_ERROR)
    MAKECASE(OID_GEN_TRANSMIT_QUEUE_LENGTH)
    MAKECASE(OID_GEN_GET_TIME_CAPS)
    MAKECASE(OID_GEN_GET_NETCARD_TIME)
    MAKECASE(OID_GEN_NETCARD_LOAD)
    MAKECASE(OID_GEN_DEVICE_PROFILE)
    MAKECASE(OID_GEN_INIT_TIME_MS)
    MAKECASE(OID_GEN_RESET_COUNTS)
    MAKECASE(OID_GEN_MEDIA_SENSE_COUNTS)
    MAKECASE(OID_GEN_VLAN_ID)
    MAKECASE(OID_PNP_CAPABILITIES)
    MAKECASE(OID_PNP_SET_POWER)
    MAKECASE(OID_PNP_QUERY_POWER)
    MAKECASE(OID_PNP_ADD_WAKE_UP_PATTERN)
    MAKECASE(OID_PNP_REMOVE_WAKE_UP_PATTERN)
    MAKECASE(OID_PNP_ENABLE_WAKE_UP)
    MAKECASE(OID_802_3_PERMANENT_ADDRESS)
    MAKECASE(OID_802_3_CURRENT_ADDRESS)
    MAKECASE(OID_802_3_MULTICAST_LIST)
    MAKECASE(OID_802_3_MAXIMUM_LIST_SIZE)
    MAKECASE(OID_802_3_MAC_OPTIONS)
    MAKECASE(OID_802_3_RCV_ERROR_ALIGNMENT)
    MAKECASE(OID_802_3_XMIT_ONE_COLLISION)
    MAKECASE(OID_802_3_XMIT_MORE_COLLISIONS)
    MAKECASE(OID_802_3_XMIT_DEFERRED)
    MAKECASE(OID_802_3_XMIT_MAX_COLLISIONS)
    MAKECASE(OID_802_3_RCV_OVERRUN)
    MAKECASE(OID_802_3_XMIT_UNDERRUN)
    MAKECASE(OID_802_3_XMIT_HEARTBEAT_FAILURE)
    MAKECASE(OID_802_3_XMIT_TIMES_CRS_LOST)
    MAKECASE(OID_802_3_XMIT_LATE_COLLISIONS)
    MAKECASE(OID_GEN_MACHINE_NAME)
    MAKECASE(OID_TCP_TASK_OFFLOAD)
    MAKECASE(OID_GEN_STATISTICS)
    MAKECASE(OID_GEN_INTERRUPT_MODERATION)
#if PARANDIS_SUPPORT_RSS
    MAKECASE(OID_GEN_RECEIVE_SCALE_PARAMETERS)
    MAKECASE(OID_GEN_RECEIVE_HASH)
#endif
#if PARANDIS_SUPPORT_RSC
    MAKECASE(OID_TCP_RSC_STATISTICS)
#endif
    MAKECASE(OID_TCP_OFFLOAD_PARAMETERS)
    MAKECASE(OID_OFFLOAD_ENCAPSULATION)
    MAKECASE(OID_IP4_OFFLOAD_STATS)
    MAKECASE(OID_IP6_OFFLOAD_STATS)
#if NDIS_SUPPORT_NDIS61
MAKECASE(OID_TCP_TASK_IPSEC_OFFLOAD_V2_ADD_SA)
MAKECASE(OID_TCP_TASK_IPSEC_OFFLOAD_V2_DELETE_SA)
MAKECASE(OID_TCP_TASK_IPSEC_OFFLOAD_V2_UPDATE_SA)
#endif
#if NDIS_SUPPORT_NDIS620
    MAKECASE(OID_PM_CURRENT_CAPABILITIES)
    MAKECASE(OID_PM_HARDWARE_CAPABILITIES)
    MAKECASE(OID_PM_PARAMETERS)
    MAKECASE(OID_PM_ADD_WOL_PATTERN)
    MAKECASE(OID_PM_REMOVE_WOL_PATTERN)
    MAKECASE(OID_PM_WOL_PATTERN_LIST)
    MAKECASE(OID_PM_ADD_PROTOCOL_OFFLOAD)
    MAKECASE(OID_PM_GET_PROTOCOL_OFFLOAD)
    MAKECASE(OID_PM_REMOVE_PROTOCOL_OFFLOAD)
    MAKECASE(OID_PM_PROTOCOL_OFFLOAD_LIST)
#endif
    default:
        {
            static UCHAR buffer[9];
            UINT i;
            for (i = 0; i < 8; ++i)
            {
                UCHAR nibble = (UCHAR)((oid >> (28 - i * 4)) & 0xf);
                buffer[i] = hexdigit(nibble);
            }
            return (const char *)buffer;
        }
        break;
    }
}

/**********************************************************
Checker of valid size of provided wake-up patter
Return value: SUCCESS or kind of failure where the buffer is wrong
***********************************************************/
static NDIS_STATUS ValidateWakeupPattern(PNDIS_PM_PACKET_PATTERN p, PULONG pValidSize)
{
    NDIS_STATUS status = NDIS_STATUS_BUFFER_TOO_SHORT;

    if (*pValidSize < sizeof(*p))
    {
        *pValidSize = sizeof(*p);
    }
    else
    {
        ULONG ul = p->PatternOffset + p->PatternSize;
        if (*pValidSize >= ul) status = NDIS_STATUS_SUCCESS;
        *pValidSize = ul;
        DPrintf(2, ("[%s] pattern of %d at %d, mask %d (%s)\n",
            __FUNCTION__, p->PatternSize, p->PatternOffset, p->MaskSize,
            status == NDIS_STATUS_SUCCESS ? "OK" : "Fail"));
    }
    return status;
}


/**********************************************************
Common handler of wake-up pattern addition
***********************************************************/
NDIS_STATUS ParaNdis_OnAddWakeupPattern(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS status;
    PNDIS_PM_PACKET_PATTERN pPmPattern = (PNDIS_PM_PACKET_PATTERN) pOid->InformationBuffer;
    ULONG ulValidSize = pOid->InformationBufferLength;

    UNREFERENCED_PARAMETER(pContext);

    status = ValidateWakeupPattern(pPmPattern, &ulValidSize);
    if (status == NDIS_STATUS_SUCCESS)
    {
        *pOid->pBytesRead = ulValidSize;
    }
    else
    {
        *pOid->pBytesRead = 0;
        *pOid->pBytesNeeded = ulValidSize;
    }
    // TODO: Apply
    return status;
}

/**********************************************************
Common handler of wake-up pattern removal
***********************************************************/
NDIS_STATUS ParaNdis_OnRemoveWakeupPattern(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS status;
    PNDIS_PM_PACKET_PATTERN pPmPattern = (PNDIS_PM_PACKET_PATTERN) pOid->InformationBuffer;
    ULONG ulValidSize = pOid->InformationBufferLength;

    UNREFERENCED_PARAMETER(pContext);

    status = ValidateWakeupPattern(pPmPattern, &ulValidSize);
    if (status == NDIS_STATUS_SUCCESS)
    {
        *pOid->pBytesRead = ulValidSize;
    }
    else
    {
        *pOid->pBytesRead = 0;
        *pOid->pBytesNeeded = ulValidSize;
    }
    return status;
}

/**********************************************************
Common handler of wake-up enabling upon standby
***********************************************************/
NDIS_STATUS ParaNdis_OnEnableWakeup(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS status = ParaNdis_OidSetCopy(pOid, &pContext->ulEnableWakeup, sizeof(pContext->ulEnableWakeup));
    if (status == NDIS_STATUS_SUCCESS)
    {
        DPrintf(0, ("[%s] new value %lX\n", __FUNCTION__, pContext->ulEnableWakeup));
    }
    return status;
}

/**********************************************************
Dummy implementation
***********************************************************/
NDIS_STATUS ParaNdis_OnSetLookahead(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    return ParaNdis_OidSetCopy(pOid, &pContext->DummyLookAhead, sizeof(pContext->DummyLookAhead));
}

NDIS_STATUS ParaNdis_OnSetVlanId(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_STATUS status = NDIS_STATUS_NOT_SUPPORTED;
    if (IsVlanSupported(pContext))
    {
        status = ParaNdis_OidSetCopy(pOid, &pContext->VlanId, sizeof(pContext->VlanId));
        pContext->VlanId &= 0xfff;
        DPrintf(0, ("[%s] new value %d on MAC %X\n", __FUNCTION__, pContext->VlanId, pContext->CurrentMacAddress[5]));
        ParaNdis_DeviceFiltersUpdateVlanId(pContext);
    }
    return status;
}

/**********************************************************
Retrieves support rules for specific OID
***********************************************************/
void ParaNdis_GetOidSupportRules(NDIS_OID oid, tOidWhatToDo *pRule, const tOidWhatToDo *Table)
{
    static const tOidWhatToDo defaultRule = { 0, 0, 0, 0, 0, NULL, "Unknown OID" };
    UINT i;
    *pRule = defaultRule;
    pRule->oid = oid;

    for (i = 0; Table[i].oid != 0; ++i)
    {
        if (Table[i].oid == oid)
        {
            *pRule = Table[i];
            break;
        }
    }
    pRule->name = ParaNdis_OidName(oid);
}
