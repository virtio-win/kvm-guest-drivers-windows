/**********************************************************************
 * Copyright (c) 2009-2016  Red Hat, Inc.
 *
 * File: balloon.c
 *
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This file contains balloon driver routines
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "precomp.h"

#if defined(EVENT_TRACING)
#include "balloon.tmh"
#endif

NTSTATUS
BalloonInit(
    IN WDFOBJECT    WdfDevice
            )
{
    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    u64 u64HostFeatures;
    u64 u64GuestFeatures = 0;
    bool notify_stat_queue = false;
    VIRTIO_WDF_QUEUE_PARAM params[3];
    PVIOQUEUE vqs[3];
    ULONG nvqs;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> BalloonInit\n");

    params[0].bEnableInterruptSuppression = false;
    params[0].Interrupt = devCtx->WdfInterrupt;
    params[0].szName = "inflate";

    params[1].bEnableInterruptSuppression = false;
    params[1].Interrupt = devCtx->WdfInterrupt;
    params[1].szName = "deflate";

    params[2].bEnableInterruptSuppression = false;
    params[2].Interrupt = devCtx->WdfInterrupt;
    params[2].szName = "stats";

    u64HostFeatures = VirtIOWdfGetDeviceFeatures(&devCtx->VDevice);

    // initialize 2 or 3 queues
    nvqs = VirtIOIsFeatureEnabled(u64HostFeatures, VIRTIO_BALLOON_F_STATS_VQ) ? 3 : 2;
    status = VirtIOWdfInitQueues(&devCtx->VDevice, nvqs, vqs, params);
    if (NT_SUCCESS(status))
    {
        devCtx->InfVirtQueue = vqs[0];
        devCtx->DefVirtQueue = vqs[1];

        if (nvqs == 3)
        {
            VIO_SG  sg;

            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                "Enable stats feature.\n");

            devCtx->StatVirtQueue = vqs[2];

            sg.physAddr = MmGetPhysicalAddress(devCtx->MemStats);
            sg.length = sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

            if (virtqueue_add_buf(
                    devCtx->StatVirtQueue, &sg, 1, 0, devCtx, NULL, 0) >= 0)
            {
                VirtIOFeatureEnable(u64GuestFeatures, VIRTIO_BALLOON_F_STATS_VQ);
                notify_stat_queue = true;
            }
            else
            {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
                    "Failed to add buffer to stats queue.\n");
            }
        }

        status = VirtIOWdfSetDriverFeatures(&devCtx->VDevice, u64GuestFeatures);
        if (NT_SUCCESS(status))
        {
            VirtIOWdfSetDriverOK(&devCtx->VDevice);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
                "VirtIOWdfSetDriverFeatures failed with %x\n", status);

            VirtIOWdfDestroyQueues(&devCtx->VDevice);
            VirtIOWdfSetDriverFailed(&devCtx->VDevice);
            notify_stat_queue = false;
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "VirtIOWdfInitQueues failed with %x\n", status);
    }

    // notify the stat queue only after the virtual device has been fully initialized
    if (notify_stat_queue)
    {
        virtqueue_kick(devCtx->StatVirtQueue);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- BalloonInit\n");
    return status;
}

VOID
BalloonFill(
    IN WDFOBJECT WdfDevice,
    IN size_t num)
{
    PDEVICE_CONTEXT ctx = GetDeviceContext(WdfDevice);
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS SkipBytes;
    PPAGE_LIST_ENTRY pNewPageListEntry;
    PMDL pPageMdl;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    ctx->num_pfns = 0;

    if (IsLowMemory(WdfDevice))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
            "Low memory. Allocated pages: %d\n", ctx->num_pages);
        return;
    }

    num = min(num, PAGE_SIZE / sizeof(PFN_NUMBER));
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS,
        "Inflate balloon with %d pages.\n", num);

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = (ULONGLONG)-1;
    SkipBytes.QuadPart = 0;

#if (NTDDI_VERSION < NTDDI_WS03SP1)
    pPageMdl = MmAllocatePagesForMdl(LowAddress, HighAddress, SkipBytes,
        num * PAGE_SIZE);
#else
    pPageMdl = MmAllocatePagesForMdlEx(LowAddress, HighAddress, SkipBytes,
        num * PAGE_SIZE, MmNonCached, MM_DONT_ZERO_ALLOCATION);
#endif

    if (pPageMdl == NULL)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
            "Failed to allocate pages.\n");
        return;
    }

    if (MmGetMdlByteCount(pPageMdl) != (num * PAGE_SIZE))
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
            "Not all requested memory was allocated (%d/%d).\n",
            MmGetMdlByteCount(pPageMdl), num * PAGE_SIZE);
        MmFreePagesFromMdl(pPageMdl);
        ExFreePool(pPageMdl);
        return;
    }

    pNewPageListEntry = (PPAGE_LIST_ENTRY)ExAllocateFromNPagedLookasideList(
        &ctx->LookAsideList);

    if (pNewPageListEntry == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Failed to allocate list entry.\n");
        MmFreePagesFromMdl(pPageMdl);
        ExFreePool(pPageMdl);
        return;
    }

    pNewPageListEntry->PageMdl = pPageMdl;
    PushEntryList(&ctx->PageListHead, &(pNewPageListEntry->SingleListEntry));

    ctx->num_pfns = (ULONG)num;
    ctx->num_pages += ctx->num_pfns;

    RtlCopyMemory(ctx->pfns_table, MmGetMdlPfnArray(pPageMdl),
        ctx->num_pfns * sizeof(PFN_NUMBER));

    BalloonTellHost(WdfDevice, ctx->InfVirtQueue);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

VOID
BalloonLeak(
    IN WDFOBJECT WdfDevice,
    IN size_t num
    )
{
    PDEVICE_CONTEXT ctx = GetDeviceContext(WdfDevice);
    PPAGE_LIST_ENTRY pPageListEntry;
    PMDL pPageMdl;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    pPageListEntry = (PPAGE_LIST_ENTRY)PopEntryList(&ctx->PageListHead);
    if (pPageListEntry == NULL)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "No list entries.\n");
        return;
    }

    pPageMdl = pPageListEntry->PageMdl;

    num = MmGetMdlByteCount(pPageMdl) / PAGE_SIZE;
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS,
        "Deflate balloon with %d pages.\n", num);

    ctx->num_pfns = (ULONG)num;
    ctx->num_pages -= ctx->num_pfns;

    RtlCopyMemory(ctx->pfns_table, MmGetMdlPfnArray(pPageMdl),
        ctx->num_pfns * sizeof(PFN_NUMBER));

    MmFreePagesFromMdl(pPageMdl);
    ExFreePool(pPageMdl);
    ExFreeToNPagedLookasideList(&ctx->LookAsideList, pPageListEntry);

    BalloonTellHost(WdfDevice, ctx->DefVirtQueue);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}

VOID
BalloonTellHost(
    IN WDFOBJECT WdfDevice,
    IN PVIOQUEUE vq
    )
{
    VIO_SG              sg;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    NTSTATUS            status;
    LARGE_INTEGER       timeout = {0};

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    sg.physAddr = MmGetPhysicalAddress(devCtx->pfns_table);
    sg.length = sizeof(devCtx->pfns_table[0]) * devCtx->num_pfns;

    if (virtqueue_add_buf(vq, &sg, 1, 0, devCtx, NULL, 0) < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
        return;
    }
    virtqueue_kick(vq);

    timeout.QuadPart = Int32x32To64(1000, -10000);
    status = KeWaitForSingleObject (
                &devCtx->HostAckEvent,
                Executive,
                KernelMode,
                FALSE,
                &timeout);
    ASSERT(NT_SUCCESS(status));
    if(STATUS_TIMEOUT == status)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "<--> TimeOut\n");
    }
}


VOID
BalloonTerm(
    IN WDFOBJECT    WdfDevice
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> BalloonTerm\n");

    VirtIOWdfDestroyQueues(&devCtx->VDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- BalloonTerm\n");
}

VOID
BalloonMemStats(
    IN WDFOBJECT WdfDevice
    )
{
    VIO_SG              sg;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    sg.physAddr = MmGetPhysicalAddress(devCtx->MemStats);
    sg.length = sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

    if (virtqueue_add_buf(devCtx->StatVirtQueue, &sg, 1, 0, devCtx, NULL, 0) < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
    }

    virtqueue_kick(devCtx->StatVirtQueue);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}
