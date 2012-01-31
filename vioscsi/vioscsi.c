/**********************************************************************
 * Copyright (c) 2012  Red Hat, Inc.
 *
 * File: vioscsi.c
 *
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This file contains vioscsi StorPort miniport driver
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "vioscsi.h"
#include "utils.h"
#include "helper.h"

ULONG   RhelDbgLevel = TRACE_LEVEL_ERROR;
BOOLEAN IsCrashDumpMode;

BOOLEAN
VioScsiHwInitialize(
    IN PVOID DeviceExtension
    );

BOOLEAN
VioScsiBuildIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

BOOLEAN
VioScsiStartIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

ULONG
VioScsiFindAdapter(
    IN PVOID DeviceExtension,
    IN PVOID HwContext,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again
    );

BOOLEAN
VioScsiResetBus(
    IN PVOID DeviceExtension,
    IN ULONG PathId
    );

SCSI_ADAPTER_CONTROL_STATUS
VioScsiAdapterControl(
    IN PVOID DeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters
    );

VOID
FORCEINLINE
CompleteSRB(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

ULONG
DriverEntry(
    IN PVOID  DriverObject,
    IN PVOID  RegistryPath
    )
{

    HW_INITIALIZATION_DATA hwInitData;
    ULONG                  initResult;

    RhelDbgPrint(TRACE_LEVEL_ERROR, ("Vioscsi driver started...built on %s %s\n", __DATE__, __TIME__));
    IsCrashDumpMode = FALSE;
    if (RegistryPath == NULL) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                     ("DriverEntry: Crash dump mode\n"));
        IsCrashDumpMode = TRUE;
    }

    memset(&hwInitData, 0, sizeof(HW_INITIALIZATION_DATA));

    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

    hwInitData.HwFindAdapter            = VioScsiFindAdapter;
    hwInitData.HwInitialize             = VioScsiHwInitialize;
    hwInitData.HwStartIo                = VioScsiStartIo;
    hwInitData.HwInterrupt              = VioScsiInterrupt;
    hwInitData.HwResetBus               = VioScsiResetBus;
    hwInitData.HwAdapterControl         = VioScsiAdapterControl;
    hwInitData.HwBuildIo                = VioScsiBuildIo;
    hwInitData.NeedPhysicalAddresses    = TRUE;
    hwInitData.TaggedQueuing            = TRUE;
    hwInitData.AutoRequestSense         = TRUE;
    hwInitData.MultipleRequestPerLu     = TRUE;

    hwInitData.DeviceExtensionSize      = sizeof(ADAPTER_EXTENSION);
    hwInitData.SrbExtensionSize         = sizeof(SRB_EXTENSION);

    hwInitData.AdapterInterfaceType     = PCIBus;

    hwInitData.NumberOfAccessRanges     = 1;
    hwInitData.MapBuffers               = STOR_MAP_NON_READ_WRITE_BUFFERS;

    initResult = StorPortInitialize(DriverObject,
                                    RegistryPath,
                                    &hwInitData,
                                    NULL);

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 ("Initialize returned 0x%x\n", initResult));

    return initResult;

}

ULONG
VioScsiFindAdapter(
    IN PVOID DeviceExtension,
    IN PVOID HwContext,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again
    )
{

    PACCESS_RANGE      accessRange;
    PADAPTER_EXTENSION adaptExt;
    ULONG_PTR          ptr;
    ULONG              vr_sz;
    ULONG              vq_sz;
    USHORT             pageNum;

    UNREFERENCED_PARAMETER( HwContext );
    UNREFERENCED_PARAMETER( BusInformation );
    UNREFERENCED_PARAMETER( ArgumentString );
    UNREFERENCED_PARAMETER( Again );

ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    adaptExt->dump_mode  = IsCrashDumpMode;

    
    if (!InitHW(DeviceExtension, ConfigInfo)) {
        return SP_RETURN_ERROR;
    }
    GetScsiConfig(DeviceExtension);

    ConfigInfo->Master                      = TRUE;
    ConfigInfo->ScatterGather               = TRUE;
    ConfigInfo->DmaWidth                    = Width32Bits;
    ConfigInfo->Dma32BitAddresses           = TRUE;
    ConfigInfo->Dma64BitAddresses           = TRUE;
    ConfigInfo->WmiDataProvider             = FALSE;
    ConfigInfo->MapBuffers                  = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->SynchronizationModel        = StorSynchronizeFullDuplex;
    ConfigInfo->MapBuffers                  = TRUE;
    ConfigInfo->NumberOfBuses               = 1;
    ConfigInfo->MaximumNumberOfTargets      = (UCHAR)adaptExt->scsi_config.max_target;
    ConfigInfo->MaximumNumberOfLogicalUnits = (UCHAR)adaptExt->scsi_config.max_lun;
    ConfigInfo->MaximumTransferLength       = 0x00FFFFFF;

    if(adaptExt->dump_mode) {
        ConfigInfo->NumberOfPhysicalBreaks  = 8;
    } else {
        ConfigInfo->NumberOfPhysicalBreaks  = min((MAX_PHYS_SEGMENTS + 1), adaptExt->scsi_config.seg_max);
    }


    VirtIODeviceReset(DeviceExtension);
    StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL), (USHORT)0);

    if (adaptExt->dump_mode) {
        StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(USHORT)0);
    }

    adaptExt->features = StorPortReadPortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_HOST_FEATURES));

    pageNum = StorPortReadPortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_NUM));
    vr_sz = vring_size(pageNum,PAGE_SIZE);
    vq_sz = (sizeof(struct vring_virtqueue) + sizeof(PVOID) * pageNum);


    adaptExt->queue_depth = pageNum / ConfigInfo->NumberOfPhysicalBreaks - 1;

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("breaks_number = %x  queue_depth = %x\n",
                ConfigInfo->NumberOfPhysicalBreaks,
                adaptExt->queue_depth));

    ptr = (ULONG_PTR)StorPortGetUncachedExtension(DeviceExtension, ConfigInfo, (PAGE_SIZE + vr_sz + vq_sz) * 20);
    if (ptr == (ULONG_PTR)NULL) {
        StorPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Couldn't get uncached extension\n"));
        return SP_RETURN_ERROR;
    }

    ptr = (ULONG_PTR)PAGE_ALIGN(((ULONG)(ptr) + PAGE_SIZE - 1));
    adaptExt->pci_vq_info[0].queue = (PVOID)ptr;

    ptr = (ULONG_PTR)PAGE_ALIGN(((ULONG)(ptr) + ROUND_TO_PAGES(vr_sz)));
    adaptExt->virtqueue[0] = (vring_virtqueue*)(ptr);

    ptr = (ULONG_PTR)PAGE_ALIGN(((ULONG)(ptr) + ROUND_TO_PAGES(vq_sz)));
    adaptExt->pci_vq_info[1].queue = (PVOID)ptr;

    ptr = (ULONG_PTR)PAGE_ALIGN(((ULONG)(ptr) + ROUND_TO_PAGES(vr_sz)));
    adaptExt->virtqueue[1] = (vring_virtqueue*)(ptr);

    ptr = (ULONG_PTR)PAGE_ALIGN(((ULONG)(ptr) + ROUND_TO_PAGES(vq_sz)));
    adaptExt->pci_vq_info[2].queue = (PVOID)ptr;

    ptr = (ULONG_PTR)PAGE_ALIGN(((ULONG)(ptr) + ROUND_TO_PAGES(vr_sz)));
    adaptExt->virtqueue[2] = (vring_virtqueue*)(ptr);

EXIT_FN();
    return SP_RETURN_FOUND;
}
BOOLEAN
VioScsiPassiveInit (
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    adaptExt->pci_vq_info[0].vq = VirtIODeviceFindVirtualQueue(DeviceExtension, 0, 0);

    if (!adaptExt->pci_vq_info[0].vq) {
        StorPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find virtual queue 0\n"));
        return FALSE;
    }

    adaptExt->pci_vq_info[1].vq = VirtIODeviceFindVirtualQueue(DeviceExtension, 1, 0);

    if (!adaptExt->pci_vq_info[1].vq) {
        StorPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find virtual queue 1\n"));
        return FALSE;
    }

    adaptExt->pci_vq_info[2].vq = VirtIODeviceFindVirtualQueue(DeviceExtension, 2, 0);

    if (!adaptExt->pci_vq_info[2].vq) {
        StorPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find virtual queue 2\n"));
        return FALSE;
    }
    StorPortWritePortUchar(DeviceExtension, (PUCHAR)(adaptExt->device_base + VIRTIO_PCI_STATUS),(UCHAR)VIRTIO_CONFIG_S_DRIVER_OK);
    return TRUE;
}


BOOLEAN
VioScsiHwInitialize(
    IN PVOID DeviceExtension
    )
{
    return  StorPortEnablePassiveInitialization(DeviceExtension, VioScsiPassiveInit);
}

BOOLEAN
VioScsiStartIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
ENTER_FN();
    if(!DoReadWrite(DeviceExtension, Srb)) {
        Srb->SrbStatus = SRB_STATUS_BUSY;
        CompleteSRB(DeviceExtension, Srb);
EXIT_ERR();
    }
EXIT_FN();
    return TRUE;
}


BOOLEAN
VioScsiInterrupt(
    IN PVOID DeviceExtension
    )
{
    VirtIOSCSICmd       *cmd;
    VirtIOSCSICmdResp   *resp;
    unsigned int        len;
    PADAPTER_EXTENSION  adaptExt;
    BOOLEAN             isInterruptServiced = FALSE;
    PSCSI_REQUEST_BLOCK Srb;
    ULONG               intReason = 0;
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s (%d)\n", __FUNCTION__, KeGetCurrentIrql()));
    intReason = VirtIODeviceISR(DeviceExtension);

    if ( intReason == 1) {
        isInterruptServiced = TRUE;
        while((cmd = adaptExt->pci_vq_info[2].vq->vq_ops->get_buf(adaptExt->pci_vq_info[2].vq, &len)) != NULL) {
           Srb = (PSCSI_REQUEST_BLOCK)cmd->sc;
           resp = &cmd->resp.cmd;
              
           switch (resp->response) {
           case VIRTIO_SCSI_S_OK:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_OK\n"));
              Srb->SrbStatus = SRB_STATUS_SUCCESS;
              break;
           case VIRTIO_SCSI_S_UNDERRUN:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_UNDERRUN\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_ABORTED:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_ABORTED\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_BAD_TARGET:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_BAD_TARGET\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_RESET:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_RESET\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_BUSY:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_BUSY\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_TRANSPORT_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_TRANSPORT_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_TARGET_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_TARGET_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_NEXUS_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_NEXUS_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           default:
              Srb->SrbStatus = SRB_STATUS_ERROR;
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("Unknown response %d\n", resp->response));
              break;
           }
//           if (Srb->DataBuffer) {
//              memcpy(Srb->DataBuffer, resp->sense,
//                 min(resp->sense_len, VIRTIO_SCSI_SENSE_SIZE));
//           }
           CompleteSRB(DeviceExtension, Srb);
        }
    } else if (intReason == 3) {
//        ScsiPortNotification( BusChangeDetected, DeviceExtension, 0);
        isInterruptServiced = TRUE;
    }
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s isInterruptServiced = %d\n", __FUNCTION__, isInterruptServiced));
    return isInterruptServiced;
}

BOOLEAN
VioScsiResetBus(
    IN PVOID DeviceExtension,
    IN ULONG PathId
    )
{
    UNREFERENCED_PARAMETER( DeviceExtension );
    UNREFERENCED_PARAMETER( PathId );
ENTER_FN();
EXIT_FN();
    return TRUE;
}

SCSI_ADAPTER_CONTROL_STATUS
VioScsiAdapterControl(
    IN PVOID DeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters
    )
{
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST ControlTypeList;
    ULONG                             AdjustedMaxControlType;
    ULONG                             Index;
    PADAPTER_EXTENSION                adaptExt;
    SCSI_ADAPTER_CONTROL_STATUS       status = ScsiAdapterControlUnsuccessful;
    BOOLEAN SupportedConrolTypes[5] = {TRUE, TRUE, TRUE, FALSE, FALSE};

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

ENTER_FN();
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s %d\n", __FUNCTION__, ControlType));

    switch (ControlType) {

    case ScsiQuerySupportedControlTypes: {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("ScsiQuerySupportedControlTypes\n"));
        ControlTypeList = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
        AdjustedMaxControlType =
            (ControlTypeList->MaxControlType < 5) ?
            ControlTypeList->MaxControlType :
            5;
        for (Index = 0; Index < AdjustedMaxControlType; Index++) {
            ControlTypeList->SupportedTypeList[Index] =
                SupportedConrolTypes[Index];
        }
        status = ScsiAdapterControlSuccess;
        break;
    }
    case ScsiStopAdapter: {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("ScsiStopAdapter\n"));
        ShutDown(DeviceExtension);
        status = ScsiAdapterControlSuccess;
        break;
    }
    case ScsiRestartAdapter: {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("ScsiRestartAdapter\n"));
        VirtIODeviceReset(DeviceExtension);
        StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL), (USHORT)0);
        StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(USHORT)0);
        adaptExt->pci_vq_info[0].vq = NULL;

        if (!VioScsiHwInitialize(DeviceExtension))
        {
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot Initialize HW\n"));
           break;
        }
        status = ScsiAdapterControlSuccess;
        break;
    }
    default:
        break;
    }

EXIT_FN();
    return status;
}

BOOLEAN
VioScsiBuildIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PCDB                  cdb;
    ULONG                 i;
    ULONG                 fragLen;
    ULONG                 sgElement;
    ULONG                 sgMaxElements;
    PADAPTER_EXTENSION    adaptExt;
    PSRB_EXTENSION        srbExt;
    PSTOR_SCATTER_GATHER_LIST sgList;
    VirtIOSCSICmd         *cmd;

ENTER_FN();
    cdb      = (PCDB)&Srb->Cdb[0];
    srbExt   = (PSRB_EXTENSION)Srb->SrbExtension;
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if(Srb->PathId || Srb->TargetId || Srb->Lun) {
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        StorPortNotification(RequestComplete,
                             DeviceExtension,
                             Srb);
        return FALSE;
    }

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("<-->%s (%d::%d::%d)\n", DbgGetScsiOpStr(Srb), Srb->PathId, Srb->TargetId, Srb->Lun));
    
    memset(srbExt, 0, sizeof(*srbExt));

    cmd = &srbExt->cmd;
    cmd->sc = Srb;
    cmd->req.cmd.lun[0] = 1;
    cmd->req.cmd.lun[1] = Srb->TargetId;
    cmd->req.cmd.lun[2] = 0;
    cmd->req.cmd.lun[3] = Srb->Lun;
    cmd->req.cmd.tag = (unsigned long)Srb;
    cmd->req.cmd.task_attr = VIRTIO_SCSI_S_SIMPLE;
    cmd->req.cmd.prio = 0;
    cmd->req.cmd.crn = 0;
    memcpy(cmd->req.cmd.cdb, cdb, Srb->CdbLength);

    sgElement = 0;
    srbExt->sg[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->req.cmd, &fragLen);
    srbExt->sg[sgElement].ulSize   = sizeof(cmd->req.cmd);
    sgElement++;

    sgList = StorPortGetScatterGatherList(DeviceExtension, Srb);
    if (sgList)
    {
//FIXME
        sgMaxElements = sgList->NumberOfElements;
        srbExt->Xfer = 0; 

        if((Srb->SrbFlags & SRB_FLAGS_DATA_OUT) == SRB_FLAGS_DATA_OUT) {
            for (i = 0; i < sgMaxElements; i++, sgElement++) {
                srbExt->sg[sgElement].physAddr = sgList->List[i].PhysicalAddress;
                srbExt->sg[sgElement].ulSize   = sgList->List[i].Length;
                srbExt->Xfer += sgList->List[i].Length;
            }
        }
    }
    srbExt->out = sgElement;
    srbExt->sg[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->resp.cmd, &fragLen);
    srbExt->sg[sgElement].ulSize   = sizeof(cmd->resp.cmd);
    sgElement++;
    if (sgList)
    {
//FIXME
        sgMaxElements = sgList->NumberOfElements;
        srbExt->Xfer = 0; 

        if((Srb->SrbFlags & SRB_FLAGS_DATA_OUT) != SRB_FLAGS_DATA_OUT) {
            for (i = 0; i < sgMaxElements; i++, sgElement++) {
                srbExt->sg[sgElement].physAddr = sgList->List[i].PhysicalAddress;
                srbExt->sg[sgElement].ulSize   = sgList->List[i].Length;
                srbExt->Xfer += sgList->List[i].Length;
            }
        }
    }
    srbExt->in = sgElement - srbExt->out;

    if ((cdb->CDB6GENERIC.OperationCode == SCSIOP_READ_CAPACITY) ||
        (cdb->CDB6GENERIC.OperationCode == SCSIOP_READ_CAPACITY16))
    {
        StorPortSetDeviceQueueDepth( DeviceExtension, Srb->PathId,
                                     Srb->TargetId, Srb->Lun,
                                     adaptExt->queue_depth);
    }
EXIT_FN();
    return TRUE;
}


VOID
CompleteSRB(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
ENTER_FN();
    StorPortNotification(RequestComplete,
                         DeviceExtension,
                         Srb);
    StorPortNotification(NextLuRequest,
                         DeviceExtension,
                         Srb->PathId,
                         Srb->TargetId,
                         Srb->Lun);
EXIT_FN();
}
