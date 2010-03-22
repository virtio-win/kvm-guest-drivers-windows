#ifndef VIOSERIAL_DRIVER_H
#define VIOSERIAL_DRIVER_H

#include "osdep.h"
#include "virtio_pci.h"
#include "VirtIO.h"
#include "VIOSerial.h"

#define VIOSERIAL_DRIVER_MEMORY_TAG (ULONG)'rsIV'

// WDF callbacks declarations
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VIOSerialEvtDeviceAdd;

//
// The device extension for the device object
//
typedef struct _DEVICE_CONTEXT
{
	VirtIODevice		IODevice;
	
	// IO imapping info
	PHYSICAL_ADDRESS	PortBasePA;
	ULONG				uPortLength;
	PVOID				pPortBase;	//Mapped IO port address
	bool				bPortMapped; // Is the port mapped

	WDFINTERRUPT		WdfInterrupt; //Interrupt object

	VIOSERIAL_PORT	SerialPorts[VIRTIO_SERIAL_MAX_QUEUES_COUPLES];

	KSPIN_LOCK			DPCLock;

	BOOLEAN				isDeviceInitialized;

	int				isHostMultiport;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

#define VIOSERIAL_SYMBOLIC_LINK L"\\DosDevices\\viosdev"

#endif /* VIOSERIAL_DRIVER_H */
