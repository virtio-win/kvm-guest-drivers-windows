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
#if (MSI_SUPPORTED == 1)
HW_MESSAGE_SIGNALED_INTERRUPT_ROUTINE VioScsiMSInterrupt;
#endif
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

VOID
TransportReset(
    IN PVOID DeviceExtension,
    IN PVirtIOSCSIEvent evt
    );

VOID
ParamChange(
    IN PVOID DeviceExtension,
    IN PVirtIOSCSIEvent evt
    );

#if (MSI_SUPPORTED == 1)
BOOLEAN
VioScsiMSInterrupt(
    IN PVOID  DeviceExtension,
    IN ULONG  MessageID
    );
#endif

ULONG
DriverEntry(
    IN PVOID  DriverObject,
    IN PVOID  RegistryPath
    )
{

    HW_INITIALIZATION_DATA hwInitData;
    ULONG                  initResult;

    InitializeDebugPrints((PDRIVER_OBJECT)DriverObject, (PUNICODE_STRING)RegistryPath);

    RhelDbgPrint(TRACE_LEVEL_FATAL, ("Vioscsi driver started...built on %s %s\n", __DATE__, __TIME__));
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
    ULONG              pageNum;
    ULONG              Size;
    ULONG              index;
    ULONG              num_cpus;
#if (MSI_SUPPORTED == 1)
    PPCI_COMMON_CONFIG pPciConf = NULL;
    UCHAR              pci_cfg_buf[256];
    ULONG              pci_cfg_len;
#endif

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
    ConfigInfo->AlignmentMask               = 0x3;
    ConfigInfo->MapBuffers                  = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->SynchronizationModel        = StorSynchronizeFullDuplex;
#if (MSI_SUPPORTED == 1)
    ConfigInfo->HwMSInterruptRoutine        = VioScsiMSInterrupt;
    ConfigInfo->InterruptSynchronizationMode=InterruptSynchronizePerMessage;
#endif
    if (!InitHW(DeviceExtension, ConfigInfo)) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot initialize HardWare\n"));
        return SP_RETURN_NOT_FOUND;
    }

#if (MSI_SUPPORTED == 1)
    pci_cfg_len = StorPortGetBusData (DeviceExtension,
                                           PCIConfiguration,
                                           ConfigInfo->SystemIoBusNumber,
                                           (ULONG)ConfigInfo->SlotNumber,
                                           (PVOID)pci_cfg_buf,
                                           (ULONG)256);
    if (pci_cfg_len == 256)
    {
        UCHAR CapOffset;
        PPCI_MSIX_CAPABILITY pMsixCapOffset;

        pPciConf = (PPCI_COMMON_CONFIG)pci_cfg_buf;
        if ( (pPciConf->Status & PCI_STATUS_CAPABILITIES_LIST) == 0)
        {
           RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("NO CAPABILITIES_LIST\n"));
        }
        else
        {
           if ( (pPciConf->HeaderType & (~PCI_MULTIFUNCTION)) == PCI_DEVICE_TYPE )
           {
              CapOffset = pPciConf->u.type0.CapabilitiesPtr;
              while (CapOffset != 0)
              {
                 pMsixCapOffset = (PPCI_MSIX_CAPABILITY)(pci_cfg_buf + CapOffset);
                 if ( pMsixCapOffset->Header.CapabilityID == PCI_CAPABILITY_ID_MSIX )
                 {
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageControl.TableSize = %d\n", pMsixCapOffset->MessageControl.TableSize));
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageControl.FunctionMask = %d\n", pMsixCapOffset->MessageControl.FunctionMask));
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageControl.MSIXEnable = %d\n", pMsixCapOffset->MessageControl.MSIXEnable));

                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageTable = %p\n", pMsixCapOffset->MessageTable));
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("PBATable = %d\n", pMsixCapOffset->PBATable));
                    adaptExt->msix_enabled = (pMsixCapOffset->MessageControl.MSIXEnable == 1);
                 }
                 else
                 {
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("CapabilityID = %x, Next CapOffset = %x\n", pMsixCapOffset->Header.CapabilityID, CapOffset));
                 }
                 CapOffset = pMsixCapOffset->Header.Next;
              }
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("msix_enabled = %d\n", adaptExt->msix_enabled));
              VirtIODeviceSetMSIXUsed(adaptExt->pvdev, adaptExt->msix_enabled);
           }
           else
           {
              RhelDbgPrint(TRACE_LEVEL_FATAL, ("NOT A PCI_DEVICE_TYPE\n"));
           }
        }
    }
    else
    {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("CANNOT READ PCI CONFIGURATION SPACE %d\n", pci_cfg_len));
    }
#endif

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

    VirtIODeviceReset(adaptExt->pvdev);

#if (NTDDI_VERSION >= NTDDI_WIN7)
    num_cpus = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
#else
    num_cpus = KeQueryActiveProcessorCount(NULL);
#endif
    adaptExt->num_queues = adaptExt->scsi_config.num_queues;
    memset(adaptExt->cpu_to_vq_map, (UCHAR)VIRTIO_SCSI_REQUEST_QUEUE_0, MAX_CPU);
    if (adaptExt->dump_mode || !adaptExt->msix_enabled)
    {
        adaptExt->num_queues = 1;
    }
    else if (adaptExt->num_queues < num_cpus)
    {
//FIXME
        adaptExt->num_queues = 1;
    }
    else
    {
//FIXME
        adaptExt->num_queues = num_cpus;
    }

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Queues %d CPUs %d\n", adaptExt->num_queues, num_cpus));

    if (adaptExt->dump_mode) {
        for (index = VIRTIO_SCSI_CONTROL_QUEUE; index < adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0; ++index) {
            StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL), (USHORT)index);
            StorPortWritePortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN), (ULONG)0);
        }
    }

    adaptExt->features = StorPortReadPortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_HOST_FEATURES));

    adaptExt->allocationSize = PAGE_SIZE;
    adaptExt->offset = 0;
    Size = 0;
    for (index = VIRTIO_SCSI_CONTROL_QUEUE; index <= VIRTIO_SCSI_REQUEST_QUEUE_0; ++index) {
        VirtIODeviceQueryQueueAllocation(adaptExt->pvdev, index, &pageNum, &Size);
        if (Size == 0) {
            LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

            RhelDbgPrint(TRACE_LEVEL_FATAL, ("Virtual queue %d config failed.\n", index));
            return SP_RETURN_ERROR;
        }
        adaptExt->allocationSize += ROUND_TO_PAGES(Size);
    }
    adaptExt->allocationSize += (ROUND_TO_PAGES(Size) * (adaptExt->num_queues - 1));
    if (adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0 > MAX_QUEUES_PER_DEVICE_DEFAULT)
    {
        adaptExt->allocationSize += ROUND_TO_PAGES(VirtIODeviceSizeRequired((USHORT)(adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0)));
    }
    adaptExt->allocationSize += ROUND_TO_PAGES(sizeof(SRB_EXTENSION));
    adaptExt->allocationSize += ROUND_TO_PAGES(sizeof(VirtIOSCSIEventNode) * 8);

#if (INDIRECT_SUPPORTED == 1)
    if(!adaptExt->dump_mode) {
        adaptExt->indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);
    }
#else
    adaptExt->indirect = 0;
#endif

    // The windows device queue must be between 20 and 254 for
    // StorPortSetDeviceQueueDepth to succeed.
    if(adaptExt->indirect) {
        adaptExt->queue_depth = min(254, max(20, (pageNum / 4)));
    } else {
        adaptExt->queue_depth = min(254, pageNum / ConfigInfo->NumberOfPhysicalBreaks - 1);
    }

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("breaks_number = %x  queue_depth = %x\n",
                ConfigInfo->NumberOfPhysicalBreaks,
                adaptExt->queue_depth));

    adaptExt->uncachedExtensionVa = StorPortGetUncachedExtension(DeviceExtension, ConfigInfo, adaptExt->allocationSize);
    if (!adaptExt->uncachedExtensionVa) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Can't get uncached extension allocation size = %d\n", adaptExt->allocationSize));
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
        PVOID  ptr = (PVOID)((ULONG_PTR)adaptExt->uncachedExtensionVa + adaptExt->offset);
        PHYSICAL_ADDRESS pa = StorPortGetPhysicalAddress(adaptExt, NULL, ptr, &len);
        BOOLEAN useEventIndex = CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX);
        if (pa.QuadPart)
        {
           ULONG Size;
           ULONG dummy;
           VirtIODeviceQueryQueueAllocation(adaptExt->pvdev, index, &dummy, &Size);
           ASSERT((adaptExt->offset + Size) < adaptExt->allocationSize);
           vq = VirtIODevicePrepareQueue(adaptExt->pvdev, index, pa, ptr, len, NULL, useEventIndex);
           if (vq == NULL)
           {
               RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> cannot create virtual queue index = %d vector = % pa = %08I64X\n", __FUNCTION__, index, vector, pa.QuadPart));
               return NULL;
           }
           adaptExt->offset += ROUND_TO_PAGES(Size);
           RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("%s index = %lu Size = %lu offset = %lu\n", __FUNCTION__, index, Size, adaptExt->offset));
        }

        if (vq == NULL)
        {
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> cannot create virtual queue index = %d vector = % pa = %08I64X\n", __FUNCTION__, index, vector, pa.QuadPart));
           return NULL;
        }
        if (vector)
        {
           unsigned res;
           StorPortWritePortUshort(adaptExt, (PUSHORT)(adaptExt->pvdev->addr + VIRTIO_MSI_QUEUE_VECTOR),(USHORT)vector);
           res = StorPortReadPortUshort(adaptExt, (PUSHORT)(adaptExt->pvdev->addr + VIRTIO_MSI_QUEUE_VECTOR));
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> VIRTIO_MSI_QUEUE_VECTOR vector = %d, res = 0x%x\n", __FUNCTION__, vector, res));
           if(res == VIRTIO_MSI_NO_VECTOR)
           {
              VirtIODeviceDeleteQueue(vq, NULL);
              vq = NULL;
              RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> Cannot create vq vector\n", __FUNCTION__));
              return NULL;
           }
           StorPortWritePortUshort(adaptExt, (PUSHORT)(adaptExt->pvdev->addr + VIRTIO_MSI_CONFIG_VECTOR),(USHORT)vector);
           res = StorPortReadPortUshort(adaptExt, (PUSHORT)(adaptExt->pvdev->addr + VIRTIO_MSI_CONFIG_VECTOR));
           if (res != vector)
           {
              VirtIODeviceDeleteQueue(vq, NULL);
              vq = NULL;
              RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> Cannot set config vector\n", __FUNCTION__));
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
    ULONG              i;
    ULONG              guestFeatures = 0;
    ULONG              index;

#if (MSI_SUPPORTED == 1)
    MESSAGE_INTERRUPT_INFORMATION msi_info;
#endif
    
ENTER_FN();
    if (CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX)) {
        guestFeatures |= (1ul << VIRTIO_RING_F_EVENT_IDX);
    }
    if (CHECKBIT(adaptExt->features, VIRTIO_SCSI_F_CHANGE)) {
        guestFeatures |= (1ul << VIRTIO_SCSI_F_CHANGE);
    }
    if (CHECKBIT(adaptExt->features, VIRTIO_SCSI_F_HOTPLUG)) {
        guestFeatures |= (1ul << VIRTIO_SCSI_F_HOTPLUG);
    }
    StorPortWritePortUlong(DeviceExtension,
             (PULONG)(adaptExt->device_base + VIRTIO_PCI_GUEST_FEATURES), guestFeatures);

    adaptExt->msix_vectors = 0;
    adaptExt->offset = 0;

#if (MSI_SUPPORTED == 1)
    while(StorPortGetMSIInfo(DeviceExtension, adaptExt->msix_vectors, &msi_info) == STOR_STATUS_SUCCESS) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageId = %x\n", msi_info.MessageId));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageData = %x\n", msi_info.MessageData));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("InterruptVector = %x\n", msi_info.InterruptVector));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("InterruptLevel = %x\n", msi_info.InterruptLevel));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("InterruptMode = %s\n", msi_info.InterruptMode == LevelSensitive ? "LevelSensitive" : "Latched"));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageAddress = %p\n\n", msi_info.MessageAddress));
        ++adaptExt->msix_vectors;
    }

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Queues %d msix_vectors %d\n", adaptExt->num_queues, adaptExt->msix_vectors));
    if (adaptExt->num_queues > 1 &&
        ((adaptExt->num_queues + 3) > adaptExt->msix_vectors)) {
        //FIXME
        adaptExt->num_queues = 1;
    }

    if (!adaptExt->dump_mode &&
        (adaptExt->msix_vectors >= adaptExt->num_queues + 3)) {
//HACK
        if (adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0 > MAX_QUEUES_PER_DEVICE_DEFAULT)
        {
            ULONG_PTR ptr = ((ULONG_PTR)adaptExt->uncachedExtensionVa + adaptExt->offset);
            ULONG size = ROUND_TO_PAGES(VirtIODeviceSizeRequired((USHORT)(adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0)));
            adaptExt->offset += size;
            memcpy((PVOID)ptr, (PVOID)adaptExt->pvdev, sizeof(VirtIODevice));
            adaptExt->pvdev = (VirtIODevice*)ptr;
            VirtIODeviceInitialize(adaptExt->pvdev,  adaptExt->device_base, size);
        }

        for (index = VIRTIO_SCSI_CONTROL_QUEUE; index < adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0; ++index) {
            adaptExt->vq[index] = FindVirtualQueue(adaptExt, index, index + 1);
              if ((adaptExt->num_queues > 1) &&
                  (index >= VIRTIO_SCSI_REQUEST_QUEUE_0)) {
                  adaptExt->cpu_to_vq_map[index - VIRTIO_SCSI_REQUEST_QUEUE_0] = (UCHAR)index;
              }
        }
    }
#else
    adaptExt->num_queues = 1;
#endif
    for (index = VIRTIO_SCSI_CONTROL_QUEUE; index < adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0; ++index) {
        if (!adaptExt->vq[index]) {
            adaptExt->vq[index] = FindVirtualQueue(adaptExt, index, 0);
        }
        if (!adaptExt->vq[index]) {
            RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find virtual queue %d\n", index));
            return FALSE;
        }
    }

    adaptExt->tmf_cmd.SrbExtension = (PSRB_EXTENSION)((ULONG_PTR)adaptExt->uncachedExtensionVa + adaptExt->offset);
    adaptExt->offset += ROUND_TO_PAGES(sizeof(SRB_EXTENSION));
    adaptExt->events = (PVirtIOSCSIEventNode)((ULONG_PTR)adaptExt->uncachedExtensionVa + adaptExt->offset);
    adaptExt->offset += ROUND_TO_PAGES(sizeof(VirtIOSCSIEventNode)* 8);

    if (!adaptExt->dump_mode && CHECKBIT(adaptExt->features, VIRTIO_SCSI_F_HOTPLUG)) {
        PVirtIOSCSIEventNode events = adaptExt->events;
        for (i = 0; i < 8; i++) {
           if (!KickEvent(DeviceExtension, (PVOID)(&events[i]))) {
                RhelDbgPrint(TRACE_LEVEL_FATAL, ("Can't add event %d\n", i));
           }
        }
    }

    StorPortWritePortUchar(DeviceExtension,
           (PUCHAR)(adaptExt->device_base + VIRTIO_PCI_STATUS),
           (UCHAR)VIRTIO_CONFIG_S_DRIVER_OK);
EXIT_FN();
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
    PVirtIOSCSIEventNode evtNode;
    unsigned int        len;
    PADAPTER_EXTENSION  adaptExt;
    BOOLEAN             isInterruptServiced = FALSE;
    PSCSI_REQUEST_BLOCK Srb;
    PSRB_EXTENSION      srbExt;
    ULONG               intReason = 0;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s (%d)\n", __FUNCTION__, KeGetCurrentIrql()));
    intReason = VirtIODeviceISR(adaptExt->pvdev);

    if ( intReason == 1) {
        isInterruptServiced = TRUE;
        while((cmd = (PVirtIOSCSICmd)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_REQUEST_QUEUE_0], &len)) != NULL) {
           VirtIOSCSICmdResp   *resp;
           Srb     = (PSCSI_REQUEST_BLOCK)cmd->sc;
           resp    = &cmd->resp.cmd;
           srbExt  = (PSRB_EXTENSION)Srb->SrbExtension;

           switch (resp->response) {
           case VIRTIO_SCSI_S_OK:
              Srb->ScsiStatus = resp->status;
              Srb->SrbStatus = (Srb->ScsiStatus == SCSISTAT_GOOD) ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_UNDERRUN:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_UNDERRUN\n"));
              Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
              break;
           case VIRTIO_SCSI_S_ABORTED:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_ABORTED\n"));
              Srb->SrbStatus = SRB_STATUS_ABORTED;
              break;
           case VIRTIO_SCSI_S_BAD_TARGET:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_BAD_TARGET\n"));
              Srb->SrbStatus = SRB_STATUS_INVALID_TARGET_ID;
              break;
           case VIRTIO_SCSI_S_RESET:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_RESET\n"));
              Srb->SrbStatus = SRB_STATUS_BUS_RESET;
              break;
           case VIRTIO_SCSI_S_BUSY:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_BUSY\n"));
              Srb->SrbStatus = SRB_STATUS_BUSY;
              break;
           case VIRTIO_SCSI_S_TRANSPORT_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_TRANSPORT_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_TARGET_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_TARGET_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_NEXUS_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_NEXUS_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           default:
              Srb->SrbStatus = SRB_STATUS_ERROR;
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Unknown response %d\n", resp->response));
              break;
           }
           if (Srb->SrbStatus != SRB_STATUS_SUCCESS)
           {
              PSENSE_DATA pSense = (PSENSE_DATA) Srb->SenseInfoBuffer;
              if (Srb->SenseInfoBufferLength >= FIELD_OFFSET(SENSE_DATA, CommandSpecificInformation)) {
                 memcpy(Srb->SenseInfoBuffer, resp->sense,
                 min(resp->sense_len, Srb->SenseInfoBufferLength));
                 if (Srb->SrbStatus == SRB_STATUS_ERROR) {
                     Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                 }
              }
              Srb->DataTransferLength = 0;
           }
           else if (srbExt && srbExt->Xfer && Srb->DataTransferLength > srbExt->Xfer)
           {
              Srb->DataTransferLength = srbExt->Xfer;
              Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
           }
           CompleteRequest(DeviceExtension, Srb);
        }
        if (adaptExt->tmf_infly) {
           while((cmd = (PVirtIOSCSICmd)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_CONTROL_QUEUE], &len)) != NULL) {
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
        while((evtNode = (PVirtIOSCSIEventNode)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_EVENTS_QUEUE], &len)) != NULL) {
           PVirtIOSCSIEvent evt = &evtNode->event;
           switch (evt->event) {
           case VIRTIO_SCSI_T_NO_EVENT:
              break;
           case VIRTIO_SCSI_T_TRANSPORT_RESET:
              TransportReset(DeviceExtension, evt);
              break;
           case VIRTIO_SCSI_T_PARAM_CHANGE:
              ParamChange(DeviceExtension, evt);
              break;
           default:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("Unsupport virtio scsi event %x\n", evt->event));
              break;
           }
           SynchronizedKickEventRoutine(DeviceExtension, evtNode);
        }
    }
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s isInterruptServiced = %d\n", __FUNCTION__, isInterruptServiced));
    return isInterruptServiced;
}

#if (MSI_SUPPORTED == 1)
BOOLEAN
VioScsiMSInterrupt (
    IN PVOID  DeviceExtension,
    IN ULONG  MessageID
    )
{
    PVirtIOSCSICmd      cmd;
    PVirtIOSCSIEventNode evtNode;
    unsigned int        len;
    PADAPTER_EXTENSION  adaptExt;
    BOOLEAN             isInterruptServiced = FALSE;
    PSCSI_REQUEST_BLOCK Srb;
    PSRB_EXTENSION      srbExt;
    ULONG               intReason = 0;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 ("<--->%s : MessageID 0x%x\n", __FUNCTION__, MessageID));

    if (MessageID == 0)
    {
       return TRUE;
    }
    if (MessageID == 1)
    {
        if (adaptExt->tmf_infly)
        {
           while((cmd = (PVirtIOSCSICmd)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_CONTROL_QUEUE], &len)) != NULL)
           {
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
        return TRUE;
    }
    if (MessageID == 2) {
        while((evtNode = (PVirtIOSCSIEventNode)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_EVENTS_QUEUE], &len)) != NULL) {
           PVirtIOSCSIEvent evt = &evtNode->event;
           switch (evt->event) {
           case VIRTIO_SCSI_T_NO_EVENT:
              break;
           case VIRTIO_SCSI_T_TRANSPORT_RESET:
              TransportReset(DeviceExtension, evt);
              break;
           case VIRTIO_SCSI_T_PARAM_CHANGE:
              ParamChange(DeviceExtension, evt);
              break;
           default:
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("Unsupport virtio scsi event %x\n", evt->event));
              break;
           }
           SynchronizedKickEventRoutine(DeviceExtension, evtNode);
        }
        return TRUE;
    }
    if (MessageID > 2)
    {
        ULONG msg = MessageID - 3;

#if (NTDDI_VERSION >= NTDDI_WIN7)
        PROCESSOR_NUMBER ProcNumber;
        ULONG processor = KeGetCurrentProcessorNumberEx(&ProcNumber);
#else
        ULONG processor = KeGetCurrentProcessorNumber();
#endif
        while((cmd = (PVirtIOSCSICmd)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_REQUEST_QUEUE_0 + msg], &len)) != NULL)
        {
           VirtIOSCSICmdResp   *resp;
           Srb     = (PSCSI_REQUEST_BLOCK)cmd->sc;
           resp    = &cmd->resp.cmd;
           srbExt  = (PSRB_EXTENSION)Srb->SrbExtension;

           if ((adaptExt->num_queues > 1) &&
#if (NTDDI_VERSION >= NTDDI_WIN7)
               (ProcNumber.Group != srbExt->procNum.Group ||
               ProcNumber.Number != srbExt->procNum.Number)
#else
               processor != srbExt->procNum.Number
#endif
               )
           {
               ULONG tmp;
               RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Srb %p issued on %d::%d received on %d::%d MessgeId %d Queue %d\n",
                   Srb, srbExt->procNum.Group, srbExt->procNum.Number, ProcNumber.Group, ProcNumber.Number, MessageID, VIRTIO_SCSI_REQUEST_QUEUE_0 + msg));

#if (NTDDI_VERSION >= NTDDI_WIN7)
               tmp = adaptExt->cpu_to_vq_map[ProcNumber.Number];
               adaptExt->cpu_to_vq_map[ProcNumber.Number] = adaptExt->cpu_to_vq_map[srbExt->procNum.Number];
#else
               tmp = adaptExt->cpu_to_vq_map[processor];
               adaptExt->cpu_to_vq_map[processor] = adaptExt->cpu_to_vq_map[srbExt->procNum.Number];
#endif
               adaptExt->cpu_to_vq_map[srbExt->procNum.Number] = (UCHAR)tmp;
           }

           switch (resp->response)
           {
           case VIRTIO_SCSI_S_OK:
              Srb->ScsiStatus = resp->status;
              Srb->SrbStatus = (Srb->ScsiStatus == SCSISTAT_GOOD) ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_UNDERRUN:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_UNDERRUN\n"));
              Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
              break;
           case VIRTIO_SCSI_S_ABORTED:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_ABORTED\n"));
              Srb->SrbStatus = SRB_STATUS_ABORTED;
              break;
           case VIRTIO_SCSI_S_BAD_TARGET:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_BAD_TARGET\n"));
              Srb->SrbStatus = SRB_STATUS_INVALID_TARGET_ID;
              break;
           case VIRTIO_SCSI_S_RESET:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_RESET\n"));
              Srb->SrbStatus = SRB_STATUS_BUS_RESET;
              break;
           case VIRTIO_SCSI_S_BUSY:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_BUSY\n"));
              Srb->SrbStatus = SRB_STATUS_BUSY;
              break;
           case VIRTIO_SCSI_S_TRANSPORT_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_TRANSPORT_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_TARGET_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_TARGET_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_NEXUS_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_NEXUS_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           case VIRTIO_SCSI_S_FAILURE:
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_FAILURE\n"));
              Srb->SrbStatus = SRB_STATUS_ERROR;
              break;
           default:
              Srb->SrbStatus = SRB_STATUS_ERROR;
              RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Unknown response %d\n", resp->response));
              break;
           }
           if (Srb->SrbStatus != SRB_STATUS_SUCCESS)
           {
              PSENSE_DATA pSense = (PSENSE_DATA) Srb->SenseInfoBuffer;
              if (Srb->SenseInfoBufferLength >= FIELD_OFFSET(SENSE_DATA, CommandSpecificInformation)) {
                 memcpy(Srb->SenseInfoBuffer, resp->sense,
                 min(resp->sense_len, Srb->SenseInfoBufferLength));
                 if (Srb->SrbStatus == SRB_STATUS_ERROR) {
                     Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                 }
              }
              Srb->DataTransferLength = 0;
           }
           else if (srbExt->Xfer && Srb->DataTransferLength > srbExt->Xfer)
           {
              Srb->DataTransferLength = srbExt->Xfer;
              Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
           }
           if (srbExt && srbExt->Xfer && Srb->DataTransferLength > srbExt->Xfer) {
              Srb->DataTransferLength = srbExt->Xfer;
              Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
           }
           CompleteRequest(DeviceExtension, Srb);
        }
        return TRUE;
    }
    return FALSE;
}
#endif

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
        ULONG index;
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("ScsiRestartAdapter\n"));
        VirtIODeviceReset(adaptExt->pvdev);
        for (index = VIRTIO_SCSI_CONTROL_QUEUE; index < adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0; ++index) {
            StorPortWritePortUshort(DeviceExtension, (PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL), (USHORT)index);
            StorPortWritePortUlong(DeviceExtension, (PULONG)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN), (ULONG)0);
            adaptExt->vq[index] = NULL;
        }

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

    StorPortGetCurrentProcessorNumber(DeviceExtension, &srbExt->procNum);
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
    memcpy(cmd->req.cmd.cdb, cdb, min(VIRTIO_SCSI_CDB_SIZE, Srb->CdbLength));

    sgElement = 0;
    srbExt->sg[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->req.cmd, &fragLen);
    srbExt->sg[sgElement].length   = sizeof(cmd->req.cmd);
    sgElement++;

    sgList = StorPortGetScatterGatherList(DeviceExtension, Srb);
    if (sgList)
    {
        sgMaxElements = sgList->NumberOfElements;

        if((Srb->SrbFlags & SRB_FLAGS_DATA_OUT) == SRB_FLAGS_DATA_OUT) {
            for (i = 0; i < sgMaxElements; i++, sgElement++) {
                srbExt->sg[sgElement].physAddr = sgList->List[i].PhysicalAddress;
                srbExt->sg[sgElement].length = sgList->List[i].Length;
                srbExt->Xfer += sgList->List[i].Length;
            }
        }
    }
    srbExt->out = sgElement;
    srbExt->sg[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->resp.cmd, &fragLen);
    srbExt->sg[sgElement].length = sizeof(cmd->resp.cmd);
    sgElement++;
    if (sgList)
    {
        sgMaxElements = sgList->NumberOfElements;

        if((Srb->SrbFlags & SRB_FLAGS_DATA_OUT) != SRB_FLAGS_DATA_OUT) {
            for (i = 0; i < sgMaxElements; i++, sgElement++) {
                srbExt->sg[sgElement].physAddr = sgList->List[i].PhysicalAddress;
                srbExt->sg[sgElement].length = sgList->List[i].Length;
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
           if (!StorPortSetDeviceQueueDepth( DeviceExtension, Srb->PathId,
                                     Srb->TargetId, Srb->Lun,
                                     adaptExt->queue_depth)) {
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("StorPortSetDeviceQueueDepth(%p, %x) failed.\n",
                          DeviceExtension,
                          adaptExt->queue_depth));
           }
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
#if (NTDDI_VERSION > NTDDI_WIN7)
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
#else
    StorPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         ErrorCode,
                         UniqueId);
#endif
}

VOID
TransportReset(
    IN PVOID DeviceExtension,
    IN PVirtIOSCSIEvent evt
    )
{
    UCHAR TargetId = evt->lun[1];
    UCHAR Lun = (evt->lun[2] << 8) | evt->lun[3];

    switch (evt->reason)
    {
        case VIRTIO_SCSI_EVT_RESET_RESCAN:
           StorPortNotification( BusChangeDetected, DeviceExtension, 0);
           break;
        case VIRTIO_SCSI_EVT_RESET_REMOVED:
           StorPortNotification( BusChangeDetected, DeviceExtension, 0);
           break;
        default:
           RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("<-->Unsupport virtio scsi event reason 0x%x\n", evt->reason));
    }
}

VOID
ParamChange(
    IN PVOID DeviceExtension,
    IN PVirtIOSCSIEvent evt
    )
{
    UCHAR TargetId = evt->lun[1];
    UCHAR Lun = (evt->lun[2] << 8) | evt->lun[3];
    UCHAR AdditionalSenseCode = (UCHAR)(evt->reason & 255);
    UCHAR AdditionalSenseCodeQualifier = (UCHAR)(evt->reason >> 8);

    if (AdditionalSenseCode == SCSI_ADSENSE_PARAMETERS_CHANGED && 
       (AdditionalSenseCodeQualifier == SPC3_SCSI_SENSEQ_PARAMETERS_CHANGED || 
        AdditionalSenseCodeQualifier == SPC3_SCSI_SENSEQ_MODE_PARAMETERS_CHANGED || 
        AdditionalSenseCodeQualifier == SPC3_SCSI_SENSEQ_CAPACITY_DATA_HAS_CHANGED))
    {
        StorPortNotification( BusChangeDetected, DeviceExtension, 0);
    }
}
