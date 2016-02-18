/**********************************************************************
 * Copyright (c) 2016  Red Hat, Inc.
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

NTSTATUS PCIAllocBars(WDFCMRESLIST ResourcesTranslated,
                      PVIRTIO_WDF_DRIVER pWdfDriver);

void PCIFreeBars(PVIRTIO_WDF_DRIVER pWdfDriver);

int PCIReadConfig(PVIRTIO_WDF_DRIVER pWdfDriver,
                  int where,
                  void *buffer,
                  size_t length);
