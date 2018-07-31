#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>

#if (NTDOI_VERSION >= NTDOI_WIN8)
#define IVSHMEM_NONPAGED_POOL NonPagedPoolNx
#else
#define IVSHMEM_NONPAGED_POOL NonPagedPool
#endif

#include "device.h"
#include "queue.h"

// using error levels to avoid the debug print filter
#define DEBUG_ERROR(fmt, ...) do { KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[E:IVSHMEM] " fmt "\n", ## __VA_ARGS__)); } while (0)
#define DEBUG_INFO(fmt, ...) do { KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[I:IVSHMEM] " fmt "\n", ## __VA_ARGS__)); } while (0)

EXTERN_C_START

//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD IVSHMEMEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP IVSHMEMEvtDriverContextCleanup;

EXTERN_C_END
