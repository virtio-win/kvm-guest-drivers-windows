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
#include "baseobj.h"
#include "edid.h"

#if !DBG
#include "viogpu_hostdisplays.tmh"
#endif

// A read that fails is retried a second later, then after twice as long each time, up to 1 << VIOGPU_RETRY_MAX_SHIFT
// seconds: a host that stopped answering is asked less and less often.
#define VIOGPU_RETRY_MAX_SHIFT 4

PAGED_CODE_SEG_BEGIN

VioGpuHostDisplays::VioGpuHostDisplays(void)
{
    PAGED_CODE();
    m_pAdapter = NULL;
    m_pWorkThread = NULL;
    m_WorkPosted = 0;
    m_ChildAnnounced = 0;
    m_AnnounceFailed = 0;
    m_ReadOutstanding = 0;
    KeInitializeEvent(&m_WorkEvent, SynchronizationEvent, FALSE);
    // Closed until the first Open.
    ExInitializeRundownProtection(&m_Rundown);
    ExWaitForRundownProtectionRelease(&m_Rundown);
    ExRundownCompleted(&m_Rundown);
}

// Starts the thread. A change the host signals before the first Open, and a re-read InitDisplay posts, are kept until
// then: in m_WorkPosted, or, once the thread has taken them, in its pending re-read.
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
        Post(VIOGPU_WORK_STOP);
        NTSTATUS wait = ZwWaitForSingleObject(threadHandle, FALSE, NULL);
        if (wait != STATUS_SUCCESS)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s the system thread was not waited for, status %x\n", __FUNCTION__, wait));
        }
    }
    ZwClose(threadHandle);
    return status;
}

// Lets the thread use the device, and has it read the displays again: a change the host made while the device was
// reset raised no interrupt. A display event the device kept across the reset counts as one; otherwise only a change
// is reported.
void VioGpuHostDisplays::Open(void)
{
    PAGED_CODE();
    ExReInitializeRundownProtection(&m_Rundown);
    Post(VIOGPU_WORK_REREAD);
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
    Post(VIOGPU_WORK_STOP);
    KeWaitForSingleObject(m_pWorkThread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(m_pWorkThread);
    m_pWorkThread = NULL;
}

// Reads what the host reports about the scanouts the driver presents into m_Next: whether the host enables each, its
// size and, while it is enabled, its EDID and the modes that brings. FALSE when the driver did not get the host's
// answer.
BOOLEAN VioGpuHostDisplays::ReadDisplays(void)
{
    PAGED_CODE();

    if (m_ReadOutstanding)
    {
        // A host that does not answer would get a new read with each retry, until the ring is full.
        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s the last read is still unanswered\n", __FUNCTION__));
        return FALSE;
    }
    GPU_RESP_DISP_INFO info;
    // Marked before the read goes: ReadLate may run before it returns.
    InterlockedExchange(&m_ReadOutstanding, 1);
    NTSTATUS status = m_pAdapter->GetCommands()->GetDisplayInfo(&info, ReadLate, this);
    if (status != STATUS_IO_TIMEOUT)
    {
        InterlockedExchange(&m_ReadOutstanding, 0);
    }
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s no display info (0x%x): the read is retried\n", __FUNCTION__, status));
        return FALSE;
    }
    for (UINT ScanoutId = 0; ScanoutId < m_pAdapter->GetController()->GetScanoutCount(); ++ScanoutId)
    {
        PVIOGPU_DISPLAY pDisplay = &m_Next[ScanoutId];
        ULONG xres = 0;
        ULONG yres = 0;
        // Nothing an earlier read left in pDisplay is published with this one.
        RtlZeroMemory(pDisplay, sizeof(*pDisplay));
        // An enabled scanout without a size counts as disabled.
        pDisplay->Connected = ScanoutId < m_pAdapter->GetNumScanouts() &&
                              VioGpuCommands::ScanoutSize(&info, ScanoutId, &xres, &yres) && xres != 0 && yres != 0;
        pDisplay->HostWidth = pDisplay->Connected ? xres : 0;
        pDisplay->HostHeight = pDisplay->Connected ? yres : 0;
        // A disabled scanout keeps the EDID and the modes it had: Publish carries them over.
        if (!pDisplay->Connected)
        {
            continue;
        }
        if (!ReadEdid(ScanoutId, pDisplay))
        {
            return FALSE;
        }
        BuildModes(pDisplay);
    }
    return TRUE;
}

// Puts the built-in EDID, without a physical size or extensions, into pDisplay.
static void BuiltinEdid(PVIOGPU_DISPLAY pDisplay)
{
    PAGED_CODE();

    RtlCopyMemory(pDisplay->Edid, g_gpu_edid, EDID_BLOCK_SIZE);
    pDisplay->Edid[EDID_OFFSET_H_SIZE_CM] = 0;
    pDisplay->Edid[EDID_OFFSET_V_SIZE_CM] = 0;
    pDisplay->Edid[EDID_OFFSET_EXTENSIONS] = 0;
    EdidSetChecksum(pDisplay->Edid);
    pDisplay->EdidSize = EDID_BLOCK_SIZE;
}

// Reads a scanout's EDID into pDisplay. The built-in EDID stands in when the feature is not negotiated, and when the
// device refuses the command or answers with an invalid EDID. FALSE when the driver did not get the device's answer.
BOOLEAN VioGpuHostDisplays::ReadEdid(UINT ScanoutId, PVIOGPU_DISPLAY pDisplay)
{
    PAGED_CODE();

    if (!m_pAdapter->HasFeature(VIRTIO_GPU_F_EDID))
    {
        BuiltinEdid(pDisplay);
        return TRUE;
    }
    PGPU_RESP_EDID resp = reinterpret_cast<PGPU_RESP_EDID>(new (NonPagedPoolNx) BYTE[sizeof(GPU_RESP_EDID)]);
    if (resp == NULL)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s no memory for the answer: the read is retried\n", __FUNCTION__));
        return FALSE;
    }
    ULONG copied = 0;
    // As GET_DISPLAY_INFO (ReadDisplays).
    InterlockedExchange(&m_ReadOutstanding, 1);
    NTSTATUS status = m_pAdapter->GetCommands()->GetEdid(ScanoutId, resp, ReadLate, this);
    if (status != STATUS_IO_TIMEOUT)
    {
        InterlockedExchange(&m_ReadOutstanding, 0);
    }
    if (NT_SUCCESS(status))
    {
        VioGpuCommands::CopyEdid(resp, pDisplay->Edid, sizeof(pDisplay->Edid), &copied);
    }
    delete[] reinterpret_cast<PBYTE>(resp);
    if (VioGpuCommands::NoAnswer(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("<--- %s no answer to GET_EDID (0x%x): the read is retried\n", __FUNCTION__, status));
        return FALSE;
    }
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("%s: the device refused GET_EDID (0x%x): the built-in EDID stands in\n", __FUNCTION__, status));
        BuiltinEdid(pDisplay);
        return TRUE;
    }

    pDisplay->EdidSize = EdidValidSize(pDisplay->Edid, copied);
    if (pDisplay->EdidSize == 0)
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("%s: the host's EDID (%d bytes) is invalid: the built-in EDID stands in\n", __FUNCTION__, copied));
        BuiltinEdid(pDisplay);
        return TRUE;
    }
    ULONG blocks = pDisplay->EdidSize / EDID_BLOCK_SIZE;
    if (blocks <= pDisplay->Edid[EDID_OFFSET_EXTENSIONS])
    {
        // Windows asks for as many extensions as the base block announces: it announces the ones kept.
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("%s: extension %d of %d is missing or invalid, the EDID ends before it\n",
                  __FUNCTION__,
                  blocks,
                  pDisplay->Edid[EDID_OFFSET_EXTENSIONS]));
        pDisplay->Edid[EDID_OFFSET_EXTENSIONS] = (UCHAR)(blocks - 1);
        EdidSetChecksum(pDisplay->Edid);
    }
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("%s: %d bytes, %d extension(s)\n",
              __FUNCTION__,
              pDisplay->EdidSize,
              pDisplay->Edid[EDID_OFFSET_EXTENSIONS]));
    return TRUE;
}

// Adds a mode to pDisplay's modes, once, if it is at least 640x480 and the driver can back it.
void VioGpuHostDisplays::AddMode(PVIOGPU_DISPLAY pDisplay, USHORT xres, USHORT yres)
{
    PAGED_CODE();

    if ((xres < MIN_WIDTH_SIZE) || (yres < MIN_HEIGHT_SIZE))
    {
        DbgPrint(TRACE_LEVEL_VERBOSE, ("%s: (%dx%d) is below the minimum\n", __FUNCTION__, xres, yres));
        return;
    }
    // A display's EDID may describe modes far beyond what the driver can back.
    if (!m_pAdapter->ModeFits(xres, yres))
    {
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("%s: (%dx%d) is wider or taller than 8192, not taken\n", __FUNCTION__, xres, yres));
        return;
    }
    for (ULONG idx = 0; idx < pDisplay->ModeCount; idx++)
    {
        if ((pDisplay->Modes[idx].XResolution == xres) && (pDisplay->Modes[idx].YResolution == yres))
        {
            return;
        }
    }
    if (pDisplay->ModeCount >= ARRAYSIZE(pDisplay->Modes))
    {
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("%s: the display's mode list is full, (%dx%d) not taken\n", __FUNCTION__, xres, yres));
        return;
    }
    DbgPrint(TRACE_LEVEL_INFORMATION, (" x_res: %d, y_res: %d\n", xres, yres));
    pDisplay->Modes[pDisplay->ModeCount].XResolution = xres;
    pDisplay->Modes[pDisplay->ModeCount].YResolution = yres;
    pDisplay->ModeCount++;
}

// Builds pDisplay's modes from its EDID: 640x480 and 1024x768, the display's preferred mode, then the others the EDID
// describes.
void VioGpuHostDisplays::BuildModes(PVIOGPU_DISPLAY pDisplay)
{
    PAGED_CODE();

    VIOGPU_DISP_MODE modes[VIOGPU_MAX_MODES];
    VIOGPU_DISP_MODE preferred = {0};

    pDisplay->ModeCount = 0;
    AddMode(pDisplay, MIN_WIDTH_SIZE, MIN_HEIGHT_SIZE);
    AddMode(pDisplay, NOM_WIDTH_SIZE, NOM_HEIGHT_SIZE);

    ULONG count = EdidGetModes(pDisplay->Edid, pDisplay->EdidSize, modes, ARRAYSIZE(modes), &preferred);
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("%s: %d EDID mode(s), preferred %dx%d\n",
              __FUNCTION__,
              count,
              preferred.XResolution,
              preferred.YResolution));
    // The display's preferred mode first, so that a full table cannot drop it.
    if (preferred.XResolution != 0)
    {
        AddMode(pDisplay, preferred.XResolution, preferred.YResolution);
    }
    for (ULONG i = 0; i < count; i++)
    {
        AddMode(pDisplay, modes[i].XResolution, modes[i].YResolution);
    }
}

// Tells Windows whether the display on a scanout, its child of the same ID, is connected.
NTSTATUS VioGpuHostDisplays::UpdateChildStatus(UINT ScanoutId, BOOLEAN Connected)
{
    PAGED_CODE();
    NTSTATUS Status(STATUS_SUCCESS);
    DXGK_CHILD_STATUS ChildStatus;
    PDXGKRNL_INTERFACE pDXGKInterface(m_pAdapter->GetVioGpu()->GetDxgkInterface());

    RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));

    ChildStatus.Type = StatusConnection;
    ChildStatus.ChildUid = ScanoutId;
    ChildStatus.HotPlug.Connected = Connected;
    Status = pDXGKInterface->DxgkCbIndicateChildStatus(pDXGKInterface->DeviceHandle, &ChildStatus);
    // A local: DbgPrint does not parenthesize its level.
    UCHAR TraceLevel = NT_SUCCESS(Status) ? TRACE_LEVEL_INFORMATION : TRACE_LEVEL_ERROR;
    DbgPrint(TraceLevel, ("%s: child %d connected %d, status 0x%x\n", __FUNCTION__, ScanoutId, Connected, Status));
    return Status;
}

// Reads the host's displays at start, before the thread runs, and publishes them. Until the host answers, scanout 0's
// display counts as connected, with the built-in EDID: QEMU enables scanout 0 when it creates the device, and the
// virtio spec lets a driver without display information fall back to display 0. Without an answer the thread asks
// again.
void VioGpuHostDisplays::InitDisplays(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VioGpuDisplayController *pController = m_pAdapter->GetController();
    VioGpuDod *pVioGpuDod = m_pAdapter->GetVioGpu();
    // A display event the host raised before the driver started is no change since this read: cleared before it, as
    // ConfigChanged clears one, so that the thread's first pass does not take the host's size over the persistent
    // mode this read applies.
    if (m_pAdapter->ReadConfigEvents() & VIRTIO_GPU_EVENT_DISPLAY)
    {
        m_pAdapter->ClearConfigEvents(VIRTIO_GPU_EVENT_DISPLAY);
    }
    BOOLEAN read = ReadDisplays();
    if (!read)
    {
        Post(VIOGPU_WORK_REREAD);
    }
    for (UINT ScanoutId = 0; ScanoutId < pController->GetScanoutCount(); ++ScanoutId)
    {
        PVIOGPU_DISPLAY pAssumed = &m_Assumed;
        RtlZeroMemory(pAssumed, sizeof(*pAssumed));
        pAssumed->Connected = ScanoutId == 0;
        BuiltinEdid(pAssumed);
        BuildModes(pAssumed);
        pController->CustomMode(NOM_WIDTH_SIZE, NOM_HEIGHT_SIZE, &pAssumed->Custom);
        if (ScanoutId == 0 && pVioGpuDod->IsPersistentDispMode0Set())
        {
            pController->CustomMode(pVioGpuDod->GetPersistentDispMode0Width(),
                                    pVioGpuDod->GetPersistentDispMode0Height(),
                                    &pAssumed->Custom);
        }
        pController->Assume(ScanoutId, pAssumed);
        if (read)
        {
            BOOLEAN wasConnected = FALSE;
            pController->Publish(ScanoutId, &m_Next[ScanoutId], FALSE, &wasConnected);
        }
    }

    // Windows asks for the children's status once the device has started.
    m_ChildAnnounced = pController->GetConnectedMask();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PAGED_CODE_SEG_END

void VioGpuHostDisplays::Post(LONG Work)
{
    InterlockedOr(&m_WorkPosted, Work);
    KeSetEvent(&m_WorkEvent, IO_NO_INCREMENT, FALSE);
}

// The answer to a read that came too late, from the DPC, or from the queue's Close: the next read may go, now.
void VioGpuHostDisplays::ReadLate(_In_opt_ PVOID ctx, _In_ NTSTATUS status)
{
    UNREFERENCED_PARAMETER(status);
    VioGpuHostDisplays *pThis = reinterpret_cast<VioGpuHostDisplays *>(ctx);
    InterlockedExchange(&pThis->m_ReadOutstanding, 0);
    pThis->Post(VIOGPU_WORK_REREAD);
}

void VioGpuHostDisplays::ThreadWork(_In_ PVOID Context)
{
    reinterpret_cast<VioGpuHostDisplays *>(Context)->ThreadWorkRoutine();
}

void VioGpuHostDisplays::ThreadWorkRoutine(void)
{
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    // A display change the driver still has to read. An open device that did not answer is asked again later
    // (VIOGPU_RETRY_MAX_SHIFT). A closed device is not asked: the read waits for the next Open, which posts work.
    BOOLEAN pending = FALSE;
    // A display event whose read is still pending: it counts as one once the display is read.
    BOOLEAN event = FALSE;
    BOOLEAN retry = FALSE;
    ULONG failures = 0;
    for (;;)
    {
        // A second after the first failure, doubling with each failure after it (VIOGPU_RETRY_MAX_SHIFT).
        LARGE_INTEGER timeout;
        ULONG shift = failures > 0 ? min(failures - 1, (ULONG)VIOGPU_RETRY_MAX_SHIFT) : 0;
        timeout.QuadPart = -10000000LL * (1LL << shift);
        KeWaitForSingleObject(&m_WorkEvent, Executive, KernelMode, FALSE, retry ? &timeout : NULL);
        LONG work = InterlockedExchange(&m_WorkPosted, 0);

        if (work & VIOGPU_WORK_STOP)
        {
            PsTerminateSystemThread(STATUS_SUCCESS);
            break;
        }
        pending = pending || (work & VIOGPU_WORK_REREAD) != 0;
        if (!ExAcquireRundownProtection(&m_Rundown))
        {
            // The next open starts the retries over.
            retry = FALSE;
            failures = 0;
            continue;
        }
        pending = !ConfigChanged(pending, &event);
        ExReleaseRundownProtection(&m_Rundown);
        retry = pending;
        if (pending)
        {
            if (failures++ == 0)
            {
                DbgPrint(TRACE_LEVEL_FATAL, ("%s: display read failed, retried\n", __FUNCTION__));
            }
        }
        else if (failures != 0)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s: display read after %d failed tries\n", __FUNCTION__, failures));
            failures = 0;
        }
    }
}

// Reads the displays when the device raised a display event, now or before a read that failed, and with Reread. After
// a display event, or when the read finds a change, Windows is told about every display connected now or when it was
// last told, and viogpuap applies the custom mode to a connected one; a read without an event that finds no change
// reports nothing. FALSE when the displays could not be read, the read then staying pending with *pEvent, or
// when Windows could not be told about one, which is told again with the next pass.
BOOLEAN VioGpuHostDisplays::ConfigChanged(BOOLEAN Reread, PBOOLEAN pEvent)
{
    UINT32 events_read = m_pAdapter->ReadConfigEvents();
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("<--> %s events 0x%x, event %d, reread %d\n", __FUNCTION__, events_read, *pEvent, Reread));
    if (events_read & VIRTIO_GPU_EVENT_DISPLAY)
    {
        // Cleared before the displays are read: a change the host makes meanwhile raises the event again.
        m_pAdapter->ClearConfigEvents(VIRTIO_GPU_EVENT_DISPLAY);
        *pEvent = TRUE;
    }
    if (!*pEvent && !Reread)
    {
        return TRUE;
    }
    if (!ReadDisplays())
    {
        return FALSE;
    }
    BOOLEAN event = *pEvent;
    *pEvent = FALSE;
    VioGpuDisplayController *pController = m_pAdapter->GetController();
    BOOLEAN changed = FALSE;
    ULONG connected = 0;
    for (UINT ScanoutId = 0; ScanoutId < pController->GetScanoutCount(); ++ScanoutId)
    {
        BOOLEAN wasConnected = FALSE;
        if (pController->Publish(ScanoutId, &m_Next[ScanoutId], event, &wasConnected))
        {
            changed = TRUE;
        }
        if (m_Next[ScanoutId].Connected)
        {
            connected |= 1UL << ScanoutId;
        }
    }
    // Told once published, outside the display lock, which is never held across a call into dxgkrnl. Every connected
    // display is announced again on every change, so that Windows reads its EDID and modes again, and so is every
    // display Windows was last told is connected; a report that failed is made again. A failed announcement does not
    // stop the others, nor hold viogpuap back.
    ULONG tell = m_AnnounceFailed;
    if (event || changed)
    {
        tell |= connected | m_ChildAnnounced;
    }
    ULONG failed = 0;
    for (UINT ScanoutId = 0; ScanoutId < pController->GetScanoutCount(); ++ScanoutId)
    {
        ULONG bit = 1UL << ScanoutId;
        if (!(tell & bit))
        {
            continue;
        }
        if (!NT_SUCCESS(UpdateChildStatus(ScanoutId, (connected & bit) != 0)))
        {
            failed |= bit;
        }
        else if (connected & bit)
        {
            m_ChildAnnounced |= bit;
        }
        else
        {
            m_ChildAnnounced &= ~bit;
        }
    }
    m_AnnounceFailed = failed;
    // viogpuap applies the custom mode to a connected display; with none connected there is nothing to apply.
    if ((event || changed) && connected != 0)
    {
        m_pAdapter->GetHelperChannel()->Notify();
    }
    if (failed != 0)
    {
        // Told again with the next pass: Windows would keep the old state.
        DbgPrint(TRACE_LEVEL_ERROR, ("%s: children 0x%x not told, told again\n", __FUNCTION__, failed));
        return FALSE;
    }
    return TRUE;
}
