/*
 * This file contains definitions of NDIS6 OID-related procedures.
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
#ifndef PARA_NDIS6_H
#define PARA_NDIS6_H

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
