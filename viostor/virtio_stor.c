/*
 * This file contains viostor StorPort(ScsiPort) miniport driver
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
#include "virtio_stor.h"
#if defined(EVENT_TRACING)
#include "virtio_stor.tmh"
#endif

BOOLEAN IsCrashDumpMode;

#ifdef EVENT_TRACING
PVOID TraceContext;
#endif

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

VOID
RhelSetGuestFeatures(
    IN PVOID DeviceExtension
);

UCHAR
RhelScsiVerify(
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
CompleteRequestWithStatus(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb,
    IN UCHAR status
    );

VOID
FORCEINLINE
DeviceChangeNotification(
    IN PVOID DeviceExtension,
    IN BOOLEAN bLun
    );

BOOLEAN
FORCEINLINE
SetSenseInfo(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    );

BOOLEAN
FORCEINLINE
CompleteDPC(
    IN PVOID DeviceExtension,
    IN ULONG  MessageID
    );

VOID
ReportDeviceIdentifier(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    );

#ifdef EVENT_TRACING
VOID WppCleanupRoutine(PVOID arg1) {
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " WppCleanupRoutine\n");

    WPP_CLEANUP(NULL, TraceContext);
}
#endif


ULONG
DriverEntry(
    IN PVOID  DriverObject,
    IN PVOID  RegistryPath
    )
{

    HW_INITIALIZATION_DATA hwInitData;
    ULONG                  initResult;

#ifdef EVENT_TRACING
    STORAGE_TRACE_INIT_INFO initInfo;
#else
//FIXME
#ifdef DBG
    InitializeDebugPrints((PDRIVER_OBJECT)DriverObject, (PUNICODE_STRING)RegistryPath);
#endif
#endif

    RhelDbgPrint(TRACE_LEVEL_ERROR, " Viostor driver started...built on %s %s\n", __DATE__, __TIME__);
    IsCrashDumpMode = FALSE;
    if (RegistryPath == NULL) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " DriverEntry: Crash dump mode\n");
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
    hwInitData.HwBuildIo                = VirtIoBuildIo;
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
    hwInitData.SrbTypeFlags = SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK;
#endif

    initResult = StorPortInitialize(DriverObject,
                                    RegistryPath,
                                    &hwInitData,
                                    NULL);

#ifdef EVENT_TRACING
    TraceContext = NULL;

    memset(&initInfo, 0, sizeof(STORAGE_TRACE_INIT_INFO));
    initInfo.Size = sizeof(STORAGE_TRACE_INIT_INFO);
    initInfo.DriverObject = DriverObject;
    initInfo.NumErrorLogRecords = 5;
    initInfo.TraceCleanupRoutine = WppCleanupRoutine;
    initInfo.TraceContext = NULL;

    WPP_INIT_TRACING(DriverObject, RegistryPath, &initInfo);

    if (initInfo.TraceContext != NULL) {
        TraceContext = initInfo.TraceContext;
    }
#endif

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " Initialize returned 0x%x\n", initResult);

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
        RhelDbgPrint(TRACE_LEVEL_FATAL, " Failed to initialize virtio device, error %x\n", status);
        if (status == STATUS_DEVICE_NOT_CONNECTED) {
            return SP_RETURN_NOT_FOUND;
        }
        return SP_RETURN_ERROR;
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
#ifdef MSI_SUPPORTED
    ULONG              num_cpus;
    ULONG              max_cpus;
#endif
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
    adaptExt->slot_number = ConfigInfo->SlotNumber;
    adaptExt->dump_mode  = IsCrashDumpMode;

    ConfigInfo->Master                 = TRUE;
    ConfigInfo->ScatterGather          = TRUE;
    ConfigInfo->DmaWidth               = Width32Bits;
    ConfigInfo->Dma32BitAddresses      = TRUE;
    ConfigInfo->Dma64BitAddresses      = TRUE;
    ConfigInfo->WmiDataProvider        = FALSE;
    ConfigInfo->AlignmentMask          = 0x3;
    ConfigInfo->MapBuffers             = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->SynchronizationModel   = StorSynchronizeFullDuplex;
#ifdef MSI_SUPPORTED
    ConfigInfo->HwMSInterruptRoutine   = VirtIoMSInterruptRoutine;
    ConfigInfo->InterruptSynchronizationMode=InterruptSynchronizePerMessage;
#endif

    pci_cfg_len = StorPortGetBusData(
        DeviceExtension,
        PCIConfiguration,
        ConfigInfo->SystemIoBusNumber,
        (ULONG)ConfigInfo->SlotNumber,
        (PVOID)&adaptExt->pci_config_buf,
        sizeof(adaptExt->pci_config_buf));

    if (pci_cfg_len != sizeof(adaptExt->pci_config_buf)) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, " CANNOT READ PCI CONFIGURATION SPACE %d\n", pci_cfg_len);
        return SP_RETURN_ERROR;
    }

    /* initialize the pci_bars array */
    for (i = 0; i < ConfigInfo->NumberOfAccessRanges; i++) {
        accessRange = *ConfigInfo->AccessRanges + i;
        if (accessRange->RangeLength != 0) {
            int iBar = virtio_get_bar_index(&adaptExt->pci_config, accessRange->RangeStart);
            if (iBar == -1) {
                RhelDbgPrint(TRACE_LEVEL_FATAL, " Cannot get index for BAR %I64d\n", accessRange->RangeStart.QuadPart);
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
           RhelDbgPrint(TRACE_LEVEL_FATAL, " NO CAPABILITIES_LIST\n");
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
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageControl.TableSize = %d\n", pMsixCapOffset->MessageControl.TableSize);
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageControl.FunctionMask = %d\n", pMsixCapOffset->MessageControl.FunctionMask);
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageControl.MSIXEnable = %d\n", pMsixCapOffset->MessageControl.MSIXEnable);

                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageTable = %lu\n", pMsixCapOffset->MessageTable.TableOffset);
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " PBATable = %lu\n", pMsixCapOffset->PBATable.TableOffset);
                    adaptExt->msix_enabled = (pMsixCapOffset->MessageControl.MSIXEnable == 1);
                    break;
                 }
                 else
                 {
                    CapOffset = pMsixCapOffset->Header.Next;
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " CapabilityID = %x, Next CapOffset = %x\n", pMsixCapOffset->Header.CapabilityID, CapOffset);
                 }
              }
           }
           else
           {
              RhelDbgPrint(TRACE_LEVEL_FATAL, " NOT A PCI_DEVICE_TYPE\n");
           }
        }
    }
#endif

#if (NTDDI_VERSION > NTDDI_WIN7)
    adaptExt->device_address.Type = STOR_ADDRESS_TYPE_BTL8;
    adaptExt->device_address.Port = 0;
    adaptExt->device_address.AddressLength = STOR_ADDR_BTL8_ADDRESS_LENGTH;
    adaptExt->device_address.Path = 0;
    adaptExt->device_address.Target = 0;
    adaptExt->device_address.Lun = 0;
#endif

    /* initialize the virtual device */
    res = InitVirtIODevice(DeviceExtension);
    if (res != SP_RETURN_FOUND) {
        return res;
    }

    RhelGetDiskGeometry(DeviceExtension);
    RhelSetGuestFeatures(DeviceExtension);

    ConfigInfo->NumberOfBuses               = 1;
    ConfigInfo->MaximumNumberOfTargets      = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;

    ConfigInfo->CachesData = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_FLUSH) ? TRUE : FALSE;
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_BLK_F_WCACHE = %d\n", ConfigInfo->CachesData);
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_BLK_F_MQ = %d\n", CHECKBIT(adaptExt->features, VIRTIO_BLK_F_MQ));

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

    ConfigInfo->MaximumTransferLength = VIOBLK_MAX_TRANSFER;

    if(!adaptExt->dump_mode) {
        adaptExt->indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);
    }
    if(adaptExt->indirect) {
        adaptExt->queue_depth = queueLength;
    }

#ifdef MSI_SUPPORTED
#if (NTDDI_VERSION >= NTDDI_WIN7)
    num_cpus = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    max_cpus = KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
    /* Set num_cpus and max_cpus to some sane values, to keep Static Driver Verification happy */
    num_cpus = max(1, num_cpus);
    max_cpus = max(1, max_cpus);
#else
    num_cpus = KeQueryActiveProcessorCount(NULL);
    max_cpus = KeQueryMaximumProcessorCount();
#endif
#endif

    adaptExt->num_queues = 1;
    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_MQ)) {
        virtio_get_config(&adaptExt->vdev, FIELD_OFFSET(blk_config, num_queues),
                          &adaptExt->num_queues, sizeof(adaptExt->num_queues));
    }

#ifdef MSI_SUPPORTED
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
        adaptExt->num_queues = (USHORT)num_cpus;
#else
        adaptExt->num_queues = 1;
#endif
    }

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Queues %d CPUs %d\n", adaptExt->num_queues, num_cpus);

    max_queues = min(max_cpus, adaptExt->num_queues);
#else
    adaptExt->num_queues = 1;
    max_queues = 1;
#endif
    adaptExt->pageAllocationSize = 0;
    adaptExt->poolAllocationSize = 0;
    adaptExt->pageOffset = 0;
    adaptExt->poolOffset = 0;
    Size = 0;

    for (index = 0; index < max_queues; ++index) {
        virtio_query_queue_allocation(&adaptExt->vdev, index, &queueLength, &Size, &HeapSize);
        if (Size == 0) {
            LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

            RhelDbgPrint(TRACE_LEVEL_FATAL, " Virtual queue %d config failed.\n", index);
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

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " breaks_number = %x  queue_depth = %x\n",
                ConfigInfo->NumberOfPhysicalBreaks,
                adaptExt->queue_depth);

    extensionSize = PAGE_SIZE + adaptExt->pageAllocationSize + adaptExt->poolAllocationSize;
    uncachedExtensionVa = StorPortGetUncachedExtension(DeviceExtension, ConfigInfo, extensionSize);
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " StorPortGetUncachedExtension uncachedExtensionVa = %p allocation size = %d\n",
                uncachedExtensionVa,
                extensionSize);
    if (!uncachedExtensionVa) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, " Can't get uncached extension allocation size = %d\n", extensionSize);
        return SP_RETURN_ERROR;
    }

    adaptExt->pageAllocationVa = (PVOID)(((ULONG_PTR)(uncachedExtensionVa) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    if (adaptExt->poolAllocationSize > 0) {
        adaptExt->poolAllocationVa = (PVOID)((ULONG_PTR)adaptExt->pageAllocationVa + adaptExt->pageAllocationSize);
    }
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Page-aligned area at %p, size = %d\n", adaptExt->pageAllocationVa, adaptExt->pageAllocationSize);
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Pool area at %p, size = %d\n", adaptExt->poolAllocationVa, adaptExt->poolAllocationSize);

    return SP_RETURN_FOUND;
}

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

static BOOLEAN InitializeVirtualQueues(PADAPTER_EXTENSION adaptExt)
{
    NTSTATUS status;
    ULONG numQueues = adaptExt->num_queues;

    RhelDbgPrint(TRACE_LEVEL_FATAL, " InitializeVirtualQueues numQueues %d\n", numQueues);
    status = virtio_find_queues(
        &adaptExt->vdev,
        numQueues,
        adaptExt->vq);
    if (!NT_SUCCESS(status)) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, " virtio_find_queues failed with error %x\n", status);
        return FALSE;
    }

    return TRUE;
}

VOID
RhelSetGuestFeatures(
    IN PVOID DeviceExtension
)
{
    ULONGLONG          guestFeatures = 0;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (CHECKBIT(adaptExt->features, VIRTIO_F_VERSION_1)) {
        guestFeatures |= (1ULL << VIRTIO_F_VERSION_1);
        if (CHECKBIT(adaptExt->features, VIRTIO_F_RING_PACKED)) {
            guestFeatures |= (1ULL << VIRTIO_F_RING_PACKED);
        }
    }

#if (WINVER == 0x0A00)
    if (CHECKBIT(adaptExt->features, VIRTIO_F_IOMMU_PLATFORM)) {
        guestFeatures |= (1ULL << VIRTIO_F_IOMMU_PLATFORM);
    }
#endif

    if (CHECKBIT(adaptExt->features, VIRTIO_F_ANY_LAYOUT)) {
        guestFeatures |= (1ULL << VIRTIO_F_ANY_LAYOUT);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX)) {
        guestFeatures |= (1ULL << VIRTIO_RING_F_EVENT_IDX);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC)) {
        guestFeatures |= (1ULL << VIRTIO_RING_F_INDIRECT_DESC);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_FLUSH)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_FLUSH);
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

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_MQ)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_MQ);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_DISCARD)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_DISCARD);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_WRITE_ZEROES)) {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_WRITE_ZEROES);
    }

    if (!NT_SUCCESS(virtio_set_features(&adaptExt->vdev, guestFeatures))) {
        RhelDbgPrint(TRACE_LEVEL_FATAL, " virtio_set_features failed\n");
        return;
    }
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " Host Features %llu gust features %llu\n", adaptExt->features, guestFeatures);
}

BOOLEAN
VirtIoHwInitialize(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION adaptExt;
    BOOLEAN            ret = FALSE;
    ULONGLONG          guestFeatures = 0;
    PERF_CONFIGURATION_DATA perfData = { 0 };
    ULONG              status = STOR_STATUS_SUCCESS;
#ifdef MSI_SUPPORTED
    MESSAGE_INTERRUPT_INFORMATION msi_info = { 0 };
#endif

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;


    adaptExt->msix_vectors = 0;
    adaptExt->pageOffset = 0;
    adaptExt->poolOffset = 0;

#ifdef MSI_SUPPORTED
    while(StorPortGetMSIInfo(DeviceExtension, adaptExt->msix_vectors, &msi_info) == STOR_STATUS_SUCCESS) {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageId = %x\n", msi_info.MessageId);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageData = %x\n", msi_info.MessageData);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " InterruptVector = %x\n", msi_info.InterruptVector);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " InterruptLevel = %x\n", msi_info.InterruptLevel);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " InterruptMode = %s\n", msi_info.InterruptMode == LevelSensitive ? "LevelSensitive" : "Latched");
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageAddress = %I64x\n\n", msi_info.MessageAddress.QuadPart);
        ++adaptExt->msix_vectors;
    }
#endif

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Queues %d msix_vectors %d\n", adaptExt->num_queues, adaptExt->msix_vectors);
    if (adaptExt->num_queues > 1 &&
        ((adaptExt->num_queues + 1U) > adaptExt->msix_vectors)) {
        //FIXME
        adaptExt->num_queues = 1;
    }

    if (adaptExt->msix_vectors >= (adaptExt->num_queues + 1U)) {
        /* initialize queues with a MSI vector per queue */
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, (" Using a unique MSI vector per queue\n"));
        adaptExt->msix_one_vector = FALSE;
    } else {
        /* if we don't have enough vectors, use one for all queues */
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, (" Using one MSI vector for all queues\n"));
        adaptExt->msix_one_vector = TRUE;
    }

    if (!InitializeVirtualQueues(adaptExt)) {
        LogError(DeviceExtension,
                SP_INTERNAL_ADAPTER_ERROR,
                __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, (" Cannot find snd virtual queue\n"));
        virtio_add_status(&adaptExt->vdev, VIRTIO_CONFIG_S_FAILED);
        return ret;
    }

    memset(&adaptExt->inquiry_data, 0, sizeof(INQUIRYDATA));

    adaptExt->inquiry_data.ANSIVersion = 4;
    adaptExt->inquiry_data.ResponseDataFormat = 2;
    adaptExt->inquiry_data.CommandQueue = 1;
    adaptExt->inquiry_data.DeviceType   = DIRECT_ACCESS_DEVICE;
    adaptExt->inquiry_data.Wide32Bit    = 1;
    adaptExt->inquiry_data.AdditionalLength = 91;
    StorPortMoveMemory(&adaptExt->inquiry_data.VendorId, "Red Hat ", sizeof("Red Hat "));
    StorPortMoveMemory(&adaptExt->inquiry_data.ProductId, "VirtIO", sizeof("VirtIO"));
    StorPortMoveMemory(&adaptExt->inquiry_data.ProductRevisionLevel, "0001", sizeof("0001"));
    StorPortMoveMemory(&adaptExt->inquiry_data.VendorSpecific, "0001", sizeof("0001"));

    ret = TRUE;

    if (!adaptExt->dump_mode)
    {
        if ((adaptExt->num_queues > 1) && (adaptExt->perfFlags == 0)) {
            perfData.Version = STOR_PERF_VERSION;
            perfData.Size = sizeof(PERF_CONFIGURATION_DATA);

            status = StorPortInitializePerfOpts(DeviceExtension, TRUE, &perfData);

            RhelDbgPrint(TRACE_LEVEL_VERBOSE, " Perf Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, FirstRedirectionMessageNumber = %d,LastRedirectionMessageNumber = %d\n",
                        perfData.Version, perfData.Flags,
                        perfData.ConcurrentChannels,
                        perfData.FirstRedirectionMessageNumber,
                        perfData.LastRedirectionMessageNumber);
            if (status == STOR_STATUS_SUCCESS) {
                if (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION)) {
                    adaptExt->perfFlags |= STOR_PERF_DPC_REDIRECTION;
                }
                if (CHECKFLAG(perfData.Flags, STOR_PERF_INTERRUPT_MESSAGE_RANGES)) {
                    adaptExt->perfFlags |= STOR_PERF_INTERRUPT_MESSAGE_RANGES;
                    perfData.FirstRedirectionMessageNumber = 1;
                    perfData.LastRedirectionMessageNumber = adaptExt->msix_vectors - 1;
                }
#if (NTDDI_VERSION > NTDDI_WIN7)
                if (CHECKFLAG(perfData.Flags, STOR_PERF_CONCURRENT_CHANNELS)) {
                    adaptExt->perfFlags |= STOR_PERF_CONCURRENT_CHANNELS;
                    perfData.ConcurrentChannels = adaptExt->num_queues;
                }
//                if (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION_CURRENT_CPU)) {
//                    adaptExt->perfFlags |= STOR_PERF_DPC_REDIRECTION_CURRENT_CPU;
//                }
                if (CHECKFLAG(perfData.Flags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO)) {
                    adaptExt->perfFlags |= STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO;
                }
#endif
                perfData.Flags = adaptExt->perfFlags;
                RhelDbgPrint(TRACE_LEVEL_VERBOSE, " Perf Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, FirstRedirectionMessageNumber = %d,LastRedirectionMessageNumber = %d\n",
                            perfData.Version,
                            perfData.Flags, perfData.ConcurrentChannels,
                            perfData.FirstRedirectionMessageNumber,
                            perfData.LastRedirectionMessageNumber);
                status = StorPortInitializePerfOpts(DeviceExtension, FALSE, &perfData);
                if (status != STOR_STATUS_SUCCESS) {
                    adaptExt->perfFlags = 0;
                    RhelDbgPrint(TRACE_LEVEL_ERROR, " StorPortInitializePerfOpts FALSE status = 0x%x\n", status);
                }
            }
            else {
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, " StorPortInitializePerfOpts TRUE status = 0x%x\n", status);
            }
        }
    }

    if (!adaptExt->dump_mode) {
        if (adaptExt->dpc == NULL) {
            adaptExt->dpc = (PSTOR_DPC)VioStorPoolAlloc(DeviceExtension, sizeof(STOR_DPC) * adaptExt->num_queues);
        }
        if ((adaptExt->dpc != NULL) && (adaptExt->dpc_ok == FALSE)) {
            ret = StorPortEnablePassiveInitialization(DeviceExtension, VirtIoPassiveInitializeRoutine);
        }
    }

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
    SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " Srb = 0x%p\n", Srb);

    switch (SRB_FUNCTION(Srb)) {
        case SRB_FUNCTION_EXECUTE_SCSI: {
            break;
        }
        case SRB_FUNCTION_IO_CONTROL: {
            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
            return TRUE;
        }
        case SRB_FUNCTION_PNP: {
            UCHAR SrbStatus = SRB_STATUS_SUCCESS;
            ULONG SrbPnPFlags;
            ULONG PnPAction;
            SRB_GET_PNP_INFO(Srb, SrbPnPFlags, PnPAction);
            RhelDbgPrint(TRACE_LEVEL_FATAL, " SrbPnPFlags = %d, PnPAction = %d\n", SrbPnPFlags, PnPAction);
            switch (PnPAction) {
            case StorQueryCapabilities:
                if (CHECKFLAG(SrbPnPFlags, SRB_PNP_FLAGS_ADAPTER_REQUEST) &&
                    (SRB_DATA_TRANSFER_LENGTH(Srb) >= sizeof(STOR_DEVICE_CAPABILITIES))) {
                    PSTOR_DEVICE_CAPABILITIES storCapabilities = (PSTOR_DEVICE_CAPABILITIES)SRB_DATA_BUFFER(Srb);
                    RtlZeroMemory(storCapabilities, sizeof(*storCapabilities));
                    storCapabilities->Removable = 1;
                }
                break;
            case StorRemoveDevice:
            case StorSurpriseRemoval:
                adaptExt->removed = TRUE;
                DeviceChangeNotification(DeviceExtension, FALSE);
                break;
            default:
                RhelDbgPrint(TRACE_LEVEL_FATAL, " Unsupported PnPAction SrbPnPFlags = %d, PnPAction = %d\n", SrbPnPFlags, PnPAction);
                SrbStatus = SRB_STATUS_INVALID_REQUEST;
            }
            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
            return TRUE;
        }
        case SRB_FUNCTION_POWER:
        case SRB_FUNCTION_RESET_BUS:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_LOGICAL_UNIT: {
            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
#ifdef DBG
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " RESET (%p) Function %x Cnt %d InQueue %d\n",
                        Srb, SRB_FUNCTION(Srb), adaptExt->srb_cnt, adaptExt->inqueue_cnt);
            for (USHORT i = 0; i < adaptExt->num_queues; i++) {
                if (adaptExt->vq[i]) {
                    RhelDbgPrint(TRACE_LEVEL_ERROR, "%d indx %d\n", i, adaptExt->vq[i]->index);
                }
            }
#endif
            return TRUE;
        }
        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_SHUTDOWN: {
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
            if (!RhelDoFlush(DeviceExtension, (PSRB_TYPE)Srb, FALSE, FALSE)) {
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_ERROR);
            }
            return TRUE;
        }

        default: {
            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_INVALID_REQUEST);
            return TRUE;
        }
    }

    if (!cdb) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " no CDB (%p) Function %x\n", Srb, SRB_FUNCTION(Srb));
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_BAD_FUNCTION);
        return TRUE;
    }

    switch (cdb->CDB6GENERIC.OperationCode) {
        case SCSIOP_MODE_SENSE: {
            UCHAR SrbStatus = RhelScsiGetModeSense(DeviceExtension, (PSRB_TYPE)Srb);
            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
            return TRUE;
        }
        case SCSIOP_INQUIRY: {
            UCHAR SrbStatus = RhelScsiGetInquiryData(DeviceExtension, (PSRB_TYPE)Srb);
            if (SRB_STATUS_PENDING != SrbStatus) {
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
            }
            return TRUE;
        }

        case SCSIOP_READ_CAPACITY16:
        case SCSIOP_READ_CAPACITY: {
            UCHAR SrbStatus = RhelScsiGetCapacity(DeviceExtension, (PSRB_TYPE)Srb);
            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
            return TRUE;
        }
        case SCSIOP_WRITE:
        case SCSIOP_WRITE16: {
            if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
                UCHAR SrbStatus = SRB_STATUS_ERROR;
                adaptExt->sense_info.senseKey = SCSI_SENSE_DATA_PROTECT;
                adaptExt->sense_info.additionalSenseCode = SCSI_ADSENSE_WRITE_PROTECT;
#if (NTDDI_VERSION > NTDDI_WIN7)
                adaptExt->sense_info.additionalSenseCodeQualifier = SCSI_SENSEQ_SPACE_ALLOC_FAILED_WRITE_PROTECT;//SCSI_ADSENSE_NO_SENSE;
#else
                adaptExt->sense_info.additionalSenseCodeQualifier = SCSI_ADSENSE_NO_SENSE;
#endif
                if (SetSenseInfo(DeviceExtension, (PSRB_TYPE)Srb)) {
                    SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                }
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
                return TRUE;
            }
        }
        case SCSIOP_READ:
        case SCSIOP_READ16: {
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
            if(!RhelDoReadWrite(DeviceExtension, (PSRB_TYPE)Srb)) {
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_BUSY);
            }
            return TRUE;
        }
        case SCSIOP_START_STOP_UNIT: {
            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
            return TRUE;
        }
        case SCSIOP_REQUEST_SENSE:
        case SCSIOP_TEST_UNIT_READY:
        case SCSIOP_RESERVE_UNIT:
        case SCSIOP_RESERVE_UNIT10:
        case SCSIOP_RELEASE_UNIT:
        case SCSIOP_RELEASE_UNIT10:
        case SCSIOP_MEDIUM_REMOVAL: {
            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
            return TRUE;
        }
        case SCSIOP_VERIFY:
        case SCSIOP_VERIFY16: {
            UCHAR SrbStatus = RhelScsiVerify(DeviceExtension, (PSRB_TYPE)Srb);
            if (SrbStatus == SRB_STATUS_INVALID_REQUEST) {
                SrbStatus = SRB_STATUS_ERROR;
                adaptExt->sense_info.senseKey = SCSI_SENSE_ILLEGAL_REQUEST;
                adaptExt->sense_info.additionalSenseCode = SCSI_ADSENSE_INVALID_CDB;
                adaptExt->sense_info.additionalSenseCodeQualifier = 0;
                if (SetSenseInfo(DeviceExtension, (PSRB_TYPE)Srb)) {
                    SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                }
            }
            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
            return TRUE;
        }
        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16: {
            if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO)) {
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
                return TRUE;
            }
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
            if (!RhelDoFlush(DeviceExtension, (PSRB_TYPE)Srb, FALSE, FALSE)) {
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_ERROR);
            }
            return TRUE;
        }
#if (NTDDI_VERSION > NTDDI_WIN7)
        case SCSIOP_UNMAP: {
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
            if (!RhelDoUnMap(DeviceExtension, (PSRB_TYPE)Srb)) {
                RhelDbgPrint(TRACE_LEVEL_FATAL, "RhelDoUnMap failed.\n");
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_ERROR);
            }
            return TRUE;
        }
#endif
    }

    if (cdb->CDB12.OperationCode == SCSIOP_REPORT_LUNS) {
        UCHAR SrbStatus = RhelScsiReportLuns(DeviceExtension, (PSRB_TYPE)Srb);
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
        return TRUE;
    }

    RhelDbgPrint(TRACE_LEVEL_ERROR, " SRB_STATUS_INVALID_REQUEST (%p) Function %x, OperationCode %x\n",
                Srb, SRB_FUNCTION(Srb), cdb->CDB6GENERIC.OperationCode);

    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_INVALID_REQUEST);
    return TRUE;
}


BOOLEAN
VirtIoInterrupt(
    IN PVOID DeviceExtension
    )
{
    PADAPTER_EXTENSION  adaptExt;
    BOOLEAN             isInterruptServiced = FALSE;
    ULONG               intReason = 0;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " IRQL (%d)\n", KeGetCurrentIrql());
    if (adaptExt->removed == TRUE) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " Interrupt on removed device)");
        return FALSE;
    }
    intReason = virtio_read_isr_status(&adaptExt->vdev);
    if ( intReason == 1 || adaptExt->dump_mode ) {
        if (!CompleteDPC(DeviceExtension, 1)) {
            VioStorCompleteRequest(DeviceExtension, 1, TRUE);
        }
        isInterruptServiced = TRUE;
    } else if (intReason == 3) {
        RhelGetDiskGeometry(DeviceExtension);
        isInterruptServiced = TRUE;
        adaptExt->sense_info.senseKey = SCSI_SENSE_UNIT_ATTENTION;
        adaptExt->sense_info.additionalSenseCode = SCSI_ADSENSE_PARAMETERS_CHANGED;
        adaptExt->sense_info.additionalSenseCodeQualifier = SCSI_SENSEQ_CAPACITY_DATA_CHANGED;
        adaptExt->check_condition = TRUE;
        DeviceChangeNotification(DeviceExtension, TRUE);
    }
    if (!isInterruptServiced) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " was not serviced ISR status = %d\n", intReason);
    }
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
    RhelDbgPrint(TRACE_LEVEL_ERROR, "<---->\n");
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

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " ControlType %d\n", ControlType);

    switch (ControlType) {

    case ScsiQuerySupportedControlTypes: {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, " ScsiQuerySupportedControlTypes\n");
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
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, " ScsiStopAdapter\n");
        if (adaptExt->removed == TRUE) {
            RhelShutDown(DeviceExtension);
        }
        status = ScsiAdapterControlSuccess;
        break;
    }
    case ScsiRestartAdapter: {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, " ScsiRestartAdapter\n");
        RhelShutDown(DeviceExtension);
        if (!VirtIoHwReinitialize(DeviceExtension))
        {
           RhelDbgPrint(TRACE_LEVEL_FATAL, " ScsiRestartAdapter Cannot reinitialize HW\n");
           break;
        }
        adaptExt->removed = FALSE;
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
    PADAPTER_EXTENSION  adaptExt = NULL;
    ULONGLONG           old_features = 0;
    /* The adapter is being restarted and we need to bring it back up without
     * running any passive-level code. Note that VirtIoFindAdapter is *not*
     * called on restart.
     */
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    old_features = adaptExt->features;
    if (InitVirtIODevice(DeviceExtension) != SP_RETURN_FOUND) {
        return FALSE;
    }
    RhelGetDiskGeometry(DeviceExtension);
    RhelSetGuestFeatures(DeviceExtension);

    if (!VirtIoHwInitialize(DeviceExtension)) {
        return FALSE;
    }

    if (CHECKBIT((old_features ^ adaptExt->features), VIRTIO_BLK_F_RO)) {
        adaptExt->sense_info.senseKey = SCSI_SENSE_DATA_PROTECT;
        adaptExt->sense_info.additionalSenseCode = SCSI_ADSENSE_WRITE_PROTECT;
#if (NTDDI_VERSION > NTDDI_WIN7)
        adaptExt->sense_info.additionalSenseCodeQualifier = SCSI_SENSEQ_SPACE_ALLOC_FAILED_WRITE_PROTECT;//SCSI_ADSENSE_NO_SENSE;
#else
        adaptExt->sense_info.additionalSenseCodeQualifier = SCSI_ADSENSE_NO_SENSE;
#endif
        adaptExt->check_condition = TRUE;
        DeviceChangeNotification(DeviceExtension, TRUE);
    }
    return TRUE;
}

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

    cdb      = SRB_CDB(Srb);
    srbExt   = SRB_EXTENSION(Srb);
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " Srb = 0x%p\n", Srb);

#ifdef DBG
    InterlockedIncrement((LONG volatile*)&adaptExt->srb_cnt);
#endif
    if(SRB_PATH_ID(Srb) || SRB_TARGET_ID(Srb) || SRB_LUN(Srb) || ((adaptExt->removed == TRUE))) {
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_NO_DEVICE);
        return FALSE;
    }

    RtlZeroMemory(srbExt, sizeof(*srbExt));

    if (SRB_FUNCTION(Srb) != SRB_FUNCTION_EXECUTE_SCSI )
    {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Srb = 0x%p Function = 0x%x\n", Srb, SRB_FUNCTION(Srb));
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
        return TRUE;
    }

    if (!cdb)
    {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " no CDB ( Srb = 0x%p on %d::%d::%d Function = 0x%x)\n", Srb, SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb), SRB_FUNCTION(Srb));
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
        return TRUE;
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
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
            return TRUE;
        }
    }

    lba = RhelGetLba(DeviceExtension, cdb);
    blocks = (SRB_DATA_TRANSFER_LENGTH(Srb) + adaptExt->info.blk_size - 1) / adaptExt->info.blk_size;
    if (lba > adaptExt->lastLBA) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " SRB_STATUS_BAD_SRB_BLOCK_LENGTH lba = %llu lastLBA= %llu\n", lba, adaptExt->lastLBA);
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        return FALSE;
    }
    if ((lba + blocks) > adaptExt->lastLBA) {
        blocks = (ULONG)(adaptExt->lastLBA + 1 - lba);
        RhelDbgPrint(TRACE_LEVEL_ERROR, " lba = %llu lastLBA= %llu blocks = %lu\n", lba, adaptExt->lastLBA, blocks);
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (blocks * adaptExt->info.blk_size));
    }

    sgList = StorPortGetScatterGatherList(DeviceExtension, Srb);
    if (!sgList) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " no SGL\n");
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_BAD_FUNCTION);
        return FALSE;
    }

    sgMaxElements = min((MAX_PHYS_SEGMENTS + 1), sgList->NumberOfElements);

    for (i = 0, sgElement = 1; i < sgMaxElements; i++, sgElement++) {
        srbExt->sg[sgElement].physAddr = sgList->List[i].PhysicalAddress;
        srbExt->sg[sgElement].length   = sgList->List[i].Length;
    }

    srbExt->vbr.out_hdr.sector = lba;
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req            = (PVOID)Srb;
    srbExt->fua                = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_FLUSH) ? (cdb->CDB10.ForceUnitAccess == 1) : FALSE;

    if (SRB_FLAGS(Srb) & SRB_FLAGS_DATA_OUT) {
        srbExt->vbr.out_hdr.type = VIRTIO_BLK_T_OUT;
        srbExt->out = sgElement;
        srbExt->in = 1;
    } else {
        srbExt->vbr.out_hdr.type = VIRTIO_BLK_T_IN;
        srbExt->out = 1;
        srbExt->in = sgElement;
    }

    srbExt->sg[0].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.out_hdr, &dummy);
    srbExt->sg[0].length = sizeof(srbExt->vbr.out_hdr);

    srbExt->sg[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &dummy);
    srbExt->sg[sgElement].length = sizeof(srbExt->vbr.status);

    return TRUE;
}

#ifdef MSI_SUPPORTED
BOOLEAN
VirtIoMSInterruptRoutine (
    IN PVOID  DeviceExtension,
    IN ULONG  MessageID
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (MessageID > adaptExt->num_queues || adaptExt->removed == TRUE) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " MessageID = %d\n", MessageID);
        return FALSE;
    }

    if (adaptExt->msix_one_vector) {
        MessageID = 1;
    } else {
        if (MessageID == VIRTIO_BLK_MSIX_CONFIG_VECTOR) {
            RhelGetDiskGeometry(DeviceExtension);
            adaptExt->sense_info.senseKey = SCSI_SENSE_UNIT_ATTENTION;
            adaptExt->sense_info.additionalSenseCode = SCSI_ADSENSE_PARAMETERS_CHANGED;
            adaptExt->sense_info.additionalSenseCodeQualifier = SCSI_SENSEQ_CAPACITY_DATA_CHANGED;
            adaptExt->check_condition = TRUE;
            DeviceChangeNotification(DeviceExtension, TRUE);
            return TRUE;
        }
    }

    if (!CompleteDPC(DeviceExtension, MessageID)) {
        VioStorCompleteRequest(DeviceExtension, MessageID, TRUE);
    }

    return TRUE;
}
#endif

UCHAR
RhelScsiGetInquiryData(
    IN PVOID DeviceExtension,
    IN OUT PSRB_TYPE Srb
    )
{

    PINQUIRYDATA InquiryData = NULL;
    ULONG dataLen = 0;
    UCHAR SrbStatus = SRB_STATUS_INVALID_LUN;
    PCDB cdb = SRB_CDB(Srb);
    PADAPTER_EXTENSION adaptExt = NULL;
#if (NTDDI_VERSION > NTDDI_WIN7)
    STOR_UNIT_ATTRIBUTES attributes = {0};
#endif

    if (!cdb)
        return SRB_STATUS_ERROR;

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
        SupportPages->SupportedPageList[0] = VPD_SUPPORTED_PAGES;
        SupportPages->SupportedPageList[1] = VPD_SERIAL_NUMBER;
        SupportPages->SupportedPageList[2] = VPD_DEVICE_IDENTIFIERS;
        SupportPages->PageLength = 3;
#if (NTDDI_VERSION >= NTDDI_WIN7)
        SupportPages->SupportedPageList[3] = VPD_BLOCK_LIMITS;
        SupportPages->PageLength = 4;
#if (NTDDI_VERSION > NTDDI_WIN7)
        if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_DISCARD)) {
            SupportPages->SupportedPageList[4] = VPD_BLOCK_DEVICE_CHARACTERISTICS;
            SupportPages->SupportedPageList[5] = VPD_LOGICAL_BLOCK_PROVISIONING;
            SupportPages->PageLength = 6;
        }
#endif
#endif
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_SUPPORTED_PAGES_PAGE) + SupportPages->PageLength));
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_SERIAL_NUMBER) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)) {

        PVPD_SERIAL_NUMBER_PAGE SerialPage;
        SerialPage = (PVPD_SERIAL_NUMBER_PAGE)SRB_DATA_BUFFER(Srb);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, "VPD_SERIAL_NUMBER dataLen = %d.\n", dataLen);

        RtlZeroMemory(SerialPage, dataLen);
        SerialPage->DeviceType = DIRECT_ACCESS_DEVICE;
        SerialPage->DeviceTypeQualifier = DEVICE_CONNECTED;
        SerialPage->PageCode = VPD_SERIAL_NUMBER;

        if (!adaptExt->sn_ok) {
            if (!RhelGetSerialNumber(DeviceExtension, Srb)) {
                RhelDbgPrint(TRACE_LEVEL_ERROR, "RhelGetSerialNumber failed.\n");
                return SRB_STATUS_ERROR;
            }
            return SRB_STATUS_PENDING;
        }

        if(dataLen >= 0x18) {
            UCHAR len = strlen(adaptExt->sn);
            SerialPage->PageLength = min(BLOCK_SERIAL_STRLEN, len);
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, "PageLength = %d (%d)\n", SerialPage->PageLength, len);
            StorPortCopyMemory(&SerialPage->SerialNumber, &adaptExt->sn, SerialPage->PageLength);
            SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_SERIAL_NUMBER_PAGE) + SerialPage->PageLength));
        }
        else {
            RhelDbgPrint(TRACE_LEVEL_FATAL, "RhelGetSerialNumber invalid dataLen = %d.\n", dataLen);
            return SRB_STATUS_INVALID_REQUEST;
        }
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_DEVICE_IDENTIFIERS) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)) {

        PVPD_IDENTIFICATION_PAGE       IdentificationPage = NULL;
        PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr = NULL;
        UCHAR len = 0;

        IdentificationPage = (PVPD_IDENTIFICATION_PAGE)SRB_DATA_BUFFER(Srb);
        memset(IdentificationPage, 0, sizeof(VPD_IDENTIFICATION_PAGE));
        IdentificationPage->PageCode = VPD_DEVICE_IDENTIFIERS;

        IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)IdentificationPage->Descriptors;
        memset(IdentificationDescr, 0, sizeof(VPD_IDENTIFICATION_DESCRIPTOR));

        if (!adaptExt->sn_ok) {
            if (!RhelGetSerialNumber(DeviceExtension, Srb)) {
                RhelDbgPrint(TRACE_LEVEL_ERROR, "RhelGetSerialNumber failed.\n");
                return SRB_STATUS_ERROR;
            }
            return SRB_STATUS_PENDING;
        }

        ReportDeviceIdentifier(DeviceExtension, Srb);
    }
#if (NTDDI_VERSION >= NTDDI_WIN7)
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_BLOCK_LIMITS) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1) &&
             (dataLen >= 0x14)) {

        PVPD_BLOCK_LIMITS_PAGE LimitsPage;
        ULONG max_io_size = VIOBLK_MAX_TRANSFER / adaptExt->info.size_max;
        USHORT pageLen = 0x10;

        LimitsPage = (PVPD_BLOCK_LIMITS_PAGE)SRB_DATA_BUFFER(Srb);
        LimitsPage->DeviceType = DIRECT_ACCESS_DEVICE;
        LimitsPage->DeviceTypeQualifier = DEVICE_CONNECTED;
        LimitsPage->PageCode = VPD_BLOCK_LIMITS;
        REVERSE_BYTES_SHORT(&LimitsPage->OptimalTransferLengthGranularity, &adaptExt->info.min_io_size);
        REVERSE_BYTES(&LimitsPage->MaximumTransferLength, &max_io_size);
        REVERSE_BYTES(&LimitsPage->OptimalTransferLength, &adaptExt->info.opt_io_size);
#if (NTDDI_VERSION > NTDDI_WIN7)
        if ((CHECKBIT(adaptExt->features, VIRTIO_BLK_F_DISCARD)) &&
            (dataLen >= 0x14)) {
            ULONG opt_unmap_granularity = 8;
            pageLen = 0x3c;
            REVERSE_BYTES(&LimitsPage->MaximumUnmapLBACount, &adaptExt->info.max_discard_sectors);
            REVERSE_BYTES(&LimitsPage->MaximumUnmapBlockDescriptorCount, &adaptExt->info.max_discard_seg);
            REVERSE_BYTES(&LimitsPage->OptimalUnmapGranularity, &opt_unmap_granularity);
            REVERSE_BYTES(&LimitsPage->UnmapGranularityAlignment, &adaptExt->info.discard_sector_alignment);
            LimitsPage->UGAValid = adaptExt->info.discard_sector_alignment ? 1 : 0;
        }
#endif
        REVERSE_BYTES_SHORT(&LimitsPage->PageLength, &pageLen);
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (FIELD_OFFSET(VPD_BLOCK_LIMITS_PAGE, Reserved0) + pageLen));
    }

#if (NTDDI_VERSION > NTDDI_WIN7)
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_BLOCK_DEVICE_CHARACTERISTICS) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1) &&
             (dataLen >= 0x08)) {

        PVPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE CharacteristicsPage;

        CharacteristicsPage = (PVPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE)SRB_DATA_BUFFER(Srb);
        CharacteristicsPage->DeviceType = DIRECT_ACCESS_DEVICE;
        CharacteristicsPage->DeviceTypeQualifier = DEVICE_CONNECTED;
        CharacteristicsPage->PageCode = VPD_BLOCK_DEVICE_CHARACTERISTICS;
        CharacteristicsPage->PageLength = 0x3C;
        CharacteristicsPage->MediumRotationRateMsb = 0;
        CharacteristicsPage->MediumRotationRateLsb = 0;
        CharacteristicsPage->NominalFormFactor = 0;
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_LOGICAL_BLOCK_PROVISIONING) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1) &&
             (dataLen >= 0x08)) {

        PVPD_LOGICAL_BLOCK_PROVISIONING_PAGE ProvisioningPage;
        USHORT pageLen = 0x04;

        ProvisioningPage = (PVPD_LOGICAL_BLOCK_PROVISIONING_PAGE)SRB_DATA_BUFFER(Srb);
        ProvisioningPage->DeviceType = DIRECT_ACCESS_DEVICE;
        ProvisioningPage->DeviceTypeQualifier = DEVICE_CONNECTED;
        ProvisioningPage->PageCode = VPD_LOGICAL_BLOCK_PROVISIONING;
        REVERSE_BYTES_SHORT(&ProvisioningPage->PageLength, &pageLen);

        ProvisioningPage->DP = 0;
        ProvisioningPage->LBPRZ = 0;
        ProvisioningPage->LBPWS10 = 0;
        ProvisioningPage->LBPWS = 0;
        ProvisioningPage->LBPU = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_DISCARD) ? 1 : 0;
        ProvisioningPage->ProvisioningType = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_DISCARD) ? PROVISIONING_TYPE_THIN : PROVISIONING_TYPE_RESOURCE;
    }

#endif
#endif
    else if (dataLen > sizeof(INQUIRYDATA)) {
        StorPortMoveMemory(InquiryData, &adaptExt->inquiry_data, sizeof(INQUIRYDATA));
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(INQUIRYDATA)));
    } else {
        StorPortMoveMemory(InquiryData, &adaptExt->inquiry_data, dataLen);
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, dataLen);
    }

    StorPortSetDeviceQueueDepth(DeviceExtension,
        SRB_PATH_ID(Srb),
        SRB_TARGET_ID(Srb),
        SRB_LUN(Srb),
        adaptExt->queue_depth);
#if (NTDDI_VERSION > NTDDI_WIN7)
    attributes.DeviceAttentionSupported = 1;
    attributes.AsyncNotificationSupported = 1;
    attributes.D3ColdNotSupported = 1;

    StorPortSetUnitAttributes(DeviceExtension,
        (PSTOR_ADDRESS)&adaptExt->device_address,
        attributes);
#endif

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

    UNREFERENCED_PARAMETER( DeviceExtension );

    data[3]=8;
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

    if (!cdb)
        return SRB_STATUS_ERROR;

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
           cachePage->WriteCacheEnable = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_FLUSH) ? 1 : 0;

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
#if (NTDDI_VERSION >= NTDDI_WIN7)
    PREAD_CAPACITY16_DATA readCapEx;
#else
    PREAD_CAPACITY_DATA_EX readCapEx;
#endif
    u64 lastLBA;
    EIGHT_BYTE lba;
    u64 blocksize;
    PADAPTER_EXTENSION adaptExt= (PADAPTER_EXTENSION)DeviceExtension;
    PCDB cdb = SRB_CDB(Srb);
    ULONG srbdatalen = 0;
    UCHAR  PMI = 0;

    if (!cdb)
        return SRB_STATUS_ERROR;

    readCap = (PREAD_CAPACITY_DATA)SRB_DATA_BUFFER(Srb);
#if (NTDDI_VERSION >= NTDDI_WIN7)
    readCapEx = (PREAD_CAPACITY16_DATA)SRB_DATA_BUFFER(Srb);
#else
    readCapEx = (PREAD_CAPACITY_DATA_EX)SRB_DATA_BUFFER(Srb);
#endif

    srbdatalen = SRB_DATA_TRANSFER_LENGTH(Srb);
    lba.AsULongLong = 0;
    if (cdb->CDB6GENERIC.OperationCode == SCSIOP_READ_CAPACITY16 ){
         PMI = cdb->READ_CAPACITY16.PMI & 1;
         REVERSE_BYTES_QUAD(&lba, &cdb->READ_CAPACITY16.LogicalBlock[0]);
    }

    if (!PMI && lba.AsULongLong) {
        PSENSE_DATA senseBuffer = NULL;
        UCHAR ScsiStatus = SCSISTAT_CHECK_CONDITION;
        SRB_GET_SENSE_INFO_BUFFER(Srb, senseBuffer);
        if (senseBuffer) {
            senseBuffer->SenseKey = SCSI_SENSE_ILLEGAL_REQUEST;
            senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_INVALID_CDB;
        }
        SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
        return SrbStatus;
    }

    blocksize = adaptExt->info.blk_size;
    lastLBA = adaptExt->info.capacity / (blocksize / SECTOR_SIZE) - 1;
    adaptExt->lastLBA = adaptExt->info.capacity;

    if (srbdatalen == sizeof(READ_CAPACITY_DATA)) {
        if (lastLBA > 0xFFFFFFFF) {
            readCap->LogicalBlockAddress = (ULONG)-1;
        } else {
            REVERSE_BYTES(&readCap->LogicalBlockAddress,
                          &lastLBA);
        }
        REVERSE_BYTES(&readCap->BytesPerBlock,
                          &blocksize);
    } else {
        REVERSE_BYTES_QUAD(&readCapEx->LogicalBlockAddress.QuadPart,
                          &lastLBA);
        REVERSE_BYTES(&readCapEx->BytesPerBlock,
                          &blocksize);

#if (NTDDI_VERSION >= NTDDI_WIN7)
        if (srbdatalen >= (ULONG)FIELD_OFFSET(READ_CAPACITY16_DATA, Reserved3)) {
            readCapEx->LogicalPerPhysicalExponent = adaptExt->info.physical_block_exp;
            srbdatalen = FIELD_OFFSET(READ_CAPACITY16_DATA, Reserved3);
            readCapEx->LBPME = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_DISCARD) ? 1 : 0;
            readCapEx->LBPRZ = 0;
            SRB_SET_DATA_TRANSFER_LENGTH(Srb, FIELD_OFFSET(READ_CAPACITY16_DATA, Reserved3));
        }
#endif
    }
    return SrbStatus;
}

UCHAR
RhelScsiVerify(
    IN PVOID DeviceExtension,
    IN OUT PSRB_TYPE Srb
)
{
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    ULONGLONG             lba;
    ULONG                 blocks;
    PADAPTER_EXTENSION adaptExt= (PADAPTER_EXTENSION)DeviceExtension;
    PCDB cdb = SRB_CDB(Srb);
    ULONG srbdatalen = 0;

    if (!cdb)
        return SRB_STATUS_ERROR;

    lba = RhelGetLba(DeviceExtension, cdb);
    blocks = RhelGetSectors(DeviceExtension, cdb);
    if ((lba + blocks) >= adaptExt->lastLBA) {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " lba = %llu lastLBA= %llu blocks = %lu\n", lba, adaptExt->lastLBA, blocks);
        SrbStatus = SRB_STATUS_INVALID_REQUEST;
    }
    return SrbStatus;
}

VOID
CompleteSRB(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    PADAPTER_EXTENSION adaptExt= (PADAPTER_EXTENSION)DeviceExtension;
#ifdef DBG
    InterlockedDecrement((LONG volatile*)&adaptExt->srb_cnt);
#endif
    StorPortNotification(RequestComplete,
                         DeviceExtension,
                         Srb);
}

VOID
FORCEINLINE
CompleteRequestWithStatus(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb,
    IN UCHAR status
    )
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (( SRB_FUNCTION(Srb) == SRB_FUNCTION_EXECUTE_SCSI ) &&
        ( adaptExt->check_condition == TRUE ) &&
        ( status == SRB_STATUS_SUCCESS ) &&
        ( !CHECKFLAG(SRB_FLAGS(Srb), SRB_FLAGS_DISABLE_AUTOSENSE) )) {
        PCDB cdb = SRB_CDB(Srb);

        if ( cdb != NULL ) {
            UCHAR OpCode = cdb->CDB6GENERIC.OperationCode;
            if (( OpCode != SCSIOP_INQUIRY ) &&
                ( OpCode != SCSIOP_REPORT_LUNS )) {
                if (SetSenseInfo(DeviceExtension, Srb)) {
                    status = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
                    adaptExt->check_condition = FALSE;
                }
            }
        }
    }
    SRB_SET_SRB_STATUS(Srb, status);
    CompleteSRB(DeviceExtension,
                Srb);
}

VOID
FORCEINLINE
DeviceChangeNotification(
    IN PVOID DeviceExtension,
    IN BOOLEAN bLun
)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
#if (NTDDI_VERSION > NTDDI_WIN7)
    StorPortStateChangeDetected(DeviceExtension,
                                bLun ? STATE_CHANGE_LUN : STATE_CHANGE_BUS,
                                (PSTOR_ADDRESS)&adaptExt->device_address,
                                0,
                                NULL,
                                NULL);
#else
    bLun;
    StorPortNotification( BusChangeDetected, DeviceExtension, 0);
#endif
     RhelDbgPrint(TRACE_LEVEL_FATAL, " StorPortStateChangeDetected.\n");
}

BOOLEAN
FORCEINLINE
SetSenseInfo(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSENSE_DATA senseInfoBuffer = NULL;
    UCHAR senseInfoBufferLength = 0;
    SRB_GET_SENSE_INFO_BUFFER(Srb, senseInfoBuffer);
    SRB_GET_SENSE_INFO_BUFFER_LENGTH(Srb, senseInfoBufferLength);
    if (senseInfoBuffer && (senseInfoBufferLength >= sizeof(SENSE_DATA))) {
        UCHAR ScsiStatus = SCSISTAT_CHECK_CONDITION;
        senseInfoBuffer->ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
        senseInfoBuffer->Valid = 1;
        senseInfoBuffer->SenseKey = adaptExt->sense_info.senseKey;
        senseInfoBuffer->AdditionalSenseLength = sizeof(SENSE_DATA) - FIELD_OFFSET(SENSE_DATA, AdditionalSenseLength); //0xb ??
        senseInfoBuffer->AdditionalSenseCode = adaptExt->sense_info.additionalSenseCode;
        senseInfoBuffer->AdditionalSenseCodeQualifier = adaptExt->sense_info.additionalSenseCodeQualifier;
        SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
        RhelDbgPrint(TRACE_LEVEL_FATAL, " senseKey = 0x%x asc = 0x%x ascq = 0x%x\n",
                    adaptExt->sense_info.senseKey,
                    adaptExt->sense_info.additionalSenseCode,
                    adaptExt->sense_info.additionalSenseCodeQualifier);
        return TRUE;
    }
    RhelDbgPrint(TRACE_LEVEL_FATAL, " INVALID senseInfoBuffer %p or senseInfoBufferLength = %d\n",
                    senseInfoBuffer, senseInfoBufferLength);
    return FALSE;
}

BOOLEAN
FORCEINLINE
CompleteDPC(
    IN PVOID DeviceExtension,
    IN ULONG MessageID
    )
{
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if(!adaptExt->dump_mode && adaptExt->dpc_ok) {
        StorPortIssueDpc(DeviceExtension,
                         &adaptExt->dpc[MessageID - 1],
                         ULongToPtr(MessageID),
                         ULongToPtr(FALSE));
        return TRUE;
    }
    return FALSE;
}

VOID
ReportDeviceIdentifier(
    IN PVOID DeviceExtension,
    IN PSRB_TYPE Srb
    )
{
    PVPD_IDENTIFICATION_PAGE       IdentificationPage = NULL;
    PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr = NULL;
    PADAPTER_EXTENSION  adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    UCHAR len = strlen(adaptExt->sn);

    IdentificationPage = (PVPD_IDENTIFICATION_PAGE)SRB_DATA_BUFFER(Srb);
    IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)IdentificationPage->Descriptors;
    if (len) {
        IdentificationDescr->IdentifierLength = min(BLOCK_SERIAL_STRLEN, len);
        IdentificationDescr->CodeSet = VpdCodeSetAscii;
        IdentificationDescr->IdentifierType = VpdIdentifierTypeVendorSpecific;
        StorPortCopyMemory(&IdentificationDescr->Identifier, &adaptExt->sn, IdentificationDescr->IdentifierLength);
    }
    else {
        IdentificationDescr->CodeSet = VpdCodeSetBinary;
        IdentificationDescr->IdentifierType = VpdIdentifierTypeEUI64;
        IdentificationDescr->IdentifierLength = 8;
        IdentificationDescr->Identifier[0] = (adaptExt->system_io_bus_number >> 12) & 0xF;
        IdentificationDescr->Identifier[1] = (adaptExt->system_io_bus_number >> 8) & 0xF;
        IdentificationDescr->Identifier[2] = (adaptExt->system_io_bus_number >> 4) & 0xF;
        IdentificationDescr->Identifier[3] = adaptExt->system_io_bus_number & 0xF;
        IdentificationDescr->Identifier[4] = (adaptExt->slot_number >> 12) & 0xF;
        IdentificationDescr->Identifier[5] = (adaptExt->slot_number >> 8) & 0xF;
        IdentificationDescr->Identifier[6] = (adaptExt->slot_number >> 4) & 0xF;
        IdentificationDescr->Identifier[7] = adaptExt->slot_number & 0xF;
    }
    IdentificationPage->PageLength = sizeof(VPD_IDENTIFICATION_DESCRIPTOR) + IdentificationDescr->IdentifierLength;
    SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_IDENTIFICATION_PAGE) +
        IdentificationPage->PageLength));
}

UCHAR DeviceToSrbStatus(UCHAR status)
{
    switch (status) {
    case VIRTIO_BLK_S_OK:
        return SRB_STATUS_SUCCESS;
    case VIRTIO_BLK_S_IOERR:
        RhelDbgPrint(TRACE_LEVEL_ERROR, " VIRTIO_BLK_S_IOERR\n");
        return SRB_STATUS_ERROR;
    case VIRTIO_BLK_S_UNSUPP:
        RhelDbgPrint(TRACE_LEVEL_ERROR, " VIRTIO_BLK_S_UNSUPP\n");
        return SRB_STATUS_INVALID_REQUEST;
    }
    RhelDbgPrint(TRACE_LEVEL_FATAL, " Unknown device status %x\n", status);
    return SRB_STATUS_ERROR;
}

VOID
VioStorCompleteRequest(
    IN PVOID DeviceExtension,
    IN ULONG MessageID,
    IN BOOLEAN bIsr
)
{
    unsigned int        len = 0;
    PADAPTER_EXTENSION  adaptExt = NULL;
    ULONG               QueueNumber = MessageID - 1;
    STOR_LOCK_HANDLE    queueLock = { 0 };
    struct virtqueue    *vq = NULL;
    pblk_req            vbr = NULL;
    PSRB_TYPE           Srb = NULL;
    PSRB_EXTENSION      srbExt = NULL;
    LIST_ENTRY          complete_list;
    UCHAR               srbStatus = SRB_STATUS_SUCCESS;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " ---> MessageID 0x%x\n", MessageID);

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    vq = adaptExt->vq[QueueNumber];

    InitializeListHead(&complete_list);

    VioStorVQLock(DeviceExtension, MessageID, &queueLock, bIsr);
    do {
        virtqueue_disable_cb(vq);
        while ((vbr = (pblk_req)virtqueue_get_buf(vq, &len)) != NULL) {
            InsertTailList(&complete_list, &vbr->list_entry);
#ifdef DBG
            InterlockedDecrement((LONG volatile*)&adaptExt->inqueue_cnt);
#endif
        }
    } while (!virtqueue_enable_cb(vq));
    VioStorVQUnlock(DeviceExtension, MessageID, &queueLock, bIsr);

    while (!IsListEmpty(&complete_list)) {
        vbr = (pblk_req)RemoveHeadList(&complete_list);
        Srb = (PSRB_TYPE)vbr->req;
        if (vbr->out_hdr.type == VIRTIO_BLK_T_GET_ID) {
            adaptExt->sn_ok = TRUE;
            if (Srb) {
                PCDB cdb = SRB_CDB(Srb);

                if (!cdb)
                    continue;

                if ((cdb->CDB6INQUIRY3.PageCode == VPD_SERIAL_NUMBER) &&
                    (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)) {
                    PVPD_SERIAL_NUMBER_PAGE SerialPage;
                    ULONG dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);
                    UCHAR len = strlen(adaptExt->sn);

                    SerialPage = (PVPD_SERIAL_NUMBER_PAGE)SRB_DATA_BUFFER(Srb);
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, "dataLen = %d\n", dataLen);
                    RtlZeroMemory(SerialPage, dataLen);
                    SerialPage->DeviceType = DIRECT_ACCESS_DEVICE;
                    SerialPage->DeviceTypeQualifier = DEVICE_CONNECTED;
                    SerialPage->PageCode = VPD_SERIAL_NUMBER;

                    SerialPage->PageLength = min(BLOCK_SERIAL_STRLEN, len);
                    StorPortCopyMemory(&SerialPage->SerialNumber, &adaptExt->sn, SerialPage->PageLength);
                    RhelDbgPrint(TRACE_LEVEL_FATAL, "PageLength = %d (%d)\n", SerialPage->PageLength, len);

                    SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_SERIAL_NUMBER_PAGE) + SerialPage->PageLength));
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
                }
                else if ((cdb->CDB6INQUIRY3.PageCode == VPD_DEVICE_IDENTIFIERS) &&
                    (cdb->CDB6INQUIRY3.EnableVitalProductData == 1))
                {
                    ReportDeviceIdentifier(DeviceExtension, Srb);
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
                }
            }
            continue;
        }
        if (Srb) {
            srbExt = SRB_EXTENSION(Srb);
            srbStatus = DeviceToSrbStatus(vbr->status);
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " srb %p, QueueNumber %lu, MessageId %lu, srbExt->MessageId %lu.\n",
                        Srb, QueueNumber, MessageID, srbExt->MessageID);
            if (srbExt->fua == TRUE) {
                SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
                if (!RhelDoFlush(DeviceExtension, Srb, TRUE, bIsr)) {
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_ERROR);
                }
                srbExt->fua = FALSE;
            }
            else {
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, srbStatus);
            }
        }
    }

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " <--- MessageID 0x%x\n", MessageID);
}

#pragma warning(disable: 4100 4701)
VOID
CompleteDpcRoutine(
    IN PSTOR_DPC  Dpc,
    IN PVOID Context,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )
{
    ULONG MessageID = PtrToUlong(SystemArgument1);
    VioStorCompleteRequest(Context, MessageID, FALSE);
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
    RhelDbgPrint(TRACE_LEVEL_FATAL, "Ran out of memory in VioStorPoolAlloc(%Id)\n", size);
    return NULL;
}
