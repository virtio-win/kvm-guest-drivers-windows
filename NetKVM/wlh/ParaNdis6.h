/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: ParaNdis.h
 *
 * This file contains definitions of NDIS6 OID-related procedures.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef PARA_NDIS6_H
#define PARA_NDIS6_H

#pragma warning (disable: 4201 4214 4115 4127) // disable annoying warnings in NDIS

#include "ndis56common.h"

/* fills supported OID and statistics information */
VOID ParaNdis6_GetSupportedOid(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES *pGenAttributes);
/* returns supported statistics for statistics structure */
ULONG ParaNdis6_GetSupportedStatisticsFlags();

NDIS_STATUS ParaNdis6_GetRegistrationOffloadInfo(
        PARANDIS_ADAPTER *pContext,
        NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES *pAttributes);

void ParaNdis6_ApplyOffloadPersistentConfiguration(PARANDIS_ADAPTER *pContext);

MINIPORT_OID_REQUEST ParaNdis6_OidRequest;
NDIS_STATUS ParaNdis6_OidRequest(
    NDIS_HANDLE miniportAdapterContext,
    PNDIS_OID_REQUEST  pNdisRequest);

MINIPORT_CANCEL_SEND ParaNdis6_CancelSendNetBufferLists;
VOID ParaNdis6_CancelSendNetBufferLists(
    NDIS_HANDLE  miniportAdapterContext,
    PVOID pCancelId);

MINIPORT_RETURN_NET_BUFFER_LISTS ParaNdis6_ReturnNetBufferLists;
VOID ParaNdis6_ReturnNetBufferLists(
    NDIS_HANDLE miniportAdapterContext,
    PNET_BUFFER_LIST pNBL, ULONG returnFlags);


NDIS_STATUS ParaNdis6_SendPauseRestart(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN bPause,
    ONPAUSECOMPLETEPROC Callback
    );

NDIS_STATUS ParaNdis6_ReceivePauseRestart(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN bPause,
    ONPAUSECOMPLETEPROC Callback
    );

/* returns number of buffers that have been sent */
UINT ParaNdis6_CopyDataFromSingleNBL(PARANDIS_ADAPTER *pContext, PNET_BUFFER_LIST pNBL);

MINIPORT_CANCEL_OID_REQUEST ParaNdis6_OidCancelRequest;
VOID ParaNdis6_OidCancelRequest(
        NDIS_HANDLE hMiniportAdapterContext,
        PVOID pRequestId);

typedef struct _tagPowerWorkItem
{
    NDIS_HANDLE                 WorkItem;
    PPARANDIS_ADAPTER           pContext;
    NDIS_DEVICE_POWER_STATE     state;
    PNDIS_OID_REQUEST           request;
}tPowerWorkItem;

typedef struct _tagGeneralWorkItem
{
    NDIS_HANDLE                 WorkItem;
    PPARANDIS_ADAPTER           pContext;
}tGeneralWorkItem;


#if NDIS_SUPPORT_NDIS620
void ParaNdis6_Fill620PowerCapabilities(PNDIS_PM_CAPABILITIES pPower620Caps);
#endif

#endif
