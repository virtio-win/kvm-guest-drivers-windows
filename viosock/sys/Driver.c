/*
 * Main driver file containing DriverEntry and driver related functions
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
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

#include "precomp.h"
#include "viosock.h"

#if defined(EVENT_TRACING)
#include "Driver.tmh"
#endif

DRIVER_INITIALIZE DriverEntry;
VOID VIOSockTimerInit();

// Context cleanup callbacks generally run at IRQL <= DISPATCH_LEVEL but
// WDFDRIVER context cleanup is guaranteed to run at PASSIVE_LEVEL.
// Annotate the prototype to make static analysis happy.
EVT_WDF_OBJECT_CONTEXT_CLEANUP _IRQL_requires_(PASSIVE_LEVEL) VIOSockEvtDriverContextCleanup;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, VIOSockEvtDriverContextCleanup)
#pragma alloc_text(INIT, VIOSockTimerInit)
#pragma alloc_text(PAGE, VIOSockTimerCreate)
#endif

ULONG ulTimeIncrement;
LONGLONG llTimerToleranceTicks;

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDRIVER Driver;

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    InitializeDebugPrints(DriverObject, RegistryPath);

    TraceEvents(TRACE_LEVEL_INFORMATION,
                DBG_INIT,
                "VirtioSocket driver started...built on %s %s\n",
                __DATE__,
                __TIME__);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = VIOSockEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, VIOSockEvtDeviceAdd);
    config.DriverPoolTag = VIOSOCK_DRIVER_MEMORY_TAG;

    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes, &config, &Driver);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfDriverCreate failed - 0x%x\n", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    VIOSockTimerInit();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}

VOID VIOSockEvtDriverContextCleanup(IN WDFOBJECT Driver)
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)Driver));
}

VOID VIOSockTimerInit()
{
    ulTimeIncrement = KeQueryTimeIncrement();
    llTimerToleranceTicks = VIOSOCK_TIMER_TOLERANCE / ulTimeIncrement;
}

NTSTATUS VIOSockTimerCreate(IN PVIOSOCK_TIMER pTimer, IN WDFOBJECT ParentObject, IN PFN_WDF_TIMER EvtTimerFunc)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_TIMER_CONFIG timerConfig;

    PAGED_CODE();

    pTimer->Deadline = MAXLONGLONG;
    pTimer->StartRefs = 0;

    WDF_TIMER_CONFIG_INIT(&timerConfig, EvtTimerFunc);

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = ParentObject;

    return WdfTimerCreate(&timerConfig, &Attributes, &pTimer->Timer);
}

VOID VIOSockTimerStartDeadline(IN PVIOSOCK_TIMER pTimer, IN PVIOSOCK_TIMER_ENTRY pEntry OPTIONAL, IN LONGLONG Deadline)
{
    BOOLEAN bSetTimer = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_SOCKET, "--> %s\n", __FUNCTION__);

    ASSERT(Deadline > 0 && Deadline != MAXLONGLONG);
    if (!Deadline || Deadline == MAXLONGLONG)
    {
        return;
    }

    ++pTimer->StartRefs;
    if (pEntry)
    {
        pEntry->Deadline = Deadline;
    }

    ASSERT(!VIOSockTimerDeadlineIsExpired(Deadline));
    // pTimer->Deadline is MAXLONGLONG when the timer is not running (after Create or Cancel).
    // The else branch for 0 is unreachable: VIOSockTimerCreate initializes to MAXLONGLONG.
    if (pTimer->Deadline != MAXLONGLONG)
    {
        if (Deadline + VIOSOCK_TIMER_TOLERANCE_TICKS < pTimer->Deadline)
        {
            bSetTimer = TRUE;
        }
    }
    else
    {
        bSetTimer = TRUE;
    }

    if (bSetTimer)
    {
        pTimer->Deadline = Deadline;
        WdfTimerStart(pTimer->Timer, -VIOSockTimerDeadlineToTimeout(Deadline));
    }
}
