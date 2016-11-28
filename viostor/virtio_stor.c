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

BOOLEAN
VirtIoHwReinitialize(
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
    IN OUT PSRB_TYPE Srb
    );

UCHAR
RhelScsiGetModeSense(
    IN PVOID DeviceExtension,
    IN OUT PSRB_TYPE Srb
    );

UCHAR
RhelScsiGetCapacity(
    IN PVOID DeviceExtension,
    IN OUT PSRB_TYPE Srb
    );

UCHAR
RhelScsiReportLuns(
    IN PVOID DeviceExtension,
    IN OUT PSRB_TYPE Srb
    );

VOID
FORCEINLINE
CompleteSRB(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
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
    hwInitData.SrbExtensionSize         = sizeof(SRB_EXTENSION);

    hwInitData.AdapterInterfaceType     = PCIBus;

#ifndef USE_STORPORT
    hwInitData.VendorIdLength           = 4;
    hwInitData.VendorId                 = venId;
    hwInitData.DeviceIdLength           = 4;
    hwInitData.DeviceId                 = devId;
#endif

    /* Virtio doesn't specify the number of BARs used by the device; it may
     * be one, it may be more. PCI_TYPE0_ADDRESSES, the theoretical maximum
     * on PCI, is a safe upper bound.
     */
    hwInitData.NumberOfAccessRanges     = PCI_TYPE0_ADDRESSES;
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

static ULONG InitVirtIODevice(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    NTSTATUS status;

    status = virtio_device_initialize(
        &adaptExt->vdev,
        &VioStorSystemOps,
        adaptExt,
        adaptExt->msix_enabled);
    if (!NT_SUCCESS(status)) {
        LogError(adaptExt,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize virtio device, error %x\n", status));
        if (status == STATUS_DEVICE_NOT_CONNECTED) {
            return SP_RETURN_NOT_FOUND;
        } else {
            return SP_RETURN_ERROR;
        }
    }

    return SP_RETURN_FOUND;
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
    USHORT             queueLength;
    ULONG              pci_cfg_len;
    ULONG              res, i;

    ULONG              index;
    ULONG              num_cpus;
    ULONG              max_cpus;
    ULONG              max_queues;
    ULONG              Size;
    ULONG              HeapSize;

    PVOID              uncachedExtensionVa;
    ULONG              extensionSize;

    UNREFERENCED_PARAMETER( HwContext );
    UNREFERENCED_PARAMETER( BusInformation );
    UNREFERENCED_PARAMETER( ArgumentString );
    UNREFERENCED_PARAMETER( Again );

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    adaptExt->system_io_bus_number = ConfigInfo->SystemIoBusNumber;
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

    ConfigInfo->NumberOfBuses               = 1;
    ConfigInfo->MaximumNumberOfTargets      = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;

    pci_cfg_len = ScsiPortGetBusData(
        DeviceExtension,
        PCIConfiguration,
        ConfigInfo->SystemIoBusNumber,
        (ULONG)ConfigInfo->SlotNumber,
        (PVOID)&adaptExt->pci_config_buf,
        sizeof(adaptExt->pci_config_buf));

    if (pci_cfg_len != sizeof(adaptExt->pci_config_buf)) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("CANNOT READ PCI CONFIGURATION SPACE %d\n", pci_cfg_len));
        return SP_RETURN_ERROR;
    }

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

    adaptExt->msix_enabled = FALSE;
#ifdef MSI_SUPPORTED
    {
        UCHAR CapOffset;
        PPCI_MSIX_CAPABILITY pMsixCapOffset;
        PPCI_COMMON_HEADER   pPciComHeader;
        pPciComHeader = &adaptExt->pci_config;
        if ( (pPciComHeader->Status & PCI_STATUS_CAPABILITIES_LIST) == 0)
        {
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("NO CAPABILITIES_LIST\n"));
        }
        else
        {
           if ( (pPciComHeader->HeaderType & (~PCI_MULTIFUNCTION)) == PCI_DEVICE_TYPE )
           {
              CapOffset = pPciComHeader->u.type0.CapabilitiesPtr;
              while (CapOffset != 0)
              {
                 pMsixCapOffset = (PPCI_MSIX_CAPABILITY)&adaptExt->pci_config_buf[CapOffset];
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
           }
           else
           {
              RhelDbgPrint(TRACE_LEVEL_FATAL, ("NOT A PCI_DEVICE_TYPE\n"));
           }
        }
    }
#endif

    /* initialize the virtual device */
    res = InitVirtIODevice(DeviceExtension);
    if (res != SP_RETURN_FOUND) {
        return res;
    }

    adaptExt->features = virtio_get_features(&adaptExt->vdev);
    ConfigInfo->CachesData = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_WCACHE) ? TRUE : FALSE;
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_WCACHE = %d\n", ConfigInfo->CachesData));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_BLK_F_MQ = %d\n", CHECKBIT(adaptExt->features, VIRTIO_BLK_F_MQ)));

    virtio_query_queue_allocation(
        &adaptExt->vdev,
        0,
        &queueLength,
        &adaptExt->pageAllocationSize,
        &adaptExt->poolAllocationSize);

    if(adaptExt->dump_mode) {
        ConfigInfo->NumberOfPhysicalBreaks = 8;
    } else {
        ConfigInfo->NumberOfPhysicalBreaks = MAX_PHYS_SEGMENTS + 1;
    }

    ConfigInfo->MaximumTransferLength = 0x00FFFFFF;

#if (INDIRECT_SUPPORTED == 1)
    if(!adaptExt->dump_mode) {
        adaptExt->indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);
    }
    if(adaptExt->indirect) {
        adaptExt->queue_depth = queueLength;
    }
#else
    adaptExt->indirect = 0;
    adaptExt->queue_depth = queueLength / ConfigInfo->NumberOfPhysicalBreaks - 1;
#endif


#if (INDIRECT_SUPPORTED)
    if(!adaptExt->dump_mode) {
        adaptExt->indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);
    }
    if(adaptExt->indirect) {
        adaptExt->queue_depth = queueLength;
    }
#else
    adaptExt->indirect = 0;
#endif
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("breaks_number = %x  queue_depth = %x\n",
                ConfigInfo->NumberOfPhysicalBreaks,
                adaptExt->queue_depth));

    /* Note that the pointer returned from ScsiPortGetUncachedExtension may not be page-aligned */
    adaptExt->pageAllocationSize = ROUND_TO_PAGES(adaptExt->pageAllocationSize);
    adaptExt->pageAllocationVa = ScsiPortGetUncachedExtension(
        DeviceExtension,
        ConfigInfo,
        PAGE_SIZE + adaptExt->pageAllocationSize + adaptExt->poolAllocationSize);
    if (!adaptExt->pageAllocationVa) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

            RhelDbgPrint(TRACE_LEVEL_FATAL, ("Virtual queue %d config failed.\n", index));
            return SP_RETURN_ERROR;
        }
        adaptExt->pageAllocationSize += ROUND_TO_PAGES(Size);
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(HeapSize);
    }
    if (!adaptExt->dump_mode) {
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(SRB_EXTENSION));
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(STOR_DPC) * max_queues);
    }
    if (max_queues > MAX_QUEUES_PER_DEVICE_DEFAULT)
    {
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(
            (max_queues) * virtio_get_queue_descriptor_size());
    }

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("breaks_number = %x  queue_depth = %x\n",
                ConfigInfo->NumberOfPhysicalBreaks,
                adaptExt->queue_depth));

    extensionSize = PAGE_SIZE + adaptExt->pageAllocationSize + adaptExt->poolAllocationSize;
    uncachedExtensionVa = ScsiPortGetUncachedExtension(DeviceExtension, ConfigInfo, extensionSize);
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("StorPortGetUncachedExtension uncachedExtensionVa = %p allocation size = %d\n", uncachedExtensionVa, extensionSize));
    if (!uncachedExtensionVa) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Can't get uncached extension allocation size = %d\n", extensionSize));
        return SP_RETURN_ERROR;
    }

    adaptExt->pageAllocationVa = (PVOID)(((ULONG_PTR)(uncachedExtensionVa) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    if (adaptExt->poolAllocationSize > 0) {
        adaptExt->poolAllocationVa = (PVOID)((ULONG_PTR)uncachedExtensionVa + adaptExt->pageAllocationSize);
    }
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Page-aligned area at %p, size = %d\n", adaptExt->pageAllocationVa, adaptExt->pageAllocationSize));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Pool area at %p, size = %d\n", adaptExt->poolAllocationVa, adaptExt->poolAllocationSize));


#if (NTDDI_VERSION > NTDDI_WIN7)
    RhelDbgPrint(TRACE_LEVEL_FATAL, ("pmsg_affinity = %p\n",adaptExt->pmsg_affinity));
    if (!adaptExt->dump_mode && (adaptExt->num_queues > 1) && (adaptExt->pmsg_affinity == NULL)) {
        ULONG Status =
        StorPortAllocatePool(DeviceExtension,
                             sizeof(GROUP_AFFINITY) * (adaptExt->num_queues),
                             VIOSCSI_POOL_TAG,
                             (PVOID*)&adaptExt->pmsg_affinity);
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("pmsg_affinity = %p Status = %lu\n",adaptExt->pmsg_affinity, Status));
    }
#endif

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
    ULONG index;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    for (index = 0; index < adaptExt->num_queues; ++index) {
        StorPortInitializeDpc(DeviceExtension,
            &adaptExt->dpc[index],
            CompleteDpcRoutine);
    }
    adaptExt->dpc_ok = TRUE;
    return TRUE;
}
#endif

static BOOLEAN InitializeVirtualQueues(PADAPTER_EXTENSION adaptExt)
{
    ULONG index;
    NTSTATUS status;
    BOOLEAN useEventIndex = CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX);
    ULONG numQueues = adaptExt->num_queues;

    RhelDbgPrint(TRACE_LEVEL_FATAL, ("InitializeVirtualQueues numQueues %d\n", numQueues));
    status = virtio_find_queues(
        &adaptExt->vdev,
        numQueues,
        adaptExt->vq);
    if (!NT_SUCCESS(status)) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("virtio_find_queues failed with error %x\n", status));
        return FALSE;
    }

    for (index = 0; index < numQueues; index++) {
        virtio_set_queue_event_suppression(
            adaptExt->vq[index],
            useEventIndex);
    }
    return TRUE;
}

BOOLEAN
VirtIoHwInitialize(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt;
    BOOLEAN            ret = FALSE;
    ULONGLONG          guestFeatures = 0;
#ifdef USE_STORPORT
    PERF_CONFIGURATION_DATA perfData = { 0 };
    ULONG              status = STOR_STATUS_SUCCESS;
#ifdef MSI_SUPPORTED
    MESSAGE_INTERRUPT_INFORMATION msi_info = { 0 };
#endif
#endif

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s (%d)\n", __FUNCTION__, KeGetCurrentIrql()));

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (CHECKBIT(adaptExt->features, VIRTIO_F_VERSION_1)) {
        guestFeatures |= (1ULL << VIRTIO_F_VERSION_1);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_F_ANY_LAYOUT)) {
        guestFeatures |= (1ULL << VIRTIO_F_ANY_LAYOUT);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX)) {
        guestFeatures |= (1ULL << VIRTIO_RING_F_EVENT_IDX);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_WCACHE)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_WCACHE);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BARRIER)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_BARRIER);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_RO);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SIZE_MAX)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_SIZE_MAX);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SEG_MAX)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_SEG_MAX);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BLK_SIZE)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_BLK_SIZE);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_GEOMETRY)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_GEOMETRY);
    }

    if (!NT_SUCCESS(virtio_set_features(&adaptExt->vdev, guestFeatures))) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("virtio_set_features failed\n"));
        return FALSE;
    }
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("Host Features %llu gust features %llu\n", adaptExt->features, guestFeatures));

    adaptExt->msix_vectors = 0;
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
#endif

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Queues %d msix_vectors %d\n", adaptExt->num_queues, adaptExt->msix_vectors));
    if (adaptExt->num_queues > 1 &&
        (adaptExt->num_queues > adaptExt->msix_vectors)) {
        //FIXME
        adaptExt->num_queues = 1;
    }

    if (!InitializeVirtualQueues(adaptExt)) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot find snd virtual queue\n"));
        virtio_add_status(&adaptExt->vdev, VIRTIO_CONFIG_S_FAILED);
        return ret;
    }

    if (!adaptExt->dump_mode) {
        adaptExt->dpc = (PSTOR_DPC)VioStorPoolAlloc(DeviceExtension, sizeof(STOR_DPC) * adaptExt->num_queues);
    }

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
    if (!adaptExt->dump_mode)
    {
        if ((adaptExt->num_queues > 1) && (adaptExt->perfFlags == 0)) {
            perfData.Version = STOR_PERF_VERSION;
            perfData.Size = sizeof(PERF_CONFIGURATION_DATA);

            status = StorPortInitializePerfOpts(DeviceExtension, TRUE, &perfData);

            RhelDbgPrint(TRACE_LEVEL_FATAL, ("Perf Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, FirstRedirectionMessageNumber = %d,LastRedirectionMessageNumber = %d\n",
                perfData.Version, perfData.Flags, perfData.ConcurrentChannels, perfData.FirstRedirectionMessageNumber, perfData.LastRedirectionMessageNumber));
            if (status == STOR_STATUS_SUCCESS) {
                if (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION)) {
                    adaptExt->perfFlags |= STOR_PERF_DPC_REDIRECTION;
                }
                if (CHECKFLAG(perfData.Flags, STOR_PERF_INTERRUPT_MESSAGE_RANGES)) {
                    adaptExt->perfFlags |= STOR_PERF_INTERRUPT_MESSAGE_RANGES;
                    perfData.FirstRedirectionMessageNumber = 0;
                    perfData.LastRedirectionMessageNumber = perfData.FirstRedirectionMessageNumber + adaptExt->num_queues;
                    if ((adaptExt->pmsg_affinity != NULL) && CHECKFLAG(perfData.Flags, STOR_PERF_ADV_CONFIG_LOCALITY)) {
                        RtlZeroMemory((PCHAR)adaptExt->pmsg_affinity, sizeof (GROUP_AFFINITY)* (adaptExt->num_queues));
                        adaptExt->perfFlags |= STOR_PERF_ADV_CONFIG_LOCALITY;
                        perfData.MessageTargets = adaptExt->pmsg_affinity;
#if (NTDDI_VERSION > NTDDI_WIN7)
                        if (CHECKFLAG(perfData.Flags, STOR_PERF_CONCURRENT_CHANNELS)) {
                            adaptExt->perfFlags |= STOR_PERF_CONCURRENT_CHANNELS;
                            perfData.ConcurrentChannels = adaptExt->num_queues;
                        }
#endif
                    }
                }
#if (NTDDI_VERSION > NTDDI_WIN7)
                if (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION_CURRENT_CPU)) {
//                    adaptExt->perfFlags |= STOR_PERF_DPC_REDIRECTION_CURRENT_CPU;
                }
#endif
                if (CHECKFLAG(perfData.Flags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO)) {
                    adaptExt->perfFlags |= STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO;
                }
                perfData.Flags = adaptExt->perfFlags;
                RhelDbgPrint(TRACE_LEVEL_FATAL, ("Perf Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, FirstRedirectionMessageNumber = %d,LastRedirectionMessageNumber = %d\n",
                    perfData.Version, perfData.Flags, perfData.ConcurrentChannels, perfData.FirstRedirectionMessageNumber, perfData.LastRedirectionMessageNumber));
                status = StorPortInitializePerfOpts(DeviceExtension, FALSE, &perfData);
                if (status != STOR_STATUS_SUCCESS) {
                    adaptExt->perfFlags = 0;
                    RhelDbgPrint(TRACE_LEVEL_ERROR, ("%s StorPortInitializePerfOpts FALSE status = 0x%x\n", __FUNCTION__, status));
                }
                else if ((adaptExt->pmsg_affinity != NULL) && CHECKFLAG(perfData.Flags, STOR_PERF_ADV_CONFIG_LOCALITY)){
                    UCHAR msg = 0;
                    PGROUP_AFFINITY ga;
                    UCHAR cpu = 0;
                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("Perf Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, FirstRedirectionMessageNumber = %d,LastRedirectionMessageNumber = %d\n",
                        perfData.Version, perfData.Flags, perfData.ConcurrentChannels, perfData.FirstRedirectionMessageNumber, perfData.LastRedirectionMessageNumber));
                    for (msg = 0; msg < adaptExt->num_queues; msg++) {
                        ga = &adaptExt->pmsg_affinity[msg];
                        if ( ga->Mask > 0) {
                            cpu = RtlFindLeastSignificantBit((ULONGLONG)ga->Mask);
                            RhelDbgPrint(TRACE_LEVEL_FATAL, ("msg = %d, mask = 0x%lx group = %hu cpu = %hu\n", msg, ga->Mask, ga->Group, cpu));
                        }
                    }
                }
            }
            else {
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("%s StorPortInitializePerfOpts TRUE status = 0x%x\n", __FUNCTION__, status));
            }
        }
    }

    if(!adaptExt->dump_mode && !adaptExt->dpc_ok)
    {
        ret = StorPortEnablePassiveInitialization(DeviceExtension, VirtIoPassiveInitializeRoutine);
    }
#endif

    if (ret) {
        virtio_device_ready(&adaptExt->vdev);
    } else {
        virtio_add_status(&adaptExt->vdev, VIRTIO_CONFIG_S_FAILED);
    }

    return ret;
}

BOOLEAN
VirtIoStartIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PCDB cdb = SRB_CDB(Srb);
    PADAPTER_EXTENSION adaptExt;
    UCHAR ScsiStatus = SCSISTAT_GOOD;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    switch (SRB_FUNCTION(Srb)) {
        case SRB_FUNCTION_EXECUTE_SCSI:
        case SRB_FUNCTION_IO_CONTROL: {
            break;
        }
        case SRB_FUNCTION_PNP:
        case SRB_FUNCTION_POWER:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_LOGICAL_UNIT: {
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
            CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
            return TRUE;
        }
        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_SHUTDOWN: {
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
            SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
            if (!RhelDoFlush(DeviceExtension, (PSRB_TYPE)Srb, FALSE)) {
                SRB_SET_SRB_STATUS(Srb, SRB_STATUS_ERROR);
                CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
            }
            return TRUE;
        }

        default: {
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_INVALID_REQUEST);
            CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
            return TRUE;
        }
    }

    switch (cdb->CDB6GENERIC.OperationCode) {
        case SCSIOP_MODE_SENSE: {
            SRB_SET_SRB_STATUS(Srb, RhelScsiGetModeSense(DeviceExtension, (PSRB_TYPE)Srb));
            CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
            return TRUE;
        }
        case SCSIOP_INQUIRY: {
            SRB_SET_SRB_STATUS(Srb, RhelScsiGetInquiryData(DeviceExtension, (PSRB_TYPE)Srb));
            CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
            return TRUE;
        }

        case SCSIOP_READ_CAPACITY16:
        case SCSIOP_READ_CAPACITY: {
            SRB_SET_SRB_STATUS(Srb, RhelScsiGetCapacity(DeviceExtension, (PSRB_TYPE)Srb));
            CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
            return TRUE;
        }
        case SCSIOP_WRITE:
        case SCSIOP_WRITE16: {
            if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
                PSENSE_DATA senseBuffer = (PSENSE_DATA) Srb->SenseInfoBuffer;
                ScsiStatus = SCSISTAT_CHECK_CONDITION;
                SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
                SRB_SET_SRB_STATUS(Srb, SRB_STATUS_ERROR);
                senseBuffer->SenseKey = SCSI_SENSE_DATA_PROTECT;
                senseBuffer->AdditionalSenseCode = SCSI_ADWRITE_PROTECT;
                CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
                return TRUE;
            }
        }
        case SCSIOP_READ:
        case SCSIOP_READ16: {
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
            if(!RhelDoReadWrite(DeviceExtension, (PSRB_TYPE)Srb)) {
                SRB_SET_SRB_STATUS(Srb, SRB_STATUS_BUSY);
                CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
            }
            return TRUE;
        }
        case SCSIOP_START_STOP_UNIT: {
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
            CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
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
            SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
            CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
            return TRUE;
        }
        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16: {
            SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
            if (!RhelDoFlush(DeviceExtension, (PSRB_TYPE)Srb, FALSE)) {
                SRB_SET_SRB_STATUS(Srb, SRB_STATUS_ERROR);
                CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
            }
            return TRUE;
        }
        default: {
            break;
        }
    }

    if (cdb->CDB12.OperationCode == SCSIOP_REPORT_LUNS) {
        SRB_SET_SRB_STATUS(Srb, RhelScsiReportLuns(DeviceExtension, (PSRB_TYPE)Srb));
        CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
        return TRUE;

    }

    SRB_SET_SRB_STATUS(Srb, SRB_STATUS_INVALID_REQUEST);
    CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
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
    PSRB_TYPE           Srb;
    ULONG               intReason = 0;
    PSRB_EXTENSION      srbExt;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s (%d)\n", __FUNCTION__, KeGetCurrentIrql()));
    intReason = virtio_read_isr_status(&adaptExt->vdev);
    if ( intReason == 1 || adaptExt->dump_mode ) {
        isInterruptServiced = TRUE;
        while((vbr = (pblk_req)virtqueue_get_buf(adaptExt->vq[0], &len)) != NULL) {
           Srb = (PSRB_TYPE)vbr->req;
           if (Srb) {
              switch (vbr->status) {
              case VIRTIO_BLK_S_OK:
                 SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
                 break;
              case VIRTIO_BLK_S_UNSUPP:
                 SRB_SET_SRB_STATUS(Srb, SRB_STATUS_INVALID_REQUEST);
                 break;
              default:
                 SRB_SET_SRB_STATUS(Srb, SRB_STATUS_ERROR);
                 RhelDbgPrint(TRACE_LEVEL_ERROR, ("SRB_STATUS_ERROR\n"));
                 break;
              }
           }
           if (vbr->out_hdr.type == VIRTIO_BLK_T_FLUSH) {
              CompleteSRB(DeviceExtension, Srb);
           } else if (vbr->out_hdr.type == VIRTIO_BLK_T_GET_ID) {
              adaptExt->sn_ok = TRUE;
           } else if (Srb) {
              srbExt = SRB_EXTENSION(Srb);
              if (srbExt->fua) {
                  UCHAR ScsiStatus = SCSISTAT_GOOD;
                  RemoveEntryList(&vbr->list_entry);
                  SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
                  SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
                  if (!RhelDoFlush(DeviceExtension, Srb, TRUE)) {
                      SRB_SET_SRB_STATUS(Srb, SRB_STATUS_ERROR);
                      CompleteSRB(DeviceExtension, Srb);
                  } else {
                      srbExt->fua = FALSE;
                  }
              } else {
                  CompleteDPC(DeviceExtension, vbr, 1);
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
    BOOLEAN SupportedControlTypes[5] = {TRUE, TRUE, TRUE, FALSE, FALSE};

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
                SupportedControlTypes[Index];
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
        RhelShutDown(DeviceExtension);
        if (!VirtIoHwReinitialize(DeviceExtension))
        {
           RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot reinitialize HW\n"));
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

BOOLEAN
VirtIoHwReinitialize(
    IN PVOID DeviceExtension
    )
{
    /* The adapter is being restarted and we need to bring it back up without
     * running any passive-level code. Note that VirtIoFindAdapter is *not*
     * called on restart.
     */
    if (InitVirtIODevice(DeviceExtension) != SP_RETURN_FOUND) {
        return FALSE;
    }
    return VirtIoHwInitialize(DeviceExtension);
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
    PSRB_EXTENSION        srbExt;
    PSTOR_SCATTER_GATHER_LIST sgList;
    ULONGLONG             lba;
    ULONG                 blocks;
#if (NTDDI_VERSION >= NTDDI_WIN7)
    PROCESSOR_NUMBER ProcNumber;
    ULONG processor = KeGetCurrentProcessorNumberEx(&ProcNumber);
    ULONG cpu = ProcNumber.Number;
#else
    ULONG cpu = KeGetCurrentProcessorNumber();
#endif

    cdb      = SRB_CDB(Srb);
    srbExt   = SRB_EXTENSION(Srb);
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if(SRB_PATH_ID(Srb) || SRB_TARGET_ID(Srb) || SRB_LUN(Srb)) {
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_NO_DEVICE);
        ScsiPortNotification(RequestComplete,
                             DeviceExtension,
                             Srb);
        return FALSE;
    }

    RtlZeroMemory(srbExt, sizeof(*srbExt));
    srbExt->cpu = (UCHAR)cpu;

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
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
            return TRUE;
        }
    }

    lba = RhelGetLba(DeviceExtension, cdb);
    blocks = (SRB_DATA_TRANSFER_LENGTH(Srb) + adaptExt->info.blk_size - 1) / adaptExt->info.blk_size;
    if (lba > adaptExt->lastLBA) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("SRB_STATUS_BAD_SRB_BLOCK_LENGTH lba = %llu lastLBA= %llu\n", lba, adaptExt->lastLBA));
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        CompleteSRB(DeviceExtension, (PSRB_TYPE)Srb);
        return FALSE;
    }
    if ((lba + blocks) > adaptExt->lastLBA) {
        blocks = (ULONG)(adaptExt->lastLBA + 1 - lba);
        RhelDbgPrint(TRACE_LEVEL_ERROR, ("lba = %llu lastLBA= %llu blocks = %lu\n", lba, adaptExt->lastLBA, blocks));
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (blocks * adaptExt->info.blk_size));
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

    if (SRB_FLAGS(Srb) & SRB_FLAGS_DATA_OUT) {
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
    PSRB_TYPE           Srb;
    BOOLEAN             isInterruptServiced = FALSE;
    PSRB_EXTENSION      srbExt;
    STOR_LOCK_HANDLE    queueLock = { 0 };

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 ("<--->%s : MessageID 0x%x\n", __FUNCTION__, MessageID));

    if (MessageID == 0) {
        RhelGetDiskGeometry(DeviceExtension);
        return TRUE;
    }

    VioStorVQLock(DeviceExtension, MessageID, &queueLock, TRUE);
    while((vbr = (pblk_req)virtqueue_get_buf(adaptExt->vq[MessageID - 1], &len)) != NULL) {
        VioStorVQUnlock(DeviceExtension, MessageID, &queueLock, TRUE);
        Srb = (PSRB_TYPE)vbr->req;
        if (Srb) {
           switch (vbr->status) {
           case VIRTIO_BLK_S_OK:
              SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
              break;
           case VIRTIO_BLK_S_UNSUPP:
              SRB_SET_SRB_STATUS(Srb, SRB_STATUS_INVALID_REQUEST);
              break;
           default:
              SRB_SET_SRB_STATUS(Srb, SRB_STATUS_ERROR);
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("SRB_STATUS_ERROR\n"));
              break;
           }
        }
        if (vbr->out_hdr.type == VIRTIO_BLK_T_FLUSH) {
            CompleteSRB(DeviceExtension, Srb);
        } else if (vbr->out_hdr.type == VIRTIO_BLK_T_GET_ID) {
            adaptExt->sn_ok = TRUE;
        } else if (Srb) {
            srbExt   = SRB_EXTENSION(Srb);
            if (srbExt->fua == TRUE) {
               UCHAR ScsiStatus = SCSISTAT_GOOD;
               RemoveEntryList(&vbr->list_entry);
               SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
               SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
               if (!RhelDoFlush(DeviceExtension, Srb, TRUE)) {
                    SRB_SET_SRB_STATUS(Srb, SRB_STATUS_ERROR);
                    CompleteSRB(DeviceExtension, Srb);
                } else {
                    srbExt->fua = FALSE;
                }
            } else {
                CompleteDPC(DeviceExtension, vbr, MessageID);
            }
        }
        VioStorVQLock(DeviceExtension, MessageID, &queueLock, TRUE);
        isInterruptServiced = TRUE;
    }
    VioStorVQUnlock(DeviceExtension, MessageID, &queueLock, TRUE);

    return isInterruptServiced;
}
#endif

#endif

UCHAR
RhelScsiGetInquiryData(
    IN PVOID DeviceExtension,
    IN OUT PSRB_TYPE Srb
    )
{

    PINQUIRYDATA InquiryData;
    ULONG dataLen;
    UCHAR SrbStatus = SRB_STATUS_INVALID_LUN;
    PCDB cdb = SRB_CDB(Srb);
    PADAPTER_EXTENSION adaptExt;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    InquiryData = (PINQUIRYDATA)SRB_DATA_BUFFER(Srb);
    dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    SrbStatus = SRB_STATUS_SUCCESS;
    if((cdb->CDB6INQUIRY3.PageCode != VPD_SUPPORTED_PAGES) &&
       (cdb->CDB6INQUIRY3.EnableVitalProductData == 0)) {
        UCHAR ScsiStatus = SCSISTAT_CHECK_CONDITION;
        SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_SUPPORTED_PAGES) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)) {

        PVPD_SUPPORTED_PAGES_PAGE SupportPages;
        SupportPages = (PVPD_SUPPORTED_PAGES_PAGE)SRB_DATA_BUFFER(Srb);
        memset(SupportPages, 0, sizeof(VPD_SUPPORTED_PAGES_PAGE));
        SupportPages->PageCode = VPD_SUPPORTED_PAGES;
        SupportPages->PageLength = 3;
        SupportPages->SupportedPageList[0] = VPD_SUPPORTED_PAGES;
        SupportPages->SupportedPageList[1] = VPD_SERIAL_NUMBER;
        SupportPages->SupportedPageList[2] = VPD_DEVICE_IDENTIFIERS;
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_SUPPORTED_PAGES_PAGE) + SupportPages->PageLength));
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_SERIAL_NUMBER) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)) {

        PVPD_SERIAL_NUMBER_PAGE SerialPage;
        SerialPage = (PVPD_SERIAL_NUMBER_PAGE)SRB_DATA_BUFFER(Srb);
        SerialPage->PageCode = VPD_SERIAL_NUMBER;
        if (!adaptExt->sn_ok) {
           SerialPage->PageLength = 1;
           SerialPage->SerialNumber[0] = '0';
        } else {
           SerialPage->PageLength = BLOCK_SERIAL_STRLEN;
           ScsiPortMoveMemory(&SerialPage->SerialNumber, &adaptExt->sn, BLOCK_SERIAL_STRLEN);
        }
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_SERIAL_NUMBER_PAGE) + SerialPage->PageLength));
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_DEVICE_IDENTIFIERS) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)) {

        PVPD_IDENTIFICATION_PAGE IdentificationPage;
        PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr;
        IdentificationPage = (PVPD_IDENTIFICATION_PAGE)SRB_DATA_BUFFER(Srb);
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

        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_IDENTIFICATION_PAGE) +
                                     IdentificationPage->PageLength));

    }
    else if (dataLen > sizeof(INQUIRYDATA)) {
        ScsiPortMoveMemory(InquiryData, &adaptExt->inquiry_data, sizeof(INQUIRYDATA));
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(INQUIRYDATA)));
    } else {
        ScsiPortMoveMemory(InquiryData, &adaptExt->inquiry_data, dataLen);
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, dataLen);
    }

    return SrbStatus;
}

UCHAR
RhelScsiReportLuns(
    IN PVOID DeviceExtension,
    IN OUT PSRB_TYPE Srb
    )
{
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    PUCHAR data = (PUCHAR)SRB_DATA_BUFFER(Srb);
    UCHAR ScsiStatus = SCSISTAT_GOOD;

    UNREFERENCED_PARAMETER( DeviceExtension );

    data[3]=8;
    SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
    SRB_SET_SRB_STATUS(Srb, SrbStatus);
    SRB_SET_DATA_TRANSFER_LENGTH(Srb, 16);
    return SrbStatus;
}

UCHAR
RhelScsiGetModeSense(
    IN PVOID DeviceExtension,
    IN OUT PSRB_TYPE Srb
    )
{
    ULONG ModeSenseDataLen;
    UCHAR SrbStatus = SRB_STATUS_INVALID_LUN;
    PCDB cdb = SRB_CDB(Srb);
    PMODE_PARAMETER_HEADER header;
    PMODE_CACHING_PAGE cachePage;
    PMODE_PARAMETER_BLOCK blockDescriptor;
    PADAPTER_EXTENSION adaptExt;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    ModeSenseDataLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    SrbStatus = SRB_STATUS_INVALID_REQUEST;

    if ((cdb->MODE_SENSE.PageCode == MODE_PAGE_CACHING) ||
        (cdb->MODE_SENSE.PageCode == MODE_SENSE_RETURN_ALL)) {

        if (sizeof(MODE_PARAMETER_HEADER) > ModeSenseDataLen)
        {
           SrbStatus = SRB_STATUS_ERROR;
           return SrbStatus;
        }

        header = (PMODE_PARAMETER_HEADER)SRB_DATA_BUFFER(Srb);

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

           SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(MODE_PARAMETER_HEADER) +
                                        sizeof(MODE_CACHING_PAGE)));

        } else {
           SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(MODE_PARAMETER_HEADER)));
        }

        SrbStatus = SRB_STATUS_SUCCESS;

    }
    else if (cdb->MODE_SENSE.PageCode == MODE_PAGE_VENDOR_SPECIFIC) {

        if (sizeof(MODE_PARAMETER_HEADER) > ModeSenseDataLen) {
           SrbStatus = SRB_STATUS_ERROR;
           return SrbStatus;
        }

        header = (PMODE_PARAMETER_HEADER)SRB_DATA_BUFFER(Srb);
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

           SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(MODE_PARAMETER_HEADER) +
                                        sizeof(MODE_PARAMETER_BLOCK)));
        } else {
           SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(MODE_PARAMETER_HEADER)));
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
    IN OUT PSRB_TYPE Srb
    )
{
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    PREAD_CAPACITY_DATA readCap;
    PREAD_CAPACITY_DATA_EX readCapEx;
    u64 lastLBA;
    EIGHT_BYTE lba;
    u64 blocksize;
    PADAPTER_EXTENSION adaptExt= (PADAPTER_EXTENSION)DeviceExtension;
    PCDB cdb = SRB_CDB(Srb);
    UCHAR ScsiStatus = SCSISTAT_GOOD;
    UCHAR  PMI = 0;
#ifdef USE_STORPORT
    BOOLEAN depthSet = StorPortSetDeviceQueueDepth(DeviceExtension,
                                           SRB_PATH_ID(Srb),
                                           SRB_TARGET_ID(Srb),
                                           SRB_LUN(Srb),
                                           adaptExt->queue_depth);
    ASSERT(depthSet);
#endif

    readCap = (PREAD_CAPACITY_DATA)SRB_DATA_BUFFER(Srb);
    readCapEx = (PREAD_CAPACITY_DATA_EX)SRB_DATA_BUFFER(Srb);

    lba.AsULongLong = 0;
    if (cdb->CDB6GENERIC.OperationCode == SCSIOP_READ_CAPACITY16 ){
         PMI = cdb->READ_CAPACITY16.PMI & 1;
         REVERSE_BYTES_QUAD(&lba, &cdb->READ_CAPACITY16.LogicalBlock[0]);
    }

    if (!PMI && lba.AsULongLong) {
        PSENSE_DATA senseBuffer;
        SRB_GET_SENSE_INFO_BUFFER(Srb, senseBuffer);
        senseBuffer->SenseKey = SCSI_SENSE_ILLEGAL_REQUEST;
        senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_INVALID_CDB;
        ScsiStatus = SCSISTAT_CHECK_CONDITION;
        SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
        return SrbStatus;
    }

    blocksize = adaptExt->info.blk_size;
    lastLBA = adaptExt->info.capacity / (blocksize / SECTOR_SIZE) - 1;
    adaptExt->lastLBA = adaptExt->info.capacity;

    if (SRB_DATA_TRANSFER_LENGTH(Srb) == sizeof(READ_CAPACITY_DATA)) {
        if (lastLBA > 0xFFFFFFFF) {
            readCap->LogicalBlockAddress = (ULONG)-1;
        } else {
            REVERSE_BYTES(&readCap->LogicalBlockAddress,
                          &lastLBA);
        }
        REVERSE_BYTES(&readCap->BytesPerBlock,
                          &blocksize);
    } else {
        ASSERT(SRB_DATA_TRANSFER_LENGTH(Srb) ==
                          sizeof(READ_CAPACITY_DATA_EX));
        REVERSE_BYTES_QUAD(&readCapEx->LogicalBlockAddress.QuadPart,
                          &lastLBA);
        REVERSE_BYTES(&readCapEx->BytesPerBlock,
                          &blocksize);
    }
    SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
    return SrbStatus;
}


VOID
CompleteSRB(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    ScsiPortNotification(RequestComplete,
                         DeviceExtension,
                         Srb);
#ifndef USE_STORPORT
    ScsiPortNotification(NextLuRequest,
                         DeviceExtension,
                         SRB_PATH_ID(Srb),
                         SRB_TARGET_ID(Srb),
                         SRB_LUN(Srb));
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
    PSRB_TYPE Srb                = (PSRB_TYPE)vbr->req;
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
#ifndef USE_STORPORT
    PSRB_EXTENSION srbExt        = SRB_EXTENSION(Srb);
    UNREFERENCED_PARAMETER( MessageID );
#endif
    RemoveEntryList(&vbr->list_entry);

#ifdef USE_STORPORT
    if(!adaptExt->dump_mode && adaptExt->dpc_ok) {
        InsertTailList(&adaptExt->complete_list, &vbr->list_entry);
        StorPortIssueDpc(DeviceExtension,
                         &adaptExt->dpc[MessageID - 1],
                         ULongToPtr(MessageID),
                         ULongToPtr(MessageID));
        return;
    }
    CompleteSRB(DeviceExtension, Srb);
#else
   if (SRB_DATA_TRANSFER_LENGTH(Srb) > srbExt->Xfer) {
       SRB_SET_DATA_TRANSFER_LENGTH(Srb, (srbExt->Xfer));
       SRB_SET_SRB_STATUS(Srb, SRB_STATUS_DATA_OVERRUN);
    }
    ScsiPortNotification(RequestComplete,
                         DeviceExtension,
                         Srb);
    if(srbExt->call_next) {
        ScsiPortNotification(NextLuRequest,
                         DeviceExtension,
                         SRB_PATH_ID(Srb),
                         SRB_TARGET_ID(Srb),
                         SRB_LUN(Srb));
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
        PSRB_TYPE Srb;
        PSRB_EXTENSION srbExt;
        pblk_req vbr;
        vbr  = (pblk_req) RemoveHeadList(&adaptExt->complete_list);
        Srb = (PSRB_TYPE)vbr->req;
        srbExt   = SRB_EXTENSION(Srb);
#ifdef MSI_SUPPORTED
        if(adaptExt->msix_vectors) {
           StorPortReleaseMSISpinLock (Context, MessageID, OldIrql);
        } else {
#endif
           StorPortReleaseSpinLock (Context, &LockHandle);
#ifdef MSI_SUPPORTED
        }
#endif
        if (SRB_DATA_TRANSFER_LENGTH(Srb) > srbExt->Xfer) {
           SRB_SET_DATA_TRANSFER_LENGTH(Srb, (srbExt->Xfer));
           SRB_SET_SRB_STATUS(Srb, SRB_STATUS_DATA_OVERRUN);
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

PVOID
VioStorPoolAlloc(
    IN PVOID DeviceExtension,
    IN SIZE_T size
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PVOID ptr = (PVOID)((ULONG_PTR)adaptExt->poolAllocationVa + adaptExt->poolOffset);

    if ((adaptExt->poolOffset + size) <= adaptExt->poolAllocationSize) {
        size = ROUND_TO_CACHE_LINES(size);
        adaptExt->poolOffset += (ULONG)size;
        RtlZeroMemory(ptr, size);
        return ptr;
    }
    RhelDbgPrint(TRACE_LEVEL_FATAL, ("Ran out of memory in VioScsiPoolAlloc(%Id)\n", size));
    return NULL;
}
