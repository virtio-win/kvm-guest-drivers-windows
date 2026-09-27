/*
 * Main include file
 * This file contains various routines and globals
 *
 * Copyright (c) 2009-2017 Red Hat, Inc.
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
#if !defined(_PROTOTYPES_H_)
#define _PROTOTYPES_H_

#include "virtio.h"
#include "public.h"
#include "trace.h"

/* The ID for virtio_balloon */
#define VIRTIO_ID_BALLOON                        5

/* The feature bitmap for virtio balloon */
#define VIRTIO_BALLOON_F_MUST_TELL_HOST          0 /* Tell before reclaiming pages */
#define VIRTIO_BALLOON_F_STATS_VQ                1 /* Memory status virtqueue */
#define VIRTIO_BALLOON_F_PAGE_REPORTING          5 /* Free page reporting virtqueue */

/*
 * Free page reporting (VIRTIO_BALLOON_F_PAGE_REPORTING) tuning parameters.
 *
 * Reported blocks are always 2MB in size and 2MB-aligned. This matches the
 * host-side transparent huge page granularity and the default reporting
 * granularity of the Linux free page reporting implementation, which only
 * reports blocks of pageblock_order and larger (order 9, i.e. 2MB, on
 * architectures with 4KB base pages). The virtio specification requires
 * the driver to "attempt to report large pages rather than smaller ones".
 *
 * The alignment is provided by MmAllocatePagesForMdlEx called with
 * MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS and SkipBytes = 2MB: the memory
 * manager returns complete 2MB blocks, each guaranteed to be exactly 2MB
 * long and aligned on a 2MB boundary, preferably taken from the system's
 * large page cache.
 */
#define REPORTING_BLOCK_SHIFT                    9                                     /* log2(512) */
#define REPORTING_BLOCK_PAGES                    (1UL << REPORTING_BLOCK_SHIFT)        /* = 512 x 4KB pages */
#define REPORTING_BLOCK_SIZE                     (REPORTING_BLOCK_PAGES << PAGE_SHIFT) /* = 2MB */

/* One batch allocates at most ReportingMaxSegments 2MB blocks, so a
 * full batch always fits into a single report request.
 * Max number of allocation batches per reporting cycle: */
#define REPORTING_BATCHES_PER_CYCLE              8
/* Upper bound of the segments per report request, see
 * BalloonReportInitialize */
#define REPORTING_MAX_SEGMENTS                   32
/* Reporting cycle interval default, matches the Linux page_reporting_delay_ms
 * default; overridable per deployment with the ReportIntervalMs value in the
 * driver service Parameters registry key, clamped to [100, 60000] */
#define REPORTING_INTERVAL_MS                    2000
#define REPORTING_MIN_INTERVAL_MS                100
#define REPORTING_MAX_INTERVAL_MS                60000
/* Commit headroom watchdog cadence while pages are held (the Windows memory
 * manager exposes no event for a low commit limit, so it is polled) */
#define REPORTING_COMMIT_POLL_MS                 100
/* Re-check pacing while the low memory condition event stays signaled
 * (notification events stay signaled until the memory manager clears
 * them, and re-running the release path would change nothing) */
#define REPORTING_EVENT_RETRY_MS                 1000

/*
 * Watermarks (in 4KB pages) controlling when pages are taken from and
 * returned to the guest. Pages are only allocated while at least an
 * eighth of the physical RAM (but never less than 256MB) remains
 * available to the guest. Once half of that amount is left, held pages
 * are handed back. This default can be overridden per deployment with
 * the MinFreeMb value in the driver service Parameters registry key,
 * clamped to [64MB, RAM/2] - in the spirit of the Linux page_reporting
 * module parameters. The mechanism itself (LowMemoryCondition handling,
 * hysteresis band, gradual release) is not configurable.
 */
#define REPORTING_AVAILABLE_FRACTION             8
#define REPORTING_MIN_AVAILABLE_PAGES            (256UL * 1024 * 1024 / PAGE_SIZE)
#define REPORTING_HARD_MIN_AVAILABLE_PAGES       (64UL * 1024 * 1024 / PAGE_SIZE)

/*
 * Commit headroom protection (built-in, not configurable): the held pages
 * consume commit charge, and commitment does not occupy physical pages
 * until first access, so the available-memory watermark alone cannot
 * prevent the held charge from eating into the last commit reserve of
 * workloads that reserve a lot of memory without touching it. An
 * exhausted commit limit fails allocations exactly as an exhausted
 * physical pool does, so both pools are kept above the same reserve rule.
 *
 * Like the available memory watermark this is a two-line rule: pages are
 * only taken while the remaining commit limit (RAM + pagefile - committed)
 * stays above the reserve - an eighth of the physical memory size, but
 * never less than 256MB - and handed back once it falls to half of the
 * reserve. The band between the two lines absorbs the batch overshoot of
 * the hold loop and the commit jitter of the guest, either of which would
 * otherwise hold and release the same pages in alternation.
 *
 * The reserve is deliberately not tied to the commit limit: a large
 * pagefile inflates the limit without making a low headroom more
 * dangerous (materialization can be paged out), so the reserve must not
 * grow with the pagefile.
 */
#define REPORTING_COMMIT_HEADROOM_FRACTION       8
#define REPORTING_MIN_COMMIT_HEADROOM_PAGES      (256UL * 1024 * 1024 / PAGE_SIZE)
#define REPORTING_HARD_MIN_COMMIT_HEADROOM_PAGES (64UL * 1024 * 1024 / PAGE_SIZE)

/*
 * Hold cooldown after a full low-memory release (built-in, not
 * configurable): once all held pages are handed back, wait before taking
 * pages again so that the guest can actually recover - otherwise the next
 * reporting cycle starts re-holding immediately and a guest with a
 * persistent workload oscillates between release and re-hold. Each new
 * full release within the reset window doubles the wait (exponential
 * backoff, capped); a quiet period resets it to the base.
 */
#define REPORTING_COOLDOWN_BASE_MS               (60UL * 1000)
#define REPORTING_COOLDOWN_MAX_MS                (5UL * 60 * 1000)
#define REPORTING_COOLDOWN_RESET_MS              (10UL * 60 * 1000)
/* CooldownSec override clamps, in seconds */
#define REPORTING_MIN_COOLDOWN_SEC               1
#define REPORTING_MAX_COOLDOWN_SEC               600

typedef struct _VIRTIO_BALLOON_CONFIG
{
    u32 num_pages;
    u32 actual;
} VIRTIO_BALLOON_CONFIG, *PVIRTIO_BALLOON_CONFIG;

typedef struct virtqueue VIOQUEUE, *PVIOQUEUE;
typedef struct VirtIOBufferDescriptor VIO_SG, *PVIO_SG;

#define __DRIVER_NAME "BALLOON: "

typedef struct
{
    SINGLE_LIST_ENTRY SingleListEntry;
    PMDL PageMdl;
} PAGE_LIST_ENTRY, *PPAGE_LIST_ENTRY;

typedef struct _DEVICE_CONTEXT
{
    WDFINTERRUPT WdfInterrupt;
    PUCHAR PortBase;
    ULONG PortCount;
    BOOLEAN PortMapped;
    BOOLEAN SurpriseRemoval;
#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
    PKEVENT evLowMem;
    HANDLE hLowMem;
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM
    VIRTIO_WDF_DRIVER VDevice;
    PVIOQUEUE InfVirtQueue;
    PVIOQUEUE DefVirtQueue;
    PVIOQUEUE StatVirtQueue;
    PVIOQUEUE RepVirtQueue;

    WDFSPINLOCK StatQueueLock;
    WDFSPINLOCK InfDefQueueLock;

    KEVENT HostAckEvent;

    volatile ULONG num_pages;
    ULONG num_pfns;
    PPFN_NUMBER pfns_table;
    NPAGED_LOOKASIDE_LIST LookAsideList;
    BOOLEAN bListInitialized;
    SINGLE_LIST_ENTRY PageListHead;
    PBALLOON_STAT MemStats;

    KEVENT WakeUpThread;
    PKTHREAD Thread;
    BOOLEAN bShutDown;

    /*
     * Free page reporting state. The held MDL list is shared by the
     * command thread (release decisions, see BalloonReportCheckRelease),
     * the reporting thread (hold cycles, see BalloonReportHold) and, after
     * both threads have been stopped, the power-management path (release),
     * so it is protected by ReportingLock; the MDL allocation and free
     * calls themselves run outside the lock at PASSIVE_LEVEL, only the
     * list and counter updates are guarded. The reporting virtqueue is
     * protected by InfDefQueueLock, like the inflate and deflate queues.
     */
    ULONG ReportingTotalPages;          /* NumberOfPhysicalPages, cached */
    ULONG ReportingMinFreePages;        /* watermark override from MinFreeMb, 0 = automatic */
    ULONG ReportingMinCommitPages;      /* commit reserve override from MinCommitMb, 0 = automatic */
    ULONG ReportingIntervalMs;          /* reporting cycle interval, from ReportIntervalMs */
    ULONG ReportingCooldownSec;         /* hold cooldown base from CooldownSec, 0 = built-in default */
    ULONGLONG ReportingCooldownUntil;   /* interrupt time until which holding is paused, 0 = none */
    ULONG ReportingCooldownMs;          /* current cooldown length, doubles on repeated low-memory */
    ULONG ReportingMaxSegments;         /* segments per report request */
    SINGLE_LIST_ENTRY ReportingMdlList; /* held PAGE_LIST_ENTRY chain */
    ULONG ReportingMdlCount;
    ULONG ReportingHeldPages;     /* pages currently held */
    ULONG ReportingReportedPages; /* pages reported so far (cumulative) */
    KEVENT RepAckEvent;           /* a report request was acknowledged by the host */
    PKTHREAD RepThread;           /* the low-priority reporting thread, NULL when FPR is off */
    WDFSPINLOCK ReportingLock;    /* guards the held MDL list and its counters */

#ifdef USE_BALLOON_SERVICE
    WDFREQUEST PendingWriteRequest;
    BOOLEAN HandleWriteRequest;
#else  // USE_BALLOON_SERVICE
    WDFWORKITEM StatWorkItem;
    LONG WorkCount;
#endif // USE_BALLOON_SERVICE

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

#define BALLOON_MGMT_POOL_TAG 'mtlB'

#ifndef _IRQL_requires_
#define _IRQL_requires_(level)
#endif

EVT_WDF_DRIVER_DEVICE_ADD BalloonDeviceAdd;
KSTART_ROUTINE BalloonRoutine;
DRIVER_INITIALIZE DriverEntry;

// Context cleanup callbacks generally run at IRQL <= DISPATCH_LEVEL but
// WDFDRIVER and WDFDEVICE cleanup is guaranteed to run at PASSIVE_LEVEL.
// Annotate the prototypes to make static analysis happy.
EVT_WDF_OBJECT_CONTEXT_CLEANUP _IRQL_requires_(PASSIVE_LEVEL) EvtDriverContextCleanup;
EVT_WDF_DEVICE_CONTEXT_CLEANUP _IRQL_requires_(PASSIVE_LEVEL) BalloonEvtDeviceContextCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE BalloonEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE BalloonEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY BalloonEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT BalloonEvtDeviceD0Exit;
EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED BalloonEvtDeviceD0ExitPreInterruptsDisabled;
EVT_WDF_DEVICE_SURPRISE_REMOVAL BalloonEvtDeviceSurpriseRemoval;
EVT_WDF_INTERRUPT_ISR BalloonInterruptIsr;
EVT_WDF_INTERRUPT_DPC BalloonInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE BalloonInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE BalloonInterruptDisable;
#ifdef USE_BALLOON_SERVICE
EVT_WDF_FILE_CLOSE BalloonEvtFileClose;
#else  // USE_BALLOON_SERVICE
EVT_WDF_WORKITEM StatWorkItemWorker;
#endif // USE_BALLOON_SERVICE

VOID BalloonInterruptDpc(IN WDFINTERRUPT WdfInterrupt, IN WDFOBJECT WdfDevice);

BOOLEAN
BalloonInterruptIsr(IN WDFINTERRUPT Interrupt, IN ULONG MessageID);

NTSTATUS
BalloonInterruptEnable(IN WDFINTERRUPT WdfInterrupt, IN WDFDEVICE WdfDevice);

NTSTATUS
BalloonInterruptDisable(IN WDFINTERRUPT WdfInterrupt, IN WDFDEVICE WdfDevice);

NTSTATUS
BalloonInit(IN WDFOBJECT WdfDevice);

VOID BalloonTerm(IN WDFOBJECT WdfDevice);

NTSTATUS
BalloonFill(IN WDFOBJECT WdfDevice, IN size_t num);

NTSTATUS
BalloonLeak(IN WDFOBJECT WdfDevice, IN size_t num);

VOID BalloonMemStats(IN WDFOBJECT WdfDevice);

NTSTATUS
BalloonTellHost(IN WDFOBJECT WdfDevice, IN PVIOQUEUE vq);

__inline VOID EnableInterrupt(IN WDFINTERRUPT WdfInterrupt, IN WDFCONTEXT Context)
{
    PDEVICE_CONTEXT devCtx = (PDEVICE_CONTEXT)Context;
    UNREFERENCED_PARAMETER(WdfInterrupt);

    virtqueue_enable_cb(devCtx->InfVirtQueue);
    virtqueue_kick(devCtx->InfVirtQueue);
    virtqueue_enable_cb(devCtx->DefVirtQueue);
    virtqueue_kick(devCtx->DefVirtQueue);

    if (devCtx->StatVirtQueue)
    {
        virtqueue_enable_cb(devCtx->StatVirtQueue);
        virtqueue_kick(devCtx->StatVirtQueue);
    }
}

__inline VOID DisableInterrupt(IN PDEVICE_CONTEXT devCtx)
{
    virtqueue_disable_cb(devCtx->InfVirtQueue);
    virtqueue_disable_cb(devCtx->DefVirtQueue);
    if (devCtx->StatVirtQueue)
    {
        virtqueue_disable_cb(devCtx->StatVirtQueue);
    }
}

VOID BalloonSetSize(IN WDFOBJECT WdfDevice, IN size_t num);

LONGLONG
BalloonGetSize(IN WDFOBJECT WdfDevice);

NTSTATUS
BalloonCloseWorkerThread(IN WDFDEVICE Device);

NTSTATUS
BalloonCreateReportingThread(IN WDFDEVICE Device);

NTSTATUS
BalloonCloseReportingThread(IN WDFDEVICE Device);

VOID BalloonRoutine(IN PVOID pContext);

VOID BalloonReportRoutine(IN PVOID pContext);

#ifndef BALLOON_INFLATE_IGNORE_LOWMEM
__inline BOOLEAN IsLowMemory(IN WDFOBJECT WdfDevice)
{
    LARGE_INTEGER TimeOut = {0};
    PDEVICE_CONTEXT devCtx = GetDeviceContext(WdfDevice);

    if (devCtx->evLowMem)
    {
        return (STATUS_WAIT_0 == KeWaitForSingleObject(devCtx->evLowMem, Executive, KernelMode, FALSE, &TimeOut));
    }
    return FALSE;
}
#endif // !BALLOON_INFLATE_IGNORE_LOWMEM

/*
 * Free page reporting (VIRTIO_BALLOON_F_PAGE_REPORTING) routines.
 *
 * The command thread (BalloonRoutine) makes the release decisions and
 * never runs hold work; the reporting thread (BalloonReportRoutine)
 * runs one hold cycle per reporting interval at low priority.
 */
BOOLEAN
ReportingIsEnabled(IN WDFDEVICE Device);

NTSTATUS
BalloonReportInitialize(IN WDFDEVICE Device);

VOID BalloonReportReleaseAll(IN WDFOBJECT WdfDevice);

/* release decisions, runs on the command thread */
VOID BalloonReportCheckRelease(IN WDFOBJECT WdfDevice);

/* one hold cycle, runs on the low-priority reporting thread */
VOID BalloonReportHold(IN WDFOBJECT WdfDevice);

#ifdef USE_BALLOON_SERVICE
NTSTATUS
BalloonQueueInitialize(IN WDFDEVICE hDevice);
#else  // USE_BALLOON_SERVICE
NTSTATUS
StatInitializeWorkItem(IN WDFDEVICE Device);
#endif // USE_BALLOON_SERVICE

#endif // _PROTOTYPES_H_
