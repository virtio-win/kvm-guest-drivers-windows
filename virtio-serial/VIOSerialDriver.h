#ifndef VIOSERIAL_DRIVER_H
#define VIOSERIAL_DRIVER_H

#include "virtio_pci.h"
#include "VirtIO.h"

#define VIOSERIAL_DRIVER_MEMORY_TAG (ULONG)'rsIV'

// WDF callbacks declarations
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VIOSerialEvtDeviceAdd;
EVT_WDF_DEVICE_CONTEXT_CLEANUP VIOSerialEvtDeviceContextCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE VIOSerialEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VIOSerialEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY VIOSerialEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT VIOSerialEvtDeviceD0Exit;
//EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT VIOSerialEvtDeviceSelfManagedIoInit;

// IO related callbacks.
EVT_WDF_DEVICE_FILE_CREATE VIOSerialEvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE VIOSerialEvtFileClose;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VIOSerialEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_READ VIOSerialEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE VIOSerialEvtIoWrite;

//
// The device extension for the device object
//
typedef struct _DEVICE_CONTEXT
{
	VirtIODevice	IODevice;
	ULONG_PTR		PortBase;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

#endif /* VIOSERIAL_DRIVER_H */
