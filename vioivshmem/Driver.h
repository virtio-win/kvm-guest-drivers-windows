#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>

#include "device.h"
#include "queue.h"
#include "trace.h"

// using error levels to avoid the debug print filter
#define DEBUG_ERROR(fmt, ...) do { KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[E:VIOIVSHMEM] " fmt "\n", ## __VA_ARGS__)); } while (0)
#define DEBUG_INFO(fmt, ...) do { KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[I:VIOIVSHMEM] " fmt "\n", ## __VA_ARGS__)); } while (0)

EXTERN_C_START

//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VIOIVSHMEMEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VIOIVSHMEMEvtDriverContextCleanup;

EXTERN_C_END
