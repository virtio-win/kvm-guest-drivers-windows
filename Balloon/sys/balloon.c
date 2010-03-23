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


#if (WINVER >= 0x0501)
#define LOMEMEVENTNAME L"\\KernelObjects\\LowMemoryCondition"
DECLARE_CONST_UNICODE_STRING(evLowMemString, LOMEMEVENTNAME);
#endif // (WINVER >= 0x0501)

NTSTATUS
BalloonInit(
            IN WDFOBJECT    WdfDevice
            )
{

    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    PDRIVER_CONTEXT     drvCtx = GetDriverContext(WdfGetDriver());
    VIO_SG              sg;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> BalloonInit\n");

    VirtIODeviceSetIOAddress(&devCtx->VDevice, (ULONG_PTR)devCtx->PortBase);

    VirtIODeviceReset(&devCtx->VDevice);

    VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_DRIVER);

    devCtx->InfVirtQueue = VirtIODeviceFindVirtualQueue(&devCtx->VDevice, 0, NULL);
    if (NULL == devCtx->InfVirtQueue) 
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto free_mem;
    }

    devCtx->DefVirtQueue = VirtIODeviceFindVirtualQueue(&devCtx->VDevice, 1, NULL);
    if (NULL == devCtx->DefVirtQueue) 
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto free_mem;
    }
    if(VirtIODeviceGetHostFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> VIRTIO_BALLOON_F_STATS_VQ\n");
        devCtx->StatVirtQueue = VirtIODeviceFindVirtualQueue(&devCtx->VDevice, 2, NULL);
        if(NULL == devCtx->StatVirtQueue)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto free_mem;
        }
        sg.physAddr = GetPhysicalAddress(drvCtx->MemStats);
        sg.ulSize = sizeof (BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

        if(devCtx->StatVirtQueue->vq_ops->add_buf(devCtx->StatVirtQueue, &sg, 1, 0, devCtx) != 0)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> Cannot add buffer\n");
        }

        devCtx->StatVirtQueue->vq_ops->kick(devCtx->StatVirtQueue);

        VirtIODeviceEnableGuestFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_STATS_VQ);
    }
    devCtx->bTellHostFirst
        = (BOOLEAN)VirtIODeviceGetHostFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_MUST_TELL_HOST); 

#if (WINVER >= 0x0501)
    devCtx->evLowMem = IoCreateNotificationEvent(
                               (PUNICODE_STRING )&evLowMemString,
                               &devCtx->hLowMem);
#endif // (WINVER >= 0x0501)

free_mem:
    KeMemoryBarrier();

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
BalloonTerm(
    IN WDFOBJECT    WdfDevice
    )
{
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    PDRIVER_CONTEXT     drvCtx = GetDriverContext(WdfGetDriver());

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> BalloonTerm\n");

    while(drvCtx->num_pages)
    {
        BalloonLeak(WdfDevice, drvCtx->num_pages);
    }

    SetBalloonSize(WdfDevice, drvCtx->num_pages); 
    VirtIODeviceRemoveStatus(&devCtx->VDevice , VIRTIO_CONFIG_S_DRIVER_OK);

    if(devCtx->DefVirtQueue) 
    {
        devCtx->DefVirtQueue->vq_ops->shutdown(devCtx->DefVirtQueue);
        VirtIODeviceDeleteVirtualQueue(devCtx->DefVirtQueue);
        devCtx->DefVirtQueue = NULL;
    }

    if(devCtx->InfVirtQueue) 
    {
        devCtx->InfVirtQueue->vq_ops->shutdown(devCtx->InfVirtQueue);
        VirtIODeviceDeleteVirtualQueue(devCtx->InfVirtQueue);
        devCtx->InfVirtQueue = NULL;
    }

    if(devCtx->StatVirtQueue) 
    {
        devCtx->StatVirtQueue->vq_ops->shutdown(devCtx->StatVirtQueue);
        VirtIODeviceDeleteVirtualQueue(devCtx->StatVirtQueue);
        devCtx->StatVirtQueue = NULL;
    }

    VirtIODeviceReset(&devCtx->VDevice);


#if (WINVER >= 0x0501)
    ZwClose(&devCtx->hLowMem);
#endif // (WINVER >= 0x0501)

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- BalloonTerm\n");
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
    PDRIVER_CONTEXT     drvCtx = GetDriverContext(WdfGetDriver());
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    ULONG               pages_per_request = PAGE_SIZE/sizeof(PFN_NUMBER); 

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> BalloonFill\n");

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = (ULONGLONG)-1;

    num = min(num, pages_per_request);
    for (drvCtx->num_pfns = 0; drvCtx->num_pfns < num; drvCtx->num_pfns++) 
    {
        if(IsLowMemory(WdfDevice))
        {
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, 
                "LowMemoryCondition event was set to signaled,allocations stops, BalPageCount=%d\n", drvCtx->num_pages);
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
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, 
                 "Balloon MDL Page Allocation Failed!!!, BalPageCount=%d\n", drvCtx->num_pages);
            break;
        }

        if (MmGetMdlByteCount(pPageMdl) != PAGE_SIZE)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, 
                 "Balloon MDL Page Allocation < PAGE_SIZE =%d, Failed!!!, BalPageCount=%d\n",MmGetMdlByteCount(pPageMdl), drvCtx->num_pages);
            MmFreePagesFromMdl(pPageMdl);
            ExFreePool(pPageMdl);
            break;
        }

        pNewPageListEntry = ExAllocateFromNPagedLookasideList(&drvCtx->LookAsideList);
        if (pNewPageListEntry == NULL)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "List Entry Allocation Failed!!!\n");
            MmFreePagesFromMdl(pPageMdl);
            ExFreePool(pPageMdl);
            break;  
        }
 
        pNewPageListEntry->PageMdl = pPageMdl;
        pNewPageListEntry->PagePfn = drvCtx->pfns_table[drvCtx->num_pfns] = *(PPFN_NUMBER)(pPageMdl + 1);
 
        WdfSpinLockAcquire(drvCtx->SpinLock);
        PushEntryList(&drvCtx->PageListHead, &(pNewPageListEntry->SingleListEntry));
        drvCtx->num_pages++;
        WdfSpinLockRelease(drvCtx->SpinLock);
    }

    if (drvCtx->num_pfns > 0)
    {
        BalloonTellHost(WdfDevice , devCtx->InfVirtQueue);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- BalloonFill\n");
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
    PDRIVER_CONTEXT     drvCtx = GetDriverContext(WdfGetDriver());

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> BalloonLeak\n");
    
    num = min(num, PAGE_SIZE/sizeof(PFN_NUMBER));

    WdfSpinLockAcquire(drvCtx->SpinLock);
    pPageListEntry = (PPAGE_LIST_ENTRY)&(drvCtx->PageListHead).Next; 

    for (drvCtx->num_pfns = 0; drvCtx->num_pfns < num; drvCtx->num_pfns++) 
    {
        if (pPageListEntry == NULL)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "PopEntryList=NULL\n");
            break;
        }
        drvCtx->pfns_table[drvCtx->num_pfns] = pPageListEntry->PagePfn;
        pPageListEntry = (PPAGE_LIST_ENTRY)pPageListEntry->SingleListEntry.Next;
    }
    WdfSpinLockRelease(drvCtx->SpinLock);
 
    if (devCtx->bTellHostFirst) 
    {
        BalloonTellHost(WdfDevice, devCtx->DefVirtQueue);
    }

    for (i = 0; i < drvCtx->num_pfns; i++) 
    {

        WdfSpinLockAcquire(drvCtx->SpinLock);
        pPageListEntry = (PPAGE_LIST_ENTRY)PopEntryList(&drvCtx->PageListHead);
        drvCtx->num_pages--;
        WdfSpinLockRelease(drvCtx->SpinLock);

        if (pPageListEntry == NULL)
        {
           TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "PopEntryList=NULL\n");
           break;
        }

        pPageMdl = pPageListEntry->PageMdl;
        MmFreePagesFromMdl(pPageMdl);
        ExFreePool(pPageMdl);

        ExFreeToNPagedLookasideList(
                                &drvCtx->LookAsideList,
                                pPageListEntry
                                );
    }

    if (!devCtx->bTellHostFirst) 
    {
        BalloonTellHost(WdfDevice, devCtx->DefVirtQueue);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- BalloonLeak\n");
}

VOID 
BalloonTellHost(
    IN WDFOBJECT WdfDevice, 
    IN PVIOQUEUE vq
    )
{
    VIO_SG              sg;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    PDRIVER_CONTEXT     drvCtx = GetDriverContext(WdfGetDriver());

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> BalloonTellHost\n");

    sg.physAddr = GetPhysicalAddress(drvCtx->pfns_table);
    sg.ulSize = sizeof(drvCtx->pfns_table[0]) * drvCtx->num_pfns;

    if(vq->vq_ops->add_buf(vq, &sg, 1, 0, devCtx) != 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> Cannot add buffer\n");
    }

    vq->vq_ops->kick(vq);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- BalloonTellHost\n");
}


VOID 
BalloonMemStats(
    IN WDFOBJECT WdfDevice
    )
{
    VIO_SG              sg;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    PDRIVER_CONTEXT     drvCtx = GetDriverContext(WdfGetDriver());
    WDFREQUEST request;
    NTSTATUS  status;
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);
    status = WdfIoQueueRetrieveNextRequest(devCtx->StatusQueue, &request);

    if (NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS,"got available request\n");
        WdfRequestComplete(request, STATUS_SUCCESS);
    } else {
        sg.physAddr = GetPhysicalAddress(drvCtx->MemStats);
        sg.ulSize = 0;

        if(devCtx->StatVirtQueue->vq_ops->add_buf(devCtx->StatVirtQueue, &sg, 1, 0, devCtx) != 0)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> Cannot add buffer\n");
        }

        devCtx->StatVirtQueue->vq_ops->kick(devCtx->StatVirtQueue);
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}


