/**********************************************************************
 * Copyright (c) 2016  Red Hat, Inc.
 *
 * File: PCI.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * PCI config space & resources helper routines
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "VirtioWDF.h"
#include "private.h"

NTSTATUS PCIAllocBars(WDFCMRESLIST ResourcesTranslated,
                      PVIRTIO_WDF_DRIVER pWdfDriver)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDescriptor;
    ULONG nInterrupts = 0, nMSIInterrupts = 0;
    int nListSize = WdfCmResourceListGetCount(ResourcesTranslated);
    int i;
    PVIRTIO_WDF_BAR pBar;
    PCI_COMMON_HEADER PCIHeader;

    /* read the PCI config header */
    if (pWdfDriver->PCIBus.GetBusData(
        pWdfDriver->PCIBus.Context,
        PCI_WHICHSPACE_CONFIG,
        &PCIHeader,
        0,
        sizeof(PCIHeader)) != sizeof(PCIHeader)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    
    for (i = 0; i < nListSize; i++) {
        pResDescriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if (pResDescriptor) {
            switch (pResDescriptor->Type) {
                case CmResourceTypePort:
                case CmResourceTypeMemory:
                    pBar = (PVIRTIO_WDF_BAR)ExAllocatePoolWithTag(
                        NonPagedPool,
                        sizeof(VIRTIO_WDF_BAR),
                        pWdfDriver->MemoryTag);
                    if (pBar == NULL) {
                        /* undo what we've done so far */
                        PCIFreeBars(pWdfDriver);
                        return STATUS_INSUFFICIENT_RESOURCES;
                    }

                    /* unfortunately WDF doesn't tell us BAR indices */
                    pBar->iBar = virtio_get_bar_index(&PCIHeader, pResDescriptor->u.Memory.Start);
                    if (pBar->iBar < 0) {
                        /* undo what we've done so far */
                        PCIFreeBars(pWdfDriver);
                        return STATUS_NOT_FOUND;
                    }

                    pBar->bPortSpace = !!(pResDescriptor->Flags & CM_RESOURCE_PORT_IO);
                    pBar->BasePA = pResDescriptor->u.Memory.Start;
                    pBar->uLength = pResDescriptor->u.Memory.Length;

                    if (pBar->bPortSpace) {
                        pBar->pBase = (PVOID)(ULONG_PTR)pBar->BasePA.QuadPart;
                    } else {
                        /* memory regions are mapped into the virtual memory space on demand */
                        pBar->pBase = NULL;
                    }
                    PushEntryList(&pWdfDriver->PCIBars, &pBar->ListEntry);
                    break;

                case CmResourceTypeInterrupt:
                    nInterrupts++;
                    if (pResDescriptor->Flags &
                        (CM_RESOURCE_INTERRUPT_LATCHED | CM_RESOURCE_INTERRUPT_MESSAGE)) {
                        nMSIInterrupts++;
                    }
                    break;
            }
        }
    }

    pWdfDriver->nInterrupts = nInterrupts;
    pWdfDriver->nMSIInterrupts = nMSIInterrupts;

    return STATUS_SUCCESS;
}

void PCIFreeBars(PVIRTIO_WDF_DRIVER pWdfDriver)
{
    PSINGLE_LIST_ENTRY iter;

    /* unmap IO space and free our BAR descriptors */
    while (iter = PopEntryList(&pWdfDriver->PCIBars)) {
        PVIRTIO_WDF_BAR pBar = CONTAINING_RECORD(iter, VIRTIO_WDF_BAR, ListEntry);
        if (pBar->pBase != NULL && !pBar->bPortSpace) {
            MmUnmapIoSpace(pBar->pBase, pBar->uLength);
        }
        ExFreePoolWithTag(pBar, pWdfDriver->MemoryTag);
    }
}

int PCIReadConfig(PVIRTIO_WDF_DRIVER pWdfDriver,
                  int where,
                  void *buffer,
                  size_t length)
{
    ULONG read;

    read = pWdfDriver->PCIBus.GetBusData(
        pWdfDriver->PCIBus.Context,
        PCI_WHICHSPACE_CONFIG,
        buffer,
        where,
        (ULONG)length);
    return (read == length ? 0 : -1);
}
