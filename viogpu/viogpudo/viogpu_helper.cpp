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
#include "viogpu_helper.h"

#if !DBG
#include "viogpu_helper.tmh"
#endif

PAGED_CODE_SEG_BEGIN

VioGpuHelperChannel::VioGpuHelperChannel(void)
{
    PAGED_CODE();
    m_pAdapter = NULL;
    m_pEvent = NULL;
    m_EventHandle = NULL;
    RtlZeroMemory(m_SourceEvents, sizeof(m_SourceEvents));
    RtlZeroMemory(m_SourceEventHandles, sizeof(m_SourceEventHandles));
    RtlZeroMemory(m_Applied, sizeof(m_Applied));
    m_AppliedStamp = 0;
    KeInitializeGuardedMutex(&m_AppliedMutex);
}

// The generation the helper last applied to a source in a layout, 0 for none.
ULONG VioGpuHelperChannel::GetApplied(UINT SourceId, ULONGLONG Layout)
{
    PAGED_CODE();

    ULONG Generation = 0;
    KeAcquireGuardedMutex(&m_AppliedMutex);
    for (UINT i = 0; i < VIOGPU_APPLIED_LAYOUTS; ++i)
    {
        if (m_Applied[SourceId][i].Generation != 0 && m_Applied[SourceId][i].Layout == Layout)
        {
            Generation = m_Applied[SourceId][i].Generation;
            break;
        }
    }
    KeReleaseGuardedMutex(&m_AppliedMutex);
    return Generation;
}

// Records the generation the helper applied to a source in a layout, in the layout's entry, or in the one set least
// recently.
VOID VioGpuHelperChannel::SetApplied(UINT SourceId, ULONGLONG Layout, ULONG Generation)
{
    PAGED_CODE();

    KeAcquireGuardedMutex(&m_AppliedMutex);
    UINT Slot = 0;
    for (UINT i = 0; i < VIOGPU_APPLIED_LAYOUTS; ++i)
    {
        if (m_Applied[SourceId][i].Generation != 0 && m_Applied[SourceId][i].Layout == Layout)
        {
            Slot = i;
            break;
        }
        if (m_Applied[SourceId][i].Stamp < m_Applied[SourceId][Slot].Stamp)
        {
            Slot = i;
        }
    }
    m_Applied[SourceId][Slot].Layout = Layout;
    m_Applied[SourceId][Slot].Generation = Generation;
    m_Applied[SourceId][Slot].Stamp = ++m_AppliedStamp;
    KeReleaseGuardedMutex(&m_AppliedMutex);
}

void VioGpuHelperChannel::Stop(void)
{
    PAGED_CODE();
    CloseEvents();
}

// Whether an escape's data fits: the helper says it sent at least size bytes after the header, and the buffer dxgkrnl
// copied holds them. Helpers built before VIOGPU_GET_SOURCE_RESOLUTION send an 8-byte VIOGPU_ESCAPE, which the data of
// the escapes they know fits in.
static BOOLEAN EscapeDataFits(_In_ CONST DXGKARG_ESCAPE *pEscape, UINT size)
{
    PAGED_CODE();

    PVIOGPU_ESCAPE pVioGpuEscape = (PVIOGPU_ESCAPE)pEscape->pPrivateDriverData;
    UINT header = (UINT)FIELD_OFFSET(VIOGPU_ESCAPE, Id);
    if (pVioGpuEscape->DataLength >= size && pEscape->PrivateDriverDataSize >= header + size)
    {
        return TRUE;
    }
    DbgPrint(TRACE_LEVEL_ERROR,
             ("%s escape 0x%x: %d bytes of data in a %d-byte buffer, needs %d after the %d-byte header\n",
              __FUNCTION__,
              pVioGpuEscape->Type,
              pVioGpuEscape->DataLength,
              pEscape->PrivateDriverDataSize,
              size,
              header));
    return FALSE;
}

NTSTATUS VioGpuHelperChannel::Escape(_In_ CONST DXGKARG_ESCAPE *pEscape)
{
    PAGED_CODE();
    PVIOGPU_ESCAPE pVioGpuEscape = (PVIOGPU_ESCAPE)pEscape->pPrivateDriverData;
    NTSTATUS status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    // The header, Type and DataLength; each escape checks its data (EscapeDataFits).
    if (pEscape->PrivateDriverDataSize < (UINT)FIELD_OFFSET(VIOGPU_ESCAPE, Id))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("%s buffer too small %d, the header takes %d\n",
                  __FUNCTION__,
                  pEscape->PrivateDriverDataSize,
                  (UINT)FIELD_OFFSET(VIOGPU_ESCAPE, Id)));
        return STATUS_INVALID_BUFFER_SIZE;
    }

    VioGpuDisplayController *pController = m_pAdapter->GetController();
    switch (pVioGpuEscape->Type)
    {
        case VIOGPU_GET_DEVICE_ID:
            {
                if (!EscapeDataFits(pEscape, sizeof(ULONG)))
                {
                    return STATUS_INVALID_BUFFER_SIZE;
                }
                // The helper opens the resolution events next: they are made now, with any an earlier call could
                // not make.
                CreateEvents();
                pVioGpuEscape->Id = m_pAdapter->GetInstanceId();
                break;
            }
        case VIOGPU_GET_CUSTOM_RESOLUTION:
            {
                if (!EscapeDataFits(pEscape, sizeof(VIOGPU_DISP_MODE)))
                {
                    return STATUS_INVALID_BUFFER_SIZE;
                }
                status = pController->GetEscapeMode(&pVioGpuEscape->Resolution);
                break;
            }
        case VIOGPU_SET_CUSTOM_RESOLUTION:
            {
                if (!EscapeDataFits(pEscape, sizeof(VIOGPU_DISP_MODE)))
                {
                    return STATUS_INVALID_BUFFER_SIZE;
                }
                if (pVioGpuEscape->Resolution.XResolution <= 0 || pVioGpuEscape->Resolution.YResolution <= 0)
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("%s PersistentDispMode0 width %d and height %d should be > 0\n",
                              __FUNCTION__,
                              pVioGpuEscape->Resolution.XResolution,
                              pVioGpuEscape->Resolution.YResolution));
                    return STATUS_INVALID_PARAMETER;
                }

                DbgPrint(TRACE_LEVEL_INFORMATION,
                         ("%s PersistentDispMode0 width %d, height %d\n",
                          __FUNCTION__,
                          pVioGpuEscape->Resolution.XResolution,
                          pVioGpuEscape->Resolution.YResolution));

                // A size the driver cannot back is refused before it is saved. The persistent mode is scanout 0's.
                UINT ScanoutId = 0;
                BOOLEAN changed = FALSE;
                VIOGPU_DISP_MODE save;
                status = pController->SetEscapeMode(pVioGpuEscape->Resolution.XResolution,
                                                    pVioGpuEscape->Resolution.YResolution,
                                                    &ScanoutId,
                                                    &changed,
                                                    &save);
                if (!NT_SUCCESS(status))
                {
                    return status;
                }
                // Saved from what the controller returned, not read back: another escape may change it meanwhile.
                if (save.XResolution != 0)
                {
                    status = m_pAdapter->GetVioGpu()->SetRegisterConfigInfo(save.XResolution, save.YResolution);
                }
                // The helpers learn the new generation now, and find the size applied: one they learned later would
                // apply it again, over a size the user chose since.
                if (changed)
                {
                    Notify();
                }
                break;
            }
        case VIOGPU_GET_SOURCE_RESOLUTION:
            {
                if (!EscapeDataFits(pEscape, sizeof(VIOGPU_SOURCE_MODE)))
                {
                    return STATUS_INVALID_BUFFER_SIZE;
                }
                status = pController->GetSourceMode(&pVioGpuEscape->SourceMode);
                pVioGpuEscape->SourceMode.Applied = 0;
                if (NT_SUCCESS(status))
                {
                    pVioGpuEscape->SourceMode.Applied = GetApplied(pVioGpuEscape->SourceMode.SourceId,
                                                                   pVioGpuEscape->SourceMode.Layout);
                }
                break;
            }
        case VIOGPU_SET_SOURCE_APPLIED:
            {
                if (!EscapeDataFits(pEscape, sizeof(VIOGPU_SOURCE_MODE)))
                {
                    return STATUS_INVALID_BUFFER_SIZE;
                }
                if (pVioGpuEscape->SourceMode.SourceId >= pController->GetSourceCount() ||
                    pVioGpuEscape->SourceMode.Generation == 0)
                {
                    return STATUS_INVALID_PARAMETER;
                }
                SetApplied(pVioGpuEscape->SourceMode.SourceId,
                           pVioGpuEscape->SourceMode.Layout,
                           pVioGpuEscape->SourceMode.Generation);
                break;
            }
        default:
            DbgPrint(TRACE_LEVEL_ERROR, ("%s: invalid Escape type 0x%x\n", __FUNCTION__, pVioGpuEscape->Type));
            status = STATUS_INVALID_PARAMETER;
    }

    return status;
}

// Makes a named event, or opens the one of that name, and publishes it in *ppEvent unless another caller published it
// first: then it drops its reference and handle. No lock is held: IoCreate*Event must run with APCs enabled (Driver
// Verifier's IrqlIoPassive4 rule). An event starts set. The device's notification event is cleared, or a helper
// waiting on it would spin; a per-source auto-reset event is left set, which costs its helper one sync and loses no
// signal another caller set meanwhile.
static void PublishEvent(PUNICODE_STRING pName, BOOLEAN Synchronization, PKEVENT *ppEvent, PHANDLE pHandle)
{
    PAGED_CODE();

    HANDLE handle = NULL;
    PKEVENT pEvent = Synchronization ? IoCreateSynchronizationEvent(pName, &handle)
                                     : IoCreateNotificationEvent(pName, &handle);
    if (pEvent == NULL)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s: no event %wZ\n", __FUNCTION__, pName));
        return;
    }
    if (!Synchronization)
    {
        KeClearEvent(pEvent);
    }
    ObReferenceObject(pEvent);
    if (InterlockedCompareExchangePointer((PVOID volatile *)ppEvent, pEvent, NULL) != NULL)
    {
        ObDereferenceObject(pEvent);
        ZwClose(handle);
        return;
    }
    *pHandle = handle;
}

// Makes the resolution events (viogpum.h) that do not exist yet: VIOGPU_GET_DEVICE_ID does, in the helper's context.
// Escapes may run alongside each other, and alongside the threads that set the events.
VOID VioGpuHelperChannel::CreateEvents(VOID)
{
    PAGED_CODE();

    DECLARE_UNICODE_STRING_SIZE(DeviceNumber, 10);
    DECLARE_UNICODE_STRING_SIZE(EventName, 256);

    ULONG Id = m_pAdapter->GetInstanceId();
    NTSTATUS status = RtlIntegerToUnicodeString(Id, 10, &DeviceNumber);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s: no name for device %d (0x%x)\n", __FUNCTION__, Id, status));
        return;
    }
    if (m_pEvent == NULL)
    {
        status = RtlUnicodeStringPrintf(&EventName,
                                        L"%ws%ws%wZ",
                                        BASE_NAMED_OBJECTS,
                                        RESOLUTION_EVENT_NAME,
                                        &DeviceNumber);
        if (NT_SUCCESS(status))
        {
            PublishEvent(&EventName, FALSE, &m_pEvent, &m_EventHandle);
        }
        else
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s: no name for the device's event (0x%x)\n", __FUNCTION__, status));
        }
    }
    for (UINT SourceId = 0; SourceId < m_pAdapter->GetController()->GetSourceCount(); ++SourceId)
    {
        // Without its own event the source's helper waits on the device's, and may miss a change.
        if (m_SourceEvents[SourceId] != NULL)
        {
            continue;
        }
        status = RtlUnicodeStringPrintf(&EventName,
                                        L"%ws%ws%wZ_%u",
                                        BASE_NAMED_OBJECTS,
                                        RESOLUTION_EVENT_NAME,
                                        &DeviceNumber,
                                        SourceId);
        if (NT_SUCCESS(status))
        {
            PublishEvent(&EventName, TRUE, &m_SourceEvents[SourceId], &m_SourceEventHandles[SourceId]);
        }
        else
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s: no name for source %d's event (0x%x)\n", __FUNCTION__, SourceId, status));
        }
    }
}

// The host's displays changed: the helpers read each display's size again and apply a new generation. With no display
// connected there is nothing to apply.
void VioGpuHelperChannel::DisplaysChanged(ULONG ConnectedMask)
{
    PAGED_CODE();

    if (ConnectedMask != 0)
    {
        Notify();
    }
}

// Tells the helpers that a display changed: pulses the device's event, for helpers that know no other, and sets every
// source's.
VOID VioGpuHelperChannel::Notify(VOID)
{
    PAGED_CODE();

    ULONG set = 0;
    if (m_pEvent != NULL)
    {
        KeSetEvent(m_pEvent, IO_NO_INCREMENT, FALSE);
        KeClearEvent(m_pEvent);
    }
    for (UINT SourceId = 0; SourceId < MAX_VIEWS; ++SourceId)
    {
        if (m_SourceEvents[SourceId] != NULL)
        {
            KeSetEvent(m_SourceEvents[SourceId], IO_NO_INCREMENT, FALSE);
            set |= 1UL << SourceId;
        }
    }
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("%s: device event %s, source events 0x%x\n", __FUNCTION__, m_pEvent ? "pulsed" : "none", set));
}

// Wakes one source's helper. A source without its own event has a helper waiting on the device's, which is pulsed
// instead.
VOID VioGpuHelperChannel::NotifySource(UINT SourceId)
{
    PAGED_CODE();

    if (SourceId >= MAX_VIEWS)
    {
        return;
    }
    if (m_SourceEvents[SourceId] != NULL)
    {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s: source %d\n", __FUNCTION__, SourceId));
        KeSetEvent(m_SourceEvents[SourceId], IO_NO_INCREMENT, FALSE);
    }
    else if (m_pEvent != NULL)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("%s: source %d has no event: the device's is pulsed\n", __FUNCTION__, SourceId));
        KeSetEvent(m_pEvent, IO_NO_INCREMENT, FALSE);
        KeClearEvent(m_pEvent);
    }
}

// A commit that moves a source to another scanout, or onto one, wakes the source's helper, whether or not the mode set
// succeeded: the helper may have read the size of the scanout the source had, while it handled the display change
// that led to the move.
void VioGpuHelperChannel::SourceCommitted(UINT SourceId, UINT OldScanoutId, UINT NewScanoutId)
{
    PAGED_CODE();

    if (NewScanoutId != OldScanoutId && NewScanoutId < m_pAdapter->GetController()->GetScanoutCount())
    {
        NotifySource(SourceId);
    }
}

// Closes the resolution events: no escape runs, and the config-change thread has stopped.
VOID VioGpuHelperChannel::CloseEvents(VOID)
{
    PAGED_CODE();

    if (m_EventHandle != NULL)
    {
        ZwClose(m_EventHandle);
        m_EventHandle = NULL;
    }
    if (m_pEvent != NULL)
    {
        ObDereferenceObject(m_pEvent);
        m_pEvent = NULL;
    }
    for (UINT SourceId = 0; SourceId < MAX_VIEWS; ++SourceId)
    {
        if (m_SourceEventHandles[SourceId] != NULL)
        {
            ZwClose(m_SourceEventHandles[SourceId]);
            m_SourceEventHandles[SourceId] = NULL;
        }
        if (m_SourceEvents[SourceId] != NULL)
        {
            ObDereferenceObject(m_SourceEvents[SourceId]);
            m_SourceEvents[SourceId] = NULL;
        }
    }
}

PAGED_CODE_SEG_END
