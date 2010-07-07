/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
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

ULONG   RhelDbgLevel = TRACE_LEVEL_ERROR;
BOOLEAN IsCrashDumpMode;

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

BOOLEAN
VirtIoInterrupt(
    IN PVOID DeviceExtension
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
    ULONG_PTR          ptr;
    ULONG              vr_sz;
    ULONG              vq_sz;
    USHORT             pageNum;

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
        ScsiPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Wrong access range %x bytes\n", accessRange->RangeLength));
        return SP_RETURN_NOT_FOUND;
    }

    if (!ScsiPortValidateRange(DeviceExtension,
                                           ConfigInfo->AdapterInterfaceType,
                                           ConfigInfo->SystemIoBusNumber,
                                           accessRange->RangeStart,
                                           accessRange->RangeLength,
                                           (BOOLEAN)!accessRange->RangeInMemory)) {

        ScsiPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Range validation failed %x for %x bytes\n",
                   (*ConfigInfo->AccessRanges)[0].RangeStart.LowPart,
                   (*ConfigInfo->AccessRanges)[0].RangeLength));

        return SP_RETURN_ERROR;
    }


    ConfigInfo->NumberOfBuses               = 1;
    ConfigInfo->MaximumNumberOfTargets      = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;

    adaptExt->device_base = (ULONG_PTR)ScsiPortGetDeviceBase(DeviceExtension,
                                           ConfigInfo->AdapterInterfaceType,
                                           ConfigInfo->SystemIoBusNumber,
                                           accessRange->RangeStart,
                                           accessRange->RangeLength,
                                           (BOOLEAN)!accessRange->RangeInMemory);

    if (adaptExt->device_base == (ULONG_PTR)NULL) {
        ScsiPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Couldn't map %x for %x bytes\n",
                   (*ConfigInfo->AccessRanges)[0].RangeStart.LowPart,
                   (*ConfigInfo->AccessRanges)[0].RangeLength));
        return SP_RETURN_ERROR;
    }

    VirtIODeviceReset(DeviceExtension);
    ScsiPortWritePortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_SEL), (USHORT)0);
    if (adaptExt->dump_mode) {
        ScsiPortWritePortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_PFN),(USHORT)0);
    }

    adaptExt->features = ScsiPortReadPortUlong((PULONG)(adaptExt->device_base + VIRTIO_PCI_HOST_FEATURES));
    ConfigInfo->CachesData = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_WCACHE) ? TRUE : FALSE;
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_WCACHE = %d\n", ConfigInfo->CachesData));

    pageNum = ScsiPortReadPortUshort((PUSHORT)(adaptExt->device_base + VIRTIO_PCI_QUEUE_NUM));
    vr_sz = vring_size(pageNum,PAGE_SIZE);
    vq_sz = (sizeof(struct vring_virtqueue) + sizeof(PVOID) * pageNum);

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
        adaptExt->queue_depth <<= 1;
    }	
#endif
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("breaks_number = %x  queue_depth = %x\n",
                ConfigInfo->NumberOfPhysicalBreaks,
                adaptExt->queue_depth));

    ptr = (ULONG_PTR)ScsiPortGetUncachedExtension(DeviceExtension, ConfigInfo, (PAGE_SIZE + vr_sz + vq_sz));
    if (ptr == (ULONG_PTR)NULL) {
        ScsiPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Couldn't get uncached extension\n"));
        return SP_RETURN_ERROR;
    }

    ptr += (PAGE_SIZE - 1);
    adaptExt->pci_vq_info.queue = PAGE_ALIGN(ptr);
    adaptExt->virtqueue = (vring_virtqueue*)((ULONG_PTR)(adaptExt->pci_vq_info.queue) + vr_sz);

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

    return TRUE;
}
#endif


BOOLEAN
VirtIoHwInitialize(
    IN PVOID DeviceExtension
    )
{

    PADAPTER_EXTENSION adaptExt;
    u64                cap;
    u32                v;
#ifdef MSI_SUPPORTED
    MESSAGE_INTERRUPT_INFORMATION msi_info;
#endif

    struct virtio_blk_geometry vgeo;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s (%d)\n", __FUNCTION__, KeGetCurrentIrql()));

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

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
    RhelDbgPrint(TRACE_LEVEL_ERROR, ("dump_mode = %x\n", adaptExt->dump_mode));
        adaptExt->pci_vq_info.vq = VirtIODeviceFindVirtualQueue(DeviceExtension, 0, adaptExt->msix_vectors - 1);
    }
#endif

    if(!adaptExt->pci_vq_info.vq) {
        adaptExt->pci_vq_info.vq = VirtIODeviceFindVirtualQueue(DeviceExtension, 0, 0);
    }
    if (!adaptExt->pci_vq_info.vq) {
        ScsiPortLogError(DeviceExtension,
                         NULL,
                         0,
                         0,
                         0,
                         SP_INTERNAL_ADAPTER_ERROR,
                         __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find snd virtual queue\n"));
        return FALSE;
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BARRIER)) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_BARRIER\n"));
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_RO\n"));
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SIZE_MAX)) {
        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, size_max),
                      &v, sizeof(v));
        adaptExt->info.size_max = v;
    } else {
        adaptExt->info.size_max = SECTOR_SIZE;
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SEG_MAX)) {
        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, seg_max),
                      &v, sizeof(v));
        adaptExt->info.seg_max = v;
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_SEG_MAX = %d\n", adaptExt->info.seg_max));
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BLK_SIZE)) {
        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, blk_size),
                      &v, sizeof(v));
        adaptExt->info.blk_size = v;
    } else {
        adaptExt->info.blk_size = SECTOR_SIZE;
    }
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_BLK_SIZE = %d\n", adaptExt->info.blk_size));

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_GEOMETRY)) {
        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, geometry),
                      &vgeo, sizeof(vgeo));
        adaptExt->info.geometry.cylinders= vgeo.cylinders;
        adaptExt->info.geometry.heads    = vgeo.heads;
        adaptExt->info.geometry.sectors  = vgeo.sectors;
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_GEOMETRY. cylinders = %d  heads = %d  sectors = %d\n", adaptExt->info.geometry.cylinders, adaptExt->info.geometry.heads, adaptExt->info.geometry.sectors));
    }

    VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, capacity),
                      &cap, sizeof(cap));
    adaptExt->info.capacity = cap;
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("capacity = %08I64X\n", adaptExt->info.capacity));


    if(CHECKBIT(adaptExt->features, VIRTIO_BLK_F_TOPOLOGY)) {
        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, physical_block_exp),
                      &adaptExt->info.physical_block_exp, sizeof(adaptExt->info.physical_block_exp));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("physical_block_exp = %d\n", adaptExt->info.physical_block_exp));

        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, alignment_offset),
                      &adaptExt->info.alignment_offset, sizeof(adaptExt->info.alignment_offset));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("alignment_offset = %d\n", adaptExt->info.alignment_offset));

        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, min_io_size),
                      &adaptExt->info.min_io_size, sizeof(adaptExt->info.min_io_size));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("min_io_size = %d\n", adaptExt->info.min_io_size));

        VirtIODeviceGet( DeviceExtension, FIELD_OFFSET(blk_config, opt_io_size),
                      &adaptExt->info.opt_io_size, sizeof(adaptExt->info.opt_io_size));
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("opt_io_size = %d\n", adaptExt->info.opt_io_size));
      
    }


    memset(&adaptExt->inquiry_data, 0, sizeof(INQUIRYDATA));

    adaptExt->inquiry_data.ANSIVersion = 4;
    adaptExt->inquiry_data.ResponseDataFormat = 2;
    adaptExt->inquiry_data.CommandQueue = 1;
    adaptExt->inquiry_data.DeviceType   = DIRECT_ACCESS_DEVICE;
    adaptExt->inquiry_data.Wide32Bit    = 1;
    ScsiPortMoveMemory(&adaptExt->inquiry_data.VendorId, "Red Hat ", sizeof("Red Hat "));
    ScsiPortMoveMemory(&adaptExt->inquiry_data.ProductId, "VirtIO", sizeof("VirtIO"));
    ScsiPortMoveMemory(&adaptExt->inquiry_data.ProductRevisionLevel, "0001", sizeof("0001"));
    ScsiPortMoveMemory(&adaptExt->inquiry_data.VendorSpecific, "0001", sizeof("0001"));

#ifdef USE_STORPORT
    if(!adaptExt->dump_mode)
    {
        return StorPortEnablePassiveInitialization(DeviceExtension, VirtIoPassiveInitializeRoutine);
    }
#endif
    return TRUE;
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
            Srb->SrbStatus = (UCHAR)RhelDoFlush(DeviceExtension, Srb);
            Srb->ScsiStatus = SCSISTAT_GOOD;
            CompleteSRB(DeviceExtension, Srb);
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
            PREAD_CAPACITY_DATA readCap;
            PREAD_CAPACITY_DATA_EX readCapEx;
            u64 lastLBA;
            u64 blocksize;
#ifdef USE_STORPORT
            BOOLEAN depthSet;
            depthSet = StorPortSetDeviceQueueDepth(DeviceExtension,
                                                   Srb->PathId,
                                                   Srb->TargetId,
                                                   Srb->Lun,
                                                   adaptExt->queue_depth);
            ASSERT(depthSet);
#endif
            readCap = (PREAD_CAPACITY_DATA)Srb->DataBuffer;
            readCapEx = (PREAD_CAPACITY_DATA_EX)Srb->DataBuffer;

            blocksize = adaptExt->info.blk_size;
            lastLBA = adaptExt->info.capacity / (blocksize / SECTOR_SIZE) - 1;

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

            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            CompleteSRB(DeviceExtension, Srb);
            return TRUE;
        }
        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_READ16:
        case SCSIOP_WRITE16: {
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
            Srb->SrbStatus = (UCHAR)RhelDoFlush(DeviceExtension, Srb);
            Srb->ScsiStatus = SCSISTAT_GOOD;
            CompleteSRB(DeviceExtension, Srb);
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

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s (%d)\n", __FUNCTION__, KeGetCurrentIrql()));

    if (VirtIODeviceISR(DeviceExtension) > 0) {
        isInterruptServiced = TRUE;
        while((vbr = adaptExt->pci_vq_info.vq->vq_ops->get_buf(adaptExt->pci_vq_info.vq, &len)) != NULL) {
           Srb = (PSCSI_REQUEST_BLOCK)vbr->req;
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
           if (vbr->out_hdr.type == VIRTIO_BLK_T_FLUSH) {
              adaptExt->flush_done = TRUE;
           }
           else
           {
               CompleteDPC(DeviceExtension, vbr, 0);
           }
        }
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

    ScsiPortCompleteRequest(DeviceExtension,
                            (UCHAR)PathId,
                            0xFF,
                            0xFF,
                            SRB_STATUS_BUS_RESET);
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
        adaptExt->pci_vq_info.vq = NULL;
#ifdef MSI_SUPPORTED
        if(!adaptExt->dump_mode && adaptExt->msix_vectors) {
           adaptExt->pci_vq_info.vq = VirtIODeviceFindVirtualQueue(DeviceExtension, 0, adaptExt->msix_vectors);
        }
#endif
        if(!adaptExt->pci_vq_info.vq) {
           adaptExt->pci_vq_info.vq = VirtIODeviceFindVirtualQueue(DeviceExtension, 0, 0);
        }
        if (!adaptExt->pci_vq_info.vq)
        {
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find snd virtual queue\n"));
           break;
        }
        VirtIoHwInitialize(DeviceExtension);
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
    PADAPTER_EXTENSION    adaptExt;
    PRHEL_SRB_EXTENSION   srbExt;
    PSTOR_SCATTER_GATHER_LIST sgList;

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

    sgList = StorPortGetScatterGatherList(DeviceExtension, Srb);

    for (i = 0, sgElement = 1; i < sgList->NumberOfElements; i++, sgElement++) {
        srbExt->vbr.sg[sgElement].physAddr = sgList->List[i].PhysicalAddress;
        srbExt->vbr.sg[sgElement].ulSize   = sgList->List[i].Length;
    }

    srbExt->vbr.out_hdr.sector = RhelGetLba(DeviceExtension, cdb);
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req            = (struct request *)Srb;

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
    srbExt->vbr.sg[0].ulSize = sizeof(srbExt->vbr.out_hdr);

    srbExt->vbr.sg[sgElement].physAddr = ScsiPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &dummy);
    srbExt->vbr.sg[sgElement].ulSize = sizeof(srbExt->vbr.status);

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

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 ("<--->%s : MessageID 0x%x\n", __FUNCTION__, MessageID));

    while((vbr = adaptExt->pci_vq_info.vq->vq_ops->get_buf(adaptExt->pci_vq_info.vq, &len)) != NULL) {
       Srb = (PSCSI_REQUEST_BLOCK)vbr->req;
       switch (vbr->status) {
       case VIRTIO_BLK_S_OK:
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;
       case VIRTIO_BLK_S_UNSUPP:
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
       default:
            Srb->SrbStatus = SRB_STATUS_ERROR;
            break;
       }
       CompleteDPC(DeviceExtension, vbr, MessageID);
    }

    return TRUE;
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
        SerialPage->PageLength = 1;
        SerialPage->SerialNumber[0] = '0';
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

        header = Srb->DataBuffer;

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

        header = Srb->DataBuffer;
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

           memset(blockDescriptor, 0, sizeof(MODE_PARAMETER_HEADER));

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
#ifdef USE_STORPORT
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
#else
    PRHEL_SRB_EXTENSION srbExt   = (PRHEL_SRB_EXTENSION)Srb->SrbExtension;
    UNREFERENCED_PARAMETER( MessageID );
#endif
    RemoveEntryList(&vbr->list_entry);

#ifdef USE_STORPORT
    if(!adaptExt->dump_mode) {
        InsertTailList(&adaptExt->complete_list, &vbr->list_entry);
        StorPortIssueDpc(DeviceExtension,
                         &adaptExt->completion_dpc,
                         ULongToPtr(MessageID),
                         NULL);
        return;
    }
    CompleteSRB(DeviceExtension, Srb);
#else
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
        pblk_req vbr;
        vbr  = (pblk_req) RemoveHeadList(&adaptExt->complete_list);
        Srb = (PSCSI_REQUEST_BLOCK)vbr->req;

#ifdef MSI_SUPPORTED
        if(adaptExt->msix_vectors) {
           StorPortReleaseMSISpinLock (Context, MessageID, OldIrql);
        } else {
#endif
           StorPortReleaseSpinLock (Context, &LockHandle);
#ifdef MSI_SUPPORTED
        }
#endif
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
