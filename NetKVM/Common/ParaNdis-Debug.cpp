/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: ParaNdis6-Debug.c
 *
 * This file contains debug support procedures, common for NDIS5 and NDIS6
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "ndis56common.h"

extern "C"
{
#include "stdarg.h"
#include "ntstrsafe.h"
}

int virtioDebugLevel = 0;
int bDebugPrint = 1;

static NDIS_SPIN_LOCK CrashLock;

static KBUGCHECK_REASON_CALLBACK_ROUTINE ParaNdis_OnBugCheck;
static VOID ParaNdis_PrepareBugCheckData();

typedef BOOLEAN (*KeRegisterBugCheckReasonCallbackType) (
    __out PKBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord,
    __in PKBUGCHECK_REASON_CALLBACK_ROUTINE CallbackRoutine,
    __in KBUGCHECK_CALLBACK_REASON Reason,
    __in PUCHAR Component
    );

typedef BOOLEAN (*KeDeregisterBugCheckReasonCallbackType) (
    __inout PKBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord
    );

typedef ULONG (*vDbgPrintExType)(
    __in ULONG ComponentId,
    __in ULONG Level,
    __in PCCH Format,
    __in va_list arglist
    );

static ULONG DummyPrintProcedure(
    __in ULONG ComponentId,
    __in ULONG Level,
    __in PCCH Format,
    __in va_list arglist
    )
{
    UNREFERENCED_PARAMETER(ComponentId);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(Format);
    UNREFERENCED_PARAMETER(arglist);

    return 0;
}
static BOOLEAN KeRegisterBugCheckReasonCallbackDummyProc(
    __out PKBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord,
    __in PKBUGCHECK_REASON_CALLBACK_ROUTINE CallbackRoutine,
    __in KBUGCHECK_CALLBACK_REASON Reason,
    __in PUCHAR Component
    )
{
    UNREFERENCED_PARAMETER(CallbackRoutine);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Component);

    CallbackRecord->State = 0;
    return FALSE;
}

BOOLEAN KeDeregisterBugCheckReasonCallbackDummyProc(
    __inout PKBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord
    )
{
    UNREFERENCED_PARAMETER(CallbackRecord);
    return FALSE;
}

static vDbgPrintExType PrintProcedure = DummyPrintProcedure;
static KeRegisterBugCheckReasonCallbackType BugCheckRegisterCallback = KeRegisterBugCheckReasonCallbackDummyProc;
static KeDeregisterBugCheckReasonCallbackType BugCheckDeregisterCallback = KeDeregisterBugCheckReasonCallbackDummyProc;
KBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord;

static void NetKVMDebugPrint(const char *fmt, ...)
{
    va_list list;
    va_start(list, fmt);
    PrintProcedure(DPFLTR_DEFAULT_ID, 9 | DPFLTR_MASK, fmt, list);
#if defined(VIRTIO_DBG_USE_IOPORT)
    {
        NTSTATUS status;
        // use this way of output only for DISPATCH_LEVEL,
        // higher requires more protection
        if (KeGetCurrentIrql() <= DISPATCH_LEVEL)
        {
            char buf[256];
            size_t len, i;
            buf[0] = 0;
            status = RtlStringCbVPrintfA(buf, sizeof(buf), fmt, list);
            if (status == STATUS_SUCCESS) len = strlen(buf);
            else if (status == STATUS_BUFFER_OVERFLOW) len = sizeof(buf);
            else { memcpy(buf, "Can't print", 11); len = 11; }
            NdisAcquireSpinLock(&CrashLock);
            for (i = 0; i < len; ++i)
            {
                NdisRawWritePortUchar(VIRTIO_DBG_USE_IOPORT, buf[i]);
            }
            NdisRawWritePortUchar(VIRTIO_DBG_USE_IOPORT, '\n');
            NdisReleaseSpinLock(&CrashLock);
        }
    }
#endif
}

tDebugPrintFunc VirtioDebugPrintProc = NetKVMDebugPrint;

#pragma warning (push)
#pragma warning (disable:4055)
void ParaNdis_DebugInitialize()
{
    NDIS_STRING usRegister, usDeregister, usPrint;
    PVOID pr, pd;
    BOOLEAN res;

    NdisAllocateSpinLock(&CrashLock);
    KeInitializeCallbackRecord(&CallbackRecord);
    ParaNdis_PrepareBugCheckData();
    NdisInitUnicodeString(&usPrint, L"vDbgPrintEx");
    NdisInitUnicodeString(&usRegister, L"KeRegisterBugCheckReasonCallback");
    NdisInitUnicodeString(&usDeregister, L"KeDeregisterBugCheckReasonCallback");
    pd = MmGetSystemRoutineAddress(&usPrint);
    if (pd) PrintProcedure = (vDbgPrintExType)pd;
    pr = MmGetSystemRoutineAddress(&usRegister);
    pd = MmGetSystemRoutineAddress(&usDeregister);
    if (pr && pd)
    {
        BugCheckRegisterCallback = (KeRegisterBugCheckReasonCallbackType)pr;
        BugCheckDeregisterCallback = (KeDeregisterBugCheckReasonCallbackType)pd;
    }
    res = BugCheckRegisterCallback(&CallbackRecord, ParaNdis_OnBugCheck, KbCallbackSecondaryDumpData, (const PUCHAR)"NetKvm");
    DPrintf(0, ("[%s] Crash callback %sregistered\n", __FUNCTION__, res ? "" : "NOT "));
}
#pragma warning (pop)

void ParaNdis_DebugCleanup(PDRIVER_OBJECT  pDriverObject)
{
    UNREFERENCED_PARAMETER(pDriverObject);

    BugCheckDeregisterCallback(&CallbackRecord);
}


#define MAX_CONTEXTS    4
#if defined(ENABLE_HISTORY_LOG)
#define MAX_HISTORY     0x40000
#else
#define MAX_HISTORY     2
#endif
typedef struct _tagBugCheckStaticData
{
    tBugCheckStaticDataHeader Header;
    tBugCheckPerNicDataContent PerNicData[MAX_CONTEXTS];
    tBugCheckStaticDataContent Data;
    tBugCheckHistoryDataEntry  History[MAX_HISTORY];
}tBugCheckStaticData;


typedef struct _tagBugCheckData
{
    tBugCheckStaticData     StaticData;
    tBugCheckDataLocation   Location;
}tBugCheckData;

static tBugCheckData BugCheckData;
static BOOLEAN bNative = TRUE;

VOID ParaNdis_PrepareBugCheckData()
{
    BugCheckData.StaticData.Header.StaticDataVersion = PARANDIS_DEBUG_STATIC_DATA_VERSION;
    BugCheckData.StaticData.Header.PerNicDataVersion = PARANDIS_DEBUG_PER_NIC_DATA_VERSION;
    BugCheckData.StaticData.Header.ulMaxContexts = MAX_CONTEXTS;
    BugCheckData.StaticData.Header.SizeOfPointer = sizeof(PVOID);
    BugCheckData.StaticData.Header.PerNicData = (UINT_PTR)(PVOID)BugCheckData.StaticData.PerNicData;
    BugCheckData.StaticData.Header.DataArea = (UINT64)&BugCheckData.StaticData.Data;
    BugCheckData.StaticData.Header.DataAreaSize = sizeof(BugCheckData.StaticData.Data);
    BugCheckData.StaticData.Data.HistoryDataVersion = PARANDIS_DEBUG_HISTORY_DATA_VERSION;
    BugCheckData.StaticData.Data.SizeOfHistory = MAX_HISTORY;
    BugCheckData.StaticData.Data.SizeOfHistoryEntry = sizeof(tBugCheckHistoryDataEntry);
    BugCheckData.StaticData.Data.HistoryData = (UINT_PTR)(PVOID)BugCheckData.StaticData.History;
    BugCheckData.Location.Address = (UINT64)&BugCheckData;
    BugCheckData.Location.Size = sizeof(BugCheckData);
}

void ParaNdis_DebugRegisterMiniport(PARANDIS_ADAPTER *pContext, BOOLEAN bRegister)
{
    UINT i;
    NdisAcquireSpinLock(&CrashLock);
    for (i = 0; i < MAX_CONTEXTS; ++i)
    {
        UINT64 val1 = bRegister ? 0 : (UINT_PTR)pContext;
        UINT64 val2 = bRegister ? (UINT_PTR)pContext : 0;
        if (BugCheckData.StaticData.PerNicData[i].Context != val1) continue;
        BugCheckData.StaticData.PerNicData[i].Context = val2;
        break;
    }
    NdisReleaseSpinLock(&CrashLock);
}

static UINT FillDataOnBugCheck()
{
    UINT i, n = 0;
    NdisGetCurrentSystemTime(&BugCheckData.StaticData.Header.qCrashTime);
    for (i = 0; i < MAX_CONTEXTS; ++i)
    {
        tBugCheckPerNicDataContent *pSave = &BugCheckData.StaticData.PerNicData[i];
        PARANDIS_ADAPTER *p = (PARANDIS_ADAPTER *)(UINT_PTR)pSave->Context;
        if (!p) continue;

        pSave->nofReadyTxBuffers = 0;
        for (UINT j = 0; j < p->nPathBundles; j++)
        {
            pSave->nofReadyTxBuffers += p->pPathBundles[j].txPath.GetFreeHWBuffers();
        }

        pSave->LastInterruptTimeStamp.QuadPart = PARADNIS_GET_LAST_INTERRUPT_TIMESTAMP(p);
        pSave->LastTxCompletionTimeStamp = p->LastTxCompletionTimeStamp;
        ParaNdis_CallOnBugCheck(p);
        ++n;
    }
    return n;
}

VOID ParaNdis_OnBugCheck(
    IN KBUGCHECK_CALLBACK_REASON Reason,
    IN PKBUGCHECK_REASON_CALLBACK_RECORD Record,
    IN OUT PVOID ReasonSpecificData,
    IN ULONG ReasonSpecificDataLength
    )
{
    KBUGCHECK_SECONDARY_DUMP_DATA *pDump = (KBUGCHECK_SECONDARY_DUMP_DATA *)ReasonSpecificData;

    UNREFERENCED_PARAMETER(Record);

    if (KbCallbackSecondaryDumpData == Reason && ReasonSpecificDataLength >= sizeof(*pDump))
    {
        ULONG dumpSize = sizeof(BugCheckData.Location);
        if (!pDump->OutBuffer)
        {
            UINT nSaved;
            nSaved = FillDataOnBugCheck();
            if (pDump->InBufferLength >= dumpSize)
            {
                pDump->OutBuffer = pDump->InBuffer;
                pDump->OutBufferLength = dumpSize;
            }
            else
            {
                pDump->OutBuffer = &BugCheckData.Location;
                pDump->OutBufferLength = dumpSize;
                bNative = FALSE;
            }
            DPrintf(0, ("[%s] system buffer of %d, saving data for %d NIC\n", __FUNCTION__,pDump->InBufferLength, nSaved));
            DPrintf(0, ("[%s] using %s buffer\n", __FUNCTION__, bNative ? "native" : "own"));
        }
        else if (pDump->OutBuffer == pDump->InBuffer)
        {
            RtlCopyMemory(&pDump->Guid, &ParaNdis_CrashGuid, sizeof(pDump->Guid));
            RtlCopyMemory(pDump->InBuffer, &BugCheckData.Location, dumpSize);
            pDump->OutBufferLength = dumpSize;
            DPrintf(0, ("[%s] written %d to %p\n", __FUNCTION__, (ULONG)BugCheckData.Location.Size, (UINT_PTR)BugCheckData.Location.Address ));
            DPrintf(0, ("[%s] dump data (%d) at %p\n", __FUNCTION__, pDump->OutBufferLength, pDump->OutBuffer));
        }
    }
}

#if defined(ENABLE_HISTORY_LOG)
void ParaNdis_DebugHistory(
    PARANDIS_ADAPTER *pContext,
    eHistoryLogOperation op,
    PVOID pParam1,
    ULONG lParam2,
    ULONG lParam3,
    ULONG lParam4)
{
    tBugCheckHistoryDataEntry *phe;
    ULONG index = InterlockedIncrement(&BugCheckData.StaticData.Data.CurrentHistoryIndex);
    index = (index - 1) % MAX_HISTORY;
    phe = &BugCheckData.StaticData.History[index];
    phe->Context = (UINT_PTR)pContext;
    phe->operation = op;
    phe->pParam1 = (UINT_PTR)pParam1;
    phe->lParam2 = lParam2;
    phe->lParam3 = lParam3;
    phe->lParam4 = lParam4;
#if (PARANDIS_DEBUG_HISTORY_DATA_VERSION == 1)
    phe->uIRQL = KeGetCurrentIrql();
    phe->uProcessor = KeGetCurrentProcessorNumber();
#endif
    NdisGetCurrentSystemTime(&phe->TimeStamp);
}

#endif
