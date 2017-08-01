/*
 * Gathers memory statistics in kernel mode
 *
 * Copyright (c) 2016  Parallels IP Holdings GmbH
 *
 * Author(s):
 *   Alexey V. Kostyushko <aleksko@virtuozzo.com>
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

#include "precomp.h"

#ifndef USE_BALLOON_SERVICE

#include "ntddkex.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, StatInitializeWorkItem)
#endif

static __inline
UINT64
U32_2_S64(ULONG x)
{
    return ((UINT64)x);
}

static __inline
VOID
UpdateStat(PBALLOON_STAT stat, USHORT tag, UINT64 val)
{
    stat->tag = tag;
    stat->val = val;
}

/*
 * NB! Counters are supposed to be monotonically increasing.
 * In case of multiple calls with the same value
 * there will be no false overflow detection
 */
static __inline
UINT64
UpdateOverflowFreeCounter(PULARGE_INTEGER ofc, ULONG value)
{
    if (value < ofc->LowPart) {
        ofc->HighPart++;
    }
    ofc->LowPart = value;
    return ofc->QuadPart;
}

typedef enum _ULONG_COUNTER {
    _PageReadCount = 0,
    _DirtyPagesWriteCount,
    _MappedPagesWriteCount,
    _CopyOnWriteCount,
    _TransitionCount,
    _CacheTransitionCount,
    _DemandZeroCount,
    _LastCounter
} ULONG_COUNTER;

static ULARGE_INTEGER Counters[_LastCounter];

/*
 * 32-bit page counters overflows at 16Tb.
 * This applies to PageReadCount, DirtyPageWriteCount and PageFaultCount.
 * PageReadIoCount depends on workload and uses 64-256Kb in average, can be overflowed too.
 *
 * AvailablePages and NumberOfPhysicalPages are current values.
 * Thus Windows implementation limit is 16Tb, but actual limit is lower, see
 * https://msdn.microsoft.com/en-us/library/windows/desktop/aa366778(v=vs.85).aspx
 * (4Tb max for Windows 2012 Datacenter Edition)
 *
 * We'll hardcoded PAGE_SHIFT to shl, instead of multiplication with basicInfo.PageSize.
 */
static BOOLEAN bBasicInfoWarning = FALSE;
static BOOLEAN bPerfInfoWarning = FALSE;
NTSTATUS GatherKernelStats(BALLOON_STAT stats[VIRTIO_BALLOON_S_NR])
{
    SYSTEM_BASIC_INFORMATION basicInfo;
    SYSTEM_PERFORMANCE_INFORMATION perfInfo;
    ULONG outLen = 0;
    NTSTATUS ntStatus;
    ULONG idx = 0;
    UINT64 SoftFaults;

    RtlZeroMemory(&basicInfo,sizeof(basicInfo));
    RtlZeroMemory(&perfInfo,sizeof(perfInfo));

    ntStatus = ZwQuerySystemInformation(SystemBasicInformation, &basicInfo, sizeof(basicInfo), &outLen);
    if(!NT_SUCCESS(ntStatus))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "GatherKernelStats (SystemBasicInformation) failed 0x%08x (outLen=0x%x)\n", ntStatus, outLen);
        return ntStatus;
    }

    if ((!bBasicInfoWarning)&&(outLen != sizeof(basicInfo))) {
        bBasicInfoWarning = TRUE;
        TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
            "GatherKernelStats (SystemBasicInformation) expected outLen=0x%08x returned with 0x%0x",
            sizeof(basicInfo), outLen);
    }

    ntStatus = ZwQuerySystemInformation(SystemPerformanceInformation, &perfInfo, sizeof(perfInfo), &outLen);
    if(!NT_SUCCESS(ntStatus))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "GatherKernelStats (SystemPerformanceInformation) failed 0x%08x (outLen=0x%x)\n", ntStatus, outLen);
        return ntStatus;
    }

    if ((!bPerfInfoWarning)&&(outLen != sizeof(perfInfo))) {
        bPerfInfoWarning = TRUE;
        TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
            "GatherKernelStats (SystemPerformanceInformation) expected outLen=0x%08x returned with 0x%0x",
            sizeof(perfInfo), outLen);
    }

    #define UpdateNoOverflow(x) UpdateOverflowFreeCounter(&Counters[_##x],perfInfo.##x)
    UpdateStat(&stats[idx++], VIRTIO_BALLOON_S_SWAP_IN,  UpdateNoOverflow(PageReadCount) << PAGE_SHIFT);
    UpdateStat(&stats[idx++], VIRTIO_BALLOON_S_SWAP_OUT,
        (UpdateNoOverflow(DirtyPagesWriteCount) + UpdateNoOverflow(MappedPagesWriteCount)) << PAGE_SHIFT);
    SoftFaults = UpdateNoOverflow(CopyOnWriteCount) + UpdateNoOverflow(TransitionCount) +
                 UpdateNoOverflow(CacheTransitionCount) + UpdateNoOverflow(DemandZeroCount);
    UpdateStat(&stats[idx++], VIRTIO_BALLOON_S_MAJFLT,   UpdateNoOverflow(PageReadCount));
    UpdateStat(&stats[idx++], VIRTIO_BALLOON_S_MINFLT,   SoftFaults);
    UpdateStat(&stats[idx++], VIRTIO_BALLOON_S_MEMFREE,  U32_2_S64(perfInfo.AvailablePages) << PAGE_SHIFT);
    UpdateStat(&stats[idx++], VIRTIO_BALLOON_S_MEMTOT,   U32_2_S64(basicInfo.NumberOfPhysicalPages) << PAGE_SHIFT);
    #undef UpdateNoOverflow

    return ntStatus;
}

NTSTATUS StatInitializeWorkItem(
    IN WDFDEVICE  Device
    )
{
    WDF_OBJECT_ATTRIBUTES   attributes;
    WDF_WORKITEM_CONFIG     workitemConfig;
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    RtlZeroMemory(Counters, sizeof(Counters));

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    WDF_WORKITEM_CONFIG_INIT(&workitemConfig, StatWorkItemWorker);
    return WdfWorkItemCreate(&workitemConfig, &attributes, &devCtx->StatWorkItem);
}

/*
 * Still use devCtx->MemStats cause it points to non-paged pool,
 * for virtio/host that access stats via physical memory.
 */
VOID
StatWorkItemWorker(
    IN WDFWORKITEM  WorkItem
    )
{
    WDFDEVICE       Device = WdfWorkItemGetParentObject(WorkItem);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
    NTSTATUS        status = STATUS_SUCCESS;

    do
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS,
            "StatWorkItemWorker Called! \n");
        status = GatherKernelStats(devCtx->MemStats);
        if (NT_SUCCESS(status))
        {
#if 0
            size_t i;
            for (i = 0; i < VIRTIO_BALLOON_S_NR; ++i)
            {
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS,
                    "st=%x tag = %d, value = %08I64X \n\n", status,
                    devCtx->MemStats[i].tag, devCtx->MemStats[i].val);
            }
#endif
        } else {
            RtlFillMemory (devCtx->MemStats, sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR, -1);
        }
        BalloonMemStats(Device);
    } while(InterlockedDecrement(&devCtx->WorkCount));
    return;
}

#endif // !USE_BALLOON_SERVICE
