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
 * not every page freed - so the engine is driven by polling: the release
 * decisions re-check both memory ledgers on every call, and the hold
 * cycle paces itself by the reporting interval. The engine exposes the
 * two roles as separate entry points (BalloonReportCheckRelease and
 * BalloonReportHold); how they are scheduled is a device-integration
 * concern and follows in subsequent patches.
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
            ULONG segment = (*SegmentCount)++;

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

    devCtx->ReportingHeldPages -= MmGetMdlByteCount(mdl) >> PAGE_SHIFT;
    devCtx->ReportingMdlCount--;

    MmFreePagesFromMdl(mdl);
    ExFreePool(mdl);
    ExFreeToNPagedLookasideList(&devCtx->LookAsideList, PageListEntry);
}

/* hands the MaxMdls most recently held MDLs back to the guest */
static VOID ReportingReleasePages(IN PDEVICE_CONTEXT devCtx, IN ULONG MaxMdls)
{
    while (devCtx->ReportingMdlCount != 0 && MaxMdls-- > 0)
    {
        PPAGE_LIST_ENTRY pageListEntry = (PPAGE_LIST_ENTRY)PopEntryList(&devCtx->ReportingMdlList);

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
 * theirs (see BalloonTellHost). Returns STATUS_UNSUCCESSFUL if the request
 * did not fit into the virtqueue, any other error indicates a host
 * timeout; a timed-out cycle is abandoned and retried on the next
 * reporting cycle.
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
    status = KeWaitForSingleObject(&devCtx->HostAckEvent, Executive, KernelMode, FALSE, &timeout);
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

/* reports the collected segments and resets the segment counter */
static BOOLEAN ReportingFlushSegments(IN PDEVICE_CONTEXT devCtx, IN PVIO_SG Segments, IN OUT PULONG SegmentCount)
{
    if (*SegmentCount == 0)
    {
        return TRUE;
    }

    if (NT_SUCCESS(ReportingSendBlocks(devCtx, Segments, *SegmentCount)))
    {
        devCtx->ReportingReportedPages += *SegmentCount * REPORTING_BLOCK_PAGES;
        *SegmentCount = 0;
        return TRUE;
    }

    TraceEvents(TRACE_LEVEL_ERROR, DBG_REPORTING, "Failed to report free pages\n");
    return FALSE;
}

/*
 * Reads the optional EnableFpr value from the driver service Parameters
 * registry key. 1 (default) negotiates VIRTIO_BALLOON_F_PAGE_REPORTING
 * whenever the device offers it and no IOMMU is in the way, 0 keeps the
 * driver to the traditional balloon behavior - an escape hatch for
 * deployments that want free page reporting off without touching the
 * host-side device configuration.
 */
BOOLEAN ReportingIsEnabled(IN WDFDEVICE Device)
{
    DECLARE_CONST_UNICODE_STRING(valueName, L"EnableFpr");
    WDFKEY parametersKey = NULL;
    ULONG enable = 1;

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
 */
static VOID ReportingStartCooldown(IN PDEVICE_CONTEXT devCtx)
{
    ULONG64 now = KeQueryInterruptTime();
    ULONG baseMs = devCtx->ReportingCooldownSec != 0 ? devCtx->ReportingCooldownSec * 1000 : REPORTING_COOLDOWN_BASE_MS;
    ULONG maxMs = baseMs > REPORTING_COOLDOWN_MAX_MS ? baseMs : REPORTING_COOLDOWN_MAX_MS;

    if (devCtx->ReportingCooldownUntil != 0 &&
        now - devCtx->ReportingCooldownUntil < (ULONG64)REPORTING_COOLDOWN_RESET_MS * 10000)
    {
        /* another full release while still close to the previous cooldown:
         * back off exponentially */
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
    devCtx->ReportingHeldPages = 0;
    devCtx->ReportingReportedPages = 0;
    devCtx->ReportingCooldownUntil = 0;
    devCtx->ReportingCooldownMs = 0;
    devCtx->ReportingIntervalMs = REPORTING_INTERVAL_MS;
    devCtx->ReportingCooldownSec = 0;

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
 * The release decisions: hands held pages back to the guest when the
 * guest needs the memory - all at once on a low memory condition, or
 * half of them per call once either ledger runs low.
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
        devCtx->ReportingCooldownUntil = 0;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_REPORTING, "<-- %s\n", __FUNCTION__);
}

/*
 * One hold cycle: allocates free pages in 2MB-aligned blocks and reports
 * them to the host. The gates are re-checked before every batch, so the
 * cycle stops immediately when the guest starts needing the memory.
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

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
    /* never take pages while the memory manager is sounding the alarm */
    if (IsLowMemory(WdfDevice))
    {
        return;
    }
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

    if (availablePages <= allocWatermark)
    {
        return;
    }

    while (batches < REPORTING_BATCHES_PER_CYCLE)
    {
        PHYSICAL_ADDRESS LowAddress;
        PHYSICAL_ADDRESS HighAddress;
        PHYSICAL_ADDRESS SkipBytes;
        PPAGE_LIST_ENTRY pageListEntry;
        PMDL mdl;

        /* re-check the available memory and commit headroom while
         * filling up; these are the upper lines of the two hysteresis
         * bands, the release lines sit at half of them */
        if (!NT_SUCCESS(ReportingQueryMemoryState(&availablePages, &commitHeadroomPages, &commitLimitPages)) ||
            availablePages <= allocWatermark || commitHeadroomPages < ReportingCommitReservePages(devCtx))
        {
            break;
        }

        /* flush the pending segments, the next batch produces up to
         * ReportingMaxSegments more (one segment per 2MB block) */
        if (!ReportingFlushSegments(devCtx, segments, &segmentCount))
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
        PushEntryList(&devCtx->ReportingMdlList, &pageListEntry->SingleListEntry);
        devCtx->ReportingMdlCount++;
        devCtx->ReportingHeldPages += MmGetMdlByteCount(mdl) >> PAGE_SHIFT;

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

    if (!ReportingFlushSegments(devCtx, segments, &segmentCount))
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
