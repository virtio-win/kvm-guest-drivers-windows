/*
 * WMI data structures for the VirtIO Balloon driver.
 * Hand-written to match balloon.mof definitions.
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
#ifndef _BALLOONWMI_H_
#define _BALLOONWMI_H_

// {F06FF16E-E670-4306-A698-C9A008FD7E40}
#define BalloonWmi_HealthInfo_Guid \
    { 0xf06ff16e, 0xe670, 0x4306, { 0xa6, 0x98, 0xc9, 0xa0, 0x08, 0xfd, 0x7e, 0x40 } }

#if !(defined(MIDL_PASS))
DEFINE_GUID(BalloonHealthInfoGuid_GUID,
    0xf06ff16e, 0xe670, 0x4306, 0xa6, 0x98, 0xc9, 0xa0, 0x08, 0xfd, 0x7e, 0x40);
#endif

#include <pshpack4.h>

typedef struct _BalloonHealthInfo
{
    ULONG CurrentPages;
#define BalloonHealthInfo_CurrentPages_SIZE sizeof(ULONG)
#define BalloonHealthInfo_CurrentPages_ID   1

    ULONG TargetPages;
#define BalloonHealthInfo_TargetPages_SIZE sizeof(ULONG)
#define BalloonHealthInfo_TargetPages_ID   2

    ULONG NumVirtQueues;
#define BalloonHealthInfo_NumVirtQueues_SIZE sizeof(ULONG)
#define BalloonHealthInfo_NumVirtQueues_ID   3

    ULONG TotalInflateOps;
#define BalloonHealthInfo_TotalInflateOps_SIZE sizeof(ULONG)
#define BalloonHealthInfo_TotalInflateOps_ID   4

    ULONG TotalDeflateOps;
#define BalloonHealthInfo_TotalDeflateOps_SIZE sizeof(ULONG)
#define BalloonHealthInfo_TotalDeflateOps_ID   5

    ULONG InflateFailures;
#define BalloonHealthInfo_InflateFailures_SIZE sizeof(ULONG)
#define BalloonHealthInfo_InflateFailures_ID   6

    ULONG LowMemInflateRejects;
#define BalloonHealthInfo_LowMemInflateRejects_SIZE sizeof(ULONG)
#define BalloonHealthInfo_LowMemInflateRejects_ID   7

    ULONG HostAckTimeouts;
#define BalloonHealthInfo_HostAckTimeouts_SIZE sizeof(ULONG)
#define BalloonHealthInfo_HostAckTimeouts_ID   8

    ULONG D0EntryCount;
#define BalloonHealthInfo_D0EntryCount_SIZE sizeof(ULONG)
#define BalloonHealthInfo_D0EntryCount_ID   9

    ULONG D0ExitCount;
#define BalloonHealthInfo_D0ExitCount_SIZE sizeof(ULONG)
#define BalloonHealthInfo_D0ExitCount_ID   10

    ULONG LastD0EntryStatus;
#define BalloonHealthInfo_LastD0EntryStatus_SIZE sizeof(ULONG)
#define BalloonHealthInfo_LastD0EntryStatus_ID   11

    ULONG DpcCount;
#define BalloonHealthInfo_DpcCount_SIZE sizeof(ULONG)
#define BalloonHealthInfo_DpcCount_ID   12

    ULONG StatRequestsFromHost;
#define BalloonHealthInfo_StatRequestsFromHost_SIZE sizeof(ULONG)
#define BalloonHealthInfo_StatRequestsFromHost_ID   13

    ULONG StatResponsesSent;
#define BalloonHealthInfo_StatResponsesSent_SIZE sizeof(ULONG)
#define BalloonHealthInfo_StatResponsesSent_ID   14

    ULONGLONG NegotiatedFeatures;
#define BalloonHealthInfo_NegotiatedFeatures_SIZE sizeof(ULONGLONG)
#define BalloonHealthInfo_NegotiatedFeatures_ID   15

    BOOLEAN WorkerThreadRunning;
#define BalloonHealthInfo_WorkerThreadRunning_SIZE sizeof(BOOLEAN)
#define BalloonHealthInfo_WorkerThreadRunning_ID   16

    BOOLEAN ServiceConnected;
#define BalloonHealthInfo_ServiceConnected_SIZE sizeof(BOOLEAN)
#define BalloonHealthInfo_ServiceConnected_ID   17

    BOOLEAN FeatureStatVQ;
#define BalloonHealthInfo_FeatureStatVQ_SIZE sizeof(BOOLEAN)
#define BalloonHealthInfo_FeatureStatVQ_ID   18

    BOOLEAN SurpriseRemoval;
#define BalloonHealthInfo_SurpriseRemoval_SIZE sizeof(BOOLEAN)
#define BalloonHealthInfo_SurpriseRemoval_ID   19

    ULONG LastPowerState;
#define BalloonHealthInfo_LastPowerState_SIZE sizeof(ULONG)
#define BalloonHealthInfo_LastPowerState_ID   20

} BalloonHealthInfo, *PBalloonHealthInfo;

#include <poppack.h>

#define BalloonHealthInfo_SIZE \
    (FIELD_OFFSET(BalloonHealthInfo, LastPowerState) + BalloonHealthInfo_LastPowerState_SIZE)

#endif /* _BALLOONWMI_H_ */
