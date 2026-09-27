/*
 * This file implements the free page reporting feature of the
 * virtio balloon device (VIRTIO_BALLOON_F_PAGE_REPORTING).
 *
 * Copyright (c) 2009-2017  Red Hat, Inc.
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

/*
 * Design overview
 * ===============
 *
 * Windows does not expose any driver-visible free page list - free pages
 * live on OS-internal lists - so free page reporting is implemented by
 * allocating pages out with MmAllocatePagesForMdlEx and reporting those
 * pages to the device through the reporting virtqueue. Unlike the inflate
 * and deflate queues, the scatter-gather list submitted to the reporting
 * queue describes the reported pages themselves (virtio 1.x, section
 * 5.5.6.7).
 *
 * Report-then-release does not work on Windows: handing the pages back to
 * the OS re-touches them (the memory manager dirties pages when they are
 * freed), so the host-side reclaim effect collapses immediately. The
 * driver therefore uses report-then-hold: reported pages stay allocated
 * inside the driver, where the guest cannot touch them, and are only
 * handed back to the guest when the guest needs the memory. Effectively
 * this is an adaptive balloon driven by the free page reporting queue.
 *
 * Only complete 2MB blocks are reported, in both size and boundary,
 * matching the granularity the host can reclaim (transparent huge pages)
 * and the default reporting granularity of the Linux implementation
 * (pageblock_order). The alignment comes for free from the memory manager:
 * MmAllocatePagesForMdlEx called with MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS
 * and SkipBytes = 2MB returns chunks that are each guaranteed to be
 * exactly 2MB long and aligned on a 2MB boundary, preferably taken from
 * the system's large page cache.
 *
 * The Linux implementation is notified by the page allocator whenever a
 * large page is freed and defers the actual reporting pass by
 * page_reporting_delay_ms. Windows offers no equivalent hook - the
 * memory manager's notification events only signal a threshold crossing,
 * not every page freed - so the reporting is driven by two threads
 * (see Device.c):
 *
 *   - The command thread (BalloonRoutine) makes all release decisions:
 *     LowMemoryCondition triggers the emergency path (all held pages
 *     are handed back at once and the hold cooldown starts), and while
 *     pages are held it re-evaluates the watermarks every
 *     REPORTING_COMMIT_POLL_MS (Windows exposes no event for a low
 *     commit limit). It never runs hold work itself, which keeps the
 *     host command latency independent of the reporting load.
 *   - The reporting thread (BalloonReportRoutine) runs one hold cycle
 *     per reporting interval at low priority (any guest thread can
 *     preempt a cycle), the counterpart of how page_reporting_delay_ms
 *     paces a reporting pass on Linux. Each cycle holds at most
 *     REPORTING_BATCHES_PER_CYCLE batches and stops early at the
 *     watermarks, checked before every batch.
 *
 * The watermark defaults to max(RAM/8, 256MB) and can be overridden per
 * deployment with MinFreeMb in the driver service Parameters registry
 * key (the Windows counterpart of the Linux page_reporting module
 * parameters), clamped to [64MB, RAM/2]. The release logic itself
 * (LowMemoryCondition handling, hysteresis band, gradual release) is
 * deliberately not configurable.
 *
 * Note on commit charge: the held pages are committed and pinned, so they
 * consume commit charge as well as physical memory. Commitment does not
 * occupy physical pages until first access (demand zero), so the physical
 * watermark alone says nothing about the remaining commit limit - the
 * reporting cycle therefore also hands pages back once the remaining
 * commit limit (RAM + pagefile - committed) runs low. A system-managed
 * pagefile absorbs most commit pressure by growing on its own.
 */

#include "precomp.h"
#include "ntddkex.h"

#if defined(EVENT_TRACING)
#include "reporting.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, BalloonReportInitialize)
#endif // ALLOC_PRAGMA

static NTSTATUS ReportingQueryMemoryState(OUT PULONG AvailablePages,
                                          OUT PULONG CommitHeadroomPages,
                                          OUT PULONG CommitLimitPages)
{
    SYSTEM_PERFORMANCE_INFORMATION perfInfo;
    ULONG outLen = 0;
    NTSTATUS status;

    RtlZeroMemory(&perfInfo, sizeof(perfInfo));
    status = ZwQuerySystemInformation(SystemPerformanceInformation, &perfInfo, sizeof(perfInfo), &outLen);
    if (NT_SUCCESS(status))
    {
        *AvailablePages = perfInfo.AvailablePages;
        *CommitLimitPages = perfInfo.CommitLimit;
        *CommitHeadroomPages = (perfInfo.CommitLimit > perfInfo.CommittedPages) ? (perfInfo.CommitLimit -
                                                                                   perfInfo.CommittedPages)
                                                                                : 0;
    }
    return status;
}

/*
 * The held pages are committed and pinned, so they consume commit charge as
 * well as physical memory. Commitment does not occupy physical pages until
 * first access (demand zero), so the available-memory watermark says nothing
 * about the remaining commit limit: workloads that reserve a lot of memory
 * without touching it can leave plenty of available pages while the commit
 * limit is nearly exhausted. Hand pages back before the held charge can eat
 * into that last reserve - built-in, deliberately not configurable.
 *
 * The reserve is anchored to the physical memory size rather than to the
 * commit limit: a large pagefile inflates the commit limit without making
 * a low headroom any more dangerous (materialization can be paged out),
 * so the reserve must not grow with the pagefile.
 */
static ULONG ReportingCommitReservePages(IN PDEVICE_CONTEXT devCtx)
{
    if (devCtx->ReportingMinCommitPages != 0)
    {
        return devCtx->ReportingMinCommitPages;
    }

    ULONG reserve = devCtx->ReportingTotalPages / REPORTING_COMMIT_HEADROOM_FRACTION;

    if (reserve < REPORTING_MIN_COMMIT_HEADROOM_PAGES)
    {
        reserve = REPORTING_MIN_COMMIT_HEADROOM_PAGES;
    }
    return reserve;
}

/*
 * The lower line of the commit headroom hysteresis band: the hold loop
 * stops taking pages at the reserve (see ReportingCommitReservePages),
 * and held pages are only handed back below half of it. The band
 * absorbs the batch overshoot of the hold loop (the gate is checked
 * before each batch, so the endpoint can land up to one batch below
 * the reserve) and the commit jitter of the guest, either of which
 * would otherwise hold and release the same pages in alternation.
 */
static __inline BOOLEAN ReportingCommitHeadroomLow(IN PDEVICE_CONTEXT devCtx, IN ULONG CommitHeadroomPages)
{
    if (devCtx->ReportingTotalPages == 0)
    {
        return FALSE;
    }
    return CommitHeadroomPages < ReportingCommitReservePages(devCtx) / 2;
}

static ULONG ReportingAllocWatermark(IN PDEVICE_CONTEXT devCtx)
{
    if (devCtx->ReportingMinFreePages != 0)
    {
        return devCtx->ReportingMinFreePages;
    }

    ULONG watermark = devCtx->ReportingTotalPages / REPORTING_AVAILABLE_FRACTION;

    if (watermark < REPORTING_MIN_AVAILABLE_PAGES)
    {
        watermark = REPORTING_MIN_AVAILABLE_PAGES;
    }
    return watermark;
}

/*
 * Splits an MDL returned by MmAllocatePagesForMdlEx into one scatter-gather
 * segment per 2MB block. The pages of a block are physically contiguous and
 * appear consecutively in the MDL, a well-formed block covers exactly 512
 * consecutive pages starting at the 2MB boundary. Anything else (which the
 * memory manager does not produce today) is never reported.
 */
static VOID ReportingExtractBlocks(IN PMDL Mdl, OUT PVIO_SG Segments, IN OUT PULONG SegmentCount)
{
    PPFN_NUMBER pfnArray = MmGetMdlPfnArray(Mdl);
    ULONG pageCount = MmGetMdlByteCount(Mdl) >> PAGE_SHIFT;
    ULONG i = 0;

    while (i < pageCount)
    {
        ULONGLONG blockIndex = pfnArray[i] >> REPORTING_BLOCK_SHIFT;
        ULONG start = i;

        while (i < pageCount && (pfnArray[i] >> REPORTING_BLOCK_SHIFT) == blockIndex)
        {
            i++;
        }

        if (i - start == REPORTING_BLOCK_PAGES && (pfnArray[start] & (REPORTING_BLOCK_PAGES - 1)) == 0)
        {
            ULONG segment;

            /* the caller flushes before every batch, so one batch can never
             * produce more segments than the array holds - the check is
             * defense in depth for a kernel driver */
            if (*SegmentCount >= REPORTING_MAX_SEGMENTS)
            {
                TraceEvents(TRACE_LEVEL_WARNING,
                            DBG_REPORTING,
                            "Segment array full (%u), skipping the remaining blocks\n",
                            *SegmentCount);
                break;
            }
            segment = (*SegmentCount)++;

            Segments[segment].physAddr.QuadPart = blockIndex << (PAGE_SHIFT + REPORTING_BLOCK_SHIFT);
            Segments[segment].length = REPORTING_BLOCK_SIZE;
        }
        else
        {
            TraceEvents(TRACE_LEVEL_WARNING,
                        DBG_REPORTING,
                        "Skipping a malformed 2MB block (PFN 0x%I64x, %d pages)\n",
                        (ULONGLONG)pfnArray[start],
                        i - start);
        }
    }
}

static VOID ReportingReleaseMdl(IN PDEVICE_CONTEXT devCtx, IN PPAGE_LIST_ENTRY PageListEntry)
{
    PMDL mdl = PageListEntry->PageMdl;
    ULONG pages = MmGetMdlByteCount(mdl) >> PAGE_SHIFT;

    /* the list and its counters are shared with the reporting thread
     * (see BalloonReportHold); the frees themselves run at PASSIVE_LEVEL
     * outside the lock */
    WdfSpinLockAcquire(devCtx->ReportingLock);
    devCtx->ReportingHeldPages -= pages;
    devCtx->ReportingMdlCount--;
    WdfSpinLockRelease(devCtx->ReportingLock);

    MmFreePagesFromMdl(mdl);
    ExFreePool(mdl);
    ExFreeToNPagedLookasideList(&devCtx->LookAsideList, PageListEntry);
}

/* hands the MaxMdls most recently held MDLs back to the guest */
static VOID ReportingReleasePages(IN PDEVICE_CONTEXT devCtx, IN ULONG MaxMdls)
{
    while (MaxMdls-- > 0)
    {
        PPAGE_LIST_ENTRY pageListEntry;

        WdfSpinLockAcquire(devCtx->ReportingLock);
        if (devCtx->ReportingMdlCount == 0)
        {
            WdfSpinLockRelease(devCtx->ReportingLock);
            break;
        }
        pageListEntry = (PPAGE_LIST_ENTRY)PopEntryList(&devCtx->ReportingMdlList);
        WdfSpinLockRelease(devCtx->ReportingLock);

        if (pageListEntry == NULL)
        {
            ASSERT(pageListEntry != NULL);
            break;
        }
        ReportingReleaseMdl(devCtx, pageListEntry);
    }
}

/*
 * Adds one report request to the reporting virtqueue and waits for the
 * host to acknowledge it, like the inflate and deflate paths wait for
 * theirs (see BalloonTellHost). Returns STATUS_UNSUCCESSFUL if the
 * request did not fit into the virtqueue (the host never saw those
 * pages), STATUS_TIMEOUT if the host did not acknowledge in time (the
 * request is still in flight: the virtio spec forbids using the
 * reported pages until the device returns the buffer, so the caller
 * freezes the batch, see ReportingFlushBatch).
 *
 * The reporting thread is the only waiter of RepAckEvent, so it never
 * races with the inflate and deflate acknowledgments.
 */
static NTSTATUS ReportingSendRequest(IN PDEVICE_CONTEXT devCtx, IN PVIO_SG Segments, IN ULONG SegmentCount)
{
    LARGE_INTEGER timeout;
    NTSTATUS status;
    bool doNotify;

    WdfSpinLockAcquire(devCtx->InfDefQueueLock);
    if (virtqueue_add_buf(devCtx->RepVirtQueue, Segments, 0, SegmentCount, devCtx, NULL, 0) < 0)
    {
        WdfSpinLockRelease(devCtx->InfDefQueueLock);
        return STATUS_UNSUCCESSFUL;
    }
    doNotify = virtqueue_kick_prepare(devCtx->RepVirtQueue);
    WdfSpinLockRelease(devCtx->InfDefQueueLock);

    if (doNotify)
    {
        virtqueue_notify(devCtx->RepVirtQueue);
    }

    timeout.QuadPart = Int32x32To64(1000, -10000);
    status = KeWaitForSingleObject(&devCtx->RepAckEvent, Executive, KernelMode, FALSE, &timeout);
    if (status == STATUS_TIMEOUT)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_REPORTING, "%s :: host did not acknowledge the report\n", __FUNCTION__);
    }
    return status;
}

static NTSTATUS ReportingSendBlocks(IN PDEVICE_CONTEXT devCtx, IN PVIO_SG Segments, IN ULONG SegmentCount)
{
    NTSTATUS status;
    ULONG i;

    status = ReportingSendRequest(devCtx, Segments, SegmentCount);
    if (status != STATUS_UNSUCCESSFUL)
    {
        return status;
    }

    /* the request did not fit into the virtqueue, report block by block */
    for (i = 0; i < SegmentCount; i++)
    {
        status = ReportingSendRequest(devCtx, &Segments[i], 1);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }
    return STATUS_SUCCESS;
}

/*
 * Moves the MDLs collected by the current hold cycle onto the held list
 * (only now are they safe to hand back: the host has acknowledged every
 * report that describes them). On a host timeout the batch is frozen on
 * the in-flight list instead - the requests may still complete late,
 * and the spec forbids using the reported pages until the device
 * returns the buffer, so those pages stay held by the driver until the
 * device is reset or removed (see BalloonReportReleaseInflight). They
 * are never re-reported: a new cycle would have to wait out the frozen
 * ones first, and one cycle is bounded by 512 MB anyway, which also
 * bounds what a dead host can pin.
 */
static VOID ReportingCollectBatch(IN PDEVICE_CONTEXT devCtx,
                                  IN PPAGE_LIST_ENTRY *PendingMdls,
                                  IN ULONG PendingCount,
                                  IN BOOLEAN Frozen)
{
    ULONG i;

    for (i = 0; i < PendingCount; i++)
    {
        PMDL mdl = PendingMdls[i]->PageMdl;
        ULONG pages = MmGetMdlByteCount(mdl) >> PAGE_SHIFT;

        WdfSpinLockAcquire(devCtx->ReportingLock);
        if (Frozen)
        {
            PushEntryList(&devCtx->ReportingInflightList, &PendingMdls[i]->SingleListEntry);
            devCtx->ReportingInflightCount++;
        }
        else
        {
            PushEntryList(&devCtx->ReportingMdlList, &PendingMdls[i]->SingleListEntry);
            devCtx->ReportingMdlCount++;
        }
        devCtx->ReportingHeldPages += pages;
        WdfSpinLockRelease(devCtx->ReportingLock);
    }
}

/* reports the collected segments and adopts the batch MDLs accordingly */
static BOOLEAN ReportingFlushBatch(IN PDEVICE_CONTEXT devCtx,
                                   IN PVIO_SG Segments,
                                   IN OUT PULONG SegmentCount,
                                   IN PPAGE_LIST_ENTRY *PendingMdls,
                                   IN OUT PULONG PendingCount)
{
    NTSTATUS status;

    if (*SegmentCount == 0)
    {
        return TRUE;
    }

    status = ReportingSendBlocks(devCtx, Segments, *SegmentCount);

    if (status == STATUS_TIMEOUT)
    {
        /* the report is still in flight: freeze the batch, the pages must
         * not be handed back to the guest until the device is done with
         * them (which only a reset or removal guarantees at this point) */
        TraceEvents(TRACE_LEVEL_ERROR,
                    DBG_REPORTING,
                    "Host did not acknowledge the report, freezing %u MDLs\n",
                    *PendingCount);
        ReportingCollectBatch(devCtx, PendingMdls, *PendingCount, TRUE);
        *SegmentCount = 0;
        *PendingCount = 0;
        return FALSE;
    }

    /* success, or the request never made it to the host (STATUS_UNSUCCESSFUL):
     * every page the host has seen is acknowledged, and the rest it never
     * saw - either way the batch can be handed back like any held batch */
    ReportingCollectBatch(devCtx, PendingMdls, *PendingCount, FALSE);

    if (NT_SUCCESS(status))
    {
        devCtx->ReportingReportedPages += *SegmentCount * REPORTING_BLOCK_PAGES;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_REPORTING, "Failed to report free pages\n");
    }
    *SegmentCount = 0;
    *PendingCount = 0;
    return NT_SUCCESS(status);
}

/*
 * Reads the optional EnableFpr value from the driver service Parameters
 * registry key. 0 (default) keeps the driver to the traditional balloon
 * behavior, 1 negotiates VIRTIO_BALLOON_F_PAGE_REPORTING whenever the
 * device offers it, no free page hints are configured and no IOMMU is
 * in the way. The feature is opt-in: the hold model has no Windows
 * precedent, so a deployment enables it explicitly instead of getting
 * it with a driver update.
 */
BOOLEAN ReportingIsEnabled(IN WDFDEVICE Device)
{
    DECLARE_CONST_UNICODE_STRING(valueName, L"EnableFpr");
    WDFKEY parametersKey = NULL;
    ULONG enable = 0;

    if (NT_SUCCESS(WdfDriverOpenParametersRegistryKey(WdfDeviceGetDriver(Device),
                                                      KEY_READ,
                                                      WDF_NO_OBJECT_ATTRIBUTES,
                                                      &parametersKey)))
    {
        WdfRegistryQueryULong(parametersKey, &valueName, &enable);
        WdfObjectDelete(parametersKey);
    }
    return enable != 0;
}

/*
 * Reads the optional free page reporting parameters from the driver
 * service Parameters registry key. A missing or zero value keeps the
 * automatic default of each:
 *   MinFreeMb        - available memory watermark override in MB,
 *                     clamped to [64MB, RAM/2]
 *   MinCommitMb      - commit headroom reserve override in MB,
 *                     clamped to [64MB, RAM/2]
 *   ReportIntervalMs - reporting cycle interval in ms,
 *                     clamped to [100, 60000]
 *   CooldownSec      - hold cooldown base after a full low-memory
 *                     release, clamped to [1, 600]
 */
static VOID ReportingReadParameters(IN WDFDEVICE Device, IN PDEVICE_CONTEXT devCtx)
{
    DECLARE_CONST_UNICODE_STRING(freeName, L"MinFreeMb");
    DECLARE_CONST_UNICODE_STRING(commitName, L"MinCommitMb");
    DECLARE_CONST_UNICODE_STRING(intervalName, L"ReportIntervalMs");
    DECLARE_CONST_UNICODE_STRING(cooldownName, L"CooldownSec");
    WDFKEY parametersKey = NULL;
    ULONG value = 0;

    devCtx->ReportingMinFreePages = 0;
    devCtx->ReportingMinCommitPages = 0;
    devCtx->ReportingIntervalMs = REPORTING_INTERVAL_MS;
    devCtx->ReportingCooldownSec = 0;

    if (!NT_SUCCESS(WdfDriverOpenParametersRegistryKey(WdfDeviceGetDriver(Device),
                                                       KEY_READ,
                                                       WDF_NO_OBJECT_ATTRIBUTES,
                                                       &parametersKey)))
    {
        return;
    }

    if (NT_SUCCESS(WdfRegistryQueryULong(parametersKey, &freeName, &value)) && value != 0)
    {
        ULONGLONG pages = (ULONGLONG)value * 1024 * 1024 / PAGE_SIZE;

        if (pages < REPORTING_HARD_MIN_AVAILABLE_PAGES)
        {
            pages = REPORTING_HARD_MIN_AVAILABLE_PAGES;
        }
        if (devCtx->ReportingTotalPages != 0 && pages > devCtx->ReportingTotalPages / 2)
        {
            pages = devCtx->ReportingTotalPages / 2;
        }
        devCtx->ReportingMinFreePages = (ULONG)pages;

        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_REPORTING,
                    "MinFreeMb=%u override, watermark %u pages\n",
                    value,
                    devCtx->ReportingMinFreePages);
    }

    if (NT_SUCCESS(WdfRegistryQueryULong(parametersKey, &commitName, &value)) && value != 0)
    {
        ULONGLONG pages = (ULONGLONG)value * 1024 * 1024 / PAGE_SIZE;

        if (pages < REPORTING_HARD_MIN_COMMIT_HEADROOM_PAGES)
        {
            pages = REPORTING_HARD_MIN_COMMIT_HEADROOM_PAGES;
        }
        if (devCtx->ReportingTotalPages != 0 && pages > devCtx->ReportingTotalPages / 2)
        {
            pages = devCtx->ReportingTotalPages / 2;
        }
        devCtx->ReportingMinCommitPages = (ULONG)pages;

        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_REPORTING,
                    "MinCommitMb=%u override, commit reserve %u pages\n",
                    value,
                    devCtx->ReportingMinCommitPages);
    }

    if (NT_SUCCESS(WdfRegistryQueryULong(parametersKey, &intervalName, &value)) && value != 0)
    {
        if (value < REPORTING_MIN_INTERVAL_MS)
        {
            value = REPORTING_MIN_INTERVAL_MS;
        }
        if (value > REPORTING_MAX_INTERVAL_MS)
        {
            value = REPORTING_MAX_INTERVAL_MS;
        }
        devCtx->ReportingIntervalMs = value;

        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_REPORTING, "ReportIntervalMs=%u override\n", value);
    }

    if (NT_SUCCESS(WdfRegistryQueryULong(parametersKey, &cooldownName, &value)) && value != 0)
    {
        if (value < REPORTING_MIN_COOLDOWN_SEC)
        {
            value = REPORTING_MIN_COOLDOWN_SEC;
        }
        if (value > REPORTING_MAX_COOLDOWN_SEC)
        {
            value = REPORTING_MAX_COOLDOWN_SEC;
        }
        devCtx->ReportingCooldownSec = value;

        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_REPORTING, "CooldownSec=%u override\n", value);
    }

    WdfObjectDelete(parametersKey);
}

/*
 * Hold cooldown with exponential backoff: after a full low-memory release
 * the guest needs time to recover before pages are taken again, otherwise
 * a persistent workload oscillates between release and re-hold. Each new
 * full release within the reset window of the previous one doubles the
 * wait (capped); a quiet period resets it to the base. The base is the
 * built-in default or the CooldownSec parameter.
 *
 * The window is measured from ReportingLastFullRelease (which is never
 * cleared) rather than from ReportingCooldownUntil: the latter is zeroed
 * when a cooldown expires, which would defeat the reset window, and while
 * a cooldown is still active subtracting it from 'now' wraps. Both fields
 * are only ever touched by the command thread.
 */
static VOID ReportingStartCooldown(IN PDEVICE_CONTEXT devCtx)
{
    ULONG64 now = KeQueryInterruptTime();
    ULONG baseMs = devCtx->ReportingCooldownSec != 0 ? devCtx->ReportingCooldownSec * 1000 : REPORTING_COOLDOWN_BASE_MS;
    ULONG maxMs = baseMs > REPORTING_COOLDOWN_MAX_MS ? baseMs : REPORTING_COOLDOWN_MAX_MS;

    if (devCtx->ReportingLastFullRelease != 0 && now > devCtx->ReportingLastFullRelease &&
        now - devCtx->ReportingLastFullRelease < (ULONG64)REPORTING_COOLDOWN_RESET_MS * 10000)
    {
        /* another full release within the reset window of the previous
         * one: back off exponentially */
        devCtx->ReportingCooldownMs *= 2;
        if (devCtx->ReportingCooldownMs > maxMs)
        {
            devCtx->ReportingCooldownMs = maxMs;
        }
    }
    else
    {
        devCtx->ReportingCooldownMs = baseMs;
    }
    devCtx->ReportingCooldownUntil = now + (ULONG64)devCtx->ReportingCooldownMs * 10000;
    devCtx->ReportingLastFullRelease = now;

    TraceEvents(TRACE_LEVEL_WARNING, DBG_REPORTING, "Hold cooldown for %u ms\n", devCtx->ReportingCooldownMs);
}

NTSTATUS
BalloonReportInitialize(IN WDFDEVICE Device)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
    SYSTEM_BASIC_INFORMATION basicInfo;
    ULONG outLen = 0;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_REPORTING, "--> %s\n", __FUNCTION__);

    devCtx->ReportingMdlList.Next = NULL;
    devCtx->ReportingMdlCount = 0;
    devCtx->ReportingInflightList.Next = NULL;
    devCtx->ReportingInflightCount = 0;
    devCtx->ReportingHeldPages = 0;
    devCtx->ReportingReportedPages = 0;
    devCtx->ReportingCooldownUntil = 0;
    devCtx->ReportingCooldownMs = 0;
    devCtx->ReportingLastFullRelease = 0;
    devCtx->ReportingIntervalMs = REPORTING_INTERVAL_MS;
    devCtx->ReportingCooldownSec = 0;

    /* the wake event may still be signaled from the previous D0 period
     * (the close path signals it to stop the thread); consume it so the
     * first cycle of this period runs on its interval, not immediately */
    KeClearEvent(&devCtx->RepWakeEvent);

    RtlZeroMemory(&basicInfo, sizeof(basicInfo));
    if (!NT_SUCCESS(ZwQuerySystemInformation(SystemBasicInformation, &basicInfo, sizeof(basicInfo), &outLen)))
    {
        basicInfo.NumberOfPhysicalPages = 0;
    }
    devCtx->ReportingTotalPages = basicInfo.NumberOfPhysicalPages;

    ReportingReadParameters(Device, devCtx);

    /* per-request segment limit. The negotiated reporting virtqueue is
     * expected to be much larger than the on-stack segment array bound
     * (QEMU uses 256 entries), and ReportingSendBlocks degrades to
     * block-by-block reporting should a request not fit after all. */
    devCtx->ReportingMaxSegments = REPORTING_MAX_SEGMENTS;
    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_REPORTING,
                "Per-request limit %u segments\n",
                devCtx->ReportingMaxSegments);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_REPORTING, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

VOID BalloonReportReleaseAll(IN WDFOBJECT WdfDevice)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_REPORTING, "--> %s\n", __FUNCTION__);

    ReportingReleasePages(devCtx, (ULONG)-1);

    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_REPORTING,
                "<-- %s :: %d pages still held\n",
                __FUNCTION__,
                devCtx->ReportingHeldPages);
}

/*
 * Frees the pages of reports the host never acknowledged. The caller
 * must have stopped the reporting thread and reset (or removed) the
 * device first: only then is the host guaranteed not to touch the
 * reported ranges anymore. Until that point those pages stay frozen -
 * they are deliberately not part of the held list, so no release path
 * can hand them back while a request might still be in flight.
 */
VOID BalloonReportReleaseInflight(IN WDFOBJECT WdfDevice)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_REPORTING, "--> %s\n", __FUNCTION__);

    for (;;)
    {
        PPAGE_LIST_ENTRY pageListEntry;

        WdfSpinLockAcquire(devCtx->ReportingLock);
        if (devCtx->ReportingInflightCount == 0)
        {
            WdfSpinLockRelease(devCtx->ReportingLock);
            break;
        }
        pageListEntry = (PPAGE_LIST_ENTRY)PopEntryList(&devCtx->ReportingInflightList);
        WdfSpinLockRelease(devCtx->ReportingLock);

        if (pageListEntry == NULL)
        {
            ASSERT(pageListEntry != NULL);
            break;
        }
        ReportingReleaseMdl(devCtx, pageListEntry);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_REPORTING,
                "<-- %s :: %d pages still held\n",
                __FUNCTION__,
                devCtx->ReportingHeldPages);
}

/*
 * The reporting decisions, run by the command thread (BalloonRoutine)
 * after every host command, on every watchdog tick and on a low memory
 * condition. Hands held pages back to the guest when the guest needs
 * them; taking pages is the reporting thread's business, at its own
 * cadence. All decisions are a few MDL frees at most, so they run at
 * the command thread's real-time priority - never the hold loop
 * itself.
 */
VOID BalloonReportCheckRelease(IN WDFOBJECT WdfDevice)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);
    ULONG availablePages = 0;
    ULONG commitHeadroomPages = 0;
    ULONG commitLimitPages = 0;
    ULONG allocWatermark;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_REPORTING, "--> %s\n", __FUNCTION__);

    if (devCtx->RepVirtQueue == NULL || devCtx->SurpriseRemoval || devCtx->bShutDown)
    {
        return;
    }

    if (!NT_SUCCESS(ReportingQueryMemoryState(&availablePages, &commitHeadroomPages, &commitLimitPages)))
    {
        return;
    }

    allocWatermark = ReportingAllocWatermark(devCtx);

    TraceEvents(TRACE_LEVEL_VERBOSE,
                DBG_REPORTING,
                "State: available %lu pages, watermark %lu pages, commit headroom %lu of %lu pages, %lu pages held\n",
                availablePages,
                allocWatermark,
                commitHeadroomPages,
                commitLimitPages,
                devCtx->ReportingHeldPages);

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
    if (IsLowMemory(WdfDevice))
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_REPORTING,
                    "Low memory condition, releasing %d held pages\n",
                    devCtx->ReportingHeldPages);
        BalloonReportReleaseAll(WdfDevice);
        ReportingStartCooldown(devCtx);
        return;
    }
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

    if (devCtx->ReportingMdlCount != 0 && ReportingCommitHeadroomLow(devCtx, commitHeadroomPages))
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_REPORTING,
                    "Commit headroom low (%d of %d pages), releasing half of the held pages\n",
                    commitHeadroomPages,
                    commitLimitPages);
        ReportingReleasePages(devCtx, devCtx->ReportingMdlCount / 2 + 1);
        return;
    }

    if (devCtx->ReportingMdlCount != 0 && availablePages < allocWatermark / 2)
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_REPORTING,
                    "Available memory low (%d pages), releasing half of the held pages\n",
                    availablePages);
        ReportingReleasePages(devCtx, devCtx->ReportingMdlCount / 2 + 1);
        return;
    }

    if (availablePages <= allocWatermark)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE,
                    DBG_REPORTING,
                    "Available %lu pages <= watermark %lu pages, idle\n",
                    availablePages,
                    allocWatermark);
        return;
    }

    /* hold cooldown: give the guest time to recover after a full
     * low-memory release before taking pages again */
    if (devCtx->ReportingCooldownUntil != 0)
    {
        if (KeQueryInterruptTime() < devCtx->ReportingCooldownUntil)
        {
            TraceEvents(TRACE_LEVEL_VERBOSE,
                        DBG_REPORTING,
                        "In cooldown (%u ms left), skipping hold\n",
                        (ULONG)((devCtx->ReportingCooldownUntil - KeQueryInterruptTime()) / 10000));
            return;
        }
        WdfSpinLockAcquire(devCtx->ReportingLock);
        devCtx->ReportingCooldownUntil = 0;
        WdfSpinLockRelease(devCtx->ReportingLock);
    }

    /* taking pages is the reporting thread's business, at its own
     * cadence (see BalloonReportRoutine) - nothing to ring here */

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_REPORTING, "<-- %s\n", __FUNCTION__);
}

/*
 * One hold cycle, run by the low-priority reporting thread
 * (BalloonReportRoutine) once per reporting interval. Allocates free
 * pages in 2MB-aligned blocks and reports them to the host. The gates
 * are re-checked before every batch, so the cycle stops immediately
 * when the guest starts needing the memory; the release decisions
 * themselves belong to the command thread (see
 * BalloonReportCheckRelease).
 *
 * The MDLs of a batch only join the held list once the host has
 * acknowledged the report describing them (see ReportingFlushBatch) -
 * until then they are private to this thread, so no release path can
 * hand back pages the host may still be walking.
 */
VOID BalloonReportHold(IN WDFOBJECT WdfDevice)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);
    ULONG availablePages = 0;
    ULONG commitHeadroomPages = 0;
    ULONG commitLimitPages = 0;
    ULONG allocWatermark;
    ULONG batches = 0;
    VIO_SG segments[REPORTING_MAX_SEGMENTS];
    ULONG segmentCount = 0;
    PPAGE_LIST_ENTRY pendingMdls[REPORTING_BATCHES_PER_CYCLE];
    ULONG pendingCount = 0;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_REPORTING, "--> %s\n", __FUNCTION__);

    if (devCtx->RepVirtQueue == NULL || devCtx->SurpriseRemoval || devCtx->bShutDown)
    {
        return;
    }

    /* reports frozen after a host timeout stay frozen until the device is
     * reset or removed; do not take new pages while any are outstanding */
    if (devCtx->ReportingInflightCount != 0)
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_REPORTING,
                    "%u reports still in flight, skipping the hold cycle\n",
                    devCtx->ReportingInflightCount);
        return;
    }

    if (!NT_SUCCESS(ReportingQueryMemoryState(&availablePages, &commitHeadroomPages, &commitLimitPages)))
    {
        return;
    }

    allocWatermark = ReportingAllocWatermark(devCtx);

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
    /* never take pages while the memory manager is sounding the alarm;
     * the command thread releases and starts the cooldown */
    if (IsLowMemory(WdfDevice))
    {
        return;
    }
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

    if (availablePages <= allocWatermark)
    {
        return;
    }

    /* hold cooldown, as re-checked by the command thread (see
     * BalloonReportCheckRelease). The field is written by the command
     * thread; on 32-bit builds an unlocked ULONG64 read could tear, so
     * take the lock for the read - it runs once per interval */
    {
        ULONGLONG cooldownUntil;

        WdfSpinLockAcquire(devCtx->ReportingLock);
        cooldownUntil = devCtx->ReportingCooldownUntil;
        WdfSpinLockRelease(devCtx->ReportingLock);

        if (cooldownUntil != 0 && KeQueryInterruptTime() < cooldownUntil)
        {
            return;
        }
    }

    while (batches < REPORTING_BATCHES_PER_CYCLE)
    {
        PHYSICAL_ADDRESS LowAddress;
        PHYSICAL_ADDRESS HighAddress;
        PHYSICAL_ADDRESS SkipBytes;
        PPAGE_LIST_ENTRY pageListEntry;
        PMDL mdl;

        if (devCtx->bShutDown)
        {
            break;
        }

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
        if (IsLowMemory(WdfDevice))
        {
            break;
        }
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

        /* re-check the available memory and commit headroom while
         * filling up; these are the upper lines of the two hysteresis
         * bands, the release lines sit at half of them */
        if (!NT_SUCCESS(ReportingQueryMemoryState(&availablePages, &commitHeadroomPages, &commitLimitPages)) ||
            availablePages <= allocWatermark || commitHeadroomPages < ReportingCommitReservePages(devCtx))
        {
            break;
        }

        /* flush the pending segments, the next batch produces up to
         * ReportingMaxSegments more (one segment per 2MB block); only an
         * acknowledged batch joins the held list */
        if (!ReportingFlushBatch(devCtx, segments, &segmentCount, pendingMdls, &pendingCount))
        {
            return;
        }

        LowAddress.QuadPart = 0;
        HighAddress.QuadPart = (ULONGLONG)-1;
        SkipBytes.QuadPart = REPORTING_BLOCK_SIZE;

        /* MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS with SkipBytes = 2MB
         * guarantees that every chunk in the returned MDL is exactly 2MB
         * long and aligned on a 2MB boundary, preferably taken from the
         * system's large page cache. MM_DONT_ZERO_ALLOCATION keeps the
         * pages clean on the host, the cache type is irrelevant as the
         * pages are never mapped. The batch matches the per-request
         * segment limit, so a full batch always fits into one request. */
        mdl = MmAllocatePagesForMdlEx(LowAddress,
                                      HighAddress,
                                      SkipBytes,
                                      (ULONGLONG)devCtx->ReportingMaxSegments * REPORTING_BLOCK_SIZE,
                                      MmCached,
                                      MM_DONT_ZERO_ALLOCATION | MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS);
        if (mdl == NULL || MmGetMdlByteCount(mdl) == 0)
        {
            /* allocation failure means memory pressure or no more aligned
             * blocks, stop for this cycle */
            if (mdl != NULL)
            {
                MmFreePagesFromMdl(mdl);
                ExFreePool(mdl);
            }
            break;
        }

        pageListEntry = (PPAGE_LIST_ENTRY)ExAllocateFromNPagedLookasideList(&devCtx->LookAsideList);
        if (pageListEntry == NULL)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_REPORTING, "Failed to allocate list entry.\n");
            MmFreePagesFromMdl(mdl);
            ExFreePool(mdl);
            return;
        }

        pageListEntry->PageMdl = mdl;

        /* the batch stays private to this thread until its report is
         * acknowledged (see ReportingFlushBatch); the allocation above
         * ran outside the lock */
        pendingMdls[pendingCount++] = pageListEntry;

        ReportingExtractBlocks(mdl, segments, &segmentCount);

        TraceEvents(TRACE_LEVEL_VERBOSE,
                    DBG_REPORTING,
                    "Batch %u: %u blocks, %u pending segments, %u pages held\n",
                    batches,
                    MmGetMdlByteCount(mdl) / REPORTING_BLOCK_SIZE,
                    segmentCount,
                    devCtx->ReportingHeldPages);
        batches++;
    }

    if (!ReportingFlushBatch(devCtx, segments, &segmentCount, pendingMdls, &pendingCount))
    {
        return;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_REPORTING,
                "Held %d pages, reported %d pages in total\n",
                devCtx->ReportingHeldPages,
                devCtx->ReportingReportedPages);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_REPORTING, "<-- %s\n", __FUNCTION__);
}
