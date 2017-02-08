/**********************************************************************
 * Copyright (c) 2012-2016 Red Hat, Inc.
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
#include "vioscsidt.h"
#include "..\Tools\vendor.check.h"

#define MS_SM_HBA_API
#include <hbapiwmi.h>

#include <hbaapi.h>
#include <ntddscsi.h>

#define VioScsiWmi_MofResourceName        L"MofResource"

#include "resources.h"
#include "..\Tools\vendor.ver"

#define VIOSCSI_SETUP_GUID_INDEX 0
#define VIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX 1
#define VIOSCSI_MS_PORT_INFORM_GUID_INDEX 2

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
HW_DPC_ROUTINE       VioScsiCompleteDpcRoutine;
HW_PASSIVE_INITIALIZE_ROUTINE         VioScsiIoPassiveInitializeRoutine;
HW_WORKITEM          VioScsiWorkItemCallback;
#if (MSI_SUPPORTED == 1)
HW_MESSAGE_SIGNALED_INTERRUPT_ROUTINE VioScsiMSInterrupt;
#endif
#endif

BOOLEAN
VioScsiHwInitialize(
    IN PVOID DeviceExtension
    );

BOOLEAN
VioScsiHwReinitialize(
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
    IN PSRB_TYPE Srb
    );

VOID
FORCEINLINE
PostProcessRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    );

VOID
FORCEINLINE
CompleteRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    );

VOID
FORCEINLINE
DispatchQueue(
    IN PVOID DeviceExtension,
    IN ULONG MessageID
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

VOID
VioScsiWmiInitialize(
    IN PVOID  DeviceExtension
    );

VOID
VioScsiWmiSrb(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    );

VOID
VioScsiIoControl(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    );

BOOLEAN
VioScsiQueryWmiDataBlock(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG OutBufferSize,
    OUT PUCHAR Buffer
    );

UCHAR
VioScsiExecuteWmiMethod(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG MethodId,
    IN ULONG InBufferSize,
    IN ULONG OutBufferSize,
    IN OUT PUCHAR Buffer
    );

UCHAR
VioScsiQueryWmiRegInfo(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    OUT PWCHAR *MofResourceName
    );

VOID
VioScsiReadExtendedData(
    IN PVOID Context,
    OUT PUCHAR Buffer
   );

VOID
VioScsiSaveInquiryData(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    );

GUID VioScsiWmiExtendedInfoGuid = VioScsiWmi_ExtendedInfo_Guid;
GUID VioScsiWmiAdapterInformationQueryGuid = MS_SM_AdapterInformationQueryGuid;
GUID VioScsiWmiPortInformationMethodsGuid = MS_SM_PortInformationMethodsGuid;

SCSIWMIGUIDREGINFO VioScsiGuidList[] =
{
   { &VioScsiWmiExtendedInfoGuid, 1, 0 },
   { &VioScsiWmiAdapterInformationQueryGuid, 1, 0 },
   { &VioScsiWmiPortInformationMethodsGuid, 1, 0 },
};

#define VioScsiGuidCount (sizeof(VioScsiGuidList) / sizeof(SCSIWMIGUIDREGINFO))


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
        IsCrashDumpMode = TRUE;
//        virtioDebugLevel = 0xff;
//        nViostorDebugLevel = TRACE_LEVEL_VERBOSE;
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("DriverEntry: Crash dump mode\n"));
    }

    RtlZeroMemory(&hwInitData, sizeof(HW_INITIALIZATION_DATA));

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

    /* Virtio doesn't specify the number of BARs used by the device; it may
     * be one, it may be more. PCI_TYPE0_ADDRESSES, the theoretical maximum
     * on PCI, is a safe upper bound.
     */
    hwInitData.NumberOfAccessRanges     = PCI_TYPE0_ADDRESSES;
    hwInitData.MapBuffers               = STOR_MAP_NON_READ_WRITE_BUFFERS;

#if (NTDDI_VERSION > NTDDI_WIN7)
    /* Specify support/use SRB Extension for Windows 8 and up */
    hwInitData.SrbTypeFlags = SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK;
#endif

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
    PVOID              uncachedExtensionVa;
    USHORT             queueLength = 0;
    ULONG              Size;
    ULONG              HeapSize;
    ULONG              extensionSize;
    ULONG              index;
    ULONG              num_cpus;
    ULONG              max_cpus;
    ULONG              max_queues;

    UNREFERENCED_PARAMETER( HwContext );
    UNREFERENCED_PARAMETER( BusInformation );
    UNREFERENCED_PARAMETER( ArgumentString );
    UNREFERENCED_PARAMETER( Again );

ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    RtlZeroMemory(adaptExt, sizeof(ADAPTER_EXTENSION));

    adaptExt->dump_mode  = IsCrashDumpMode;
    adaptExt->hba_id     = HBA_ID;
    ConfigInfo->Master                      = TRUE;
    ConfigInfo->ScatterGather               = TRUE;
    ConfigInfo->DmaWidth                    = Width32Bits;
    ConfigInfo->Dma32BitAddresses           = TRUE;
#if (NTDDI_VERSION > NTDDI_WIN7)
    ConfigInfo->Dma64BitAddresses           = SCSI_DMA64_MINIPORT_FULL64BIT_SUPPORTED;
#else
    ConfigInfo->Dma64BitAddresses           = TRUE;
#endif
    ConfigInfo->WmiDataProvider             = TRUE;
    ConfigInfo->AlignmentMask               = 0x3;
    ConfigInfo->MapBuffers                  = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->SynchronizationModel        = StorSynchronizeFullDuplex;
#if (MSI_SUPPORTED == 1)
    ConfigInfo->HwMSInterruptRoutine        = VioScsiMSInterrupt;
    ConfigInfo->InterruptSynchronizationMode=InterruptSynchronizePerMessage;
#endif

    VioScsiWmiInitialize(DeviceExtension);

    if (!InitHW(DeviceExtension, ConfigInfo)) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Cannot initialize HardWare\n"));
        return SP_RETURN_NOT_FOUND;
    }

    GetScsiConfig(DeviceExtension);

    ConfigInfo->NumberOfBuses               = 1;//(UCHAR)adaptExt->num_queues;
    ConfigInfo->MaximumNumberOfTargets      = min((UCHAR)adaptExt->scsi_config.max_target, 255/*SCSI_MAXIMUM_TARGETS_PER_BUS*/);
    ConfigInfo->MaximumNumberOfLogicalUnits = min((UCHAR)adaptExt->scsi_config.max_lun, SCSI_MAXIMUM_LUNS_PER_TARGET);

    if(adaptExt->dump_mode) {
        ConfigInfo->NumberOfPhysicalBreaks  = 8;
    } else {
        ConfigInfo->NumberOfPhysicalBreaks  = min((MAX_PHYS_SEGMENTS + 1), adaptExt->scsi_config.seg_max);
    }
    ConfigInfo->MaximumTransferLength       = 0x00FFFFFF;

#if (NTDDI_VERSION >= NTDDI_WIN7)
    num_cpus = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    max_cpus = KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
#else
    num_cpus = KeQueryActiveProcessorCount(NULL);
    max_cpus = KeQueryMaximumProcessorCount();
#endif
    adaptExt->num_queues = adaptExt->scsi_config.num_queues;

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
#if (NTDDI_VERSION > NTDDI_WIN7)
        adaptExt->num_queues = num_cpus;
#else
        adaptExt->num_queues = 1;
#endif
    }

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Queues %d CPUs %d\n", adaptExt->num_queues, num_cpus));

    /* Figure out the maximum number of queues we will ever need to set up. Note that this may
     * be higher than adaptExt->num_queues, because the driver may be reinitialized by calling
     * VioScsiFindAdapter again with more CPUs enabled. Unfortunately StorPortGetUncachedExtension
     * only allocates when called for the first time so we need to always use this upper bound.
     */
    max_queues = min(max_cpus, adaptExt->scsi_config.num_queues);
    if (adaptExt->num_queues > max_queues) {
	RhelDbgPrint(TRACE_LEVEL_WARNING, ("Multiqueue can only use at most one queue per cpu."));
        adaptExt->num_queues = max_queues;
    }
    

    /* This function is our only chance to allocate memory for the driver; allocations are not
     * possible later on. Even worse, the only allocation mechanism guaranteed to work in all
     * cases is StorPortGetUncachedExtension, which gives us one block of physically contiguous
     * pages.
     *
     * Allocations that need to be page-aligned will be satisfied from this one block starting
     * at the first page-aligned offset, up to adaptExt->pageAllocationSize computed below. Other
     * allocations will be cache-line-aligned, of total size adaptExt->poolAllocationSize, also
     * computed below.
     */
    adaptExt->pageAllocationSize = 0;
    adaptExt->poolAllocationSize = 0;
    adaptExt->pageOffset = 0;
    adaptExt->poolOffset = 0;
    Size = 0;
    for (index = VIRTIO_SCSI_CONTROL_QUEUE; index < max_queues + VIRTIO_SCSI_REQUEST_QUEUE_0; ++index) {
        virtio_query_queue_allocation(&adaptExt->vdev, index, &queueLength, &Size, &HeapSize);
        if (Size == 0) {
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
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(VirtIOSCSIEventNode) * 8);
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(STOR_DPC) * max_queues);
    }
    if (max_queues + VIRTIO_SCSI_REQUEST_QUEUE_0 > MAX_QUEUES_PER_DEVICE_DEFAULT)
    {
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(
            (max_queues + VIRTIO_SCSI_REQUEST_QUEUE_0) * virtio_get_queue_descriptor_size());
    }

#if (INDIRECT_SUPPORTED == 1)
    if(!adaptExt->dump_mode) {
        adaptExt->indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);
    }
#else
    adaptExt->indirect = 0;
#endif

    if(adaptExt->indirect) {
        adaptExt->queue_depth = max(20, (queueLength / 4));
    } else {
        adaptExt->queue_depth = queueLength / ConfigInfo->NumberOfPhysicalBreaks - 1;
    }
#if (NTDDI_VERSION > NTDDI_WIN7)
    ConfigInfo->MaxIOsPerLun = adaptExt->queue_depth * adaptExt->num_queues;
    ConfigInfo->InitialLunQueueDepth = ConfigInfo->MaxIOsPerLun;
    if (ConfigInfo->MaxIOsPerLun * ConfigInfo->MaximumNumberOfTargets > ConfigInfo->MaxNumberOfIO) {
        ConfigInfo->MaxNumberOfIO = ConfigInfo->MaxIOsPerLun * ConfigInfo->MaximumNumberOfTargets;
    }
#else
    // Prior to win8, lun queue depth must be at most 254.
    adaptExt->queue_depth = min(254, adaptExt->queue_depth);
#endif

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("breaks_number = %x  queue_depth = %x\n",
                ConfigInfo->NumberOfPhysicalBreaks,
                adaptExt->queue_depth));

    extensionSize = PAGE_SIZE + adaptExt->pageAllocationSize + adaptExt->poolAllocationSize;
    uncachedExtensionVa = StorPortGetUncachedExtension(DeviceExtension, ConfigInfo, extensionSize);
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("StorPortGetUncachedExtension uncachedExtensionVa = %p allocation size = %d\n", uncachedExtensionVa, extensionSize));
    if (!uncachedExtensionVa) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Can't get uncached extension allocation size = %d\n", extensionSize));
        return SP_RETURN_ERROR;
    }

    /* At this point we have all the memory we're going to need. We lay it out as follows.
     * Note that StorPortGetUncachedExtension tends to return page-aligned memory so the
     * padding1 region will typically be empty and the size of padding2 equal to PAGE_SIZE.
     *
     * uncachedExtensionVa    pageAllocationVa         poolAllocationVa
     * +----------------------+------------------------+--------------------------+----------------------+
     * | \ \ \ \ \ \ \ \ \ \  |<= pageAllocationSize =>|<=  poolAllocationSize  =>| \ \ \ \ \ \ \ \ \ \  |
     * |  \ \  padding1 \ \ \ |                        |                          |  \ \  padding2 \ \ \ |
     * | \ \ \ \ \ \ \ \ \ \  |    page-aligned area   | pool area for cache-line | \ \ \ \ \ \ \ \ \ \  |
     * |  \ \ \ \ \ \ \ \ \ \ |                        | aligned allocations      |  \ \ \ \ \ \ \ \ \ \ |
     * +----------------------+------------------------+--------------------------+----------------------+
     * |<=====================================  extensionSize  =========================================>|
     */
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
                             sizeof(GROUP_AFFINITY) * (adaptExt->num_queues + 3),
                             VIOSCSI_POOL_TAG,
                             (PVOID*)&adaptExt->pmsg_affinity);
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("pmsg_affinity = %p Status = %lu\n",adaptExt->pmsg_affinity, Status));
    }
#endif

EXIT_FN();
    return SP_RETURN_FOUND;
}

BOOLEAN
VioScsiPassiveInitializeRoutine(
    IN PVOID DeviceExtension
)
{
    ULONG index;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
ENTER_FN();

    for (index = 0; index < adaptExt->num_queues; ++index) {
        StorPortInitializeDpc(DeviceExtension,
            &adaptExt->dpc[index],
            VioScsiCompleteDpcRoutine);
    }
    adaptExt->dpc_ok = TRUE;
EXIT_FN();
    return TRUE;
}

static BOOLEAN InitializeVirtualQueues(PADAPTER_EXTENSION adaptExt, ULONG numQueues)
{
    ULONG index;
    NTSTATUS status;
    BOOLEAN useEventIndex = CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX);

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

PVOID
VioScsiPoolAlloc(
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
    } else {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("Ran out of memory in VioScsiPoolAlloc(%Id)\n", size));
        return NULL;
    }
}

BOOLEAN
VioScsiHwInitialize(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONG              i;
    ULONGLONG          guestFeatures = 0;
    ULONG              index;

    PERF_CONFIGURATION_DATA perfData = { 0 };
    ULONG              status = STOR_STATUS_SUCCESS;
#if (MSI_SUPPORTED == 1)
    MESSAGE_INTERRUPT_INFORMATION msi_info = { 0 };
#endif
    
ENTER_FN();
    if (CHECKBIT(adaptExt->features, VIRTIO_F_VERSION_1)) {
        guestFeatures |= (1ULL << VIRTIO_F_VERSION_1);
    }
    if (CHECKBIT(adaptExt->features, VIRTIO_F_ANY_LAYOUT)) {
        guestFeatures |= (1ULL << VIRTIO_F_ANY_LAYOUT);
    }
    if (CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX)) {
        guestFeatures |= (1ULL << VIRTIO_RING_F_EVENT_IDX);
    }
    if (CHECKBIT(adaptExt->features, VIRTIO_SCSI_F_CHANGE)) {
        guestFeatures |= (1ULL << VIRTIO_SCSI_F_CHANGE);
    }
    if (CHECKBIT(adaptExt->features, VIRTIO_SCSI_F_HOTPLUG)) {
        guestFeatures |= (1ULL << VIRTIO_SCSI_F_HOTPLUG);
    }
    if (!NT_SUCCESS(virtio_set_features(&adaptExt->vdev, guestFeatures))) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, ("virtio_set_features failed\n"));
        return FALSE;
    }

    adaptExt->msix_vectors = 0;
    adaptExt->pageOffset = 0;
    adaptExt->poolOffset = 0;

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

    if (!adaptExt->dump_mode && adaptExt->msix_vectors > 0) {
        if (adaptExt->msix_vectors >= adaptExt->num_queues + 3) {
            /* initialize queues with a MSI vector per queue */
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Using a unique MSI vector per queue\n"));
            adaptExt->msix_one_vector = FALSE;
        } else {
            /* if we don't have enough vectors, use one for all queues */
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Using one MSI vector for all queues\n"));
            adaptExt->msix_one_vector = TRUE;
        }
        if (!InitializeVirtualQueues(adaptExt, adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0)) {
            return FALSE;
        }

        for (index = VIRTIO_SCSI_CONTROL_QUEUE; index < adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0; ++index) {
              if ((adaptExt->num_queues > 1) &&
                  (index >= VIRTIO_SCSI_REQUEST_QUEUE_0)) {
                  if (!CHECKFLAG(adaptExt->perfFlags, STOR_PERF_ADV_CONFIG_LOCALITY)) {
                      adaptExt->cpu_to_vq_map[index - VIRTIO_SCSI_REQUEST_QUEUE_0] = (UCHAR)(index - VIRTIO_SCSI_REQUEST_QUEUE_0);
                  }
#if (NTDDI_VERSION > NTDDI_WIN7)
                  status = StorPortInitializeSListHead(DeviceExtension, &adaptExt->srb_list[index - VIRTIO_SCSI_REQUEST_QUEUE_0]); 
                  if (status != STOR_STATUS_SUCCESS) {
                     RhelDbgPrint(TRACE_LEVEL_FATAL, ("StorPortInitializeSListHead failed with status  0x%x\n", status));
                  }
#endif
              }
        }
    }
    else
#else
    adaptExt->num_queues = 1;
#endif
    {
        /* initialize queues with no MSI interrupts */
        adaptExt->msix_enabled = FALSE;
        if (!InitializeVirtualQueues(adaptExt, adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0)) {
            return FALSE;
        }
    }

    if (!adaptExt->dump_mode) {
        /* we don't get another chance to call StorPortEnablePassiveInitialization and initialize
         * DPCs if the adapter is being restarted, so leave our datastructures alone on restart
         */
        if (adaptExt->dpc == NULL) {
            adaptExt->tmf_cmd.SrbExtension = (PSRB_EXTENSION)VioScsiPoolAlloc(DeviceExtension, sizeof(SRB_EXTENSION));
            adaptExt->events = (PVirtIOSCSIEventNode)VioScsiPoolAlloc(DeviceExtension, sizeof(VirtIOSCSIEventNode) * 8);
            adaptExt->dpc = (PSTOR_DPC)VioScsiPoolAlloc(DeviceExtension, sizeof(STOR_DPC) * adaptExt->num_queues);
        }
    }

    if (!adaptExt->dump_mode && CHECKBIT(adaptExt->features, VIRTIO_SCSI_F_HOTPLUG)) {
        PVirtIOSCSIEventNode events = adaptExt->events;
        for (i = 0; i < 8; i++) {
           if (!KickEvent(DeviceExtension, (PVOID)(&events[i]))) {
                RhelDbgPrint(TRACE_LEVEL_FATAL, ("Can't add event %d\n", i));
           }
        }
    }
    if (!adaptExt->dump_mode)
    {
        if ((adaptExt->num_queues > 1) && (adaptExt->perfFlags == 0)) {
#if 1
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
                    perfData.FirstRedirectionMessageNumber = 3;
                    perfData.LastRedirectionMessageNumber = perfData.FirstRedirectionMessageNumber + adaptExt->num_queues - 1;
                    if ((adaptExt->pmsg_affinity != NULL) && CHECKFLAG(perfData.Flags, STOR_PERF_ADV_CONFIG_LOCALITY)) {
                        RtlZeroMemory((PCHAR)adaptExt->pmsg_affinity, sizeof (GROUP_AFFINITY)* (adaptExt->num_queues + 3));
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
                    for (msg = 0; msg < adaptExt->num_queues + 3; msg++) {
                        ga = &adaptExt->pmsg_affinity[msg];
                        if ( ga->Mask > 0 && msg > 2) {
                            cpu = RtlFindLeastSignificantBit((ULONGLONG)ga->Mask);
                            adaptExt->cpu_to_vq_map[cpu] = msg - 3;
                            RhelDbgPrint(TRACE_LEVEL_FATAL, ("msg = %d, mask = 0x%lx group = %hu cpu = %hu vq = %hu\n", msg, ga->Mask, ga->Group, cpu, adaptExt->cpu_to_vq_map[cpu]));
                        }
                    }
                }
            }
            else {
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("%s StorPortInitializePerfOpts TRUE status = 0x%x\n", __FUNCTION__, status));
            }
#endif
        }
        if ((adaptExt->num_queues > 1) && !adaptExt->dpc_ok && !StorPortEnablePassiveInitialization(DeviceExtension, VioScsiPassiveInitializeRoutine)) {
            RhelDbgPrint(TRACE_LEVEL_FATAL, ("%s StorPortEnablePassiveInitialization FAILED\n", __FUNCTION__));
            return FALSE;
        }
    }

    virtio_device_ready(&adaptExt->vdev);
EXIT_FN();
    return TRUE;
}

BOOLEAN
VioScsiHwReinitialize(
    IN PVOID DeviceExtension
    )
{
    /* The adapter is being restarted and we need to bring it back up without
     * running any passive-level code. Note that VioScsiFindAdapter is *not*
     * called on restart.
     */
    if (!InitVirtIODevice(DeviceExtension)) {
        return FALSE;
    }
    return VioScsiHwInitialize(DeviceExtension);
}

BOOLEAN
VioScsiStartIo(
    IN PVOID DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
ENTER_FN();
    if (PreProcessRequest(DeviceExtension, (PSRB_TYPE)Srb))
    {
        CompleteRequest(DeviceExtension, (PSRB_TYPE)Srb);
    }
    else
    {
        return SendSRB(DeviceExtension, (PSRB_TYPE)Srb);
    }
EXIT_FN();
    return TRUE;
}

VOID
//FORCEINLINE
HandleResponse(PVOID DeviceExtension, PVirtIOSCSICmd cmd) {
    PSRB_TYPE Srb = (PSRB_TYPE)(cmd->srb);
    PSRB_EXTENSION srbExt = SRB_EXTENSION(Srb);
    VirtIOSCSICmdResp *resp = &cmd->resp.cmd;
    UCHAR senseInfoBufferLength = 0;
    PVOID senseInfoBuffer = NULL;
    UCHAR srbStatus = SRB_STATUS_SUCCESS;
    ULONG srbDataTransferLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    switch (resp->response) {
    case VIRTIO_SCSI_S_OK:
        SRB_SET_SCSI_STATUS(Srb, resp->status);
        srbStatus = (resp->status == SCSISTAT_GOOD) ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR;
        break;
    case VIRTIO_SCSI_S_UNDERRUN:
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_UNDERRUN\n"));
        srbStatus = SRB_STATUS_DATA_OVERRUN;
        break;
    case VIRTIO_SCSI_S_ABORTED:
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_ABORTED\n"));
        srbStatus = SRB_STATUS_ABORTED;
        break;
    case VIRTIO_SCSI_S_BAD_TARGET:
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_BAD_TARGET\n"));
        srbStatus = SRB_STATUS_INVALID_TARGET_ID;
        break;
    case VIRTIO_SCSI_S_RESET:
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_RESET\n"));
        srbStatus = SRB_STATUS_BUS_RESET;
        break;
    case VIRTIO_SCSI_S_BUSY:
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_BUSY\n"));
        srbStatus = SRB_STATUS_BUSY;
        break;
    case VIRTIO_SCSI_S_TRANSPORT_FAILURE:
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_TRANSPORT_FAILURE\n"));
        srbStatus = SRB_STATUS_ERROR;
        break;
    case VIRTIO_SCSI_S_TARGET_FAILURE:
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_TARGET_FAILURE\n"));
        srbStatus = SRB_STATUS_ERROR;
        break;
    case VIRTIO_SCSI_S_NEXUS_FAILURE:
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_NEXUS_FAILURE\n"));
        srbStatus = SRB_STATUS_ERROR;
        break;
    case VIRTIO_SCSI_S_FAILURE:
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("VIRTIO_SCSI_S_FAILURE\n"));
        srbStatus = SRB_STATUS_ERROR;
        break;
    default:
        srbStatus = SRB_STATUS_ERROR;
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Unknown response %d\n", resp->response));
        break;
    }
    if (srbStatus == SRB_STATUS_SUCCESS &&
        resp->resid &&
        srbDataTransferLen > resp->resid)
    {
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, srbDataTransferLen - resp->resid);
        srbStatus = SRB_STATUS_DATA_OVERRUN;
    }
    else if (srbStatus != SRB_STATUS_SUCCESS)
    {
        SRB_GET_SENSE_INFO(Srb, senseInfoBuffer, senseInfoBufferLength);
        if (senseInfoBufferLength >= FIELD_OFFSET(SENSE_DATA, CommandSpecificInformation)) {
            RtlCopyMemory(senseInfoBuffer, resp->sense,
                min(resp->sense_len, senseInfoBufferLength));
            if (srbStatus == SRB_STATUS_ERROR) {
                srbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }
        }
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, 0);
    }
    else if (srbExt && srbExt->Xfer && srbDataTransferLen > srbExt->Xfer)
    {
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, srbExt->Xfer);
        srbStatus = SRB_STATUS_DATA_OVERRUN;
    }
    SRB_SET_SRB_STATUS(Srb, srbStatus);
    CompleteRequest(DeviceExtension, Srb);
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
    PSRB_TYPE           Srb;
    ULONG               intReason = 0;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, ("%s (%d)\n", __FUNCTION__, KeGetCurrentIrql()));
    intReason = virtio_read_isr_status(&adaptExt->vdev);

    if (intReason == 1 || adaptExt->dump_mode) {
        struct virtqueue *vq = adaptExt->vq[VIRTIO_SCSI_REQUEST_QUEUE_0];
        isInterruptServiced = TRUE;

        virtqueue_disable_cb(vq);
        do {
            while ((cmd = (PVirtIOSCSICmd)virtqueue_get_buf(vq, &len)) != NULL) {
                HandleResponse(DeviceExtension, cmd);
            }
        } while (!virtqueue_enable_cb(vq));

        if (adaptExt->tmf_infly) {
           while((cmd = (PVirtIOSCSICmd)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_CONTROL_QUEUE], &len)) != NULL) {
              VirtIOSCSICtrlTMFResp *resp;
              Srb = (PSRB_TYPE)cmd->srb;
              ASSERT(Srb == (PSRB_TYPE)&adaptExt->tmf_cmd.Srb);
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
static BOOLEAN
VioScsiMSInterruptWorker(
    IN PVOID  DeviceExtension,
    IN ULONG  MessageID
    )
{
    PVirtIOSCSICmd      cmd;
    PVirtIOSCSIEventNode evtNode;
    unsigned int        len;
    PADAPTER_EXTENSION  adaptExt;
    PSRB_TYPE           Srb = NULL;
    ULONG               intReason = 0;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 ("<--->%s : MessageID 0x%x\n", __FUNCTION__, MessageID));

    if (MessageID > 2)
    {
        DispatchQueue(DeviceExtension, MessageID);
        return TRUE;
    }
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
              Srb = (PSRB_TYPE)(cmd->srb);
              ASSERT(Srb == (PSRB_TYPE)&adaptExt->tmf_cmd.Srb);
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
    return FALSE;
}

BOOLEAN
VioScsiMSInterrupt(
    IN PVOID  DeviceExtension,
    IN ULONG  MessageID
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    BOOLEAN isInterruptServiced = FALSE;
    ULONG i;

    if (!adaptExt->msix_one_vector) {
        /* Each queue has its own vector, this is the fast and common case */
        return VioScsiMSInterruptWorker(DeviceExtension, MessageID);
    }

    /* Fall back to checking all queues */
    for (i = 0; i < adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0; i++) {
        if (virtqueue_has_buf(adaptExt->vq[i])) {
            isInterruptServiced |= VioScsiMSInterruptWorker(DeviceExtension, i + 1);
        }
    }
    return isInterruptServiced;
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
    BOOLEAN SupportedControlTypes[5] = {TRUE, TRUE, TRUE, FALSE, FALSE};

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
                SupportedControlTypes[Index];
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
        ShutDown(DeviceExtension);
        if (!VioScsiHwReinitialize(DeviceExtension))
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
    UCHAR                 TargetId;
    UCHAR                 Lun;
#if (NTDDI_VERSION >= NTDDI_WIN7)
    PROCESSOR_NUMBER ProcNumber;
    ULONG processor = KeGetCurrentProcessorNumberEx(&ProcNumber);
    ULONG cpu = ProcNumber.Number;
#else
    ULONG cpu = KeGetCurrentProcessorNumber();
#endif

ENTER_FN();
    cdb      = SRB_CDB(Srb);
    srbExt   = SRB_EXTENSION(Srb);
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    TargetId = SRB_TARGET_ID(Srb);
    Lun      = SRB_LUN(Srb);

    if( (SRB_PATH_ID(Srb) > (UCHAR)adaptExt->num_queues) ||
        (TargetId >= adaptExt->scsi_config.max_target) ||
        (Lun >= adaptExt->scsi_config.max_lun) ) {
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_NO_DEVICE);
        StorPortNotification(RequestComplete,
                             DeviceExtension,
                             Srb);
        return FALSE;
    }

//    RhelDbgPrint(TRACE_LEVEL_FATAL, ("<-->%s (%d::%d::%d)\n", DbgGetScsiOpStr(Srb), SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb)));
//RhelDbgPrint(TRACE_LEVEL_FATAL, ("<-->%s (%d::%d::%d on %d)\n", DbgGetScsiOpStr(Srb), SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb), cpu));

    RtlZeroMemory(srbExt, sizeof(*srbExt));
    srbExt->Srb = Srb;
    srbExt->cpu = (UCHAR)cpu;
    cmd = &srbExt->cmd;
    cmd->srb = (PVOID)Srb;
    cmd->req.cmd.lun[0] = 1;
    cmd->req.cmd.lun[1] = TargetId;
    cmd->req.cmd.lun[2] = 0;
    cmd->req.cmd.lun[3] = Lun;
    cmd->req.cmd.tag = (ULONG_PTR)(Srb);
    cmd->req.cmd.task_attr = VIRTIO_SCSI_S_SIMPLE;
    cmd->req.cmd.prio = 0;
    cmd->req.cmd.crn = 0;
    if (cdb != NULL) {
        RtlCopyMemory(cmd->req.cmd.cdb, cdb, min(VIRTIO_SCSI_CDB_SIZE, SRB_CDB_LENGTH(Srb)));
    }

    sgElement = 0;
    srbExt->sg[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->req.cmd, &fragLen);
    srbExt->sg[sgElement].length   = sizeof(cmd->req.cmd);
    sgElement++;

    sgList = StorPortGetScatterGatherList(DeviceExtension, Srb);
    if (sgList)
    {
        sgMaxElements = sgList->NumberOfElements;

        if((SRB_FLAGS(Srb) & SRB_FLAGS_DATA_OUT) == SRB_FLAGS_DATA_OUT) {
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

        if((SRB_FLAGS(Srb) & SRB_FLAGS_DATA_OUT) != SRB_FLAGS_DATA_OUT) {
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


VOID
FORCEINLINE
DispatchQueue(
    IN PVOID DeviceExtension,
    IN ULONG MessageID
)
{
    PADAPTER_EXTENSION  adaptExt;
ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if ((adaptExt->num_queues > 1) && adaptExt->dpc_ok && MessageID > 0) {
        StorPortIssueDpc(DeviceExtension,
            &adaptExt->dpc[MessageID-3],
            ULongToPtr(MessageID),
            ULongToPtr(MessageID));
EXIT_FN();
        return;
    }
    ProcessQueue(DeviceExtension, MessageID, TRUE);
EXIT_FN();
}

VOID
ProcessQueue(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN BOOLEAN isr
)
{
    PVirtIOSCSICmd      cmd;
    unsigned int        len;
    PADAPTER_EXTENSION  adaptExt;
    ULONG               msg = MessageID - 3;
    STOR_LOCK_HANDLE    queueLock = { 0 };
    struct virtqueue    *vq;
    BOOLEAN             handleResponseInline;
#ifdef USE_WORK_ITEM
#if (NTDDI_VERSION > NTDDI_WIN7)
    UCHAR               cnt = 0;
#endif
#endif
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
ENTER_FN();
#ifdef USE_WORK_ITEM
    handleResponseInline = (adaptExt->num_queues == 1);
#else
    handleResponseInline = TRUE;
#endif
    vq = adaptExt->vq[VIRTIO_SCSI_REQUEST_QUEUE_0 + msg];

    VioScsiVQLock(DeviceExtension, MessageID, &queueLock, isr);

    virtqueue_disable_cb(vq);
    do {
        while ((cmd = (PVirtIOSCSICmd)virtqueue_get_buf(vq, &len)) != NULL) {
            if (handleResponseInline) {
                HandleResponse(DeviceExtension, cmd);
            }
#ifdef USE_WORK_ITEM
            else {
#if (NTDDI_VERSION > NTDDI_WIN7)
                PSRB_TYPE Srb = (PSRB_TYPE)(cmd->srb);
                PSRB_EXTENSION srbExt = SRB_EXTENSION(Srb);
                ULONG status = STOR_STATUS_SUCCESS;
                PSTOR_SLIST_ENTRY Result = NULL;
                VioScsiVQUnlock(DeviceExtension, MessageID, &queueLock, isr);
                srbExt->priv = (PVOID)cmd;
                status = StorPortInterlockedPushEntrySList(DeviceExtension, &adaptExt->srb_list[msg], &srbExt->list_entry, &Result);
                if (status != STOR_STATUS_SUCCESS) {
                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("StorPortInterlockedPushEntrySList failed with status 0x%x\n\n", status));
                }
                cnt++;
                VioScsiVQLock(DeviceExtension, MessageID, &queueLock, isr);
#else
                NT_ASSERT(0);
#endif
            }
#endif
        }
    } while (!virtqueue_enable_cb(vq));

    VioScsiVQUnlock(DeviceExtension, MessageID, &queueLock, isr);

#ifdef USE_WORK_ITEM
#if (NTDDI_VERSION > NTDDI_WIN7)
    if (cnt) {
       ULONG status = STOR_STATUS_SUCCESS;
       PVOID Worker = NULL;
       status = StorPortInitializeWorker(DeviceExtension, &Worker);
       if (status != STOR_STATUS_SUCCESS) {
          RhelDbgPrint(TRACE_LEVEL_FATAL, ("StorPortInitializeWorker failed with status 0x%x\n\n", status));
//FIXME   VioScsiWorkItemCallback
          return;
       }
       status = StorPortQueueWorkItem(DeviceExtension, &VioScsiWorkItemCallback, Worker, ULongToPtr(MessageID));
       if (status != STOR_STATUS_SUCCESS) {
          RhelDbgPrint(TRACE_LEVEL_FATAL, ("StorPortQueueWorkItem failed with status 0x%x\n\n", status));
//FIXME   VioScsiWorkItemCallback
       }
    }
#endif
#endif
EXIT_FN();
}

VOID
VioScsiCompleteDpcRoutine(
    IN PSTOR_DPC  Dpc,
    IN PVOID Context,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )
{
    ULONG MessageId;

ENTER_FN();
    MessageId = PtrToUlong(SystemArgument1);
    ProcessQueue(Context, MessageId, FALSE);
EXIT_FN();
}

BOOLEAN
FORCEINLINE
PreProcessRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    PADAPTER_EXTENSION adaptExt;

ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    switch (SRB_FUNCTION(Srb)) {
        case SRB_FUNCTION_PNP:
        case SRB_FUNCTION_POWER:
        case SRB_FUNCTION_RESET_BUS:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_LOGICAL_UNIT: {
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
            return TRUE;
        }
        case SRB_FUNCTION_WMI:
            VioScsiWmiSrb(DeviceExtension, Srb);
            return TRUE;
        case SRB_FUNCTION_IO_CONTROL:
            VioScsiIoControl(DeviceExtension, Srb);
            return TRUE;
    }
EXIT_FN();
    return FALSE;
}

VOID
PostProcessRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    PCDB                  cdb;
    PADAPTER_EXTENSION    adaptExt;

ENTER_FN();
    if (SRB_FUNCTION(Srb) != SRB_FUNCTION_EXECUTE_SCSI) {
        return;
    }
    cdb      = SRB_CDB(Srb);
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    switch (cdb->CDB6GENERIC.OperationCode)
    {
        case SCSIOP_READ_CAPACITY:
        case SCSIOP_READ_CAPACITY16:
           if (!StorPortSetDeviceQueueDepth( DeviceExtension, SRB_PATH_ID(Srb),
                                     SRB_TARGET_ID(Srb), SRB_LUN(Srb),
                                     adaptExt->queue_depth)) {
              RhelDbgPrint(TRACE_LEVEL_ERROR, ("StorPortSetDeviceQueueDepth(%p, %x) failed.\n",
                          DeviceExtension,
                          adaptExt->queue_depth));
           }
           break;
        case SCSIOP_INQUIRY:
            VioScsiSaveInquiryData(DeviceExtension, Srb);
           break;
        default:
           break;

    }
EXIT_FN();
}

VOID
CompleteRequest(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
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
    ULONG sz = 0;
    RtlZeroMemory( &logEvent, sizeof(logEvent) );
    logEvent.InterfaceRevision         = STOR_CURRENT_LOG_INTERFACE_REVISION;
    logEvent.Size                      = sizeof(logEvent);
    logEvent.EventAssociation          = StorEventAdapterAssociation;
    logEvent.StorportSpecificErrorCode = TRUE;
    logEvent.ErrorCode                 = ErrorCode;
    logEvent.DumpDataSize              = sizeof(UniqueId);
    logEvent.DumpData                  = &UniqueId;
    StorPortLogSystemEvent( DeviceExtension, &logEvent, &sz );
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

VOID
VioScsiWmiInitialize(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION    adaptExt;
    PSCSI_WMILIB_CONTEXT WmiLibContext;
ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    WmiLibContext = (PSCSI_WMILIB_CONTEXT)(&(adaptExt->WmiLibContext));

    WmiLibContext->GuidList = VioScsiGuidList;
    WmiLibContext->GuidCount = VioScsiGuidCount;
    WmiLibContext->QueryWmiRegInfo = VioScsiQueryWmiRegInfo;
    WmiLibContext->QueryWmiDataBlock = VioScsiQueryWmiDataBlock;
    WmiLibContext->SetWmiDataItem = NULL;
    WmiLibContext->SetWmiDataBlock = NULL;
    WmiLibContext->ExecuteWmiMethod = VioScsiExecuteWmiMethod;
    WmiLibContext->WmiFunctionControl = NULL;
EXIT_FN();
}

VOID
VioScsiWmiSrb(
    IN PVOID DeviceExtension,
    IN OUT PSRB_TYPE Srb
    )
{
    UCHAR status;
    SCSIWMI_REQUEST_CONTEXT requestContext = {0};
    ULONG retSize;
    PADAPTER_EXTENSION    adaptExt;
    PSRB_WMI_DATA pSrbWmi = SRB_WMI_DATA(Srb);

ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    ASSERT(SRB_FUNCTION(Srb) == SRB_FUNCTION_WMI);
    ASSERT(SRB_LENGTH(Srb)  == sizeof(SCSI_WMI_REQUEST_BLOCK));
    ASSERT(SRB_DATA_TRANSFER_LENGTH(Srb) >= sizeof(ULONG));
    ASSERT(SRB_DATA_BUFFER(Srb));

    if (!(pSrbWmi->WMIFlags & SRB_WMI_FLAGS_ADAPTER_REQUEST))
    {
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, 0);
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
    }
    else
    {
        requestContext.UserContext = Srb;
        (VOID)ScsiPortWmiDispatchFunction(&adaptExt->WmiLibContext,
                                                pSrbWmi->WMISubFunction,
                                                DeviceExtension,
                                                &requestContext,
                                                pSrbWmi->DataPath,
                                                SRB_DATA_TRANSFER_LENGTH(Srb),
                                                SRB_DATA_BUFFER(Srb));

        retSize =  ScsiPortWmiGetReturnSize(&requestContext);
        status =  ScsiPortWmiGetReturnStatus(&requestContext);

        SRB_SET_DATA_TRANSFER_LENGTH(Srb, retSize);
        SRB_SET_SRB_STATUS(Srb, status);
    }

EXIT_FN();
}

VOID
VioScsiIoControl(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    )
{
    PSRB_IO_CONTROL srbControl;
    PVOID           srbDataBuffer = SRB_DATA_BUFFER(Srb);
    PADAPTER_EXTENSION    adaptExt;

ENTER_FN();
    RhelDbgPrint(TRACE_LEVEL_FATAL, ("<-->VioScsiIoControl\n"));

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    srbControl = (PSRB_IO_CONTROL)srbDataBuffer;

    switch (srbControl->ControlCode) {
        case IOCTL_SCSI_MINIPORT_NOT_QUORUM_CAPABLE:
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_ERROR);
			RhelDbgPrint(TRACE_LEVEL_FATAL, ("<--> Signature = %02x %02x %02x %02x %02x %02x %02x %02x\n",
				srbControl->Signature[0], srbControl->Signature[1], srbControl->Signature[2], srbControl->Signature[3],
				srbControl->Signature[4], srbControl->Signature[5], srbControl->Signature[6], srbControl->Signature[7]));
			RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("<-->IOCTL_SCSI_MINIPORT_NOT_QUORUM_CAPABLE\n"));
            break;
        default:
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_INVALID_REQUEST);
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("<-->Unsupport control code 0x%x\n", srbControl->ControlCode));
            break;
    }
EXIT_FN();
}

VOID
VioScsiSaveInquiryData(
    IN PVOID  DeviceExtension,
    IN OUT PSRB_TYPE Srb
    )
{
    PVOID           dataBuffer;
    PADAPTER_EXTENSION    adaptExt;
    PCDB cdb;
    PINQUIRYDATA InquiryData;
    ULONG dataLen;

ENTER_FN();
    RhelDbgPrint(TRACE_LEVEL_FATAL, ("<-->VioScsiSaveInquiryData\n"));

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    cdb      = SRB_CDB(Srb);
    dataBuffer = SRB_DATA_BUFFER(Srb);
    InquiryData = (PINQUIRYDATA)dataBuffer;
    dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);
    switch (cdb->CDB6INQUIRY3.PageCode) {
        case VPD_SERIAL_NUMBER: {
            PVPD_SERIAL_NUMBER_PAGE SerialPage;
            SerialPage = (PVPD_SERIAL_NUMBER_PAGE)dataBuffer;
            RhelDbgPrint(TRACE_LEVEL_FATAL, ("VPD_SERIAL_NUMBER PageLength = %d\n", SerialPage->PageLength));
            if (SerialPage->PageLength > 0 && adaptExt->ser_num == NULL) {
                int ln = min (64, SerialPage->PageLength);
                ULONG Status =
                             StorPortAllocatePool(DeviceExtension,
                             ln + 1,
                             VIOSCSI_POOL_TAG,
                             (PVOID*)&adaptExt->ser_num);
                if (NT_SUCCESS(Status)) {
                    StorPortMoveMemory(adaptExt->ser_num, SerialPage->SerialNumber, ln);
                    adaptExt->ser_num[ln] = '\0';
                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("serial number %s\n", adaptExt->ser_num));
                }
            }
            break;
        }
        case VPD_DEVICE_IDENTIFIERS: {
            PVPD_IDENTIFICATION_PAGE IdentificationPage;
            PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr;
            IdentificationPage = (PVPD_IDENTIFICATION_PAGE)dataBuffer;
            if (IdentificationPage->PageLength >= sizeof(VPD_IDENTIFICATION_DESCRIPTOR)) {
                IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)IdentificationPage->Descriptors;
                RhelDbgPrint(TRACE_LEVEL_FATAL, ("VPD_DEVICE_IDENTIFIERS CodeSet = %x IdentifierType = %x IdentifierLength= %d\n", IdentificationDescr->CodeSet, IdentificationDescr->IdentifierType, IdentificationDescr->IdentifierLength));
                if (IdentificationDescr->IdentifierLength >= (sizeof(ULONGLONG)) && (IdentificationDescr->CodeSet == VpdCodeSetBinary)) {
                    REVERSE_BYTES_QUAD(&adaptExt->hba_id, &IdentificationDescr->Identifier[8]);
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x  %llx\n",
                                 IdentificationDescr->Identifier[0], IdentificationDescr->Identifier[1],
                                 IdentificationDescr->Identifier[2], IdentificationDescr->Identifier[3],
                                 IdentificationDescr->Identifier[4], IdentificationDescr->Identifier[5],
                                 IdentificationDescr->Identifier[6], IdentificationDescr->Identifier[7],
                                 IdentificationDescr->Identifier[8], IdentificationDescr->Identifier[9],
                                 IdentificationDescr->Identifier[10], IdentificationDescr->Identifier[11],
                                 IdentificationDescr->Identifier[12], IdentificationDescr->Identifier[13],
                                 IdentificationDescr->Identifier[14], IdentificationDescr->Identifier[15],
                                 adaptExt->hba_id));
                }
            }
            break;
        }
        default:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, ("Unhandled page code %x\n", cdb->CDB6INQUIRY3.PageCode));
            break;
    }

EXIT_FN();
}

void CopyWMIString(void* _pDest, const void* _pSrc, size_t _maxlength)
{
     PUSHORT _pDestTemp = _pDest;
     USHORT  _length = _maxlength - sizeof(USHORT);
                                                                                                                                                 \
     *_pDestTemp++ = _length;
                                                                                                                                                 \
     _length = (USHORT)min(wcslen(_pSrc)*sizeof(WCHAR), _length);
     memcpy(_pDestTemp, _pSrc, _length);
}

BOOLEAN
VioScsiQueryWmiDataBlock(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG OutBufferSize,
    OUT PUCHAR Buffer
    )
{
    ULONG size = 0;
    UCHAR status = SRB_STATUS_SUCCESS;
    PADAPTER_EXTENSION    adaptExt;

ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)Context;

    ASSERT(InstanceIndex == 0);

    switch (GuidIndex)
    {
        case VIOSCSI_SETUP_GUID_INDEX:
        {
            size = sizeof(VioScsiExtendedInfo) - 1;
            if (OutBufferSize < size)
            {
                status = SRB_STATUS_DATA_OVERRUN;
                break;
            }

            VioScsiReadExtendedData(Context,
                                     Buffer);
            *InstanceLengthArray = size;
            status = SRB_STATUS_SUCCESS;
            break;
        }
        case VIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX:
        {
            PMS_SM_AdapterInformationQuery pOutBfr = (PMS_SM_AdapterInformationQuery)Buffer;
            RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->VIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX\n"));
            size = sizeof(MS_SM_AdapterInformationQuery);
            if (OutBufferSize < size)
            {
                status = SRB_STATUS_DATA_OVERRUN;
                break;
            }

            memset(pOutBfr, 0, size);
            pOutBfr->UniqueAdapterId = adaptExt->hba_id;
            pOutBfr->HBAStatus = HBA_STATUS_OK;
            pOutBfr->NumberOfPorts = 1;
            pOutBfr->VendorSpecificID = VENDORID | (PRODUCTID << 16);
            CopyWMIString(pOutBfr->Manufacturer, MANUFACTURER, sizeof(pOutBfr->Manufacturer));
//FIXME
//			CopyWMIString(pOutBfr->SerialNumber, adaptExt->ser_num ? adaptExt->ser_num : SERIALNUMBER, sizeof(pOutBfr->SerialNumber));
			CopyWMIString(pOutBfr->SerialNumber, SERIALNUMBER, sizeof(pOutBfr->SerialNumber));
            CopyWMIString(pOutBfr->Model, MODEL, sizeof(pOutBfr->Model));
            CopyWMIString(pOutBfr->ModelDescription, MODELDESCRIPTION, sizeof(pOutBfr->ModelDescription));
            CopyWMIString(pOutBfr->FirmwareVersion, FIRMWAREVERSION, sizeof(pOutBfr->FirmwareVersion));
            CopyWMIString(pOutBfr->DriverName, DRIVERNAME, sizeof(pOutBfr->DriverName));
            CopyWMIString(pOutBfr->HBASymbolicName, HBASYMBOLICNAME, sizeof(pOutBfr->HBASymbolicName));
            CopyWMIString(pOutBfr->RedundantFirmwareVersion, FIRMWAREVERSION, sizeof(pOutBfr->RedundantFirmwareVersion));
            CopyWMIString(pOutBfr->MfgDomain, MFRDOMAIN, sizeof(pOutBfr->MfgDomain));

            *InstanceLengthArray = size;
            status = SRB_STATUS_SUCCESS;
            break;
        }
        case VIOSCSI_MS_PORT_INFORM_GUID_INDEX:
        {
            RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->VIOSCSI_MS_PORT_INFORM_GUID_INDEX ERROR\n"));
            break;
        }
        default:
        {
            status = SRB_STATUS_ERROR;
        }
    }

    ScsiPortWmiPostProcess(RequestContext,
                           status,
                           size);

EXIT_FN();
    return TRUE;
}

UCHAR
VioScsiExecuteWmiMethod(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG MethodId,
    IN ULONG InBufferSize,
    IN ULONG OutBufferSize,
    IN OUT PUCHAR Buffer
    )
{
    PADAPTER_EXTENSION      adaptExt = (PADAPTER_EXTENSION)Context;
    ULONG                   size = 0;
    UCHAR                   status = SRB_STATUS_SUCCESS;

    ENTER_FN();
    RhelDbgPrint(TRACE_LEVEL_FATAL, ("<-->VioScsiQueryWmiDataBlock\n"));
    switch (GuidIndex)
    {
        case VIOSCSI_SETUP_GUID_INDEX:
        {
            RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->VIOSCSI_SETUP_GUID_INDEX ERROR\n"));
            break;
        }
        case VIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX:
        {
            PMS_SM_AdapterInformationQuery pOutBfr = (PMS_SM_AdapterInformationQuery)Buffer;
            RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->VIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX ERROR\n"));
            break;
        }
        case VIOSCSI_MS_PORT_INFORM_GUID_INDEX:
        {
            switch (MethodId)
            {
                case SM_GetPortType:
                {
                    PSM_GetPortType_IN  pInBfr = (PSM_GetPortType_IN)Buffer;
                    PSM_GetPortType_OUT pOutBfr = (PSM_GetPortType_OUT)Buffer;

                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->SM_GetPortType\n"));
                    size = SM_GetPortType_OUT_SIZE;

                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }

                    if (InBufferSize < SM_GetPortType_IN_SIZE)
                    {
                        status = SRB_STATUS_BAD_FUNCTION;
                        break;
                    }

                    break;
                }
                case SM_GetAdapterPortAttributes:
                {
                    PSM_GetAdapterPortAttributes_IN  pInBfr = (PSM_GetAdapterPortAttributes_IN)Buffer;
                    PSM_GetAdapterPortAttributes_OUT pOutBfr = (PSM_GetAdapterPortAttributes_OUT)Buffer;

                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->SM_GetAdapterPortAttributes\n"));
                    size = SM_GetAdapterPortAttributes_OUT_SIZE;

                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }

                    if (InBufferSize < SM_GetAdapterPortAttributes_IN_SIZE)
                    {
                        status = SRB_STATUS_BAD_FUNCTION;
                        break;
                    }

                    break;
                }

                case SM_GetDiscoveredPortAttributes:
                {
                    PSM_GetDiscoveredPortAttributes_IN  pInBfr = (PSM_GetDiscoveredPortAttributes_IN)Buffer;
                    PSM_GetDiscoveredPortAttributes_OUT pOutBfr = (PSM_GetDiscoveredPortAttributes_OUT)Buffer;

                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->SM_GetDiscoveredPortAttributes\n"));
                    size = SM_GetDiscoveredPortAttributes_OUT_SIZE;

                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }

                    if (InBufferSize < SM_GetDiscoveredPortAttributes_IN_SIZE)
                    {
                        status = SRB_STATUS_BAD_FUNCTION;
                        break;
                    }

                    break;
                }

                case SM_GetPortAttributesByWWN:
                {
                    PSM_GetPortAttributesByWWN_IN  pInBfr = (PSM_GetPortAttributesByWWN_IN)Buffer;
                    PSM_GetPortAttributesByWWN_OUT pOutBfr = (PSM_GetPortAttributesByWWN_OUT)Buffer;

                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->SM_GetPortAttributesByWWN\n"));
                    size = SM_GetPortAttributesByWWN_OUT_SIZE;

                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }

                    if (InBufferSize < SM_GetPortAttributesByWWN_IN_SIZE)
                    {
                        status = SRB_STATUS_BAD_FUNCTION;
                        break;
                    }

                    break;
                }

                case SM_GetProtocolStatistics:
                {
                    PSM_GetProtocolStatistics_IN  pInBfr = (PSM_GetProtocolStatistics_IN)Buffer;
                    PSM_GetProtocolStatistics_OUT pOutBfr = (PSM_GetProtocolStatistics_OUT)Buffer;

                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->SM_GetProtocolStatistics\n"));
                    size = SM_GetProtocolStatistics_OUT_SIZE;

                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }

                    if (InBufferSize < SM_GetProtocolStatistics_IN_SIZE)
                    {
                        status = SRB_STATUS_BAD_FUNCTION;
                        break;
                    }

                    break;
                }

                case SM_GetPhyStatistics:
                {
                    PSM_GetPhyStatistics_IN  pInBfr = (PSM_GetPhyStatistics_IN)Buffer;
                    PSM_GetPhyStatistics_OUT pOutBfr = (PSM_GetPhyStatistics_OUT)Buffer;

                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->SM_GetPhyStatistics\n"));
                    //FIXME
                    size = FIELD_OFFSET(SM_GetPhyStatistics_OUT, PhyCounter) + sizeof(LONGLONG);
                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }

                    if (InBufferSize < SM_GetPhyStatistics_IN_SIZE)
                    {
                        status = SRB_STATUS_BAD_FUNCTION;
                        break;
                    }

                    break;
                }


                case SM_GetFCPhyAttributes:
                {
                    PSM_GetFCPhyAttributes_IN  pInBfr = (PSM_GetFCPhyAttributes_IN)Buffer;
                    PSM_GetFCPhyAttributes_OUT pOutBfr = (PSM_GetFCPhyAttributes_OUT)Buffer;

                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->SM_GetFCPhyAttributes\n"));
                    size = SM_GetFCPhyAttributes_OUT_SIZE;

                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }

                    if (InBufferSize < SM_GetFCPhyAttributes_IN_SIZE)
                    {
                        status = SRB_STATUS_BAD_FUNCTION;
                        break;
                    }

                    break;
                }

                case SM_GetSASPhyAttributes:
                {
                    PSM_GetSASPhyAttributes_IN  pInBfr = (PSM_GetSASPhyAttributes_IN)Buffer;
                    PSM_GetSASPhyAttributes_OUT pOutBfr = (PSM_GetSASPhyAttributes_OUT)Buffer;

                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->SM_GetSASPhyAttributes\n"));
                    size = SM_GetSASPhyAttributes_OUT_SIZE;

                    if (OutBufferSize < size)
                    {
                        status = SRB_STATUS_DATA_OVERRUN;
                        break;
                    }

                    if (InBufferSize < SM_GetSASPhyAttributes_IN_SIZE)
                    {
                        status = SRB_STATUS_BAD_FUNCTION;
                        break;
                    }

                    break;
                }

                case SM_RefreshInformation:
                {
                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("-->SM_RefreshInformation\n"));
                    break;
                }

                default:
                    status = SRB_STATUS_INVALID_REQUEST;
                    RhelDbgPrint(TRACE_LEVEL_FATAL, ("--> ERROR Unknown MethodId = %lu\n", MethodId));
                    break;
            }
            default:
                status = SRB_STATUS_INVALID_REQUEST;
                RhelDbgPrint(TRACE_LEVEL_FATAL, ("--> ERROR Unknown GuidIndex = %lu\n", GuidIndex));

                break;
        }

    }
    ScsiPortWmiPostProcess(RequestContext,
        status,
        size);

    EXIT_FN();
    return SRB_STATUS_SUCCESS;

}

UCHAR
VioScsiQueryWmiRegInfo(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    OUT PWCHAR *MofResourceName
    )
{
ENTER_FN();
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(RequestContext);

    *MofResourceName = VioScsiWmi_MofResourceName;
    return SRB_STATUS_SUCCESS;
}

VOID
VioScsiReadExtendedData(
IN PVOID Context,
OUT PUCHAR Buffer
)
{
    UCHAR numberOfBytes = sizeof(VioScsiExtendedInfo) - 1;
    PADAPTER_EXTENSION    adaptExt;
    PVioScsiExtendedInfo  extInfo;

ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)Context;
    extInfo = (PVioScsiExtendedInfo)Buffer;

    RtlZeroMemory(Buffer, numberOfBytes);

    extInfo->QueueDepth = (ULONG)adaptExt->queue_depth;
    extInfo->QueuesCount = (UCHAR)adaptExt->num_queues;
    extInfo->Indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);
    extInfo->EventIndex = CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX);
    extInfo->DpcRedirection = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_DPC_REDIRECTION);
    extInfo->ConcurrentChannels = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_CONCURRENT_CHANNELS);
    extInfo->InterruptMsgRanges = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_INTERRUPT_MESSAGE_RANGES);
    extInfo->CompletionDuringStartIo = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO);

EXIT_FN();
}

#ifdef USE_WORK_ITEM
#if (NTDDI_VERSION > NTDDI_WIN7)
VOID
VioScsiWorkItemCallback(
    _In_ PVOID DeviceExtension,
    _In_opt_ PVOID Context,
    _In_ PVOID Worker
    )
{
    ULONG MessageId = PtrToUlong(Context);
    ULONG status = STOR_STATUS_SUCCESS;
    ULONG msg = MessageId - 3;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSTOR_SLIST_ENTRY   listEntryRev, listEntry;
ENTER_FN();
    status = StorPortInterlockedFlushSList(DeviceExtension, &adaptExt->srb_list[msg], &listEntryRev);
    if ((status == STOR_STATUS_SUCCESS) && (listEntryRev != NULL)) {
        KAFFINITY old_affinity, new_affinity;
        old_affinity = new_affinity = 0;
#if 1
        listEntry = listEntryRev;
#else
        listEntry = NULL;
        while (listEntryRev != NULL) {
            next = listEntryRev->Next;
            listEntryRev->Next = listEntry;
            listEntry = listEntryRev;
            listEntryRev = next;
        }
#endif
        while(listEntry)
        {
            PVirtIOSCSICmd  cmd = NULL;
            PSRB_TYPE Srb = NULL;
            PSRB_EXTENSION srbExt = NULL;
            PSTOR_SLIST_ENTRY next = listEntry->Next;
            srbExt = CONTAINING_RECORD(listEntry,
                        SRB_EXTENSION, list_entry);

            ASSERT(srExt);
            Srb = (PSRB_TYPE)(srbExt->Srb);
            cmd = (PVirtIOSCSICmd)srbExt->priv;
            ASSERT(cmd);
            if (new_affinity == 0) {
                new_affinity = ((KAFFINITY)1) << srbExt->cpu;
                old_affinity = KeSetSystemAffinityThreadEx(new_affinity);
            }
            HandleResponse(DeviceExtension, cmd);
            listEntry = next;
        }
        if (new_affinity != 0) {
            KeRevertToUserAffinityThreadEx(old_affinity);
        }
    }
    else if (status != STOR_STATUS_SUCCESS) {
       RhelDbgPrint(TRACE_LEVEL_FATAL, ("StorPortInterlockedPushEntrySList failed with status 0x%x\n\n", status));
    }

    status = StorPortFreeWorker(DeviceExtension, Worker);
    if (status != STOR_STATUS_SUCCESS) {
       RhelDbgPrint(TRACE_LEVEL_FATAL, ("StorPortFreeWorker failed with status 0x%x\n\n", status));
    }
EXIT_FN();
}
#endif
#endif
