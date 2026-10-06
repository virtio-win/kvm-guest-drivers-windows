/*
 * Crash dump data structures for the VirtIO Balloon driver.
 * Shared between the kernel driver and dump parser tools.
 *
 * Copyright (c) 2026 Red Hat, Inc.
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
#ifndef _BALLOON_DEBUGDATA_H_
#define _BALLOON_DEBUGDATA_H_

#define BALLOON_HISTORY_SIZE                1024
#define BALLOON_BUGCHECK_VERSION            1

// {266FD447-31AD-45AB-97A4-8D4AD166B3D0}
#define BalloonCrash_Guid \
    { 0x266fd447, 0x31ad, 0x45ab, { 0x97, 0xa4, 0x8d, 0x4a, 0xd1, 0x66, 0xb3, 0xd0 } }

#if !(defined(MIDL_PASS))
DEFINE_GUID(BalloonCrashGuid,
    0x266fd447, 0x31ad, 0x45ab, 0x97, 0xa4, 0x8d, 0x4a, 0xd1, 0x66, 0xb3, 0xd0);
#endif

typedef enum _BALLOON_HISTORY_OP
{
    BalloonOpInflate,        // (pages_requested, current_num_pages)
    BalloonOpInflateFail,    // (pages_requested, NTSTATUS)
    BalloonOpDeflate,        // (pages_released, current_num_pages)
    BalloonOpLowMemReject,   // (pages_requested, 0)
    BalloonOpHostAckTimeout, // (0, 0)
    BalloonOpStatRequest,    // (0, 0) - host requested stats via virtqueue
    BalloonOpStatSent,       // (0, 0) - stats buffer re-queued to host
    BalloonOpWorkerWake,     // (abs(diff), 1=inflate 2=deflate 0=none)
    BalloonOpD0Entry,        // (previous_power_state, 0)
    BalloonOpD0Exit,         // (target_power_state, 0)
    BalloonOpSurpriseRemoval,// (0, 0)
    BalloonOpInit,           // (num_virtqueues, 0)
    BalloonOpTerm,           // (0, 0)
    BalloonOpD0EntryFail,    // (NTSTATUS, previous_power_state)
    BalloonOpTellHostStart,  // (vq_index, num_pfns)
    BalloonOpTellHostDone,   // (vq_index, duration_ms)
} BALLOON_HISTORY_OP;

#include <pshpack4.h>

typedef struct _BALLOON_HISTORY_ENTRY
{
    LARGE_INTEGER Timestamp;
    ULONG Operation;
    ULONG Param1;
    ULONG Param2;
} BALLOON_HISTORY_ENTRY, *PBALLOON_HISTORY_ENTRY;

typedef struct _BALLOON_BUGCHECK_DATA
{
    ULONG Version;
    ULONG CurrentPages;
    ULONG TargetPages;
    ULONG TotalInflateOps;
    ULONG TotalDeflateOps;
    ULONG InflateFailures;
    ULONG LowMemInflateRejects;
    ULONG HistorySize;
    LONG HistoryIndex;
    LARGE_INTEGER CrashTime;
    BOOLEAN SurpriseRemoval;
    BOOLEAN FeatureStatVQ;
    BOOLEAN WorkerThreadRunning;
    BOOLEAN ServiceConnected;
    ULONG HostAckTimeouts;
    ULONG D0EntryCount;
    ULONG D0ExitCount;
    ULONG LastD0EntryStatus;
    ULONG LastPowerState;
    ULONG DpcCount;
    ULONG StatRequestsFromHost;
    ULONG StatResponsesSent;
    ULONGLONG NegotiatedFeatures;
    BALLOON_HISTORY_ENTRY History[BALLOON_HISTORY_SIZE];
} BALLOON_BUGCHECK_DATA, *PBALLOON_BUGCHECK_DATA;

#include <poppack.h>

#endif /* _BALLOON_DEBUGDATA_H_ */
