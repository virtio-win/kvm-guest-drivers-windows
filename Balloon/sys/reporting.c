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
 * Report-then-release does not work on Windows: handing the pages back
 * to the OS makes them allocatable again, and ordinary guest activity
 * can reuse such pages, so the host-side reclaim is not durable (whether
 * the memory manager additionally writes the freed pages varies with
 * the Windows version and the allocation form and is undocumented). The
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
 * decisions re-check both memory counters on every call, and the hold
 * cycle paces itself by the reporting interval. The engine exposes the
 * two roles as separate entry points (BalloonReportCheckRelease and
 * BalloonReportHold); how they are scheduled is a device-integration
 * concern and follows in subsequent patches.
 *
 * Each of the two counters the driver watches - available memory and
 * commit headroom - has a floor: the line below which held pages are
 * handed back (demand-sized, back above the gate). The floors default
 * to max(RAM/16, 128MB) and can be overridden per deployment
 * with MinFreeMb / MinCommitMb in the driver service
 * Parameters registry key, clamped to [64MB, RAM/2]; the hysteresis
 * band between the gate and the floor defaults to
 * max(min(RAM/32, 1GB), 256MB) and can be overridden with HysteresisMb,
 * clamped to [256MB, RAM/4] - the Windows counterpart of the Linux
 * page_reporting module parameters. The release logic itself
 * (LowMemoryCondition handling) is deliberately not configurable.
 *
 * Note on commit charge: the held pages are committed and pinned, so they
 * consume commit charge as well as physical memory. Commitment does not
 * occupy physical pages until first access (demand zero), so the physical
 * floor alone says nothing about the remaining commit limit - the
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
 * The lower line of available memory: held pages are handed back once
 * the available memory falls below this floor, and pages are only
 * taken above floor + hysteresis (see ReportingFreeGatePages), so in
 * the steady state the guest does not stay below it - the release
 * decisions defend the floor at their sampling cadence, not
 * instantaneously.
 */
static ULONG ReportingFreeFloorPages(IN PDEVICE_CONTEXT devCtx)
{
    if (devCtx->ReportingMinFreePages != 0)
    {
        return devCtx->ReportingMinFreePages;
    }

    ULONG floor = devCtx->ReportingTotalPages / REPORTING_FLOOR_FRACTION;

    if (floor < REPORTING_MIN_FLOOR_PAGES)
    {
        floor = REPORTING_MIN_FLOOR_PAGES;
    }
    return floor;
}

/*
 * The lower line of commit headroom, same rule as
 * the available-memory floor. The floor is anchored to the physical
 * memory size rather than to the commit limit: a large pagefile inflates
 * the commit limit without making a low headroom any more dangerous
 * (materialization can be paged out), so the floor must not grow with
 * the pagefile.
 */
static ULONG ReportingCommitFloorPages(IN PDEVICE_CONTEXT devCtx)
{
    if (devCtx->ReportingMinCommitPages != 0)
    {
        return devCtx->ReportingMinCommitPages;
    }

    ULONG floor = devCtx->ReportingTotalPages / REPORTING_FLOOR_FRACTION;

    if (floor < REPORTING_MIN_FLOOR_PAGES)
    {
        floor = REPORTING_MIN_FLOOR_PAGES;
    }
    return floor;
}

/*
 * The width of the hysteresis band above the floors, shared by the two
 * counters. The band absorbs the batch overshoot of the hold loop (the
 * gate is checked before each batch, so the endpoint can land up to one
 * batch below the gate) and the jitter of the guest, either of which
 * would otherwise hold and release the same pages in alternation.
 */
static ULONG ReportingHysteresisBandPages(IN PDEVICE_CONTEXT devCtx)
{
    if (devCtx->ReportingHysteresisPages != 0)
    {
        return devCtx->ReportingHysteresisPages;
    }

    ULONG band = devCtx->ReportingTotalPages / REPORTING_HYSTERESIS_FRACTION;

    if (band < REPORTING_MIN_HYSTERESIS_PAGES)
    {
        band = REPORTING_MIN_HYSTERESIS_PAGES;
    }
    if (band > REPORTING_MAX_AUTO_HYSTERESIS_PAGES)
    {
        band = REPORTING_MAX_AUTO_HYSTERESIS_PAGES;
    }
    return band;
}

/*
 * The upper line of a counter (the gate): the hold loop stops taking
 * pages once the counter falls to floor + hysteresis, which is what keeps
 * the floor a defended line rather than a best effort.
 */
static ULONG ReportingFreeGatePages(IN PDEVICE_CONTEXT devCtx)
{
    return ReportingFreeFloorPages(devCtx) + ReportingHysteresisBandPages(devCtx);
}

static ULONG ReportingCommitGatePages(IN PDEVICE_CONTEXT devCtx)
{
    return ReportingCommitFloorPages(devCtx) + ReportingHysteresisBandPages(devCtx);
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
 * Releases whole held MDLs until at least PagesNeeded pages have been
 * handed back (or the held list runs out). The caller computes how
 * many pages the counter is short of its gate, and exactly that
 * many pages (rounded up to whole MDLs) are returned - no more, so a
 * small dip below the floor does not churn the whole hold. The callers
 * only invoke it with the counter below its floor, so the gate
 * minus the current reading never wraps.
 */
static VOID ReportingReleaseUpTo(IN PDEVICE_CONTEXT devCtx, IN ULONG PagesNeeded)
{
    ULONG freedPages = 0;

    while (freedPages < PagesNeeded && devCtx->ReportingMdlCount != 0)
    {
        PPAGE_LIST_ENTRY pageListEntry = (PPAGE_LIST_ENTRY)PopEntryList(&devCtx->ReportingMdlList);

        if (pageListEntry == NULL)
        {
            ASSERT(pageListEntry != NULL);
            break;
        }
        freedPages += MmGetMdlByteCount(pageListEntry->PageMdl) >> PAGE_SHIFT;
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
 *   MinFreeMb        - available memory floor override in MB,
 *                     clamped to [64MB, RAM/2]
 *   MinCommitMb      - commit headroom floor override in MB,
 *                     clamped to [64MB, RAM/2]
 *   HysteresisMb     - hysteresis band width override in MB,
 *                     clamped to [256MB, RAM/4]
 *   ReportIntervalMs - reporting cycle interval in ms,
 *                     clamped to [100, 60000]
 *   CooldownSec      - hold cooldown base after a full low-memory
 *                     release, clamped to [1, 600]
 */
static VOID ReportingReadParameters(IN WDFDEVICE Device, IN PDEVICE_CONTEXT devCtx)
{
    DECLARE_CONST_UNICODE_STRING(freeName, L"MinFreeMb");
    DECLARE_CONST_UNICODE_STRING(commitName, L"MinCommitMb");
    DECLARE_CONST_UNICODE_STRING(hysteresisName, L"HysteresisMb");
    DECLARE_CONST_UNICODE_STRING(intervalName, L"ReportIntervalMs");
    DECLARE_CONST_UNICODE_STRING(cooldownName, L"CooldownSec");
    WDFKEY parametersKey = NULL;
    ULONG value = 0;

    devCtx->ReportingMinFreePages = 0;
    devCtx->ReportingMinCommitPages = 0;
    devCtx->ReportingHysteresisPages = 0;
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

        if (pages < REPORTING_HARD_MIN_FLOOR_PAGES)
        {
            pages = REPORTING_HARD_MIN_FLOOR_PAGES;
        }
        if (devCtx->ReportingTotalPages != 0 && pages > devCtx->ReportingTotalPages / 2)
        {
            pages = devCtx->ReportingTotalPages / 2;
        }
        devCtx->ReportingMinFreePages = (ULONG)pages;

        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_REPORTING,
                    "MinFreeMb=%u override, available floor %u pages\n",
                    value,
                    devCtx->ReportingMinFreePages);
    }

    if (NT_SUCCESS(WdfRegistryQueryULong(parametersKey, &commitName, &value)) && value != 0)
    {
        ULONGLONG pages = (ULONGLONG)value * 1024 * 1024 / PAGE_SIZE;

        if (pages < REPORTING_HARD_MIN_FLOOR_PAGES)
        {
            pages = REPORTING_HARD_MIN_FLOOR_PAGES;
        }
        if (devCtx->ReportingTotalPages != 0 && pages > devCtx->ReportingTotalPages / 2)
        {
            pages = devCtx->ReportingTotalPages / 2;
        }
        devCtx->ReportingMinCommitPages = (ULONG)pages;

        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_REPORTING,
                    "MinCommitMb=%u override, commit floor %u pages\n",
                    value,
                    devCtx->ReportingMinCommitPages);
    }

    if (NT_SUCCESS(WdfRegistryQueryULong(parametersKey, &hysteresisName, &value)) && value != 0)
    {
        ULONGLONG pages = (ULONGLONG)value * 1024 * 1024 / PAGE_SIZE;

        /* the lower clamp keeps the band wide enough to absorb the batch
         * overshoot and the guest jitter - a band narrower than both
         * makes the hold oscillate between taking and releasing */
        if (pages < REPORTING_MIN_HYSTERESIS_PAGES)
        {
            pages = REPORTING_MIN_HYSTERESIS_PAGES;
        }
        if (devCtx->ReportingTotalPages != 0 && pages > devCtx->ReportingTotalPages / REPORTING_HYSTERESIS_FRACTION_MAX)
        {
            pages = devCtx->ReportingTotalPages / REPORTING_HYSTERESIS_FRACTION_MAX;
        }
        devCtx->ReportingHysteresisPages = (ULONG)pages;

        TraceEvents(TRACE_LEVEL_INFORMATION,
                    DBG_REPORTING,
                    "HysteresisMb=%u override, band %u pages\n",
                    value,
                    devCtx->ReportingHysteresisPages);
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
    devCtx->ReportingHeldPages = 0;
    devCtx->ReportingReportedPages = 0;
    devCtx->ReportingCooldownUntil = 0;
    devCtx->ReportingCooldownMs = 0;
    devCtx->ReportingLastFullRelease = 0;
    devCtx->ReportingIntervalMs = REPORTING_INTERVAL_MS;
    devCtx->ReportingCooldownSec = 0;

    RtlZeroMemory(&basicInfo, sizeof(basicInfo));
    if (!NT_SUCCESS(ZwQuerySystemInformation(SystemBasicInformation, &basicInfo, sizeof(basicInfo), &outLen)))
    {
        basicInfo.NumberOfPhysicalPages = 0;
    }
    devCtx->ReportingTotalPages = basicInfo.NumberOfPhysicalPages;

    ReportingReadParameters(Device, devCtx);

    /* per-request segment limit, matched to the batch allocation below
     * (the same constant sizes the on-stack segment array in
     * BalloonReportHold - a full batch always fits into one request).
     * QEMU sizes the reporting virtqueue at 32 descriptors, equal to
     * this bound; ReportingSendBlocks degrades to block-by-block
     * reporting should a request not fit after all. Do not change one
     * without the other. */
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
                "<-- %s :: %lu pages still held\n",
                __FUNCTION__,
                devCtx->ReportingHeldPages);
}

/*
 * The release decisions: hands held pages back to the guest when the
 * guest needs the memory - all at once on a low memory condition, or
 * exactly enough to lift the counter back above its gate once either
 * counter runs low.
 */
VOID BalloonReportCheckRelease(IN WDFOBJECT WdfDevice)
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);
    ULONG availablePages = 0;
    ULONG commitHeadroomPages = 0;
    ULONG commitLimitPages = 0;
    ULONG freeFloor;
    ULONG freeGate;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_REPORTING, "--> %s\n", __FUNCTION__);

    if (devCtx->RepVirtQueue == NULL || devCtx->SurpriseRemoval || devCtx->bShutDown)
    {
        return;
    }

    if (!NT_SUCCESS(ReportingQueryMemoryState(&availablePages, &commitHeadroomPages, &commitLimitPages)))
    {
        return;
    }

    freeFloor = ReportingFreeFloorPages(devCtx);
    freeGate = ReportingFreeGatePages(devCtx);

    TraceEvents(TRACE_LEVEL_VERBOSE,
                DBG_REPORTING,
                "State: available %lu pages, floor %lu pages, commit headroom %lu of %lu pages, %lu pages held\n",
                availablePages,
                freeFloor,
                commitHeadroomPages,
                commitLimitPages,
                devCtx->ReportingHeldPages);

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
    if (IsLowMemory(WdfDevice))
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_REPORTING,
                    "Low memory condition, releasing %lu held pages\n",
                    devCtx->ReportingHeldPages);
        BalloonReportReleaseAll(WdfDevice);
        ReportingStartCooldown(devCtx);
        return;
    }
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

    if (devCtx->ReportingMdlCount != 0 && commitHeadroomPages < ReportingCommitFloorPages(devCtx))
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_REPORTING,
                    "Commit headroom below floor (%lu of %lu pages), releasing up to the gate\n",
                    commitHeadroomPages,
                    commitLimitPages);
        ReportingReleaseUpTo(devCtx, ReportingCommitGatePages(devCtx) - commitHeadroomPages);
        return;
    }

    if (devCtx->ReportingMdlCount != 0 && availablePages < freeFloor)
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    DBG_REPORTING,
                    "Available memory below floor (%lu pages), releasing up to the gate\n",
                    availablePages);
        ReportingReleaseUpTo(devCtx, freeGate - availablePages);
        return;
    }

    if (availablePages <= freeGate)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE,
                    DBG_REPORTING,
                    "Available %lu pages <= gate %lu pages, idle\n",
                    availablePages,
                    freeGate);
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
    ULONG freeGate;
    ULONG commitGate;
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

    freeGate = ReportingFreeGatePages(devCtx);
    commitGate = ReportingCommitGatePages(devCtx);

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
    /* never take pages while the memory manager is sounding the alarm */
    if (IsLowMemory(WdfDevice))
    {
        return;
    }
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

    if (availablePages <= freeGate)
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
         * filling up: these are the gates of the two hysteresis bands
         * (floor + hysteresis, see ReportingFreeGatePages), the release
         * lines sit at the floors */
        if (!NT_SUCCESS(ReportingQueryMemoryState(&availablePages, &commitHeadroomPages, &commitLimitPages)) ||
            availablePages <= freeGate || commitHeadroomPages < commitGate)
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
                "Held %lu pages, reported %lu pages in total\n",
                devCtx->ReportingHeldPages,
                devCtx->ReportingReportedPages);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_REPORTING, "<-- %s\n", __FUNCTION__);
}
