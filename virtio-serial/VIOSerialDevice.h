#ifndef VIOSERIAL_DEVICE_H
#define VIOSERIAL_DEVICE_H

#include "osdep.h"
#include "virtio_pci.h"
#include "VirtIO.h"
#include "VIOSerial.h"

EVT_WDF_DEVICE_PREPARE_HARDWARE		VIOSerialEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE		VIOSerialEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY				VIOSerialEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT				VIOSerialEvtDeviceD0Exit;

// IO related callbacks.
EVT_WDF_DEVICE_FILE_CREATE			VIOSerialEvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE					VIOSerialEvtFileClose;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL	VIOSerialEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_READ			VIOSerialEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE			VIOSerialEvtIoWrite;

// Interrupt handling related callbacks
EVT_WDF_INTERRUPT_ISR				VIOSerialInterruptIsr;
EVT_WDF_INTERRUPT_DPC				VIOSerialInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE			VIOSerialInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE			VIOSerialInterruptDisable;


#endif /* VIOSERIAL_DEVICE_H */
