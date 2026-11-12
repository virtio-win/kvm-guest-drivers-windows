/*
 * Copyright (C) 2019-2020 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
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

#pragma once
#include "viogpu.h"

#pragma pack(1)
typedef struct virtio_gpu_config
{
    u32 events_read;
    u32 events_clear;
    u32 num_scanouts;
    u32 num_capsets;
} GPU_CONFIG, *PGPU_CONFIG;
#pragma pack()

// #pragma pack(1)
typedef struct virtio_gpu_vbuffer
{
    char *buf;
    int size;

    void *data_buf;
    u32 data_size;

    char *resp_buf;
    int resp_size;
    LIST_ENTRY list_entry;

    void (*complete_cb)(void *ctx);
    void *complete_ctx;

    bool auto_release;
    bool use_indirect;

    // Indirect descriptor table (PVOID since we only need to allocate/free, not access fields)
    PVOID desc;
    PHYSICAL_ADDRESS desc_pa;
} GPU_VBUFFER, *PGPU_VBUFFER;
// #pragma pack()

#define MAX_INLINE_CMD_SIZE  96
#define MAX_INLINE_RESP_SIZE 24
#define VBUFFER_SIZE         (sizeof(GPU_VBUFFER) + MAX_INLINE_CMD_SIZE + MAX_INLINE_RESP_SIZE)

class VioGpuBuf
{
  public:
    VioGpuBuf();
    ~VioGpuBuf();
    PGPU_VBUFFER GetBuf(_In_ int size, _In_ int resp_size, _In_opt_ void *resp_buf);
    void FreeBuf(_In_ PGPU_VBUFFER pbuf);
    BOOLEAN Init(_In_ UINT cnt);
    BOOLEAN AllocateIndirectDescriptors(_In_ PGPU_VBUFFER pbuf, _In_ SIZE_T dataSize);

  private:
    void Close(void);
    void DeleteBuffer(_In_ PGPU_VBUFFER pbuf);

  private:
    LIST_ENTRY m_FreeBufs;
    LIST_ENTRY m_InUseBufs;
    KSPIN_LOCK m_SpinLock;
    UINT m_uCount;
    UINT m_uCountMin = 0;
};

// Contiguous memory allocation fallback chain: 1MB -> 64KB -> 32KB -> 16KB -> 4KB
//
// Why these specific sizes:
// - 1MB:  Segment Heap internal segment size; optimal for large allocations
// - 64KB: VirtualAlloc allocation granularity on all Windows platforms
// - 32KB: Intermediate fallback for moderate fragmentation
// - 16KB: Final fallback with safe SGL margin (8K res: 8192 entries, 50% headroom)
// - 4KB:  Page size, minimum allocation unit
//
// 8KB is SKIPPED: Windows Buddy System merges adjacent free blocks,
// so if 16KB fails, isolated 8KB fragments are unlikely.
// Also 8KB leaves only 0.78% SGL margin (16256/16384 entries) - too risky for production use.
//
static const SIZE_T g_ContiguousBlockSizes[] = {1024 * 1024, 64 * 1024, 32 * 1024, 16 * 1024, PAGE_SIZE};
#define CONTIGUOUS_BLOCK_SIZE_COUNT    ARRAYSIZE(g_ContiguousBlockSizes)

// QEMU hard limit for nr_entries in virtio_gpu_create_mapping_iov()
// Reference: https://github.com/qemu/qemu/blob/master/hw/display/virtio-gpu.c
#define VIRTIO_GPU_MAX_BACKING_ENTRIES 16384

class VioGpuMemSegment
{
  public:
    VioGpuMemSegment(void);
    ~VioGpuMemSegment(void);
    SIZE_T GetSize(void)
    {
        return m_Size;
    }
    PVOID GetVirtualAddress(void)
    {
        return m_pVAddr;
    }
    PHYSICAL_ADDRESS GetPhysicalAddress(void);
    PSCATTER_GATHER_LIST GetSGList(void)
    {
        return m_pSGList;
    }
    BOOLEAN Init(_In_ UINT size, _In_opt_ CPciBar *pBar = NULL, _In_ BOOLEAN singleBlock = FALSE);
    BOOLEAN IsSystemMemory(void)
    {
        return m_bSystemMemory;
    }
    void Close(void);
    BOOLEAN Merge(SIZE_T targetSize, CPciBar *pBar, SIZE_T fixedBlockSize = 0);
    void TakeFrom(VioGpuMemSegment &other);

  private:
    BOOLEAN ExpandSystemMemory(SIZE_T targetSize, SIZE_T fixedBlockSize = 0);
    BOOLEAN ShrinkSystemMemory(SIZE_T targetSize);
    BOOLEAN RebuildMapping();
    BOOLEAN RebuildSGList();
    void CleanMapping();
    void CleanSGList();
    BOOLEAN AllocateBar(PHYSICAL_ADDRESS pAddr, SIZE_T size, PVOID *pVAddr, PMDL *pMdl, PSCATTER_GATHER_LIST *pSGList);
    void CloseBar();
    void CloseSystemMemory();
    BOOLEAN m_bSystemMemory;
    BOOLEAN m_bMapped;
    PSCATTER_GATHER_LIST m_pSGList;
    PVOID m_pVAddr;
    PMDL m_pMdl;
    SIZE_T m_Size;
    // Multi-block allocation support
    PVOID *m_pBlocks;      // Array of block virtual addresses
    SIZE_T *m_pBlockSizes; // Array of block sizes (may vary due to fallback)
    UINT m_nBlocks;        // Number of allocated blocks
};

class VioGpuObj
{
  public:
    VioGpuObj(void);
    ~VioGpuObj(void);
    void SetId(_In_ UINT id)
    {
        m_uiHwRes = id;
    }
    UINT GetId(void)
    {
        return m_uiHwRes;
    }
    BOOLEAN Init(_In_ UINT size, VioGpuMemSegment *pSegment);
    SIZE_T GetSize(void)
    {
        return m_Size;
    }
    PSCATTER_GATHER_LIST GetSGList(void)
    {
        return m_pSegment ? m_pSegment->GetSGList() : NULL;
    }
    PHYSICAL_ADDRESS GetPhysicalAddress(void)
    {
        PHYSICAL_ADDRESS pa = {0};
        return m_pSegment ? m_pSegment->GetPhysicalAddress() : pa;
    }
    PVOID GetVirtualAddress(void)
    {
        return m_pSegment ? m_pSegment->GetVirtualAddress() : NULL;
    }

  private:
    UINT m_uiHwRes;
    SIZE_T m_Size;
    VioGpuMemSegment *m_pSegment;
};

class VioGpuQueue
{
  public:
    VioGpuQueue();
    ~VioGpuQueue();
    BOOLEAN Init(_In_ VirtIODevice *pVIODevice, _In_ struct virtqueue *pVirtQueue, _In_ UINT index);
    void Close(void);
    int AddBuf(_In_ struct VirtIOBufferDescriptor sg[],
               _In_ UINT out_num,
               _In_ UINT in_num,
               _In_ void *data,
               _In_opt_ void *va_indirect,
               _In_ ULONGLONG phys_indirect)
    {
        return m_pVirtQueue ? virtqueue_add_buf(m_pVirtQueue, sg, out_num, in_num, data, va_indirect, phys_indirect)
                            : 0;
    }
    void *GetBuf(_Out_ UINT *len)
    {
        if (m_pVirtQueue)
        {
            return virtqueue_get_buf(m_pVirtQueue, len);
        }
        *len = 0;
        return NULL;
    }
    void Kick()
    {
        if (m_pVirtQueue)
        {
            virtqueue_kick_always(m_pVirtQueue);
        }
    }
    bool EnableInterrupt(void)
    {
        return m_pVirtQueue ? virtqueue_enable_cb(m_pVirtQueue) : false;
    }
    VOID DisableInterrupt(void)
    {
        if (m_pVirtQueue)
        {
            virtqueue_disable_cb(m_pVirtQueue);
        }
    }
    UINT QueryAllocation();
    void SetGpuBuf(_In_ VioGpuBuf *pbuf)
    {
        m_pBuf = pbuf;
    }
    void ReleaseBuffer(PGPU_VBUFFER buf);

  protected:
    _IRQL_requires_max_(DISPATCH_LEVEL) _IRQL_saves_global_(OldIrql,
                                                            Irql) _IRQL_raises_(DISPATCH_LEVEL) void Lock(KIRQL *Irql);
    _IRQL_requires_(DISPATCH_LEVEL) _IRQL_restores_global_(OldIrql, Irql) void Unlock(KIRQL Irql);

  private:
    struct virtqueue *m_pVirtQueue;
    VirtIODevice *m_pVIODevice;
    UINT m_Index;
    KSPIN_LOCK m_SpinLock;

  protected:
    VioGpuBuf *m_pBuf;
};

class CtrlQueue : public VioGpuQueue
{
  public:
    CtrlQueue() : VioGpuQueue()
    {
        m_FenceIdr = 0;
    };

    PVOID AllocCmd(PGPU_VBUFFER *buf, int sz);
    PVOID AllocCmdResp(PGPU_VBUFFER *buf, int cmd_sz, PVOID resp_buf, int resp_sz);

    UINT QueueBuffer(PGPU_VBUFFER buf);
    PGPU_VBUFFER DequeueBuffer(_Out_ UINT *len);

    void CreateResource(UINT res_id, UINT format, UINT width, UINT height);
    BOOLEAN CreateResourceSync(UINT res_id, UINT format, UINT width, UINT height);
    void DestroyResource(UINT res_id);
    void DestroyResourceSync(UINT res_id);
    void SetScanout(UINT scan_id, UINT res_id, UINT width, UINT height, UINT x, UINT y);
    void ResFlush(UINT res_id, UINT width, UINT height, UINT x, UINT y);
    void TransferToHost2D(UINT res_id, ULONG offset, UINT width, UINT height, UINT x, UINT y);
    BOOLEAN AttachBacking(UINT res_id, PGPU_MEM_ENTRY ents, UINT nents);
    void DetachBacking(UINT res_id);
    void DetachBackingSync(UINT res_id);

    BOOLEAN GetDisplayInfo(PGPU_VBUFFER buf, UINT id, PULONG xres, PULONG yres);
    BOOLEAN AskDisplayInfo(PGPU_VBUFFER *buf);
    BOOLEAN AskEdidInfo(PGPU_VBUFFER *buf, UINT id);
    BOOLEAN GetEdidInfo(PGPU_VBUFFER buf, UINT id, PBYTE edid);

  private:
    volatile LONG m_FenceIdr;
};

class CrsrQueue : public VioGpuQueue
{
  public:
    PVOID AllocCursor(PGPU_VBUFFER *buf);
    UINT QueueCursor(PGPU_VBUFFER buf);
    PGPU_VBUFFER DequeueCursor(_Out_ UINT *len);
};
