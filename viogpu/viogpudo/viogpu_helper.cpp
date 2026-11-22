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
}

// Closes the event once the config-change thread, which sets it, has stopped.
void VioGpuHelperChannel::Stop(void)
{
    PAGED_CODE();
    CloseEvent();
}

NTSTATUS VioGpuHelperChannel::Escape(_In_ CONST DXGKARG_ESCAPE *pEscape)
{
    PAGED_CODE();
    PVIOGPU_ESCAPE pVioGpuEscape = (PVIOGPU_ESCAPE)pEscape->pPrivateDriverData;
    NTSTATUS status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    UINT size = pEscape->PrivateDriverDataSize;
    if (size < sizeof(PVIOGPU_ESCAPE))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("%s buffer too small %d, should be at least %d\n",
                  __FUNCTION__,
                  pEscape->PrivateDriverDataSize,
                  size));
        return STATUS_INVALID_BUFFER_SIZE;
    }

    switch (pVioGpuEscape->Type)
    {
        case VIOGPU_GET_DEVICE_ID:
            {
                CreateEvent();
                size = sizeof(ULONG);
                if (pVioGpuEscape->DataLength < size)
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("%s buffer too small %d, should be at least %d\n",
                              __FUNCTION__,
                              pVioGpuEscape->DataLength,
                              size));
                    return STATUS_INVALID_BUFFER_SIZE;
                }
                pVioGpuEscape->Id = m_pAdapter->GetInstanceId();
                break;
            }
        case VIOGPU_GET_CUSTOM_RESOLUTION:
            {
                size = sizeof(VIOGPU_DISP_MODE);
                if (pVioGpuEscape->DataLength < size)
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("%s buffer too small %d, should be at least %d\n",
                              __FUNCTION__,
                              pVioGpuEscape->DataLength,
                              size));
                    return STATUS_INVALID_BUFFER_SIZE;
                }
                pVioGpuEscape->Resolution = m_pAdapter->GetController()->GetCustomMode(0);
                break;
            }
        case VIOGPU_SET_CUSTOM_RESOLUTION:
            {
                size = sizeof(VIOGPU_DISP_MODE);
                if (pVioGpuEscape->DataLength < size)
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("%s buffer too small %d, should be at least %d\n",
                              __FUNCTION__,
                              pVioGpuEscape->DataLength,
                              size));
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

                // A size the driver cannot back is refused before it is saved.
                if (!m_pAdapter->GetController()->SetPersistentMode(0,
                                                                    pVioGpuEscape->Resolution.XResolution,
                                                                    pVioGpuEscape->Resolution.YResolution))
                {
                    return STATUS_INVALID_PARAMETER;
                }
                m_pAdapter->GetVioGpu()->SetRegisterConfigInfo();
                break;
            }
        default:
            DbgPrint(TRACE_LEVEL_ERROR, ("%s: invalid Escape type 0x%x\n", __FUNCTION__, pVioGpuEscape->Type));
            status = STATUS_INVALID_PARAMETER;
    }

    return status;
}

VOID VioGpuHelperChannel::CreateEvent(VOID)
{
    PAGED_CODE();

    if (m_pEvent != NULL && m_EventHandle != NULL)
    {
        return;
    }
    DECLARE_UNICODE_STRING_SIZE(DeviceNumber, 10);
    DECLARE_UNICODE_STRING_SIZE(EventName, 256);

    RtlIntegerToUnicodeString(m_pAdapter->GetInstanceId(), 10, &DeviceNumber);
    NTSTATUS status = RtlUnicodeStringPrintf(&EventName,
                                             L"%ws%ws%ws",
                                             BASE_NAMED_OBJECTS,
                                             RESOLUTION_EVENT_NAME,
                                             DeviceNumber.Buffer);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("RtlUnicodeStringPrintf failed 0x%x\n", status));
        return;
    }
    m_pEvent = IoCreateNotificationEvent(&EventName, &m_EventHandle);
    if (m_pEvent == NULL)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--> %s\n", __FUNCTION__));
        return;
    }
    KeClearEvent(m_pEvent);
    ObReferenceObject(m_pEvent);
}

VOID VioGpuHelperChannel::Notify(VOID)
{
    PAGED_CODE();

    if (m_pEvent != NULL)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("Notify\n"));
        KeSetEvent(m_pEvent, IO_NO_INCREMENT, FALSE);
        KeClearEvent(m_pEvent);
    }
}

VOID VioGpuHelperChannel::CloseEvent(VOID)
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
}

PAGED_CODE_SEG_END
