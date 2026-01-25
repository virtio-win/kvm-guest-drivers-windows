/*
 * Virtio block device include module.
 *
 * Copyright (c) 2008-2017 Red Hat, Inc.
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
#ifndef ___VIOSTOR_HW_HELPER_H___
#define ___VIOSTOR_HW_HELPER_H___

#include <ntddk.h>

#include <storport.h>

#include "osdep.h"
#include "virtio_pci.h"
#include "virtio.h"

#include <srbhelper.h>

typedef enum _PROCESS_BUFFER_LOCKING_MODE
{
    PROCESS_BUFFER_NO_SPINLOCKS = 0,
    PROCESS_BUFFER_WITH_SPINLOCKS
} PROCESS_BUFFER_LOCKING_MODE;

typedef enum _SEND_SRB_FUNCTION
{
    SEND_SRB_FLUSH = 0,
    SEND_SRB_READ_WRITE,
    SEND_SRB_UNMAP,
    SEND_SRB_GET_SERIAL_NUMBER
} SEND_SRB_FUNCTION;

typedef enum _SEND_SRB_RESEND_MODE
{
    SEND_SRB_NO_EXISTING_SPINLOCK = 0,
    SEND_SRB_ALREADY_UNDER_SPINLOCK
} SEND_SRB_RESEND_MODE;

typedef enum _SEND_SRB_COMPLETE_MODE
{
    SEND_SRB_COMPLETE_NORMAL = 0,
    SEND_SRB_COMPLETE_IN_STARTIO
} SEND_SRB_COMPLETE_MODE;

FORCEINLINE ULONG SrbGetCdbLenght(_In_ PVOID Srb)
{
    ULONG CdbLen32 = 0;
    UCHAR CdbLen8 = 0;
    SrbGetScsiData(Srb, &CdbLen8, &CdbLen32, NULL, NULL, NULL);
    return (CdbLen8 != 0) ? CdbLen8 : CdbLen32;
}

FORCEINLINE VOID SrbGetPnpInfo(_In_ PVOID Srb, ULONG *PnPFlags, ULONG *PnPAction)
{
    PSCSI_PNP_REQUEST_BLOCK pPnpSrb = NULL;
    PSRBEX_DATA_PNP pSrbExPnp = NULL;
    pSrbExPnp = (PSRBEX_DATA_PNP)SrbGetSrbExDataByType((PSTORAGE_REQUEST_BLOCK)Srb, SrbExDataTypePnP);
    if (pSrbExPnp != NULL)
    {
        *PnPFlags = pSrbExPnp->SrbPnPFlags;
        *PnPAction = pSrbExPnp->PnPAction;
    }
    else
    {
        pPnpSrb = (PSCSI_PNP_REQUEST_BLOCK)Srb;
        *PnPFlags = pPnpSrb->SrbPnPFlags;
        *PnPAction = pPnpSrb->PnPAction;
    }
}

#define PSRB_TYPE                      PSTORAGE_REQUEST_BLOCK
#define PSRB_WMI_DATA                  PSRBEX_DATA_WMI
#define PSTOR_DEVICE_CAPABILITIES_TYPE PSTOR_DEVICE_CAPABILITIES_EX
#define SRB_EXTENSION(Srb)             SrbGetMiniportContext(Srb)
#define SRB_FUNCTION(Srb)              SrbGetSrbFunction(Srb)
#define SRB_CDB(Srb)                   SrbGetCdb(Srb)
#define SRB_CDB_LENGTH(Srb)            SrbGetCdbLenght(Srb)
#define SRB_FLAGS(Srb)                 SrbGetSrbFlags(Srb)
#define SRB_PATH_ID(Srb)               SrbGetPathId(Srb)
#define SRB_TARGET_ID(Srb)             SrbGetTargetId(Srb)
#define SRB_LUN(Srb)                   SrbGetLun(Srb)
#define SRB_DATA_BUFFER(Srb)           SrbGetDataBuffer(Srb)
#define SRB_DATA_TRANSFER_LENGTH(Srb)  SrbGetDataTransferLength(Srb)
#define SRB_LENGTH(Srb)                SrbGetSrbLength(Srb)
#define SRB_WMI_DATA(Srb)              (PSRBEX_DATA_WMI) SrbGetSrbExDataByType((PSTORAGE_REQUEST_BLOCK)Srb, SrbExDataTypeWmi)
#define SRB_PNP_DATA(Srb)              (PSRBEX_DATA_PNP) SrbGetSrbExDataByType((PSTORAGE_REQUEST_BLOCK)Srb, SrbExDataTypePnP)
#define SRB_GET_SENSE_INFO(Srb, senseInfoBuffer, senseInfoBufferLen)                                                   \
    SrbGetScsiData(Srb, NULL, NULL, NULL, &senseInfoBuffer, &senseInfoBufferLen)
#define SRB_GET_SENSE_INFO_BUFFER(Srb, senseInfoBuffer) senseInfoBuffer = SrbGetSenseInfoBuffer(Srb)
#define SRB_GET_SENSE_INFO_BUFFER_LENGTH(Srb, senseInfoBufferLength)                                                   \
    senseInfoBufferLength = SrbGetSenseInfoBufferLength(Srb)
#define SRB_GET_PNP_INFO(Srb, PnPFlags, PnPAction) SrbGetPnpInfo(Srb, &PnPFlags, &PnPAction)
#define SRB_SET_SCSI_STATUS(Srb, status)           SrbSetScsiData(Srb, NULL, NULL, &status, NULL, NULL)
#define SRB_GET_SRB_STATUS(Srb)                    SrbGetSrbStatus(Srb)
#define SRB_SET_SRB_STATUS(Srb, status)            SrbSetSrbStatus(Srb, status)
#define SRB_SET_DATA_TRANSFER_LENGTH(Srb, Len)     SrbSetDataTransferLength(Srb, Len)

BOOLEAN
SendSRB(IN PVOID DeviceExtension, IN PSRB_TYPE Srb, IN SEND_SRB_FUNCTION srbFunction, IN SEND_SRB_RESEND_MODE resend);

VOID RhelShutDown(IN PVOID DeviceExtension);

ULONGLONG
RhelGetLba(IN PVOID DeviceExtension, IN PCDB Cdb);

ULONG
RhelGetSectors(IN PVOID DeviceExtension, IN PCDB Cdb);

VOID RhelGetDiskGeometry(IN PVOID DeviceExtension);

VOID ProcessBuffer(IN PVOID DeviceExtension, IN ULONG MessageId, IN PROCESS_BUFFER_LOCKING_MODE LockMode);

PVOID
VioStorPoolAlloc(IN PVOID DeviceExtension, IN SIZE_T size);

VOID VioStorVQLock(IN PVOID DeviceExtension, IN ULONG MessageId, IN OUT PSTOR_LOCK_HANDLE LockHandle);

VOID VioStorVQUnlock(IN PVOID DeviceExtension, IN ULONG MessageId, IN PSTOR_LOCK_HANDLE LockHandle);

VOID CompleteRequestWithStatus(IN PVOID DeviceExtension, IN PSRB_TYPE Srb, IN UCHAR status);

extern VirtIOSystemOps VioStorSystemOps;

extern int vring_add_buf_stor(IN struct virtqueue *_vq,
                              IN struct VirtIOBufferDescriptor sg[],
                              IN unsigned int out,
                              IN unsigned int in,
                              IN PVOID data);

#endif //___VIOSTOR_HW_HELPER_H___
