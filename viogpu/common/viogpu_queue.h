/*
 * Copyright (C) 2019-2020 Red Hat, Inc.
 * Copyright (C) 2026 Zededa, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
 *             Mikhail Malyshev <mike.malyshev@gmail.com>
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

// The outcome of a command that Send sent: it runs once, from the DPC when the device answers, possibly before Send
// has returned. It never runs for a command Send could not send, and must not wait. SendAndWait passes its late
// callback here, which therefore runs only for a command SendAndWait gave up on (STATUS_IO_TIMEOUT).
typedef void (*PVIOGPU_COMPLETE)(_In_opt_ PVOID ctx, _In_ NTSTATUS status);

// Who frees a command's buffer. NONE: nobody waits, the completion frees it. PENDING: SendAndWait waits. DONE: the
// completion handed the outcome to the waiter, which frees it. ABANDONED: the waiter gave up, the completion frees it.
#define VIOGPU_WAIT_NONE      0
#define VIOGPU_WAIT_PENDING   1
#define VIOGPU_WAIT_DONE      2
#define VIOGPU_WAIT_ABANDONED 3

typedef struct virtio_gpu_vbuffer
{
    char *buf;
    int size;

    void *data_buf;
    u32 data_size;

    char *resp_buf;
    int resp_size;
    // The type of the answer that reports success, or 0 for a command without an answer.
    u32 resp_type;
    // The command's outcome, STATUS_PENDING until the device answers.
    NTSTATUS status;
    LIST_ENTRY list_entry;

    PVIOGPU_COMPLETE complete_cb;
    void *complete_ctx;

    // A caller that waits for the answer, VIOGPU_WAIT_*, and the event it waits on.
    volatile LONG wait_state;
    KEVENT done;
} GPU_VBUFFER, *PGPU_VBUFFER;

#define MAX_INLINE_CMD_SIZE  96
#define MAX_INLINE_RESP_SIZE 24
#define VBUFFER_SIZE         (sizeof(GPU_VBUFFER) + MAX_INLINE_CMD_SIZE + MAX_INLINE_RESP_SIZE)

class VioGpuBuf
{
  public:
    VioGpuBuf();
    ~VioGpuBuf();
    VioGpuBuf(const VioGpuBuf &) = delete;
    VioGpuBuf &operator=(const VioGpuBuf &) = delete;
    PGPU_VBUFFER GetBuf(_In_ int size, _In_ int resp_size, _In_ u32 resp_type);
    void FreeBuf(_In_ PGPU_VBUFFER pbuf);
    BOOLEAN Init(_In_ UINT cnt);

  private:
    void Close(void);

  private:
    LIST_ENTRY m_FreeBufs;
    LIST_ENTRY m_InUseBufs;
    KSPIN_LOCK m_SpinLock;
    UINT m_uCount;
    UINT m_uCountMin = 0;
};

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
    BOOLEAN Init(_In_ UINT size, _In_opt_ PPHYSICAL_ADDRESS pPAddr);
    BOOLEAN IsSystemMemory(void)
    {
        return m_bSystemMemory;
    }
    void Close(void);

  private:
    BOOLEAN m_bSystemMemory;
    BOOLEAN m_bMapped;
    PSCATTER_GATHER_LIST m_pSGList;
    PVOID m_pVAddr;
    PMDL m_pMdl;
    SIZE_T m_Size;
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

// Send: the device answers once it has finished the command (VIRTIO_GPU_FLAG_FENCE).
#define VIOGPU_SEND_FENCE        0x1
// How long SendAndWait waits for an answer, unless the caller gives a deadline.
#define VIOGPU_ANSWER_TIMEOUT_MS 1000
// How long a sender waits for room in a full ring, unless it gives a deadline.
#define VIOGPU_ROOM_TIMEOUT_MS   1000

class VioGpuQueue
{
    friend class VioGpuCommands;

  public:
    VioGpuQueue();
    ~VioGpuQueue();
    VioGpuQueue(const VioGpuQueue &) = delete;
    VioGpuQueue &operator=(const VioGpuQueue &) = delete;
    BOOLEAN Init(_In_ VirtIODevice *pVIODevice, _In_ struct virtqueue *pVirtQueue, _In_ UINT index);
    void Close(void);
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
    void Complete(void);

  private:
    // For VioGpuCommands, which alone fills a command buffer and sends it.
    PGPU_VBUFFER AllocBuffer(int size, int resp_size, u32 resp_type)
    {
        return m_pBuf->GetBuf(size, resp_size, resp_type);
    }
    void ReleaseBuffer(PGPU_VBUFFER buf);
    NTSTATUS Send(PGPU_VBUFFER buf,
                  ULONG flags = 0,
                  PVIOGPU_COMPLETE complete = NULL,
                  PVOID ctx = NULL,
                  ULONGLONG RoomDeadline = 0);
    NTSTATUS SendAndWait(PGPU_VBUFFER buf,
                         ULONG flags,
                         PVOID answer,
                         ULONG size,
                         ULONGLONG Deadline = 0,
                         PVIOGPU_COMPLETE late = NULL,
                         PVOID ctx = NULL);

    _IRQL_requires_max_(DISPATCH_LEVEL) _IRQL_saves_global_(OldIrql,
                                                            Irql) _IRQL_raises_(DISPATCH_LEVEL) void Lock(KIRQL *Irql);
    _IRQL_requires_(DISPATCH_LEVEL) _IRQL_restores_global_(OldIrql, Irql) void Unlock(KIRQL Irql);

  private:
    int AddBuf(_In_ struct VirtIOBufferDescriptor sg[],
               _In_ UINT out_num,
               _In_ UINT in_num,
               _In_ void *data,
               _In_opt_ void *va_indirect,
               _In_ ULONGLONG phys_indirect)
    {
        // Only on an open queue (AddWait): a closed one adds nothing, a failure like a full ring's.
        return m_pVirtQueue ? virtqueue_add_buf(m_pVirtQueue, sg, out_num, in_num, data, va_indirect, phys_indirect)
                            : -1;
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
    NTSTATUS Queue(PGPU_VBUFFER buf, ULONGLONG RoomDeadline);
    NTSTATUS AddWait(struct VirtIOBufferDescriptor sg[], UINT out_num, UINT in_num, PVOID data, ULONGLONG RoomDeadline);
    void Finish(PGPU_VBUFFER buf, NTSTATUS status);

  private:
    struct virtqueue *m_pVirtQueue;
    VirtIODevice *m_pVIODevice;
    UINT m_Index;
    KSPIN_LOCK m_SpinLock;
    // The last fence ID given, under m_SpinLock.
    ULONG64 m_FenceId;
    // Set when the DPC hands descriptors back, or the queue closes.
    KEVENT m_Room;

    VioGpuBuf *m_pBuf;
};
