/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: virtio_stor.c
 *
 * Author(s):
 *  Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This file contains viostor StorPort(ScsiPort) miniport driver
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "virtio_stor.h"
#include "virtio_stor_utils.h"
#include "virtio_stor_hw_helper.h"

BOOLEAN IsCrashDumpMode;

#if (NTDDI_VERSION > NTDDI_WIN7)
sp_DRIVER_INITIALIZE DriverEntry;
HW_INITIALIZE        VirtIoHwInitialize;
HW_STARTIO           VirtIoStartIo;
HW_FIND_ADAPTER      VirtIoFindAdapter;
HW_RESET_BUS         VirtIoResetBus;
HW_ADAPTER_CONTROL   VirtIoAdapterControl;
HW_INTERRUPT         VirtIoInterrupt;
HW_BUILDIO           VirtIoBuildIo;
HW_DPC_ROUTINE       CompleteDpcRoutine;
HW_MESSAGE_SIGNALED_INTERRUPT_ROUTINE VirtIoMSInterruptRoutine;
HW_PASSIVE_INITIALIZE_ROUTINE         VirtIoPassiveInitializeRoutine;
#endif

extern int vring_add_buf_stor(
    IN struct virtqueue *_vq,
    IN struct VirtIOBufferDescriptor sg[],
    IN unsigned int out,
    IN unsigned int in,
    IN PVOID data);

BOOLEAN
VirtIoHwInitialize(
    IN PVOID DeviceExtension
    );

#ifdef USE_STORPORT
BOOLEAN
VirtIoBuildIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

VOID
CompleteDpcRoutine(
    IN PSTOR_DPC  Dpc,
    IN PVOID Context,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    ) ;
#ifdef MSI_SUPPORTED
BOOLEAN
VirtIoMSInterruptRoutine (
    IN PVOID  DeviceExtension,
    IN ULONG  MessageID
    );
#endif
#endif

BOOLEAN
VirtIoStartIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

ULONG
VirtIoFindAdapter(
    IN PVOID DeviceExtension,
    IN PVOID HwContext,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again
    );

BOOLEAN
VirtIoResetBus(
    IN PVOID DeviceExtension,
    IN ULONG PathId
    );

SCSI_ADAPTER_CONTROL_STATUS
VirtIoAdapterControl(
    IN PVOID DeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters
    );

UCHAR
RhelScsiGetInquiryData(
    IN PVOID DeviceExtension,
    IN OUT PSCSI_REQUEST_BLOCK Srb
    );

UCHAR
RhelScsiGetModeSense(
    IN PVOID DeviceExtension,
    IN OUT PSCSI_REQUEST_BLOCK Srb
    );

UCHAR
RhelScsiGetCapacity(
    IN PVOID DeviceExtension,
    IN OUT PSCSI_REQUEST_BLOCK Srb
    );

UCHAR
RhelScsiReportLuns(
    IN PVOID DeviceExtension,
    IN OUT PSCSI_REQUEST_BLOCK Srb
    );

VOID
FORCEINLINE
CompleteSRB(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    );

VOID
FORCEINLINE
CompleteDPC(
    IN PVOID DeviceExtension,
    IN pblk_req vbr,
    IN ULONG  MessageID
    );

VOID
LogError(
    IN PVOID HwDeviceExtension,
    IN ULONG ErrorCode,
    IN ULONG UniqueId
    );


ULONG
DriverEntry(
    IN PVOID  DriverObject,
    IN PVOID  RegistryPath
    )
{

    HW_INITIALIZATION_DATA hwInitData;
    ULONG                  initResult;

#ifndef USE_STORPORT
    UCHAR venId[4]  = {'1', 'A', 'F', '4'};
    UCHAR devId[4]  = {'1', '0', '0', '1'};
#endif

    InitializeDebugPrints((PDRIVER_OBJECT)DriverObject, (PUNICODE_STRING)RegistryPath);

    RhelDbgPrint(TRACE_LEVEL_ERROR, ("Viostor driver started...built on %s %s\n", __DATE__, __TIME__));
    IsCrashDumpMode = FALSE;
    if (RegistryPath == NULL) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                     ("DriverEntry: Crash dump mode\n"));
        IsCrashDumpMode = TRUE;
    }

    memset(&hwInitData, 0, sizeof(HW_INITIALIZATION_DATA));

    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

    hwInitData.HwFindAdapter            = VirtIoFindAdapter;
    hwInitData.HwInitialize             = VirtIoHwInitialize;
    hwInitData.HwStartIo                = VirtIoStartIo;
    hwInitData.HwInterrupt              = VirtIoInterrupt;
    hwInitData.HwResetBus               = VirtIoResetBus;
    hwInitData.HwAdapterControl         = VirtIoAdapterControl;
#ifdef USE_STORPORT
    hwInitData.HwBuildIo                = VirtIoBuildIo;
#endif
    hwInitData.NeedPhysicalAddresses    = TRUE;
    hwInitData.TaggedQueuing            = TRUE;
    hwInitData.AutoRequestSense         = TRUE;
    hwInitData.MultipleRequestPerLu     = TRUE;

    hwInitData.DeviceExtensionSize      = sizeof(ADAPTER_EXTENSION);
    hwInitData.SrbExtensionSize         = sizeof(RHEL_SRB_EXTENSION);

    hwInitData.AdapterInterfaceType     = PCIBus;

#ifndef USE_STORPORT
    hwInitData.VendorIdLength           = 4;
    hwInitData.VendorId                 = venId;
    hwInitData.DeviceIdLength           = 4;
    hwInitData.DeviceId                 = devId;
#endif

    hwInitData.NumberOfAccessRanges     = 1;
#ifdef USE_STORPORT
    hwInitData.MapBuffers               = STOR_MAP_NON_READ_WRITE_BUFFERS;
#else
    hwInitData.MapBuffers               = TRUE;
#endif
    initResult = ScsiPortInitialize(DriverObject,
                                    RegistryPath,
                                    &hwInitData,
                                    NULL);

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 ("Initialize returned 0x%x\n", initResult));

    return initResult;

}

ULONG
VirtIoFindAdapter(
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
    ULONG_PTR          deviceBase;
    ULONG              allocationSize;
    ULONG              pageNum;

#ifdef MSI_SUPPORTED
    PPCI_COMMON_CONFIG pPciConf = NULL;
    UCHAR              pci_cfg_buf[256];
    ULONG              pci_cfg_len;
#endif

    UNREFERENCED_PARAMETER( HwContext );
    UNREFERENCED_PARAMETER( BusInformation );
    UNREFERENCED_PARAMETER( ArgumentString );
    UNREFERENCED_PARAMETER( Again );

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    adaptExt->dump_mode  = IsCrashDumpMode;

    ConfigInfo->Master                 = TRUE;
    ConfigInfo->ScatterGather          = TRUE;
    ConfigInfo->DmaWidth               = Width32Bits;
    ConfigInfo->Dma32BitAddresses      = TRUE;
    ConfigInfo->Dma64BitAddresses      = TRUE;
    ConfigInfo->WmiDataProvider        = FALSE;
    ConfigInfo->AlignmentMask          = 0x3;
#ifdef USE_STORPORT
    ConfigInfo->MapBuffers             = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->SynchronizationModel   = StorSynchronizeFullDuplex;
#ifdef MSI_SUPPORTED
    ConfigInfo->HwMSInterruptRoutine   = VirtIoMSInterruptRoutine;
    ConfigInfo->InterruptSynchronizationMode=InterruptSynchronizePerMessage;
#endif
#else
    ConfigInfo->MapBuffers             = TRUE;
#endif

    accessRange = &(*ConfigInfo->AccessRanges)[0];

    ASSERT (FALSE == accessRange->RangeInMemory) ;

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Port  Resource [%08I64X-%08I64X]\n",
                accessRange->RangeStart.QuadPart,
                accessRange->RangeStart.QuadPart +
                accessRange->RangeLength));

    if ( accessRange->RangeLength < IO_PORT_LENGTH) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Wrong access range %x bytes\n", accessRange->RangeLength));
        return SP_RETURN_NOT_FOUND;
    }

#ifndef USE_STORPORT
    if (!ScsiPortValidateRange(DeviceExtension,
                                           ConfigInfo->AdapterInterfaceType,
                                           ConfigInfo->SystemIoBusNumber,
                                           accessRange->RangeStart,
                                           accessRange->RangeLength,
                                           (BOOLEAN)!accessRange->RangeInMemory)) {

        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Range validation failed %x for %x bytes\n",
                   (*ConfigInfo->AccessRanges)[0].RangeStart.LowPart,
                   (*ConfigInfo->AccessRanges)[0].RangeLength));

        return SP_RETURN_ERROR;
    }

#endif

    ConfigInfo->NumberOfBuses               = 1;
    ConfigInfo->MaximumNumberOfTargets      = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;

    deviceBase = (ULONG_PTR)ScsiPortGetDeviceBase(DeviceExtension,
                                           ConfigInfo->AdapterInterfaceType,
                                           ConfigInfo->SystemIoBusNumber,
                                           accessRange->RangeStart,
                                           accessRange->RangeLength,
                                           (BOOLEAN)!accessRange->RangeInMemory);

    if (deviceBase == (ULONG_PTR)NULL) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Couldn't map %x for %x bytes\n",
                   (*ConfigInfo->AccessRanges)[0].RangeStart.LowPart,
                   (*ConfigInfo->AccessRanges)[0].RangeLength));
        return SP_RETURN_ERROR;
    }

    VirtIODeviceInitialize(&adaptExt->vdev, deviceBase, sizeof(adaptExt->vdev));
    VirtIODeviceAddStatus(&adaptExt->vdev, VIRTIO_CONFIG_S_DRIVER);
    adaptExt->msix_enabled = FALSE;

#ifdef MSI_SUPPORTED
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
        PPCI_COMMON_HEADER   pPciComHeader;
        pPciComHeader = (PPCI_COMMON_HEADER)pci_cfg_buf;
        pPciConf = (PPCI_COMMON_CONFIG)pci_cfg_buf;
        if ( (pPciComHeader->Status & PCI_STATUS_CAPABILITIES_LIST) == 0)
        {
           RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("NO CAPABILITIES_LIST\n"));
        }
        else
        {
           if ( (pPciComHeader->HeaderType & (~PCI_MULTIFUNCTION)) == PCI_DEVICE_TYPE )
           {
              CapOffset = pPciComHeader->u.type0.CapabilitiesPtr;
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
                    break;
                 }
                 else
                 {
                    CapOffset = pMsixCapOffset->Header.Next;
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("CapabilityID = %x, Next CapOffset = %x\n", pMsixCapOffset->Header.CapabilityID, CapOffset));
                 }
              }
              VirtIODeviceSetMSIXUsed(&adaptExt->vdev, adaptExt->msix_enabled);
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

    VirtIODeviceReset(&adaptExt->vdev);
    VirtIODeviceAddStatus(&adaptExt->vdev, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    WriteVirtIODeviceWord(adaptExt->vdev.addr + VIRTIO_PCI_QUEUE_SEL, (USHORT)0);
    if (adaptExt->dump_mode) {
        WriteVirtIODeviceWord(adaptExt->vdev.addr + VIRTIO_PCI_QUEUE_PFN, (USHORT)0);
    }

    adaptExt->features = ReadVirtIODeviceRegister(adaptExt->vdev.addr + VIRTIO_PCI_HOST_FEATURES);
    ConfigInfo->CachesData = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_WCACHE) ? TRUE : FALSE;
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_WCACHE = %d\n", ConfigInfo->CachesData));

    VirtIODeviceQueryQueueAllocation(&adaptExt->vdev, 0, &pageNum, &allocationSize);

    if(adaptExt->dump_mode) {
        ConfigInfo->NumberOfPhysicalBreaks = 8;
    } else {
        ConfigInfo->NumberOfPhysicalBreaks = MAX_PHYS_SEGMENTS + 1;
    }

    ConfigInfo->MaximumTransferLength = 0x00FFFFFF;
    adaptExt->queue_depth = pageNum / ConfigInfo->NumberOfPhysicalBreaks - 1;

#if (INDIRECT_SUPPORTED)
    if(!adaptExt->dump_mode) {
        adaptExt->indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);
    }
    if(adaptExt->indirect) {
        adaptExt->queue_depth = pageNum;
    }
#else
    adaptExt->indirect = 0;
#endif
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("breaks_number = %x  queue_depth = %x\n",
                ConfigInfo->NumberOfPhysicalBreaks,
                adaptExt->queue_depth));

    adaptExt->uncachedExtensionVa = ScsiPortGetUncachedExtension(DeviceExtension, ConfigInfo, allocationSize);
    if (!adaptExt->uncachedExtensionVa) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Couldn't get uncached extension\n"));
        return SP_RETURN_ERROR;
    }

    InitializeListHead(&adaptExt->list_head);
#ifdef USE_STORPORT
    InitializeListHead(&adaptExt->complete_list);
#endif
    return SP_RETURN_FOUND;
}

#ifdef USE_STORPORT
BOOLEAN
VirtIoPassiveInitializeRoutine (
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    StorPortInitializeDpc(DeviceExtension,
                    &adaptExt->completion_dpc,
                    CompleteDpcRoutine);
    adaptExt->dpc_ok = TRUE;
    return TRUE;
}
#endif


static struct virtqueue *FindVirtualQueue(PADAPTER_EXTENSION adaptExt, ULONG index, ULONG vector)
{
    struct virtqueue *vq = NULL;
    if (adaptExt->uncachedExtensionVa)
    {
        ULONG len;
        BOOLEAN useEventIndex = CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX);
        PHYSICAL_ADDRESS pa = ScsiPortGetPhysicalAddress(adaptExt, NULL, adaptExt->uncachedExtensionVa, &len);
        if (pa.QuadPart)
        {
            vq = VirtIODevicePrepareQueue(&adaptExt->vdev, index, pa, adaptExt->uncachedExtensionVa, len, NULL, useEventIndex);
        }
    }

    if (!vq) return NULL;

    if (vector)
    {
        unsigned res;
        ScsiPortWritePortUshort((PUSHORT)(adaptExt->vdev.addr + VIRTIO_MSI_QUEUE_VECTOR),(USHORT)vector);
        res = ScsiPortReadPortUshort((PUSHORT)(adaptExt->vdev.addr + VIRTIO_MSI_QUEUE_VECTOR));
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> VIRTIO_MSI_QUEUE_VECTOR vector = %d, res = 0x%x\n", __FUNCTION__, vector, res));
        if(res == VIRTIO_MSI_NO_VECTOR)
        {
            VirtIODeviceDeleteQueue(vq, NULL);
            vq = NULL;
            RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> Cannot create vq vector\n", __FUNCTION__));
            return NULL;
        }
        ScsiPortWritePortUshort((PUSHORT)(adaptExt->vdev.addr + VIRTIO_MSI_CONFIG_VECTOR),(USHORT)0);
        res = ScsiPortReadPortUshort((PUSHORT)(adaptExt->vdev.addr + VIRTIO_MSI_CONFIG_VECTOR));
        if (res != 0)
        {
            RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s>> Cannot set config vector\n", __FUNCTION__));
        }
    }
    return vq;
}

BOOLEAN
VirtIoHwInitialize(
    IN PVOID DeviceExtension
    )
{

    PADAPTER_EXTENSION adaptExt;
    BOOLEAN            ret = FALSE;
    ULONG              guestFeatures = 0;

#ifdef MSI_SUPPORTED
    MESSAGE_INTERRUPT_INFORMATION msi_info;
#endif

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s (%d)\n", __FUNCTION__, KeGetCurrentIrql()));

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    adaptExt->msix_vectors = 0;

    if (CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX)) {
        guestFeatures |= (1ul << VIRTIO_RING_F_EVENT_IDX);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_WCACHE)) {
        guestFeatures |= (1ul << VIRTIO_BLK_F_WCACHE);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BARRIER)) {
        guestFeatures |= (1ul << VIRTIO_BLK_F_BARRIER);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
        guestFeatures |= (1ul << VIRTIO_BLK_F_RO);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SIZE_MAX)) {
        guestFeatures |= (1ul << VIRTIO_BLK_F_SIZE_MAX);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SEG_MAX)) {
        guestFeatures |= (1ul << VIRTIO_BLK_F_SEG_MAX);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BLK_SIZE)) {
        guestFeatures |= (1ul << VIRTIO_BLK_F_BLK_SIZE);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_GEOMETRY)) {
        guestFeatures |= (1ul << VIRTIO_BLK_F_GEOMETRY);
    }

    ScsiPortWritePortUlong((PULONG)(adaptExt->vdev.addr + VIRTIO_PCI_GUEST_FEATURES), guestFeatures);

#ifdef MSI_SUPPORTED
    while(StorPortGetMSIInfo(DeviceExtension, adaptExt->msix_vectors, &msi_info) == STOR_STATUS_SUCCESS) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageId = %x\n", msi_info.MessageId));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageData = %x\n", msi_info.MessageData));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("InterruptVector = %x\n", msi_info.InterruptVector));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("InterruptLevel = %x\n", msi_info.InterruptLevel));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("InterruptMode = %s\n", msi_info.InterruptMode == LevelSensitive ? "LevelSensitive" : "Latched"));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("MessageAddress = %p\n\n", msi_info.MessageAddress));
        ++adaptExt->msix_vectors;
    }

    if(!adaptExt->dump_mode && (adaptExt->msix_vectors > 1)) {
        adaptExt->vq = FindVirtualQueue(adaptExt, 0, adaptExt->msix_vectors - 1);
    }
#endif

    if(!adaptExt->vq) {
        adaptExt->vq = FindVirtualQueue(adaptExt, 0, 0);
    }
    if (!adaptExt->vq) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find snd virtual queue\n"));
        VirtIODeviceAddStatus(&adaptExt->vdev, VIRTIO_CONFIG_S_FAILED);
        return ret;
    }

    RhelGetDiskGeometry(DeviceExtension);

    memset(&adaptExt->inquiry_data, 0, sizeof(INQUIRYDATA));

    adaptExt->inquiry_data.ANSIVersion = 4;
    adaptExt->inquiry_data.ResponseDataFormat = 2;
    adaptExt->inquiry_data.CommandQueue = 1;
    adaptExt->inquiry_data.DeviceType   = DIRECT_ACCESS_DEVICE;
    adaptExt->inquiry_data.Wide32Bit    = 1;
    adaptExt->inquiry_data.AdditionalLength = 91;
    ScsiPortMoveMemory(&adaptExt->inquiry_data.VendorId, "Red Hat ", sizeof("Red Hat "));
    ScsiPortMoveMemory(&adaptExt->inquiry_data.ProductId, "VirtIO", sizeof("VirtIO"));
    ScsiPortMoveMemory(&adaptExt->inquiry_data.ProductRevisionLevel, "0001", sizeof("0001"));
    ScsiPortMoveMemory(&adaptExt->inquiry_data.VendorSpecific, "0001", sizeof("0001"));

    if(!adaptExt->dump_mode && !adaptExt->sn_ok)
    {
        RhelGetSerialNumber(DeviceExtension);
    }

    ret = TRUE;

#ifdef USE_STORPORT
    if(!adaptExt->dump_mode && !adaptExt->dpc_ok)
    {
        ret = StorPortEnablePassiveInitialization(DeviceExtension, VirtIoPassiveInitializeRoutine);
    }
#endif

    if (ret) {
        VirtIODeviceAddStatus(&adaptExt->vdev, VIRTIO_CONFIG_S_DRIVER_OK);
    } else {
        VirtIODeviceAddStatus(&adaptExt->vdev, VIRTIO_CONFIG_S_FAILED);
    }

    return ret;
}

BOOLEAN
VirtIoStartIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PCDB cdb = (PCDB)&Srb->Cdb[0];

    PADAPTER_EXTENSION adaptExt;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    switch (Srb->Function) {
        case SRB_FUNCTION_EXECUTE_SCSI:
        case SRB_FUNCTION_IO_CONTROL: {
            break;
        }
        case SRB_FUNCTION_PNP:
        case SRB_FUNCTION_POWER:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_LOGICAL_UNIT: {
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            CompleteSRB(DeviceExtension, Srb);
            return TRUE;
        }
        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_SHUTDOWN: {
            Srb->SrbStatus = SRB_STATUS_PENDING;
            Srb->ScsiStatus = SCSISTAT_GOOD;
            if (!RhelDoFlush(DeviceExtension, Srb, TRUE)) {
                Srb->SrbStatus = SRB_STATUS_ERROR;
                CompleteSRB(DeviceExtension, Srb);
            }
            return TRUE;
        }

        default: {
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            CompleteSRB(DeviceExtension, Srb);
            return TRUE;
        }
    }

    switch (cdb->CDB6GENERIC.OperationCode) {
        case SCSIOP_MODE_SENSE: {
            Srb->SrbStatus = RhelScsiGetModeSense(DeviceExtension, Srb);
            CompleteSRB(DeviceExtension, Srb);
            return TRUE;
        }
        case SCSIOP_INQUIRY: {
            Srb->SrbStatus = RhelScsiGetInquiryData(DeviceExtension, Srb);
            CompleteSRB(DeviceExtension, Srb);
            return TRUE;
        }

        case SCSIOP_READ_CAPACITY16:
        case SCSIOP_READ_CAPACITY: {
            Srb->SrbStatus = RhelScsiGetCapacity(DeviceExtension, Srb);
            CompleteSRB(DeviceExtension, Srb);
            return TRUE;
        }
        case SCSIOP_WRITE:
        case SCSIOP_WRITE16: {
            if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
                PSENSE_DATA senseBuffer = (PSENSE_DATA) Srb->SenseInfoBuffer;
                Srb->SrbStatus = SRB_STATUS_ERROR;
                Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
                senseBuffer->SenseKey = SCSI_SENSE_DATA_PROTECT;
                senseBuffer->AdditionalSenseCode = SCSI_ADWRITE_PROTECT;
                CompleteSRB(DeviceExtension, Srb);
                return TRUE;
            }
        }
        case SCSIOP_READ:
        case SCSIOP_READ16: {
            Srb->SrbStatus = SRB_STATUS_PENDING;
            if(!RhelDoReadWrite(DeviceExtension, Srb)) {
                Srb->SrbStatus = SRB_STATUS_BUSY;
                CompleteSRB(DeviceExtension, Srb);
            }
            return TRUE;
        }
        case SCSIOP_START_STOP_UNIT: {
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            CompleteSRB(DeviceExtension, Srb);
            return TRUE;
        }
        case SCSIOP_REQUEST_SENSE:
        case SCSIOP_TEST_UNIT_READY:
        case SCSIOP_RESERVE_UNIT:
        case SCSIOP_RESERVE_UNIT10:
        case SCSIOP_RELEASE_UNIT:
        case SCSIOP_RELEASE_UNIT10:
        case SCSIOP_VERIFY:
        case SCSIOP_VERIFY16:
        case SCSIOP_MEDIUM_REMOVAL: {
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            Srb->ScsiStatus = SCSISTAT_GOOD;
            CompleteSRB(DeviceExtension, Srb);
            return TRUE;
        }
        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16: {
            Srb->SrbStatus = SRB_STATUS_PENDING;
            Srb->ScsiStatus = SCSISTAT_GOOD;
            if (!RhelDoFlush(DeviceExtension, Srb, TRUE)) {
                Srb->SrbStatus = SRB_STATUS_ERROR;
                CompleteSRB(DeviceExtension, Srb);
            }
            return TRUE;
        }
        default: {
            break;
        }
    }

    if (cdb->CDB12.OperationCode == SCSIOP_REPORT_LUNS) {
        Srb->SrbStatus = RhelScsiReportLuns(DeviceExtension, Srb);
        CompleteSRB(DeviceExtension, Srb);
        return TRUE;

    }

    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
    CompleteSRB(DeviceExtension, Srb);
    return TRUE;
}


BOOLEAN
VirtIoInterrupt(
    IN PVOID DeviceExtension
    )
{
    pblk_req            vbr;
    unsigned int        len;
    PADAPTER_EXTENSION  adaptExt;
    BOOLEAN             isInterruptServiced = FALSE;
    PSCSI_REQUEST_BLOCK Srb;
    ULONG               intReason = 0;
    PRHEL_SRB_EXTENSION srbExt;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s (%d)\n", __FUNCTION__, KeGetCurrentIrql()));
    intReason = VirtIODeviceISR((VirtIODevice*)DeviceExtension);
    if ( intReason == 1 || adaptExt->dump_mode ) {
        isInterruptServiced = TRUE;
        while((vbr = (pblk_req)virtqueue_get_buf(adaptExt->vq, &len)) != NULL) {
           Srb = (PSCSI_REQUEST_BLOCK)vbr->req;
           if (Srb) {
              switch (vbr->status) {
              case VIRTIO_BLK_S_OK:
                 Srb->SrbStatus = SRB_STATUS_SUCCESS;
                 break;
              case VIRTIO_BLK_S_UNSUPP:
                 Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                 break;
              default:
                 Srb->SrbStatus = SRB_STATUS_ERROR;
                 RhelDbgPrint(TRACE_LEVEL_ERROR, ("SRB_STATUS_ERROR\n"));
                 break;
              }
           }
           if (vbr->out_hdr.type == VIRTIO_BLK_T_FLUSH) {
              CompleteSRB(DeviceExtension, Srb);
           } else if (vbr->out_hdr.type == VIRTIO_BLK_T_GET_ID) {
              adaptExt->sn_ok = TRUE;
           } else if (Srb) {
              srbExt = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
              if (srbExt->fua) {
                  RemoveEntryList(&vbr->list_entry);
                  Srb->SrbStatus = SRB_STATUS_PENDING;
                  Srb->ScsiStatus = SCSISTAT_GOOD;
                  if (!RhelDoFlush(DeviceExtension, Srb, FALSE)) {
                      Srb->SrbStatus = SRB_STATUS_ERROR;
                      CompleteSRB(DeviceExtension, Srb);
                  } else {
                      srbExt->fua = FALSE;
                  }
              } else {
                  CompleteDPC(DeviceExtension, vbr, 0);
              }
           }
        }
    } else if (intReason == 3) {
        RhelGetDiskGeometry(DeviceExtension);
        isInterruptServiced = TRUE;
    }
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s isInterruptServiced = %d\n", __FUNCTION__, isInterruptServiced));
    return isInterruptServiced;
}

BOOLEAN
VirtIoResetBus(
    IN PVOID DeviceExtension,
    IN ULONG PathId
    )
{
    UNREFERENCED_PARAMETER( DeviceExtension );
    UNREFERENCED_PARAMETER( PathId );
    return TRUE;
}

SCSI_ADAPTER_CONTROL_STATUS
VirtIoAdapterControl(
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
        RhelShutDown(DeviceExtension);
        status = ScsiAdapterControlSuccess;
        break;
    }
    case ScsiRestartAdapter: {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("ScsiRestartAdapter\n"));
        VirtIODeviceReset(&adaptExt->vdev);
        WriteVirtIODeviceWord(adaptExt->vdev.addr + VIRTIO_PCI_QUEUE_SEL, (USHORT)0);
        WriteVirtIODeviceRegister(adaptExt->vdev.addr + VIRTIO_PCI_QUEUE_PFN,(USHORT)0);
        adaptExt->vq = NULL;

        if (!VirtIoHwInitialize(DeviceExtension))
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

    return status;
}

#ifdef USE_STORPORT
BOOLEAN
VirtIoBuildIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PCDB                  cdb;
    ULONG                 i;
    ULONG                 dummy;
    ULONG                 sgElement;
    ULONG                 sgMaxElements;
    PADAPTER_EXTENSION    adaptExt;
    PRHEL_SRB_EXTENSION   srbExt;
    PSTOR_SCATTER_GATHER_LIST sgList;
    ULONGLONG             lba;
    ULONG                 blocks;

    cdb      = (PCDB)&Srb->Cdb[0];
    srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if(Srb->PathId || Srb->TargetId || Srb->Lun) {
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        ScsiPortNotification(RequestComplete,
                             DeviceExtension,
                             Srb);
        return FALSE;
    }

    switch (cdb->CDB6GENERIC.OperationCode) {
        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_WRITE_VERIFY:
        case SCSIOP_READ6:
        case SCSIOP_WRITE6:
        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
        case SCSIOP_WRITE_VERIFY12:
        case SCSIOP_READ16:
        case SCSIOP_WRITE16:
        case SCSIOP_WRITE_VERIFY16: {
            break;
        }
        default: {
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            return TRUE;
        }
    }

    lba = RhelGetLba(DeviceExtension, cdb);
    blocks = (Srb->DataTransferLength + adaptExt->info.blk_size - 1) / adaptExt->info.blk_size;
    if (lba > adaptExt->lastLBA) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("SRB_STATUS_BAD_SRB_BLOCK_LENGTH lba = %llu lastLBA= %llu\n", lba, adaptExt->lastLBA));
        Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
        CompleteSRB(DeviceExtension, Srb);
        return FALSE;
    }
    if ((lba + blocks) > adaptExt->lastLBA) {
        blocks = (ULONG)(adaptExt->lastLBA + 1 - lba);
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("lba = %llu lastLBA= %llu blocks = %lu\n", lba, adaptExt->lastLBA, blocks));
        Srb->DataTransferLength = (ULONG)(blocks * adaptExt->info.blk_size);
    }

    sgList = StorPortGetScatterGatherList(DeviceExtension, Srb);
    sgMaxElements = min((MAX_PHYS_SEGMENTS + 1), sgList->NumberOfElements);
    srbExt->Xfer = 0;
    for (i = 0, sgElement = 1; i < sgMaxElements; i++, sgElement++) {
        srbExt->vbr.sg[sgElement].physAddr = sgList->List[i].PhysicalAddress;
        srbExt->vbr.sg[sgElement].length   = sgList->List[i].Length;
        srbExt->Xfer += sgList->List[i].Length;
    }

    srbExt->vbr.out_hdr.sector = lba;
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req            = (PVOID)Srb;
    srbExt->fua                = (cdb->CDB10.ForceUnitAccess == 1);

    if (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) {
        srbExt->vbr.out_hdr.type = VIRTIO_BLK_T_OUT;
        srbExt->out = sgElement;
        srbExt->in = 1;
    } else {
        srbExt->vbr.out_hdr.type = VIRTIO_BLK_T_IN;
        srbExt->out = 1;
        srbExt->in = sgElement;
    }

    srbExt->vbr.sg[0].physAddr = ScsiPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.out_hdr, &dummy);
    srbExt->vbr.sg[0].length = sizeof(srbExt->vbr.out_hdr);

    srbExt->vbr.sg[sgElement].physAddr = ScsiPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &dummy);
    srbExt->vbr.sg[sgElement].length = sizeof(srbExt->vbr.status);

    return TRUE;
}

#ifdef MSI_SUPPORTED
BOOLEAN
VirtIoMSInterruptRoutine (
    IN PVOID  DeviceExtension,
    IN ULONG  MessageID
    )
{
    pblk_req            vbr;
    unsigned int        len;
    PADAPTER_EXTENSION  adaptExt;
    PSCSI_REQUEST_BLOCK Srb;
    BOOLEAN             isInterruptServiced = FALSE;
    PRHEL_SRB_EXTENSION srbExt;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 ("<--->%s : MessageID 0x%x\n", __FUNCTION__, MessageID));

    if (MessageID == 0) {
        RhelGetDiskGeometry(DeviceExtension);
        return TRUE;
    }

    while((vbr = (pblk_req)virtqueue_get_buf(adaptExt->vq, &len)) != NULL) {
        Srb = (PSCSI_REQUEST_BLOCK)vbr->req;
        if (Srb) {
           switch (vbr->status) {
           case VIRTIO_BLK_S_OK:
              Srb->SrbStatus = SRB_STATUS_SUCCESS;
              break;
           case VIRTIO_BLK_S_UNSUPP:
              Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
              break;
           default:
              Srb->SrbStatus = SRB_STATUS_ERROR;
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("SRB_STATUS_ERROR\n"));
              break;
           }
        }
        if (vbr->out_hdr.type == VIRTIO_BLK_T_FLUSH) {
            CompleteSRB(DeviceExtension, Srb);
        } else if (vbr->out_hdr.type == VIRTIO_BLK_T_GET_ID) {
            adaptExt->sn_ok = TRUE;
        } else if (Srb) {
            srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
            if (srbExt->fua == TRUE) {
               RemoveEntryList(&vbr->list_entry);
               Srb->SrbStatus = SRB_STATUS_PENDING;
               Srb->ScsiStatus = SCSISTAT_GOOD;
               if (!RhelDoFlush(DeviceExtension, Srb, FALSE)) {
                    Srb->SrbStatus = SRB_STATUS_ERROR;
                    CompleteSRB(DeviceExtension, Srb);
                } else {
                    srbExt->fua = FALSE;
                }
            } else {
                CompleteDPC(DeviceExtension, vbr, MessageID);
            }
        }
        isInterruptServiced = TRUE;
    }
    return isInterruptServiced;
}
#endif

#endif

UCHAR
RhelScsiGetInquiryData(
    IN PVOID DeviceExtension,
    IN OUT PSCSI_REQUEST_BLOCK Srb
    )
{

    PINQUIRYDATA InquiryData;
    ULONG dataLen;
    UCHAR SrbStatus = SRB_STATUS_INVALID_LUN;
    PCDB cdb = (PCDB)&Srb->Cdb[0];
    PADAPTER_EXTENSION adaptExt;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    InquiryData = (PINQUIRYDATA)Srb->DataBuffer;
    dataLen = Srb->DataTransferLength;

    SrbStatus = SRB_STATUS_SUCCESS;
    if((cdb->CDB6INQUIRY3.PageCode != VPD_SUPPORTED_PAGES) &&
       (cdb->CDB6INQUIRY3.EnableVitalProductData == 0)) {
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_SUPPORTED_PAGES) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)) {

        PVPD_SUPPORTED_PAGES_PAGE SupportPages;
        SupportPages = (PVPD_SUPPORTED_PAGES_PAGE)Srb->DataBuffer;
        memset(SupportPages, 0, sizeof(VPD_SUPPORTED_PAGES_PAGE));
        SupportPages->PageCode = VPD_SUPPORTED_PAGES;
        SupportPages->PageLength = 3;
        SupportPages->SupportedPageList[0] = VPD_SUPPORTED_PAGES;
        SupportPages->SupportedPageList[1] = VPD_SERIAL_NUMBER;
        SupportPages->SupportedPageList[2] = VPD_DEVICE_IDENTIFIERS;
        Srb->DataTransferLength = sizeof(VPD_SUPPORTED_PAGES_PAGE) + SupportPages->PageLength;
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_SERIAL_NUMBER) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)) {

        PVPD_SERIAL_NUMBER_PAGE SerialPage;
        SerialPage = (PVPD_SERIAL_NUMBER_PAGE)Srb->DataBuffer;
        SerialPage->PageCode = VPD_SERIAL_NUMBER;
        if (!adaptExt->sn_ok) {
           SerialPage->PageLength = 1;
           SerialPage->SerialNumber[0] = '0';
        } else {
           SerialPage->PageLength = BLOCK_SERIAL_STRLEN;
           ScsiPortMoveMemory(&SerialPage->SerialNumber, &adaptExt->sn, BLOCK_SERIAL_STRLEN);
        }
        Srb->DataTransferLength = sizeof(VPD_SERIAL_NUMBER_PAGE) + SerialPage->PageLength;
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_DEVICE_IDENTIFIERS) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)) {

        PVPD_IDENTIFICATION_PAGE IdentificationPage;
        PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr;
        IdentificationPage = (PVPD_IDENTIFICATION_PAGE)Srb->DataBuffer;
        memset(IdentificationPage, 0, sizeof(VPD_IDENTIFICATION_PAGE));
        IdentificationPage->PageCode = VPD_DEVICE_IDENTIFIERS;
        IdentificationPage->PageLength = sizeof(VPD_IDENTIFICATION_DESCRIPTOR) + 8;

        IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)IdentificationPage->Descriptors;
        memset(IdentificationDescr, 0, sizeof(VPD_IDENTIFICATION_DESCRIPTOR));
        IdentificationDescr->CodeSet = VpdCodeSetBinary;
        IdentificationDescr->IdentifierType = VpdIdentifierTypeEUI64;
        IdentificationDescr->IdentifierLength = 8;
        IdentificationDescr->Identifier[0] = '1';
        IdentificationDescr->Identifier[1] = 'A';
        IdentificationDescr->Identifier[2] = 'F';
        IdentificationDescr->Identifier[3] = '4';
        IdentificationDescr->Identifier[4] = '1';
        IdentificationDescr->Identifier[5] = '0';
        IdentificationDescr->Identifier[6] = '0';
        IdentificationDescr->Identifier[7] = '1';

        Srb->DataTransferLength = sizeof(VPD_IDENTIFICATION_PAGE) +
                                 IdentificationPage->PageLength;

    }
    else if (dataLen > sizeof(INQUIRYDATA)) {
        ScsiPortMoveMemory(InquiryData, &adaptExt->inquiry_data, sizeof(INQUIRYDATA));
        Srb->DataTransferLength = sizeof(INQUIRYDATA);
    } else {
        ScsiPortMoveMemory(InquiryData, &adaptExt->inquiry_data, dataLen);
        Srb->DataTransferLength = dataLen;
    }

    return SrbStatus;
}

UCHAR
RhelScsiReportLuns(
    IN PVOID DeviceExtension,
    IN OUT PSCSI_REQUEST_BLOCK Srb
    )
{
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    PUCHAR data = (PUCHAR)Srb->DataBuffer;

    UNREFERENCED_PARAMETER( DeviceExtension );

    data[3]=8;
    Srb->ScsiStatus = SCSISTAT_GOOD;
    Srb->SrbStatus = SrbStatus;
    Srb->DataTransferLength = 16;
    return SrbStatus;
}

UCHAR
RhelScsiGetModeSense(
    IN PVOID DeviceExtension,
    IN OUT PSCSI_REQUEST_BLOCK Srb
    )
{
    ULONG ModeSenseDataLen;
    UCHAR SrbStatus = SRB_STATUS_INVALID_LUN;
    PCDB cdb = (PCDB)&Srb->Cdb[0];
    PMODE_PARAMETER_HEADER header;
    PMODE_CACHING_PAGE cachePage;
    PMODE_PARAMETER_BLOCK blockDescriptor;
    PADAPTER_EXTENSION adaptExt;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    ModeSenseDataLen = Srb->DataTransferLength;

    SrbStatus = SRB_STATUS_INVALID_REQUEST;

    if ((cdb->MODE_SENSE.PageCode == MODE_PAGE_CACHING) ||
        (cdb->MODE_SENSE.PageCode == MODE_SENSE_RETURN_ALL)) {

        if (sizeof(MODE_PARAMETER_HEADER) > ModeSenseDataLen)
        {
           SrbStatus = SRB_STATUS_ERROR;
           return SrbStatus;
        }

        header = (PMODE_PARAMETER_HEADER)Srb->DataBuffer;

        memset(header, 0, sizeof(MODE_PARAMETER_HEADER));
        header->DeviceSpecificParameter = MODE_DSP_FUA_SUPPORTED;

        if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
           header->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;
        }

        ModeSenseDataLen -= sizeof(MODE_PARAMETER_HEADER);
        if (ModeSenseDataLen >= sizeof(MODE_CACHING_PAGE)) {

           header->ModeDataLength = sizeof(MODE_CACHING_PAGE) + 3;
           cachePage = (PMODE_CACHING_PAGE)header;
           cachePage = (PMODE_CACHING_PAGE)((unsigned char *)(cachePage) + (ULONG)sizeof(MODE_PARAMETER_HEADER));
           memset(cachePage, 0, sizeof(MODE_CACHING_PAGE));
           cachePage->PageCode = MODE_PAGE_CACHING;
           cachePage->PageLength = 10;
           cachePage->WriteCacheEnable = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_WCACHE) ? 1 : 0;

           Srb->DataTransferLength = sizeof(MODE_PARAMETER_HEADER) +
                                     sizeof(MODE_CACHING_PAGE);

        } else {
           Srb->DataTransferLength = sizeof(MODE_PARAMETER_HEADER);
        }

        SrbStatus = SRB_STATUS_SUCCESS;

    }
    else if (cdb->MODE_SENSE.PageCode == MODE_PAGE_VENDOR_SPECIFIC) {

        if (sizeof(MODE_PARAMETER_HEADER) > ModeSenseDataLen) {
           SrbStatus = SRB_STATUS_ERROR;
           return SrbStatus;
        }

        header = (PMODE_PARAMETER_HEADER)Srb->DataBuffer;
        memset(header, 0, sizeof(MODE_PARAMETER_HEADER));
        header->DeviceSpecificParameter = MODE_DSP_FUA_SUPPORTED;

        if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
           header->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;
        }

        ModeSenseDataLen -= sizeof(MODE_PARAMETER_HEADER);
        if (ModeSenseDataLen >= sizeof(MODE_PARAMETER_BLOCK)) {

           header->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);
           blockDescriptor = (PMODE_PARAMETER_BLOCK)header;
           blockDescriptor = (PMODE_PARAMETER_BLOCK)((unsigned char *)(blockDescriptor) + (ULONG)sizeof(MODE_PARAMETER_HEADER));

           memset(blockDescriptor, 0, sizeof(MODE_PARAMETER_BLOCK));

           Srb->DataTransferLength = sizeof(MODE_PARAMETER_HEADER) +
                                     sizeof(MODE_PARAMETER_BLOCK);
        } else {
           Srb->DataTransferLength = sizeof(MODE_PARAMETER_HEADER);
        }
        SrbStatus = SRB_STATUS_SUCCESS;

    } else {
        SrbStatus = SRB_STATUS_INVALID_REQUEST;
    }

    return SrbStatus;
}

UCHAR
RhelScsiGetCapacity(
    IN PVOID DeviceExtension,
    IN OUT PSCSI_REQUEST_BLOCK Srb
    )
{
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    PREAD_CAPACITY_DATA readCap;
    PREAD_CAPACITY_DATA_EX readCapEx;
    u64 lastLBA;
    EIGHT_BYTE lba;
    u64 blocksize;
    BOOLEAN depthSet;
    PADAPTER_EXTENSION adaptExt= (PADAPTER_EXTENSION)DeviceExtension;
    PCDB cdb = (PCDB)&Srb->Cdb[0];
    UCHAR  PMI = 0;
#ifdef USE_STORPORT
    depthSet = StorPortSetDeviceQueueDepth(DeviceExtension,
                                           Srb->PathId,
                                           Srb->TargetId,
                                           Srb->Lun,
                                           adaptExt->queue_depth);
    ASSERT(depthSet);
#endif

    readCap = (PREAD_CAPACITY_DATA)Srb->DataBuffer;
    readCapEx = (PREAD_CAPACITY_DATA_EX)Srb->DataBuffer;

    lba.AsULongLong = 0;
    if (cdb->CDB6GENERIC.OperationCode == SCSIOP_READ_CAPACITY16 ){
         PMI = cdb->READ_CAPACITY16.PMI & 1;
         REVERSE_BYTES_QUAD(&lba, &cdb->READ_CAPACITY16.LogicalBlock[0]);
    }

    if (!PMI && lba.AsULongLong) {

        PSENSE_DATA senseBuffer = (PSENSE_DATA) Srb->SenseInfoBuffer;
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        senseBuffer->SenseKey = SCSI_SENSE_ILLEGAL_REQUEST;
        senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_INVALID_CDB;
        return SrbStatus;
    }

    blocksize = adaptExt->info.blk_size;
    lastLBA = adaptExt->info.capacity / (blocksize / SECTOR_SIZE) - 1;
    adaptExt->lastLBA = lastLBA;

    if (Srb->DataTransferLength == sizeof(READ_CAPACITY_DATA)) {
        if (lastLBA > 0xFFFFFFFF) {
            readCap->LogicalBlockAddress = (ULONG)-1;
        } else {
            REVERSE_BYTES(&readCap->LogicalBlockAddress,
                          &lastLBA);
        }
        REVERSE_BYTES(&readCap->BytesPerBlock,
                          &blocksize);
    } else {
        ASSERT(Srb->DataTransferLength ==
                          sizeof(READ_CAPACITY_DATA_EX));
        REVERSE_BYTES_QUAD(&readCapEx->LogicalBlockAddress.QuadPart,
                          &lastLBA);
        REVERSE_BYTES(&readCapEx->BytesPerBlock,
                          &blocksize);
    }
    Srb->ScsiStatus = SCSISTAT_GOOD;
    return SrbStatus;
}


VOID
CompleteSRB(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    ScsiPortNotification(RequestComplete,
                         DeviceExtension,
                         Srb);
#ifndef USE_STORPORT
    ScsiPortNotification(NextLuRequest,
                         DeviceExtension,
                         Srb->PathId,
                         Srb->TargetId,
                         Srb->Lun);
#endif
}

VOID
FORCEINLINE
CompleteDPC(
    IN PVOID DeviceExtension,
    IN pblk_req vbr,
    IN ULONG MessageID
    )
{
    PSCSI_REQUEST_BLOCK Srb      = (PSCSI_REQUEST_BLOCK)vbr->req;
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
#ifndef USE_STORPORT
    PRHEL_SRB_EXTENSION srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
    UNREFERENCED_PARAMETER( MessageID );
#endif
    RemoveEntryList(&vbr->list_entry);

#ifdef USE_STORPORT
    if(!adaptExt->dump_mode && adaptExt->dpc_ok) {
        InsertTailList(&adaptExt->complete_list, &vbr->list_entry);
        StorPortIssueDpc(DeviceExtension,
                         &adaptExt->completion_dpc,
                         ULongToPtr(MessageID),
                         NULL);
        return;
    }
    CompleteSRB(DeviceExtension, Srb);
#else
   if (Srb->DataTransferLength > srbExt->Xfer) {
       Srb->DataTransferLength = srbExt->Xfer;
       Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
    }
    ScsiPortNotification(RequestComplete,
                         DeviceExtension,
                         Srb);
    if(srbExt->call_next) {
        ScsiPortNotification(NextLuRequest,
                         DeviceExtension,
                         Srb->PathId,
                         Srb->TargetId,
                         Srb->Lun);
    }
#endif
}
#ifdef USE_STORPORT
#pragma warning(disable: 4100 4701)
VOID
CompleteDpcRoutine(
    IN PSTOR_DPC  Dpc,
    IN PVOID Context,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )
{
    STOR_LOCK_HANDLE  LockHandle;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)Context;

#ifdef MSI_SUPPORTED
    ULONG MessageID = PtrToUlong(SystemArgument1);
    ULONG OldIrql;
#endif

#ifdef MSI_SUPPORTED
    if(adaptExt->msix_vectors) {
        StorPortAcquireMSISpinLock (Context, MessageID, &OldIrql);
    } else {
#endif
        StorPortAcquireSpinLock ( Context, InterruptLock , NULL, &LockHandle);
#ifdef MSI_SUPPORTED
    }
#endif

    while (!IsListEmpty(&adaptExt->complete_list)) {
        PSCSI_REQUEST_BLOCK Srb;
        PRHEL_SRB_EXTENSION srbExt;
        pblk_req vbr;
        vbr  = (pblk_req) RemoveHeadList(&adaptExt->complete_list);
        Srb = (PSCSI_REQUEST_BLOCK)vbr->req;
        srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
#ifdef MSI_SUPPORTED
        if(adaptExt->msix_vectors) {
           StorPortReleaseMSISpinLock (Context, MessageID, OldIrql);
        } else {
#endif
           StorPortReleaseSpinLock (Context, &LockHandle);
#ifdef MSI_SUPPORTED
        }
#endif
        if (Srb->DataTransferLength > srbExt->Xfer) {
           Srb->DataTransferLength = srbExt->Xfer;
           Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
        }
        ScsiPortNotification(RequestComplete,
                         Context,
                         Srb);
#ifdef MSI_SUPPORTED
        if(adaptExt->msix_vectors) {
           StorPortAcquireMSISpinLock (Context, MessageID, &OldIrql);
        } else {
#endif
           StorPortAcquireSpinLock ( Context, InterruptLock , NULL, &LockHandle);
#ifdef MSI_SUPPORTED
        }
#endif
    }

#ifdef MSI_SUPPORTED
    if(adaptExt->msix_vectors) {
        StorPortReleaseMSISpinLock (Context, MessageID, OldIrql);
    } else {
#endif
        StorPortReleaseSpinLock (Context, &LockHandle);
#ifdef MSI_SUPPORTED
    }
#endif
    return;
}
#endif

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
    ScsiPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         ErrorCode,
                         UniqueId);
#endif
}
