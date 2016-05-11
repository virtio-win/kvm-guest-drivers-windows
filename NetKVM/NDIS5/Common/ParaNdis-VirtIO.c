/**********************************************************************
* Copyright (c) 2008-2016 Red Hat, Inc.
*
* File: ParaNdis-VirtIO.c
*
* This file contains NDIS driver VirtIO callbacks
*
* This work is licensed under the terms of the GNU GPL, version 2.  See
* the COPYING file in the top-level directory.
*
**********************************************************************/
#include "ndis56common.h"

/////////////////////////////////////////////////////////////////////////////////////
//
// ReadVirtIODeviceRegister\WriteVirtIODeviceRegister
// NDIS specific implementation of the IO and memory space read\write
//
// The lower 64k of memory is never mapped so we can use the same routines
// for both port I/O and memory access and use the address alone to decide
// which space to use.
/////////////////////////////////////////////////////////////////////////////////////

#define PORT_MASK 0xFFFF

u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    ULONG ulValue;

    if (ulRegister & ~PORT_MASK) {
        NdisReadRegisterUlong(ulRegister, &ulValue);
    } else {
        NdisRawReadPortUlong(ulRegister, &ulValue);
    }

    DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, ulValue));
    return ulValue;
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, ulValue));

    if (ulRegister & ~PORT_MASK) {
        NdisWriteRegisterUlong((PULONG)ulRegister, ulValue);
    } else {
        NdisRawWritePortUlong(ulRegister, ulValue);
    }
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    u8 bValue;

    if (ulRegister & ~PORT_MASK) {
        NdisReadRegisterUchar(ulRegister, &bValue);
    } else {
        NdisRawReadPortUchar(ulRegister, &bValue);
    }

    DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, bValue));
    return bValue;
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    DPrintf(6, ("[%s]R[%x]=%x", __FUNCTION__, (ULONG)ulRegister, bValue));

    if (ulRegister & ~PORT_MASK) {
        NdisWriteRegisterUchar((PUCHAR)ulRegister, bValue);
    } else {
        NdisRawWritePortUchar(ulRegister, bValue);
    }
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    u16 wValue;

    if (ulRegister & ~PORT_MASK) {
        NdisReadRegisterUshort(ulRegister, &wValue);
    } else {
        NdisRawReadPortUshort(ulRegister, &wValue);
    }

    DPrintf(6, ("[%s]R[%x]=%x\n", __FUNCTION__, (ULONG)ulRegister, wValue));
    return wValue;
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
#if 1
    if (ulRegister & ~PORT_MASK) {
        NdisWriteRegisterUshort((PUSHORT)ulRegister, wValue);
    } else {
        NdisRawWritePortUshort(ulRegister, wValue);
    }
#else
    // test only to cause long TX waiting queue of NDIS packets
    // to recognize it and request for reset via Hang handler
    static int nCounterToFail = 0;
    static const int StartFail = 200, StopFail = 600;
    BOOLEAN bFail = FALSE;
    DPrintf(6, ("%s> R[%x] = %x\n", __FUNCTION__, (ULONG)ulRegister, wValue));
    if ((ulRegister & 0x1F) == 0x10)
    {
        nCounterToFail++;
        bFail = nCounterToFail >= StartFail && nCounterToFail < StopFail;
    }
    if (!bFail) NdisRawWritePortUshort(ulRegister, wValue);
    else
    {
        DPrintf(0, ("%s> FAILING R[%x] = %x\n", __FUNCTION__, (ULONG)ulRegister, wValue));
    }
#endif
}

static void *alloc_pages_exact(void *context, size_t size)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;
    PVOID retVal = NULL;
    ULONG i;

    /* find the first unused memory range of the requested size */
    for (i = 0; i < MAX_NUM_OF_QUEUES; i++) {
        if (pContext->SharedMemoryRanges[i].pBase != NULL &&
            pContext->SharedMemoryRanges[i].bUsed == FALSE &&
            pContext->SharedMemoryRanges[i].uLength == (ULONG)size) {
            retVal = pContext->SharedMemoryRanges[i].pBase;
            pContext->SharedMemoryRanges[i].bUsed = TRUE;
            break;
        }
    }

    if (!retVal) {
        /* find the first null memory range descriptor and allocate */
        for (i = 0; i < MAX_NUM_OF_QUEUES; i++) {
            if (pContext->SharedMemoryRanges[i].pBase == NULL) {
                break;
            }
        }
        if (i < MAX_NUM_OF_QUEUES) {
            NdisMAllocateSharedMemory(
                pContext->MiniportHandle,
                (ULONG)size,
                TRUE /* Cached */,
                &pContext->SharedMemoryRanges[i].pBase,
                &pContext->SharedMemoryRanges[i].BasePA);
            retVal = pContext->SharedMemoryRanges[i].pBase;
            if (retVal) {
                NdisZeroMemory(retVal, size);
                pContext->SharedMemoryRanges[i].uLength = (ULONG)size;
                pContext->SharedMemoryRanges[i].bUsed = TRUE;
            }
        }
    }

    if (retVal) {
        DPrintf(6, ("[%s] returning %p, size %x\n", __FUNCTION__, retVal, (ULONG)size));
    } else {
        DPrintf(0, ("[%s] failed to allocate size %x\n", __FUNCTION__, (ULONG)size));
    }
    return retVal;
}

static void free_pages_exact(void *context, void *virt, size_t size)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;
    ULONG i;

    for (i = 0; i < MAX_NUM_OF_QUEUES; i++) {
        if (pContext->SharedMemoryRanges[i].pBase == virt) {
            pContext->SharedMemoryRanges[i].bUsed = FALSE;
            break;
        }
    }

    if (i < MAX_NUM_OF_QUEUES) {
        DPrintf(6, ("[%s] freed %p at index %d\n", __FUNCTION__, virt, i));
    } else {
        DPrintf(0, ("[%s] failed to free %p\n", __FUNCTION__, virt));
    }
}

static ULONGLONG virt_to_phys(void *context, void *address)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;
    ULONG_PTR uAddr = (ULONG_PTR)address;
    ULONG i;

    for (i = 0; i < MAX_NUM_OF_QUEUES; i++) {
        ULONG_PTR uBase = (ULONG_PTR)pContext->SharedMemoryRanges[i].pBase;
        if (uAddr >= uBase && uAddr < (uBase + pContext->SharedMemoryRanges[i].uLength)) {
            ULONGLONG retVal = pContext->SharedMemoryRanges[i].BasePA.QuadPart + (uAddr - uBase);
 
            DPrintf(6, ("[%s] translated %p to %I64X\n", __FUNCTION__, address, retVal));
            return retVal;
        }
    }

    DPrintf(0, ("[%s] failed to translate %p\n", __FUNCTION__, address));
    return 0;
}

static void *kmalloc(void *context, size_t size)
{
    PVOID retVal;

    if (NdisAllocateMemoryWithTag(
        &retVal,
        (UINT)size,
        PARANDIS_MEMORY_TAG) != NDIS_STATUS_SUCCESS) {
        retVal = NULL;
    }

    if (retVal) {
        NdisZeroMemory(retVal, size);
        DPrintf(6, ("[%s] returning %p, len %x\n", __FUNCTION__, retVal, (ULONG)size));
    } else {
        DPrintf(0, ("[%s] failed to allocate size %x\n", __FUNCTION__, (ULONG)size));
    }
    return retVal;
}

static void kfree(void *context, void *addr)
{
    UNREFERENCED_PARAMETER(context);

    NdisFreeMemory(addr, 0, 0);
    DPrintf(6, ("[%s] freed %p\n", __FUNCTION__, addr));
}

static int PCIReadConfig(PPARANDIS_ADAPTER pContext,
                         int where,
                         void *buffer,
                         size_t length)
{
    ULONG read;

    read = NdisReadPciSlotInformation(
        pContext->MiniportHandle,
        0 /* SlotNumber */,
        where,
        buffer,
        (ULONG)length);

    if (read == length) {
        DPrintf(6, ("[%s] read %d bytes at %d\n", __FUNCTION__, read, where));
        return 0;
    } else {
        DPrintf(0, ("[%s] failed to read %d bytes at %d\n", __FUNCTION__, read, where));
        return -1;
    }
}

static int pci_read_config_byte(void *context, int where, u8 *bVal)
{
    return PCIReadConfig((PPARANDIS_ADAPTER)context, where, bVal, sizeof(*bVal));
}

static int pci_read_config_word(void *context, int where, u16 *wVal)
{
    return PCIReadConfig((PPARANDIS_ADAPTER)context, where, wVal, sizeof(*wVal));
}

static int pci_read_config_dword(void *context, int where, u32 *dwVal)
{
    return PCIReadConfig((PPARANDIS_ADAPTER)context, where, dwVal, sizeof(*dwVal));
}

static void msleep(void *context, unsigned int msecs)
{
    UNREFERENCED_PARAMETER(context);

    NdisMSleep(1000 * msecs);
}

static size_t pci_resource_len(void *context, int bar)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;

    if (bar < PCI_TYPE0_ADDRESSES) {
        return pContext->AdapterResources.PciBars[bar].uLength;
    }

    DPrintf(0, ("[%s] queried invalid BAR %d\n", __FUNCTION__, bar));
    return 0;
}

static u32 pci_resource_flags(void *context, int bar)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;

    if (bar < PCI_TYPE0_ADDRESSES) {
        return (pContext->AdapterResources.PciBars[bar].bPortSpace ? IORESOURCE_IO : IORESOURCE_MEM);
    }

    DPrintf(0, ("[%s] queried invalid BAR %d\n", __FUNCTION__, bar));
    return 0;
}

static void *pci_iomap_range(void *context, int bar, size_t offset, size_t maxlen)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;

    if (bar < PCI_TYPE0_ADDRESSES) {
        tBusResource *pRes = &pContext->AdapterResources.PciBars[bar];
        if (pRes->pBase == NULL) {
            /* BAR not mapped yet */
            if (pRes->bPortSpace) {
                if (NDIS_STATUS_SUCCESS == NdisMRegisterIoPortRange(
                    &pRes->pBase,
                    pContext->MiniportHandle,
                    pRes->BasePA.LowPart,
                    pRes->uLength)) {
                    DPrintf(6, ("[%s] mapped port BAR at %x\n", __FUNCTION__, pRes->BasePA.LowPart));
                } else {
                    pRes->pBase = NULL;
                    DPrintf(0, ("[%s] failed to map port BAR at %x\n", __FUNCTION__, pRes->BasePA.LowPart));
                }
            } else {
                if (NDIS_STATUS_SUCCESS == NdisMMapIoSpace(
                    &pRes->pBase,
                    pContext->MiniportHandle,
                    pRes->BasePA,
                    pRes->uLength)) {
                    DPrintf(6, ("[%s] mapped memory BAR at %I64x\n", __FUNCTION__, pRes->BasePA.QuadPart));
                } else {
                    pRes->pBase = NULL;
                    DPrintf(0, ("[%s] failed to map memory BAR at %I64x\n", __FUNCTION__, pRes->BasePA.QuadPart));
                }
            }
        }
        if (pRes->pBase != NULL && offset < pRes->uLength) {
            if (pRes->bPortSpace) {
                /* use physical address for port I/O */
                return (PUCHAR)(ULONG_PTR)pRes->BasePA.LowPart + offset;
            } else {
                /* use virtual address for memory I/O */
                return (PUCHAR)pRes->pBase + offset;
            }
        } else {
            DPrintf(0, ("[%s] failed to get map BAR %d, offset %x\n", __FUNCTION__, bar, offset));
        }
    } else {
        DPrintf(0, ("[%s] queried invalid BAR %d\n", __FUNCTION__, bar));
    }

    return NULL;
}

static void pci_iounmap(void *context, void *address)
{
    /* We map entire memory/IO regions on demand and unmap all of them on shutdown
     * so nothing to do here.
     */
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(address);
}

static u16 pci_get_msix_vector(void *context, int queue)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)context;
    u16 vector = VIRTIO_MSI_NO_VECTOR;

    /* we don't run on MSI support so this will never be true */
    if (pContext->bUsingMSIX && queue >= 0) {
        vector = (u16)pContext->AdapterResources.Vector;
    }

    return vector;
}

VirtIOSystemOps ParaNdisSystemOps = {
   alloc_pages_exact,
   free_pages_exact,
   virt_to_phys,
   kmalloc,
   kfree,
   pci_read_config_byte,
   pci_read_config_word,
   pci_read_config_dword,
   pci_resource_len,
   pci_resource_flags,
   pci_get_msix_vector,
   pci_iomap_range,
   pci_iounmap,
   msleep,
};
