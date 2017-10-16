#include "public.h"

EXTERN_C_START

typedef struct _DEVICE_CONTEXT
{
	MM_PHYSICAL_ADDRESS_LIST shmemAddr;	  // physical address of the shared memory
	PMDL                     shmemMDL;    // memory discriptor list of the shared memory
	PVOID                    shmemMap;    // memory mapping of the shared memory
	WDFFILEOBJECT            owner;       // the file object that currently owns the mapping
}
DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

NTSTATUS VIOIVSHMEMCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit);

EVT_WDF_DEVICE_PREPARE_HARDWARE VIOIVSHMEMEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VIOIVSHMEMEvtDeviceReleaseHardware;

EXTERN_C_END
