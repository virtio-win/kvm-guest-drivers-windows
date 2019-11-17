/*
 * This file contains balloon driver routines
 *
 * Copyright (c) 2009-2017  Red Hat, Inc.
 *
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@redhat.com>
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

    WdfObjectAcquireLock(WdfDevice);

    // inflate
    params[0].Interrupt = devCtx->WdfInterrupt;

    // deflate
    params[1].Interrupt = devCtx->WdfInterrupt;

    // stats
    params[2].Interrupt = devCtx->WdfInterrupt;

    u64HostFeatures = VirtIOWdfGetDeviceFeatures(&devCtx->VDevice);

    if (virtio_is_feature_enabled(u64HostFeatures, VIRTIO_BALLOON_F_STATS_VQ))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
            "Enable stats feature.\n");

        virtio_feature_enable(u64GuestFeatures, VIRTIO_BALLOON_F_STATS_VQ);
        nvqs = 3;
    }
    else
    {
        nvqs = 2;
    }

    status = VirtIOWdfSetDriverFeatures(&devCtx->VDevice, u64GuestFeatures, 0);
    if (NT_SUCCESS(status))
    {
        // initialize 2 or 3 queues
        status = VirtIOWdfInitQueues(&devCtx->VDevice, nvqs, vqs, params);
        if (NT_SUCCESS(status))
        {
            devCtx->InfVirtQueue = vqs[0];
            devCtx->DefVirtQueue = vqs[1];

            if (nvqs == 3)
            {
                VIO_SG  sg;

                devCtx->StatVirtQueue = vqs[2];

                sg.physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice, devCtx->MemStats);
                sg.length = sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

                if (virtqueue_add_buf(
                    devCtx->StatVirtQueue, &sg, 1, 0, devCtx, NULL, 0) >= 0)
                {
                    notify_stat_queue = true;
                }
                else
                {
                    TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
                        "Failed to add buffer to stats queue.\n");
                }
            }
            VirtIOWdfSetDriverOK(&devCtx->VDevice);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
                "VirtIOWdfInitQueues failed with %x\n", status);
            VirtIOWdfSetDriverFailed(&devCtx->VDevice);
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "VirtIOWdfSetDriverFeatures failed with %x\n", status);
        VirtIOWdfSetDriverFailed(&devCtx->VDevice);
    }

    // notify the stat queue only after the virtual device has been fully initialized
    if (notify_stat_queue)
    {
        virtqueue_kick(devCtx->StatVirtQueue);
    }

    WdfObjectReleaseLock(WdfDevice);

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
    bool                do_notify;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    sg.physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice, devCtx->pfns_table);
    sg.length = sizeof(devCtx->pfns_table[0]) * devCtx->num_pfns;

    WdfSpinLockAcquire(devCtx->InfDefQueueLock);
    if (virtqueue_add_buf(vq, &sg, 1, 0, devCtx, NULL, 0) < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
        WdfSpinLockRelease(devCtx->InfDefQueueLock);
        return;
    }
    do_notify = virtqueue_kick_prepare(vq);
    WdfSpinLockRelease(devCtx->InfDefQueueLock);

    if (do_notify)
    {
        virtqueue_notify(vq);
    }

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

    WdfObjectAcquireLock(WdfDevice);

    VirtIOWdfDestroyQueues(&devCtx->VDevice);
    devCtx->StatVirtQueue = NULL;

    WdfObjectReleaseLock(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- BalloonTerm\n");
}

VOID
BalloonMemStats(
    IN WDFOBJECT WdfDevice
    )
{
    VIO_SG              sg;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    bool                do_notify;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    sg.physAddr = VirtIOWdfDeviceGetPhysicalAddress(&devCtx->VDevice.VIODevice, devCtx->MemStats);
    sg.length = sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

    WdfSpinLockAcquire(devCtx->StatQueueLock);
    if (virtqueue_add_buf(devCtx->StatVirtQueue, &sg, 1, 0, devCtx, NULL, 0) < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
    }
    do_notify = virtqueue_kick_prepare(devCtx->StatVirtQueue);
    WdfSpinLockRelease(devCtx->StatQueueLock);

    if (do_notify)
    {
        virtqueue_notify(devCtx->StatVirtQueue);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}
