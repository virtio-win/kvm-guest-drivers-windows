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
#include "ntddi_ver.h"
#include "virtio_stor_reg_helper.h"

#if defined(EVENT_TRACING)
#include "virtio_stor.tmh"
#endif

BOOLEAN IsCrashDumpMode;

#ifdef EVENT_TRACING
PVOID TraceContext = NULL;
VOID WppCleanupRoutine(PVOID arg1)
{
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " WppCleanupRoutine\n");
    WPP_CLEANUP(NULL, TraceContext);
}
#endif

sp_DRIVER_INITIALIZE DriverEntry;
HW_INITIALIZE VirtIoHwInitialize;
HW_BUILDIO VirtIoBuildIo;
HW_STARTIO VirtIoStartIo;
HW_FIND_ADAPTER VirtIoFindAdapter;
HW_RESET_BUS VirtIoResetBus;
HW_ADAPTER_CONTROL VirtIoAdapterControl;
HW_UNIT_CONTROL VirtIoUnitControl;
HW_INTERRUPT VirtIoInterrupt;
HW_MESSAGE_SIGNALED_INTERRUPT_ROUTINE VirtIoMSInterruptRoutine;
HW_DPC_ROUTINE CompleteDpcRoutine;
HW_PASSIVE_INITIALIZE_ROUTINE VirtIoPassiveInitializeRoutine;

ULONG
DriverEntry(IN PVOID DriverObject, IN PVOID RegistryPath);

ULONG
VirtIoFindAdapter(IN PVOID DeviceExtension,
                  IN PVOID HwContext,
                  IN PVOID BusInformation,
                  IN PCHAR ArgumentString,
                  IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                  OUT PBOOLEAN Again);

static ULONG InitVirtIODevice(PVOID DeviceExtension);

BOOLEAN
VirtIoHwInitialize(IN PVOID DeviceExtension);

BOOLEAN
VirtIoHwReinitialize(IN PVOID DeviceExtension);

BOOLEAN
VirtIoPassiveInitializeRoutine(IN PVOID DeviceExtension);

static BOOLEAN InitializeVirtualQueues(PADAPTER_EXTENSION adaptExt, ULONG numQueues);

BOOLEAN
VirtIoBuildIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb);

BOOLEAN
VirtIoStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb);

BOOLEAN
VirtIoInterrupt(IN PVOID DeviceExtension);

BOOLEAN
VirtIoMSInterruptRoutine(IN PVOID DeviceExtension, IN ULONG MessageId);

static BOOLEAN VirtIoMSInterruptWorker(IN PVOID DeviceExtension, IN ULONG MessageId);

VOID CompleteDpcRoutine(IN PSTOR_DPC Dpc, IN PVOID Context, IN PVOID SystemArgument1, IN PVOID SystemArgument2);

VOID CompletePendingRequests(IN PVOID DeviceExtension);

VOID DispatchQueue(IN PVOID DeviceExtension, IN ULONG MessageId);

BOOLEAN
VirtIoResetBus(IN PVOID DeviceExtension, IN ULONG PathId);

SCSI_ADAPTER_CONTROL_STATUS
VirtIoAdapterControl(IN PVOID DeviceExtension, IN SCSI_ADAPTER_CONTROL_TYPE ControlType, IN PVOID Parameters);

SCSI_UNIT_CONTROL_STATUS
VirtIoUnitControl(IN PVOID DeviceExtension, IN SCSI_UNIT_CONTROL_TYPE ControlType, IN PVOID Parameters);

UCHAR
RhelScsiGetInquiryData(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb);

UCHAR
RhelScsiGetModeSense(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb);

UCHAR
RhelScsiGetCapacity(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb);

VOID RhelSetGuestFeatures(IN PVOID DeviceExtension);

UCHAR
RhelScsiVerify(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb);

UCHAR
RhelScsiReportLuns(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb);

VOID FORCEINLINE DeviceChangeNotification(IN PVOID DeviceExtension,
                                          IN BOOLEAN bLun,
                                          IN INL_FUNC_IDX idx_ICN,
                                          IN INL_FUNC_IDX idx_IFN);

BOOLEAN
FORCEINLINE
SetSenseInfo(IN PVOID DeviceExtension, IN PSRB_TYPE Srb, IN INL_FUNC_IDX idx_ICN, IN INL_FUNC_IDX idx_IFN);

UCHAR DeviceToSrbStatus(IN UCHAR status);

UCHAR FirmwareRequest(IN PVOID DeviceExtension, IN PSRB_TYPE Srb);

VOID ReportDeviceIdentifier(IN PVOID DeviceExtension, IN PSRB_TYPE Srb);

ULONG
DriverEntry(PVOID DriverObject, PVOID RegistryPath)
{

    HW_INITIALIZATION_DATA hwInitData;
    ULONG initResult;
    ANSI_STRING aRegistryPath;
    NTSTATUS u2a_status;

#ifdef EVENT_TRACING
    STORAGE_TRACE_INIT_INFO initInfo;
#endif

    InitializeDebugPrints((PDRIVER_OBJECT)DriverObject, (PUNICODE_STRING)RegistryPath);

    RhelDbgPrint(TRACE_LEVEL_ERROR, " Viostor driver started...built on %s %s\n", __DATE__, __TIME__);
    IsCrashDumpMode = FALSE;
    if (RegistryPath == NULL)
    {
        IsCrashDumpMode = TRUE;
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " DriverEntry: Crash dump mode\n");
    }

    RtlZeroMemory(&hwInitData, sizeof(HW_INITIALIZATION_DATA));

    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

    hwInitData.HwFindAdapter = VirtIoFindAdapter;
    hwInitData.HwInitialize = VirtIoHwInitialize;
    hwInitData.HwStartIo = VirtIoStartIo;
    hwInitData.HwInterrupt = VirtIoInterrupt;
    hwInitData.HwResetBus = VirtIoResetBus;
    hwInitData.HwAdapterControl = VirtIoAdapterControl;
    hwInitData.HwUnitControl = VirtIoUnitControl;
    hwInitData.HwBuildIo = VirtIoBuildIo;

    hwInitData.NeedPhysicalAddresses = TRUE;
    hwInitData.TaggedQueuing = TRUE;
    hwInitData.AutoRequestSense = TRUE;
    hwInitData.MultipleRequestPerLu = TRUE;

    hwInitData.DeviceExtensionSize = sizeof(ADAPTER_EXTENSION);
    hwInitData.SrbExtensionSize = sizeof(SRB_EXTENSION);

    hwInitData.AdapterInterfaceType = PCIBus;

    /* Virtio doesn't specify the number of BARs used by the device; it may
     * be one, it may be more. PCI_TYPE0_ADDRESSES, the theoretical maximum
     * on PCI, is a safe upper bound.
     */
    hwInitData.NumberOfAccessRanges = PCI_TYPE0_ADDRESSES;
    hwInitData.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;

    // hwInitData.FeatureSupport |= STOR_FEATURE_FULL_PNP_DEVICE_CAPABILITIES;
    // hwInitData.FeatureSupport |= STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE;

    hwInitData.SrbTypeFlags = SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK;
    hwInitData.AddressTypeFlags = ADDRESS_TYPE_FLAG_BTL8;

    initResult = StorPortInitialize(DriverObject, RegistryPath, &hwInitData, NULL);

#ifdef EVENT_TRACING
    TraceContext = NULL;

    RtlZeroMemory(&initInfo, sizeof(STORAGE_TRACE_INIT_INFO));
    initInfo.Size = sizeof(STORAGE_TRACE_INIT_INFO);
    initInfo.DriverObject = DriverObject;
    initInfo.NumErrorLogRecords = 5;
    initInfo.TraceCleanupRoutine = WppCleanupRoutine;
    initInfo.TraceContext = NULL;

    WPP_INIT_TRACING(DriverObject, RegistryPath, &initInfo);

    if (initInfo.TraceContext != NULL)
    {
        TraceContext = initInfo.TraceContext;
    }
#endif

    RhelDbgPrint(TRACE_LEVEL_NONE, " VIOSTOR driver starting...");
    RhelDbgPrint(TRACE_LEVEL_NONE, " Built on %s at %s \n", __DATE__, __TIME__);
    RtlZeroMemory(&aRegistryPath, sizeof(aRegistryPath));
    u2a_status = RtlUnicodeStringToAnsiString(&aRegistryPath, RegistryPath, TRUE);
    if (u2a_status == STATUS_SUCCESS)
    {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, " RegistryPath : %s \n", aRegistryPath.Buffer);
        RtlFreeAnsiString(&aRegistryPath);
    }
    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " Crash dump mode : %s \n",
                 (IsCrashDumpMode) ? "ACTIVATED" : "NOT ACTIVATED");
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " StorPortInitialize() returned : 0x%x (%lu) \n", initResult, initResult);

    if (strcmp(GetNtddiDesc(), "UNKNOWN") == 0)
    {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, " NTDDI_VERSION : 0x%x \n", NTDDI_VERSION);
    }
    else
    {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE, " %s \n", GetNtddiDesc());
    }

    return initResult;
}

static ULONG InitVirtIODevice(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    NTSTATUS status;

    status = virtio_device_initialize(&adaptExt->vdev, &VioStorSystemOps, adaptExt, adaptExt->msix_enabled);

    if (!NT_SUCCESS(status))
    {
        LogError(adaptExt, SP_INTERNAL_ADAPTER_ERROR, __LINE__);
        RhelDbgPrint(TRACE_LEVEL_FATAL, " Failed to initialize virtio device, error %x\n", status);
        if (status == STATUS_DEVICE_NOT_CONNECTED)
        {
            return SP_RETURN_NOT_FOUND;
        }
        return SP_RETURN_ERROR;
    }
    return SP_RETURN_FOUND;
}

ULONG
VirtIoFindAdapter(PVOID DeviceExtension,
                  PVOID HwContext,
                  PVOID BusInformation,
                  PCHAR ArgumentString,
                  PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                  PBOOLEAN Again)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PACCESS_RANGE accessRange;
    USHORT queue_length;
    ULONG pci_cfg_len;
    ULONG access_range_idx;
    ULONG init_result;

    ULONG vq_req_idx;
    ULONG num_cpus;
    ULONG max_cpus;
    ULONG max_queues;
    ULONG max_sectors;
    ULONG max_segs_candidate[3] = {0};
    ULONG Size;
    ULONG HeapSize;

    PVOID uncachedExtensionVa;
    ULONG extensionSize;
    ULONG uncachedExtPad1 = 0;
    ULONG uncachedExtPad2 = 4096;
    ULONG uncachedExtSize;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);
    UNREFERENCED_PARAMETER(Again);

    RtlZeroMemory(adaptExt, sizeof(ADAPTER_EXTENSION));

    adaptExt->system_io_bus_number = ConfigInfo->SystemIoBusNumber;
    adaptExt->slot_number = ConfigInfo->SlotNumber;
    adaptExt->dump_mode = IsCrashDumpMode;

    ConfigInfo->Master = TRUE;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->DmaWidth = Width32Bits;
    ConfigInfo->Dma32BitAddresses = TRUE;
    ConfigInfo->Dma64BitAddresses = SCSI_DMA64_MINIPORT_FULL64BIT_SUPPORTED;
    ConfigInfo->WmiDataProvider = FALSE;
    ConfigInfo->AlignmentMask = 0x3;
    ConfigInfo->MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->HwMSInterruptRoutine = VirtIoMSInterruptRoutine;

    pci_cfg_len = StorPortGetBusData(DeviceExtension,
                                     PCIConfiguration,
                                     ConfigInfo->SystemIoBusNumber,
                                     (ULONG)ConfigInfo->SlotNumber,
                                     (PVOID)&adaptExt->pci_config_buf,
                                     sizeof(adaptExt->pci_config_buf));

    if (pci_cfg_len != sizeof(adaptExt->pci_config_buf))
    {
        RhelDbgPrint(TRACE_LEVEL_FATAL, " CANNOT READ PCI CONFIGURATION SPACE %d\n", pci_cfg_len);
        return SP_RETURN_ERROR;
    }

    /* initialize the pci_bars array */
    for (access_range_idx = 0; access_range_idx < ConfigInfo->NumberOfAccessRanges; access_range_idx++)
    {
        accessRange = *ConfigInfo->AccessRanges + access_range_idx;
        if (accessRange->RangeLength != 0)
        {
            int BAR_idx = virtio_get_bar_index(&adaptExt->pci_config, accessRange->RangeStart);
            if (BAR_idx == -1)
            {
                RhelDbgPrint(TRACE_LEVEL_FATAL, " Cannot get index for BAR %I64d\n", accessRange->RangeStart.QuadPart);
                return FALSE;
            }
            adaptExt->pci_bars[BAR_idx].BasePA = accessRange->RangeStart;
            adaptExt->pci_bars[BAR_idx].uLength = accessRange->RangeLength;
            adaptExt->pci_bars[BAR_idx].bPortSpace = !accessRange->RangeInMemory;
        }
    }

    adaptExt->msix_enabled = FALSE;
    {
        UCHAR CapOffset;
        PPCI_MSIX_CAPABILITY pMsixCapOffset;
        PPCI_COMMON_HEADER pPciComHeader;
        pPciComHeader = &adaptExt->pci_config;
        if ((pPciComHeader->Status & PCI_STATUS_CAPABILITIES_LIST) == 0)
        {
            RhelDbgPrint(TRACE_LEVEL_FATAL, " NO CAPABILITIES_LIST\n");
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
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                     " MessageControl.TableSize = %d\n",
                                     pMsixCapOffset->MessageControl.TableSize);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                     " MessageControl.FunctionMask = %d\n",
                                     pMsixCapOffset->MessageControl.FunctionMask);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                     " MessageControl.MSIXEnable = %d\n",
                                     pMsixCapOffset->MessageControl.MSIXEnable);

                        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                     " MessageTable = %lu\n",
                                     pMsixCapOffset->MessageTable.TableOffset);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                     " PBATable = %lu\n",
                                     pMsixCapOffset->PBATable.TableOffset);
                        adaptExt->msix_enabled = (pMsixCapOffset->MessageControl.MSIXEnable == 1);
                        break;
                    }
                    else
                    {
                        CapOffset = pMsixCapOffset->Header.Next;
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                     " CapabilityID = %x, Next CapOffset = %x\n",
                                     pMsixCapOffset->Header.CapabilityID,
                                     CapOffset);
                    }
                }
            }
            else
            {
                RhelDbgPrint(TRACE_LEVEL_FATAL, " NOT A PCI_DEVICE_TYPE\n");
            }
        }
    }

    if (adaptExt->msix_enabled)
    {
        /* Always use InterruptSynchronizePerMessage mode when MSI-X is emabled
         * Pathways for legacy InterruptSynchronizeAll mode are retained for completeness
         */
        ConfigInfo->InterruptSynchronizationMode = InterruptSynchronizePerMessage;
        // ConfigInfo->InterruptSynchronizationMode = InterruptSynchronizeAll;
    }
    else
    {
        ConfigInfo->InterruptSynchronizationMode = InterruptSupportNone;
    }
    adaptExt->msix_sync_mode = ConfigInfo->InterruptSynchronizationMode;
    /* Trace does not include legacy InterruptSynchronizeAll mode - it will report InterruptSupportNone */
    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " InterruptSynchronizationMode : %s \n",
                 (!adaptExt->msix_sync_mode) ? "InterruptSupportNone" : "InterruptSynchronizePerMessage");

    adaptExt->device_address.Type = STOR_ADDRESS_TYPE_BTL8;
    adaptExt->device_address.Port = 0;
    adaptExt->device_address.AddressLength = STOR_ADDR_BTL8_ADDRESS_LENGTH;
    adaptExt->device_address.Path = 0;
    adaptExt->device_address.Target = 0;
    adaptExt->device_address.Lun = 0;

    /* initialize the virtual device */
    init_result = InitVirtIODevice(DeviceExtension);
    if (init_result != SP_RETURN_FOUND)
    {
        return init_result;
    }

    adaptExt->indirect = FALSE;
    adaptExt->max_segments = SCSI_MINIMUM_PHYSICAL_BREAKS;

    RhelGetDiskGeometry(DeviceExtension);
    RhelSetGuestFeatures(DeviceExtension);

    ConfigInfo->CachesData = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_FLUSH) ? TRUE : FALSE;

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_BLK_F_WCACHE = %s\n", (ConfigInfo->CachesData) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " VIRTIO_BLK_F_MQ = %s\n",
                 (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_MQ)) ? "ON" : "OFF");

    if (!adaptExt->dump_mode)
    {
        adaptExt->indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);
    }
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_RING_F_INDIRECT_DESC = %s\n", (adaptExt->indirect) ? "ON" : "OFF");

    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->MaximumNumberOfTargets = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;
    ConfigInfo->MaximumTransferLength = SP_UNINITIALIZED_VALUE;  // Unlimited
    ConfigInfo->NumberOfPhysicalBreaks = SP_UNINITIALIZED_VALUE; // Unlimited

    if (!adaptExt->dump_mode)
    {
        /* Begin max_segments determinations... */
        /* Allow user to override max_segments via reg key
         * [HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\viostor\Parameters\Device]
         * "MaxPhysicalSegments"={dword value here}
         *
         * ATTENTION : This should be the maximum number of memory pages (typ. 4KiB each) in a transfer
         *             Equivalent to any of the following:
         *             NumberOfPhysicalBreaks - 1 (NOPB includes known off-by-one error)
         *             VIRTIO_MAX_SG - 1
         *             MaximumSGList - 1 (SCSI Port legacy value)
         *
         * ATTENTION : This should be the same as the max_segments value of the backing device.
         *
         *  WARNING  : This is adapter-wide. Using disks with different max_segments values will
         *             result in sub-optimal performance.
         */
        if (VioStorReadRegistryParameter(DeviceExtension,
                                         REGISTRY_MAX_PH_SEGMENTS,
                                         FIELD_OFFSET(ADAPTER_EXTENSION, max_segments)))
        {
            /* Grab the maximum SEGMENTS value from the registry */
            RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                         " max_segments candidate was specified in the registry : %lu \n",
                         adaptExt->max_segments);
        }
        else if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SEG_MAX))
        {
            if ((adaptExt->info.size_max > 0) && (adaptExt->info.seg_max > 0))
            {
                /* Grab the maximum SEGMENTS value via Guest Features */
                adaptExt->max_segments = (ULONG)((ULONGLONG)adaptExt->info.seg_max * adaptExt->info.size_max) /
                                         (ROUND_TO_PAGES(adaptExt->info.size_max));
                RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                             " max_segments candidate was derived from valid Guest Features : %lu \n",
                             adaptExt->max_segments);
            }
        }
        else
        {
            /* Grab the VirtIO reported maximum SEGMENTS value from the HBA and put it somewhere mutable */
            adaptExt->max_segments = adaptExt->info.seg_max;
            RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                         " max_segments candidate was NOT specified in the registry or derived via valid Guest "
                         "Features. We will attempt to derive the value by other means...\n");
        }

        /* Use our maximum SEGMENTS value OR use a derived value... */
        if (adaptExt->indirect)
        {
            max_segs_candidate[1] = adaptExt->max_segments;
            RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                         " max_segments candidate derived from MAX SEGMENTS (reported by QEMU/KVM) as "
                         "VIRTIO_RING_F_INDIRECT_DESC is ON: %lu \n",
                         max_segs_candidate[1]);
        }
        else
        {
            Size = 0;
            virtio_query_queue_allocation(&adaptExt->vdev, VIRTIO_BLK_REQUEST_QUEUE_0, &queue_length, &Size, &HeapSize);
            max_segs_candidate[1] = max(SCSI_MINIMUM_PHYSICAL_BREAKS, (queue_length / 4));
            RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                         " max_segments candidate derived from reported Queue Length as VIRTIO_RING_F_INDIRECT_DESC is "
                         "OFF: %lu \n",
                         max_segs_candidate[1]);
        }

        /* Grab the VirtIO reported maximum SECTORS value from the HBA to start with */
        if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_GEOMETRY))
        {
            max_sectors = adaptExt->info.geometry.cylinders * adaptExt->info.geometry.sectors /
                          adaptExt->info.geometry.heads;
            if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BLK_SIZE))
            {
                max_segs_candidate[2] = (max_sectors * adaptExt->info.blk_size) / PAGE_SIZE;
            }
            else
            {
                max_segs_candidate[2] = (max_sectors * SECTOR_SIZE) / PAGE_SIZE;
            }
        }
        else
        {
            max_sectors = 0;
            max_segs_candidate[2] = 0;
        }
        RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                     " max_segments candidate derived from %lu total sectors : %lu \n",
                     max_sectors,
                     max_segs_candidate[2]);

        /* Choose the best candidate... */
        if (max_segs_candidate[1] == max_segs_candidate[2])
        {
            /* Start with a comparison of equality */
            max_segs_candidate[0] = max_segs_candidate[1];
            RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                         " max_segs_candidate[0] : init - the candidates were the same value : %lu \n",
                         max_segs_candidate[0]);
        }
        else if ((max_segs_candidate[2] > 0) && (max_segs_candidate[2] < MAX_PHYS_SEGMENTS))
        {
            /* Use the value derived from the QEMU/KVM hint if it is below the MAX_PHYS_SEGMENTS */
            max_segs_candidate[0] = max_segs_candidate[2];
            RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                         " max_segs_candidate[0] : init - the total numner of sectors (%lu) was used to select the "
                         "candidate : %lu \n",
                         max_sectors,
                         max_segs_candidate[0]);
        }
        else
        {
            /* Take the smallest candidate */
            max_segs_candidate[0] = min((max_segs_candidate[1]), (max_segs_candidate[2]));
            RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                         " max_segs_candidate[0] : init - the smallest candidate was selected : %lu \n",
                         max_segs_candidate[0]);
        }
        /* Check the value is within SG list bounds */
        max_segs_candidate[0] = min(max(SCSI_MINIMUM_PHYSICAL_BREAKS, max_segs_candidate[0]), (VIRTIO_MAX_SG - 1));
        RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                     " max_segs_candidate[0] : within SG list bounds (%lu) : %lu\n",
                     (VIRTIO_MAX_SG - 1),
                     max_segs_candidate[0]);

        /* Check the value is within physical bounds */
        max_segs_candidate[0] = min(max(SCSI_MINIMUM_PHYSICAL_BREAKS, max_segs_candidate[0]), MAX_PHYS_SEGMENTS);
        RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                     " max_segs_candidate[0] : within physical bounds (%lu) : %lu\n",
                     MAX_PHYS_SEGMENTS,
                     max_segs_candidate[0]);

        /* Update max_segments for all cases */
        adaptExt->max_segments = max_segs_candidate[0];
    }
    /* Here we enforce legacy off-by-one NumberOfPhysicalBreaks (NOPB) behaviour for StorPort.
     * This behaviour was retained in StorPort to maintain backwards compatibility.
     * This is analogous to the legacy MaximumSGList parameter in the SCSI Port driver.
     * Where:
     * MaximumSGList = ((MAX_BLOCK_SIZE)/PAGE_SIZE) + 1
     * The default x86/x64 values being:
     * MaximumSGList = (64KiB/4KiB) + 1 = 16 + 1 = 17 (0x11)
     * The MAX_BLOCK_SIZE limit is no longer 64KiB, but increased to 2048KiB (2MiB):
     * NOPB or MaximumSGList = (2048KiB/4KiB) + 1 = 512 + 1 = 513 (0x201)
     * Testing reveals the MAX_BLOCK_SIZE may now be 4096KiB (4MiB) limited by VIRTQUEUE_MAX_SIZE:
     * NOPB or MaximumSGList = (4096KiB/4KiB) + 1 = 1024 + 1 = 1025 (0x401)
     *
     * ATTENTION: The MS NOPB documentation for both the SCSI Port and StorPort drivers is incorrect.
     *
     * As max_segments = MAX_BLOCK_SIZE/PAGE_SIZE we use:
     */
    ConfigInfo->NumberOfPhysicalBreaks = adaptExt->max_segments + VIRTIO_MS_NOPB_OFFSET;

    /* Here we use the efficient single step calculation for MaximumTransferLength
     *
     * The alternative would be:
     * ConfigInfo->MaximumTransferLength = adaptExt->max_segments;
     * ConfigInfo->MaximumTransferLength <<= PAGE_SHIFT;
     * ...where #define PAGE_SHIFT 12L
     *
     */
    ConfigInfo->MaximumTransferLength = adaptExt->max_segments * PAGE_SIZE;
    adaptExt->max_tx_length = ConfigInfo->MaximumTransferLength;

    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " adaptExt->max_segments : 0x%x (%lu) | ConfigInfo->NumberOfPhysicalBreaks : 0x%x (%lu) | "
                 "ConfigInfo->MaximumTransferLength : 0x%x (%lu) Bytes (%lu KiB) \n",
                 adaptExt->max_segments,
                 adaptExt->max_segments,
                 ConfigInfo->NumberOfPhysicalBreaks,
                 ConfigInfo->NumberOfPhysicalBreaks,
                 ConfigInfo->MaximumTransferLength,
                 ConfigInfo->MaximumTransferLength,
                 (ConfigInfo->MaximumTransferLength / 1024));

    num_cpus = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    max_cpus = KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
    /* Set num_cpus and max_cpus to some sane values, to keep Static Driver Verification happy */
    num_cpus = max(1, num_cpus);
    max_cpus = max(1, max_cpus);

    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " Detected Number Of CPUs : %lu | Maximum Number Of CPUs : %lu\n",
                 num_cpus,
                 max_cpus);

    adaptExt->num_queues = 1;

    if (adaptExt->dump_mode || !adaptExt->msix_enabled)
    {
        adaptExt->num_queues = 1;
    }
    else
    {
        if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_MQ))
        {
            virtio_get_config(&adaptExt->vdev,
                              FIELD_OFFSET(blk_config, num_queues),
                              &adaptExt->num_queues,
                              sizeof(adaptExt->num_queues));
        }
        adaptExt->num_queues = min(adaptExt->num_queues, num_cpus);
    }

    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " VirtIO Request Queues : %lu | CPUs : %lu \n",
                 adaptExt->num_queues,
                 num_cpus);

    max_queues = min(max_cpus, adaptExt->num_queues);
    adaptExt->pageAllocationSize = 0;
    adaptExt->poolAllocationSize = 0;
    adaptExt->pageOffset = 0;
    adaptExt->poolOffset = 0;
    Size = 0;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 " START: pageAllocationSize : %lu KiB | Size : %lu KiB |"
                 " poolAllocationSize : %lu Bytes | HeapSize is not yet defined. \n",
                 (adaptExt->pageAllocationSize / 1024),
                 (Size / 1024),
                 adaptExt->poolAllocationSize);

    for (vq_req_idx = VIRTIO_BLK_REQUEST_QUEUE_0; vq_req_idx < max_queues; ++vq_req_idx)
    {
        virtio_query_queue_allocation(&adaptExt->vdev, vq_req_idx, &queue_length, &Size, &HeapSize);
        if (Size == 0)
        {
            LogError(DeviceExtension, SP_INTERNAL_ADAPTER_ERROR, __LINE__);

            RhelDbgPrint(TRACE_LEVEL_FATAL, " Virtual queue %d config failed.\n", vq_req_idx);
            return SP_RETURN_ERROR;
        }
        adaptExt->pageAllocationSize += ROUND_TO_PAGES(Size);
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(HeapSize);
        RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                     " INCR : pageAllocationSize : %lu KiB | Size : %lu KiB |"
                     " poolAllocationSize : %lu Bytes | HeapSize : %lu Bytes \n",
                     (adaptExt->pageAllocationSize / 1024),
                     (Size / 1024),
                     adaptExt->poolAllocationSize,
                     HeapSize);
    }
    if (!adaptExt->dump_mode)
    {
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(SRB_EXTENSION));
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(STOR_DPC) * max_queues);
        RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                     " DUMP : pageAllocationSize : %lu KiB | Size : %lu KiB |"
                     " poolAllocationSize : %lu Bytes | HeapSize : %lu Bytes \n",
                     (adaptExt->pageAllocationSize / 1024),
                     (Size / 1024),
                     adaptExt->poolAllocationSize,
                     HeapSize);
    }
    if (max_queues > MAX_QUEUES_PER_DEVICE_DEFAULT)
    {
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES((ULONGLONG)(max_queues)*virtio_get_queue_descriptor_size());
        RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                     " LIMIT: pageAllocationSize : %lu KiB | Size : %lu KiB |"
                     " poolAllocationSize : %lu Bytes | HeapSize : %lu Bytes \n",
                     (adaptExt->pageAllocationSize / 1024),
                     (Size / 1024),
                     adaptExt->poolAllocationSize,
                     HeapSize);
    }

    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " FINAL: pageAllocationSize : %lu KiB | Size : %lu KiB |"
                 " poolAllocationSize : %lu Bytes | HeapSize : %lu Bytes \n",
                 (adaptExt->pageAllocationSize / 1024),
                 (Size / 1024),
                 adaptExt->poolAllocationSize,
                 HeapSize);

    if (adaptExt->indirect)
    {
        adaptExt->queue_depth = queue_length;
    }
    else
    {
        adaptExt->queue_depth = max((queue_length / adaptExt->max_segments), 1);
    }
    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " Calculate Queue Depth : VIRTIO_RING_F_INDIRECT_DESC is %s | VIRTIO determined Queue Length "
                 "[queue_length] : %lu | Calulated Queue Depth [queue_depth] : %lu \n",
                 (adaptExt->indirect) ? "ON" : "OFF",
                 queue_length,
                 adaptExt->queue_depth);

    ConfigInfo->MaxIOsPerLun = adaptExt->queue_depth * adaptExt->num_queues;
    ConfigInfo->InitialLunQueueDepth = ConfigInfo->MaxIOsPerLun;
    ConfigInfo->MaxNumberOfIO = ConfigInfo->MaxIOsPerLun;

    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " StorPort Submission: NumberOfPhysicalBreaks = 0x%x (%lu), MaximumTransferLength = 0x%x (%lu KiB), "
                 "MaxNumberOfIO = %lu, MaxIOsPerLun = %lu, InitialLunQueueDepth = %lu \n",
                 ConfigInfo->NumberOfPhysicalBreaks,
                 ConfigInfo->NumberOfPhysicalBreaks,
                 ConfigInfo->MaximumTransferLength,
                 (ConfigInfo->MaximumTransferLength / 1024),
                 ConfigInfo->MaxNumberOfIO,
                 ConfigInfo->MaxIOsPerLun,
                 ConfigInfo->InitialLunQueueDepth);

    /* If needed, calculate the padding for the pool allocation to keep the uncached extension page aligned */
    if (adaptExt->poolAllocationSize > 0)
    {
        uncachedExtPad2 = (ROUND_TO_PAGES(adaptExt->poolAllocationSize)) - adaptExt->poolAllocationSize;
    }
    uncachedExtSize = uncachedExtPad1 + adaptExt->pageAllocationSize + adaptExt->poolAllocationSize + uncachedExtPad2;
    uncachedExtensionVa = StorPortGetUncachedExtension(DeviceExtension, ConfigInfo, uncachedExtSize);

    if (!uncachedExtensionVa)
    {
        LogError(DeviceExtension, SP_INTERNAL_ADAPTER_ERROR, __LINE__);
        RhelDbgPrint(TRACE_LEVEL_FATAL,
                     " Unable to obtain uncached extension allocation of size = %lu Bytes (%lu KiB)\n",
                     uncachedExtSize,
                     (uncachedExtSize / 1024));
        return SP_RETURN_ERROR;
    }
    else
    {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                     " MEMORY ALLOCATION : %p, size = %lu (0x%x) Bytes | StorPortGetUncachedExtension() "
                     "[uncachedExtensionVa] (size = %lu KiB) \n",
                     uncachedExtensionVa,
                     uncachedExtSize,
                     uncachedExtSize,
                     (uncachedExtSize / 1024));
    }

    /* At this point we have all the memory we're going to need. We lay it out as follows.
     * Note that we cause StorPortGetUncachedExtension to return a page-aligned memory allocation so
     * the padding1 region will typically be empty and padding2 will be sized to ensure page alignment.
     *
     * uncachedExtensionVa    pageAllocationVa         poolAllocationVa
     * +----------------------+------------------------+--------------------------+----------------------+
     * | \ \ \ \ \ \ \ \ \ \  |<= pageAllocationSize =>|<=  poolAllocationSize  =>| \ \ \ \ \ \ \ \ \ \  |
     * |  \ \  padding1 \ \ \ |                        |                          |  \ \  padding2 \ \ \ |
     * | \ \ \ \ \ \ \ \ \ \  |    page-aligned area   | pool area for cache-line | \ \ \ \ \ \ \ \ \ \  |
     * |  \ \ \ \ \ \ \ \ \ \ |                        | aligned allocations      |  \ \ \ \ \ \ \ \ \ \ |
     * +----------------------+------------------------+--------------------------+----------------------+
     * |<====================================  uncachedExtSize  ========================================>|
     */

    /* Get the Virtual Address of the page aligned area */
    adaptExt->pageAllocationVa = (PVOID)(((ULONG_PTR)(uncachedExtensionVa) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1));

    /* If needed, get the Virtual Address of the pool (cache-line aligned) area */
    if (adaptExt->poolAllocationSize > 0)
    {
        adaptExt->poolAllocationVa = (PVOID)((ULONG_PTR)adaptExt->pageAllocationVa + adaptExt->pageAllocationSize);
    }
    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " MEMORY ALLOCATION : %p, size = %lu (0x%x) Bytes |"
                 " Page-aligned area [pageAllocationVa] (size = %lu KiB) \n",
                 adaptExt->pageAllocationVa,
                 adaptExt->pageAllocationSize,
                 adaptExt->pageAllocationSize,
                 (adaptExt->pageAllocationSize / 1024));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " MEMORY ALLOCATION : %p, size = %lu (0x%x) Bytes |"
                 " Pool (cache-line aligned) area [poolAllocationVa] \n",
                 adaptExt->poolAllocationVa,
                 adaptExt->poolAllocationSize,
                 adaptExt->poolAllocationSize);

    /* Allocate a memory pool for the CPU affinity masks */
    if ((!adaptExt->dump_mode) && (adaptExt->num_queues > 1) && (adaptExt->pmsg_affinity == NULL))
    {

        adaptExt->num_affinity = adaptExt->num_queues + VIRTIO_BLK_REQUEST_QUEUE_0 + VIRTIO_BLK_MSIX_VQ_OFFSET;

        ULONG Status = StorPortAllocatePool(DeviceExtension,
                                            sizeof(GROUP_AFFINITY) * (ULONGLONG)adaptExt->num_affinity,
                                            VIOBLK_POOL_TAG,
                                            (PVOID *)&adaptExt->pmsg_affinity);

        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                     " MEMORY ALLOCATION : %p, size = %lu (0x%x) Bytes | CPU Affinity [pmsg_affinity] |"
                     " num_affinity = %lu, StorPortAllocatePool() Status = 0x%x \n",
                     adaptExt->pmsg_affinity,
                     (sizeof(GROUP_AFFINITY) * (ULONGLONG)adaptExt->num_affinity),
                     (sizeof(GROUP_AFFINITY) * (ULONGLONG)adaptExt->num_affinity),
                     adaptExt->num_affinity,
                     Status);
    }

    /* Used to check sizes to calc struct member alignments */
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, "Size of SRB_EXTENSION : %d \n", sizeof(SRB_EXTENSION));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, "Size of blk_config : %d \n", sizeof(blk_config));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, "Size of blk_outhdr : %d \n", sizeof(blk_outhdr));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, "Size of blk_discard_write_zeroes : %d \n", sizeof(blk_discard_write_zeroes));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, "Size of blk_req : %d \n", sizeof(blk_req));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, "Size of SENSE_INFO : %d \n", sizeof(SENSE_INFO));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, "Size of REQUEST_LIST : %d \n", sizeof(REQUEST_LIST));
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, "Size of ULONG_PTR : %d \n", sizeof(ULONG_PTR));

    adaptExt->fw_ver = '0';
    return SP_RETURN_FOUND;
}

BOOLEAN
VirtIoPassiveInitializeRoutine(PVOID DeviceExtension)
{
    ENTER_FN();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    for (ULONG vq_req_idx = VIRTIO_BLK_REQUEST_QUEUE_0; vq_req_idx < adaptExt->num_queues; ++vq_req_idx)
    {
        StorPortInitializeDpc(DeviceExtension, &adaptExt->dpc[vq_req_idx], CompleteDpcRoutine);
    }

    // TODO: Perform sanity check before setting adaptExt->dpc_ready = TRUE, etc.
    //       Maybe (adaptExt->dpc[vq_req_idx] != NULL / 0UL ) ...?
    adaptExt->dpc_ready = TRUE;
    virtio_device_ready(&adaptExt->vdev);

    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " DPC Ready Status : %s | VIRTIO Device Presumed Ready."
                 " VIRTIO_CONFIG_S_DRIVER_OK bit has been added to the device status.\n",
                 (adaptExt->dpc_ready) ? "READY (Presumed)" : "NOT_READY");
    EXIT_FN();
    return adaptExt->dpc_ready;
}

static BOOLEAN InitializeVirtualQueues(PADAPTER_EXTENSION adaptExt, ULONG numQueues)
{
    NTSTATUS status;

    RhelDbgPrint(TRACE_VQ,
                 " InitializeVirtualQueues - Number of VQs to look for [numQueues / num_queues] : %d\n",
                 numQueues);

    status = virtio_find_queues(&adaptExt->vdev, numQueues, adaptExt->vq);
    if (!NT_SUCCESS(status))
    {
        RhelDbgPrint(TRACE_LEVEL_FATAL, " virtio_find_queues failed with error 0x%x\n", status);
        return FALSE;
    }

    return TRUE;
}

VOID RhelSetGuestFeatures(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONGLONG guestFeatures = 0;

    if (CHECKBIT(adaptExt->features, VIRTIO_F_VERSION_1))
    {
        guestFeatures |= (1ULL << VIRTIO_F_VERSION_1);
        if (CHECKBIT(adaptExt->features, VIRTIO_F_RING_PACKED))
        {
            guestFeatures |= (1ULL << VIRTIO_F_RING_PACKED);
        }
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_F_ACCESS_PLATFORM))
    {
        guestFeatures |= (1ULL << VIRTIO_F_ACCESS_PLATFORM);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_F_ANY_LAYOUT))
    {
        guestFeatures |= (1ULL << VIRTIO_F_ANY_LAYOUT);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX))
    {
        guestFeatures |= (1ULL << VIRTIO_RING_F_EVENT_IDX);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC))
    {
        guestFeatures |= (1ULL << VIRTIO_RING_F_INDIRECT_DESC);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_FLUSH))
    {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_FLUSH);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BARRIER))
    {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_BARRIER);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO))
    {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_RO);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SIZE_MAX))
    {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_SIZE_MAX);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SEG_MAX))
    {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_SEG_MAX);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_BLK_SIZE))
    {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_BLK_SIZE);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_GEOMETRY))
    {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_GEOMETRY);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_MQ))
    {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_MQ);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_DISCARD))
    {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_DISCARD);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_WRITE_ZEROES))
    {
        guestFeatures |= (1ULL << VIRTIO_BLK_F_WRITE_ZEROES);
    }

    if (CHECKBIT(adaptExt->features, VIRTIO_F_ORDER_PLATFORM))
    {
        guestFeatures |= (1ULL << VIRTIO_F_ORDER_PLATFORM);
    }

    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_F_VERSION_1 flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_F_VERSION_1)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_F_RING_PACKED flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_F_RING_PACKED)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_F_ANY_LAYOUT flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_F_ANY_LAYOUT)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_F_ACCESS_PLATFORM flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_F_ACCESS_PLATFORM)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_RING_F_EVENT_IDX flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_RING_F_EVENT_IDX)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_RING_F_INDIRECT_DESC flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_RING_F_INDIRECT_DESC)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_BLK_F_FLUSH flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_BLK_F_FLUSH)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_BLK_F_BARRIER flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_BLK_F_BARRIER)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_BLK_F_RO flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_BLK_F_RO)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_BLK_F_SIZE_MAX flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_BLK_F_SIZE_MAX)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_BLK_F_SEG_MAX flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_BLK_F_SEG_MAX)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_BLK_F_BLK_SIZE flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_BLK_F_BLK_SIZE)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_BLK_F_GEOMETRY flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_BLK_F_GEOMETRY)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_BLK_F_MQ flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_BLK_F_MQ)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_BLK_F_DISCARD flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_BLK_F_DISCARD)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_BLK_F_WRITE_ZEROES flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_BLK_F_WRITE_ZEROES)) ? "ON" : "OFF");
    RhelDbgPrint(TRACE_GUEST_FEATURES,
                 " VIRTIO_F_ORDER_PLATFORM flag is : %s \n",
                 (guestFeatures & (1ULL << VIRTIO_F_ORDER_PLATFORM)) ? "ON" : "OFF");

    if (!NT_SUCCESS(virtio_set_features(&adaptExt->vdev, guestFeatures)))
    {
        RhelDbgPrint(TRACE_LEVEL_WARNING, " virtio_set_features() FAILED...!!!\n");
    }
    else
    {
        RhelDbgPrint(TRACE_GUEST_FEATURES, " virtio_set_features() executed successfully.\n");
    }
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " _Host Features %016llu \n", adaptExt->features);
    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " Guest Features %016llu \n", guestFeatures);
}

BOOLEAN
VirtIoHwInitialize(PVOID DeviceExtension)
{
    ENTER_FN();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONGLONG guestFeatures = 0;
    PERF_CONFIGURATION_DATA perfData = {0};
    ULONG status = STOR_STATUS_SUCCESS;
    MESSAGE_INTERRUPT_INFORMATION msi_info = {0};
    PREQUEST_LIST element = NULL;

    adaptExt->msix_vectors = 0;
    adaptExt->pageOffset = 0;
    adaptExt->poolOffset = 0;
    adaptExt->stopped = FALSE;

    if ((!adaptExt->dump_mode) && (adaptExt->num_queues > 1) && (adaptExt->perfFlags == 0))
    {
        perfData.Version = STOR_PERF_VERSION;
        perfData.Size = sizeof(PERF_CONFIGURATION_DATA);

        status = StorPortInitializePerfOpts(DeviceExtension, TRUE, &perfData);

        if (status == STOR_STATUS_SUCCESS)
        {
            RhelDbgPrint(TRACE_PERF,
                         " PERF: GET PerfOpts : Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, "
                         "FirstRedirectionMessageNumber = %d, LastRedirectionMessageNumber = %d, DeviceNode = %d\n",
                         perfData.Version,
                         perfData.Flags,
                         perfData.ConcurrentChannels,
                         perfData.FirstRedirectionMessageNumber,
                         perfData.LastRedirectionMessageNumber,
                         perfData.DeviceNode);
        }
        else
        {
            RhelDbgPrint(TRACE_LEVEL_WARNING,
                         " PERF: StorPortInitializePerfOpts GET failed with status = 0x%x\n",
                         status);
        }

        if ((status == STOR_STATUS_SUCCESS) && (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION)))
        {
            adaptExt->perfFlags = STOR_PERF_DPC_REDIRECTION;
            if (CHECKFLAG(perfData.Flags, STOR_PERF_CONCURRENT_CHANNELS))
            {
                adaptExt->perfFlags |= STOR_PERF_CONCURRENT_CHANNELS;
                perfData.ConcurrentChannels = adaptExt->num_queues;
            }
            if (CHECKFLAG(perfData.Flags, STOR_PERF_INTERRUPT_MESSAGE_RANGES))
            {
                adaptExt->perfFlags |= STOR_PERF_INTERRUPT_MESSAGE_RANGES;
                perfData.FirstRedirectionMessageNumber = VIRTIO_BLK_REQUEST_QUEUE_0 + VIRTIO_BLK_MSIX_VQ_OFFSET;
                perfData.LastRedirectionMessageNumber = perfData.FirstRedirectionMessageNumber + adaptExt->num_queues -
                                                        VIRTIO_BLK_REQUEST_QUEUE_0 - VIRTIO_BLK_MSIX_VQ_OFFSET;
                ASSERT(perfData.LastRedirectionMessageNumber < adaptExt->num_affinity);
            }

            if ((adaptExt->pmsg_affinity != NULL) && CHECKFLAG(perfData.Flags, STOR_PERF_ADV_CONFIG_LOCALITY))
            {
                RtlZeroMemory((PCHAR)adaptExt->pmsg_affinity,
                              sizeof(GROUP_AFFINITY) * ((ULONGLONG)adaptExt->num_affinity));
                adaptExt->perfFlags |= STOR_PERF_ADV_CONFIG_LOCALITY;
                perfData.MessageTargets = adaptExt->pmsg_affinity;
            }
            if (CHECKFLAG(perfData.Flags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO))
            {
                adaptExt->perfFlags |= STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO;
            }
            if (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION_CURRENT_CPU))
            {
                // adaptExt->perfFlags |= STOR_PERF_DPC_REDIRECTION_CURRENT_CPU;
            }
            if (CHECKFLAG(perfData.Flags, STOR_PERF_NO_SGL))
            {
                /* FIXME : We still use:
                 *         * StorPortGetScatterGatherList(), and
                 *         * ConfigInfo->ScatterGather = TRUE,
                 *         so not sure why we are using STOR_PERF_NO_SGL here.
                 *         Does not enable anyway...
                 */
                adaptExt->perfFlags |= STOR_PERF_NO_SGL;
            }

            perfData.Flags = adaptExt->perfFlags;

            RhelDbgPrint(TRACE_PERF,
                         " PERF: SET PerfOpts : Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, "
                         "FirstRedirectionMessageNumber = %d, LastRedirectionMessageNumber = %d, DeviceNode = %d\n",
                         perfData.Version,
                         perfData.Flags,
                         perfData.ConcurrentChannels,
                         perfData.FirstRedirectionMessageNumber,
                         perfData.LastRedirectionMessageNumber,
                         perfData.DeviceNode);
            RhelDbgPrint(TRACE_PERF,
                         " PERF: STOR_PERF_DPC_REDIRECTION flag is : %s \n",
                         (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION)) ? "ON" : "OFF");
            RhelDbgPrint(TRACE_PERF,
                         " PERF: STOR_PERF_CONCURRENT_CHANNELS flag is: %s \n",
                         (CHECKFLAG(perfData.Flags, STOR_PERF_CONCURRENT_CHANNELS)) ? "ON" : "OFF");
            RhelDbgPrint(TRACE_PERF,
                         " PERF: STOR_PERF_INTERRUPT_MESSAGE_RANGES flag is : %s \n",
                         (CHECKFLAG(perfData.Flags, STOR_PERF_INTERRUPT_MESSAGE_RANGES)) ? "ON" : "OFF");
            RhelDbgPrint(TRACE_PERF,
                         " PERF: STOR_PERF_ADV_CONFIG_LOCALITY flag is: %s \n",
                         (CHECKFLAG(perfData.Flags, STOR_PERF_ADV_CONFIG_LOCALITY)) ? "ON" : "OFF");
            RhelDbgPrint(TRACE_PERF,
                         " PERF: STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO flag is: %s \n",
                         (CHECKFLAG(perfData.Flags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO)) ? "ON" : "OFF");
            RhelDbgPrint(TRACE_PERF,
                         " PERF: STOR_PERF_DPC_REDIRECTION_CURRENT_CPU flag is : %s \n",
                         (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION_CURRENT_CPU)) ? "ON" : "OFF");
            RhelDbgPrint(TRACE_PERF,
                         " PERF: STOR_PERF_NO_SGL flag is : %s \n",
                         (CHECKFLAG(perfData.Flags, STOR_PERF_NO_SGL)) ? "ON" : "OFF");

            status = StorPortInitializePerfOpts(DeviceExtension, FALSE, &perfData);

            if (status != STOR_STATUS_SUCCESS)
            {
                adaptExt->perfFlags = 0;
                RhelDbgPrint(TRACE_LEVEL_ERROR,
                             " PERF: StorPortInitializePerfOpts SET failed with status = 0x%x\n",
                             status);
            }
            for (ULONG cpu_idx = 0; cpu_idx < adaptExt->num_affinity; ++cpu_idx)
            {
                GROUP_AFFINITY vector_affinity = adaptExt->pmsg_affinity[cpu_idx];
                RhelDbgPrint(TRACE_MSIX_CPU_AFFINITY,
                             " PERF: MSI-X Vector %lu CPU Affinity : KAFFINITY Mask = %I64d, CPU Group = %lu \n",
                             cpu_idx,
                             vector_affinity.Mask,
                             vector_affinity.Group);
            }
        }
    }

    while (StorPortGetMSIInfo(DeviceExtension, adaptExt->msix_vectors, &msi_info) == STOR_STATUS_SUCCESS)
    {
        if (adaptExt->num_queues > 1)
        {
            RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                         " MSI-X Vector [MessageId] = %lu, MessageAddress = 0x%I64x, MessageData = %lu,"
                         " InterruptVector = %lu, InterruptLevel = %lu, InterruptMode = %s,"
                         " CPU Affinity : Mask = %I64d, Group = %lu \n",
                         msi_info.MessageId,
                         msi_info.MessageAddress.QuadPart,
                         msi_info.MessageData,
                         msi_info.InterruptVector,
                         msi_info.InterruptLevel,
                         msi_info.InterruptMode == LevelSensitive ? "LevelSensitive" : "Latched",
                         adaptExt->pmsg_affinity[msi_info.MessageId].Mask,
                         adaptExt->pmsg_affinity[msi_info.MessageId].Group);
        }
        else
        {
            RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                         " MSI-X Vector [MessageId] = %lu, MessageAddress = 0x%I64x, MessageData = %lu,"
                         " InterruptVector = %lu, InterruptLevel = %lu, InterruptMode = %s \n",
                         msi_info.MessageId,
                         msi_info.MessageAddress.QuadPart,
                         msi_info.MessageData,
                         msi_info.InterruptVector,
                         msi_info.InterruptLevel,
                         msi_info.InterruptMode == LevelSensitive ? "LevelSensitive" : "Latched");
        }
        ++adaptExt->msix_vectors;
    }

    /* edge case when queues exceed MSI vectors */
    if (adaptExt->num_queues > 1 &&
        ((adaptExt->num_queues + VIRTIO_BLK_REQUEST_QUEUE_0 + VIRTIO_BLK_MSIX_VQ_OFFSET) > adaptExt->msix_vectors))
    {
        if (adaptExt->msix_vectors == 1)
        {
            adaptExt->num_queues = 1;
        }
        else
        {
            adaptExt->num_queues = (adaptExt->msix_vectors - VIRTIO_BLK_REQUEST_QUEUE_0 - VIRTIO_BLK_MSIX_VQ_OFFSET);
        }
    }

    if (!adaptExt->dump_mode && adaptExt->msix_vectors > 0)
    {
        if (adaptExt->msix_vectors >= (adaptExt->num_queues + VIRTIO_BLK_REQUEST_QUEUE_0 + VIRTIO_BLK_MSIX_VQ_OFFSET))
        {
            /* initialize queues with a MSI vector per queue */
            adaptExt->msix_one_vector = FALSE;
        }
        else
        {
            /* if we don't have enough vectors, use one for all queues */
            adaptExt->msix_one_vector = TRUE;
        }
    }
    else
    {
        /* initialize queues with no MSI interrupts */
        adaptExt->msix_enabled = FALSE;
    }

    if (!InitializeVirtualQueues(adaptExt, (adaptExt->num_queues + VIRTIO_BLK_REQUEST_QUEUE_0)))
    {
        LogError(DeviceExtension, SP_INTERNAL_ADAPTER_ERROR, __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, " !!! - Failed to initialize the Virtual Queues - !!!\n");
        virtio_add_status(&adaptExt->vdev, VIRTIO_CONFIG_S_FAILED);
        EXIT_FN();
        return FALSE;
    }

    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " VirtIO Request Queues : %d, MSI-X Enabled : %s, MSI-X Use ONE Vector : %s,"
                 " MSI-X Vectors [msix_vectors] : %d \n",
                 adaptExt->num_queues,
                 (adaptExt->msix_enabled) ? "YES" : "NO",
                 (adaptExt->msix_one_vector) ? "YES" : "NO",
                 adaptExt->msix_vectors);

    if (adaptExt->msix_enabled && !adaptExt->msix_one_vector)
    {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                     " MSI-X Vector : %lu | StorPort exclusive control \n",
                     VIRTIO_BLK_MSIX_CONFIG_VECTOR);
    }

    for (ULONG vq_req_idx = VIRTIO_BLK_REQUEST_QUEUE_0; vq_req_idx < adaptExt->num_queues; ++vq_req_idx)
    {
        element = &adaptExt->processing_srbs[vq_req_idx];
        InitializeListHead(&element->srb_list);
        element->srb_cnt = 0;
        if (adaptExt->msix_enabled)
        {
            RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                         " MSI-X Vector : %lu | VIRTIO Request Queue : %lu (QueueNumber / vq_req_idx : %lu) \n",
                         (vq_req_idx + VIRTIO_BLK_REQUEST_QUEUE_0 +
                          (adaptExt->msix_one_vector ? VIRTIO_BLK_MSIX_VQ_1_VCTR_OFFSET : VIRTIO_BLK_MSIX_VQ_OFFSET)),
                         (vq_req_idx + 1),
                         vq_req_idx);
        }
        else
        {
            RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                         " VIRTIO Request Queue : %lu (QueueNumber / vq_req_idx : %lu) \n",
                         (vq_req_idx + 1),
                         vq_req_idx);
        }
    }

    if (!adaptExt->dump_mode)
    {
        /* We don't get another chance to call StorPortEnablePassiveInitialization and initialize
         * DPCs if the adapter is being restarted, so leave our datastructures alone on restart
         */
        if (adaptExt->dpc == NULL)
        {
            adaptExt->dpc = (PSTOR_DPC)VioStorPoolAlloc(DeviceExtension, sizeof(STOR_DPC) * adaptExt->num_queues);
        }

        if (adaptExt->dpc_ready)
        {
            RhelDbgPrint(TRACE_LEVEL_WARNING, "DPC already initialized.\n");
        }
        else
        {
            if (!StorPortEnablePassiveInitialization(DeviceExtension, VirtIoPassiveInitializeRoutine))
            {
                virtio_add_status(&adaptExt->vdev, VIRTIO_CONFIG_S_FAILED);
                RhelDbgPrint(TRACE_LEVEL_FATAL, " StorPortEnablePassiveInitialization FAILED\n");
                EXIT_FN();
                return FALSE;
            }
        }
    }

    /* Zero out and set inquiry data */
    RtlZeroMemory(&adaptExt->inquiry_data, sizeof(INQUIRYDATA));

    adaptExt->inquiry_data.ANSIVersion = 4;
    adaptExt->inquiry_data.ResponseDataFormat = 2;
    adaptExt->inquiry_data.CommandQueue = 1;
    adaptExt->inquiry_data.DeviceType = DIRECT_ACCESS_DEVICE;
    adaptExt->inquiry_data.Wide32Bit = 1;
    adaptExt->inquiry_data.AdditionalLength = 91;
    StorPortMoveMemory(&adaptExt->inquiry_data.VendorId, VIRTIO_BLK_VENDOR_ID, sizeof(UCHAR[8]));
    StorPortMoveMemory(&adaptExt->inquiry_data.ProductId, VIRTIO_BLK_PRODUCT_ID, sizeof(UCHAR[16]));
    StorPortMoveMemory(&adaptExt->inquiry_data.ProductRevisionLevel, VIRTIO_BLK_PROD_REV_LVL, sizeof(UCHAR[4]));
    StorPortMoveMemory(&adaptExt->inquiry_data.VendorSpecific, VIRTIO_BLK_VEND_SPECIFIC, sizeof(UCHAR[20]));

    /* Get any NULL-terminated strings we might need for DBG / ETW, adding extra byte for terminator */
    UCHAR vend_id[9];
    UCHAR prod_id[17];
    UCHAR prod_rev_lvl[5];
    UCHAR vend_specific[21];
    GetTerminatedString(vend_id, adaptExt->inquiry_data.VendorId, 8);
    GetTerminatedString(prod_id, adaptExt->inquiry_data.ProductId, 16);
    GetTerminatedString(prod_rev_lvl, adaptExt->inquiry_data.ProductRevisionLevel, 4);
    GetTerminatedString(vend_specific, adaptExt->inquiry_data.VendorSpecific, 20);

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 " INQUIRYDATA : SCSI Primary Commands - 2 (SPC-2) Compliant : %s | ANSIVersion : 0x%x\n",
                 (adaptExt->inquiry_data.ANSIVersion >= 4) ? "YES" : "NO",
                 adaptExt->inquiry_data.ANSIVersion);

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 " INQUIRYDATA : Disk Device Type : %s\n",
                 (adaptExt->inquiry_data.DeviceType == DIRECT_ACCESS_DEVICE) ? "YES" : "NO");

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 " INQUIRYDATA : 32-bit Wide Transfers Supported : %s\n",
                 (adaptExt->inquiry_data.Wide32Bit) ? "YES" : "NO");

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 " INQUIRYDATA : Command Queuing Supported : %s\n",
                 (adaptExt->inquiry_data.CommandQueue) ? "YES" : "NO");

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 " INQUIRYDATA : CDB Parameter Length : %d Bytes\n",
                 adaptExt->inquiry_data.AdditionalLength);

    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 " INQUIRYDATA : VendorId : \"%s\""
                 " without quotes - used to show ASCII padding ('\\20') |"
                 " size : %d Bytes / Characters\n",
                 vend_id,
                 sizeof(adaptExt->inquiry_data.VendorId));
    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 " INQUIRYDATA : ProductId : \"%s\""
                 " without quotes - used to show ASCII padding ('\\20') |"
                 " size : %d Bytes / Characters\n",
                 prod_id,
                 sizeof(adaptExt->inquiry_data.ProductId));
    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 " INQUIRYDATA : ProductRevisionLevel : \"%s\""
                 " without quotes - used to show ASCII padding ('\\20') |"
                 " size : %d Bytes / Characters\n",
                 prod_rev_lvl,
                 sizeof(adaptExt->inquiry_data.ProductRevisionLevel));
    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                 " INQUIRYDATA : VendorSpecific : \"%s\""
                 " without quotes - used to show ASCII padding ('\\20') |"
                 " size : %d Bytes / Characters\n",
                 vend_specific,
                 sizeof(adaptExt->inquiry_data.VendorSpecific));

    EXIT_FN();
    return TRUE;
}

VOID CompletePendingRequests(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
#ifdef DBG
    RhelDbgPrint(TRACE_LEVEL_WARNING,
                 " CompletePendingRequests - srb_cnt : %d | inqueue_cnt : %d\n",
                 adaptExt->srb_cnt,
                 adaptExt->inqueue_cnt);
#endif
    if (!adaptExt->reset_in_progress)
    {
        adaptExt->reset_in_progress = TRUE;
        PREQUEST_LIST element = NULL;
        STOR_LOCK_HANDLE LockHandle = {0};

        StorPortPause(DeviceExtension, 10);

        for (ULONG vq_req_idx = VIRTIO_BLK_REQUEST_QUEUE_0; vq_req_idx < adaptExt->num_queues; vq_req_idx++)
        {
            if (adaptExt->msix_enabled && adaptExt->msix_one_vector)
            {
                VioStorVQLock(DeviceExtension, QUEUE_TO_MESSAGE_1_VECTOR(vq_req_idx), &LockHandle);
            }
            else
            {
                VioStorVQLock(DeviceExtension, QUEUE_TO_MESSAGE(vq_req_idx), &LockHandle);
            }

            element = &adaptExt->processing_srbs[vq_req_idx];

            while (!IsListEmpty(&element->srb_list))
            {
                PLIST_ENTRY entry = RemoveHeadList(&element->srb_list);
                if (entry)
                {
                    pblk_req req = CONTAINING_RECORD(entry, blk_req, list_entry);
                    PSCSI_REQUEST_BLOCK currSrb = (PSCSI_REQUEST_BLOCK)req->req;
                    if (currSrb)
                    {
                        SRB_SET_DATA_TRANSFER_LENGTH(currSrb, 0);
                        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)currSrb, SRB_STATUS_BUS_RESET);
                        element->srb_cnt--;
                    }
                }
            }
            if (element->srb_cnt)
            {
                element->srb_cnt = 0;
            }
            if (adaptExt->msix_enabled && adaptExt->msix_one_vector)
            {
                VioStorVQUnlock(DeviceExtension, QUEUE_TO_MESSAGE_1_VECTOR(vq_req_idx), &LockHandle);
            }
            else
            {
                VioStorVQUnlock(DeviceExtension, QUEUE_TO_MESSAGE(vq_req_idx), &LockHandle);
            }
        }
        StorPortResume(DeviceExtension);
    }
    else
    {
        RhelDbgPrint(TRACE_LEVEL_ERROR, "RESET ALREADY IN PROGRESS !!!!\n");
    }
    adaptExt->reset_in_progress = FALSE;
}

BOOLEAN
VirtIoStartIo(PVOID DeviceExtension, PSCSI_REQUEST_BLOCK Srb)
{
    ENTER_FN_SRB();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PCDB cdb = SRB_CDB(Srb);
    UCHAR ScsiStatus = SCSISTAT_GOOD;

    SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);

    // RhelDbgPrint(TRACE_LEVEL_VERBOSE, " Srb = 0x%p\n", Srb);

    switch (SRB_FUNCTION(Srb))
    {
        case SRB_FUNCTION_EXECUTE_SCSI:
            {
                break;
            }
        case SRB_FUNCTION_IO_CONTROL:
            {
                PVOID srbDataBuffer = SRB_DATA_BUFFER(Srb);
                PSRB_IO_CONTROL srbControl = (PSRB_IO_CONTROL)srbDataBuffer;
                UCHAR srbStatus = SRB_STATUS_INVALID_REQUEST;
                switch (srbControl->ControlCode)
                {
                    case IOCTL_SCSI_MINIPORT_FIRMWARE:
                        srbStatus = FirmwareRequest(DeviceExtension, (PSRB_TYPE)Srb);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " <--> IOCTL_SCSI_MINIPORT_FIRMWARE\n");
                        break;
                    default:
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                     " <--> Unsupport control code 0x%x\n",
                                     srbControl->ControlCode);
                        break;
                }

                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, srbStatus);
                EXIT_FN_SRB();
                return TRUE;
            }
        case SRB_FUNCTION_PNP:
            {
                UCHAR SrbStatus = SRB_STATUS_SUCCESS;
                ULONG SrbPnPFlags;
                ULONG PnPAction;
                SRB_GET_PNP_INFO(Srb, SrbPnPFlags, PnPAction);
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, " SrbPnPFlags = %d, PnPAction = %d\n", SrbPnPFlags, PnPAction);
                switch (PnPAction)
                {
                    case StorQueryCapabilities:
                        if (CHECKFLAG(SrbPnPFlags, SRB_PNP_FLAGS_ADAPTER_REQUEST) &&
                            (SRB_DATA_TRANSFER_LENGTH(Srb) >= sizeof(STOR_DEVICE_CAPABILITIES)))
                        {
                            PSTOR_DEVICE_CAPABILITIES storCapabilities = (PSTOR_DEVICE_CAPABILITIES)SRB_DATA_BUFFER(Srb);
                            RtlZeroMemory(storCapabilities, sizeof(*storCapabilities));
                            storCapabilities->Removable = 1;
                        }
                        break;
                    case StorRemoveDevice:
                    case StorSurpriseRemoval:
                        adaptExt->removed = TRUE;
                        DeviceChangeNotification(DeviceExtension,
                                                 FALSE,
                                                 idx_VirtIoStartIo,
                                                 idx_DeviceChangeNotification);
                        break;
                    case StorStopDevice:
                        adaptExt->stopped = TRUE;
                        break;
                    default:
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                     " Unsupported PnPAction SrbPnPFlags = %d, PnPAction = %d\n",
                                     SrbPnPFlags,
                                     PnPAction);
                        SrbStatus = SRB_STATUS_INVALID_REQUEST;
                }
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
                EXIT_FN_SRB();
                return TRUE;
            }
        case SRB_FUNCTION_POWER:
            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
            EXIT_FN_SRB();
            return TRUE;
        case SRB_FUNCTION_RESET_BUS:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_LOGICAL_UNIT:
            {
                CompletePendingRequests(DeviceExtension);
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
#ifdef DBG
                RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                             " RESET (%p) Function %x Cnt %d InQueue %d\n",
                             Srb,
                             SRB_FUNCTION(Srb),
                             adaptExt->srb_cnt,
                             adaptExt->inqueue_cnt);
                for (ULONG vq_req_idx = VIRTIO_BLK_REQUEST_QUEUE_0; vq_req_idx < adaptExt->num_queues; vq_req_idx++)
                {
                    if (adaptExt->vq[vq_req_idx])
                    {
                        RhelDbgPrint(TRACE_LEVEL_ERROR, "%d indx %d\n", vq_req_idx, adaptExt->vq[vq_req_idx]->index);
                    }
                }
#endif
                EXIT_FN_SRB();
                return TRUE;
            }
        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_SHUTDOWN:
            {
                SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
                if (!SendSRB(DeviceExtension, (PSRB_TYPE)Srb, SEND_SRB_FLUSH, SEND_SRB_NO_EXISTING_SPINLOCK))
                {
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_ERROR);
                }
                EXIT_FN_SRB();
                return TRUE;
            }

        default:
            {
                SRB_SET_DATA_TRANSFER_LENGTH(Srb, 0);
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_INVALID_REQUEST);
                EXIT_FN_SRB();
                return TRUE;
            }
    }

    if (!cdb)
    {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " no CDB (%p) Function %x\n", Srb, SRB_FUNCTION(Srb));
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_BAD_FUNCTION);
        EXIT_FN_SRB();
        return TRUE;
    }

    switch (cdb->CDB6GENERIC.OperationCode)
    {
        case SCSIOP_MODE_SENSE:
            {
                UCHAR SrbStatus = RhelScsiGetModeSense(DeviceExtension, (PSRB_TYPE)Srb);
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
                EXIT_FN_SRB();
                return TRUE;
            }
        case SCSIOP_INQUIRY:
            {
                UCHAR SrbStatus = RhelScsiGetInquiryData(DeviceExtension, (PSRB_TYPE)Srb);
                if (SRB_STATUS_PENDING != SrbStatus)
                {
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
                }
                EXIT_FN_SRB();
                return TRUE;
            }

        case SCSIOP_READ_CAPACITY16:
        case SCSIOP_READ_CAPACITY:
            {
                UCHAR SrbStatus = RhelScsiGetCapacity(DeviceExtension, (PSRB_TYPE)Srb);
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
                EXIT_FN_SRB();
                return TRUE;
            }
        case SCSIOP_WRITE:
        case SCSIOP_WRITE16:
            {
                if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO))
                {
                    UCHAR SrbStatus = SRB_STATUS_ERROR;
                    adaptExt->sense_info.senseKey = SCSI_SENSE_DATA_PROTECT;
                    adaptExt->sense_info.additionalSenseCode = SCSI_ADSENSE_WRITE_PROTECT;
                    adaptExt->sense_info.additionalSenseCodeQualifier = SCSI_SENSEQ_SPACE_ALLOC_FAILED_WRITE_PROTECT; // SCSI_ADSENSE_NO_SENSE;
                    if (SetSenseInfo(DeviceExtension, (PSRB_TYPE)Srb, idx_VirtIoStartIo, idx_SetSenseInfo))
                    {
                        SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                    }
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
                    EXIT_FN_SRB();
                    return TRUE;
                }
                RhelDbgPrint(TRACE_WRITE_PATH, " WRITE PATH - SRB : %p \n", Srb);
                // Will now continue to SCSIOP_READ16...
            }
        case SCSIOP_READ:
        case SCSIOP_READ16:
            {
                SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);

                if (!SendSRB(DeviceExtension, (PSRB_TYPE)Srb, SEND_SRB_READ_WRITE, SEND_SRB_NO_EXISTING_SPINLOCK))
                {
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_BUSY);
                }
                EXIT_FN_SRB();
                return TRUE;
            }
        case SCSIOP_START_STOP_UNIT:
            {
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
                EXIT_FN_SRB();
                return TRUE;
            }
        case SCSIOP_REQUEST_SENSE:
        case SCSIOP_TEST_UNIT_READY:
        case SCSIOP_RESERVE_UNIT:
        case SCSIOP_RESERVE_UNIT10:
        case SCSIOP_RELEASE_UNIT:
        case SCSIOP_RELEASE_UNIT10:
        case SCSIOP_MEDIUM_REMOVAL:
            {
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
                EXIT_FN_SRB();
                return TRUE;
            }
        case SCSIOP_VERIFY:
        case SCSIOP_VERIFY16:
            {
                UCHAR SrbStatus = RhelScsiVerify(DeviceExtension, (PSRB_TYPE)Srb);
                if (SrbStatus == SRB_STATUS_INVALID_REQUEST)
                {
                    SrbStatus = SRB_STATUS_ERROR;
                    adaptExt->sense_info.senseKey = SCSI_SENSE_ILLEGAL_REQUEST;
                    adaptExt->sense_info.additionalSenseCode = SCSI_ADSENSE_INVALID_CDB;
                    adaptExt->sense_info.additionalSenseCodeQualifier = 0;
                    if (SetSenseInfo(DeviceExtension, (PSRB_TYPE)Srb, idx_VirtIoStartIo, idx_SetSenseInfo))
                    {
                        SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                    }
                }
                CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
                EXIT_FN_SRB();
                return TRUE;
            }
        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16:
            {
                if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO))
                {
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_SUCCESS);
                    EXIT_FN_SRB();
                    return TRUE;
                }
                SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
                if (!SendSRB(DeviceExtension, (PSRB_TYPE)Srb, SEND_SRB_FLUSH, SEND_SRB_NO_EXISTING_SPINLOCK))
                {
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_ERROR);
                }
                EXIT_FN_SRB();
                return TRUE;
            }
        case SCSIOP_UNMAP:
            {
                SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
                if (!SendSRB(DeviceExtension, (PSRB_TYPE)Srb, SEND_SRB_UNMAP, SEND_SRB_NO_EXISTING_SPINLOCK))
                {
                    RhelDbgPrint(TRACE_LEVEL_ERROR, " UnMap operation FAILED.\n");
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_ERROR);
                }
                EXIT_FN_SRB();
                return TRUE;
            }
    }

    if (cdb->CDB12.OperationCode == SCSIOP_REPORT_LUNS)
    {
        UCHAR SrbStatus = RhelScsiReportLuns(DeviceExtension, (PSRB_TYPE)Srb);
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SrbStatus);
        EXIT_FN_SRB();
        return TRUE;
    }

    RhelDbgPrint(TRACE_LEVEL_ERROR,
                 " SRB_STATUS_INVALID_REQUEST (%p) Function %x, OperationCode %x\n",
                 Srb,
                 SRB_FUNCTION(Srb),
                 cdb->CDB6GENERIC.OperationCode);
    SRB_SET_DATA_TRANSFER_LENGTH(Srb, 0);
    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_INVALID_REQUEST);
    EXIT_FN_SRB();
    return TRUE;
}

BOOLEAN
VirtIoInterrupt(PVOID DeviceExtension)
{
    ENTER_FN();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    BOOLEAN isInterruptServiced = FALSE;
    ULONG intReason = 0;

    if (adaptExt->removed == TRUE || adaptExt->stopped == TRUE)
    {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " Interrupt on removed or stopped device...! IRQL : %d \n", KeGetCurrentIrql());
        EXIT_FN();
        return FALSE;
    }

    intReason = virtio_read_isr_status(&adaptExt->vdev);
    RhelDbgPrint(TRACE_INTERRUPT, " ISR status : %d | IRQL : %d \n", intReason, KeGetCurrentIrql());

    if (intReason == 1 || adaptExt->dump_mode)
    {
        isInterruptServiced = TRUE;

        if (!adaptExt->dump_mode && adaptExt->dpc_ready)
        {
            if (adaptExt->msix_enabled && adaptExt->msix_one_vector)
            {
                StorPortIssueDpc(DeviceExtension,
                                 &adaptExt->dpc[VIRTIO_BLK_REQUEST_QUEUE_0],
                                 ULongToPtr(QUEUE_TO_MESSAGE_1_VECTOR(VIRTIO_BLK_REQUEST_QUEUE_0)),
                                 ULongToPtr(QUEUE_TO_MESSAGE_1_VECTOR(VIRTIO_BLK_REQUEST_QUEUE_0)));
            }
            else
            {
                StorPortIssueDpc(DeviceExtension,
                                 &adaptExt->dpc[VIRTIO_BLK_REQUEST_QUEUE_0],
                                 ULongToPtr(QUEUE_TO_MESSAGE(VIRTIO_BLK_REQUEST_QUEUE_0)),
                                 ULongToPtr(QUEUE_TO_MESSAGE(VIRTIO_BLK_REQUEST_QUEUE_0)));
            }
        }
        else
        {
            ProcessBuffer(DeviceExtension, QUEUE_TO_MESSAGE(VIRTIO_BLK_REQUEST_QUEUE_0), PROCESS_BUFFER_NO_SPINLOCKS);
        }
    }
    else if (intReason == 3)
    {
        RhelGetDiskGeometry(DeviceExtension);
        isInterruptServiced = TRUE;
        adaptExt->sense_info.senseKey = SCSI_SENSE_UNIT_ATTENTION;
        adaptExt->sense_info.additionalSenseCode = SCSI_ADSENSE_PARAMETERS_CHANGED;
        adaptExt->sense_info.additionalSenseCodeQualifier = SCSI_SENSEQ_CAPACITY_DATA_CHANGED;
        adaptExt->check_condition = TRUE;
        DeviceChangeNotification(DeviceExtension, TRUE, idx_VirtIoInterrupt, idx_DeviceChangeNotification);
    }

    RhelDbgPrint(TRACE_INTERRUPT,
                 " Interrupt has been serviced : %s | ISR Status : %d \n",
                 (isInterruptServiced) ? "YES" : "NO",
                 intReason);
    EXIT_FN();
    return isInterruptServiced;
}

static BOOLEAN VirtIoMSInterruptWorker(IN PVOID DeviceExtension, IN ULONG MessageId)
{
    ENTER_FN();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (MessageId >= QUEUE_TO_MESSAGE(VIRTIO_BLK_REQUEST_QUEUE_0) ||
        (adaptExt->msix_enabled && adaptExt->msix_one_vector))
    {
        RhelDbgPrint(TRACE_DPC, " Dispatching to Request Queue...\n");

        DispatchQueue(DeviceExtension, MessageId);

        EXIT_FN();
        return TRUE;
    }

    /* NOTE: Unable to hit this when adaptExt->msix_one_vector == TRUE if we want to send DPCs too */
    if (MessageId == VIRTIO_BLK_MSIX_CONFIG_VECTOR)
    {
        RhelDbgPrint(TRACE_INTERRUPT, " Processing Device Change Interrupt...\n");

        RhelGetDiskGeometry(DeviceExtension);
        adaptExt->sense_info.senseKey = SCSI_SENSE_UNIT_ATTENTION;
        adaptExt->sense_info.additionalSenseCode = SCSI_ADSENSE_PARAMETERS_CHANGED;
        adaptExt->sense_info.additionalSenseCodeQualifier = SCSI_SENSEQ_CAPACITY_DATA_CHANGED;
        adaptExt->check_condition = TRUE;
        DeviceChangeNotification(DeviceExtension, TRUE, idx_VirtIoMSInterruptWorker, idx_DeviceChangeNotification);
        EXIT_FN();
        return TRUE;
    }

    return FALSE;
}

BOOLEAN
VirtIoMSInterruptRoutine(PVOID DeviceExtension, ULONG MessageId)
{
    ENTER_FN();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    BOOLEAN isInterruptServiced = FALSE;

    RhelDbgPrint(TRACE_INTERRUPT, " MSI MessageId 0x%x\n", MessageId);

    if (MessageId > adaptExt->num_queues || adaptExt->removed == TRUE || adaptExt->stopped == TRUE)
    {
        RhelDbgPrint(TRACE_LEVEL_ERROR,
                     " Adapter has been removed. Returning without servicing interrupt. MessageId : %d\n",
                     MessageId);
        EXIT_FN();
        return FALSE;
    }

    // if (adaptExt->msix_enabled && !adaptExt->msix_one_vector)
    if (adaptExt->msix_enabled)
    {
        /* Each queue has its own vector, this is the fast and common case */
        isInterruptServiced = VirtIoMSInterruptWorker(DeviceExtension, MessageId);
    }
    else
    {
        /* Fall back to checking all queues - not sure if this ever gets hit... */
        for (ULONG vq_req_idx = VIRTIO_BLK_REQUEST_QUEUE_0;
             vq_req_idx < adaptExt->num_queues + VIRTIO_BLK_REQUEST_QUEUE_0;
             vq_req_idx++)
        {
            RhelDbgPrint(TRACE_INTERRUPT,
                         " MessageId : %d | Checking VQ  %lu for buffer content... \n",
                         MessageId,
                         vq_req_idx);
            if (virtqueue_has_buf(adaptExt->vq[vq_req_idx]))
            {
                RhelDbgPrint(TRACE_INTERRUPT,
                             " MessageId : %d | VQ  %lu has buffer content."
                             " Calulated MessageId to be serviced : %d \n",
                             MessageId,
                             vq_req_idx,
                             (adaptExt->msix_one_vector ? QUEUE_TO_MESSAGE_1_VECTOR(vq_req_idx)
                                                        : QUEUE_TO_MESSAGE(vq_req_idx)));
                isInterruptServiced |= VirtIoMSInterruptWorker(DeviceExtension,
                                                               (adaptExt->msix_one_vector ? QUEUE_TO_MESSAGE_1_VECTOR(vq_req_idx)
                                                                                          : QUEUE_TO_MESSAGE(vq_req_idx)));
            }
        }
    }

    RhelDbgPrint(TRACE_INTERRUPT,
                 " Interrupt has been serviced : %s | MessageId : %d \n",
                 (isInterruptServiced) ? "YES" : "NO",
                 MessageId);
    EXIT_FN();
    return isInterruptServiced;
}

BOOLEAN
VirtIoResetBus(PVOID DeviceExtension, ULONG PathId)
{
    UNREFERENCED_PARAMETER(PathId);

    CompletePendingRequests(DeviceExtension);
    return TRUE;
}

SCSI_ADAPTER_CONTROL_STATUS
VirtIoAdapterControl(PVOID DeviceExtension, SCSI_ADAPTER_CONTROL_TYPE ControlType, PVOID Parameters)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST ControlTypeList;
    ULONG AdjustedMaxControlType;
    ULONG Index;
    SCSI_ADAPTER_CONTROL_STATUS status = ScsiAdapterControlUnsuccessful;
    BOOLEAN SupportedControlTypes[5] = {TRUE, TRUE, TRUE, FALSE, FALSE};

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " ControlType %d\n", ControlType);

    switch (ControlType)
    {

        case ScsiQuerySupportedControlTypes:
            {
                RhelDbgPrint(TRACE_LEVEL_VERBOSE, " ScsiQuerySupportedControlTypes\n");
                ControlTypeList = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
                AdjustedMaxControlType = (ControlTypeList->MaxControlType < 5) ? ControlTypeList->MaxControlType : 5;
                for (Index = 0; Index < AdjustedMaxControlType; Index++)
                {
                    ControlTypeList->SupportedTypeList[Index] = SupportedControlTypes[Index];
                }
                status = ScsiAdapterControlSuccess;
                break;
            }
        case ScsiStopAdapter:
            {
                RhelDbgPrint(TRACE_LEVEL_VERBOSE, " ScsiStopAdapter\n");
                if (adaptExt->removed == TRUE || adaptExt->stopped == TRUE)
                {
                    RhelShutDown(DeviceExtension);
                }
                if (adaptExt->stopped)
                {
                    if (adaptExt->pmsg_affinity != NULL)
                    {
                        StorPortFreePool(DeviceExtension, (PVOID)adaptExt->pmsg_affinity);
                        adaptExt->pmsg_affinity = NULL;
                    }
                    adaptExt->perfFlags = 0;
                }
                status = ScsiAdapterControlSuccess;
                break;
            }
        case ScsiRestartAdapter:
            {
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

SCSI_UNIT_CONTROL_STATUS
VirtIoUnitControl(PVOID DeviceExtension, SCSI_UNIT_CONTROL_TYPE ControlType, PVOID Parameters)
{
    ENTER_FN();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST ControlTypeList;
    ULONG AdjustedMaxControlType;
    ULONG list_idx;
    ULONG vq_req_idx;
    SCSI_UNIT_CONTROL_STATUS Status = ScsiUnitControlUnsuccessful;
    BOOLEAN SupportedControlTypes[ScsiUnitControlMax] = {FALSE};

    SupportedControlTypes[ScsiQuerySupportedControlTypes] = TRUE;
    SupportedControlTypes[ScsiUnitStart] = TRUE;
    SupportedControlTypes[ScsiUnitRemove] = TRUE;
    SupportedControlTypes[ScsiUnitSurpriseRemoval] = TRUE;

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Unit Control Type %d\n", ControlType);
    switch (ControlType)
    {
        case ScsiQuerySupportedUnitControlTypes:
            ControlTypeList = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
            AdjustedMaxControlType = (ControlTypeList->MaxControlType < ScsiUnitControlMax) ? ControlTypeList->MaxControlType
                                                                                            : ScsiUnitControlMax;
            for (list_idx = 0; list_idx < AdjustedMaxControlType; list_idx++)
            {
                ControlTypeList->SupportedTypeList[list_idx] = SupportedControlTypes[list_idx];
            }
            Status = ScsiUnitControlSuccess;
            break;
        case ScsiUnitStart:
            Status = ScsiUnitControlSuccess;
            break;
        case ScsiUnitRemove:
        case ScsiUnitSurpriseRemoval:
            ULONG QueueNumber;
            ULONG MessageId;
            PREQUEST_LIST element;
            STOR_LOCK_HANDLE LockHandle = {0};
            PSTOR_ADDR_BTL8 stor_addr = (PSTOR_ADDR_BTL8)Parameters;
            for (vq_req_idx = VIRTIO_BLK_REQUEST_QUEUE_0; vq_req_idx < adaptExt->num_queues; vq_req_idx++)
            {
                element = &adaptExt->processing_srbs[vq_req_idx];
                QueueNumber = vq_req_idx + VIRTIO_BLK_REQUEST_QUEUE_0;
                if (adaptExt->msix_enabled && adaptExt->msix_one_vector)
                {
                    MessageId = QUEUE_TO_MESSAGE_1_VECTOR(QueueNumber);
                }
                else
                {
                    MessageId = QUEUE_TO_MESSAGE(QueueNumber);
                }
                VioStorVQLock(DeviceExtension, MessageId, &LockHandle);
                if (!IsListEmpty(&element->srb_list))
                {
                    PLIST_ENTRY entry = element->srb_list.Flink;
                    while (entry != &element->srb_list)
                    {
                        pblk_req req = CONTAINING_RECORD(entry, blk_req, list_entry);
                        PSRB_TYPE currSrb = req->req;
                        PLIST_ENTRY next = entry->Flink;
                        if (SRB_PATH_ID(currSrb) == stor_addr->Path && SRB_TARGET_ID(currSrb) == stor_addr->Target &&
                            SRB_LUN(currSrb) == stor_addr->Lun)
                        {
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                         " Complete pending I/Os on Path %d Target %d Lun %d \n",
                                         SRB_PATH_ID(currSrb),
                                         SRB_TARGET_ID(currSrb),
                                         SRB_LUN(currSrb));
                            SRB_SET_DATA_TRANSFER_LENGTH(currSrb, 0);
                            CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)currSrb, SRB_STATUS_NO_DEVICE);
                            RemoveEntryList(entry);
                            element->srb_cnt--;
                        }
                        entry = next;
                    }
                }
                VioStorVQUnlock(DeviceExtension, MessageId, &LockHandle);
            }
            Status = ScsiUnitControlSuccess;
            break;
        default:
            RhelDbgPrint(TRACE_LEVEL_ERROR, " Unsupported Unit ControlType %d\n", ControlType);
            break;
    }

    EXIT_FN();
    return Status;
}

BOOLEAN
VirtIoHwReinitialize(PVOID DeviceExtension)
{
    /* The adapter is being restarted and we need to bring it back up without
     * running any passive-level code. Note that VirtIoFindAdapter is *not*
     * called on restart.
     */
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONGLONG old_features = 0;
    old_features = adaptExt->features;
    if (InitVirtIODevice(DeviceExtension) != SP_RETURN_FOUND)
    {
        return FALSE;
    }
    RhelGetDiskGeometry(DeviceExtension);
    RhelSetGuestFeatures(DeviceExtension);

    if (!VirtIoHwInitialize(DeviceExtension))
    {
        return FALSE;
    }

    if (CHECKBIT((old_features ^ adaptExt->features), VIRTIO_BLK_F_RO))
    {
        adaptExt->sense_info.senseKey = SCSI_SENSE_DATA_PROTECT;
        adaptExt->sense_info.additionalSenseCode = SCSI_ADSENSE_WRITE_PROTECT;
        adaptExt->sense_info.additionalSenseCodeQualifier = SCSI_SENSEQ_SPACE_ALLOC_FAILED_WRITE_PROTECT; // SCSI_ADSENSE_NO_SENSE;
        adaptExt->check_condition = TRUE;
        DeviceChangeNotification(DeviceExtension, TRUE, idx_VirtIoHwReinitialize, idx_DeviceChangeNotification);
    }
    return TRUE;
}

BOOLEAN
VirtIoBuildIo(PVOID DeviceExtension, PSCSI_REQUEST_BLOCK Srb)
{
    ENTER_FN_SRB();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PCDB cdb;
    ULONG sgl_idx;
    ULONG dummy;
    ULONG sgElement;
    ULONG sgMaxElements;
    ULONG sgLength;
    ULONG sgOffset;
    PSRB_EXTENSION srbExt;
    PSTOR_SCATTER_GATHER_LIST sgList;
    ULONGLONG lba;
    ULONG blocks;

    cdb = SRB_CDB(Srb);
    srbExt = SRB_EXTENSION(Srb);

#ifdef DBG
    InterlockedIncrement((LONG volatile *)&adaptExt->srb_cnt);
#endif
    if (SRB_PATH_ID(Srb) || SRB_TARGET_ID(Srb) || SRB_LUN(Srb) || ((adaptExt->removed == TRUE)))
    {
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, 0);
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_NO_DEVICE);
        EXIT_FN_SRB();
        return FALSE;
    }
    if (adaptExt->stopped == TRUE)
    {
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_ABORTED);
        EXIT_FN_SRB();
        return FALSE;
    }

    RtlZeroMemory(srbExt, sizeof(*srbExt));
    srbExt->psgl = srbExt->sg;
    srbExt->pdesc = srbExt->desc;

    if (SRB_FUNCTION(Srb) != SRB_FUNCTION_EXECUTE_SCSI)
    {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Srb = 0x%p Function = 0x%x\n", Srb, SRB_FUNCTION(Srb));
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
        EXIT_FN_SRB();
        return TRUE;
    }

    if (!cdb)
    {
        RhelDbgPrint(TRACE_LEVEL_ERROR,
                     " no CDB ( Srb = 0x%p on %d::%d::%d Function = 0x%x)\n",
                     Srb,
                     SRB_PATH_ID(Srb),
                     SRB_TARGET_ID(Srb),
                     SRB_LUN(Srb),
                     SRB_FUNCTION(Srb));
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
        EXIT_FN_SRB();
        return TRUE;
    }

    switch (cdb->CDB6GENERIC.OperationCode)
    {
        case SCSIOP_READ6:
        case SCSIOP_READ:
        case SCSIOP_READ12:
        case SCSIOP_READ16:

        case SCSIOP_WRITE6:
        case SCSIOP_WRITE:
        case SCSIOP_WRITE12:
        case SCSIOP_WRITE16:

        case SCSIOP_VERIFY6:
        case SCSIOP_VERIFY:
        case SCSIOP_VERIFY12:
        case SCSIOP_VERIFY16:

        case SCSIOP_WRITE_VERIFY:
        case SCSIOP_WRITE_VERIFY12:
        case SCSIOP_WRITE_VERIFY16:
            {
                break;
            }
        default:
            {
                SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
                EXIT_FN_SRB();
                return TRUE;
            }
    }

    lba = RhelGetLba(DeviceExtension, cdb);
    blocks = (SRB_DATA_TRANSFER_LENGTH(Srb) + adaptExt->info.blk_size - 1) / adaptExt->info.blk_size;
    if (lba > adaptExt->lastLBA - 1)
    {
        RhelDbgPrint(TRACE_LEVEL_ERROR,
                     " SRB_STATUS_BAD_SRB_BLOCK_LENGTH lba = %llu lastLBA= %llu\n",
                     lba,
                     adaptExt->lastLBA);
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        EXIT_FN_SRB();
        return FALSE;
    }
    if ((lba + blocks) > adaptExt->lastLBA - 1)
    {
        RhelDbgPrint(TRACE_LEVEL_ERROR,
                     " SRB_STATUS_BAD_SRB_BLOCK_LENGTH lba = %llu lastLBA= %llu blocks = %lu\n",
                     lba,
                     adaptExt->lastLBA,
                     blocks);
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
        EXIT_FN_SRB();
        return FALSE;
    }

    sgList = StorPortGetScatterGatherList(DeviceExtension, Srb);
    if (!sgList)
    {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " no SGL\n");
        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_BAD_FUNCTION);
        EXIT_FN_SRB();
        return FALSE;
    }

    sgMaxElements = min((adaptExt->max_segments + VIRTIO_MS_NOPB_OFFSET), sgList->NumberOfElements);

    sgElement = 0;
    srbExt->Xfer = 0;

    if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_SIZE_MAX))
    {
        for (sgl_idx = 0, sgElement = 1; sgl_idx < sgMaxElements; sgl_idx++)
        {
            sgLength = sgList->List[sgl_idx].Length;
            sgOffset = 0;
            while (sgLength > 0)
            {
                if (sgElement > adaptExt->info.seg_max)
                {
                    RhelDbgPrint(TRACE_LEVEL_ERROR, " wrong SGL, the numer of elements or the size is wrong\n");
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_BAD_SRB_BLOCK_LENGTH);
                    return FALSE;
                }

                srbExt->psgl[sgElement].physAddr.QuadPart = sgList->List[sgl_idx].PhysicalAddress.QuadPart + sgOffset;

                if (sgLength > adaptExt->info.size_max)
                {
                    srbExt->psgl[sgElement].length = adaptExt->info.size_max;
                    sgOffset += adaptExt->info.size_max;
                    sgLength -= adaptExt->info.size_max;
                    srbExt->Xfer += sgLength;
                }
                else
                {
                    srbExt->psgl[sgElement].length = sgLength;
                    srbExt->Xfer += sgLength;
                    sgLength = 0;
                }
                sgElement++;
            }
        }
    }
    else
    {
        for (sgl_idx = 0, sgElement = 1; sgl_idx < sgMaxElements; sgl_idx++, sgElement++)
        {
            srbExt->psgl[sgElement].physAddr = sgList->List[sgl_idx].PhysicalAddress;
            srbExt->psgl[sgElement].length = sgList->List[sgl_idx].Length;
            srbExt->Xfer += sgList->List[sgl_idx].Length;
        }
    }

    srbExt->vbr.out_hdr.sector = lba;
    srbExt->vbr.out_hdr.ioprio = 0;
    srbExt->vbr.req = (PSRB_TYPE)Srb;

    srbExt->fua = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_FLUSH) ? (cdb->CDB10.ForceUnitAccess == 1) : FALSE;
    RhelDbgPrint(TRACE_FUA,
                 " cdb->CDB10.ForceUnitAccess : %d | Force Unit Access (FUA) flag : %s.\n",
                 cdb->CDB10.ForceUnitAccess,
                 srbExt->fua ? "ON" : "OFF");

    if (SRB_FLAGS(Srb) & SRB_FLAGS_DATA_OUT)
    {
        srbExt->vbr.out_hdr.type = VIRTIO_BLK_T_OUT;
        srbExt->out = sgElement;
        srbExt->in = 1;
    }
    else
    {
        srbExt->vbr.out_hdr.type = VIRTIO_BLK_T_IN;
        srbExt->out = 1;
        srbExt->in = sgElement;
    }

    srbExt->psgl[0].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.out_hdr, &dummy);
    srbExt->psgl[0].length = sizeof(srbExt->vbr.out_hdr);

    srbExt->psgl[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &srbExt->vbr.status, &dummy);
    srbExt->psgl[sgElement].length = sizeof(srbExt->vbr.status);

    EXIT_FN_SRB();
    return TRUE;
}

UCHAR
RhelScsiGetInquiryData(PVOID DeviceExtension, PSRB_TYPE Srb)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PINQUIRYDATA InquiryData = NULL;
    ULONG dataLen = 0;
    UCHAR SrbStatus = SRB_STATUS_INVALID_LUN;
    PCDB cdb = SRB_CDB(Srb);
    STOR_UNIT_ATTRIBUTES attributes = {0};

    if (!cdb)
    {
        return SRB_STATUS_ERROR;
    }

    InquiryData = (PINQUIRYDATA)SRB_DATA_BUFFER(Srb);
    dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    SrbStatus = SRB_STATUS_SUCCESS;
    if ((cdb->CDB6INQUIRY3.PageCode != VPD_SUPPORTED_PAGES) && (cdb->CDB6INQUIRY3.EnableVitalProductData == 0))
    {
        UCHAR ScsiStatus = SCSISTAT_CHECK_CONDITION;
        SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_SUPPORTED_PAGES) && (cdb->CDB6INQUIRY3.EnableVitalProductData == 1))
    {
        PVPD_SUPPORTED_PAGES_PAGE SupportPages;
        SupportPages = (PVPD_SUPPORTED_PAGES_PAGE)SRB_DATA_BUFFER(Srb);
        memset(SupportPages, 0, sizeof(VPD_SUPPORTED_PAGES_PAGE));
        SupportPages->PageCode = VPD_SUPPORTED_PAGES;
        SupportPages->SupportedPageList[0] = VPD_SUPPORTED_PAGES;
        SupportPages->SupportedPageList[1] = VPD_SERIAL_NUMBER;
        SupportPages->SupportedPageList[2] = VPD_DEVICE_IDENTIFIERS;
        SupportPages->PageLength = 3;
        SupportPages->SupportedPageList[3] = VPD_BLOCK_LIMITS;
        SupportPages->PageLength = 4;
        if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_DISCARD))
        {
            SupportPages->SupportedPageList[4] = VPD_BLOCK_DEVICE_CHARACTERISTICS;
            SupportPages->SupportedPageList[5] = VPD_LOGICAL_BLOCK_PROVISIONING;
            SupportPages->PageLength = 6;
        }
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_SUPPORTED_PAGES_PAGE) + SupportPages->PageLength));
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_SERIAL_NUMBER) && (cdb->CDB6INQUIRY3.EnableVitalProductData == 1))
    {

        PVPD_SERIAL_NUMBER_PAGE SerialPage;
        SerialPage = (PVPD_SERIAL_NUMBER_PAGE)SRB_DATA_BUFFER(Srb);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, "VPD_SERIAL_NUMBER dataLen = %d.\n", dataLen);

        RtlZeroMemory(SerialPage, dataLen);
        SerialPage->DeviceType = DIRECT_ACCESS_DEVICE;
        SerialPage->DeviceTypeQualifier = DEVICE_CONNECTED;
        SerialPage->PageCode = VPD_SERIAL_NUMBER;

        if (!adaptExt->sn_ok)
        {
            if (!SendSRB(DeviceExtension, (PSRB_TYPE)Srb, SEND_SRB_GET_SERIAL_NUMBER, SEND_SRB_NO_EXISTING_SPINLOCK))
            {
                RhelDbgPrint(TRACE_LEVEL_ERROR, " GetSerialNumber operation FAILED.\n");
                return SRB_STATUS_ERROR;
            }
            return SRB_STATUS_PENDING;
        }

        if (dataLen >= 0x18)
        {
            UCHAR len = strlen(adaptExt->sn);
            SerialPage->PageLength = min(BLOCK_SERIAL_STRLEN, len);
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, "PageLength = %d (%d)\n", SerialPage->PageLength, len);
            StorPortCopyMemory(&SerialPage->SerialNumber, &adaptExt->sn, SerialPage->PageLength);
            SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_SERIAL_NUMBER_PAGE) + SerialPage->PageLength));
        }
        else
        {
            RhelDbgPrint(TRACE_LEVEL_ERROR, " GetSerialNumber invalid dataLen = %d.\n", dataLen);
            return SRB_STATUS_INVALID_REQUEST;
        }
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_DEVICE_IDENTIFIERS) && (cdb->CDB6INQUIRY3.EnableVitalProductData == 1))
    {

        PVPD_IDENTIFICATION_PAGE IdentificationPage = NULL;
        PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr = NULL;
        UCHAR len = 0;

        IdentificationPage = (PVPD_IDENTIFICATION_PAGE)SRB_DATA_BUFFER(Srb);
        memset(IdentificationPage, 0, sizeof(VPD_IDENTIFICATION_PAGE));
        IdentificationPage->PageCode = VPD_DEVICE_IDENTIFIERS;

        IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)IdentificationPage->Descriptors;
        memset(IdentificationDescr, 0, sizeof(VPD_IDENTIFICATION_DESCRIPTOR));

        if (!adaptExt->sn_ok)
        {
            if (!SendSRB(DeviceExtension, (PSRB_TYPE)Srb, SEND_SRB_GET_SERIAL_NUMBER, SEND_SRB_NO_EXISTING_SPINLOCK))
            {
                RhelDbgPrint(TRACE_LEVEL_ERROR, " GetSerialNumber operation FAILED.\n");
                return SRB_STATUS_ERROR;
            }
            return SRB_STATUS_PENDING;
        }

        ReportDeviceIdentifier(DeviceExtension, Srb);
    }
    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_BLOCK_LIMITS) && (cdb->CDB6INQUIRY3.EnableVitalProductData == 1) &&
             (dataLen >= 0x14))
    {

        PVPD_BLOCK_LIMITS_PAGE LimitsPage;
        ULONG max_io_size = adaptExt->max_tx_length / adaptExt->info.blk_size;
        USHORT pageLen = 0x10;

        LimitsPage = (PVPD_BLOCK_LIMITS_PAGE)SRB_DATA_BUFFER(Srb);
        LimitsPage->DeviceType = DIRECT_ACCESS_DEVICE;
        LimitsPage->DeviceTypeQualifier = DEVICE_CONNECTED;
        LimitsPage->PageCode = VPD_BLOCK_LIMITS;
        REVERSE_BYTES_SHORT(&LimitsPage->OptimalTransferLengthGranularity, &adaptExt->info.min_io_size);
        REVERSE_BYTES(&LimitsPage->MaximumTransferLength, &max_io_size);
        REVERSE_BYTES(&LimitsPage->OptimalTransferLength, &adaptExt->info.opt_io_size);
        if ((CHECKBIT(adaptExt->features, VIRTIO_BLK_F_DISCARD)) && (dataLen >= 0x14))
        {
            ULONG max_discard_sectors = adaptExt->info.max_discard_sectors;
            ULONG discard_sector_alignment = 0;
            ULONG opt_unmap_granularity = adaptExt->info.discard_sector_alignment / adaptExt->info.blk_size;

            pageLen = 0x3c;
            REVERSE_BYTES(&LimitsPage->MaximumUnmapLBACount, &max_discard_sectors);
            REVERSE_BYTES(&LimitsPage->MaximumUnmapBlockDescriptorCount, &adaptExt->info.max_discard_seg);
            REVERSE_BYTES(&LimitsPage->OptimalUnmapGranularity, &opt_unmap_granularity);
            REVERSE_BYTES(&LimitsPage->UnmapGranularityAlignment, &discard_sector_alignment);
            LimitsPage->UGAValid = 1;
        }
        REVERSE_BYTES_SHORT(&LimitsPage->PageLength, &pageLen);
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (FIELD_OFFSET(VPD_BLOCK_LIMITS_PAGE, Reserved0) + pageLen));
    }

    else if ((cdb->CDB6INQUIRY3.PageCode == VPD_BLOCK_DEVICE_CHARACTERISTICS) &&
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1) && (dataLen >= 0x08))
    {

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
             (cdb->CDB6INQUIRY3.EnableVitalProductData == 1) && (dataLen >= 0x08))
    {

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
        ProvisioningPage->ProvisioningType = adaptExt->info.discard_sector_alignment ? PROVISIONING_TYPE_THIN
                                                                                     : PROVISIONING_TYPE_RESOURCE;
    }

    else if (dataLen > sizeof(INQUIRYDATA))
    {
        StorPortMoveMemory(InquiryData, &adaptExt->inquiry_data, sizeof(INQUIRYDATA));
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(INQUIRYDATA)));
    }
    else
    {
        StorPortMoveMemory(InquiryData, &adaptExt->inquiry_data, dataLen);
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, dataLen);
    }

    StorPortSetDeviceQueueDepth(DeviceExtension,
                                SRB_PATH_ID(Srb),
                                SRB_TARGET_ID(Srb),
                                SRB_LUN(Srb),
                                adaptExt->queue_depth);
    attributes.DeviceAttentionSupported = 1;
    attributes.AsyncNotificationSupported = 1;
    attributes.D3ColdNotSupported = 1;

    StorPortSetUnitAttributes(DeviceExtension, (PSTOR_ADDRESS)&adaptExt->device_address, attributes);
    return SrbStatus;
}

UCHAR
RhelScsiReportLuns(PVOID DeviceExtension, PSRB_TYPE Srb)
{
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    PUCHAR data = (PUCHAR)SRB_DATA_BUFFER(Srb);

    UNREFERENCED_PARAMETER(DeviceExtension);

    data[3] = 8;
    SRB_SET_SRB_STATUS(Srb, SrbStatus);
    SRB_SET_DATA_TRANSFER_LENGTH(Srb, 16);
    return SrbStatus;
}

UCHAR
RhelScsiGetModeSense(PVOID DeviceExtension, PSRB_TYPE Srb)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONG ModeSenseDataLen;
    UCHAR SrbStatus = SRB_STATUS_INVALID_LUN;
    PCDB cdb = SRB_CDB(Srb);
    PMODE_PARAMETER_HEADER header;
    PMODE_CACHING_PAGE cachePage;
    PMODE_PARAMETER_BLOCK blockDescriptor;

    ModeSenseDataLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    SrbStatus = SRB_STATUS_INVALID_REQUEST;

    if (!cdb)
    {
        return SRB_STATUS_ERROR;
    }

    if ((cdb->MODE_SENSE.PageCode == MODE_PAGE_CACHING) || (cdb->MODE_SENSE.PageCode == MODE_SENSE_RETURN_ALL))
    {

        if (sizeof(MODE_PARAMETER_HEADER) > ModeSenseDataLen)
        {
            SrbStatus = SRB_STATUS_ERROR;
            return SrbStatus;
        }

        header = (PMODE_PARAMETER_HEADER)SRB_DATA_BUFFER(Srb);

        memset(header, 0, sizeof(MODE_PARAMETER_HEADER));
        header->DeviceSpecificParameter = MODE_DSP_FUA_SUPPORTED;

        if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO))
        {
            header->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;
        }

        ModeSenseDataLen -= sizeof(MODE_PARAMETER_HEADER);
        if (ModeSenseDataLen >= sizeof(MODE_CACHING_PAGE))
        {

            header->ModeDataLength = sizeof(MODE_CACHING_PAGE) + 3;
            cachePage = (PMODE_CACHING_PAGE)header;
            cachePage = (PMODE_CACHING_PAGE)((unsigned char *)(cachePage) + (ULONG)sizeof(MODE_PARAMETER_HEADER));
            memset(cachePage, 0, sizeof(MODE_CACHING_PAGE));
            cachePage->PageCode = MODE_PAGE_CACHING;
            cachePage->PageLength = 10;
            cachePage->WriteCacheEnable = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_FLUSH) ? 1 : 0;

            SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(MODE_PARAMETER_HEADER) + sizeof(MODE_CACHING_PAGE)));
        }
        else
        {
            SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(MODE_PARAMETER_HEADER)));
        }

        SrbStatus = SRB_STATUS_SUCCESS;
    }
    else if (cdb->MODE_SENSE.PageCode == MODE_PAGE_VENDOR_SPECIFIC)
    {

        if (sizeof(MODE_PARAMETER_HEADER) > ModeSenseDataLen)
        {
            SrbStatus = SRB_STATUS_ERROR;
            return SrbStatus;
        }

        header = (PMODE_PARAMETER_HEADER)SRB_DATA_BUFFER(Srb);
        memset(header, 0, sizeof(MODE_PARAMETER_HEADER));
        header->DeviceSpecificParameter = MODE_DSP_FUA_SUPPORTED;

        if (CHECKBIT(adaptExt->features, VIRTIO_BLK_F_RO))
        {
            header->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;
        }

        ModeSenseDataLen -= sizeof(MODE_PARAMETER_HEADER);
        if (ModeSenseDataLen >= sizeof(MODE_PARAMETER_BLOCK))
        {

            header->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);
            blockDescriptor = (PMODE_PARAMETER_BLOCK)header;
            blockDescriptor = (PMODE_PARAMETER_BLOCK)((unsigned char *)(blockDescriptor) +
                                                      (ULONG)sizeof(MODE_PARAMETER_HEADER));

            memset(blockDescriptor, 0, sizeof(MODE_PARAMETER_BLOCK));

            SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(MODE_PARAMETER_HEADER) + sizeof(MODE_PARAMETER_BLOCK)));
        }
        else
        {
            SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(MODE_PARAMETER_HEADER)));
        }
        SrbStatus = SRB_STATUS_SUCCESS;
    }
    else
    {
        SrbStatus = SRB_STATUS_INVALID_REQUEST;
    }

    return SrbStatus;
}

UCHAR
RhelScsiGetCapacity(PVOID DeviceExtension, PSRB_TYPE Srb)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    PREAD_CAPACITY_DATA readCap;
    PREAD_CAPACITY16_DATA readCapEx;
    u64 lastLBA;
    EIGHT_BYTE lba;
    u64 blocksize;
    PCDB cdb = SRB_CDB(Srb);
    ULONG srbdatalen = 0;
    UCHAR PMI = 0;

    if (!cdb)
    {
        return SRB_STATUS_ERROR;
    }

    readCap = (PREAD_CAPACITY_DATA)SRB_DATA_BUFFER(Srb);
    readCapEx = (PREAD_CAPACITY16_DATA)SRB_DATA_BUFFER(Srb);

    srbdatalen = SRB_DATA_TRANSFER_LENGTH(Srb);
    lba.AsULongLong = 0;
    if (cdb->CDB6GENERIC.OperationCode == SCSIOP_READ_CAPACITY16)
    {
        PMI = cdb->READ_CAPACITY16.PMI & 1;
        REVERSE_BYTES_QUAD(&lba, &cdb->READ_CAPACITY16.LogicalBlock[0]);
    }

    if (!PMI && lba.AsULongLong)
    {
        PSENSE_DATA senseBuffer = NULL;
        UCHAR ScsiStatus = SCSISTAT_CHECK_CONDITION;
        SRB_GET_SENSE_INFO_BUFFER(Srb, senseBuffer);
        if (senseBuffer)
        {
            senseBuffer->SenseKey = SCSI_SENSE_ILLEGAL_REQUEST;
            senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_INVALID_CDB;
        }
        SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
        return SrbStatus;
    }

    blocksize = adaptExt->info.blk_size;
    lastLBA = adaptExt->info.capacity / (blocksize / SECTOR_SIZE) - 1;
    adaptExt->lastLBA = adaptExt->info.capacity;

    if (srbdatalen == sizeof(READ_CAPACITY_DATA))
    {
        if (lastLBA > 0xFFFFFFFF)
        {
            readCap->LogicalBlockAddress = (ULONG)-1;
        }
        else
        {
            REVERSE_BYTES(&readCap->LogicalBlockAddress, &lastLBA);
        }
        REVERSE_BYTES(&readCap->BytesPerBlock, &blocksize);
    }
    else
    {
        REVERSE_BYTES_QUAD(&readCapEx->LogicalBlockAddress.QuadPart, &lastLBA);
        REVERSE_BYTES(&readCapEx->BytesPerBlock, &blocksize);

        if (srbdatalen >= (ULONG)FIELD_OFFSET(READ_CAPACITY16_DATA, Reserved3))
        {
            readCapEx->LogicalPerPhysicalExponent = adaptExt->info.physical_block_exp;
            srbdatalen = FIELD_OFFSET(READ_CAPACITY16_DATA, Reserved3);
            readCapEx->LBPME = CHECKBIT(adaptExt->features, VIRTIO_BLK_F_DISCARD) ? 1 : 0;
            readCapEx->LBPRZ = 0;
            SRB_SET_DATA_TRANSFER_LENGTH(Srb, srbdatalen);
        }
    }
    return SrbStatus;
}

UCHAR
RhelScsiVerify(PVOID DeviceExtension, PSRB_TYPE Srb)
{
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    ULONGLONG lba;
    ULONG blocks;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PCDB cdb = SRB_CDB(Srb);
    ULONG srbdatalen = 0;

    if (!cdb)
    {
        return SRB_STATUS_ERROR;
    }

    lba = RhelGetLba(DeviceExtension, cdb);
    blocks = RhelGetSectors(DeviceExtension, cdb);
    if ((lba + blocks) > adaptExt->lastLBA)
    {
        RhelDbgPrint(TRACE_LEVEL_ERROR, " lba = %llu lastLBA= %llu blocks = %lu\n", lba, adaptExt->lastLBA, blocks);
        SrbStatus = SRB_STATUS_INVALID_REQUEST;
    }
    return SrbStatus;
}

VOID CompleteRequestWithStatus(PVOID DeviceExtension, PSRB_TYPE Srb, UCHAR status)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION srbExt = SRB_EXTENSION(Srb);
    ULONG srbDataTransferLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    if ((SRB_FUNCTION(Srb) == SRB_FUNCTION_EXECUTE_SCSI) && (adaptExt->check_condition == TRUE) &&
        (status == SRB_STATUS_SUCCESS) && (!CHECKFLAG(SRB_FLAGS(Srb), SRB_FLAGS_DISABLE_AUTOSENSE)))
    {
        PCDB cdb = SRB_CDB(Srb);

        if (cdb != NULL)
        {
            UCHAR OpCode = cdb->CDB6GENERIC.OperationCode;
            if ((OpCode != SCSIOP_INQUIRY) && (OpCode != SCSIOP_REPORT_LUNS))
            {
                if (SetSenseInfo(DeviceExtension, Srb, idx_CompleteRequestWithStatus, idx_SetSenseInfo))
                {
                    status = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
                    adaptExt->check_condition = FALSE;
                }
            }
        }
    }
    else if (srbExt && srbExt->Xfer && srbDataTransferLen > srbExt->Xfer)
    {
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, srbExt->Xfer);
        status = SRB_STATUS_DATA_OVERRUN;
        adaptExt->check_condition = FALSE;
    }

    SRB_SET_SRB_STATUS(Srb, status);
#ifdef DBG
    InterlockedDecrement((LONG volatile *)&adaptExt->srb_cnt);
#endif
    StorPortNotification(RequestComplete, DeviceExtension, Srb);
}

VOID FORCEINLINE DeviceChangeNotification(PVOID DeviceExtension,
                                          BOOLEAN bLun,
                                          INL_FUNC_IDX idx_ICN,
                                          INL_FUNC_IDX idx_IFN)
{
    PVOID ICN = inline_func_str_map[idx_ICN];
    PVOID IFN = inline_func_str_map[idx_IFN];

    ENTER_INL_FN();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    StorPortStateChangeDetected(DeviceExtension,
                                bLun ? STATE_CHANGE_LUN : STATE_CHANGE_BUS,
                                (PSTOR_ADDRESS)&adaptExt->device_address,
                                0,
                                NULL,
                                NULL);
    RhelDbgPrintInline(TRACE_LEVEL_INFORMATION, " StorPortStateChangeDetected.\n");

    EXIT_INL_FN();
}

BOOLEAN
FORCEINLINE
SetSenseInfo(PVOID DeviceExtension, PSRB_TYPE Srb, INL_FUNC_IDX idx_ICN, INL_FUNC_IDX idx_IFN)
{
    PVOID ICN = inline_func_str_map[idx_ICN];
    PVOID IFN = inline_func_str_map[idx_IFN];
    ENTER_INL_FN_SRB();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSENSE_DATA senseInfoBuffer = NULL;
    UCHAR senseInfoBufferLength = 0;
    SRB_GET_SENSE_INFO_BUFFER(Srb, senseInfoBuffer);
    SRB_GET_SENSE_INFO_BUFFER_LENGTH(Srb, senseInfoBufferLength);
    if (senseInfoBuffer && (senseInfoBufferLength >= sizeof(SENSE_DATA)))
    {
        UCHAR ScsiStatus = SCSISTAT_CHECK_CONDITION;
        senseInfoBuffer->ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
        senseInfoBuffer->Valid = 1;
        senseInfoBuffer->SenseKey = adaptExt->sense_info.senseKey;
        senseInfoBuffer->AdditionalSenseLength = sizeof(SENSE_DATA) -
                                                 FIELD_OFFSET(SENSE_DATA, AdditionalSenseLength); // 0xb ??
        senseInfoBuffer->AdditionalSenseCode = adaptExt->sense_info.additionalSenseCode;
        senseInfoBuffer->AdditionalSenseCodeQualifier = adaptExt->sense_info.additionalSenseCodeQualifier;
        SRB_SET_SCSI_STATUS(((PSRB_TYPE)Srb), ScsiStatus);
        RhelDbgPrintInline(TRACE_LEVEL_INFORMATION,
                           " senseKey = 0x%x asc = 0x%x ascq = 0x%x\n",
                           adaptExt->sense_info.senseKey,
                           adaptExt->sense_info.additionalSenseCode,
                           adaptExt->sense_info.additionalSenseCodeQualifier);
        EXIT_INL_FN_SRB();
        return TRUE;
    }
    RhelDbgPrintInline(TRACE_LEVEL_ERROR,
                       " INVALID senseInfoBuffer %p or senseInfoBufferLength = %d\n",
                       senseInfoBuffer,
                       senseInfoBufferLength);
    EXIT_INL_FN_SRB();
    return FALSE;
}

VOID DispatchQueue(PVOID DeviceExtension, ULONG MessageId)
{
    ENTER_FN();

    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    BOOLEAN dpc_queued_status = FALSE;

    if (!adaptExt->dump_mode && adaptExt->dpc_ready)
    {
        if (adaptExt->msix_enabled && adaptExt->msix_one_vector)
        {
            NT_ASSERT(MessageId == VIRTIO_BLK_MSIX_1_VECTOR_MSG_ID);
            dpc_queued_status = StorPortIssueDpc(DeviceExtension,
                                                 &adaptExt->dpc[MessageId -
                                                                QUEUE_TO_MESSAGE_1_VECTOR(VIRTIO_BLK_REQUEST_QUEUE_0)],
                                                 ULongToPtr(MessageId),
                                                 ULongToPtr(MessageId));
        }
        else
        {
            NT_ASSERT(MessageId >= QUEUE_TO_MESSAGE(VIRTIO_BLK_REQUEST_QUEUE_0));
            dpc_queued_status = StorPortIssueDpc(DeviceExtension,
                                                 &adaptExt->dpc[MessageId -
                                                                QUEUE_TO_MESSAGE(VIRTIO_BLK_REQUEST_QUEUE_0)],
                                                 ULongToPtr(MessageId),
                                                 ULongToPtr(MessageId));
        }
        if (dpc_queued_status)
        {
            RhelDbgPrint(TRACE_DPC, " The request to queue a DPC was successful.\n");
        }
        else
        {
            RhelDbgPrint(TRACE_DPC,
                         " The request to queue a DPC was NOT successful. It may already be queued elsewhere.\n");
        }
    }
    else
    {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                     " We are in Crash Dump Mode or DPC is unavailable."
                     " Calling ProcessBuffer() without spinlocks...\n");
        ProcessBuffer(DeviceExtension, MessageId, PROCESS_BUFFER_NO_SPINLOCKS);
    }

    EXIT_FN();
    return;
}

UCHAR DeviceToSrbStatus(UCHAR status)
{
    switch (status)
    {
        case VIRTIO_BLK_S_OK:
            RhelDbgPrint(TRACE_LEVEL_VERBOSE, " VIRTIO_BLK_S_OK\n");
            return SRB_STATUS_SUCCESS;
        case VIRTIO_BLK_S_IOERR:
            RhelDbgPrint(TRACE_LEVEL_ERROR, " VIRTIO_BLK_S_IOERR\n");
            return SRB_STATUS_ERROR;
        case VIRTIO_BLK_S_UNSUPP:
            RhelDbgPrint(TRACE_LEVEL_ERROR, " VIRTIO_BLK_S_UNSUPP\n");
            return SRB_STATUS_INVALID_REQUEST;
    }
    RhelDbgPrint(TRACE_LEVEL_ERROR, " Unknown device status %x\n", status);
    return SRB_STATUS_ERROR;
}

VOID ProcessBuffer(PVOID DeviceExtension, ULONG MessageId, PROCESS_BUFFER_LOCKING_MODE LockMode)
{
    ENTER_FN();

    unsigned int len;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONG QueueNumber = 0;
    STOR_LOCK_HANDLE LockHandle = {0};
    struct virtqueue *vq;
    ULONG_PTR srbId = 0;
    PSRB_TYPE Srb = NULL;
    PSRB_EXTENSION srbExt = NULL;
    UCHAR srbStatus = SRB_STATUS_SUCCESS;
    PREQUEST_LIST element = NULL;
    ULONG vq_req_idx = VIRTIO_BLK_REQUEST_QUEUE_0;

    if (adaptExt->msix_enabled && adaptExt->msix_one_vector)
    {
        QueueNumber = MESSAGE_TO_QUEUE_1_VECTOR(MessageId);
    }
    else
    {
        QueueNumber = MESSAGE_TO_QUEUE(MessageId);
        if (QueueNumber >= adaptExt->num_queues + VIRTIO_BLK_REQUEST_QUEUE_0)
        {
            RhelDbgPrint(TRACE_VQ,
                         " Modulo assignment required for QueueNumber as it"
                         " exceeds the number of virtqueues available.\n");
            QueueNumber %= adaptExt->num_queues;
        }
    }

    vq_req_idx = QueueNumber - VIRTIO_BLK_REQUEST_QUEUE_0;
    vq = adaptExt->vq[vq_req_idx];

    if (LockMode != PROCESS_BUFFER_NO_SPINLOCKS)
    {
        VioStorVQLock(DeviceExtension, MessageId, &LockHandle);
    }

    do
    {
        virtqueue_disable_cb(vq);
        while ((srbId = (ULONG_PTR)virtqueue_get_buf(vq, &len)) != 0)
        {
            element = &adaptExt->processing_srbs[vq_req_idx];

            PLIST_ENTRY le = NULL;
            BOOLEAN bFound = FALSE;
#ifdef DBG
            InterlockedDecrement((LONG volatile *)&adaptExt->inqueue_cnt);
#endif
            for (le = element->srb_list.Flink; le != &element->srb_list && !bFound; le = le->Flink)
            {
                pblk_req req = CONTAINING_RECORD(le, blk_req, list_entry);

                Srb = req->req;
                srbExt = SRB_EXTENSION(Srb);

                RhelDbgPrint(TRACE_VQ_PB_SRB_ID,
                             " le : %p | srb_list : %p | Next le (Flink) : %p |"
                             " SRB : %p | SrbExt->id : %p | srbId : %p \n",
                             le,
                             &element->srb_list,
                             le->Flink,
                             Srb,
                             (void *)srbExt->id,
                             (void *)srbId);

                // Only SRBs with existing (i.e. non-NULL) extension
                // are inserted into our queues, thus, we may help
                // the Code Analysis and provide it with this information
                // in order to avoid false-positive warnings.
                _Analysis_assume_(srbExt != NULL);
                if (srbExt->id == srbId)
                {
                    RemoveEntryList(le);
                    bFound = TRUE;
                    element->srb_cnt--;
                    RhelDbgPrint(TRACE_VQ,
                                 " VQ Buffer Length : %lu | SRB DataTransferLength : %lu \n",
                                 len,
                                 Srb->DataTransferLength);

                    break;
                }
            }

            if (!bFound)
            {
                PVOID lockmode_str = lockmode_str_map[LockMode];
                PVOID type_str = virtio_blk_t_str_map[srbExt->vbr.out_hdr.type];
                if (adaptExt->msix_enabled)
                {
                    RhelDbgPrint(TRACE_LEVEL_WARNING,
                                 " SRB Extension with ID 0x%p was NOT found...! |"
                                 " SRB : %p | Type : %s | MessageId : 0x%x | LockMode : %s \n",
                                 (void *)srbId,
                                 Srb,
                                 type_str,
                                 MessageId,
                                 lockmode_str);
                }
                else
                {
                    RhelDbgPrint(TRACE_LEVEL_WARNING,
                                 " SRB Extension with ID 0x%p was NOT found...! |"
                                 " SRB : %p | Type : %s | LockMode : %s \n",
                                 (void *)srbId,
                                 Srb,
                                 type_str,
                                 lockmode_str);
                }
            }

            if (bFound && srbExt->vbr.out_hdr.type == VIRTIO_BLK_T_GET_ID)
            {
                adaptExt->sn_ok = TRUE;
                if (Srb)
                {
                    PCDB cdb = SRB_CDB(Srb);

                    if (!cdb)
                    {
                        continue;
                    }

                    if ((cdb->CDB6INQUIRY3.PageCode == VPD_SERIAL_NUMBER) &&
                        (cdb->CDB6INQUIRY3.EnableVitalProductData == 1))
                    {
                        PVPD_SERIAL_NUMBER_PAGE SerialPage;
                        ULONG dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);
                        UCHAR len = strlen(adaptExt->sn);

                        SerialPage = (PVPD_SERIAL_NUMBER_PAGE)SRB_DATA_BUFFER(Srb);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_BLK_T_GET_ID - dataLen = %d\n", dataLen);
                        RtlZeroMemory(SerialPage, dataLen);
                        SerialPage->DeviceType = DIRECT_ACCESS_DEVICE;
                        SerialPage->DeviceTypeQualifier = DEVICE_CONNECTED;
                        SerialPage->PageCode = VPD_SERIAL_NUMBER;

                        SerialPage->PageLength = min(BLOCK_SERIAL_STRLEN, len);
                        StorPortCopyMemory(&SerialPage->SerialNumber, &adaptExt->sn, SerialPage->PageLength);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                     " VIRTIO_BLK_T_GET_ID - PageLength = %d (%d)\n",
                                     SerialPage->PageLength,
                                     len);

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
            if (bFound && Srb)
            {
                srbStatus = DeviceToSrbStatus(srbExt->vbr.status);
                PVOID srbStatus_str = srb_status_str_map[srbStatus];
                if (adaptExt->msix_enabled)
                {
                    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                                 " SRB : %p | QueueNumber : %lu | MessageId : %lu |"
                                 " VBR Status : %s | SRB Status : %s | FUA : %s \n",
                                 Srb,
                                 QueueNumber,
                                 MessageId,
                                 GetVbrStatusDesc(srbExt->vbr.status),
                                 srbStatus_str,
                                 srbExt->fua ? "ON" : "OFF");
                }
                else
                {
                    RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                                 " SRB : %p | QueueNumber : %lu |"
                                 " VBR Status : %s | SRB Status : %s | FUA : %s \n",
                                 Srb,
                                 QueueNumber,
                                 GetVbrStatusDesc(srbExt->vbr.status),
                                 srbStatus_str,
                                 srbExt->fua ? "ON" : "OFF");
                }
                if (srbExt && srbExt->fua == TRUE)
                {
                    SRB_SET_SRB_STATUS(Srb, SRB_STATUS_PENDING);
                    if (!SendSRB(DeviceExtension, (PSRB_TYPE)Srb, SEND_SRB_FLUSH, SEND_SRB_ALREADY_UNDER_SPINLOCK))
                    {
                        CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, SRB_STATUS_ERROR);
                    }
                    srbExt->fua = FALSE;
                }
                else
                {
                    CompleteRequestWithStatus(DeviceExtension, (PSRB_TYPE)Srb, srbStatus);
                }
            }
        }
    } while (!virtqueue_enable_cb(vq));

    if (LockMode != PROCESS_BUFFER_NO_SPINLOCKS)
    {
        VioStorVQUnlock(DeviceExtension, MessageId, &LockHandle);
    }

    EXIT_FN();
}

VOID ReportDeviceIdentifier(PVOID DeviceExtension, PSRB_TYPE Srb)
{
    PVPD_IDENTIFICATION_PAGE IdentificationPage = NULL;
    PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr = NULL;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    UCHAR len = strlen(adaptExt->sn);

    IdentificationPage = (PVPD_IDENTIFICATION_PAGE)SRB_DATA_BUFFER(Srb);
    IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)IdentificationPage->Descriptors;
    if (len)
    {
        IdentificationDescr->IdentifierLength = min(BLOCK_SERIAL_STRLEN, len);
        IdentificationDescr->CodeSet = VpdCodeSetAscii;
        IdentificationDescr->IdentifierType = VpdIdentifierTypeVendorSpecific;
        StorPortCopyMemory(&IdentificationDescr->Identifier, &adaptExt->sn, IdentificationDescr->IdentifierLength);
    }
    else
    {
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
    SRB_SET_DATA_TRANSFER_LENGTH(Srb, (sizeof(VPD_IDENTIFICATION_PAGE) + IdentificationPage->PageLength));
}

VOID CompleteDpcRoutine(PSTOR_DPC Dpc, PVOID Context, PVOID SystemArgument1, PVOID SystemArgument2)
{
    ENTER_FN();

    ULONG MessageId = PtrToUlong(SystemArgument1);
    ProcessBuffer(Context, MessageId, PROCESS_BUFFER_WITH_SPINLOCKS);

    EXIT_FN();
}

VOID LogError(PVOID DeviceExtension, ULONG ErrorCode, ULONG UniqueId)
{
    STOR_LOG_EVENT_DETAILS logEvent;
    memset(&logEvent, 0, sizeof(logEvent));
    logEvent.InterfaceRevision = STOR_CURRENT_LOG_INTERFACE_REVISION;
    logEvent.Size = sizeof(logEvent);
    logEvent.EventAssociation = StorEventAdapterAssociation;
    logEvent.StorportSpecificErrorCode = TRUE;
    logEvent.ErrorCode = ErrorCode;
    logEvent.DumpDataSize = sizeof(UniqueId);
    logEvent.DumpData = &UniqueId;
    StorPortLogSystemEvent(DeviceExtension, &logEvent, NULL);
}

PVOID
VioStorPoolAlloc(PVOID DeviceExtension, SIZE_T size)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PVOID ptr = (PVOID)((ULONG_PTR)adaptExt->poolAllocationVa + adaptExt->poolOffset);

    if ((adaptExt->poolOffset + size) <= adaptExt->poolAllocationSize)
    {
        size = ROUND_TO_CACHE_LINES(size);
        adaptExt->poolOffset += (ULONG)size;
        RtlZeroMemory(ptr, size);
        return ptr;
    }
    RhelDbgPrint(TRACE_LEVEL_FATAL, "Ran out of memory in VioStorPoolAlloc(%Id)\n", size);
    return NULL;
}

UCHAR FirmwareRequest(PVOID DeviceExtension, PSRB_TYPE Srb)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION srbExt = NULL;
    ULONG dataLen = 0;
    PSRB_IO_CONTROL srbControl = NULL;
    PFIRMWARE_REQUEST_BLOCK firmwareRequest = NULL;
    UCHAR srbStatus = SRB_STATUS_SUCCESS;
    srbExt = SRB_EXTENSION(Srb);
    srbControl = (PSRB_IO_CONTROL)SRB_DATA_BUFFER(Srb);
    dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    if (dataLen < (sizeof(SRB_IO_CONTROL) + sizeof(FIRMWARE_REQUEST_BLOCK)))
    {
        srbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
        srbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
        RhelDbgPrint(TRACE_LEVEL_ERROR, " FirmwareRequest Bad Block Length  %ul\n", dataLen);
        return srbStatus;
    }

    firmwareRequest = (PFIRMWARE_REQUEST_BLOCK)(srbControl + 1);
    switch (firmwareRequest->Function)
    {

        case FIRMWARE_FUNCTION_GET_INFO:
            {
                PSTORAGE_FIRMWARE_INFO_V2 firmwareInfo;
                firmwareInfo = (PSTORAGE_FIRMWARE_INFO_V2)((PUCHAR)srbControl + firmwareRequest->DataBufferOffset);
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, " FIRMWARE_FUNCTION_GET_INFO \n");
                if ((firmwareInfo->Version >= STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2) ||
                    (firmwareInfo->Size >= sizeof(STORAGE_FIRMWARE_INFO_V2)))
                {
                    firmwareInfo->Version = STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2;
                    firmwareInfo->Size = sizeof(STORAGE_FIRMWARE_INFO_V2);

                    firmwareInfo->UpgradeSupport = TRUE;

                    firmwareInfo->SlotCount = 1;
                    firmwareInfo->ActiveSlot = 0;
                    firmwareInfo->PendingActivateSlot = STORAGE_FIRMWARE_INFO_INVALID_SLOT;
                    firmwareInfo->FirmwareShared = FALSE;
                    firmwareInfo->ImagePayloadAlignment = PAGE_SIZE;
                    firmwareInfo->ImagePayloadMaxSize = PAGE_SIZE;

                    if (firmwareRequest->DataBufferLength >=
                        (sizeof(STORAGE_FIRMWARE_INFO_V2) + sizeof(STORAGE_FIRMWARE_SLOT_INFO_V2)))
                    {
                        firmwareInfo->Slot[0].SlotNumber = 0;
                        firmwareInfo->Slot[0].ReadOnly = FALSE;
                        StorPortCopyMemory(&firmwareInfo->Slot[0].Revision,
                                           &adaptExt->fw_ver,
                                           sizeof(adaptExt->fw_ver));
                        srbControl->ReturnCode = FIRMWARE_STATUS_SUCCESS;
                    }
                    else
                    {
                        firmwareRequest->DataBufferLength = sizeof(STORAGE_FIRMWARE_INFO_V2) +
                                                            sizeof(STORAGE_FIRMWARE_SLOT_INFO_V2);
                        srbControl->ReturnCode = FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL;
                    }
                }
                else
                {
                    RhelDbgPrint(TRACE_LEVEL_ERROR,
                                 " Wrong Version %ul or Size %ul\n",
                                 firmwareInfo->Version,
                                 firmwareInfo->Size);
                    srbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
                    srbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
                }
            }
            break;
        case FIRMWARE_FUNCTION_DOWNLOAD:
            {
                PSTORAGE_FIRMWARE_DOWNLOAD_V2 firmwareDwnld;
                firmwareDwnld = (PSTORAGE_FIRMWARE_DOWNLOAD_V2)((PUCHAR)srbControl + firmwareRequest->DataBufferOffset);
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, " FIRMWARE_FUNCTION_DOWNLOAD \n");
                if ((firmwareDwnld->Version >= STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2) ||
                    (firmwareDwnld->Size >= sizeof(STORAGE_FIRMWARE_DOWNLOAD_V2)))
                {
                    firmwareDwnld->Version = STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2;
                    firmwareDwnld->Size = sizeof(STORAGE_FIRMWARE_DOWNLOAD_V2);
                    adaptExt->fw_ver++;
                    srbControl->ReturnCode = FIRMWARE_STATUS_SUCCESS;
                }
                else
                {
                    RhelDbgPrint(TRACE_LEVEL_ERROR,
                                 " Wrong Version %ul or Size %ul\n",
                                 firmwareDwnld->Version,
                                 firmwareDwnld->Size);
                    srbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
                    srbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
                }
            }
            break;
        case FIRMWARE_FUNCTION_ACTIVATE:
            {
                PSTORAGE_FIRMWARE_ACTIVATE firmwareActivate;
                firmwareActivate = (PSTORAGE_FIRMWARE_ACTIVATE)((PUCHAR)srbControl + firmwareRequest->DataBufferOffset);
                if ((firmwareActivate->Version == STORAGE_FIRMWARE_ACTIVATE_STRUCTURE_VERSION) ||
                    (firmwareActivate->Size >= sizeof(STORAGE_FIRMWARE_ACTIVATE)))
                {
                    firmwareActivate->Version = STORAGE_FIRMWARE_ACTIVATE_STRUCTURE_VERSION;
                    firmwareActivate->Size = sizeof(STORAGE_FIRMWARE_ACTIVATE);
                    srbControl->ReturnCode = FIRMWARE_STATUS_SUCCESS;
                }
                else
                {
                    RhelDbgPrint(TRACE_LEVEL_ERROR,
                                 " Wrong Version %ul or Size %ul\n",
                                 firmwareActivate->Version,
                                 firmwareActivate->Size);
                    srbControl->ReturnCode = FIRMWARE_STATUS_INVALID_PARAMETER;
                    srbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
                }
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, " FIRMWARE_FUNCTION_ACTIVATE \n");
            }
            break;
        default:
            RhelDbgPrint(TRACE_LEVEL_ERROR, " Unsupported Function %ul\n", firmwareRequest->Function);
            srbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }

    return srbStatus;
}
