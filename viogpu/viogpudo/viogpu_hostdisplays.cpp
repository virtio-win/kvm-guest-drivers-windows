/*
 * Copyright (C) 2019-2022 Red Hat, Inc.
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

#include "helper.h"
#include "driver.h"
#include "viogpudo.h"
#include "viogpu_hostdisplays.h"

#if !DBG
#include "viogpu_hostdisplays.tmh"
#endif

PAGED_CODE_SEG_BEGIN

VioGpuHostDisplays::VioGpuHostDisplays(void)
{
    PAGED_CODE();
    m_pAdapter = NULL;
    m_pWorkThread = NULL;
    m_bStopWorkThread = FALSE;
    KeInitializeEvent(&m_ConfigUpdateEvent, SynchronizationEvent, FALSE);
    // Closed until the first Open.
    ExInitializeRundownProtection(&m_Rundown);
    ExWaitForRundownProtectionRelease(&m_Rundown);
    ExRundownCompleted(&m_Rundown);
}

// Starts the thread. A change the host signals before the first Open waits for it in m_ConfigUpdateEvent.
NTSTATUS VioGpuHostDisplays::Start(void)
{
    PAGED_CODE();

    HANDLE threadHandle = 0;
    NTSTATUS status = PsCreateSystemThread(&threadHandle,
                                           (ACCESS_MASK)0,
                                           NULL,
                                           (HANDLE)0,
                                           NULL,
                                           VioGpuHostDisplays::ThreadWork,
                                           this);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s failed to create system thread, status %x\n", __FUNCTION__, status));
        VioGpuDbgBreak();
        return status;
    }
    status = ObReferenceObjectByHandle(threadHandle,
                                       THREAD_ALL_ACCESS,
                                       NULL,
                                       KernelMode,
                                       (PVOID *)(&m_pWorkThread),
                                       NULL);
    if (!NT_SUCCESS(status))
    {
        // Without the reference Stop could not wait for the thread: it is told to stop, and waited for, here.
        DbgPrint(TRACE_LEVEL_FATAL, ("%s no reference to the system thread, status %x\n", __FUNCTION__, status));
        m_pWorkThread = NULL;
        m_bStopWorkThread = TRUE;
        KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);
        NTSTATUS wait = ZwWaitForSingleObject(threadHandle, FALSE, NULL);
        if (wait != STATUS_SUCCESS)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s the system thread was not waited for, status %x\n", __FUNCTION__, wait));
        }
    }
    ZwClose(threadHandle);
    return status;
}

// Lets the thread use the device, and wakes it to read a display event the device kept across a reset (QEMU leaves
// events_read as it is; the virtio spec does not say).
void VioGpuHostDisplays::Open(void)
{
    PAGED_CODE();
    ExReInitializeRundownProtection(&m_Rundown);
    KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);
}

// Waits for a read in progress, which may wait for the device's answers; a read posted from now on waits for Open.
void VioGpuHostDisplays::Close(void)
{
    PAGED_CODE();
    ExWaitForRundownProtectionRelease(&m_Rundown);
    ExRundownCompleted(&m_Rundown);
}

// Stops the thread and waits for it. The stop wakes the thread's idle wait, and Close has already waited for a read.
void VioGpuHostDisplays::Stop(void)
{
    PAGED_CODE();

    if (m_pWorkThread == NULL)
    {
        return;
    }
    m_bStopWorkThread = TRUE;
    KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(m_pWorkThread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(m_pWorkThread);
    m_pWorkThread = NULL;
}

PAGED_CODE_SEG_END

void VioGpuHostDisplays::Post(void)
{
    KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);
}

void VioGpuHostDisplays::ThreadWork(_In_ PVOID Context)
{
    reinterpret_cast<VioGpuHostDisplays *>(Context)->ThreadWorkRoutine();
}

void VioGpuHostDisplays::ThreadWorkRoutine(void)
{
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    for (;;)
    {
        KeWaitForSingleObject(&m_ConfigUpdateEvent, Executive, KernelMode, FALSE, NULL);

        if (m_bStopWorkThread)
        {
            PsTerminateSystemThread(STATUS_SUCCESS);
            break;
        }
        // A change the device reports while the component is closed is read when it opens again.
        if (ExAcquireRundownProtection(&m_Rundown))
        {
            m_pAdapter->ConfigChanged();
            m_pAdapter->NotifyResolutionEvent();
            ExReleaseRundownProtection(&m_Rundown);
        }
    }
}
