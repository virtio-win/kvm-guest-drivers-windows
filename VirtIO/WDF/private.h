/**********************************************************************
 * Copyright (c) 2016-2017 Red Hat, Inc.
 *
 * File: private.h
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Private VirtioLib-WDF prototypes
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
**********************************************************************/
#pragma once

#include <Ntddk.h>
#include <wdf.h>

typedef struct virtio_wdf_bar {
    SINGLE_LIST_ENTRY ListEntry;

    int               iBar;
    PHYSICAL_ADDRESS  BasePA;
    ULONG             uLength;
    PVOID             pBase;
    bool              bPortSpace;
} VIRTIO_WDF_BAR, *PVIRTIO_WDF_BAR;

typedef struct virtio_wdf_interrupt_context {
    /* This is a workaround for a WDF bug where on resource rebalance
     * it does not preserve the MessageNumber field of its internal
     * data structures describing interrupts. As a result, we fail to
     * report the right MSI message number to the virtio device when
     * re-initializing it and it may stop working.
     */
    USHORT            uMessageNumber;
    bool              bMessageNumberSet;
} VIRTIO_WDF_INTERRUPT_CONTEXT, *PVIRTIO_WDF_INTERRUPT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VIRTIO_WDF_INTERRUPT_CONTEXT, GetInterruptContext)

NTSTATUS PCIAllocBars(WDFCMRESLIST ResourcesTranslated,
                      PVIRTIO_WDF_DRIVER pWdfDriver);

void PCIFreeBars(PVIRTIO_WDF_DRIVER pWdfDriver);

int PCIReadConfig(PVIRTIO_WDF_DRIVER pWdfDriver,
                  int where,
                  void *buffer,
                  size_t length);

NTSTATUS PCIRegisterInterrupt(WDFINTERRUPT Interrupt);

u16 PCIGetMSIInterruptVector(WDFINTERRUPT Interrupt);
