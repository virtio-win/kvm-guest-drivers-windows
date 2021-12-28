/*
 * This file contains driver related definitions
 *
 * Copyright (C) 2018 Virtuozzo International GmbH
 *
 */
#include <ntddk.h>
#include <wdf.h>
#include "fwcfg.h"

#define ENTRY_NAME              "etc/vmcoreinfo"
#define VMCI_ELF_NOTE_NAME      "VMCOREINFO"
#define DUMP_TYPE_FULL          1
#define VMCOREINFO_FORMAT_ELF   0x1
#ifdef _AMD64_
    #define DUMP_HDR_SIZE                   (PAGE_SIZE * 2)
    #define MINIDUMP_OFFSET_KDBG_OFFSET     (DUMP_HDR_SIZE + 0x70)
    #define MINIDUMP_OFFSET_KDBG_SIZE       (DUMP_HDR_SIZE + 0x74)
    #define DUMP_HDR_OFFSET_BUGCHECK_PARAM1 0x40
#else
    #define DUMP_HDR_SIZE                   (PAGE_SIZE)
    #define MINIDUMP_OFFSET_KDBG_OFFSET     (DUMP_HDR_SIZE + 0x58)
    #define MINIDUMP_OFFSET_KDBG_SIZE       (DUMP_HDR_SIZE + 0x5c)
    #define DUMP_HDR_OFFSET_BUGCHECK_PARAM1 0x2c
#endif

#define MINIDUMP_BUFFER_SIZE 0x40000

#define ROUND_UP(x, n) (((x) + (n) - 1) & (-(n)))

#pragma pack(push, 1)
typedef struct VMCOREINFO {
    UINT16 host_fmt;
    UINT16 guest_fmt;
    UINT32 size;
    UINT64 paddr;
} VMCOREINFO, *PVMCOREINFO;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct VMCI_ELF64_NOTE {
    UINT32  n_namesz;
    UINT32  n_descsz;
    UINT32  n_type;
    UCHAR   n_name[ROUND_UP(sizeof(VMCI_ELF_NOTE_NAME), 4)];
    UCHAR   n_desc[ROUND_UP(DUMP_HDR_SIZE, 4)];
} VMCI_ELF64_NOTE, *PVMCI_ELF64_NOTE;
#pragma pack(pop)

typedef struct CBUF_DATA {
    VMCI_ELF64_NOTE note;
    VMCOREINFO vmci;
    FWCfgDmaAccess fwcfg_da;
} CBUF_DATA, *PCBUF_DATA;

typedef struct VMCI_DATA {
    PVMCOREINFO         pVmci;
    PVMCI_ELF64_NOTE    pNote;
    LONGLONG            vmci_pa;
    LONGLONG            note_pa;
} VMCI_DATA, *PVMCI_DATA;

typedef struct DEVICE_CONTEXT {
    PVOID               ioBase;
    ULONG               ioSize;
    UINT16              index;
    WDFCOMMONBUFFER     cbuf;
    WDFDMAENABLER       dmaEnabler;
    VMCI_DATA           vmci_data;
    FWCfgDmaAccess      *dma_access;
    LONGLONG            dma_access_pa;
    PUCHAR              kdbg;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

ULONG NTAPI KeCapturePersistentThreadState(PCONTEXT Context,
                                           PKTHREAD Thread,
                                           ULONG BugCheckCode,
                                           ULONG BugCheckParameter1,
                                           ULONG BugCheckParameter2,
                                           ULONG BugCheckParameter3,
                                           ULONG BugCheckParameter4,
                                           PVOID VirtualAddress);

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD FwCfgEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP _IRQL_requires_(PASSIVE_LEVEL) FwCfgEvtDriverCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE FwCfgEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE FwCfgEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY FwCfgEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT FwCfgEvtDeviceD0Exit;

NTSTATUS VMCoreInfoFill(PDEVICE_CONTEXT ctx);
NTSTATUS VMCoreInfoSend(PDEVICE_CONTEXT ctx);

NTSTATUS GetKdbg(PDEVICE_CONTEXT ctx);

