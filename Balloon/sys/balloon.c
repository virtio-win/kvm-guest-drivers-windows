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

static PVIOQUEUE FindVirtualQueue(VIODEVICE *dev, ULONG index)
{
    PVIOQUEUE  pq = NULL;
    PVOID p;
    ULONG size, allocSize;
    VirtIODeviceQueryQueueAllocation(dev, index, &size, &allocSize);
    if (allocSize)
    {
        PHYSICAL_ADDRESS HighestAcceptable;
        HighestAcceptable.QuadPart = 0xFFFFFFFFFF;
        p = MmAllocateContiguousMemory(allocSize, HighestAcceptable);
        if (p)
        {
            pq = VirtIODevicePrepareQueue(dev, index, MmGetPhysicalAddress(p), p, allocSize, p, FALSE);
        }
    }
    return pq;
}

static void DeleteQueue(struct virtqueue **pvq)
{
    struct virtqueue *vq = *pvq;
    PVOID p;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Deleting queue %p\n", vq);

    if (vq != NULL)
    {
        VirtIODeviceDeleteQueue(vq, &p);
        MmFreeContiguousMemory(p);
        *pvq = NULL;
    }
}

NTSTATUS
BalloonInit(
    IN WDFOBJECT    WdfDevice
            )
{
    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    u32 hostFeatures;
    u32 guestFeatures = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> BalloonInit\n");

    VirtIODeviceReset(&devCtx->VDevice);

    VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_DRIVER);

    do
    {
        devCtx->InfVirtQueue = FindVirtualQueue(&devCtx->VDevice, 0);
        if (!devCtx->InfVirtQueue)
        {
           status = STATUS_INSUFFICIENT_RESOURCES;
           break;
        }

        devCtx->DefVirtQueue = FindVirtualQueue(&devCtx->VDevice, 1);
        if (!devCtx->DefVirtQueue)
        {
           status = STATUS_INSUFFICIENT_RESOURCES;
           break;
        }

        hostFeatures = VirtIODeviceReadHostFeatures(&devCtx->VDevice);

        if (VirtIOIsFeatureEnabled(hostFeatures, VIRTIO_BALLOON_F_STATS_VQ))
        {
           VIO_SG  sg;
           
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
               "Enable stats feature.\n");

           devCtx->StatVirtQueue = FindVirtualQueue(&devCtx->VDevice, 2);
           if (!devCtx->StatVirtQueue)
           {
              status = STATUS_INSUFFICIENT_RESOURCES;
              break;
           }

           sg.physAddr = MmGetPhysicalAddress(devCtx->MemStats);
           sg.length = sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

           if (virtqueue_add_buf(
                   devCtx->StatVirtQueue, &sg, 1, 0, devCtx, NULL, 0) >= 0)
           {
               virtqueue_kick(devCtx->StatVirtQueue);
               VirtIOFeatureEnable(guestFeatures, VIRTIO_BALLOON_F_STATS_VQ);
           }
           else
           {
               TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
                   "Failed to add buffer to stats queue.\n");
           }
        }
#pragma warning(suppress: 4127)
    } while(FALSE);

    VirtIODeviceWriteGuestFeatures(&devCtx->VDevice, guestFeatures);

    if(NT_SUCCESS(status))
    {
        VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_DRIVER_OK);
    }
    else
    {
        VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_FAILED);
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

    VirtIODeviceRemoveStatus(&devCtx->VDevice , VIRTIO_CONFIG_S_DRIVER_OK);

    DeleteQueue(&devCtx->DefVirtQueue);
    DeleteQueue(&devCtx->InfVirtQueue);
    DeleteQueue(&devCtx->StatVirtQueue);

    VirtIODeviceReset(&devCtx->VDevice);

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
