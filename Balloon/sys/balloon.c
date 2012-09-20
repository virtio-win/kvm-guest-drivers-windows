/**********************************************************************
 * Copyright (c) 2009  Red Hat, Inc.
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

NTSTATUS
BalloonInit(
    IN WDFOBJECT    WdfDevice
            )
{

    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    VIO_SG              sg;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> BalloonInit\n");

    VirtIODeviceReset(&devCtx->VDevice);

    VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_DRIVER);

    do
    {
        devCtx->InfVirtQueue = FindVirtualQueue(&devCtx->VDevice, 0);
        if (NULL == devCtx->InfVirtQueue)
        {
           status = STATUS_INSUFFICIENT_RESOURCES;
           break;
        }

        devCtx->DefVirtQueue = FindVirtualQueue(&devCtx->VDevice, 1);
        if (NULL == devCtx->DefVirtQueue)
        {
           status = STATUS_INSUFFICIENT_RESOURCES;
           break;
        }

        if(VirtIODeviceGetHostFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ))
        {
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> VIRTIO_BALLOON_F_STATS_VQ\n");
		devCtx->StatVirtQueue = FindVirtualQueue(&devCtx->VDevice, 2);
           if(NULL == devCtx->StatVirtQueue)
           {
              status = STATUS_INSUFFICIENT_RESOURCES;
              break;
           }
           sg.physAddr = MmGetPhysicalAddress(devCtx->MemStats);
           sg.ulSize = sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

           if(devCtx->StatVirtQueue->vq_ops->add_buf(devCtx->StatVirtQueue, &sg, 1, 0, devCtx, NULL, 0) < 0)
           {
              TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
           }
           devCtx->StatVirtQueue->vq_ops->kick(devCtx->StatVirtQueue);

           if(devCtx->bServiceConnected)
           {
              VirtIODeviceEnableGuestFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ);
           }
        }
    } while(FALSE);

    if(NT_SUCCESS(status)) {
        VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_DRIVER_OK);
    } else {
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
    PMDL                pPageMdl;
    PHYSICAL_ADDRESS    LowAddress;
    PHYSICAL_ADDRESS    HighAddress;
    PPAGE_LIST_ENTRY    pNewPageListEntry;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    ULONG               pages_per_request = PAGE_SIZE/sizeof(PFN_NUMBER);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = (ULONGLONG)-1;

    num = min(num, pages_per_request);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> BalloonFill num = %d\n", num);

    for (devCtx->num_pfns = 0; devCtx->num_pfns < num; devCtx->num_pfns++)
    {
        if(IsLowMemory(WdfDevice))
        {
           TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
                "LowMemoryCondition event was set to signaled,allocations stops, BalPageCount=%d\n", devCtx->num_pages);
           break;
        }
        pPageMdl = MmAllocatePagesForMdl(
                                        LowAddress,
                                        HighAddress,
                                        LowAddress,
                                        PAGE_SIZE
                                        );
        if (pPageMdl == NULL)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
                 "Balloon MDL Page Allocation Failed!!!, BalPageCount=%d\n", devCtx->num_pages);
            break;
        }

        if (MmGetMdlByteCount(pPageMdl) != PAGE_SIZE)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
                 "Balloon MDL Page Allocation < PAGE_SIZE =%d, Failed!!!, BalPageCount=%d\n",MmGetMdlByteCount(pPageMdl), devCtx->num_pages);
            MmFreePagesFromMdl(pPageMdl);
            ExFreePool(pPageMdl);
            break;
        }

        pNewPageListEntry = ExAllocateFromNPagedLookasideList(&devCtx->LookAsideList);
        if (pNewPageListEntry == NULL)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "List Entry Allocation Failed!!!\n");
            MmFreePagesFromMdl(pPageMdl);
            ExFreePool(pPageMdl);
            break;
        }

        pNewPageListEntry->PageMdl = pPageMdl;
        pNewPageListEntry->PagePfn = devCtx->pfns_table[devCtx->num_pfns] = *MmGetMdlPfnArray(pPageMdl);

        PushEntryList(&devCtx->PageListHead, &(pNewPageListEntry->SingleListEntry));
        devCtx->num_pages++;
    }

    if (devCtx->num_pfns > 0)
    {
        BalloonTellHost(WdfDevice, devCtx->InfVirtQueue);
    }
}

VOID
BalloonLeak(
    IN WDFOBJECT WdfDevice,
    IN size_t num
    )
{
    size_t              i;
    PPAGE_LIST_ENTRY    pPageListEntry;
    PMDL                pPageMdl;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    num = min(num, PAGE_SIZE/sizeof(PFN_NUMBER));

    pPageListEntry = (PPAGE_LIST_ENTRY)&(devCtx->PageListHead).Next;

    for (devCtx->num_pfns = 0; devCtx->num_pfns < num; devCtx->num_pfns++)
    {
        if (pPageListEntry == NULL)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "PopEntryList=NULL\n");
            break;
        }
        devCtx->pfns_table[devCtx->num_pfns] = pPageListEntry->PagePfn;
        pPageListEntry = (PPAGE_LIST_ENTRY)pPageListEntry->SingleListEntry.Next;
    }

    for (i = 0; i < devCtx->num_pfns; i++)
    {

        pPageListEntry = (PPAGE_LIST_ENTRY)PopEntryList(&devCtx->PageListHead);

        if (pPageListEntry == NULL)
        {
           TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "PopEntryList=NULL\n");
           break;
        }
        devCtx->num_pages--;

        pPageMdl = pPageListEntry->PageMdl;
        MmFreePagesFromMdl(pPageMdl);
        ExFreePool(pPageMdl);

        ExFreeToNPagedLookasideList(
                                &devCtx->LookAsideList,
                                pPageListEntry
                                );
    }

    BalloonTellHost(WdfDevice, devCtx->DefVirtQueue);
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
    sg.ulSize = sizeof(devCtx->pfns_table[0]) * devCtx->num_pfns;

    if(vq->vq_ops->add_buf(vq, &sg, 1, 0, devCtx, NULL, 0) < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
        return;
    }
    vq->vq_ops->kick(vq);

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
    PVOID p;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> BalloonTerm\n");

    VirtIODeviceRemoveStatus(&devCtx->VDevice , VIRTIO_CONFIG_S_DRIVER_OK);

    if(devCtx->DefVirtQueue)
    {
        devCtx->DefVirtQueue->vq_ops->shutdown(devCtx->DefVirtQueue);
        VirtIODeviceDeleteQueue(devCtx->DefVirtQueue, &p);
		MmFreeContiguousMemory(p);
		devCtx->DefVirtQueue = NULL;
    }

    if(devCtx->InfVirtQueue)
    {
        devCtx->InfVirtQueue->vq_ops->shutdown(devCtx->InfVirtQueue);
        VirtIODeviceDeleteQueue(devCtx->InfVirtQueue, &p);
		MmFreeContiguousMemory(p);
		devCtx->InfVirtQueue = NULL;
    }

    if(devCtx->StatVirtQueue)
    {
        devCtx->StatVirtQueue->vq_ops->shutdown(devCtx->StatVirtQueue);
        VirtIODeviceDeleteQueue(devCtx->StatVirtQueue, &p);
		MmFreeContiguousMemory(p);
		devCtx->StatVirtQueue = NULL;
    }

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
    WDFREQUEST request;
    NTSTATUS  status;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    sg.physAddr = MmGetPhysicalAddress(devCtx->MemStats);
    sg.ulSize = sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

    if(devCtx->StatVirtQueue->vq_ops->add_buf(devCtx->StatVirtQueue, &sg, 1, 0, devCtx, NULL, 0) < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
    }

    devCtx->StatVirtQueue->vq_ops->kick(devCtx->StatVirtQueue);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}
