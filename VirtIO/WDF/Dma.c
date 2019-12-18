/*
 * Implementation of virtio_system_ops VirtioLib callbacks
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
 *
 * Author(s):
 *  Yuri Benditovich <ybendito@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "osdep.h"
#include "virtio_pci.h"
#include "VirtIOWdf.h"
#include "private.h"

static EVT_WDF_OBJECT_CONTEXT_DESTROY OnDmaTransactionDestroy;
static EVT_WDF_PROGRAM_DMA            OnDmaTransactionProgramDma;

static void *AllocateCommonBuffer(PVIRTIO_WDF_DRIVER pWdfDriver, size_t size, ULONG groupTag)
{
    NTSTATUS status;
    WDFCOMMONBUFFER commonBuffer;
    PVIRTIO_WDF_MEMORY_BLOCK_CONTEXT context;
    WDF_OBJECT_ATTRIBUTES attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, VIRTIO_WDF_MEMORY_BLOCK_CONTEXT);

    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        DPrintf(0, "%s FAILED(irql)\n", __FUNCTION__);
        return NULL;
    }
    status = WdfCommonBufferCreate(pWdfDriver->DmaEnabler, size, &attr, &commonBuffer);
    if (!NT_SUCCESS(status)) {
        return NULL;
    }
    WdfSpinLockAcquire(pWdfDriver->DmaSpinlock);
    status = WdfCollectionAdd(pWdfDriver->MemoryBlockCollection, commonBuffer);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(commonBuffer);
        WdfSpinLockRelease(pWdfDriver->DmaSpinlock);
        return NULL;
    }
    context = GetMemoryBlockContext(commonBuffer);
    context->WdfBuffer = commonBuffer;
    context->Length = size;
    context->PhysicalAddress = WdfCommonBufferGetAlignedLogicalAddress(commonBuffer);
    context->pVirtualAddress = WdfCommonBufferGetAlignedVirtualAddress(commonBuffer);
    context->groupTag = groupTag;
    context->bToBeDeleted = FALSE;
    WdfSpinLockRelease(pWdfDriver->DmaSpinlock);
    RtlZeroMemory(context->pVirtualAddress, size);

    DPrintf(1, "%s done %p@%I64x(tag %08X), size 0x%x\n", __FUNCTION__,
        context->pVirtualAddress,
        context->PhysicalAddress.QuadPart,
        context->groupTag,
        (ULONG)size);

    return context->pVirtualAddress;
}

void *VirtIOWdfDeviceAllocDmaMemory(VirtIODevice *vdev, size_t size, ULONG groupTag)
{
    return AllocateCommonBuffer(vdev->DeviceContext, size, groupTag);
}

static BOOLEAN FindCommonBuffer(
    PVIRTIO_WDF_DRIVER pWdfDriver,
    void *p,
    PHYSICAL_ADDRESS *ppa,
    size_t *pOffset,
    BOOLEAN bRemoval)
{
    BOOLEAN b = FALSE;
    ULONG_PTR va = (ULONG_PTR)p;
    ULONG i, n;
    WDFOBJECT obj = NULL;
    WdfSpinLockAcquire(pWdfDriver->DmaSpinlock);
    n = WdfCollectionGetCount(pWdfDriver->MemoryBlockCollection);
    for (i = 0; i < n; ++i) {
        obj = WdfCollectionGetItem(pWdfDriver->MemoryBlockCollection, i);
        if (!obj) {
            break;
        }
        PVIRTIO_WDF_MEMORY_BLOCK_CONTEXT context = GetMemoryBlockContext(obj);
        if (context->bToBeDeleted && !bRemoval) {
            continue;
        }
        ULONG_PTR currentVaStart = (ULONG_PTR)context->pVirtualAddress;
        if (va >= currentVaStart && va < (currentVaStart + context->Length)) {
            *ppa = context->PhysicalAddress;
            *pOffset = va - currentVaStart;
            b = TRUE;
            if (bRemoval) {
                b = *pOffset == 0;
                if (b) {
                    context->bToBeDeleted = TRUE;
                }
            }
            break;
        }
    }
    WdfSpinLockRelease(pWdfDriver->DmaSpinlock);
    if (!b) {
        DPrintf(0, "%s(%s) FAILED!\n", __FUNCTION__, bRemoval ? "Remove" : "Locate");
    }
    else if (bRemoval) {
        if (KeGetCurrentIrql() == PASSIVE_LEVEL) {

            WdfSpinLockAcquire(pWdfDriver->DmaSpinlock);
            WdfCollectionRemove(pWdfDriver->MemoryBlockCollection, obj);
            WdfSpinLockRelease(pWdfDriver->DmaSpinlock);

            WdfObjectDelete(obj);
            DPrintf(1, "%s %p freed (%d common buffers)\n", __FUNCTION__, va, n - 1);
        }
        else {
            DPrintf(0, "%s %p marked for deletion\n", __FUNCTION__, va);
        }
    }
    return b;
}

static PHYSICAL_ADDRESS GetPhysicalAddress(PVIRTIO_WDF_DRIVER pWdfDriver, PVOID va)
{
    PHYSICAL_ADDRESS pa;
    size_t offset;
    pa.QuadPart = 0;
    if (FindCommonBuffer(pWdfDriver, va, &pa, &offset, FALSE)) {
        pa.QuadPart += offset;
    }
    return pa;
}

PHYSICAL_ADDRESS VirtIOWdfDeviceGetPhysicalAddress(VirtIODevice *vdev, void *va)
{
    return GetPhysicalAddress(vdev->DeviceContext, va);
}

void VirtIOWdfDeviceFreeDmaMemory(VirtIODevice *vdev, void *va)
{
    PHYSICAL_ADDRESS pa;
    size_t offset;
    FindCommonBuffer(vdev->DeviceContext, va, &pa, &offset, TRUE);
}

static BOOLEAN FindCommonBufferByTag(
    PVIRTIO_WDF_DRIVER pWdfDriver,
    ULONG tag
)
{
    BOOLEAN b = FALSE;
    ULONG i, n;
    WDFOBJECT obj = NULL;
    PVIRTIO_WDF_MEMORY_BLOCK_CONTEXT context = NULL;
    WdfSpinLockAcquire(pWdfDriver->DmaSpinlock);
    n = WdfCollectionGetCount(pWdfDriver->MemoryBlockCollection);
    for (i = 0; i < n; ++i) {
        obj = WdfCollectionGetItem(pWdfDriver->MemoryBlockCollection, i);
        if (!obj) {
            break;
        }
        context = GetMemoryBlockContext(obj);
        if (context->groupTag == tag) {
            b = TRUE;
            break;
        }
    }
    WdfSpinLockRelease(pWdfDriver->DmaSpinlock);
    if (b) {
        DPrintf(1, "%s %p (tag %08X) freed (%d common buffers)\n", __FUNCTION__,
            context->pVirtualAddress, tag, n - 1);
        WdfSpinLockAcquire(pWdfDriver->DmaSpinlock);
        WdfCollectionRemove(pWdfDriver->MemoryBlockCollection, obj);
        WdfSpinLockRelease(pWdfDriver->DmaSpinlock);
        WdfObjectDelete(obj);
    }
    return b;
}

void VirtIOWdfDeviceFreeDmaMemoryByTag(VirtIODevice *vdev, ULONG groupTag)
{
    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        DPrintf(0, "%s FAILED(irql)\n", __FUNCTION__);
        return;
    }
    if (!groupTag) {
        DPrintf(0, "%s FAILED(default tag)\n", __FUNCTION__);
        return;
    }
    if (!vdev->DeviceContext) {
        DPrintf(0, "%s was not initialized\n", __FUNCTION__);
        return;
    }
    while (FindCommonBufferByTag(vdev->DeviceContext, groupTag));
}

static void FreeSlicedBlock(PVIRTIO_DMA_MEMORY_SLICED p)
{
    size_t offset;
    FindCommonBuffer(p->drv, p->va, &p->pa, &offset, TRUE);
    ExFreePoolWithTag(p, p->drv->MemoryTag);
}

static PVOID AllocateSlice(PVIRTIO_DMA_MEMORY_SLICED p, PHYSICAL_ADDRESS *ppa)
{
    ULONG offset, index = RtlFindClearBitsAndSet(&p->bitmap, 1, 0);
    if (index >= p->bitmap.SizeOfBitMap) {
        return NULL;
    }
    offset = p->slice * index;
    ppa->QuadPart = p->pa.QuadPart + offset;
    return (PUCHAR)p->va + offset;
}

static void FreeSlice(PVIRTIO_DMA_MEMORY_SLICED p, PVOID va)
{
    PHYSICAL_ADDRESS pa;
    size_t offset;
    if (!FindCommonBuffer(p->drv, va, &pa, &offset, FALSE)) {
        DPrintf(0, "%s: block with va %p not found\n", __FUNCTION__, va);
        return;
    }
    if (offset % p->slice) {
        DPrintf(0, "%s: offset %d is wrong for slice %d\n", __FUNCTION__,
            (ULONG)offset, p->slice);
        return;
    }
    ULONG index = (ULONG)(offset / p->slice);
    if (!RtlTestBit(&p->bitmap, index)) {
        DPrintf(0, "%s: bit %d is NOT set\n", __FUNCTION__, index);
        return;
    }
    RtlClearBit(&p->bitmap, index);
}

PVIRTIO_DMA_MEMORY_SLICED VirtIOWdfDeviceAllocDmaMemorySliced(
    VirtIODevice *vdev,
    size_t blockSize,
    ULONG sliceSize)
{
    PVIRTIO_WDF_DRIVER pWdfDriver = vdev->DeviceContext;
    size_t allocSize = sizeof(VIRTIO_DMA_MEMORY_SLICED) + (blockSize / sliceSize) / 8 + sizeof(ULONG);
    PVIRTIO_DMA_MEMORY_SLICED p = ExAllocatePoolWithTag(NonPagedPool, allocSize, pWdfDriver->MemoryTag);
    if (!p) {
        return NULL;
    }
    RtlZeroMemory(p, sizeof(*p));
    p->va = AllocateCommonBuffer(pWdfDriver, blockSize, 0);
    p->pa = GetPhysicalAddress(pWdfDriver, p->va);
    if (!p->va || !p->pa.QuadPart) {
        ExFreePoolWithTag(p, pWdfDriver->MemoryTag);
        return NULL;
    }
    p->slice = sliceSize;
    p->drv = pWdfDriver;
    RtlInitializeBitMap(&p->bitmap, p->bitmap_buffer, (ULONG)blockSize / sliceSize);
    p->return_slice = FreeSlice;
    p->get_slice = AllocateSlice;
    p->destroy   = FreeSlicedBlock;
    return p;
}

VOID OnDmaTransactionDestroy(WDFOBJECT Object)
{
    PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx = GetDmaTransactionContext(Object);
    DPrintf(1, "%s %p\n", __FUNCTION__, Object);
    if (ctx->mdl) {
        IoFreeMdl(ctx->mdl);
    }
    if (ctx->buffer) {
        ExFreePoolWithTag(ctx->buffer, ctx->parameters.allocationTag);
    }
}

static FORCEINLINE void RefTransaction(PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx)
{
    InterlockedIncrement(&ctx->refCount);
}

static FORCEINLINE void DerefTransaction(PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx)
{
    if (!InterlockedDecrement(&ctx->refCount)) {
        WdfObjectDelete(ctx->parameters.transaction);
    }
}

BOOLEAN OnDmaTransactionProgramDma(
    WDFDMATRANSACTION Transaction,
    WDFDEVICE Device,
    WDFCONTEXT Context,
    WDF_DMA_DIRECTION Direction,
    PSCATTER_GATHER_LIST SgList
)
{
    PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx = GetDmaTransactionContext(Transaction);
    RefTransaction(ctx);
    ctx->parameters.transaction = Transaction;
    ctx->parameters.sgList = SgList;
    DPrintf(1, "-->%s %p %d frags\n", __FUNCTION__,
        Transaction,
        SgList->NumberOfElements);
    BOOLEAN bFailed = !ctx->callback(&ctx->parameters);
    DPrintf(1, "<--%s %s\n", __FUNCTION__, bFailed ? "Failed" : "OK");
    DerefTransaction(ctx);
    return TRUE;
}

BOOLEAN VirtIOWdfDeviceDmaTxAsync(VirtIODevice *vdev,
    PVIRTIO_DMA_TRANSACTION_PARAMS params,
    VirtIOWdfDmaTransactionCallback callback)
{
    PVIRTIO_WDF_DRIVER pWdfDriver = vdev->DeviceContext;
    WDFDMATRANSACTION tr;
    WDF_OBJECT_ATTRIBUTES attr;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, VIRTIO_WDF_DMA_TRANSACTION_CONTEXT);
    attr.EvtDestroyCallback = OnDmaTransactionDestroy;
    status = WdfDmaTransactionCreate(pWdfDriver->DmaEnabler, &attr, &tr);
    if (!NT_SUCCESS(status)) {
        DPrintf(0, "%s FAILED(create) %X\n", __FUNCTION__, status);
        return FALSE;
    }
    PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx = GetDmaTransactionContext(tr);
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->parameters = *params;
    ctx->callback = callback;
    ctx->refCount = 1;
    if (params->req) {
        status = WdfDmaTransactionInitializeUsingRequest(
            tr, params->req, OnDmaTransactionProgramDma, WdfDmaDirectionWriteToDevice);
    } else {
        ctx->buffer = ExAllocatePoolWithTag(NonPagedPool, ctx->parameters.size, ctx->parameters.allocationTag);
        if (ctx->buffer) {
            RtlCopyMemory(ctx->buffer, params->buffer, params->size);
            ctx->mdl = IoAllocateMdl(ctx->buffer, params->size, FALSE, FALSE, NULL);
        }
        if (ctx->mdl) {
            MmBuildMdlForNonPagedPool(ctx->mdl);
            status = WdfDmaTransactionInitialize(
                tr, OnDmaTransactionProgramDma, WdfDmaDirectionWriteToDevice,
                ctx->mdl, ctx->buffer, params->size);
        } else {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (!NT_SUCCESS(status)) {
        DPrintf(0, "%s FAILED(init) %X\n", __FUNCTION__, status);
        WdfObjectDelete(tr);
        return FALSE;
    }

    status = WdfDmaTransactionExecute(tr, NULL);
    if (!NT_SUCCESS(status)) {
        DPrintf(0, "%s FAILED(execution) %X\n", __FUNCTION__, status);
        WdfObjectDelete(tr);
        return FALSE;
    }

    return TRUE;
}

void VirtIOWdfDeviceDmaTxComplete(VirtIODevice *vdev, WDFDMATRANSACTION transaction)
{
    PVIRTIO_WDF_DRIVER pWdfDriver = vdev->DeviceContext;
    PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx = GetDmaTransactionContext(transaction);
    NTSTATUS status;
    DPrintf(1, "%s %p\n", __FUNCTION__, transaction);
    WdfDmaTransactionDmaCompletedFinal(transaction, 0, &status);
    DerefTransaction(ctx);
}
