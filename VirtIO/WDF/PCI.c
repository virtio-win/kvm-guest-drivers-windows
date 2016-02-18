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

static int PCIGetBarIndex(
    int iLastIndex,
    PPCI_COMMON_HEADER pPCIHeader,
    PHYSICAL_ADDRESS BasePA)
{
    int iBar, i;

    /* no point in supporting PCI and CardBus bridges */
    ASSERT(pPCIHeader->HeaderType & ~PCI_MULTIFUNCTION == PCI_DEVICE_TYPE);

    for (i = iLastIndex + 1; i < PCI_TYPE0_ADDRESSES; i++) {
        PHYSICAL_ADDRESS BAR;
        BAR.LowPart = pPCIHeader->u.type0.BaseAddresses[i];

        iBar = i;
        if (BAR.LowPart & 0x01) {
            /* I/O space */
            BAR.LowPart &= 0xFFFFFFFC;
            BAR.HighPart = 0;
        }
        else if ((BAR.LowPart & 0x06) == 0x04) {
            /* memory space 64-bit */
            BAR.LowPart &= 0xFFFFFFF0;
            BAR.HighPart = pPCIHeader->u.type0.BaseAddresses[++i];
        }
        else {
            /* memory space 32-bit */
            BAR.LowPart &= 0xFFFFFFF0;
            BAR.HighPart = 0;
        }

        if (BAR.QuadPart == BasePA.QuadPart) {
            return iBar;
        }
    }

    ASSERT(!"BAR index not found in PCI config!");

    /* best guess */
    return iLastIndex + 1;
}

NTSTATUS PCIAllocBars(WDFCMRESLIST ResourcesTranslated,
                      PVIRTIO_WDF_DRIVER pWdfDriver)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDescriptor;
    ULONG nInterrupts = 0, nMSIInterrupts = 0;
    int nListSize = WdfCmResourceListGetCount(ResourcesTranslated);
    int i, iBar = -1;
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

                    /* unfortunately WDF doesn't tell us the exact BAR indices, all we can
                     * rely on is the order of resource descriptors
                     */
                    iBar = PCIGetBarIndex(iBar, &PCIHeader, pResDescriptor->u.Memory.Start);

                    pBar->iBar = iBar;
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
