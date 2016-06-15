/**********************************************************************
 * Copyright (c) 2012-2016 Red Hat, Inc.
 *
 * File: helper.c
 *
 * Author(s):
 * Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This file contains various virtio queue related routines.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "helper.h"
#include "utils.h"

#if (INDIRECT_SUPPORTED == 1)
#define SET_VA_PA() { ULONG len; va = adaptExt->indirect ? srbExt->desc : NULL; \
                      pa = va ? StorPortGetPhysicalAddress(DeviceExtension, NULL, va, &len).QuadPart : 0; \
                    }
#else
#define SET_VA_PA()   va = NULL; pa = 0;
#endif

BOOLEAN
SendSRB(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION      srbExt   = SRB_EXTENSION(Srb);
    PVOID               va = NULL;
    ULONGLONG           pa = 0;
    ULONG               QueueNumber = 0;
    ULONG               OldIrql = 0;
    ULONG               MessageId = 0;
    BOOLEAN             result = FALSE;
    bool                notify = FALSE;
    STOR_LOCK_HANDLE    LockHandle = { 0 };
    ULONG               status = STOR_STATUS_SUCCESS;
ENTER_FN();
    SET_VA_PA();

    if (adaptExt->num_queues > 1) {
        QueueNumber = adaptExt->cpu_to_vq_map[srbExt->cpu] + VIRTIO_SCSI_REQUEST_QUEUE_0;
    }
    else {
        QueueNumber = VIRTIO_SCSI_REQUEST_QUEUE_0;
    }
    MessageId = QueueNumber + 1;
    VioScsiVQLock(DeviceExtension, MessageId, &LockHandle, FALSE);
    if (virtqueue_add_buf(adaptExt->vq[QueueNumber],
                     &srbExt->sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->cmd, va, pa) >= 0){
        result = TRUE;
        notify = virtqueue_kick_prepare(adaptExt->vq[QueueNumber]);
    }
    else {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s Can not add packet to queue.\n", __FUNCTION__));
//FIXME
    }
    VioScsiVQUnlock(DeviceExtension, MessageId, &LockHandle, FALSE);
    if (notify) {
        virtqueue_notify(adaptExt->vq[QueueNumber]);
    }
#if (NTDDI_VERSION > NTDDI_WIN7)
    if (adaptExt->num_queues > 1) {
        if (CHECKFLAG(adaptExt->perfFlags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO)) {
            ULONG msg = MessageId - 3;
            PSTOR_SLIST_ENTRY   listEntryRev, listEntry;
            status = StorPortInterlockedFlushSList(DeviceExtension, &adaptExt->srb_list[msg], &listEntryRev);
            if ((status == STOR_STATUS_SUCCESS) && (listEntryRev != NULL)) {
                listEntry = listEntryRev;
                while(listEntry)
                {
                    PVirtIOSCSICmd  cmd = NULL;
                    PSTOR_SLIST_ENTRY next = listEntry->Next;
                    srbExt = CONTAINING_RECORD(listEntry,
                                SRB_EXTENSION, list_entry);

                    ASSERT(srExt);
                    cmd = (PVirtIOSCSICmd)srbExt->priv;
                    ASSERT(cmd);
                    HandleResponse(DeviceExtension, cmd);
                    listEntry = next;
                }
            }
        }
    }
#endif
EXIT_FN();
    return result;
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
                     &srbExt->sg[0],
                     srbExt->out, srbExt->in,
                     &srbExt->cmd, va, pa) >= 0){
        virtqueue_kick(adaptExt->vq[VIRTIO_SCSI_CONTROL_QUEUE]);
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

    sgElement = 0;
    srbExt->sg[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->req.tmf, &fragLen);
    srbExt->sg[sgElement].length   = sizeof(cmd->req.tmf);
    sgElement++;
    srbExt->out = sgElement;
    srbExt->sg[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->resp.tmf, &fragLen);
    srbExt->sg[sgElement].length = sizeof(cmd->resp.tmf);
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

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, seg_max),
                      &adaptExt->scsi_config.seg_max, sizeof(adaptExt->scsi_config.seg_max));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("seg_max %lu\n", adaptExt->scsi_config.seg_max));

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, num_queues),
                      &adaptExt->scsi_config.num_queues, sizeof(adaptExt->scsi_config.num_queues));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("num_queues %lu\n", adaptExt->scsi_config.num_queues));

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, max_sectors),
                      &adaptExt->scsi_config.max_sectors, sizeof(adaptExt->scsi_config.max_sectors));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("max_sectors %lu\n", adaptExt->scsi_config.max_sectors));

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, cmd_per_lun),
                      &adaptExt->scsi_config.cmd_per_lun, sizeof(adaptExt->scsi_config.cmd_per_lun));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("cmd_per_lun %lu\n", adaptExt->scsi_config.cmd_per_lun));

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, event_info_size),
                      &adaptExt->scsi_config.event_info_size, sizeof(adaptExt->scsi_config.event_info_size));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("seg_max %lu\n", adaptExt->scsi_config.seg_max));

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, sense_size),
                      &adaptExt->scsi_config.sense_size, sizeof(adaptExt->scsi_config.sense_size));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("event_info_size %lu\n", adaptExt->scsi_config.event_info_size));

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, cdb_size),
                      &adaptExt->scsi_config.cdb_size, sizeof(adaptExt->scsi_config.cdb_size));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("cdb_size %lu\n", adaptExt->scsi_config.cdb_size));

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, max_channel),
                      &adaptExt->scsi_config.max_channel, sizeof(adaptExt->scsi_config.max_channel));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("max_channel %u\n", adaptExt->scsi_config.max_channel));

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, max_target),
                      &adaptExt->scsi_config.max_target, sizeof(adaptExt->scsi_config.max_target));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("max_target %u\n", adaptExt->scsi_config.max_target));

    virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(VirtIOSCSIConfig, max_lun),
                      &adaptExt->scsi_config.max_lun, sizeof(adaptExt->scsi_config.max_lun));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("max_lun %lu\n", adaptExt->scsi_config.max_lun));

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
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize virtio device, error %x\n", status));
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
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("CANNOT READ PCI CONFIGURATION SPACE %d\n", pci_cfg_len));
        return FALSE;
    }

#if (MSI_SUPPORTED == 1)
    {
        UCHAR CapOffset;
        PPCI_MSIX_CAPABILITY pMsixCapOffset;
        PPCI_COMMON_HEADER   pPciComHeader;
        pPciComHeader = &adaptExt->pci_config;
        if ((pPciComHeader->Status & PCI_STATUS_CAPABILITIES_LIST) == 0)
        {
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("NO CAPABILITIES_LIST\n"));
        } else
        {
            if ((pPciComHeader->HeaderType & (~PCI_MULTIFUNCTION)) == PCI_DEVICE_TYPE)
            {
                CapOffset = pPciComHeader->u.type0.CapabilitiesPtr;
                while (CapOffset != 0)
                {
                    pMsixCapOffset = (PPCI_MSIX_CAPABILITY)&adaptExt->pci_config_buf[CapOffset];
                    if (pMsixCapOffset->Header.CapabilityID == PCI_CAPABILITY_ID_MSIX)
                    {
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageControl.TableSize = %d\n", pMsixCapOffset->MessageControl.TableSize));
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageControl.FunctionMask = %d\n", pMsixCapOffset->MessageControl.FunctionMask));
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageControl.MSIXEnable = %d\n", pMsixCapOffset->MessageControl.MSIXEnable));

                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageTable = %p\n", pMsixCapOffset->MessageTable));
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("PBATable = %d\n", pMsixCapOffset->PBATable));
                        adaptExt->msix_enabled = (pMsixCapOffset->MessageControl.MSIXEnable == 1);
                    } else
                    {
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("CapabilityID = %x, Next CapOffset = %x\n", pMsixCapOffset->Header.CapabilityID, CapOffset));
                    }
                    CapOffset = pMsixCapOffset->Header.Next;
                }
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("msix_enabled = %d\n", adaptExt->msix_enabled));
            } else
            {
                RhelDbgPrint(TRACE_LEVEL_FATAL, ("NOT A PCI_DEVICE_TYPE\n"));
            }
        }
    }
#endif

    /* initialize the pci_bars array */
    for (i = 0; i < ConfigInfo->NumberOfAccessRanges; i++) {
        accessRange = *ConfigInfo->AccessRanges + i;
        if (accessRange->RangeLength != 0) {
            int iBar = virtio_get_bar_index(&adaptExt->pci_config, accessRange->RangeStart);
            if (iBar == -1) {
                RhelDbgPrint(TRACE_LEVEL_FATAL,
                             ("Cannot get index for BAR %I64d\n", accessRange->RangeStart.QuadPart));
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

    if (!adaptExt->msix_enabled) {
        if (!isr) {
            StorPortAcquireSpinLock(DeviceExtension, InterruptLock, NULL, LockHandle);
        }
    }
    else {
        if (adaptExt->num_queues == 1) {
            if (!isr) {
                ULONG oldIrql = 0;
                StorPortAcquireMSISpinLock(DeviceExtension, MessageID, &oldIrql);
                LockHandle->Context.OldIrql = (KIRQL)oldIrql;
            }
        }
        else {
            NT_ASSERT(MessageID > VIRTIO_SCSI_REQUEST_QUEUE_0);
            NT_ASSERT(MessageID <= VIRTIO_SCSI_REQUEST_QUEUE_0 + adaptExt->num_queues);
            if (CHECKFLAG(adaptExt->perfFlags, STOR_PERF_CONCURRENT_CHANNELS)) {
                if (CHECKFLAG(adaptExt->perfFlags, STOR_PERF_ADV_CONFIG_LOCALITY)) {
                    StorPortAcquireSpinLock(DeviceExtension, StartIoLock, &adaptExt->dpc[MessageID - VIRTIO_SCSI_REQUEST_QUEUE_0 - 1], LockHandle);
                }
                else {
                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s STOR_PERF_CONCURRENT_CHANNELS yes, STOR_PERF_ADV_CONFIG_LOCALITY no\n", __FUNCTION__));
                }
            }
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
    PADAPTER_EXTENSION  adaptExt;
ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (!adaptExt->msix_enabled) {
        if (!isr) {
            StorPortReleaseSpinLock(DeviceExtension, LockHandle);
        }
    }
    else {
        if (adaptExt->num_queues == 1) {
            if (!isr) {
                StorPortReleaseMSISpinLock(DeviceExtension, MessageID, LockHandle->Context.OldIrql);
            }
        }
        else {
            NT_ASSERT(MessageID > VIRTIO_SCSI_REQUEST_QUEUE_0);
            NT_ASSERT(MessageID <= VIRTIO_SCSI_REQUEST_QUEUE_0 + adaptExt->num_queues);
            if (CHECKFLAG(adaptExt->perfFlags, STOR_PERF_CONCURRENT_CHANNELS)) {
                if (CHECKFLAG(adaptExt->perfFlags, STOR_PERF_ADV_CONFIG_LOCALITY)) {
                    StorPortReleaseSpinLock(DeviceExtension, LockHandle);
                }
                else {
                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s STOR_PERF_CONCURRENT_CHANNELS yes, STOR_PERF_ADV_CONFIG_LOCALITY no\n", __FUNCTION__));
                }
            }
        }
    }

EXIT_FN();
}
