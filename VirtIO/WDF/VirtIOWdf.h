/**********************************************************************
 * Copyright (c) 2016  Red Hat, Inc.
 *
 * File: VirtIOWdf.h
 *
 * Author(s):
 *  Ladi Prosek <lprosek@redhat.com>
 *
 * Public VirtioLib-WDF prototypes (driver API)
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
**********************************************************************/
#pragma once

#include <wdf.h>
#include "virtio_pci.h"

/* Configures a virtqueue, see VirtIOWdfInitQueues. */
typedef struct virtio_wdf_queue_param {
    /* queue name, optional */
    LPCSTR                  szName;

    /* interrupt associated with the queue */
    WDFINTERRUPT            Interrupt;

    /* old-style interrupt suppression, see virtio 1.0 spec § 2.4.7 */
    bool                    bEnableInterruptSuppression;
} VIRTIO_WDF_QUEUE_PARAM , *PVIRTIO_WDF_QUEUE_PARAM;

/* Data associated with a WDF virtio driver, usually declared as
 * a field in the driver's context structure and treated opaque.
 */
typedef struct virtio_wdf_driver {
    VirtIODevice            *pVIODevice;

    ULONG                   MemoryTag;

    BUS_INTERFACE_STANDARD  PCIBus;
    SINGLE_LIST_ENTRY       PCIBars;

    ULONG                   nInterrupts;
    ULONG                   nMSIInterrupts;

    WDFINTERRUPT            ConfigInterrupt;
    PVIRTIO_WDF_QUEUE_PARAM pQueueParams;
    
} VIRTIO_WDF_DRIVER, *PVIRTIO_WDF_DRIVER;

/* Initializes the VIRTIO_WDF_DRIVER context, called from driver's
 * EvtDevicePrepareHardware callback.
 */
NTSTATUS VirtIOWdfInitialize(PVIRTIO_WDF_DRIVER pWdfDriver,
                             WDFDEVICE Device,
                             WDFCMRESLIST ResourcesTranslated,
                             WDFINTERRUPT ConfigInterrupt,
                             ULONG MemoryTag,
                             USHORT nMaxQueues);

/* Device/driver feature negotiation routines. These can be called from
 * driver's EvtDevicePrepareHardware callback or later from its
 * EvtDeviceD0Entry callback. If the device is reset and re-initialized
 * (D0 exit, then D0 entry) and VirtIOWdfSetDriverFeatures is not called,
 * the same features are automatically negotiated.
 */
ULONGLONG VirtIOWdfGetDeviceFeatures(PVIRTIO_WDF_DRIVER pWdfDriver);
NTSTATUS VirtIOWdfSetDriverFeatures(PVIRTIO_WDF_DRIVER pWdfDriver,
                                    ULONGLONG uFeatures);

/* Queue discovery entry point. Must be called after each device reset as
 * there is no way to reinitialize or reset individual queues.
 */
NTSTATUS VirtIOWdfInitQueues(PVIRTIO_WDF_DRIVER pWdfDriver,
                             ULONG nQueues,
                             struct virtqueue **pQueues,
                             PVIRTIO_WDF_QUEUE_PARAM pQueueParams);

/* Final signal to the device that the driver has successfully initialized
 * and is ready for device operation or that it has failed to do so.
 * It is not legal to notify the device before VirtIOWdfSetDriverOK is called.
 */
void VirtIOWdfSetDriverOK(PVIRTIO_WDF_DRIVER pWdfDriver);
void VirtIOWdfSetDriverFailed(PVIRTIO_WDF_DRIVER pWdfDriver);

/* Resets the device and destroys virtqueue data structures. To be called
 * from driver's EvtDeviceD0Exit callback.
 */
NTSTATUS VirtIOWdfDestroyQueues(PVIRTIO_WDF_DRIVER pWdfDriver);

/* Destroys the VIRTIO_WDF_DRIVER context and deallocates all resources.
 * To be called from driver's EvtDeviceReleaseHardware callback.
 */
NTSTATUS VirtIOWdfShutdown(PVIRTIO_WDF_DRIVER pWdfDriver);

/* Returns the contents of the ISR status field and acknowledges the
 * interrupt. Called from driver's ISR if traditional IRQ interrupts
 * are used.
 */
UCHAR VirtIOWdfGetISRStatus(PVIRTIO_WDF_DRIVER pWdfDriver);

/* Device config space access routines. Follow specific device documentation
 * for rules on when and how these can be called. If interrupt on device
 * config change is desired, a valid WDFINTERRUPT should be passed to
 * VirtIOWdfInitialize.
 */
void VirtIOWdfDeviceGet(PVIRTIO_WDF_DRIVER pWdfDriver,
                        ULONG offset,
                        PVOID buf,
                        ULONG len);
void VirtIOWdfDeviceSet(PVIRTIO_WDF_DRIVER pWdfDriver,
                        ULONG offset,
                        CONST PVOID buf,
                        ULONG len);
