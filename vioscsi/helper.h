/*
 * Virtio block device include module.
 *
 * Copyright (c) 2012-2017 Red Hat, Inc.
 *
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@redhat.com>
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
#ifndef ___HELPER_H___
#define ___HELPER_H___


#include <ntddk.h>
#include <storport.h>
#include <ntddscsi.h>


#include "osdep.h"
#include "virtio_pci.h"
#include "vioscsi.h"

#define CHECKBIT(value, nbit) virtio_is_feature_enabled(value, nbit)
#define CHECKFLAG(value, flag) ((value & (flag)) == flag)
#define SETFLAG(value, flag) (value |= (flag))

#define CACHE_LINE_SIZE 64
#define ROUND_TO_CACHE_LINES(Size)  (((ULONG_PTR)(Size) + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1))

#include <srbhelper.h>

// Note: SrbGetCdbLength is defined in srbhelper.h
FORCEINLINE ULONG
SrbGetCdbLength32(_In_ PVOID Srb) {
    ULONG CdbLen32 = 0;
    UCHAR CdbLen8 = 0;
    SrbGetScsiData(Srb, &CdbLen8, &CdbLen32, NULL, NULL, NULL);
    return (CdbLen8 != 0) ? CdbLen8 : CdbLen32;
}

FORCEINLINE VOID
SrbGetPnpInfo(_In_ PVOID Srb, ULONG* PnPFlags, ULONG* PnPAction) {
    PSCSI_PNP_REQUEST_BLOCK pPnpSrb = NULL;
    PSRBEX_DATA_PNP pSrbExPnp = NULL;
    pSrbExPnp = (PSRBEX_DATA_PNP)SrbGetSrbExDataByType(
        (PSTORAGE_REQUEST_BLOCK)Srb, SrbExDataTypePnP);
    if (pSrbExPnp != NULL) {
        *PnPFlags = pSrbExPnp->SrbPnPFlags;
        *PnPAction = pSrbExPnp->PnPAction;
    }
    else {
        pPnpSrb = (PSCSI_PNP_REQUEST_BLOCK)Srb;
        *PnPFlags = pPnpSrb->SrbPnPFlags;
        *PnPAction = pPnpSrb->PnPAction;
    }
}
#define PSRB_TYPE PSTORAGE_REQUEST_BLOCK
#define PSRB_WMI_DATA PSRBEX_DATA_WMI
#define PSTOR_DEVICE_CAPABILITIES_TYPE PSTOR_DEVICE_CAPABILITIES_EX
#define SRB_EXTENSION(Srb) SrbGetMiniportContext(Srb)
#define SRB_FUNCTION(Srb) SrbGetSrbFunction(Srb)
#define SRB_CDB(Srb) SrbGetCdb(Srb)
#define SRB_CDB_LENGTH(Srb) SrbGetCdbLength32(Srb)
#define SRB_FLAGS(Srb) SrbGetSrbFlags(Srb)
#define SRB_PATH_ID(Srb) SrbGetPathId(Srb)
#define SRB_TARGET_ID(Srb) SrbGetTargetId(Srb)
#define SRB_LUN(Srb) SrbGetLun(Srb)
#define SRB_DATA_BUFFER(Srb) SrbGetDataBuffer(Srb)
#define SRB_DATA_TRANSFER_LENGTH(Srb) SrbGetDataTransferLength(Srb)
#define SRB_LENGTH(Srb) SrbGetSrbLength(Srb)
#define SRB_WMI_DATA(Srb) (PSRBEX_DATA_WMI)SrbGetSrbExDataByType((PSTORAGE_REQUEST_BLOCK)Srb, SrbExDataTypeWmi)
#define SRB_GET_SENSE_INFO(Srb, senseInfoBuffer, senseInfoBufferLen) SrbGetScsiData(Srb, NULL, NULL, NULL, &senseInfoBuffer, &senseInfoBufferLen)
#define SRB_GET_PNP_INFO(Srb, PnPFlags, PnPAction) SrbGetPnpInfo(Srb, &PnPFlags, &PnPAction)
#define SRB_SET_SCSI_STATUS(Srb, status) SrbSetScsiData(Srb, NULL, NULL, &status, NULL, NULL)
#define SRB_GET_SCSI_STATUS(Srb, status) SrbGetScsiData(Srb, NULL, NULL, &status, NULL, NULL)
#define SRB_SET_SRB_STATUS(Srb, status) SrbSetSrbStatus(Srb, status)
#define SRB_GET_SRB_STATUS(Srb, status) status = SrbSetSrbStatus(Srb)
#define SRB_SET_DATA_TRANSFER_LENGTH(Srb, Len) SrbSetDataTransferLength(Srb, Len)

VOID
SendSRB(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    );

BOOLEAN
SendTMF(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

VOID
ShutDown(
    IN PVOID DeviceExtension
    );

BOOLEAN
DeviceReset(
    IN PVOID DeviceExtension
    );

VOID
GetScsiConfig(
    IN PVOID DeviceExtension
    );
VOID
SetGuestFeatures(
    IN PVOID DeviceExtension
    );

BOOLEAN
InitVirtIODevice(
    IN PVOID DeviceExtension
    );

BOOLEAN
InitHW(
    IN PVOID DeviceExtension,
    IN PPORT_CONFIGURATION_INFORMATION ConfigInfo
    );

VOID
LogError(
    IN PVOID HwDeviceExtension,
    IN ULONG ErrorCode,
    IN ULONG UniqueId
    );

BOOLEAN
KickEvent(
    IN PVOID DeviceExtension,
    IN PVirtIOSCSIEventNode event
    );

BOOLEAN
SynchronizedKickEventRoutine(
    IN PVOID DeviceExtension,
    IN PVOID Context
    );

VOID
VioScsiCompleteDpcRoutine(
    IN PSTOR_DPC  Dpc,
    IN PVOID Context,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
);

VOID
ProcessQueue(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN BOOLEAN isr
    );

VOID
//FORCEINLINE
VioScsiVQLock(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN OUT PSTOR_LOCK_HANDLE LockHandle,
    IN BOOLEAN isr
    );

VOID
//FORCEINLINE
VioScsiVQUnlock(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN PSTOR_LOCK_HANDLE LockHandle,
    IN BOOLEAN isr
    );

VOID
//FORCEINLINE
HandleResponse(
    IN PVOID DeviceExtension,
    IN PVirtIOSCSICmd cmd
    );

PVOID
VioScsiPoolAlloc(
    IN PVOID DeviceExtension,
    IN SIZE_T size
    );

VOID
CompleteRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
 );

#if (NTDDI_VERSION >= NTDDI_WINTHRESHOLD)
VOID FirmwareRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    );
#endif

extern VirtIOSystemOps VioScsiSystemOps;

#endif ___HELPER_H___
