#pragma once

//#define ENABLE_HISTORY_LOG
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
