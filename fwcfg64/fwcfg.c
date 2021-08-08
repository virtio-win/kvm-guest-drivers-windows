/*
 * This file contains fw_cfg routines
 *
 * Copyright (C) 2018 Virtuozzo International GmbH
 *
 */
#include "fwcfg.h"
#include "trace.h"
#include "fwcfg.tmh"

VOID FWCfgReadBlob(PVOID ioBase, UINT16 key, PVOID buf, ULONG count)
{
    WRITE_PORT_USHORT((PUSHORT)FW_CFG_CTL(ioBase), key);
    READ_PORT_BUFFER_UCHAR(FW_CFG_DAT(ioBase), (PUCHAR)buf, count);
}

NTSTATUS FWCfgCheckSig(PVOID ioBase)
{
    UCHAR signature[FW_CFG_SIG_SIZE];

    FWCfgReadBlob(ioBase, FW_CFG_SIGNATURE, signature, FW_CFG_SIG_SIZE);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_ALL,
                "Signature is [%.4s]", (PCHAR)signature);
    if (memcmp(signature, FW_CFG_QEMU, FW_CFG_SIG_SIZE))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_ALL, "Signature check failed, "
                                                "["FW_CFG_QEMU"] expected");
        return STATUS_BAD_DATA;
    }

    return STATUS_SUCCESS;
}

NTSTATUS FWCfgCheckFeatures(PVOID ioBase, UINT32 features)
{
    UINT32 f_bitmap;

    FWCfgReadBlob(ioBase, FW_CFG_ID, &f_bitmap, sizeof(f_bitmap));
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_ALL, "Features are 0x%lx", f_bitmap);
    if ((f_bitmap & features) != features)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_ALL, "Features check failed, "
                                                " 0x%lx expected", features);
        return STATUS_BAD_DATA;
    }

    return STATUS_SUCCESS;
}

UINT64 FWCfgReadDmaReg(PVOID ioBase)
{
    UINT64 dma_reg;

    ((UINT32 *)&dma_reg)[0] = READ_PORT_ULONG((PULONG)FW_CFG_DMA(ioBase));
    ((UINT32 *)&dma_reg)[1] = READ_PORT_ULONG((PULONG)FW_CFG_DMA(ioBase) + 1);

    return RtlUlonglongByteSwap(dma_reg);
}

VOID FWCfgWriteDmaReg(PVOID ioBase, UINT64 val)
{
    val = RtlUlonglongByteSwap(val);
    WRITE_PORT_ULONG((PULONG)FW_CFG_DMA(ioBase), ((PULONG)&val)[0]);
    WRITE_PORT_ULONG((PULONG)FW_CFG_DMA(ioBase) + 1, ((PULONG)&val)[1]);
}

NTSTATUS FWCfgCheckDma(PVOID ioBase)
{
    UINT64 test = FWCfgReadDmaReg(ioBase);
    if (test != FW_CFG_QEMU_DMA)
    {
        return STATUS_BAD_DATA;
    }

    return STATUS_SUCCESS;
}

/*
    At the end of this routine selector points at first file entry
*/
UINT32 FWCfgGetEntriesNum(PVOID ioBase)
{
    UINT32 num;

    FWCfgReadBlob(ioBase, FW_CFG_FILE_DIR, &num, sizeof(num));
    num = RtlUlongByteSwap(num);
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_ALL, "Total %lu entries", num);

    return num;
}

NTSTATUS FWCfgFindEntry(PVOID ioBase, const char *name,
                        PUSHORT index, ULONG size)
{
    UINT32 i;
    UINT32 total = FWCfgGetEntriesNum(ioBase);
    FWCfgFile f;

    if (total > MAXUINT16)
    {
        return STATUS_BAD_DATA;
    }

    for (i = 0; i < total; i++)
    {
        READ_PORT_BUFFER_UCHAR(FW_CFG_DAT(ioBase), (PUCHAR)&f, sizeof(f));
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_ALL, "[%.56s]", f.name);
        if (!strncmp(f.name, name, FW_CFG_MAX_FILE_PATH))
        {
            if (RtlUlongByteSwap(f.size) == size)
            {
                *index = RtlUshortByteSwap(f.select);
                return STATUS_SUCCESS;
            }
            return STATUS_BAD_DATA;
        }
    }

    return STATUS_BAD_DATA;
}

NTSTATUS FWCfgDmaSend(PVOID ioBase, LONGLONG data_pa, USHORT index,
                      UINT32 size, FWCfgDmaAccess *pDmaAccess, LONGLONG dmaAccess_pa)
{
    UINT16 ctrl = FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE;
    NTSTATUS status;

    pDmaAccess->control = RtlUlongByteSwap(((UINT32)index << 16UL) | ctrl);
    pDmaAccess->length = RtlUlongByteSwap(size);
    pDmaAccess->address = RtlUlonglongByteSwap(data_pa);

    FWCfgWriteDmaReg(ioBase, (UINT64)dmaAccess_pa);

    ctrl = RtlUlongByteSwap(pDmaAccess->control) & MAXUINT16;
    if (!ctrl)
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_ALL, "Transfer succeed");
        status = STATUS_SUCCESS;
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_ALL, "Transfer failed, 0x%x", ctrl);
        status = STATUS_IO_DEVICE_ERROR;
    }

    return status;
}
