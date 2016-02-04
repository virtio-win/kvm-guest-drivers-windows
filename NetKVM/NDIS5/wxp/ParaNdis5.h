/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: ParaNdis5.h
 *
 * This file contains NDIS5.X specific procedure definitions in NDIS driver.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef _PARA_NDIS5_H
#define _PARA_NDIS5_H

#include "ndis56common.h"


NDIS_STATUS ParaNdis5_SetOID(IN NDIS_HANDLE MiniportAdapterContext,
                                            IN NDIS_OID Oid,
                                            IN PVOID InformationBuffer,
                                            IN ULONG InformationBufferLength,
                                            OUT PULONG BytesRead,
                                            OUT PULONG BytesNeeded);

NDIS_STATUS ParaNdis5_QueryOID(IN NDIS_HANDLE  MiniportAdapterContext,
                                              IN NDIS_OID     Oid,
                                              IN PVOID        InformationBuffer,
                                              IN ULONG        InformationBufferLength,
                                              OUT PULONG      BytesWritten,
                                              OUT PULONG      BytesNeeded);


VOID ParaNdis5_SendPackets(IN NDIS_HANDLE MiniportAdapterContext,
                               IN PPNDIS_PACKET PacketArray,
                               IN UINT NumberOfPackets);


VOID ParaNdis5_ReturnPacket(IN NDIS_HANDLE  MiniportAdapterContext,IN PNDIS_PACKET Packet);

VOID ParaNdis5_IndicateConnect(PARANDIS_ADAPTER *pContext, BOOLEAN bConnected);


#ifdef NDIS51_MINIPORT
//NDIS 5.1 related functions
VOID ParaNdis5_CancelSendPackets(IN NDIS_HANDLE MiniportAdapterContext,IN PVOID CancelId);
#endif /* NDIS51_MINIPORT */

NDIS_STATUS ParaNdis5_StopSend(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN bStop,
    ONPAUSECOMPLETEPROC Callback);
NDIS_STATUS ParaNdis5_StopReceive(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN bStop,
    ONPAUSECOMPLETEPROC Callback
    );
VOID ParaNdis5_HandleDPC(
    IN NDIS_HANDLE MiniportAdapterContext);

typedef struct _tagPowerWorkItem
{
    NDIS_WORK_ITEM              wi;
    PPARANDIS_ADAPTER           pContext;
    NDIS_DEVICE_POWER_STATE     state;
}tPowerWorkItem;

typedef struct _tagGeneralWorkItem
{
    NDIS_WORK_ITEM              wi;
    PPARANDIS_ADAPTER           pContext;
}tGeneralWorkItem;

#endif    // _PARA_NDIS5_H
