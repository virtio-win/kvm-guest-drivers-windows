#pragma once

//#define ENABLE_HISTORY_LOG
//#define KEEP_PENDING_NBL

#if !defined(KEEP_PENDING_NBL)

void FORCEINLINE ParaNdis_DebugNBLIn(PNET_BUFFER_LIST nbl, ULONG& index)
{
    UNREFERENCED_PARAMETER(nbl);
    UNREFERENCED_PARAMETER(index);
}

void FORCEINLINE ParaNdis_DebugNBLOut(ULONG index, PNET_BUFFER_LIST nbl)
{
    UNREFERENCED_PARAMETER(index);
    UNREFERENCED_PARAMETER(nbl);
}

#else

void ParaNdis_DebugNBLIn(PNET_BUFFER_LIST nbl, ULONG& index);
void ParaNdis_DebugNBLOut(ULONG index, PNET_BUFFER_LIST nbl);

#endif

#if !defined(ENABLE_HISTORY_LOG)

void FORCEINLINE ParaNdis_DebugHistory(
    PARANDIS_ADAPTER *pContext,
    eHistoryLogOperation op,
    PVOID pParam1,
    ULONG lParam2,
    ULONG lParam3,
    ULONG lParam4)
{
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(op);
    UNREFERENCED_PARAMETER(pParam1);
    UNREFERENCED_PARAMETER(lParam2);
    UNREFERENCED_PARAMETER(lParam3);
    UNREFERENCED_PARAMETER(lParam4);
}

#else

void ParaNdis_DebugHistory(
    PARANDIS_ADAPTER *pContext,
    eHistoryLogOperation op,
    PVOID pParam1,
    ULONG lParam2,
    ULONG lParam3,
    ULONG lParam4);

#endif
