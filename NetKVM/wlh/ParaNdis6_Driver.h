#pragma once
#include "ndis56common.h"

VOID ParaNdis6_SendNBLInternal(NDIS_HANDLE miniportAdapterContext, PNET_BUFFER_LIST pNBL,
    NDIS_PORT_NUMBER portNumber, ULONG flags);