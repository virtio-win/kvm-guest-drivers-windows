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
#include "virtio.h"
#include "VirtIOWdf.h"
#include "VirtIODmb.h"
#include "private.h"
#include <devpropdef.h>

static EVT_WDF_OBJECT_CONTEXT_DESTROY OnDmaTransactionDestroy;
static EVT_WDF_PROGRAM_DMA OnDmaTransactionProgramDma;

/*
 * Device Memory Buffer paths
 *
 * When the device accepted VIRTIO_F_DMB, every address it sees must be an
 * offset into the DMB, so the allocators hand out DMB runs instead of common
 * buffers and report the DMB offset as the "physical address". The runs are
 * tracked in pWdfDriver->DmbBlocks rather than the WDFCOMMONBUFFER
 * collection. Non-DMB devices never enter these paths.
 */
static BOOLEAN DmbActive(PVIRTIO_WDF_DRIVER pWdfDriver)
{
    return virtio_dmb_is_active(&pWdfDriver->VIODevice);
}

static void *AllocateDmbBlock(PVIRTIO_WDF_DRIVER pWdfDriver, size_t size, ULONG groupTag)
{
    VirtIODevice *vdev = &pWdfDriver->VIODevice;
    PVIRTIO_WDF_DMB_BLOCK block;
    ULONGLONG offset;
    void *va;

    /* Match AllocateCommonBuffer: PASSIVE only. */
    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        DPrintf(0, "%s FAILED(irql)\n", __FUNCTION__);
        return NULL;
    }
    block = ExAllocatePoolUninitialized(NonPagedPoolNx, sizeof(*block), pWdfDriver->MemoryTag);
    if (block == NULL) {
        return NULL;
    }
    va = virtio_dmb_alloc(vdev, size, &offset);
    if (va == NULL) {
        DPrintf(0, "%s FAILED(dmb exhausted, size 0x%Ix)\n", __FUNCTION__, size);
        ExFreePoolWithTag(block, pWdfDriver->MemoryTag);
        return NULL;
    }
    /* Keep AllocateCommonBuffer's zero-on-alloc behaviour. The block is
     * device-writable, so zeroing it before it is posted also keeps private
     * data out of the device's reach. */
    RtlZeroMemory(va, size);

    block->VirtualAddress = va;
    block->Length = size;
    block->groupTag = groupTag;

    WdfSpinLockAcquire(pWdfDriver->DmaSpinlock);
    InsertTailList(&pWdfDriver->DmbBlocks, &block->ListEntry);
    WdfSpinLockRelease(pWdfDriver->DmaSpinlock);

    DPrintf(1, "%s done %p@%I64x(tag %08X), size 0x%x\n", __FUNCTION__, va, offset, groupTag,
            (ULONG)size);
    return va;
}

/* Free a DMB run by any VA inside it. Returns FALSE when no run matches. */
static BOOLEAN FreeDmbBlock(PVIRTIO_WDF_DRIVER pWdfDriver, void *va)
{
    ULONG_PTR p = (ULONG_PTR)va;
    PLIST_ENTRY entry;
    PVIRTIO_WDF_DMB_BLOCK block = NULL;

    WdfSpinLockAcquire(pWdfDriver->DmaSpinlock);
    for (entry = pWdfDriver->DmbBlocks.Flink; entry != &pWdfDriver->DmbBlocks;
         entry = entry->Flink) {
        PVIRTIO_WDF_DMB_BLOCK b = CONTAINING_RECORD(entry, VIRTIO_WDF_DMB_BLOCK, ListEntry);
        ULONG_PTR start = (ULONG_PTR)b->VirtualAddress;
        if (p >= start && p < start + b->Length) {
            block = b;
            RemoveEntryList(entry);
            break;
        }
    }
    WdfSpinLockRelease(pWdfDriver->DmaSpinlock);
    if (block == NULL) {
        return FALSE;
    }
    virtio_dmb_free(&pWdfDriver->VIODevice, block->VirtualAddress, block->Length);
    ExFreePoolWithTag(block, pWdfDriver->MemoryTag);
    return TRUE;
}

/* Free every DMB run with a matching group tag. */
static void FreeDmbBlocksByTag(PVIRTIO_WDF_DRIVER pWdfDriver, ULONG groupTag)
{
    for (;;) {
        PLIST_ENTRY entry;
        PVIRTIO_WDF_DMB_BLOCK block = NULL;

        WdfSpinLockAcquire(pWdfDriver->DmaSpinlock);
        for (entry = pWdfDriver->DmbBlocks.Flink; entry != &pWdfDriver->DmbBlocks;
             entry = entry->Flink) {
            PVIRTIO_WDF_DMB_BLOCK b = CONTAINING_RECORD(entry, VIRTIO_WDF_DMB_BLOCK, ListEntry);
            if (b->groupTag == groupTag) {
                block = b;
                RemoveEntryList(entry);
                break;
            }
        }
        WdfSpinLockRelease(pWdfDriver->DmaSpinlock);
        if (block == NULL) {
            return;
        }
        virtio_dmb_free(&pWdfDriver->VIODevice, block->VirtualAddress, block->Length);
        ExFreePoolWithTag(block, pWdfDriver->MemoryTag);
    }
}

static void *AllocateCommonBuffer(PVIRTIO_WDF_DRIVER pWdfDriver, size_t size, ULONG groupTag)
{
    NTSTATUS status;
    WDFCOMMONBUFFER commonBuffer;
    PVIRTIO_WDF_MEMORY_BLOCK_CONTEXT context;
    WDF_OBJECT_ATTRIBUTES attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, VIRTIO_WDF_MEMORY_BLOCK_CONTEXT);

    if (DmbActive(pWdfDriver)) {
        return AllocateDmbBlock(pWdfDriver, size, groupTag);
    }

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

    DPrintf(1, "%s done %p@%I64x(tag %08X), size 0x%x\n", __FUNCTION__, context->pVirtualAddress,
            context->PhysicalAddress.QuadPart, context->groupTag, (ULONG)size);

    return context->pVirtualAddress;
}

void *VirtIOWdfDeviceAllocDmaMemory(VirtIODevice *vdev, size_t size, ULONG groupTag)
{
    return AllocateCommonBuffer(vdev->DeviceContext, size, groupTag);
}

static BOOLEAN FindCommonBuffer(PVIRTIO_WDF_DRIVER pWdfDriver, void *p, PHYSICAL_ADDRESS *ppa,
                                size_t *pOffset, BOOLEAN bRemoval)
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
    } else if (bRemoval) {
        if (KeGetCurrentIrql() == PASSIVE_LEVEL) {
            WdfSpinLockAcquire(pWdfDriver->DmaSpinlock);
            WdfCollectionRemove(pWdfDriver->MemoryBlockCollection, obj);
            WdfSpinLockRelease(pWdfDriver->DmaSpinlock);

            WdfObjectDelete(obj);
            DPrintf(1, "%s %p freed (%d common buffers)\n", __FUNCTION__, va, n - 1);
        } else {
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
    if (DmbActive(pWdfDriver)) {
        u64 dmbOffset = 0;

        if (!virtio_dmb_va_to_offset(&pWdfDriver->VIODevice, va, 1, &dmbOffset)) {
            DPrintf(0, "%s: %p is not in the DMB\n", __FUNCTION__, va);
        }
        pa.QuadPart = (LONGLONG)dmbOffset;
        return pa;
    }
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
    PVIRTIO_WDF_DRIVER pWdfDriver = vdev->DeviceContext;
    PHYSICAL_ADDRESS pa;
    size_t offset;
    if (DmbActive(pWdfDriver)) {
        FreeDmbBlock(pWdfDriver, va);
        return;
    }
    FindCommonBuffer(pWdfDriver, va, &pa, &offset, TRUE);
}

static BOOLEAN FindCommonBufferByTag(PVIRTIO_WDF_DRIVER pWdfDriver, ULONG tag)
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
    if (DmbActive(vdev->DeviceContext)) {
        FreeDmbBlocksByTag(vdev->DeviceContext, groupTag);
        return;
    }
    while (FindCommonBufferByTag(vdev->DeviceContext, groupTag))
        ;
}

static void FreeSlicedBlock(PVIRTIO_DMA_MEMORY_SLICED p)
{
    size_t offset;
    if (DmbActive(p->drv)) {
        FreeDmbBlock(p->drv, p->va);
    } else {
        FindCommonBuffer(p->drv, p->va, &p->pa, &offset, TRUE);
    }
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
    if (DmbActive(p->drv)) {
        /* The block is one DMB run starting at p->va; the slice offset is the
         * distance from it. GetPhysicalAddress would also work, but this
         * needs no lookup. */
        offset = (size_t)((PUCHAR)va - (PUCHAR)p->va);
    } else if (!FindCommonBuffer(p->drv, va, &pa, &offset, FALSE)) {
        DPrintf(0, "%s: block with va %p not found\n", __FUNCTION__, va);
        return;
    }
    if (offset % p->slice) {
        DPrintf(0, "%s: offset %d is wrong for slice %d\n", __FUNCTION__, (ULONG)offset, p->slice);
        return;
    }
    ULONG index = (ULONG)(offset / p->slice);
    if (!RtlTestBit(&p->bitmap, index)) {
        DPrintf(0, "%s: bit %d is NOT set\n", __FUNCTION__, index);
        return;
    }
    RtlClearBit(&p->bitmap, index);
}

PVIRTIO_DMA_MEMORY_SLICED VirtIOWdfDeviceAllocDmaMemorySliced(VirtIODevice *vdev, size_t blockSize,
                                                              ULONG sliceSize)
{
    PVIRTIO_WDF_DRIVER pWdfDriver = vdev->DeviceContext;
    size_t allocSize =
        sizeof(VIRTIO_DMA_MEMORY_SLICED) + (blockSize / sliceSize) / 8 + sizeof(ULONG);
    PVIRTIO_DMA_MEMORY_SLICED p =
        ExAllocatePoolUninitialized(NonPagedPool, allocSize, pWdfDriver->MemoryTag);
    if (!p) {
        return NULL;
    }
    __analysis_assume(allocSize > sizeof(*p));
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
    p->destroy = FreeSlicedBlock;
    return p;
}

VOID OnDmaTransactionDestroy(WDFOBJECT Object)
{
    PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx = GetDmaTransactionContext(Object);
    DPrintf(1, "%s %p\n", __FUNCTION__, Object);
    // the MDL is one we allocated for the buffer
    // if there is no buffer - this is the MDL provided by the caller
    if (ctx->mdl && ctx->buffer) {
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

BOOLEAN OnDmaTransactionProgramDma(WDFDMATRANSACTION Transaction, WDFDEVICE Device,
                                   WDFCONTEXT Context, WDF_DMA_DIRECTION Direction,
                                   PSCATTER_GATHER_LIST SgList)
{
    PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx = GetDmaTransactionContext(Transaction);
    RefTransaction(ctx);
    ctx->parameters.transaction = Transaction;
    ctx->parameters.sgList = SgList;
    DPrintf(1, "-->%s %p %d frags\n", __FUNCTION__, Transaction, SgList->NumberOfElements);
    BOOLEAN bFailed = !ctx->callback(&ctx->parameters);
    DPrintf(1, "<--%s %s\n", __FUNCTION__, bFailed ? "Failed" : "OK");
    DerefTransaction(ctx);
    return TRUE;
}

/* Copy between the bounce of a transfer and an MDL chain, up to @max bytes.
 * toDmb drives the direction. Returns the number of bytes copied. */
static size_t CopyMdlChain(VirtIODevice *vdev, VirtIODmbReq *req, PMDL mdl, size_t max,
                           BOOLEAN toDmb)
{
    size_t done = 0;

    for (; mdl != NULL && done < max; mdl = mdl->Next) {
        void *va = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);
        size_t n = MmGetMdlByteCount(mdl);

        if (va == NULL) {
            break;
        }
        if (n > max - done) {
            n = max - done;
        }
        if (toDmb) {
            if (!virtio_dmb_req_write(vdev, req, 0, (ULONG)done, va, (ULONG)n)) {
                break;
            }
        } else {
            /* The engine stops at the end of what we posted. */
            n = virtio_dmb_req_read(vdev, req, 0, (ULONG)done, va, (ULONG)n);
            if (n == 0) {
                break;
            }
        }
        done += n;
    }
    return done;
}

/* Bounce an async transfer through the DMB: map it as one segment with the
 * request engine and hand the callback a one-element scatter/gather list
 * pointing at the run. The result is copied back out on completion. Mirrors
 * the contract of VirtIOWdfDeviceDmaAsync: once the callback is invoked this
 * returns STATUS_SUCCESS; on failure the callback is never called. */
static NTSTATUS DmbDmaAsync(VirtIODevice *vdev, PVIRTIO_DMA_TRANSACTION_PARAMS params,
                            VirtIOWdfDmaTransactionCallback callback, WDF_DMA_DIRECTION Direction)
{
    PVIRTIO_WDF_DRIVER pWdfDriver = vdev->DeviceContext;
    BOOLEAN isWrite = (Direction == WdfDmaDirectionWriteToDevice);
    PMDL mdlChain = NULL;
    VirtIODmbSeg seg = { 0 };
    struct VirtIOBufferDescriptor sg;
    unsigned out, in;
    WDFMEMORY handle;
    WDF_OBJECT_ATTRIBUTES attr;
    PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx;
    size_t len = params->size;
    NTSTATUS status;

    /* Resolve the guest-RAM buffer the transfer moves to or from. */
    if (params->req != NULL && params->req != (WDFREQUEST)WDF_INVALID_HANDLE) {
        WDFMEMORY mem;
        status = isWrite ? WdfRequestRetrieveInputMemory(params->req, &mem) :
                           WdfRequestRetrieveOutputMemory(params->req, &mem);
        if (!NT_SUCCESS(status)) {
            DPrintf(0, "%s FAILED(retrieve) %X\n", __FUNCTION__, status);
            return status;
        }
        seg.va = WdfMemoryGetBuffer(mem, &len);
    } else if (params->req == (WDFREQUEST)WDF_INVALID_HANDLE) {
        mdlChain = (PMDL)params->buffer;
    } else {
        seg.va = params->buffer;
    }

    /* A transfer is mapped in one piece, so it may take at most the mapping
     * limit; a larger one never fits. */
    if (len == 0 || len > virtio_dmb_max_mapping(vdev)) {
        DPrintf(0, "%s FAILED(bad length %Iu)\n", __FUNCTION__, len);
        return STATUS_INVALID_BUFFER_SIZE;
    }
    seg.len = (ULONG)len;
    seg.to_device = isWrite;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, VIRTIO_WDF_DMA_TRANSACTION_CONTEXT);
    status =
        WdfMemoryCreate(&attr, NonPagedPoolNx, pWdfDriver->MemoryTag, sizeof(ULONG), &handle, NULL);
    if (!NT_SUCCESS(status)) {
        DPrintf(0, "%s FAILED(handle) %X\n", __FUNCTION__, status);
        return status;
    }
    ctx = GetDmaTransactionContext(handle);
    RtlZeroMemory(ctx, sizeof(*ctx));

    switch (virtio_dmb_req_map(vdev, &ctx->dmbReq, &seg, 1, 0, &sg, 1, &out, &in)) {
    case VIRTIO_DMB_MAPPED:
        break;
    case VIRTIO_DMB_FULL:
        WdfObjectDelete(handle);
        return STATUS_DEVICE_BUSY;
    default:
        WdfObjectDelete(handle);
        return STATUS_INVALID_BUFFER_SIZE;
    }
    /* A short copy would post zeroes as payload. */
    if (isWrite && mdlChain != NULL &&
        CopyMdlChain(vdev, &ctx->dmbReq, mdlChain, len, TRUE) != len) {
        DPrintf(0, "%s FAILED(map MDL)\n", __FUNCTION__);
        virtio_dmb_req_release(vdev, &ctx->dmbReq);
        WdfObjectDelete(handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ctx->parameters = *params;
    ctx->callback = callback;
    ctx->refCount = 1;
    ctx->direction = Direction;
    ctx->bDmb = TRUE;
    ctx->dmbCopyMdl = isWrite ? NULL : mdlChain;
    ctx->dmbSgl.NumberOfElements = 1;
    ctx->dmbSgl.Elements[0].Address.QuadPart = sg.physAddr.QuadPart;
    ctx->dmbSgl.Elements[0].Length = sg.length;
    ctx->parameters.transaction = (WDFDMATRANSACTION)handle;
    ctx->parameters.sgList = (PSCATTER_GATHER_LIST)&ctx->dmbSgl;

    /* The callback posts the descriptor, and the device may complete it on
     * another CPU before the callback returns. Hold a reference across the
     * call, as OnDmaTransactionProgramDma does, so the context outlives the
     * callback. A FALSE return means the callback already completed the
     * transfer; either way it was handed off, so report success. */
    RefTransaction(ctx);
    callback(&ctx->parameters);
    DerefTransaction(ctx);
    return STATUS_SUCCESS;
}

static NTSTATUS VirtIOWdfDeviceDmaAsync(VirtIODevice *vdev, PVIRTIO_DMA_TRANSACTION_PARAMS params,
                                        VirtIOWdfDmaTransactionCallback callback,
                                        WDF_DMA_DIRECTION Direction)
{
    PVIRTIO_WDF_DRIVER pWdfDriver = vdev->DeviceContext;
    WDFDMATRANSACTION tr;
    WDF_OBJECT_ATTRIBUTES attr;
    NTSTATUS status;

    if (DmbActive(pWdfDriver)) {
        return DmbDmaAsync(vdev, params, callback, Direction);
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, VIRTIO_WDF_DMA_TRANSACTION_CONTEXT);
    attr.EvtDestroyCallback = OnDmaTransactionDestroy;
    status = WdfDmaTransactionCreate(pWdfDriver->DmaEnabler, &attr, &tr);
    if (!NT_SUCCESS(status)) {
        DPrintf(0, "%s FAILED(create) %X\n", __FUNCTION__, status);
        return status;
    }
    PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx = GetDmaTransactionContext(tr);
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->parameters = *params;
    ctx->callback = callback;
    ctx->refCount = 1;
    ctx->direction = Direction;
    if (params->req && params->req != (WDFREQUEST)WDF_INVALID_HANDLE) {
        status = WdfDmaTransactionInitializeUsingRequest(tr, params->req,
                                                         OnDmaTransactionProgramDma, Direction);
    } else if (params->req == (WDFREQUEST)WDF_INVALID_HANDLE) {
        status = WdfDmaTransactionInitializeUsingOffset(tr, OnDmaTransactionProgramDma, Direction,
                                                        (PMDL)params->buffer, 0, params->size);
    } else {
        ctx->buffer = ExAllocatePoolUninitialized(NonPagedPool, ctx->parameters.size,
                                                  ctx->parameters.allocationTag);
        if (ctx->buffer) {
            if (Direction == WdfDmaDirectionWriteToDevice) {
                RtlCopyMemory(ctx->buffer, params->buffer, params->size);
            }
            ctx->mdl = IoAllocateMdl(ctx->buffer, params->size, FALSE, FALSE, NULL);
            if (ctx->mdl) {
                MmBuildMdlForNonPagedPool(ctx->mdl);
                status = WdfDmaTransactionInitialize(tr, OnDmaTransactionProgramDma, Direction,
                                                     ctx->mdl, ctx->buffer, params->size);
            } else {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
        } else {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (!NT_SUCCESS(status)) {
        DPrintf(0, "%s FAILED(init) %X\n", __FUNCTION__, status);
        WdfObjectDelete(tr);
        return status;
    }

    status = WdfDmaTransactionExecute(tr, NULL);
    if (!NT_SUCCESS(status)) {
        DPrintf(0, "%s FAILED(execution) %X\n", __FUNCTION__, status);
        WdfObjectDelete(tr);
        return status;
    }

    return STATUS_SUCCESS;
}

BOOLEAN VirtIOWdfDeviceDmaTxAsync(VirtIODevice *vdev, PVIRTIO_DMA_TRANSACTION_PARAMS params,
                                  VirtIOWdfDmaTransactionCallback callback)
{
    return NT_SUCCESS(
        VirtIOWdfDeviceDmaAsync(vdev, params, callback, WdfDmaDirectionWriteToDevice));
}

BOOLEAN VirtIOWdfDeviceDmaRxAsync(VirtIODevice *vdev, PVIRTIO_DMA_TRANSACTION_PARAMS params,
                                  VirtIOWdfDmaTransactionCallback callback)
{
    return NT_SUCCESS(
        VirtIOWdfDeviceDmaAsync(vdev, params, callback, WdfDmaDirectionReadFromDevice));
}

NTSTATUS VirtIOWdfDeviceDmaTxAsyncEx(VirtIODevice *vdev, PVIRTIO_DMA_TRANSACTION_PARAMS params,
                                     VirtIOWdfDmaTransactionCallback callback)
{
    return VirtIOWdfDeviceDmaAsync(vdev, params, callback, WdfDmaDirectionWriteToDevice);
}

NTSTATUS VirtIOWdfDeviceDmaRxAsyncEx(VirtIODevice *vdev, PVIRTIO_DMA_TRANSACTION_PARAMS params,
                                     VirtIOWdfDmaTransactionCallback callback)
{
    return VirtIOWdfDeviceDmaAsync(vdev, params, callback, WdfDmaDirectionReadFromDevice);
}

void VirtIOWdfDeviceDmaTxComplete(VirtIODevice *vdev, WDFDMATRANSACTION transaction)
{
    PVIRTIO_WDF_DRIVER pWdfDriver = vdev->DeviceContext;
    PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx = GetDmaTransactionContext(transaction);
    NTSTATUS status;
    DPrintf(1, "%s %p\n", __FUNCTION__, transaction);
    if (ctx->bDmb) {
        virtio_dmb_req_release(vdev, &ctx->dmbReq);
        DerefTransaction(ctx);
        return;
    }
    WdfDmaTransactionDmaCompletedFinal(transaction, 0, &status);
    DerefTransaction(ctx);
}

ULONG VirtIOWdfDeviceDmaRxComplete(VirtIODevice *vdev, WDFDMATRANSACTION transaction, ULONG length)
{
    PVIRTIO_WDF_DRIVER pWdfDriver = vdev->DeviceContext;
    PVIRTIO_WDF_DMA_TRANSACTION_CONTEXT ctx = GetDmaTransactionContext(transaction);
    NTSTATUS status;
    DPrintf(1, "%s %p, len %d\n", __FUNCTION__, transaction, length);
    if (ctx->bDmb) {
        /* The engine never copies back more than we posted, and a part of an
         * MDL chain that cannot be mapped stays uncopied, so report only what
         * arrived. */
        if (ctx->dmbCopyMdl != NULL) {
            length = (ULONG)CopyMdlChain(vdev, &ctx->dmbReq, ctx->dmbCopyMdl, length, FALSE);
        } else {
            length = virtio_dmb_req_copy_out(vdev, &ctx->dmbReq, 0, length);
        }
        virtio_dmb_req_release(vdev, &ctx->dmbReq);
        DerefTransaction(ctx);
        return length;
    }
    /* length is the used length the device reported. Never copy back more
     * than we posted: the bounce buffer and the caller's buffer are both
     * ctx->parameters.size bytes, so a device that over-reports would
     * otherwise write past the end of both. */
    if (length > ctx->parameters.size) {
        length = ctx->parameters.size;
    }
    WdfDmaTransactionDmaCompletedFinal(transaction, length, &status);
    if (length && ctx->buffer) {
        RtlCopyMemory(ctx->parameters.buffer, ctx->buffer, length);
    }
    DerefTransaction(ctx);
    return length;
}

NTSTATUS VirtIOWdfDeviceCheckIOMMUActive(PVIRTIO_WDF_DRIVER pWdfDriver, WDFDEVICE wdfDev)
{
    ULONGLONG deviceFeatures = VirtIOWdfGetDeviceFeatures(pWdfDriver);
    BOOLEAN bHasFeature = virtio_is_feature_enabled(deviceFeatures, VIRTIO_F_ACCESS_PLATFORM);

    DPrintf(0, "%s: VIRTIO_F_ACCESS_PLATFORM is %s\n", __FUNCTION__,
            bHasFeature ? "set" : "not set");

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/pci/enabling-dma-remapping-for-device-drivers

    const DEVPROPKEY propKey = {
        { 0x83da6326, 0x97a6, 0x4088, { 0x94, 0x53, 0xa1, 0x92, 0x3f, 0x57, 0x3b, 0x29 } }, 18
    };
    ULONG value = 0, reqSize = 0;
    DEVPROPTYPE propType;
    WDF_DEVICE_PROPERTY_DATA propData;
    WDF_DEVICE_PROPERTY_DATA_INIT(&propData, &propKey);
    NTSTATUS status =
        WdfDeviceQueryPropertyEx(wdfDev, &propData, sizeof(value), &value, &reqSize, &propType);
    DPrintf(0, "%s: status %X, dma remap=%d\n", __FUNCTION__, status, value);
    pWdfDriver->IsIoMmuActive = bHasFeature && value == 2;
    if (!NT_SUCCESS(status) || value != 2 || bHasFeature) {
        return STATUS_SUCCESS;
    }
    pWdfDriver->IsIoMmuActive = FALSE;

    // the VIRTIO_F_ACCESS_PLATFORM is not set and there is
    // a possibility of DMA remapping
    WDFCOMMONBUFFER commonBuffer = NULL;
    status = WdfCommonBufferCreate(pWdfDriver->DmaEnabler, PAGE_SIZE, WDF_NO_OBJECT_ATTRIBUTES,
                                   &commonBuffer);
    if (!NT_SUCCESS(status)) {
        DPrintf(0, "%s: Can't allocate common buffer\n", __FUNCTION__);
        return status;
    }

    // let's check whether the physical address returned from common buffer API
    // is the same as returned from plain MmGetPhysicalAddress
    PHYSICAL_ADDRESS pa = WdfCommonBufferGetAlignedLogicalAddress(commonBuffer);
    PVOID va = WdfCommonBufferGetAlignedVirtualAddress(commonBuffer);
    PHYSICAL_ADDRESS plain = MmGetPhysicalAddress(va);
    DPrintf(0, "%s: buffer at %I64X, plain %I64X\n", __FUNCTION__, pa.QuadPart, plain.QuadPart);
    status = plain.QuadPart == pa.QuadPart ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
    WdfObjectDelete(commonBuffer);
    return status;
}
