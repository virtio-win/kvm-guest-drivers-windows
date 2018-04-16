/*
 * This file contains fw_cfg related definitions
 *
 * Copyright (C) 2018 Virtuozzo International GmbH
 *
 */
#pragma once
#include <ntddk.h>

#define FW_CFG_CTL_OFFSET       0x00
#define FW_CFG_DAT_OFFSET       0x01
#define FW_CFG_DMA_OFFSET       0x04

#define FW_CFG_CTL(p)           ((PUCHAR)(p) + FW_CFG_CTL_OFFSET)
#define FW_CFG_DAT(p)           ((PUCHAR)(p) + FW_CFG_DAT_OFFSET)
#define FW_CFG_DMA(p)           ((PUCHAR)(p) + FW_CFG_DMA_OFFSET)

#define FW_CFG_SIG_SIZE         4
#define FW_CFG_MAX_FILE_PATH    56

#define FW_CFG_SIGNATURE        0x00
#define FW_CFG_ID               0x01
#define FW_CFG_FILE_DIR         0x19

#define FW_CFG_VERSION          0x01
#define FW_CFG_VERSION_DMA      0x02

#define FW_CFG_DMA_CTL_ERROR    0x01
#define FW_CFG_DMA_CTL_READ     0x02
#define FW_CFG_DMA_CTL_SKIP     0x04
#define FW_CFG_DMA_CTL_SELECT   0x08
#define FW_CFG_DMA_CTL_WRITE    0x10

#define FW_CFG_QEMU             "QEMU"
#define FW_CFG_QEMU_DMA         0x51454d5520434647ULL

#pragma pack(push, 1)
typedef struct FWCfgFile {
    UINT32  size;
    UINT16  select;
    UINT16  reserved;
    char    name[FW_CFG_MAX_FILE_PATH];
} FWCfgFile;

typedef struct FWCfgDmaAccess {
    UINT32 control;
    UINT32 length;
    UINT64 address;
} FWCfgDmaAccess;
#pragma pack(pop)

VOID FWCfgReadBlob(PVOID ioBase, UINT16 key, PVOID buf, ULONG count);
NTSTATUS FWCfgCheckSig(PVOID ioBase);
NTSTATUS FWCfgCheckFeatures(PVOID ioBase, UINT32 features);
UINT64 FWCfgReadDmaReg(PVOID ioBase);
VOID FWCfgWriteDmaReg(PVOID ioBase, UINT64 val);
NTSTATUS FWCfgCheckDma(PVOID ioBase);
UINT32 FWCfgGetEntriesNum(PVOID ioBase);
NTSTATUS FWCfgFindEntry(PVOID ioBase, const char *name,
                        PUSHORT index, ULONG size);
NTSTATUS FWCfgDmaSend(PVOID ioBase, LONGLONG data_pa, USHORT index,
                      UINT32 size, FWCfgDmaAccess *da, LONGLONG da_pa);
