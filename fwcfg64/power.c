/*
 * This file contains PNP power callbacks
 *
 * Copyright (C) 2018 Virtuozzo International GmbH
 *
 */
#include "driver.h"
#include "fwcfg.h"
#include "trace.h"
#include "power.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, FwCfgEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, FwCfgEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, FwCfgEvtDeviceD0Entry)
#pragma alloc_text(PAGE, FwCfgEvtDeviceD0Exit)
#endif

VOID PutKdbg(PDEVICE_CONTEXT ctx)
{
    if (ctx->kdbg)
    {
        ExFreePool(ctx->kdbg);
        ctx->kdbg = NULL;
        VMCoreInfoSend(ctx);
    }
}

NTSTATUS FwCfgEvtDeviceD0Exit(IN WDFDEVICE Device,
                             IN WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(TargetState);

    PAGED_CODE();

    return STATUS_SUCCESS;
}

NTSTATUS FwCfgEvtDeviceD0Entry(IN WDFDEVICE Device,
                              IN WDF_POWER_DEVICE_STATE PreviousState)
{
    PDEVICE_CONTEXT ctx = GetDeviceContext(Device);
    NTSTATUS status;
    PVMCI_ELF64_NOTE note = ctx->vmci_data.pNote;
    PVMCOREINFO pVmci = ctx->vmci_data.pVmci;

    UNREFERENCED_PARAMETER(PreviousState);

    PAGED_CODE();

    note->n_namesz = sizeof(VMCI_ELF_NOTE_NAME);
    note->n_descsz = DUMP_HDR_SIZE;
    note->n_type = 0;
    memcpy(note->n_name, VMCI_ELF_NOTE_NAME, note->n_namesz);

    pVmci->host_fmt = 0;
    pVmci->guest_fmt = VMCOREINFO_FORMAT_ELF;
    pVmci->paddr = ctx->vmci_data.note_pa;
    pVmci->size = sizeof(VMCI_ELF64_NOTE);

    status = VMCoreInfoFill(ctx);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = VMCoreInfoSend(ctx);

    return status;
}

NTSTATUS FwCfgEvtDevicePrepareHardware(IN WDFDEVICE Device,
                                      IN WDFCMRESLIST Resources,
                                      IN WDFCMRESLIST ResourcesTranslated)
{
    ULONG i;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR desc;
    PDEVICE_CONTEXT ctx;
    NTSTATUS status = STATUS_RESOURCE_IN_USE;

    UNREFERENCED_PARAMETER(Resources);

    PAGED_CODE();

    ctx = GetDeviceContext(Device);

    for (i = 0; i < WdfCmResourceListGetCount(ResourcesTranslated); i++)
    {
        desc = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if (desc &&
            (desc->Type == CmResourceTypePort) &&
            (desc->Flags & CM_RESOURCE_PORT_IO))
        {
            ctx->ioBase = (PVOID)(ULONG_PTR)desc->u.Port.Start.QuadPart;
            ctx->ioSize = desc->u.Port.Length;
            TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP,
                        "I/O ports: 0x%llx-0x%llx", (UINT64)ctx->ioBase,
                        (UINT64)ctx->ioBase + ctx->ioSize);
            status = STATUS_SUCCESS;
        }
    }

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "No I/O ports");
        return status;
    }

    if (FWCfgCheckSig(ctx->ioBase) ||
        FWCfgCheckFeatures(ctx->ioBase, FW_CFG_VERSION_DMA) ||
        FWCfgCheckDma(ctx->ioBase))
    {
        return STATUS_BAD_DATA;
    }

    ctx->index = 0;
    FWCfgFindEntry(ctx->ioBase, ENTRY_NAME, &ctx->index, sizeof(VMCOREINFO));
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP,
                "VMCoreInfo index is 0x%x", ctx->index);
    if (!ctx->index)
    {
        return STATUS_BAD_DATA;
    }

    status = GetKdbg(ctx);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "Failed to get KdDebuggerDataBlock");
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS FwCfgEvtDeviceReleaseHardware(IN WDFDEVICE Device,
                                      IN WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT ctx;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    ctx = GetDeviceContext(Device);

    PutKdbg(ctx);

    return STATUS_SUCCESS;
}
