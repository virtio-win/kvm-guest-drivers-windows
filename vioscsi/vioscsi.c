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

#if (NTDDI_VERSION > NTDDI_WIN7)
sp_DRIVER_INITIALIZE DriverEntry;
HW_INITIALIZE        VioScsiHwInitialize;
HW_BUILDIO           VioScsiBuildIo;
HW_STARTIO           VioScsiStartIo;
HW_FIND_ADAPTER      VioScsiFindAdapter;
HW_RESET_BUS         VioScsiResetBus;
HW_ADAPTER_CONTROL   VioScsiAdapterControl;
HW_INTERRUPT         VioScsiInterrupt;
#endif

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
    IN PBOOLEAN Again
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

BOOLEAN
FORCEINLINE
PreProcessRequest(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

VOID
FORCEINLINE
PostProcessRequest(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

VOID
FORCEINLINE
CompleteRequest(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );
BOOLEAN
VioScsiInterrupt(
    IN PVOID DeviceExtension
    );

ULONG
DriverEntry(
    IN PVOID  DriverObject,
    IN PVOID  RegistryPath
    )
{

    HW_INITIALIZATION_DATA hwInitData;
    ULONG                  initResult;

    InitializeDebugPrints((PDRIVER_OBJECT)DriverObject, (PUNICODE_STRING)RegistryPath);

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
    IN PBOOLEAN Again
    )
{
    PADAPTER_EXTENSION adaptExt;
    ULONG              allocationSize;
    ULONG              pageNum;
    ULONG              dummy;
    ULONG              Size;

    UNREFERENCED_PARAMETER( HwContext );
    UNREFERENCED_PARAMETER( BusInformation );
    UNREFERENCED_PARAMETER( ArgumentString );
    UNREFERENCED_PARAMETER( Again );

ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    memset(adaptExt, 0, sizeof(ADAPTER_EXTENSION));

    adaptExt->dump_mode  = IsCrashDumpMode;
    
    ConfigInfo->Master                      = TRUE;
    ConfigInfo->ScatterGather               = TRUE;
    ConfigInfo->DmaWidth                    = Width32Bits;
    ConfigInfo->Dma32BitAddresses           = TRUE;
    ConfigInfo->Dma64BitAddresses           = TRUE;
    ConfigInfo->WmiDataProvider             = FALSE;
    ConfigInfo->MapBuffers                  = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->SynchronizationModel        = StorSynchronizeFullDuplex;

    if (!InitHW(DeviceExtension, ConfigInfo)) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("Cannot initialize HardWare\n"));
        return SP_RETURN_NOT_FOUND;
    }

    GetScsiConfig(DeviceExtension);

    ConfigInfo->NumberOfBuses               = 1;
    ConfigInfo->MaximumNumberOfTargets      = (UCHAR)adaptExt->scsi_config.max_target;
    ConfigInfo->MaximumNumberOfLogicalUnits = (UCHAR)adaptExt->scsi_config.max_lun;
    if(adaptExt->dump_mode) {
        ConfigInfo->NumberOfPhysicalBreaks  = 8;
    } else {
        ConfigInfo->NumberOfPhysicalBreaks  = min((MAX_PHYS_SEGMENTS + 1), adaptExt->scsi_config.seg_max);
    }
    ConfigInfo->MaximumTransferLength       = 0x00FFFFFF;

    VirtIODeviceReset(&adaptExt->vdev);
    StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL), (USHORT)0);

    if (adaptExt->dump_mode) {
        StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(USHORT)0);
    }

    adaptExt->features = StorPortReadPortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_HOST_FEATURES));

    allocationSize = 0;
    adaptExt->offset[0] = 0;
    VirtIODeviceQueryQueueAllocation(&adaptExt->vdev, 0, &pageNum, &Size);
    allocationSize += ROUND_TO_PAGES(Size);
    adaptExt->offset[1] = ROUND_TO_PAGES(Size);
    VirtIODeviceQueryQueueAllocation(&adaptExt->vdev, 1, &dummy, &Size);
    allocationSize += ROUND_TO_PAGES(Size);
    adaptExt->offset[2] = adaptExt->offset[1] + ROUND_TO_PAGES(Size);
    VirtIODeviceQueryQueueAllocation(&adaptExt->vdev, 2, &dummy, &Size);
    allocationSize += ROUND_TO_PAGES(Size);
    adaptExt->offset[3] = adaptExt->offset[2] + ROUND_TO_PAGES(Size);
    allocationSize += ROUND_TO_PAGES(sizeof(SRB_EXTENSION));

#if (INDIRECT_SUPPORTED)
    if(!adaptExt->dump_mode) {
        adaptExt->indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);
    }
#else
    adaptExt->indirect = 0;
#endif


    if(adaptExt->indirect) {
        adaptExt->queue_depth = pageNum;
    } else {
        adaptExt->queue_depth = pageNum / ConfigInfo->NumberOfPhysicalBreaks - 1;
    }

    RhelDbgPrint(TRACE_LEVEL_ERROR, ("breaks_number = %x  queue_depth = %x\n",
                ConfigInfo->NumberOfPhysicalBreaks,
                adaptExt->queue_depth));


    adaptExt->uncachedExtensionVa = StorPortGetUncachedExtension(DeviceExtension, ConfigInfo, allocationSize);
    if (!adaptExt->uncachedExtensionVa) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Couldn't get uncached extension\n"));
        return SP_RETURN_ERROR;
    }

    return SP_RETURN_FOUND;
}


static struct virtqueue *FindVirtualQueue(PADAPTER_EXTENSION adaptExt, ULONG index, ULONG vector)
{
    struct virtqueue *vq = NULL;
    if (adaptExt->uncachedExtensionVa)
    {
        ULONG len;
        PVOID  ptr = (PVOID)((ULONG_PTR)adaptExt->uncachedExtensionVa + adaptExt->offset[index]);
        PHYSICAL_ADDRESS pa = StorPortGetPhysicalAddress(adaptExt, NULL, ptr, &len);
        if (pa.QuadPart)
        {
           vq = VirtIODevicePrepareQueue(&adaptExt->vdev, index, pa, ptr, len, NULL, FALSE);
        }

        if (vq && vector)
        {
           unsigned res;
           StorPortWritePortUshort(adaptExt, (PUSHORT)(adaptExt->vdev.addr + VIRTIO_MSI_QUEUE_VECTOR),(USHORT)vector);
           res = StorPortReadPortUshort(adaptExt, (PUSHORT)(adaptExt->vdev.addr + VIRTIO_MSI_QUEUE_VECTOR));
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> VIRTIO_MSI_QUEUE_VECTOR vector = %d, res = 0x%x\n", __FUNCTION__, vector, res));
           if(res == VIRTIO_MSI_NO_VECTOR)
           {
              VirtIODeviceDeleteQueue(vq, NULL);
              vq = NULL;
              RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> Cannot create vq vector\n", __FUNCTION__));
              return NULL;
           }
        }
    }
    return vq;
}

BOOLEAN
VioScsiHwInitialize(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PVOID              ptr      = adaptExt->uncachedExtensionVa;

    adaptExt->vq[0] = FindVirtualQueue(adaptExt, 0, 0);
    if (!adaptExt->vq[0]) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find virtual queue 0\n"));
        return FALSE;
    }

    adaptExt->vq[1] = FindVirtualQueue(adaptExt, 1, 0);

    if (!adaptExt->vq[1]) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find virtual queue 1\n"));
        return FALSE;
    }

    adaptExt->vq[2] = FindVirtualQueue(adaptExt, 2, 0);

    if (!adaptExt->vq[2]) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find virtual queue 2\n"));
        return FALSE;
    }
    adaptExt->tmf_cmd.SrbExtension = (PSRB_EXTENSION)((ULONG_PTR)adaptExt->uncachedExtensionVa + adaptExt->offset[3]);
    StorPortWritePortUchar(DeviceExtension, (PUCHAR)(adaptExt->device_base + VIRTIO_PCI_STATUS),(UCHAR)VIRTIO_CONFIG_S_DRIVER_OK);
    return TRUE;
}

BOOLEAN
VioScsiStartIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
ENTER_FN();
    if (PreProcessRequest(DeviceExtension, Srb) ||
        !SendSRB(DeviceExtension, Srb))
    {
        if(Srb->SrbStatus == SRB_STATUS_PENDING)
        {
           Srb->SrbStatus = SRB_STATUS_ERROR;
        }
        CompleteRequest(DeviceExtension, Srb);
    }
EXIT_FN();
    return TRUE;
}


BOOLEAN
VioScsiInterrupt(
    IN PVOID DeviceExtension
    )
{
    PVirtIOSCSICmd      cmd;
    unsigned int        len;
    PADAPTER_EXTENSION  adaptExt;
    BOOLEAN             isInterruptServiced = FALSE;
    PSCSI_REQUEST_BLOCK Srb;
    PSRB_EXTENSION      srbExt;
    ULONG               intReason = 0;
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s (%d)\n", __FUNCTION__, KeGetCurrentIrql()));
    intReason = VirtIODeviceISR(&adaptExt->vdev);

    if ( intReason == 1) {
        isInterruptServiced = TRUE;
        while((cmd = (PVirtIOSCSICmd)adaptExt->vq[2]->vq_ops->get_buf(adaptExt->vq[2], &len)) != NULL) {
           VirtIOSCSICmdResp   *resp;
           Srb     = (PSCSI_REQUEST_BLOCK)cmd->sc;
           resp    = &cmd->resp.cmd;
           srbExt  = (PSRB_EXTENSION)Srb->SrbExtension;

           switch (resp->response) {
           case VIRTIO_SCSI_S_OK:
              Srb->SrbStatus = SRB_STATUS_SUCCESS;
              break;
           case VIRTIO_SCSI_S_UNDERRUN:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_UNDERRUN\n"));
              Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
              break;
           case VIRTIO_SCSI_S_ABORTED:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_ABORTED\n"));
              Srb->SrbStatus = SRB_STATUS_ABORTED;
              break;
           case VIRTIO_SCSI_S_BAD_TARGET:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_BAD_TARGET\n"));
              Srb->SrbStatus = SRB_STATUS_INVALID_TARGET_ID;
              break;
           case VIRTIO_SCSI_S_RESET:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_RESET\n"));
              Srb->SrbStatus = SRB_STATUS_BUS_RESET;
              break;
           case VIRTIO_SCSI_S_BUSY:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("VIRTIO_SCSI_S_BUSY\n"));
              Srb->SrbStatus = SRB_STATUS_BUSY;
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
           if (Srb->DataBuffer) {
              memcpy(Srb->DataBuffer, resp->sense,
              min(resp->sense_len, Srb->DataTransferLength));
           }
           if (srbExt->Xfer && Srb->DataTransferLength > srbExt->Xfer) {
              Srb->DataTransferLength = srbExt->Xfer;
              Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
           }
           CompleteRequest(DeviceExtension, Srb);
        }
        if (adaptExt->tmf_infly) {
           while((cmd = (PVirtIOSCSICmd)adaptExt->vq[0]->vq_ops->get_buf(adaptExt->vq[0], &len)) != NULL) {
              VirtIOSCSICtrlTMFResp *resp;
              Srb = (PSCSI_REQUEST_BLOCK)cmd->sc;
              ASSERT(Srb == &adaptExt->tmf_cmd.Srb);
              resp = &cmd->resp.tmf;
              switch(resp->response) {
              case VIRTIO_SCSI_S_OK:
              case VIRTIO_SCSI_S_FUNCTION_SUCCEEDED:
                 break;
              default:
                 RhelDbgPrint(TRACE_LEVEL_ERROR, ("Unknown response %d\n", resp->response));
                 ASSERT(0);
                 break;
              }
              StorPortResume(DeviceExtension);
           }
           adaptExt->tmf_infly = FALSE;
        }
    } else if (intReason == 3) {
        StorPortNotification( BusChangeDetected, DeviceExtension, 0);
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
    UNREFERENCED_PARAMETER( PathId );

    return DeviceReset(DeviceExtension);
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
        VirtIODeviceReset(&adaptExt->vdev);
        StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL), (USHORT)0);
        StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(USHORT)0);
        adaptExt->vq[0] = NULL;
        adaptExt->vq[1] = NULL;
        adaptExt->vq[2] = NULL;

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

    if( (Srb->PathId > 0) ||
        (Srb->TargetId >= adaptExt->scsi_config.max_target) ||
        (Srb->Lun >= adaptExt->scsi_config.max_lun) ) {
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
        sgMaxElements = sgList->NumberOfElements;

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
        sgMaxElements = sgList->NumberOfElements;

        if((Srb->SrbFlags & SRB_FLAGS_DATA_OUT) != SRB_FLAGS_DATA_OUT) {
            for (i = 0; i < sgMaxElements; i++, sgElement++) {
                srbExt->sg[sgElement].physAddr = sgList->List[i].PhysicalAddress;
                srbExt->sg[sgElement].ulSize   = sgList->List[i].Length;
                srbExt->Xfer += sgList->List[i].Length;
            }
        }
    }
    srbExt->in = sgElement - srbExt->out;

EXIT_FN();
    return TRUE;
}

BOOLEAN
FORCEINLINE
PreProcessRequest(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PADAPTER_EXTENSION adaptExt;

ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    switch (Srb->Function) {
        case SRB_FUNCTION_PNP:
        case SRB_FUNCTION_POWER:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_LOGICAL_UNIT: {
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            return TRUE;
        }
    }
EXIT_FN();
    return FALSE;
}

VOID
PostProcessRequest(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PCDB                  cdb;
    PADAPTER_EXTENSION    adaptExt;
    PSRB_EXTENSION        srbExt;

ENTER_FN();
    cdb      = (PCDB)&Srb->Cdb[0];
    srbExt   = (PSRB_EXTENSION)Srb->SrbExtension;
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    switch (cdb->CDB6GENERIC.OperationCode)
    {
        case SCSIOP_READ_CAPACITY:
        case SCSIOP_READ_CAPACITY16:
           StorPortSetDeviceQueueDepth( DeviceExtension, Srb->PathId,
                                     Srb->TargetId, Srb->Lun,
                                     adaptExt->queue_depth);
           break;
        default:
           break;

    }
EXIT_FN();
}

VOID
CompleteRequest(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
ENTER_FN();
    PostProcessRequest(DeviceExtension, Srb);
    StorPortNotification(RequestComplete,
                         DeviceExtension,
                         Srb);
EXIT_FN();
}

VOID
LogError(
    IN PVOID DeviceExtension,
    IN ULONG ErrorCode,
    IN ULONG UniqueId
    )
{
    STOR_LOG_EVENT_DETAILS logEvent;
    memset( &logEvent, 0, sizeof(logEvent) );
    logEvent.InterfaceRevision         = STOR_CURRENT_LOG_INTERFACE_REVISION;
    logEvent.Size                      = sizeof(logEvent);
    logEvent.EventAssociation          = StorEventAdapterAssociation;
    logEvent.StorportSpecificErrorCode = TRUE;
    logEvent.ErrorCode                 = ErrorCode;
    logEvent.DumpDataSize              = sizeof(UniqueId);
    logEvent.DumpData                  = &UniqueId;

    StorPortLogSystemEvent( DeviceExtension, &logEvent, NULL );
}
