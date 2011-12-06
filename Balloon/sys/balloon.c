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
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
        }

        devCtx->StatVirtQueue->vq_ops->kick(devCtx->StatVirtQueue);

    }
    devCtx->bTellHostFirst
        = (BOOLEAN)VirtIODeviceGetHostFeature(&devCtx->VDevice, VIRTIO_BALLOON_F_MUST_TELL_HOST);

free_mem:
    KeMemoryBarrier();

    if(NT_SUCCESS(status))
    {
        LONGLONG  diff = GetBalloonSize(WdfDevice);
        VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_DRIVER_OK);

        if (diff != 0) {
           PWORKITEM_CONTEXT     context;
           WDF_OBJECT_ATTRIBUTES attributes;
           WDF_WORKITEM_CONFIG   workitemConfig;
           WDFWORKITEM           hWorkItem;
           WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
           WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, WORKITEM_CONTEXT);
           attributes.ParentObject = WdfDevice;

           WDF_WORKITEM_CONFIG_INIT(&workitemConfig, FillLeakWorkItem);

           status = WdfWorkItemCreate( &workitemConfig,
                                &attributes,
                                &hWorkItem);

           if (NT_SUCCESS(status)) {
              context = GetWorkItemContext(hWorkItem);

              context->Device = WdfDevice;
              context->Diff = GetBalloonSize(WdfDevice);

              context->bStatUpdate = FALSE;

              WdfWorkItemEnqueue(hWorkItem);

           }
           else
           {
              VirtIODeviceAddStatus(&devCtx->VDevice, VIRTIO_CONFIG_S_FAILED);
              TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "WdfWorkItemCreate failed with status = 0x%08x\n", status);
           }
        }
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

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = (ULONGLONG)-1;

    num = min(num, pages_per_request);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> BalloonFill num = %d\n", num);

    for (drvCtx->num_pfns = 0; drvCtx->num_pfns < num; drvCtx->num_pfns++)
    {
        if(IsLowMemory(WdfDevice))
        {
           TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
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
            TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
                 "Balloon MDL Page Allocation Failed!!!, BalPageCount=%d\n", drvCtx->num_pages);
            break;
        }

        if (MmGetMdlByteCount(pPageMdl) != PAGE_SIZE)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS,
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
        pNewPageListEntry->PagePfn = drvCtx->pfns_table[drvCtx->num_pfns] = *MmGetMdlPfnArray(pPageMdl);

        WdfSpinLockAcquire(drvCtx->SpinLock);
        PushEntryList(&drvCtx->PageListHead, &(pNewPageListEntry->SingleListEntry));
        drvCtx->num_pages++;
        WdfSpinLockRelease(drvCtx->SpinLock);
    }

    if (drvCtx->num_pfns > 0)
    {
        BalloonTellHost(WdfDevice , devCtx->InfVirtQueue, &drvCtx->InfEvent);
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
    PDRIVER_CONTEXT     drvCtx = GetDriverContext(WdfGetDriver());

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    num = min(num, PAGE_SIZE/sizeof(PFN_NUMBER));

    WdfSpinLockAcquire(drvCtx->SpinLock);
    pPageListEntry = (PPAGE_LIST_ENTRY)&(drvCtx->PageListHead).Next;

    for (drvCtx->num_pfns = 0; drvCtx->num_pfns < num; drvCtx->num_pfns++)
    {
        if (pPageListEntry == NULL)
        {
            TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "PopEntryList=NULL\n");
            break;
        }
        drvCtx->pfns_table[drvCtx->num_pfns] = pPageListEntry->PagePfn;
        pPageListEntry = (PPAGE_LIST_ENTRY)pPageListEntry->SingleListEntry.Next;
    }
    WdfSpinLockRelease(drvCtx->SpinLock);

    if (devCtx->bTellHostFirst)
    {
        BalloonTellHost(WdfDevice, devCtx->DefVirtQueue, &drvCtx->DefEvent);
    }

    for (i = 0; i < drvCtx->num_pfns; i++)
    {

        WdfSpinLockAcquire(drvCtx->SpinLock);
        pPageListEntry = (PPAGE_LIST_ENTRY)PopEntryList(&drvCtx->PageListHead);

        if (pPageListEntry == NULL)
        {
           TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "PopEntryList=NULL\n");
           WdfSpinLockRelease(drvCtx->SpinLock);
           break;
        }
        drvCtx->num_pages--;
        WdfSpinLockRelease(drvCtx->SpinLock);

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
        BalloonTellHost(WdfDevice, devCtx->DefVirtQueue, &drvCtx->DefEvent);
    }
}

VOID
BalloonTellHost(
    IN WDFOBJECT WdfDevice,
    IN PVIOQUEUE vq,
    IN PVOID     ev
    )
{
    VIO_SG              sg;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(WdfDevice);
    PDRIVER_CONTEXT     drvCtx = GetDriverContext(WdfGetDriver());
    NTSTATUS            status;
    LARGE_INTEGER       timeout = {0};

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    sg.physAddr = GetPhysicalAddress(drvCtx->pfns_table);
    sg.ulSize = sizeof(drvCtx->pfns_table[0]) * drvCtx->num_pfns;

    if(vq->vq_ops->add_buf(vq, &sg, 1, 0, devCtx) != 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
        return;
    }
    vq->vq_ops->kick(vq);

    timeout.QuadPart = Int32x32To64(1000, -10000);
    status = KeWaitForSingleObject (
                ev,
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

    sg.physAddr = GetPhysicalAddress(drvCtx->MemStats);
    sg.ulSize = sizeof(BALLOON_STAT) * VIRTIO_BALLOON_S_NR;

    if(devCtx->StatVirtQueue->vq_ops->add_buf(devCtx->StatVirtQueue, &sg, 1, 0, devCtx) != 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
    }

    devCtx->StatVirtQueue->vq_ops->kick(devCtx->StatVirtQueue);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
}


