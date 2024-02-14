/*
 * This file contains debug support procedures, common for NDIS5 and NDIS6
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
#include "ndis56common.h"
#include "ParaNdis_DebugHistory.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_Debug.tmh"
#endif


extern "C"
{
#include "stdarg.h"
#include "ntstrsafe.h"
}

int virtioDebugLevel = 0;
int bDebugPrint = 1;

/* Crash callback support makes SDV to skip all the tests
 * of NDIS entry points and report them as 'Not applicable'
 * Leave it enabled for 8.1 and private builds without WPP
 */
//#define ENABLE_CRASH_CALLBACK       1
#if !NDIS_SUPPORT_NDIS680 || !defined(NETKVM_WPP_ENABLED)
#define ENABLE_CRASH_CALLBACK       1
#endif

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

#ifndef NETKVM_WPP_ENABLED

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
    va_end(list);
}

#else

static void NetKVMDebugPrint(const char *fmt, ...)
{
    va_list list;
    va_start(list, fmt);
    char buf[256];
    buf[0] = 0;
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, list);
    TraceNoPrefix(0, "%s", buf);
}

#endif

tDebugPrintFunc VirtioDebugPrintProc = NetKVMDebugPrint;

void ParaNdis_DebugInitialize()
{
    NDIS_STRING usRegister, usDeregister, usPrint;
    PVOID pr, pd;
    BOOLEAN res = false;

    NdisAllocateSpinLock(&CrashLock);
    KeInitializeCallbackRecord(&CallbackRecord);
    ParaNdis_PrepareBugCheckData();
    NdisInitUnicodeString(&usPrint, L"vDbgPrintEx");
    NdisInitUnicodeString(&usRegister, L"KeRegisterBugCheckReasonCallback");
    NdisInitUnicodeString(&usDeregister, L"KeDeregisterBugCheckReasonCallback");
#ifndef NETKVM_WPP_ENABLED
    pd = MmGetSystemRoutineAddress(&usPrint);
    if (pd) PrintProcedure = (vDbgPrintExType)pd;
#endif
    pr = MmGetSystemRoutineAddress(&usRegister);
    pd = MmGetSystemRoutineAddress(&usDeregister);
    if (pr && pd)
    {
        BugCheckRegisterCallback = (KeRegisterBugCheckReasonCallbackType)pr;
        BugCheckDeregisterCallback = (KeDeregisterBugCheckReasonCallbackType)pd;
    }
#if ENABLE_CRASH_CALLBACK
    res = BugCheckRegisterCallback(&CallbackRecord, ParaNdis_OnBugCheck, KbCallbackSecondaryDumpData, (const PUCHAR)"NetKvm");
#endif
    DPrintf(0, "[%s] Crash callback %sregistered\n", __FUNCTION__, res ? "" : "NOT ");
}

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

#if defined(KEEP_PENDING_NBL)
#define MAX_KEEP_NBLS   1024
#else
#define MAX_KEEP_NBLS   1
#endif

typedef struct _tagBugCheckStaticData
{
    tBugCheckStaticDataHeader Header;
    tBugCheckPerNicDataContent PerNicData[MAX_CONTEXTS];
    tBugCheckStaticDataContent Data;
    tBugCheckHistoryDataEntry  History[MAX_HISTORY];

    RTL_BITMAP          PendingNblsBitmap;
    ULONG               PendingNblsBitmapBuffer[MAX_KEEP_NBLS/32 + !!(MAX_KEEP_NBLS%32)];
    tPendingNBlEntry    PendingNbls[MAX_KEEP_NBLS];
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
    BugCheckData.StaticData.Data.StaticDataV0.HistoryDataVersion = PARANDIS_DEBUG_HISTORY_DATA_VERSION;
    BugCheckData.StaticData.Data.StaticDataV0.SizeOfHistory = MAX_HISTORY;
    BugCheckData.StaticData.Data.StaticDataV0.SizeOfHistoryEntry = sizeof(tBugCheckHistoryDataEntry);
    BugCheckData.StaticData.Data.StaticDataV0.HistoryData = (UINT_PTR)(PVOID)BugCheckData.StaticData.History;
    BugCheckData.StaticData.Data.PendingNblEntryVersion = PARANDIS_DEBUG_PENDING_NBL_ENTRY_VERSION;
    BugCheckData.StaticData.Data.PendingNblData = (UINT_PTR)(PVOID)BugCheckData.StaticData.PendingNbls;
    BugCheckData.StaticData.Data.MaxPendingNbl = MAX_KEEP_NBLS;
    BugCheckData.Location.Address = (UINT64)&BugCheckData;
    BugCheckData.Location.Size = sizeof(BugCheckData);
    RtlInitializeBitMap(&BugCheckData.StaticData.PendingNblsBitmap, BugCheckData.StaticData.PendingNblsBitmapBuffer, MAX_KEEP_NBLS);
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

#if ENABLE_CRASH_CALLBACK
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

        pSave->LastInterruptTimeStamp.QuadPart = PARANDIS_GET_LAST_INTERRUPT_TIMESTAMP(p);
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
            DPrintf(0, "[%s] system buffer of %d, saving data for %d NIC\n", __FUNCTION__,pDump->InBufferLength, nSaved);
            DPrintf(0, "[%s] using %s buffer\n", __FUNCTION__, bNative ? "native" : "own");
        }
        else if (pDump->OutBuffer == pDump->InBuffer)
        {
            RtlCopyMemory(&pDump->Guid, &ParaNdis_CrashGuid, sizeof(pDump->Guid));
            RtlCopyMemory(pDump->InBuffer, &BugCheckData.Location, dumpSize);
            pDump->OutBufferLength = dumpSize;
            DPrintf(0, "[%s] written %d to 0x%llx\n", __FUNCTION__, (ULONG)BugCheckData.Location.Size, (UINT_PTR)BugCheckData.Location.Address );
            DPrintf(0, "[%s] dump data (%d) at %p\n", __FUNCTION__, pDump->OutBufferLength, pDump->OutBuffer);
        }
    }
}
#endif

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

#if defined(KEEP_PENDING_NBL)

void ParaNdis_DebugNBLIn(PNET_BUFFER_LIST nbl, ULONG& index)
{
    NdisAcquireSpinLock(&CrashLock);
    index = RtlFindClearBitsAndSet(&BugCheckData.StaticData.PendingNblsBitmap, 1, 0);
    NdisReleaseSpinLock(&CrashLock);
    if (index < MAX_KEEP_NBLS)
    {
        BugCheckData.StaticData.PendingNbls[index].NBL = (ULONG_PTR)(PVOID)nbl;
        NdisGetCurrentSystemTime(&BugCheckData.StaticData.PendingNbls[index].TimeStamp);
    }
    else
    {
        // if no free bit in bitmap, ULONG(-1) returned
        BugCheckData.StaticData.Data.fNBLOverflow = 1;
    }
}

void ParaNdis_DebugNBLOut(ULONG index, PNET_BUFFER_LIST nbl)
{
    if (index >= MAX_KEEP_NBLS)
        return;

    NdisAcquireSpinLock(&CrashLock);
    if (!RtlCheckBit(&BugCheckData.StaticData.PendingNblsBitmap, index))
    {
        // simple double free
        RtlAssert(__FUNCTION__, __FILE__, __LINE__, NULL);
    }
    else if (BugCheckData.StaticData.PendingNbls[index].NBL != (ULONG_PTR)(PVOID)nbl)
    {
        // complicated double free
        RtlAssert(__FUNCTION__, __FILE__, __LINE__, NULL);
    }
    else
    {
        RtlClearBit(&BugCheckData.StaticData.PendingNblsBitmap, index);
        BugCheckData.StaticData.PendingNbls[index].NBL = 0;
    }
    NdisReleaseSpinLock(&CrashLock);
}
#endif
