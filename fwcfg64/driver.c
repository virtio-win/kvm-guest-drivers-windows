/*
 * This file contains driver routines
 *
 * Copyright (C) 2018 Virtuozzo International GmbH
 *
 */
#include "driver.h"
#include "fwcfg.h"
#include "trace.h"
#include "driver.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, FwCfgEvtDeviceAdd)
#pragma alloc_text(PAGE, FwCfgEvtDriverCleanup)
#endif

NTSTATUS VMCoreInfoFill(PDEVICE_CONTEXT ctx)
{
    NTSTATUS status;
    PUCHAR hdr_buf;
    ULONG bufSizeNeeded;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_ALL, "Obtaining header");

    hdr_buf = (PUCHAR)ctx->vmci_data.pNote + FIELD_OFFSET(VMCI_ELF64_NOTE, n_desc);
    status = KeInitializeCrashDumpHeader(DUMP_TYPE_FULL, 0, hdr_buf,
                                         DUMP_HDR_SIZE, &bufSizeNeeded);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_ALL, "Failed to obtain header");
        return status;
    }

    /*
     * Original KDBG pointer was saved in header by system.
     * BugcheckParameter1 field is unused in live system and will be filled by QEMU.
     * So the pointer to decoded KDBG can be stored in this field.
     */
    *(PULONG64)(hdr_buf + DUMP_HDR_OFFSET_BUGCHECK_PARAM1) = (ULONG64)ctx->kdbg;

    return status;
}

NTSTATUS VMCoreInfoSend(PDEVICE_CONTEXT ctx)
{
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_ALL, "Sending header");

    status = FWCfgDmaSend(ctx->ioBase, ctx->vmci_data.vmci_pa, ctx->index,
                          sizeof(VMCOREINFO), ctx->dma_access, ctx->dma_access_pa);

    return status;
}

VOID FwCfgEvtDriverCleanup(IN WDFOBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE();

    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)DriverObject));
}

NTSTATUS GetKdbg(PDEVICE_CONTEXT ctx)
{
    PUCHAR minidump;
    ULONG32 kdbg_offset;
    ULONG32 kdbg_size;
    CONTEXT context = { 0 };
    NTSTATUS status = STATUS_SUCCESS;

    minidump = ExAllocatePoolWithTag(NonPagedPoolNx, 0x40000, 'pmdm');
    if (!minidump)
    {
         return STATUS_MEMORY_NOT_ALLOCATED;
    }

    KeCapturePersistentThreadState(&context, NULL, 0, 0, 0, 0, 0, minidump);

    kdbg_offset = *(PULONG32)(minidump + MINIDUMP_OFFSET_KDBG_OFFSET);
    kdbg_size = *(PULONG32)(minidump + MINIDUMP_OFFSET_KDBG_SIZE);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT,
        "KdDebuggerDataBlock size = %lx, offset = 0x%lx",
        kdbg_size, kdbg_offset);

    ctx->kdbg = ExAllocatePoolWithTag(NonPagedPoolNx, kdbg_size, 'gbdk');
    if (!ctx->kdbg)
    {
        status = STATUS_MEMORY_NOT_ALLOCATED;
        goto out_free_minidump;
    }

    memcpy(ctx->kdbg, minidump + kdbg_offset, kdbg_size);

out_free_minidump:
    ExFreePool(minidump);

    return status;
}

static VOID FwCfgContextInit(PDEVICE_CONTEXT ctx)
{
    PCBUF_DATA pcbuf_data;
    LONGLONG pcbuf_data_pa;

    pcbuf_data = WdfCommonBufferGetAlignedVirtualAddress(ctx->cbuf);
    pcbuf_data_pa = WdfCommonBufferGetAlignedLogicalAddress(ctx->cbuf).QuadPart;

    ctx->vmci_data.pNote = &pcbuf_data->note;
    ctx->vmci_data.note_pa = pcbuf_data_pa + FIELD_OFFSET(CBUF_DATA, note);

    ctx->vmci_data.pVmci = &pcbuf_data->vmci;
    ctx->vmci_data.vmci_pa = pcbuf_data_pa + FIELD_OFFSET(CBUF_DATA, vmci);

    ctx->dma_access = &pcbuf_data->fwcfg_da;
    ctx->dma_access_pa = pcbuf_data_pa + FIELD_OFFSET(CBUF_DATA, fwcfg_da);
}

NTSTATUS FwCfgEvtDeviceAdd(IN WDFDRIVER Driver, IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS status;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDEVICE device;
    PDEVICE_CONTEXT ctx;
    WDF_DMA_ENABLER_CONFIG dmaEnablerConfig;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

    pnpPowerCallbacks.EvtDevicePrepareHardware = FwCfgEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = FwCfgEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = FwCfgEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = FwCfgEvtDeviceD0Exit;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfDeviceCreate failed: %!STATUS!", status);
        return status;
    }

    ctx = GetDeviceContext(device);
    memset(ctx, 0, sizeof(*ctx));

    WDF_DMA_ENABLER_CONFIG_INIT(&dmaEnablerConfig, WdfDmaProfilePacket64,
                                sizeof(CBUF_DATA));
    status = WdfDmaEnablerCreate(device, &dmaEnablerConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES, &ctx->dmaEnabler);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "Failed to create DMA enabler");
        return status;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT,
        "DMA enabler created");

    status = WdfCommonBufferCreate(ctx->dmaEnabler, sizeof(CBUF_DATA),
                                   WDF_NO_OBJECT_ATTRIBUTES, &ctx->cbuf);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "Failed to create common buffer");
        return status;
    }

    FwCfgContextInit(ctx);

    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject,
                     IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    WPP_INIT_TRACING(DriverObject, RegistryPath);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = FwCfgEvtDriverCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, FwCfgEvtDeviceAdd);

    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes,
                             &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                    "WdfDriverCreate failed: %!STATUS!", status);
        WPP_CLEANUP(DriverObject);
    }

    return status;
}
