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

#include "viogpu_queue.h"
#include "baseobj.h"
#if !DBG
#include "viogpu_queue.tmh"
#endif

// Adds the guest memory of size bytes at va to sg, after the count pieces already there, one piece for each run of
// physically adjacent pages. STATUS_BUFFER_OVERFLOW when that takes more than max pieces in all, and
// STATUS_INVALID_ADDRESS for an address that is not valid.
static NTSTATUS AddPieces(VirtIOBufferDescriptor *sg, UINT *count, UINT max, PVOID va, ULONG size)
{
    PUCHAR p = (PUCHAR)va;
    UINT first = *count;
    while (size != 0)
    {
        ULONG len = min(size, PAGE_SIZE - BYTE_OFFSET(p));
        if (!MmIsAddressValid(p))
        {
            return STATUS_INVALID_ADDRESS;
        }
        PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(p);
        if (*count > first && sg[*count - 1].physAddr.QuadPart + sg[*count - 1].length == pa.QuadPart)
        {
            sg[*count - 1].length += len;
        }
        else
        {
            if (*count >= max)
            {
                return STATUS_BUFFER_OVERFLOW;
            }
            sg[*count].physAddr = pa;
            sg[*count].length = len;
            (*count)++;
        }
        p += len;
        size -= len;
    }
    return STATUS_SUCCESS;
}

// Describes a command to the device in at most max pieces: first what the device reads, the command and its payload,
// outcnt pieces, then what it writes, the answer.
static NTSTATUS BuildPieces(PGPU_VBUFFER buf, VirtIOBufferDescriptor *sg, UINT max, UINT *count, UINT *outcnt)
{
    NTSTATUS status;
    *count = 0;
    status = AddPieces(sg, count, max, buf->buf, buf->size);
    if (NT_SUCCESS(status))
    {
        status = AddPieces(sg, count, max, buf->data_buf, buf->data_size);
    }
    *outcnt = *count;
    if (NT_SUCCESS(status))
    {
        status = AddPieces(sg, count, max, buf->resp_buf, buf->resp_size);
    }
    return status;
}

static NTSTATUS CheckAnswer(PGPU_VBUFFER buf, UINT len);

VioGpuQueue::VioGpuQueue()
{
    m_pBuf = NULL;
    m_Index = (UINT)-1;
    m_pVIODevice = NULL;
    m_pVirtQueue = NULL;
    m_FenceId = 0;
    m_Size = 0;
    m_bIndirect = FALSE;
    m_MaxPieces = 0;
    KeInitializeSpinLock(&m_SpinLock);
    KeInitializeEvent(&m_Room, NotificationEvent, FALSE);
}

VioGpuQueue::~VioGpuQueue()
{
    // The adapter closes the queue when it resets the device. Here, after the pool is gone, the ring is only
    // forgotten.
    m_pVirtQueue = NULL;
}

// Closes the queue once the device is reset, its interrupts are off and the DPCs are done. Each command still on
// the ring gets its outcome: the answer the device gave before the reset, or STATUS_CANCELLED.
void VioGpuQueue::Close(void)
{
    KIRQL SavedIrql;
    struct virtqueue *vq;
    Lock(&SavedIrql);
    vq = m_pVirtQueue;
    m_pVirtQueue = NULL;
    Unlock(SavedIrql);

    // At DISPATCH_LEVEL and above only the bugcheck flow closes a queue: the DPCs were not flushed, and a processor it
    // stopped may hold the pool's lock. The commands are left as they are.
    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
    {
        return;
    }
    if (vq != NULL)
    {
        PGPU_VBUFFER buf;
        UINT len = 0;
        UINT cancelled = 0;
        // Answered, but not taken off the ring yet.
        while ((buf = (PGPU_VBUFFER)virtqueue_get_buf(vq, &len)) != NULL)
        {
            Finish(buf, CheckAnswer(buf, len));
        }
        // Never answered.
        while ((buf = (PGPU_VBUFFER)virtqueue_detach_unused_buf(vq)) != NULL)
        {
            Finish(buf, STATUS_CANCELLED);
            cancelled++;
        }
        // A local, since DbgPrint does not parenthesize its level: a warning when the reset cancelled commands the
        // device never answered.
        UCHAR TraceLevel = cancelled != 0 ? TRACE_LEVEL_WARNING : TRACE_LEVEL_INFORMATION;
        DbgPrint(TraceLevel, ("%s: queue %d closed, %u commands cancelled\n", __FUNCTION__, m_Index, cancelled));
    }
    // Whoever waits for room in the ring finds the queue closed.
    KeSetEvent(&m_Room, IO_NO_INCREMENT, FALSE);
}

BOOLEAN VioGpuQueue::Init(_In_ VirtIODevice *pVIODevice,
                          _In_ struct virtqueue *pVirtQueue,
                          _In_ UINT index,
                          _In_ BOOLEAN indirect)
{
    if ((pVIODevice == NULL) || (pVirtQueue == NULL))
    {
        return FALSE;
    }
    m_pVIODevice = pVIODevice;
    m_Index = index;
    m_Size = QueryAllocation();
    if (m_Size == 0)
    {
        return FALSE;
    }
    // A chain, in the ring or in an indirect table, is never longer than the ring; an indirect table also fits a
    // page.
    m_bIndirect = indirect;
    m_MaxPieces = indirect ? min(m_Size, virtio_get_indirect_page_capacity()) : m_Size;
    m_pVirtQueue = pVirtQueue;
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("%s: queue %d has %u descriptors, indirect %d\n", __FUNCTION__, m_Index, m_Size, m_bIndirect));
    EnableInterrupt();
    return TRUE;
}

_IRQL_requires_max_(DISPATCH_LEVEL) _IRQL_saves_global_(OldIrql, Irql) _IRQL_raises_(DISPATCH_LEVEL) void VioGpuQueue::
                                                                                                    Lock(KIRQL *Irql)
{
    KIRQL SavedIrql = KeGetCurrentIrql();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s at IRQL %d\n", __FUNCTION__, SavedIrql));

    if (SavedIrql < DISPATCH_LEVEL)
    {
        KeAcquireSpinLock(&m_SpinLock, &SavedIrql);
    }
    else if (SavedIrql == DISPATCH_LEVEL)
    {
        KeAcquireSpinLockAtDpcLevel(&m_SpinLock);
    }
    else
    {
        // Only the bugcheck flow gets here: DxgkDdiSystemDisplayEnable, which may run at any IRQL, closes the queues.
        // No spin lock may be taken there, and nothing runs alongside.
    }
    *Irql = SavedIrql;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

_IRQL_requires_(DISPATCH_LEVEL) _IRQL_restores_global_(OldIrql, Irql) void VioGpuQueue::Unlock(KIRQL Irql)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s at IRQL %d\n", __FUNCTION__, Irql));

    if (Irql < DISPATCH_LEVEL)
    {
        KeReleaseSpinLock(&m_SpinLock, Irql);
    }
    else if (Irql == DISPATCH_LEVEL)
    {
        KeReleaseSpinLockFromDpcLevel(&m_SpinLock);
    }
    else
    {
        // Only the bugcheck flow gets here: DxgkDdiSystemDisplayEnable, which may run at any IRQL, closes the queues.
        // No spin lock may be taken there, and nothing runs alongside.
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PAGED_CODE_SEG_BEGIN

UINT VioGpuQueue::QueryAllocation()
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    USHORT NumEntries;
    ULONG RingSize, HeapSize;

    NTSTATUS status = virtio_query_queue_allocation(m_pVIODevice, m_Index, &NumEntries, &RingSize, &HeapSize);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("[%s] virtio_query_queue_allocation(%d) failed with error %x\n", __FUNCTION__, m_Index, status));
        return 0;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return NumEntries;
}
PAGED_CODE_SEG_END

// The relative timeout of a wait until deadline, interrupt time in 100 ns units: 0 once it has passed, and never so
// far ahead that it would read as an absolute time.
static LARGE_INTEGER TimeoutUntil(ULONGLONG deadline, ULONGLONG now)
{
    LARGE_INTEGER timeout;
    ULONGLONG left = deadline > now ? deadline - now : 0;
    timeout.QuadPart = -(LONGLONG)min(left, (ULONGLONG)MAXLONGLONG);
    return timeout;
}

// Gives a command a zeroed payload of size bytes, which goes with the command's buffer. Above a page the payload is
// allocated physically contiguous where memory allows, so that it takes one piece of the command however large it
// is; otherwise, and up to a page, it comes from the pool.
PVOID VioGpuQueue::AllocPayload(PGPU_VBUFFER buf, ULONG size)
{
    PVOID va = NULL;
    // A command has one payload.
    ASSERT(buf->data_buf == NULL && size != 0);
    if (size > PAGE_SIZE)
    {
        PHYSICAL_ADDRESS low, high, boundary;
        low.QuadPart = 0;
        high.QuadPart = -1;
        boundary.QuadPart = 0;
        va = MmAllocateContiguousNodeMemory(size, low, high, boundary, PAGE_READWRITE, MM_ANY_NODE_OK);
        if (va != NULL)
        {
            buf->data_contiguous = TRUE;
        }
        else
        {
            DbgPrint(TRACE_LEVEL_WARNING,
                     ("%s: no contiguous memory for %u bytes, the payload takes a piece per run of adjacent pages\n",
                      __FUNCTION__,
                      size));
        }
    }
    if (va == NULL)
    {
        va = new (NonPagedPoolNx) BYTE[size];
    }
    if (va != NULL)
    {
        RtlZeroMemory(va, size);
        buf->data_buf = va;
        buf->data_size = size;
    }
    return va;
}

// Sends a command without waiting for its answer. The buffer is the queue's from then on: a command that cannot be
// sent is freed at once, and the failure returned. Once the device answers a command that was sent, complete, if
// given, runs with the outcome, from the DPC or, after a device reset, from Close (PVIOGPU_COMPLETE). At IRQL <=
// APC_LEVEL a full ring is waited for until RoomDeadline, interrupt time in 100 ns units, or for VIOGPU_ROOM_TIMEOUT_MS
// when it is 0; at DISPATCH_LEVEL it fails at once.
NTSTATUS VioGpuQueue::Send(PGPU_VBUFFER buf, ULONG flags, PVIOGPU_COMPLETE complete, PVOID ctx, ULONGLONG RoomDeadline)
{
    // Only an answer tells that a fenced command has finished.
    ASSERT(!(flags & VIOGPU_SEND_FENCE) || buf->resp_type != 0);
    buf->complete_cb = complete;
    buf->complete_ctx = ctx;
    if (flags & VIOGPU_SEND_FENCE)
    {
        // The device answers a fenced command only once it has finished it, not once it has taken it. AddWait gives
        // it its fence ID.
        PGPU_CTRL_HDR hdr = (PGPU_CTRL_HDR)buf->buf;
        hdr->flags |= VIRTIO_GPU_FLAG_FENCE;
    }
    return Queue(buf, RoomDeadline);
}

// Sends a command and waits for the device to answer, then, when the device reports success, copies the answer, size
// bytes at most, to answer. The buffer is the queue's from the call on. Deadline, interrupt time in 100 ns units,
// bounds the wait for room in a full ring and for the answer together, for a caller that holds others up while it waits
// and that reports its failures itself: the queue logs only at VERBOSE what the deadline causes, a full ring, a command
// not sent, no answer in time. With Deadline 0 the wait for room is up to VIOGPU_ROOM_TIMEOUT_MS, then for the answer
// up to VIOGPU_ANSWER_TIMEOUT_MS.
//
// STATUS_DEVICE_BUSY when the ring had no room in time, or the deadline had passed already: nothing was sent.
// STATUS_IO_TIMEOUT, and only that, means the command is on the ring without an answer: the buffer is freed once the
// device answers or the queue closes, and late, if given, runs then with the outcome, from the DPC at DISPATCH_LEVEL,
// or from Close with the answer the device gave before the reset or STATUS_CANCELLED; never in the bugcheck flow. It
// may run before SendAndWait returns. late and ctx must stay valid until then, and late must be non-paged. With any
// other outcome late never runs; VioGpuCommands::NotSent names the outcomes of a command the device never saw.
// STATUS_INVALID_DEVICE_STATE above APC_LEVEL, where no wait is allowed. Any other failure: nothing was sent, the
// device refused the command, or the queue closed before it answered (STATUS_CANCELLED).
NTSTATUS VioGpuQueue::SendAndWait(PGPU_VBUFFER buf,
                                  ULONG flags,
                                  PVOID answer,
                                  ULONG size,
                                  ULONGLONG Deadline,
                                  PVIOGPU_COMPLETE late,
                                  PVOID ctx)
{
    NTSTATUS status;
    LARGE_INTEGER timeout;
    u32 type = ((PGPU_CTRL_HDR)buf->buf)->type;
    // A local: DbgPrint does not parenthesize its level.
    UCHAR TraceLevel = Deadline != 0 ? TRACE_LEVEL_VERBOSE : TRACE_LEVEL_FATAL;

    ASSERT(answer == NULL || size <= (ULONG)buf->resp_size);

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("<--- %s queue %d: command 0x%x not sent: no wait at IRQL %d\n",
                  __FUNCTION__,
                  m_Index,
                  type,
                  KeGetCurrentIrql()));
        ReleaseBuffer(buf);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Deadline != 0 && KeQueryInterruptTime() >= Deadline)
    {
        // A command sent now would be given up on at once, and left on the ring.
        DbgPrint(TRACE_LEVEL_VERBOSE,
                 ("<--- %s queue %d: command 0x%x not sent: deadline passed\n", __FUNCTION__, m_Index, type));
        ReleaseBuffer(buf);
        return STATUS_DEVICE_BUSY;
    }

    buf->wait_state = VIOGPU_WAIT_PENDING;
    status = Send(buf, flags, late, ctx, Deadline);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    if (Deadline == 0)
    {
        timeout.QuadPart = -10000LL * VIOGPU_ANSWER_TIMEOUT_MS;
    }
    else
    {
        timeout = TimeoutUntil(Deadline, KeQueryInterruptTime());
    }
    if (KeWaitForSingleObject(&buf->done, Executive, KernelMode, FALSE, &timeout) == STATUS_TIMEOUT)
    {
        if (InterlockedCompareExchange(&buf->wait_state, VIOGPU_WAIT_ABANDONED, VIOGPU_WAIT_PENDING) ==
            VIOGPU_WAIT_PENDING)
        {
            // The buffer belongs to the completion now.
            DbgPrint(TraceLevel, ("<--- %s queue %d: no answer to command 0x%x\n", __FUNCTION__, m_Index, type));
            return STATUS_IO_TIMEOUT;
        }
        // Answered just now: the completion sets the event right after it marks the command done.
        KeWaitForSingleObject(&buf->done, Executive, KernelMode, FALSE, NULL);
    }

    status = buf->status;
    if (NT_SUCCESS(status) && answer != NULL)
    {
        RtlCopyMemory(answer, buf->resp_buf, min(size, (ULONG)buf->resp_size));
    }
    ReleaseBuffer(buf);
    return status;
}

// Adds a buffer to the ring and notifies the device, and gives a fenced command its fence ID. When the ring is full, a
// caller at IRQL <= APC_LEVEL waits, until RoomDeadline (interrupt time) or for VIOGPU_ROOM_TIMEOUT_MS when it is 0,
// for the DPC to hand descriptors back. STATUS_DEVICE_BUSY when the ring stays full, and STATUS_DEVICE_NOT_READY when
// the queue is closed.
NTSTATUS VioGpuQueue::AddWait(struct VirtIOBufferDescriptor sg[],
                              UINT out_num,
                              UINT in_num,
                              PVOID data,
                              PVOID va_indirect,
                              ULONGLONG phys_indirect,
                              ULONGLONG RoomDeadline)
{
    ULONGLONG deadline = RoomDeadline;
    BOOLEAN waited = FALSE;
    UCHAR TraceLevel = RoomDeadline != 0 ? TRACE_LEVEL_VERBOSE : TRACE_LEVEL_WARNING;
    PGPU_CTRL_HDR hdr = (PGPU_CTRL_HDR)((PGPU_VBUFFER)data)->buf;
    for (;;)
    {
        NTSTATUS status;
        KIRQL SavedIrql;
        Lock(&SavedIrql);
        // Fence IDs reach the ring in the order they are given: with virgl the device answers every fenced command up
        // to the fence that signalled. A retry takes a new ID; the gap does no harm.
        if (hdr->flags & VIRTIO_GPU_FLAG_FENCE)
        {
            hdr->fence_id = ++m_FenceId;
        }
        if (m_pVirtQueue == NULL)
        {
            // Closed: the device was reset.
            status = STATUS_DEVICE_NOT_READY;
        }
        else if (AddBuf(sg, out_num, in_num, data, va_indirect, phys_indirect) >= 0)
        {
            Kick();
            status = STATUS_SUCCESS;
        }
        else
        {
            // Cleared under the lock: whatever the DPC hands back from here on sets it again.
            KeClearEvent(&m_Room);
            status = STATUS_DEVICE_BUSY;
        }
        Unlock(SavedIrql);

        if (status != STATUS_DEVICE_BUSY || KeGetCurrentIrql() > APC_LEVEL)
        {
            return status;
        }
        ULONGLONG now = KeQueryInterruptTime();
        if (!waited)
        {
            DbgPrint(TraceLevel, ("%s: queue %d is full, waiting\n", __FUNCTION__, m_Index));
            waited = TRUE;
            if (deadline == 0)
            {
                deadline = now + 10000ULL * VIOGPU_ROOM_TIMEOUT_MS;
            }
        }
        if (now >= deadline)
        {
            return STATUS_DEVICE_BUSY;
        }
        LARGE_INTEGER timeout = TimeoutUntil(deadline, now);
        KeWaitForSingleObject(&m_Room, Executive, KernelMode, FALSE, &timeout);
    }
}

// Adds a command to the ring and notifies the device. A command that cannot be added is freed.
NTSTATUS VioGpuQueue::Queue(PGPU_VBUFFER buf, ULONGLONG RoomDeadline)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    // A buffer is sent once: an answered one has its outcome already, a queued one of more than VIOGPU_INDIRECT_INLINE
    // pieces its separate descriptor table.
    ASSERT(buf->status == STATUS_PENDING && buf->indirect_ext == NULL);

    VirtIOBufferDescriptor inline_sg[VIOGPU_INLINE_PIECES];
    VirtIOBufferDescriptor *sg = inline_sg;
    UINT count = 0;
    UINT outcnt = 0;
    PVOID table = NULL;
    ULONGLONG table_pa = 0;
    NTSTATUS status;

    // A ring of fewer descriptors than the stack holds limits the first try too.
    status = BuildPieces(buf, sg, min((UINT)VIOGPU_INLINE_PIECES, m_MaxPieces), &count, &outcnt);
    if (status == STATUS_BUFFER_OVERFLOW && m_MaxPieces > VIOGPU_INLINE_PIECES)
    {
        // Only a payload over many pages that are not adjacent takes this many pieces.
        sg = new (NonPagedPoolNx) VirtIOBufferDescriptor[m_MaxPieces];
        status = (sg == NULL) ? STATUS_INSUFFICIENT_RESOURCES : BuildPieces(buf, sg, m_MaxPieces, &count, &outcnt);
    }
    if (status == STATUS_BUFFER_OVERFLOW)
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("%s: queue %d takes no more than %u pieces\n", __FUNCTION__, m_Index, m_MaxPieces));
        status = STATUS_INVALID_BUFFER_SIZE;
    }

    // With indirect descriptors a command of more than one piece takes one descriptor of the ring, which points to a
    // table of its pieces.
    if (NT_SUCCESS(status) && m_bIndirect && count > 1)
    {
        if (count <= VIOGPU_INDIRECT_INLINE)
        {
            table = buf->indirect;
        }
        else
        {
            buf->indirect_ext = new (NonPagedPoolNx) BYTE[count * VIOGPU_DESC_SIZE];
            table = buf->indirect_ext;
            if (table == NULL)
            {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        if (table != NULL)
        {
            table_pa = (ULONGLONG)MmGetPhysicalAddress(table).QuadPart;
        }
    }

    if (NT_SUCCESS(status))
    {
        status = AddWait(sg, outcnt, count - outcnt, buf, table, table_pa, RoomDeadline);
    }
    if (sg != inline_sg)
    {
        delete[] sg;
    }

    if (!NT_SUCCESS(status))
    {
        // A sender with a deadline reports a ring that stayed full itself.
        UCHAR TraceLevel = RoomDeadline != 0 && status == STATUS_DEVICE_BUSY ? TRACE_LEVEL_VERBOSE : TRACE_LEVEL_ERROR;
        DbgPrint(TraceLevel,
                 ("<--- %s queue %d: command 0x%x not sent, status 0x%x\n",
                  __FUNCTION__,
                  m_Index,
                  ((PGPU_CTRL_HDR)buf->buf)->type,
                  status));
        ReleaseBuffer(buf);
        return status;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

// The outcome of a command, from the device's answer of len bytes.
static NTSTATUS CheckAnswer(PGPU_VBUFFER buf, UINT len)
{
    PGPU_CTRL_HDR cmd = (PGPU_CTRL_HDR)buf->buf;
    PGPU_CTRL_HDR resp = (PGPU_CTRL_HDR)buf->resp_buf;
    NTSTATUS status;

    if (buf->resp_type == 0)
    {
        return STATUS_SUCCESS;
    }
    if (len < sizeof(GPU_CTRL_HDR))
    {
        status = STATUS_DEVICE_PROTOCOL_ERROR;
    }
    else if (resp->type == buf->resp_type)
    {
        status = (len < (UINT)buf->resp_size) ? STATUS_DEVICE_PROTOCOL_ERROR : STATUS_SUCCESS;
    }
    else
    {
        switch (resp->type)
        {
            case VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY:
                status = STATUS_NO_MEMORY;
                break;
            case VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID:
                // The host has no such resource: a release that finds none has nothing to wait for.
                status = STATUS_NOT_FOUND;
                break;
            case VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID:
            case VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID:
            case VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER:
                status = STATUS_INVALID_PARAMETER;
                break;
            case VIRTIO_GPU_RESP_ERR_UNSPEC:
                status = STATUS_UNSUCCESSFUL;
                break;
            default:
                status = STATUS_DEVICE_PROTOCOL_ERROR;
                break;
        }
    }
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("%s: command 0x%x failed: answer 0x%x of %u bytes, status 0x%x\n",
                  __FUNCTION__,
                  cmd->type,
                  (len < sizeof(GPU_CTRL_HDR)) ? 0 : resp->type,
                  len,
                  status));
    }
    return status;
}

// Hands each command the device completed its outcome, in the DPC.
void VioGpuQueue::Complete(void)
{
    BOOLEAN room = FALSE;
    for (;;)
    {
        KIRQL SavedIrql;
        UINT len = 0;
        Lock(&SavedIrql);
        PGPU_VBUFFER buf = (PGPU_VBUFFER)GetBuf(&len);
        Unlock(SavedIrql);
        if (buf == NULL)
        {
            break;
        }
        room = TRUE;

        Finish(buf, CheckAnswer(buf, len));
    }
    if (room)
    {
        KeSetEvent(&m_Room, IO_NO_INCREMENT, FALSE);
    }
}

// Hands a command its outcome. A caller still waiting frees the buffer itself; otherwise the completion callback, if
// any, runs and the buffer is freed here.
void VioGpuQueue::Finish(PGPU_VBUFFER buf, NTSTATUS status)
{
    buf->status = status;
    if (buf->wait_state != VIOGPU_WAIT_NONE &&
        InterlockedCompareExchange(&buf->wait_state, VIOGPU_WAIT_DONE, VIOGPU_WAIT_PENDING) == VIOGPU_WAIT_PENDING)
    {
        // Nothing touches the buffer after the event: the waiter may free it at once.
        KeSetEvent(&buf->done, IO_NO_INCREMENT, FALSE);
        return;
    }
    if (buf->complete_cb != NULL)
    {
        buf->complete_cb(buf->complete_ctx, buf->status);
    }
    else if (buf->wait_state == VIOGPU_WAIT_ABANDONED)
    {
        // SendAndWait gave up on the answer and said so; a caller with a late callback reports the end itself.
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("%s: queue %d: command 0x%x, given up on, ended with 0x%x\n",
                  __FUNCTION__,
                  m_Index,
                  ((PGPU_CTRL_HDR)buf->buf)->type,
                  buf->status));
    }
    ReleaseBuffer(buf);
}

void VioGpuQueue::ReleaseBuffer(PGPU_VBUFFER buf)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    m_pBuf->FreeBuf(buf);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuBuf::Init(_In_ UINT cnt)
{
    KIRQL OldIrql;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    m_uCountMin = cnt;

    for (UINT i = 0; i < cnt; ++i)
    {
        PGPU_VBUFFER pvbuf = reinterpret_cast<PGPU_VBUFFER>(new (NonPagedPoolNx) BYTE[VBUFFER_SIZE]);
        if (pvbuf)
        {
            RtlZeroMemory(pvbuf, VBUFFER_SIZE);
            KeAcquireSpinLock(&m_SpinLock, &OldIrql);
            InsertTailList(&m_FreeBufs, &pvbuf->list_entry);
            ++m_uCount;
            KeReleaseSpinLock(&m_SpinLock, OldIrql);
        }
    }
    ASSERT(m_uCount == cnt);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return (m_uCount > 0);
}

// Frees what a buffer carries besides itself: a separate answer, a payload and a separate descriptor table.
static void FreeContents(_In_ PGPU_VBUFFER pbuf)
{
    if (pbuf->resp_buf && pbuf->resp_size > MAX_INLINE_RESP_SIZE)
    {
        delete[] reinterpret_cast<PBYTE>(pbuf->resp_buf);
        pbuf->resp_buf = NULL;
        pbuf->resp_size = 0;
    }

    if (pbuf->data_buf)
    {
        if (pbuf->data_contiguous)
        {
            MmFreeContiguousMemory(pbuf->data_buf);
        }
        else
        {
            delete[] reinterpret_cast<PBYTE>(pbuf->data_buf);
        }
        pbuf->data_buf = NULL;
        pbuf->data_size = 0;
        pbuf->data_contiguous = FALSE;
    }

    delete[] reinterpret_cast<PBYTE>(pbuf->indirect_ext);
    pbuf->indirect_ext = NULL;
}

void VioGpuBuf::Close(void)
{
    KIRQL OldIrql;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    KeAcquireSpinLock(&m_SpinLock, &OldIrql);
    while (!IsListEmpty(&m_InUseBufs))
    {
        LIST_ENTRY *pListItem = RemoveHeadList(&m_InUseBufs);
        if (pListItem)
        {
            PGPU_VBUFFER pvbuf = CONTAINING_RECORD(pListItem, GPU_VBUFFER, list_entry);
            ASSERT(pvbuf);

            // A command left on a ring by the bugcheck flow, which closes the queues without finishing their commands.
            FreeContents(pvbuf);
            delete[] reinterpret_cast<PBYTE>(pvbuf);
            --m_uCount;
        }
    }

    while (!IsListEmpty(&m_FreeBufs))
    {
        LIST_ENTRY *pListItem = RemoveHeadList(&m_FreeBufs);
        if (pListItem)
        {
            PGPU_VBUFFER pbuf = CONTAINING_RECORD(pListItem, GPU_VBUFFER, list_entry);
            ASSERT(pbuf);

            delete[] reinterpret_cast<PBYTE>(pbuf);
            --m_uCount;
        }
    }
    KeReleaseSpinLock(&m_SpinLock, OldIrql);

    ASSERT(m_uCount == 0);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PGPU_VBUFFER VioGpuBuf::GetBuf(_In_ int size, _In_ int resp_size, _In_ u32 resp_type)
{

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER pbuf = NULL;
    PBYTE resp_buf = NULL;
    BOOLEAN Allocated = FALSE;
    KIRQL OldIrql;

    // Above DISPATCH_LEVEL, where only the bugcheck flow runs, neither the pool's spin lock nor the system's pool may
    // be used. No command is sent there: the bugcheck flow resets the device right after, and the reset drops every
    // resource and scanout on its own.
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
    {
        return NULL;
    }
    if (size > MAX_INLINE_CMD_SIZE)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s no room for %d bytes\n", __FUNCTION__, size));
        return NULL;
    }
    // An answer too large for the buffer gets its own memory, which the pool frees with the buffer.
    if (resp_size > MAX_INLINE_RESP_SIZE)
    {
        resp_buf = new (NonPagedPoolNx) BYTE[resp_size];
        if (resp_buf == NULL)
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s no memory for a %d byte answer\n", __FUNCTION__, resp_size));
            return NULL;
        }
    }

    KeAcquireSpinLock(&m_SpinLock, &OldIrql);
    if (!IsListEmpty(&m_FreeBufs))
    {
        pbuf = CONTAINING_RECORD(RemoveHeadList(&m_FreeBufs), GPU_VBUFFER, list_entry);
    }
    KeReleaseSpinLock(&m_SpinLock, OldIrql);

    if (pbuf == NULL)
    {
        pbuf = reinterpret_cast<PGPU_VBUFFER>(new (NonPagedPoolNx) BYTE[VBUFFER_SIZE]);
        if (pbuf == NULL)
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s no memory for a command buffer\n", __FUNCTION__));
            delete[] resp_buf;
            return NULL;
        }
        Allocated = TRUE;
    }

    memset(pbuf, 0, VBUFFER_SIZE);

    pbuf->buf = (char *)((ULONG_PTR)pbuf + sizeof(*pbuf));
    pbuf->size = size;

    // A command has an answer type exactly when it has an answer.
    ASSERT((resp_size == 0) == (resp_type == 0));
    pbuf->resp_size = resp_size;
    pbuf->resp_type = resp_type;
    pbuf->status = STATUS_PENDING;
    if (resp_size <= MAX_INLINE_RESP_SIZE)
    {
        pbuf->resp_buf = (char *)((ULONG_PTR)pbuf->buf + size);
    }
    else
    {
        // Whatever the device does not write must read as zeroes.
        pbuf->resp_buf = (char *)resp_buf;
        RtlZeroMemory(pbuf->resp_buf, resp_size);
    }
    KeInitializeEvent(&pbuf->done, NotificationEvent, FALSE);

    KeAcquireSpinLock(&m_SpinLock, &OldIrql);
    if (Allocated)
    {
        ++m_uCount;
    }
    InsertTailList(&m_InUseBufs, &pbuf->list_entry);
    KeReleaseSpinLock(&m_SpinLock, OldIrql);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s buf = %p\n", __FUNCTION__, pbuf));

    return pbuf;
}

void VioGpuBuf::FreeBuf(_In_ PGPU_VBUFFER pbuf)
{
    KIRQL OldIrql;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s buf = %p\n", __FUNCTION__, pbuf));
    KeAcquireSpinLock(&m_SpinLock, &OldIrql);

    BOOLEAN Found = FALSE;
    for (PLIST_ENTRY le = m_InUseBufs.Flink; le != &m_InUseBufs; le = le->Flink)
    {
        if (le == &pbuf->list_entry)
        {
            RemoveEntryList(le);
            Found = TRUE;
            break;
        }
    }
    if (!Found)
    {
        // Freed twice, or never handed out: neither list may take it again.
        KeReleaseSpinLock(&m_SpinLock, OldIrql);
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s buf = %p is not in use\n", __FUNCTION__, pbuf));
        ASSERT(Found);
        return;
    }
    FreeContents(pbuf);

    if (m_uCount > m_uCountMin)
    {
        delete[] reinterpret_cast<PBYTE>(pbuf);
        --m_uCount;
    }
    else
    {
        InsertTailList(&m_FreeBufs, &pbuf->list_entry);
    }

    KeReleaseSpinLock(&m_SpinLock, OldIrql);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PAGED_CODE_SEG_BEGIN
VioGpuBuf::VioGpuBuf()
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    InitializeListHead(&m_FreeBufs);
    InitializeListHead(&m_InUseBufs);
    KeInitializeSpinLock(&m_SpinLock);
    m_uCount = 0;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VioGpuBuf::~VioGpuBuf()
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s 0x%p\n", __FUNCTION__, this));

    Close();

    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
}

VioGpuMemSegment::VioGpuMemSegment(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    m_pSGList = NULL;
    m_pVAddr = NULL;
    m_pMdl = NULL;
    m_bSystemMemory = FALSE;
    m_bMapped = FALSE;
    m_Size = 0;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VioGpuMemSegment::~VioGpuMemSegment(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    Close();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

// The most entries one ATTACH_BACKING may carry: QEMU refuses more (virtio_gpu_create_mapping_iov).
#define VIOGPU_MAX_BACKING_ENTRIES 16384

// Allocates size bytes, rounded up to whole chunks, or maps them, rounded up to whole pages, from *pPAddr when that is
// not 0. Allocated memory comes in physically contiguous chunks: 2 MiB when memory allows, else 64 KiB, else single
// pages, a chunk being no larger than the allocation. The scatter-gather list, one element per contiguous run, stays
// short: a frame buffer is attached whole, in one ATTACH_BACKING.
BOOLEAN VioGpuMemSegment::Init(_In_ UINT size, _In_opt_ PPHYSICAL_ADDRESS pPAddr)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    ASSERT(size);
    if (m_pSGList != NULL || m_pMdl != NULL || m_pVAddr != NULL)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s the segment is set up already\n", __FUNCTION__));
        ASSERT(FALSE);
        return FALSE;
    }
    SIZE_T bytes = ROUND_TO_PAGES(size);

    if (pPAddr != NULL && pPAddr->QuadPart != 0LL)
    {
        NTSTATUS Status = MapFrameBuffer(*pPAddr, (ULONG)bytes, &m_pVAddr);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s MapFrameBuffer failed with Status: 0x%X\n", __FUNCTION__, Status));
            return FALSE;
        }
        m_bMapped = TRUE;
        m_Size = bytes;
        SIZE_T sglsize = sizeof(SCATTER_GATHER_LIST) + sizeof(SCATTER_GATHER_ELEMENT);
        m_pSGList = reinterpret_cast<PSCATTER_GATHER_LIST>(new (NonPagedPoolNx) BYTE[sglsize]);
        if (m_pSGList == NULL)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s no memory for the scatter-gather list\n", __FUNCTION__));
            Close();
            return FALSE;
        }
        RtlZeroMemory(m_pSGList, sglsize);
        // A BAR is physically contiguous.
        m_pSGList->NumberOfElements = 1;
        m_pSGList->Elements[0].Address = *pPAddr;
        m_pSGList->Elements[0].Length = (ULONG)bytes;
        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
        return TRUE;
    }

    static const SIZE_T Chunks[] = {2 * 1024 * 1024, 64 * 1024, PAGE_SIZE};
    SIZE_T chunk = PAGE_SIZE;
    for (ULONG i = 0; i < ARRAYSIZE(Chunks) && m_pMdl == NULL; i++)
    {
        // The chunk is no larger than the allocation needs, since the allocation is rounded up to whole chunks.
        chunk = Chunks[i];
        while (chunk > PAGE_SIZE && chunk / 2 >= bytes)
        {
            chunk /= 2;
        }
        SIZE_T total = (bytes + chunk - 1) & ~(chunk - 1);
        PHYSICAL_ADDRESS low, high, skip;
        low.QuadPart = 0;
        high.QuadPart = -1;
        skip.QuadPart = (chunk > PAGE_SIZE) ? (LONGLONG)chunk : 0;
        ULONG flags = MM_ALLOCATE_FULLY_REQUIRED | ((chunk > PAGE_SIZE) ? MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS : 0);
#if defined(_AMD64_) || defined(_ARM64_)
        // Large pages only from the cache: building more may take long, and smaller chunks serve as well.
        if (chunk == 2 * 1024 * 1024)
        {
            flags |= MM_ALLOCATE_FAST_LARGE_PAGES;
        }
#endif
        m_pMdl = MmAllocatePagesForMdlEx(low, high, skip, total, MmCached, flags);
        if (m_pMdl != NULL && MmGetMdlByteCount(m_pMdl) != total)
        {
            MmFreePagesFromMdl(m_pMdl);
            ExFreePool(m_pMdl);
            m_pMdl = NULL;
        }
        if (m_pMdl == NULL && chunk > PAGE_SIZE)
        {
            DbgPrint(TRACE_LEVEL_WARNING, ("%s no %Iu-byte chunks for %Iu bytes\n", __FUNCTION__, chunk, bytes));
        }
    }
    if (m_pMdl == NULL)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s no memory for %Iu bytes\n", __FUNCTION__, bytes));
        return FALSE;
    }
    m_bSystemMemory = TRUE;
    m_Size = MmGetMdlByteCount(m_pMdl);
    m_pVAddr = MmMapLockedPagesSpecifyCache(m_pMdl,
                                            KernelMode,
                                            MmCached,
                                            NULL,
                                            FALSE,
                                            NormalPagePriority | MdlMappingNoExecute);
    if (m_pVAddr == NULL)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s cannot map %Iu bytes\n", __FUNCTION__, m_Size));
        Close();
        return FALSE;
    }

    PPFN_NUMBER pfn = MmGetMdlPfnArray(m_pMdl);
    ULONG pages = (ULONG)(m_Size >> PAGE_SHIFT);
    ULONG runs = 0;
    for (ULONG i = 0; i < pages; i++)
    {
        if (i == 0 || pfn[i] != pfn[i - 1] + 1)
        {
            runs++;
        }
    }
    if (runs > VIOGPU_MAX_BACKING_ENTRIES)
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("<--- %s %Iu bytes in chunks of %Iu are %d runs, more than one ATTACH_BACKING takes\n",
                  __FUNCTION__,
                  m_Size,
                  chunk,
                  runs));
        Close();
        return FALSE;
    }
    SIZE_T sglsize = sizeof(SCATTER_GATHER_LIST) + sizeof(SCATTER_GATHER_ELEMENT) * runs;
    m_pSGList = reinterpret_cast<PSCATTER_GATHER_LIST>(new (NonPagedPoolNx) BYTE[sglsize]);
    if (m_pSGList == NULL)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s no memory for the scatter-gather list\n", __FUNCTION__));
        Close();
        return FALSE;
    }
    RtlZeroMemory(m_pSGList, sglsize);
    for (ULONG i = 0; i < pages; i++)
    {
        if (i == 0 || pfn[i] != pfn[i - 1] + 1)
        {
            m_pSGList->Elements[m_pSGList->NumberOfElements].Address.QuadPart = (LONGLONG)pfn[i] << PAGE_SHIFT;
            m_pSGList->NumberOfElements++;
        }
        m_pSGList->Elements[m_pSGList->NumberOfElements - 1].Length += PAGE_SIZE;
    }
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("<--- %s %Iu bytes in %d run(s), chunks of %Iu\n", __FUNCTION__, m_Size, runs, chunk));
    return TRUE;
}

// Frees or unmaps what Init set up. A segment closed before holds nothing.
void VioGpuMemSegment::Close(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (m_bSystemMemory)
    {
        if (m_pVAddr != NULL)
        {
            MmUnmapLockedPages(m_pVAddr, m_pMdl);
        }
        if (m_pMdl != NULL)
        {
            MmFreePagesFromMdl(m_pMdl);
            ExFreePool(m_pMdl);
        }
    }
    else if (m_bMapped && m_pVAddr != NULL)
    {
        UnmapFrameBuffer(m_pVAddr, (ULONG)m_Size);
    }
    m_pMdl = NULL;
    m_pVAddr = NULL;
    m_bSystemMemory = FALSE;
    m_bMapped = FALSE;
    m_Size = 0;

    delete[] reinterpret_cast<PBYTE>(m_pSGList);
    m_pSGList = NULL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VioGpuObj::VioGpuObj(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    m_uiHwRes = 0;
    m_pNextReleased = NULL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

// Frees the pages with the segment, only once the host no longer uses them: the bugcheck flow never deletes an object.
VioGpuObj::~VioGpuObj(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuObj::Init(_In_ UINT size, _In_opt_ PPHYSICAL_ADDRESS pBar)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s requested size = %d\n", __FUNCTION__, size));

    ASSERT(size);
    return m_Segment.Init(size, pBar);
}

PAGED_CODE_SEG_END
