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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> BalloonInit\n");

    VirtIODeviceReset(&devCtx->VDevice);

    VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_DRIVER);

    do
    {
        if (!devCtx->InfVirtQueue)
        {
           devCtx->InfVirtQueue = FindVirtualQueue(&devCtx->VDevice, 0);
        }
        else
        {
           VirtIODeviceRenewQueue(devCtx->InfVirtQueue);
        }
        if (!devCtx->InfVirtQueue)
        {
           status = STATUS_INSUFFICIENT_RESOURCES;
           break;
        }

        if (!devCtx->DefVirtQueue)
        {
           devCtx->DefVirtQueue = FindVirtualQueue(&devCtx->VDevice, 1);
        }
        else
        {
           VirtIODeviceRenewQueue(devCtx->DefVirtQueue);
        }
        if (!devCtx->DefVirtQueue)
        {
           status = STATUS_INSUFFICIENT_RESOURCES;
           break;
        }

        devCtx->GuestFeatures = 0;
        devCtx->HostFeatures = VirtIODeviceReadHostFeatures(&devCtx->VDevice);

        if(VirtIOIsFeatureEnabled(devCtx->HostFeatures, VIRTIO_BALLOON_F_STATS_VQ))
        {
           BOOLEAN bNeedBuffer = FALSE;
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> VIRTIO_BALLOON_F_STATS_VQ\n");
           
           if(!devCtx->StatVirtQueue)
           {
              devCtx->StatVirtQueue = FindVirtualQueue(&devCtx->VDevice, 2);
              bNeedBuffer = TRUE;
           }
           else
           {
              VirtIODeviceRenewQueue(devCtx->StatVirtQueue);
           }
           if(!devCtx->StatVirtQueue)
           {
              status = STATUS_INSUFFICIENT_RESOURCES;
              break;
           }
           if (bNeedBuffer)
           {
              VIO_SG  sg;
              sg.physAddr = MmGetPhysicalAddress(devCtx->MemStats);
              sg.ulSize = sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

              if(devCtx->StatVirtQueue->vq_ops->add_buf(devCtx->StatVirtQueue, &sg, 1, 0, devCtx, NULL, 0) < 0)
              {
                 TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
              }
           }
           devCtx->StatVirtQueue->vq_ops->kick(devCtx->StatVirtQueue);

           if(devCtx->bServiceConnected)
           {
              VirtIOFeatureEnable(devCtx->GuestFeatures, VIRTIO_BALLOON_F_STATS_VQ);
           }
        }
    } while(FALSE);

    VirtIODeviceWriteGuestFeatures(&devCtx->VDevice, devCtx->GuestFeatures);

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

        pNewPageListEntry = (PPAGE_LIST_ENTRY)ExAllocateFromNPagedLookasideList(&devCtx->LookAsideList);
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
    PPAGE_LIST_ENTRY    pPageListEntry;
    PMDL                pPageMdl;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    num = min(num, PAGE_SIZE/sizeof(PFN_NUMBER));

    for (devCtx->num_pfns = 0; devCtx->num_pfns < num; devCtx->num_pfns++)
    {

        pPageListEntry = (PPAGE_LIST_ENTRY)PopEntryList(&devCtx->PageListHead);

        if (pPageListEntry == NULL)
        {
           TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "PopEntryList=NULL\n");
           break;
        }

        devCtx->pfns_table[devCtx->num_pfns] = pPageListEntry->PagePfn;
        pPageMdl = pPageListEntry->PageMdl;
        MmFreePagesFromMdl(pPageMdl);
        ExFreePool(pPageMdl);

        ExFreeToNPagedLookasideList(
                                &devCtx->LookAsideList,
                                pPageListEntry
                                );
        devCtx->num_pages--;
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
    IN WDFOBJECT    WdfDevice,
    IN BOOLEAN      bFinal
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    PVOID p;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> BalloonTerm\n");

    VirtIODeviceRemoveStatus(&devCtx->VDevice , VIRTIO_CONFIG_S_DRIVER_OK);

    if(devCtx->DefVirtQueue)
    {
        devCtx->DefVirtQueue->vq_ops->shutdown(devCtx->DefVirtQueue);
        if (bFinal)
        {
           VirtIODeviceDeleteQueue(devCtx->DefVirtQueue, &p);
           MmFreeContiguousMemory(p);
           devCtx->DefVirtQueue = NULL;
        }
    }

    if(devCtx->InfVirtQueue)
    {
        devCtx->InfVirtQueue->vq_ops->shutdown(devCtx->InfVirtQueue);
        if (bFinal)
        {
           VirtIODeviceDeleteQueue(devCtx->InfVirtQueue, &p);
           MmFreeContiguousMemory(p);
           devCtx->InfVirtQueue = NULL;
        }
    }

    if(devCtx->StatVirtQueue)
    {
        devCtx->StatVirtQueue->vq_ops->shutdown(devCtx->StatVirtQueue);
        if (bFinal)
        {
           VirtIODeviceDeleteQueue(devCtx->StatVirtQueue, &p);
           MmFreeContiguousMemory(p);
           devCtx->StatVirtQueue = NULL;
        }
    }

    if (bFinal)
    {
       VirtIODeviceReset(&devCtx->VDevice);
    }
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
    sg.ulSize = sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

    if(devCtx->StatVirtQueue->vq_ops->add_buf(devCtx->StatVirtQueue, &sg, 1, 0, devCtx, NULL, 0) < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
    }

    devCtx->StatVirtQueue->vq_ops->kick(devCtx->StatVirtQueue);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}
