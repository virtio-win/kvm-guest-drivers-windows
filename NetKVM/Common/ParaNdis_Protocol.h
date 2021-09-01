#pragma once

#include "ndis56common.h"

NDIS_STATUS ParaNdis_ProtocolInitialize(NDIS_HANDLE DriverHandle);
void ParaNdis_ProtocolRegisterAdapter(PARANDIS_ADAPTER *pContext);
void ParaNdis_ProtocolUnregisterAdapter(PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)(ULONG_PTR)1, bool UnregisterOnLast = true);
bool ParaNdis_ProtocolSend(PARANDIS_ADAPTER *pContext, PNET_BUFFER_LIST pNBL);
void ParaNdis_ProtocolReturnNbls(PARANDIS_ADAPTER *pContext, PNET_BUFFER_LIST pNBL, ULONG numNBLs, ULONG flags);
void ParaNdis_ProtocolActive();
