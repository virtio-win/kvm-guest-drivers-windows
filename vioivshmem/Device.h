#include "public.h"

EXTERN_C_START

typedef __int8 int8_t;
typedef __int32 int32_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int32 uint32_t;

#pragma align(push,4)
typedef struct VIOIVSHMEMDeviceRegisters
{
	uint32_t irqMask;
	uint32_t irqStatus;
	int32_t  ivProvision;
	uint32_t doorbell;
	uint8_t  reserved[240];
}
VIOIVSHMEMDeviceRegisters, *PVIOIVSHMEMDeviceRegisters;
#pragma align(pop)

typedef struct _DEVICE_CONTEXT
{
	PVIOIVSHMEMDeviceRegisters devRegisters; // the device registers (BAR0)

	MM_PHYSICAL_ADDRESS_LIST   shmemAddr;      // physical address of the shared memory (BAR2)
	PMDL                       shmemMDL;       // memory discriptor list of the shared memory
	PVOID                      shmemMap;       // memory mapping of the shared memory
	WDFFILEOBJECT              owner;          // the file object that currently owns the mapping
	UINT16                     interruptCount; // the number of interrupt entries allocated
	UINT16                     interruptsUsed; // the number of interrupt entries used
	WDFINTERRUPT              *interrupts;     // interrupts for this device
}
DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

NTSTATUS VIOIVSHMEMCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit);

EVT_WDF_DEVICE_PREPARE_HARDWARE VIOIVSHMEMEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VIOIVSHMEMEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY VIOIVSHMEMEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT VIOIVSHMEMEvtD0Exit;
EVT_WDF_INTERRUPT_ISR VIOIVSHMEMInterruptISR;

EXTERN_C_END
