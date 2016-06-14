/**********************************************************************
 * Copyright (c) 2016  Red Hat, Inc.
 *
 * File: VirtIOWdf.c
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Implementation of VirtioLib-WDF driver API
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "osdep.h"
#include "virtio_pci.h"
#include "VirtIOWdf.h"
#include "private.h"

#include <wdmguid.h>

extern VirtIOSystemOps VirtIOWdfSystemOps;

NTSTATUS VirtIOWdfInitialize(PVIRTIO_WDF_DRIVER pWdfDriver,
                             WDFDEVICE Device,
                             WDFCMRESLIST ResourcesTranslated,
                             WDFINTERRUPT ConfigInterrupt,
                             ULONG MemoryTag,
                             USHORT nMaxQueues)
{
    NTSTATUS status = STATUS_SUCCESS;

    RtlZeroMemory(pWdfDriver, sizeof(*pWdfDriver));
    pWdfDriver->MemoryTag = MemoryTag;

    /* get the PCI bus interface */
    status = WdfFdoQueryForInterface(
        Device,
        &GUID_BUS_INTERFACE_STANDARD,
        (PINTERFACE)&pWdfDriver->PCIBus,
        sizeof(pWdfDriver->PCIBus),
        1 /* version */,
        NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* set up resources */
    status = PCIAllocBars(ResourcesTranslated, pWdfDriver);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* initialize the underlying VirtIODevice */
    status = virtio_device_initialize(
        &pWdfDriver->VIODevice,
        &VirtIOWdfSystemOps,
        pWdfDriver,
        sizeof(pWdfDriver->VIODevice));
    if (!NT_SUCCESS(status)) {
        PCIFreeBars(pWdfDriver);
    }

    pWdfDriver->ConfigInterrupt = ConfigInterrupt;
    virtio_device_set_msix_used(&pWdfDriver->VIODevice, pWdfDriver->nMSIInterrupts > 0);

    return status;
}

ULONGLONG VirtIOWdfGetDeviceFeatures(PVIRTIO_WDF_DRIVER pWdfDriver)
{
    return virtio_get_features(&pWdfDriver->VIODevice);
}

NTSTATUS VirtIOWdfSetDriverFeatures(PVIRTIO_WDF_DRIVER pWdfDriver,
                                    ULONGLONG uFeatures)
{
    /* make sure that we always follow the status bit-setting protocol */
    u8 status = virtio_get_status(&pWdfDriver->VIODevice);
    if (!(status & VIRTIO_CONFIG_S_ACKNOWLEDGE)) {
        virtio_add_status(&pWdfDriver->VIODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    }
    if (!(status & VIRTIO_CONFIG_S_DRIVER)) {
        virtio_add_status(&pWdfDriver->VIODevice, VIRTIO_CONFIG_S_DRIVER);
    }

    pWdfDriver->VIODevice.features = uFeatures;
    return virtio_finalize_features(&pWdfDriver->VIODevice);
}

NTSTATUS VirtIOWdfInitQueues(PVIRTIO_WDF_DRIVER pWdfDriver,
                             ULONG nQueues,
                             struct virtqueue **pQueues,
                             PVIRTIO_WDF_QUEUE_PARAM pQueueParams)
{
    NTSTATUS status;
    ULONG i;

    /* make sure that we always follow the status bit-setting protocol */
    u8 dev_status = virtio_get_status(&pWdfDriver->VIODevice);
    if (!(dev_status & VIRTIO_CONFIG_S_ACKNOWLEDGE)) {
        virtio_add_status(&pWdfDriver->VIODevice, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    }
    if (!(dev_status & VIRTIO_CONFIG_S_DRIVER)) {
        virtio_add_status(&pWdfDriver->VIODevice, VIRTIO_CONFIG_S_DRIVER);
    }
    if (!(dev_status & VIRTIO_CONFIG_S_FEATURES_OK)) {
        status = virtio_finalize_features(&pWdfDriver->VIODevice);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    /* find and initialize queues */
    pWdfDriver->pQueueParams = pQueueParams;
    status = virtio_find_queues(
        &pWdfDriver->VIODevice,
        nQueues,
        pQueues);
    pWdfDriver->pQueueParams = NULL;

    if (NT_SUCCESS(status)) {
        /* set interrupt suppression flags */
        for (i = 0; i < nQueues; i++) {
            virtqueue_set_event_suppression(
                pQueues[i],
                pQueueParams[i].bEnableInterruptSuppression);
        }
    }
    return status;
}

void VirtIOWdfSetDriverOK(PVIRTIO_WDF_DRIVER pWdfDriver)
{
    virtio_device_ready(&pWdfDriver->VIODevice);
}

void VirtIOWdfSetDriverFailed(PVIRTIO_WDF_DRIVER pWdfDriver)
{
    virtio_add_status(&pWdfDriver->VIODevice, VIRTIO_CONFIG_S_FAILED);
}

NTSTATUS VirtIOWdfShutdown(PVIRTIO_WDF_DRIVER pWdfDriver)
{
    virtio_device_shutdown(&pWdfDriver->VIODevice);

    PCIFreeBars(pWdfDriver);

    return STATUS_SUCCESS;
}

NTSTATUS VirtIOWdfDestroyQueues(PVIRTIO_WDF_DRIVER pWdfDriver)
{
    virtio_device_reset(&pWdfDriver->VIODevice);
    virtio_delete_queues(&pWdfDriver->VIODevice);

    return STATUS_SUCCESS;
}

void VirtIOWdfDeviceGet(PVIRTIO_WDF_DRIVER pWdfDriver,
                        ULONG offset,
                        PVOID buf,
                        ULONG len)
{
    virtio_get_config(
        &pWdfDriver->VIODevice,
        offset,
        buf,
        len);
}

void VirtIOWdfDeviceSet(PVIRTIO_WDF_DRIVER pWdfDriver,
                        ULONG offset,
                        CONST PVOID buf,
                        ULONG len)
{
    virtio_set_config(
        &pWdfDriver->VIODevice,
        offset,
        buf,
        len);
}

UCHAR VirtIOWdfGetISRStatus(PVIRTIO_WDF_DRIVER pWdfDriver)
{
    return virtio_read_isr_status(&pWdfDriver->VIODevice);
}
