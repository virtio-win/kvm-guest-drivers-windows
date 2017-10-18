#include "public.h"

EXTERN_C_START

#pragma align(push,4)
typedef struct IVSHMEMDeviceRegisters
{
    UINT32  irqMask;
    UINT32  irqStatus;
    INT32   ivProvision;
    UINT32  doorbell;
    UINT8   reserved[240];
}
IVSHMEMDeviceRegisters, *PIVSHMEMDeviceRegisters;
#pragma align(pop)

typedef struct IVSHMEMEventListEntry
{
    UINT16   vector;
    PRKEVENT event;
    LIST_ENTRY ListEntry;
}
IVSHMEMEventListEntry, *PIVSHMEMEventListEntry;

typedef struct _DEVICE_CONTEXT
{
    PIVSHMEMDeviceRegisters devRegisters; // the device registers (BAR0)

    MM_PHYSICAL_ADDRESS_LIST   shmemAddr;      // physical address of the shared memory (BAR2)
    PMDL                       shmemMDL;       // memory discriptor list of the shared memory
    PVOID                      shmemMap;       // memory mapping of the shared memory
    WDFFILEOBJECT              owner;          // the file object that currently owns the mapping
    UINT16                     interruptCount; // the number of interrupt entries allocated
    UINT16                     interruptsUsed; // the number of interrupt entries used
    WDFINTERRUPT              *interrupts;     // interrupts for this device

    KSPIN_LOCK                 eventListLock;  // spinlock for the below event list
    LIST_ENTRY                 eventList;      // pending events to fire
}
DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

NTSTATUS IVSHMEMCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit);

EVT_WDF_DEVICE_PREPARE_HARDWARE IVSHMEMEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE IVSHMEMEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY IVSHMEMEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT IVSHMEMEvtD0Exit;
EVT_WDF_INTERRUPT_ISR IVSHMEMInterruptISR;

EXTERN_C_END
