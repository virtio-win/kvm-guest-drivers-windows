/*
 * This file contains various virtio queue related routines.
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
#include "trace.h"
#include "helper.h"

#if defined(EVENT_TRACING)
#include "helper.tmh"
#endif

#define SET_VA_PA() { ULONG len; va = adaptExt->indirect ? srbExt->pdesc : NULL; \
                      pa = va ? StorPortGetPhysicalAddress(DeviceExtension, NULL, va, &len).QuadPart : 0; \
                    }

VOID
SendSRB(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION      srbExt   = NULL;
    PVOID               va = NULL;
    ULONGLONG           pa = 0;
    ULONG               QueueNumber = VIRTIO_SCSI_REQUEST_QUEUE_0;
    STOR_LOCK_HANDLE    LockHandle = { 0 };
    ULONG               status = STOR_STATUS_SUCCESS;
    ULONG MessageID;
    int res = 0;
    PREQUEST_LIST       element;
    ULONG               index;
ENTER_FN_SRB();

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " SRB %p.\n", Srb);

    if (!Srb)
        return;

    if (adaptExt->num_queues > 1) {
        STARTIO_PERFORMANCE_PARAMETERS param;
        param.Size = sizeof(STARTIO_PERFORMANCE_PARAMETERS);
        status = StorPortGetStartIoPerfParams(DeviceExtension, (PSCSI_REQUEST_BLOCK)Srb, &param);
        if (status == STOR_STATUS_SUCCESS && param.MessageNumber != 0) {
            QueueNumber = MESSAGE_TO_QUEUE(param.MessageNumber);
        } else {
            RhelDbgPrint(TRACE_LEVEL_ERROR, " StorPortGetStartIoPerfParams failed srb %p status 0x%x MessageNumber %d.\n", Srb, status, param.MessageNumber);
        }
    }

    srbExt = SRB_EXTENSION(Srb);

    if (!srbExt) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " No SRB Ext after ExInterlockedRemoveHeadList QueueNumber (%d) \n", QueueNumber);
        return;
    }

    MessageID = QUEUE_TO_MESSAGE(QueueNumber);
    index = QueueNumber - VIRTIO_SCSI_REQUEST_QUEUE_0;

    if (adaptExt->reset_in_progress) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, "WHAT ATE YOU DOING SPB = %p ??\n", Srb);
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_BUS_RESET);
        CompleteRequest(DeviceExtension, Srb);
        return;
    }

    VioScsiVQLock(DeviceExtension, MessageID, &LockHandle, FALSE);
    SET_VA_PA();
    res = virtqueue_add_buf(adaptExt->vq[QueueNumber],
        srbExt->psgl,
        srbExt->out, srbExt->in,
        &srbExt->cmd, va, pa);

    if (res >= 0) {
        element = &adaptExt->processing_srbs[index];
        InsertTailList(&element->srb_list, &srbExt->list_entry);
        element->srb_cnt++;
    }
    VioScsiVQUnlock(DeviceExtension, MessageID, &LockHandle, FALSE);
    if ( res >= 0){
        if (virtqueue_kick_prepare(adaptExt->vq[QueueNumber])) {
            virtqueue_notify(adaptExt->vq[QueueNumber]);
        }
    } else {
        virtqueue_notify(adaptExt->vq[QueueNumber]);
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_BUSY);
        StorPortBusy(DeviceExtension, 10);
        CompleteRequest(DeviceExtension, Srb);
        RhelDbgPrint(TRACE_LEVEL_FATAL, " CompleteRequest queue (%d) SRB = %p Lun = %d TimeOut = %d.\n", QueueNumber, srbExt->Srb, SRB_LUN(Srb), Srb->TimeOutValue);
    }

EXIT_FN_SRB();
}

BOOLEAN
SynchronizedTMFRoutine(
    IN PVOID DeviceExtension,
    IN PVOID Context
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSCSI_REQUEST_BLOCK Srb      = (PSCSI_REQUEST_BLOCK) Context;
    PSRB_EXTENSION      srbExt   = SRB_EXTENSION(Srb);
    PVOID               va;
    ULONGLONG           pa;

ENTER_FN();
    SET_VA_PA();
    if (virtqueue_add_buf(adaptExt->vq[VIRTIO_SCSI_CONTROL_QUEUE],
                     srbExt->psgl,
                     srbExt->out, srbExt->in,
                     &srbExt->cmd, va, pa) >= 0){
        virtqueue_kick(adaptExt->vq[VIRTIO_SCSI_CONTROL_QUEUE]);
EXIT_FN();
        return TRUE;
    }
    SRB_SET_SRB_STATUS(Srb, SRB_STATUS_BUSY);
    StorPortBusy(DeviceExtension, adaptExt->queue_depth);
EXIT_ERR();
    return FALSE;
}

BOOLEAN
SendTMF(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
ENTER_FN();
    return StorPortSynchronizeAccess(DeviceExtension, SynchronizedTMFRoutine, (PVOID)Srb);
EXIT_FN();
}

BOOLEAN
DeviceReset(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSCSI_REQUEST_BLOCK   Srb = &adaptExt->tmf_cmd.Srb;
    PSRB_EXTENSION        srbExt = adaptExt->tmf_cmd.SrbExtension;
    VirtIOSCSICmd         *cmd = &srbExt->cmd;
    ULONG                 fragLen;
    ULONG                 sgElement;

ENTER_FN();
    if (adaptExt->dump_mode) {
        return TRUE;
    }
    ASSERT(adaptExt->tmf_infly == FALSE);
    Srb->SrbExtension = srbExt;
    RtlZeroMemory((PVOID)cmd, sizeof(VirtIOSCSICmd));
    cmd->srb = (PVOID)Srb;
    cmd->req.tmf.lun[0] = 1;
    cmd->req.tmf.lun[1] = 0;
    cmd->req.tmf.lun[2] = 0;
    cmd->req.tmf.lun[3] = 0;
    cmd->req.tmf.type = VIRTIO_SCSI_T_TMF;
    cmd->req.tmf.subtype = VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET;

    srbExt->psgl = srbExt->vio_sg;
    srbExt->pdesc = srbExt->desc_alias;
    sgElement = 0;
    srbExt->psgl[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->req.tmf, &fragLen);
    srbExt->psgl[sgElement].length   = sizeof(cmd->req.tmf);
    sgElement++;
    srbExt->out = sgElement;
    srbExt->psgl[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->resp.tmf, &fragLen);
    srbExt->psgl[sgElement].length = sizeof(cmd->resp.tmf);
    sgElement++;
    srbExt->in = sgElement - srbExt->out;
    StorPortPause(DeviceExtension, 60);
    if (!SendTMF(DeviceExtension, Srb)) {
        StorPortResume(DeviceExtension);
        return FALSE;
    }
    adaptExt->tmf_infly = TRUE;
    return TRUE;
}

VOID
ShutDown(
    IN PVOID DeviceExtension
    )
{
    ULONG index;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
ENTER_FN();
    virtio_device_reset(&adaptExt->vdev);
    virtio_delete_queues(&adaptExt->vdev);
    for (index = VIRTIO_SCSI_CONTROL_QUEUE; index < adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0; ++index) {
        adaptExt->vq[index] = NULL;
    }

    virtio_device_shutdown(&adaptExt->vdev);
EXIT_FN();
}

VOID
GetScsiConfig(
    IN PVOID DeviceExtension
)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
ENTER_FN();

    adaptExt->features = virtio_get_features(&adaptExt->vdev);
    adaptExt->indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, seg_max),
                      &adaptExt->scsi_config.seg_max, sizeof(adaptExt->scsi_config.seg_max));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " seg_max %lu\n", adaptExt->scsi_config.seg_max);

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, num_queues),
                      &adaptExt->scsi_config.num_queues, sizeof(adaptExt->scsi_config.num_queues));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " num_queues %lu\n", adaptExt->scsi_config.num_queues);

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, max_sectors),
                      &adaptExt->scsi_config.max_sectors, sizeof(adaptExt->scsi_config.max_sectors));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " max_sectors %lu\n", adaptExt->scsi_config.max_sectors);

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, cmd_per_lun),
                      &adaptExt->scsi_config.cmd_per_lun, sizeof(adaptExt->scsi_config.cmd_per_lun));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " cmd_per_lun %lu\n", adaptExt->scsi_config.cmd_per_lun);

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, event_info_size),
                      &adaptExt->scsi_config.event_info_size, sizeof(adaptExt->scsi_config.event_info_size));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " event_info_size %lu\n", adaptExt->scsi_config.event_info_size);

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, sense_size),
                      &adaptExt->scsi_config.sense_size, sizeof(adaptExt->scsi_config.sense_size));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " sense_size %lu\n", adaptExt->scsi_config.sense_size);

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, cdb_size),
                      &adaptExt->scsi_config.cdb_size, sizeof(adaptExt->scsi_config.cdb_size));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " cdb_size %lu\n", adaptExt->scsi_config.cdb_size);

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, max_channel),
                      &adaptExt->scsi_config.max_channel, sizeof(adaptExt->scsi_config.max_channel));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " max_channel %u\n", adaptExt->scsi_config.max_channel);

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, max_target),
                      &adaptExt->scsi_config.max_target, sizeof(adaptExt->scsi_config.max_target));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " max_target %u\n", adaptExt->scsi_config.max_target);

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, max_lun),
                      &adaptExt->scsi_config.max_lun, sizeof(adaptExt->scsi_config.max_lun));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " max_lun %lu\n", adaptExt->scsi_config.max_lun);

EXIT_FN();
}


VOID
SetGuestFeatures(
    IN PVOID DeviceExtension
)
{
    ULONGLONG          guestFeatures = 0;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
ENTER_FN();

    if (CHECKBIT(adaptExt->features, VIRTIO_F_VERSION_1)) {
        guestFeatures |= (1ULL << VIRTIO_F_VERSION_1);
        if (CHECKBIT(adaptExt->features, VIRTIO_F_RING_PACKED)) {
            guestFeatures |= (1ULL << VIRTIO_F_RING_PACKED);
        }
    }
    if (CHECKBIT(adaptExt->features, VIRTIO_F_ANY_LAYOUT)) {
        guestFeatures |= (1ULL << VIRTIO_F_ANY_LAYOUT);
    }
#if (NTDDI_VERSION >=NTDDI_WINTHRESHOLD)
    if (CHECKBIT(adaptExt->features, VIRTIO_F_ACCESS_PLATFORM)) {
        guestFeatures |= (1ULL << VIRTIO_F_ACCESS_PLATFORM);
    }
#endif
    if (CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX)) {
        guestFeatures |= (1ULL << VIRTIO_RING_F_EVENT_IDX);
    }
    if (CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC)) {
        guestFeatures |= (1ULL << VIRTIO_RING_F_INDIRECT_DESC);
    }
    if (CHECKBIT(adaptExt->features, VIRTIO_SCSI_F_CHANGE)) {
        guestFeatures |= (1ULL << VIRTIO_SCSI_F_CHANGE);
    }
    if (CHECKBIT(adaptExt->features, VIRTIO_SCSI_F_HOTPLUG)) {
        guestFeatures |= (1ULL << VIRTIO_SCSI_F_HOTPLUG);
    }
    if (!NT_SUCCESS(virtio_set_features(&adaptExt->vdev, guestFeatures))) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, " virtio_set_features failed\n");
    }

EXIT_FN();
}


BOOLEAN
InitVirtIODevice(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    NTSTATUS status;

    status = virtio_device_initialize(
        &adaptExt->vdev,
        &VioScsiSystemOps,
        adaptExt,
        adaptExt->msix_enabled);
    if (!NT_SUCCESS(status)) {
        LogError(adaptExt,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);
        RhelDbgPrint(TRACE_LEVEL_FATAL, " Failed to initialize virtio device, error %x\n", status);
        return FALSE;
    }
    return TRUE;
}

BOOLEAN
InitHW(
    IN PVOID DeviceExtension,
    IN PPORT_CONFIGURATION_INFORMATION ConfigInfo
    )
{
    PACCESS_RANGE      accessRange;
    PADAPTER_EXTENSION adaptExt;
    ULONG pci_cfg_len, i;

ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    adaptExt->system_io_bus_number = ConfigInfo->SystemIoBusNumber;
    adaptExt->slot_number = ConfigInfo->SlotNumber;

    /* read PCI config space */
    pci_cfg_len = StorPortGetBusData(
        DeviceExtension,
        PCIConfiguration,
        ConfigInfo->SystemIoBusNumber,
        (ULONG)ConfigInfo->SlotNumber,
        (PVOID)&adaptExt->pci_config_buf,
        sizeof(adaptExt->pci_config_buf));

    if (pci_cfg_len != sizeof(adaptExt->pci_config_buf)) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);
        RhelDbgPrint(TRACE_LEVEL_FATAL, " CANNOT READ PCI CONFIGURATION SPACE %d\n", pci_cfg_len);
        return FALSE;
    }

    {
        UCHAR CapOffset;
        PPCI_MSIX_CAPABILITY pMsixCapOffset;
        PPCI_COMMON_HEADER   pPciComHeader;
        pPciComHeader = &adaptExt->pci_config;
        if ((pPciComHeader->Status & PCI_STATUS_CAPABILITIES_LIST) == 0)
        {
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " NO CAPABILITIES_LIST\n");
        }
        else
        {
            if ((pPciComHeader->HeaderType & (~PCI_MULTIFUNCTION)) == PCI_DEVICE_TYPE)
            {
                CapOffset = pPciComHeader->u.type0.CapabilitiesPtr;
                while (CapOffset != 0)
                {
                    pMsixCapOffset = (PPCI_MSIX_CAPABILITY)&adaptExt->pci_config_buf[CapOffset];
                    if (pMsixCapOffset->Header.CapabilityID == PCI_CAPABILITY_ID_MSIX)
                    {
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, "MessageControl.TableSize = %d\n", pMsixCapOffset->MessageControl.TableSize);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, "MessageControl.FunctionMask = %d\n", pMsixCapOffset->MessageControl.FunctionMask);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, "MessageControl.MSIXEnable = %d\n", pMsixCapOffset->MessageControl.MSIXEnable);

                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageTable = %lu\n", pMsixCapOffset->MessageTable.TableOffset);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " PBATable = %lu\n", pMsixCapOffset->PBATable.TableOffset);
                        adaptExt->msix_enabled = (pMsixCapOffset->MessageControl.MSIXEnable == 1);
                    } else
                    {
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " CapabilityID = %x, Next CapOffset = %x\n", pMsixCapOffset->Header.CapabilityID, CapOffset);
                    }
                    CapOffset = pMsixCapOffset->Header.Next;
                }
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, " msix_enabled = %d\n", adaptExt->msix_enabled);
            } else
            {
                RhelDbgPrint(TRACE_LEVEL_FATAL, " NOT A PCI_DEVICE_TYPE\n");
            }
        }
    }

    /* initialize the pci_bars array */
    for (i = 0; i < ConfigInfo->NumberOfAccessRanges; i++) {
        accessRange = *ConfigInfo->AccessRanges + i;
        if (accessRange->RangeLength != 0) {
            int iBar = virtio_get_bar_index(&adaptExt->pci_config, accessRange->RangeStart);
            if (iBar == -1) {
                RhelDbgPrint(TRACE_LEVEL_FATAL,
                             " Cannot get index for BAR %I64d\n", accessRange->RangeStart.QuadPart);
                return FALSE;
            }
            adaptExt->pci_bars[iBar].BasePA = accessRange->RangeStart;
            adaptExt->pci_bars[iBar].uLength = accessRange->RangeLength;
            adaptExt->pci_bars[iBar].bPortSpace = !accessRange->RangeInMemory;
        }
    }

    /* initialize the virtual device */
    if (!InitVirtIODevice(DeviceExtension)) {
        return FALSE;
    }

EXIT_FN();
    return TRUE;
}

BOOLEAN
SynchronizedKickEventRoutine(
    IN PVOID DeviceExtension,
    IN PVOID Context
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PVirtIOSCSIEventNode eventNode   = (PVirtIOSCSIEventNode) Context;
    PVOID               va = NULL;
    ULONGLONG           pa = 0;

ENTER_FN();
    if (virtqueue_add_buf(adaptExt->vq[VIRTIO_SCSI_EVENTS_QUEUE],
                     &eventNode->sg,
                     0, 1,
                     eventNode, va, pa) >= 0){
        virtqueue_kick(adaptExt->vq[VIRTIO_SCSI_EVENTS_QUEUE]);
EXIT_FN();
        return TRUE;
    }
EXIT_ERR();
    return FALSE;
}


BOOLEAN
KickEvent(
    IN PVOID DeviceExtension,
    IN PVirtIOSCSIEventNode EventNode
    )
{
    PADAPTER_EXTENSION adaptExt;
    ULONG              fragLen;

ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    RtlZeroMemory((PVOID)EventNode, sizeof(VirtIOSCSIEventNode));
    EventNode->sg.physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &EventNode->event, &fragLen);
    EventNode->sg.length   = sizeof(VirtIOSCSIEvent);
    return SynchronizedKickEventRoutine(DeviceExtension, (PVOID)EventNode);
EXIT_FN();
}

VOID
//FORCEINLINE
VioScsiVQLock(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN OUT PSTOR_LOCK_HANDLE LockHandle,
    IN BOOLEAN isr
    )
{
    PADAPTER_EXTENSION  adaptExt;
ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (!isr) {
        if (adaptExt->msix_enabled) {
            // Queue numbers start at 0, message ids at 1.
            NT_ASSERT(MessageID > VIRTIO_SCSI_REQUEST_QUEUE_0);
            NT_ASSERT(MessageID <= VIRTIO_SCSI_REQUEST_QUEUE_0 + adaptExt->num_queues);
            StorPortAcquireSpinLock(DeviceExtension, DpcLock, &adaptExt->dpc[MESSAGE_TO_QUEUE(MessageID) - VIRTIO_SCSI_REQUEST_QUEUE_0], LockHandle);
        }
        else {
            StorPortAcquireSpinLock(DeviceExtension, InterruptLock, NULL, LockHandle);
        }
    }
EXIT_FN();
}

VOID
//FORCEINLINE
VioScsiVQUnlock(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN PSTOR_LOCK_HANDLE LockHandle,
    IN BOOLEAN isr
    )
{
ENTER_FN();
    if (!isr) {
        StorPortReleaseSpinLock(DeviceExtension, LockHandle);
    }
EXIT_FN();
}

#if (NTDDI_VERSION >= NTDDI_WINTHRESHOLD)
VOID FirmwareRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    PADAPTER_EXTENSION  adaptExt;
    PSRB_EXTENSION      srbExt   = NULL;
    ULONG                   dataLen = 0;
    PSRB_IO_CONTROL         srbControl = NULL;
    PFIRMWARE_REQUEST_BLOCK firmwareRequest = NULL;
ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    srbExt = SRB_EXTENSION(Srb);
    srbControl = (PSRB_IO_CONTROL)SRB_DATA_BUFFER(Srb);
    dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);
    if (dataLen < (sizeof(SRB_IO_CONTROL) + sizeof(FIRMWARE_REQUEST_BLOCK))) {
        srbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        RhelDbgPrint(TRACE_LEVEL_ERROR,
                         " FirmwareRequest Bad Block Length  %ul\n", dataLen);
        return;
    }

    firmwareRequest = (PFIRMWARE_REQUEST_BLOCK)(srbControl + 1);
    switch (firmwareRequest->Function) {

    case FIRMWARE_FUNCTION_GET_INFO: {
        PSTORAGE_FIRMWARE_INFO_V2   firmwareInfo;
        firmwareInfo = (PSTORAGE_FIRMWARE_INFO_V2)((PUCHAR)srbControl + firmwareRequest->DataBufferOffset);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                         " FIRMWARE_FUNCTION_GET_INFO \n");
        if ((firmwareInfo->Version >= STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2) ||
            (firmwareInfo->Size >= sizeof(STORAGE_FIRMWARE_INFO_V2))) {
            firmwareInfo->Version = STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2;
            firmwareInfo->Size = sizeof(STORAGE_FIRMWARE_INFO_V2);

            firmwareInfo->UpgradeSupport = TRUE;

            firmwareInfo->SlotCount = 1;
            firmwareInfo->ActiveSlot = 0;
            firmwareInfo->PendingActivateSlot = STORAGE_FIRMWARE_INFO_INVALID_SLOT;
            firmwareInfo->FirmwareShared = FALSE;
            firmwareInfo->ImagePayloadAlignment = PAGE_SIZE;
            firmwareInfo->ImagePayloadMaxSize = PAGE_SIZE;

            if (firmwareRequest->DataBufferLength >= (sizeof(STORAGE_FIRMWARE_INFO_V2) + sizeof(STORAGE_FIRMWARE_SLOT_INFO_V2))) {
                firmwareInfo->Slot[0].SlotNumber = 0;
                firmwareInfo->Slot[0].ReadOnly = FALSE;
                StorPortCopyMemory(&firmwareInfo->Slot[0].Revision, &adaptExt->fw_ver, sizeof (adaptExt->fw_ver));
                srbControl->ReturnCode = FIRMWARE_STATUS_SUCCESS;
            } else {
                firmwareRequest->DataBufferLength = sizeof(STORAGE_FIRMWARE_INFO_V2) + sizeof(STORAGE_FIRMWARE_SLOT_INFO_V2);
                srbControl->ReturnCode = FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL;
            }
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
        }
        else {
            RhelDbgPrint(TRACE_LEVEL_ERROR,
                         " Wrong Version %ul or Size %ul\n", firmwareInfo->Version, firmwareInfo->Size);
            srbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        }
    }
    break;
    case FIRMWARE_FUNCTION_DOWNLOAD: {
        PSTORAGE_FIRMWARE_DOWNLOAD_V2   firmwareDwnld;
        firmwareDwnld = (PSTORAGE_FIRMWARE_DOWNLOAD_V2)((PUCHAR)srbControl + firmwareRequest->DataBufferOffset);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
            " FIRMWARE_FUNCTION_DOWNLOAD \n");
        if ((firmwareDwnld->Version >= STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2) ||
            (firmwareDwnld->Size >= sizeof(STORAGE_FIRMWARE_DOWNLOAD_V2))) {
            firmwareDwnld->Version = STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2;
            firmwareDwnld->Size = sizeof(STORAGE_FIRMWARE_DOWNLOAD_V2);
            adaptExt->fw_ver++;
            srbControl->ReturnCode = FIRMWARE_STATUS_SUCCESS;
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
        }
        else {
            RhelDbgPrint(TRACE_LEVEL_ERROR,
                " Wrong Version %ul or Size %ul\n", firmwareDwnld->Version, firmwareDwnld->Size);
            srbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        }
    }
    break;
    case FIRMWARE_FUNCTION_ACTIVATE: {
        PSTORAGE_FIRMWARE_ACTIVATE firmwareActivate;
        firmwareActivate = (PSTORAGE_FIRMWARE_ACTIVATE)((PUCHAR)srbControl + firmwareRequest->DataBufferOffset);
        if ((firmwareActivate->Version == STORAGE_FIRMWARE_ACTIVATE_STRUCTURE_VERSION) ||
            (firmwareActivate->Size >= sizeof(STORAGE_FIRMWARE_ACTIVATE))) {
            firmwareActivate->Version = STORAGE_FIRMWARE_ACTIVATE_STRUCTURE_VERSION;
            firmwareActivate->Size = sizeof(STORAGE_FIRMWARE_ACTIVATE);
            srbControl->ReturnCode = FIRMWARE_STATUS_SUCCESS;
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
        }
        else {
            RhelDbgPrint(TRACE_LEVEL_ERROR,
                " Wrong Version %ul or Size %ul\n", firmwareActivate->Version, firmwareActivate->Size);
            srbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        }
        RhelDbgPrint(TRACE_LEVEL_VERBOSE,
            " FIRMWARE_FUNCTION_ACTIVATE \n");
    }
    break;
    default:
        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                     " Unsupported Function %ul\n", firmwareRequest->Function);
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_INVALID_REQUEST);
        break;
    }
EXIT_FN();
}

#endif
