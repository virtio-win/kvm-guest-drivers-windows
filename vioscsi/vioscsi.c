/*
 * This file contains vioscsi StorPort miniport driver
 *
 * Copyright (c) 2012-2017 Red Hat, Inc.
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
#include "vioscsi.h"
#include "helper.h"
#include "vioscsidt.h"
#include "trace.h"

#if defined(EVENT_TRACING)
#include "vioscsi.tmh"
#endif

#define MS_SM_HBA_API
#include <hbapiwmi.h>

#include <hbaapi.h>
#include <ntddscsi.h>

#define VioScsiWmi_MofResourceName L"MofResource"

#include "resources.h"
#include "..\build\vendor.ver"

#define VIOSCSI_SETUP_GUID_INDEX             0
#define VIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX 1
#define VIOSCSI_MS_PORT_INFORM_GUID_INDEX    2

BOOLEAN IsCrashDumpMode;

sp_DRIVER_INITIALIZE DriverEntry;
HW_INITIALIZE VioScsiHwInitialize;
HW_BUILDIO VioScsiBuildIo;
HW_STARTIO VioScsiStartIo;
HW_FIND_ADAPTER VioScsiFindAdapter;
HW_RESET_BUS VioScsiResetBus;
HW_ADAPTER_CONTROL VioScsiAdapterControl;
HW_UNIT_CONTROL VioScsiUnitControl;
HW_INTERRUPT VioScsiInterrupt;
HW_DPC_ROUTINE VioScsiCompleteDpcRoutine;
HW_PASSIVE_INITIALIZE_ROUTINE VioScsiPassiveInitializeRoutine;
HW_MESSAGE_SIGNALED_INTERRUPT_ROUTINE VioScsiMSInterrupt;

#ifdef EVENT_TRACING
PVOID TraceContext = NULL;
VOID WppCleanupRoutine(PVOID arg1)
{
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " WppCleanupRoutine\n");
    WPP_CLEANUP(NULL, TraceContext);
}
#endif

BOOLEAN
VioScsiHwInitialize(IN PVOID DeviceExtension);

BOOLEAN
VioScsiHwReinitialize(IN PVOID DeviceExtension);

BOOLEAN
VioScsiBuildIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb);

BOOLEAN
VioScsiStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb);

ULONG
VioScsiFindAdapter(IN PVOID DeviceExtension,
                   IN PVOID HwContext,
                   IN PVOID BusInformation,
                   IN PCHAR ArgumentString,
                   IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                   IN PBOOLEAN Again);

BOOLEAN
VioScsiResetBus(IN PVOID DeviceExtension, IN ULONG PathId);

SCSI_ADAPTER_CONTROL_STATUS
VioScsiAdapterControl(IN PVOID DeviceExtension, IN SCSI_ADAPTER_CONTROL_TYPE ControlType, IN PVOID Parameters);

SCSI_UNIT_CONTROL_STATUS
VioScsiUnitControl(IN PVOID DeviceExtension, IN SCSI_UNIT_CONTROL_TYPE ControlType, IN PVOID Parameters);

UCHAR
VioScsiProcessPnP(IN PVOID DeviceExtension, IN PSRB_TYPE Srb);

BOOLEAN
FORCEINLINE
PreProcessRequest(IN PVOID DeviceExtension, IN PSRB_TYPE Srb);

VOID FORCEINLINE PostProcessRequest(IN PVOID DeviceExtension, IN PSRB_TYPE Srb);

VOID FORCEINLINE DispatchQueue(IN PVOID DeviceExtension, IN ULONG MessageId);

BOOLEAN
VioScsiInterrupt(IN PVOID DeviceExtension);

VOID TransportReset(IN PVOID DeviceExtension, IN PVirtIOSCSIEvent evt);

VOID ParamChange(IN PVOID DeviceExtension, IN PVirtIOSCSIEvent evt);

BOOLEAN
VioScsiMSInterrupt(IN PVOID DeviceExtension, IN ULONG MessageID);

VOID VioScsiWmiInitialize(IN PVOID DeviceExtension);

VOID VioScsiWmiSrb(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb);

VOID VioScsiIoControl(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb);

BOOLEAN
VioScsiQueryWmiDataBlock(IN PVOID Context,
                         IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
                         IN ULONG GuidIndex,
                         IN ULONG InstanceIndex,
                         IN ULONG InstanceCount,
                         IN OUT PULONG InstanceLengthArray,
                         IN ULONG OutBufferSize,
                         OUT PUCHAR Buffer);

UCHAR
VioScsiExecuteWmiMethod(IN PVOID Context,
                        IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
                        IN ULONG GuidIndex,
                        IN ULONG InstanceIndex,
                        IN ULONG MethodId,
                        IN ULONG InBufferSize,
                        IN ULONG OutBufferSize,
                        IN OUT PUCHAR Buffer);

UCHAR
VioScsiQueryWmiRegInfo(IN PVOID Context, IN PSCSIWMI_REQUEST_CONTEXT RequestContext, OUT PWCHAR *MofResourceName);

VOID VioScsiReadExtendedData(IN PVOID Context, OUT PUCHAR Buffer);

VOID VioScsiSaveInquiryData(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb);

VOID VioScsiPatchInquiryData(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb);

GUID VioScsiWmiExtendedInfoGuid = VioScsiWmi_ExtendedInfo_Guid;
GUID VioScsiWmiAdapterInformationQueryGuid = MS_SM_AdapterInformationQueryGuid;
GUID VioScsiWmiPortInformationMethodsGuid = MS_SM_PortInformationMethodsGuid;

// clang-format off
SCSIWMIGUIDREGINFO VioScsiGuidList[] =
{
   { &VioScsiWmiExtendedInfoGuid,            1, 0 },
   { &VioScsiWmiAdapterInformationQueryGuid, 1, 0 },
   { &VioScsiWmiPortInformationMethodsGuid,  1, 0 },
};
// clang-format on

#define VioScsiGuidCount (sizeof(VioScsiGuidList) / sizeof(SCSIWMIGUIDREGINFO))

void CopyUnicodeString(void *_pDest, const void *_pSrc, size_t _maxlength)
{
    PUSHORT _pDestTemp = _pDest;
    USHORT _length = _maxlength - sizeof(USHORT);
    *_pDestTemp++ = _length;
    _length = (USHORT)min(wcslen(_pSrc) * sizeof(WCHAR), _length);
    memcpy(_pDestTemp, _pSrc, _length);
}

void CopyAnsiToUnicodeString(void *_pDest, const void *_pSrc, size_t _maxlength)
{
    PUSHORT _pDestTemp = _pDest;
    PWCHAR dst;
    PCHAR src = (PCHAR)_pSrc;
    USHORT _length = _maxlength - sizeof(USHORT);
    *_pDestTemp++ = _length;
    dst = (PWCHAR)_pDestTemp;
    _length = (USHORT)min(strlen((const char *)_pSrc) * sizeof(WCHAR), _length);
    _length /= sizeof(WCHAR);
    while (_length)
    {
        *dst++ = *src++;
        --_length;
    };
}

USHORT CopyBufferToAnsiString(void *_pDest, const void *_pSrc, const char delimiter, size_t _maxlength)
{
    PCHAR dst = (PCHAR)_pDest;
    PCHAR src = (PCHAR)_pSrc;
    USHORT _length = _maxlength;

    while (_length && (*src != delimiter))
    {
        *dst++ = *src++;
        --_length;
    };
    *dst = '\0';
    return _length;
}

BOOLEAN VioScsiReadRegistryParameter(IN PVOID DeviceExtension, IN PUCHAR ValueName, IN LONG offset)
{
    BOOLEAN Ret = FALSE;
    ULONG Len = sizeof(ULONG);
    UCHAR *pBuf = NULL;
    PADAPTER_EXTENSION adaptExt;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    pBuf = StorPortAllocateRegistryBuffer(DeviceExtension, &Len);
    if (pBuf == NULL)
    {
        RhelDbgPrint(TRACE_LEVEL_FATAL, "StorPortAllocateRegistryBuffer failed to allocate buffer\n");
        return FALSE;
    }

    memset(pBuf, 0, sizeof(ULONG));

    Ret = StorPortRegistryRead(DeviceExtension, ValueName, 1, MINIPORT_REG_DWORD, pBuf, &Len);

    if ((Ret == FALSE) || (Len == 0))
    {
        RhelDbgPrint(TRACE_LEVEL_FATAL, "StorPortRegistryRead returned 0x%x, Len = %d\n", Ret, Len);
        StorPortFreeRegistryBuffer(DeviceExtension, pBuf);
        return FALSE;
    }

    StorPortCopyMemory((PVOID)((UINT_PTR)adaptExt + offset), (PVOID)pBuf, sizeof(ULONG));

    StorPortFreeRegistryBuffer(DeviceExtension, pBuf);

    return TRUE;
}

ULONG
DriverEntry(IN PVOID DriverObject, IN PVOID RegistryPath)
{

    HW_INITIALIZATION_DATA hwInitData;
    ULONG initResult;

#ifdef EVENT_TRACING
    STORAGE_TRACE_INIT_INFO initInfo;
#endif

    InitializeDebugPrints((PDRIVER_OBJECT)DriverObject, (PUNICODE_STRING)RegistryPath);

    IsCrashDumpMode = FALSE;
    RhelDbgPrint(TRACE_LEVEL_FATAL, " Vioscsi driver started...built on %s %s\n", __DATE__, __TIME__);
    if (RegistryPath == NULL)
    {
        IsCrashDumpMode = TRUE;
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Crash dump mode\n");
    }

    RtlZeroMemory(&hwInitData, sizeof(HW_INITIALIZATION_DATA));

    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

    hwInitData.HwFindAdapter = VioScsiFindAdapter;
    hwInitData.HwInitialize = VioScsiHwInitialize;
    hwInitData.HwStartIo = VioScsiStartIo;
    hwInitData.HwInterrupt = VioScsiInterrupt;
    hwInitData.HwResetBus = VioScsiResetBus;
    hwInitData.HwAdapterControl = VioScsiAdapterControl;
    hwInitData.HwUnitControl = VioScsiUnitControl;
    hwInitData.HwBuildIo = VioScsiBuildIo;

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

    hwInitData.SrbTypeFlags = SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK;
    hwInitData.AddressTypeFlags = ADDRESS_TYPE_FLAG_BTL8;

    initResult = StorPortInitialize(DriverObject, RegistryPath, &hwInitData, NULL);

#ifdef EVENT_TRACING
    TraceContext = NULL;

    memset(&initInfo, 0, sizeof(STORAGE_TRACE_INIT_INFO));
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

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " Initialize returned 0x%x\n", initResult);

    return initResult;
}

ULONG
VioScsiFindAdapter(IN PVOID DeviceExtension,
                   IN PVOID HwContext,
                   IN PVOID BusInformation,
                   IN PCHAR ArgumentString,
                   IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                   IN PBOOLEAN Again)
{
    PADAPTER_EXTENSION adaptExt;
    PVOID uncachedExtensionVa;
    USHORT queueLength = 0;
    ULONG Size;
    ULONG HeapSize;
    ULONG extensionSize;
    ULONG index;
    ULONG num_cpus;
    ULONG max_cpus;
    ULONG max_queues;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);
    UNREFERENCED_PARAMETER(Again);

    ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    RtlZeroMemory(adaptExt, sizeof(ADAPTER_EXTENSION));

    adaptExt->dump_mode = IsCrashDumpMode;
    adaptExt->last_srb_id = 1;
    adaptExt->hba_id = HBA_ID;
    ConfigInfo->Master = TRUE;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->DmaWidth = Width32Bits;
    ConfigInfo->Dma32BitAddresses = TRUE;
    ConfigInfo->Dma64BitAddresses = SCSI_DMA64_MINIPORT_FULL64BIT_SUPPORTED;
    ConfigInfo->WmiDataProvider = TRUE;
    ConfigInfo->AlignmentMask = 0x3;
    ConfigInfo->MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->HwMSInterruptRoutine = VioScsiMSInterrupt;
    ConfigInfo->InterruptSynchronizationMode = InterruptSynchronizePerMessage;

    VioScsiWmiInitialize(DeviceExtension);

    if (!InitHW(DeviceExtension, ConfigInfo))
    {
        RhelDbgPrint(TRACE_LEVEL_FATAL, " Cannot initialize HardWare\n");
        return SP_RETURN_NOT_FOUND;
    }

    /* Initialize the following variables to temporary values to keep
     * Static Driver Verification happy. These values will be immediately
     * reconfigured within GetScsiConfig below, which will retrieve the runtime
     * values advertised by the underlying device.
     */
    adaptExt->scsi_config.num_queues = 1;
    adaptExt->scsi_config.seg_max = SCSI_MINIMUM_PHYSICAL_BREAKS;
    adaptExt->indirect = FALSE;
    adaptExt->max_physical_breaks = SCSI_MINIMUM_PHYSICAL_BREAKS;
    GetScsiConfig(DeviceExtension);
    SetGuestFeatures(DeviceExtension);

    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->MaximumNumberOfTargets = min((UCHAR)adaptExt->scsi_config.max_target,
                                             255 /*SCSI_MAXIMUM_TARGETS_PER_BUS*/);
    ConfigInfo->MaximumNumberOfLogicalUnits = min((UCHAR)adaptExt->scsi_config.max_lun, SCSI_MAXIMUM_LUNS_PER_TARGET);
    ConfigInfo->MaximumTransferLength = SP_UNINITIALIZED_VALUE;  // Unlimited
    ConfigInfo->NumberOfPhysicalBreaks = SP_UNINITIALIZED_VALUE; // Unlimited

    if (!adaptExt->dump_mode)
    {
        adaptExt->max_physical_breaks = adaptExt->indirect ? MAX_PHYS_SEGMENTS : PHYS_SEGMENTS;

        /* Allow user to override max_physical_breaks via reg key
         * [HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\vioscsi\Parameters\Device]
         * "PhysicalBreaks"={dword value here}
         */
        VioScsiReadRegistryParameter(DeviceExtension,
                                     REGISTRY_MAX_PH_BREAKS,
                                     FIELD_OFFSET(ADAPTER_EXTENSION, max_physical_breaks));
        adaptExt->max_physical_breaks = min(max(SCSI_MINIMUM_PHYSICAL_BREAKS, adaptExt->max_physical_breaks),
                                            MAX_PHYS_SEGMENTS);

        if (adaptExt->scsi_config.max_sectors > 0 && adaptExt->scsi_config.max_sectors != 0xFFFF &&
            adaptExt->max_physical_breaks * PAGE_SIZE > adaptExt->scsi_config.max_sectors * SECTOR_SIZE)
        {
            adaptExt->max_physical_breaks = adaptExt->scsi_config.max_sectors * SECTOR_SIZE / PAGE_SIZE;
        }
    }
    ConfigInfo->NumberOfPhysicalBreaks = adaptExt->max_physical_breaks + 1;
    ConfigInfo->MaximumTransferLength = adaptExt->max_physical_breaks * PAGE_SIZE;

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " NumberOfPhysicalBreaks %d\n", ConfigInfo->NumberOfPhysicalBreaks);
    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MaximumTransferLength %d\n", ConfigInfo->MaximumTransferLength);

    num_cpus = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    max_cpus = KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
    /* Set num_cpus and max_cpus to some sane values, to keep "Static Driver Verification" happy */
    num_cpus = max(1, num_cpus);
    max_cpus = max(1, max_cpus);

    adaptExt->num_queues = adaptExt->scsi_config.num_queues;
    if (adaptExt->dump_mode || !adaptExt->msix_enabled)
    {
        adaptExt->num_queues = 1;
    }
    else
    {
        adaptExt->num_queues = min(adaptExt->num_queues, (USHORT)num_cpus);
    }

    adaptExt->action_on_reset = VioscsiResetCompleteRequests;
    VioScsiReadRegistryParameter(DeviceExtension,
                                 REGISTRY_ACTION_ON_RESET,
                                 FIELD_OFFSET(ADAPTER_EXTENSION, action_on_reset));

    adaptExt->resp_time = 0;
    VioScsiReadRegistryParameter(DeviceExtension, REGISTRY_RESP_TIME_LIMIT, FIELD_OFFSET(ADAPTER_EXTENSION, resp_time));

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Queues %d CPUs %d\n", adaptExt->num_queues, num_cpus);

    /* Figure out the maximum number of queues we will ever need to set up. Note that this may
     * be higher than adaptExt->num_queues, because the driver may be reinitialized by calling
     * VioScsiFindAdapter again with more CPUs enabled. Unfortunately StorPortGetUncachedExtension
     * only allocates when called for the first time so we need to always use this upper bound.
     */
    if (adaptExt->dump_mode)
    {
        max_queues = adaptExt->num_queues;
    }
    else
    {
        max_queues = min(max_cpus, adaptExt->scsi_config.num_queues);
        if (adaptExt->num_queues > max_queues)
        {
            RhelDbgPrint(TRACE_LEVEL_WARNING, " Multiqueue can only use at most one queue per cpu.");
            adaptExt->num_queues = max_queues;
        }
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
    for (index = VIRTIO_SCSI_CONTROL_QUEUE; index < max_queues + VIRTIO_SCSI_REQUEST_QUEUE_0; ++index)
    {
        virtio_query_queue_allocation(&adaptExt->vdev, index, &queueLength, &Size, &HeapSize);
        if (Size == 0)
        {
            LogError(DeviceExtension, SP_INTERNAL_ADAPTER_ERROR, __LINE__);

            RhelDbgPrint(TRACE_LEVEL_FATAL, " Virtual queue %d config failed.\n", index);
            return SP_RETURN_ERROR;
        }
        adaptExt->pageAllocationSize += ROUND_TO_PAGES(Size);
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(HeapSize);
    }
    if (!adaptExt->dump_mode)
    {
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(SRB_EXTENSION));
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(VirtIOSCSIEventNode) * 8);
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(sizeof(STOR_DPC) * max_queues);
    }
    if (max_queues + VIRTIO_SCSI_REQUEST_QUEUE_0 > MAX_QUEUES_PER_DEVICE_DEFAULT)
    {
        adaptExt->poolAllocationSize += ROUND_TO_CACHE_LINES(((ULONGLONG)max_queues + VIRTIO_SCSI_REQUEST_QUEUE_0) *
                                                             virtio_get_queue_descriptor_size());
    }

    if (adaptExt->indirect)
    {
        adaptExt->queue_depth = queueLength;
    }
    else
    {
        adaptExt->queue_depth = queueLength / ConfigInfo->NumberOfPhysicalBreaks - 1;
    }
    ConfigInfo->MaxIOsPerLun = adaptExt->queue_depth * adaptExt->num_queues;
    ConfigInfo->InitialLunQueueDepth = ConfigInfo->MaxIOsPerLun;
    ConfigInfo->MaxNumberOfIO = ConfigInfo->MaxIOsPerLun;

    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " breaks_number = %x  queue_depth = %x\n",
                 ConfigInfo->NumberOfPhysicalBreaks,
                 adaptExt->queue_depth);

    extensionSize = PAGE_SIZE + adaptExt->pageAllocationSize + adaptExt->poolAllocationSize;
    uncachedExtensionVa = StorPortGetUncachedExtension(DeviceExtension, ConfigInfo, extensionSize);
    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " StorPortGetUncachedExtension uncachedExtensionVa = %p allocation size = %d\n",
                 uncachedExtensionVa,
                 extensionSize);
    if (!uncachedExtensionVa)
    {
        LogError(DeviceExtension, SP_INTERNAL_ADAPTER_ERROR, __LINE__);

        RhelDbgPrint(TRACE_LEVEL_FATAL, " Can't get uncached extension allocation size = %d\n", extensionSize);
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
    if (adaptExt->poolAllocationSize > 0)
    {
        adaptExt->poolAllocationVa = (PVOID)((ULONG_PTR)adaptExt->pageAllocationVa + adaptExt->pageAllocationSize);
    }
    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " Page-aligned area at %p, size = %d\n",
                 adaptExt->pageAllocationVa,
                 adaptExt->pageAllocationSize);
    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                 " Pool area at %p, size = %d\n",
                 adaptExt->poolAllocationVa,
                 adaptExt->poolAllocationSize);

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " pmsg_affinity = %p\n", adaptExt->pmsg_affinity);
    if (!adaptExt->dump_mode && (adaptExt->num_queues > 1) && (adaptExt->pmsg_affinity == NULL))
    {
        adaptExt->num_affinity = adaptExt->num_queues + 3;
        ULONG Status = StorPortAllocatePool(DeviceExtension,
                                            sizeof(GROUP_AFFINITY) * (ULONGLONG)adaptExt->num_affinity,
                                            VIOSCSI_POOL_TAG,
                                            (PVOID *)&adaptExt->pmsg_affinity);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " pmsg_affinity = %p Status = %lu\n", adaptExt->pmsg_affinity, Status);
    }
    adaptExt->fw_ver = '0';

    EXIT_FN();
    return SP_RETURN_FOUND;
}

BOOLEAN
VioScsiPassiveInitializeRoutine(IN PVOID DeviceExtension)
{
    ULONG index;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ENTER_FN();

    for (index = 0; index < adaptExt->num_queues; ++index)
    {
        StorPortInitializeDpc(DeviceExtension, &adaptExt->dpc[index], VioScsiCompleteDpcRoutine);
    }
    adaptExt->dpc_ok = TRUE;
    EXIT_FN();
    return TRUE;
}

static BOOLEAN InitializeVirtualQueues(PADAPTER_EXTENSION adaptExt, ULONG numQueues)
{
    NTSTATUS status;

    status = virtio_find_queues(&adaptExt->vdev, numQueues, adaptExt->vq);
    if (!NT_SUCCESS(status))
    {
        RhelDbgPrint(TRACE_LEVEL_FATAL, " FAILED with status 0x%x\n", status);
        return FALSE;
    }

    return TRUE;
}

PVOID
VioScsiPoolAlloc(IN PVOID DeviceExtension, IN SIZE_T size)
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
    RhelDbgPrint(TRACE_LEVEL_FATAL, " Out of memory %Id \n", size);
    return NULL;
}

BOOLEAN
VioScsiHwInitialize(IN PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONG i;
    ULONG index;

    PERF_CONFIGURATION_DATA perfData = {0};
    ULONG status = STOR_STATUS_SUCCESS;
    MESSAGE_INTERRUPT_INFORMATION msi_info = {0};
    PREQUEST_LIST element;
    ENTER_FN();

    adaptExt->msix_vectors = 0;
    adaptExt->pageOffset = 0;
    adaptExt->poolOffset = 0;

    while (StorPortGetMSIInfo(DeviceExtension, adaptExt->msix_vectors, &msi_info) == STOR_STATUS_SUCCESS)
    {
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageId = %x\n", msi_info.MessageId);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageData = %x\n", msi_info.MessageData);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " InterruptVector = %x\n", msi_info.InterruptVector);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " InterruptLevel = %x\n", msi_info.InterruptLevel);
        RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                     " InterruptMode = %s\n",
                     msi_info.InterruptMode == LevelSensitive ? "LevelSensitive" : "Latched");
        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " MessageAddress = %I64x\n\n", msi_info.MessageAddress.QuadPart);
        ++adaptExt->msix_vectors;
    }

    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Queues %d msix_vectors %d\n", adaptExt->num_queues, adaptExt->msix_vectors);
    if (adaptExt->num_queues > 1 && ((adaptExt->num_queues + 3) > adaptExt->msix_vectors))
    {
        adaptExt->num_queues = (USHORT)adaptExt->msix_vectors;
    }

    if (!adaptExt->dump_mode && adaptExt->msix_vectors > 0)
    {
        if (adaptExt->msix_vectors >= adaptExt->num_queues + 3)
        {
            /* initialize queues with a MSI vector per queue */
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Using a unique MSI vector per queue\n");
            adaptExt->msix_one_vector = FALSE;
        }
        else
        {
            /* if we don't have enough vectors, use one for all queues */
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Using one MSI vector for all queues\n");
            adaptExt->msix_one_vector = TRUE;
        }
        if (!InitializeVirtualQueues(adaptExt, adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0))
        {
            return FALSE;
        }
    }
    else
    {
        /* initialize queues with no MSI interrupts */
        adaptExt->msix_enabled = FALSE;
        if (!InitializeVirtualQueues(adaptExt, adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0))
        {
            return FALSE;
        }
    }

    for (index = 0; index < adaptExt->num_queues; ++index)
    {
        element = &adaptExt->processing_srbs[index];
        InitializeListHead(&element->srb_list);
        element->srb_cnt = 0;
    }

    if (!adaptExt->dump_mode)
    {
        /* we don't get another chance to call StorPortEnablePassiveInitialization and initialize
         * DPCs if the adapter is being restarted, so leave our datastructures alone on restart
         */
        if (adaptExt->dpc == NULL)
        {
            adaptExt->tmf_cmd.SrbExtension = (PSRB_EXTENSION)VioScsiPoolAlloc(DeviceExtension, sizeof(SRB_EXTENSION));
            adaptExt->events = (PVirtIOSCSIEventNode)VioScsiPoolAlloc(DeviceExtension, sizeof(VirtIOSCSIEventNode) * 8);
            adaptExt->dpc = (PSTOR_DPC)VioScsiPoolAlloc(DeviceExtension, sizeof(STOR_DPC) * adaptExt->num_queues);
        }
    }

    if (!adaptExt->dump_mode && CHECKBIT(adaptExt->features, VIRTIO_SCSI_F_HOTPLUG))
    {
        PVirtIOSCSIEventNode events = adaptExt->events;
        for (i = 0; i < 8; i++)
        {
            if (!KickEvent(DeviceExtension, (PVOID)(&events[i])))
            {
                RhelDbgPrint(TRACE_LEVEL_FATAL, " Cannot add event %d\n", i);
            }
        }
    }
    if (!adaptExt->dump_mode)
    {
        if ((adaptExt->num_queues > 1) && (adaptExt->perfFlags == 0))
        {
            perfData.Version = STOR_PERF_VERSION;
            perfData.Size = sizeof(PERF_CONFIGURATION_DATA);

            status = StorPortInitializePerfOpts(DeviceExtension, TRUE, &perfData);

            RhelDbgPrint(TRACE_LEVEL_FATAL,
                         " Current PerfOpts Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, "
                         "FirstRedirectionMessageNumber = %d,LastRedirectionMessageNumber = %d\n",
                         perfData.Version,
                         perfData.Flags,
                         perfData.ConcurrentChannels,
                         perfData.FirstRedirectionMessageNumber,
                         perfData.LastRedirectionMessageNumber);
            if ((status == STOR_STATUS_SUCCESS) && (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION)))
            {
                adaptExt->perfFlags = STOR_PERF_DPC_REDIRECTION;
                if (CHECKFLAG(perfData.Flags, STOR_PERF_INTERRUPT_MESSAGE_RANGES))
                {
                    adaptExt->perfFlags |= STOR_PERF_INTERRUPT_MESSAGE_RANGES;
                    perfData.FirstRedirectionMessageNumber = 3;
                    perfData.LastRedirectionMessageNumber = perfData.FirstRedirectionMessageNumber +
                                                            adaptExt->num_queues - 1;
                    ASSERT(perfData.LastRedirectionMessageNumber < adaptExt->num_affinity);
                    if ((adaptExt->pmsg_affinity != NULL) && CHECKFLAG(perfData.Flags, STOR_PERF_ADV_CONFIG_LOCALITY))
                    {
                        RtlZeroMemory((PCHAR)adaptExt->pmsg_affinity,
                                      sizeof(GROUP_AFFINITY) * ((ULONGLONG)adaptExt->num_queues + 3));
                        adaptExt->perfFlags |= STOR_PERF_ADV_CONFIG_LOCALITY;
                        perfData.MessageTargets = adaptExt->pmsg_affinity;
                        if (CHECKFLAG(perfData.Flags, STOR_PERF_CONCURRENT_CHANNELS))
                        {
                            adaptExt->perfFlags |= STOR_PERF_CONCURRENT_CHANNELS;
                            perfData.ConcurrentChannels = adaptExt->num_queues;
                        }
                    }
                }
                if (CHECKFLAG(perfData.Flags, STOR_PERF_DPC_REDIRECTION_CURRENT_CPU))
                {
                    //                    adaptExt->perfFlags |= STOR_PERF_DPC_REDIRECTION_CURRENT_CPU;
                }
                if (CHECKFLAG(perfData.Flags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO))
                {
                    //                    adaptExt->perfFlags |= STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO;
                }
                perfData.Flags = adaptExt->perfFlags;
                RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                             "Applied PerfOpts Version = 0x%x, Flags = 0x%x, ConcurrentChannels = %d, "
                             "FirstRedirectionMessageNumber = %d,LastRedirectionMessageNumber = %d\n",
                             perfData.Version,
                             perfData.Flags,
                             perfData.ConcurrentChannels,
                             perfData.FirstRedirectionMessageNumber,
                             perfData.LastRedirectionMessageNumber);
                status = StorPortInitializePerfOpts(DeviceExtension, FALSE, &perfData);
                if (status != STOR_STATUS_SUCCESS)
                {
                    adaptExt->perfFlags = 0;
                    RhelDbgPrint(TRACE_LEVEL_ERROR,
                                 " StorPortInitializePerfOpts set failed with status = 0x%x\n",
                                 status);
                }
            }
            else
            {
                RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                             " StorPortInitializePerfOpts get failed with status = 0x%x\n",
                             status);
            }
        }
        if (!adaptExt->dpc_ok && !StorPortEnablePassiveInitialization(DeviceExtension, VioScsiPassiveInitializeRoutine))
        {
            RhelDbgPrint(TRACE_LEVEL_FATAL, " StorPortEnablePassiveInitialization FAILED\n");
            return FALSE;
        }
    }

    virtio_device_ready(&adaptExt->vdev);
    EXIT_FN();
    return TRUE;
}

BOOLEAN
VioScsiHwReinitialize(IN PVOID DeviceExtension)
{
    /* The adapter is being restarted and we need to bring it back up without
     * running any passive-level code. Note that VioScsiFindAdapter is *not*
     * called on restart.
     */
    if (!InitVirtIODevice(DeviceExtension))
    {
        return FALSE;
    }
    SetGuestFeatures(DeviceExtension);
    return VioScsiHwInitialize(DeviceExtension);
}

BOOLEAN
VioScsiStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb)
{
    ENTER_FN_SRB();
    if (PreProcessRequest(DeviceExtension, (PSRB_TYPE)Srb))
    {
        CompleteRequest(DeviceExtension, (PSRB_TYPE)Srb);
    }
    else
    {
        PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
        PSRB_EXTENSION srbExt = SRB_EXTENSION(Srb);

        srbExt->id = adaptExt->last_srb_id;
        adaptExt->last_srb_id++;
        if (adaptExt->last_srb_id == 0 || (adaptExt->tmf_cmd.SrbExtension &&
                                           adaptExt->last_srb_id == (ULONG_PTR)&adaptExt->tmf_cmd.SrbExtension->cmd))
        {
            adaptExt->last_srb_id++;
        }

        SendSRB(DeviceExtension, (PSRB_TYPE)Srb);
    }
    EXIT_FN_SRB();
    return TRUE;
}

VOID HandleResponse(IN PVOID DeviceExtension, IN PVirtIOSCSICmd cmd)
{
    PSRB_TYPE Srb = (PSRB_TYPE)(cmd->srb);
    PSRB_EXTENSION srbExt = SRB_EXTENSION(Srb);
    VirtIOSCSICmdResp *resp = &cmd->resp.cmd;
    UCHAR senseInfoBufferLength = 0;
    PVOID senseInfoBuffer = NULL;
    UCHAR srbStatus = SRB_STATUS_SUCCESS;
    ULONG srbDataTransferLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    ENTER_FN();

    LOG_SRB_INFO();

    switch (resp->response)
    {
        case VIRTIO_SCSI_S_OK:
            SRB_SET_SCSI_STATUS(Srb, resp->status);
            srbStatus = (resp->status == SCSISTAT_GOOD) ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR;
            break;
        case VIRTIO_SCSI_S_UNDERRUN:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_SCSI_S_UNDERRUN\n");
            srbStatus = SRB_STATUS_DATA_OVERRUN;
            break;
        case VIRTIO_SCSI_S_ABORTED:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_SCSI_S_ABORTED\n");
            srbStatus = SRB_STATUS_ABORTED;
            break;
        case VIRTIO_SCSI_S_BAD_TARGET:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_SCSI_S_BAD_TARGET\n");
            srbStatus = SRB_STATUS_INVALID_TARGET_ID;
            break;
        case VIRTIO_SCSI_S_RESET:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_SCSI_S_RESET\n");
            srbStatus = SRB_STATUS_BUS_RESET;
            break;
        case VIRTIO_SCSI_S_BUSY:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_SCSI_S_BUSY\n");
            srbStatus = SRB_STATUS_BUSY;
            break;
        case VIRTIO_SCSI_S_TRANSPORT_FAILURE:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_SCSI_S_TRANSPORT_FAILURE\n");
            srbStatus = SRB_STATUS_ERROR;
            break;
        case VIRTIO_SCSI_S_TARGET_FAILURE:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_SCSI_S_TARGET_FAILURE\n");
            srbStatus = SRB_STATUS_ERROR;
            break;
        case VIRTIO_SCSI_S_NEXUS_FAILURE:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_SCSI_S_NEXUS_FAILURE\n");
            srbStatus = SRB_STATUS_ERROR;
            break;
        case VIRTIO_SCSI_S_FAILURE:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " VIRTIO_SCSI_S_FAILURE\n");
            srbStatus = SRB_STATUS_ERROR;
            break;
        default:
            srbStatus = SRB_STATUS_ERROR;
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Unknown response %d\n", resp->response);
            break;
    }
    if (srbStatus == SRB_STATUS_SUCCESS && resp->resid && srbDataTransferLen > resp->resid)
    {
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, srbDataTransferLen - resp->resid);
        srbStatus = SRB_STATUS_DATA_OVERRUN;
    }
    else if (srbStatus != SRB_STATUS_SUCCESS)
    {
        SRB_GET_SENSE_INFO(Srb, senseInfoBuffer, senseInfoBufferLength);
        if (senseInfoBufferLength >= FIELD_OFFSET(SENSE_DATA, CommandSpecificInformation))
        {
            RtlCopyMemory(senseInfoBuffer, resp->sense, min(resp->sense_len, senseInfoBufferLength));
            if (srbStatus == SRB_STATUS_ERROR)
            {
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

    EXIT_FN();
}

BOOLEAN
VioScsiInterrupt(IN PVOID DeviceExtension)
{
    PVirtIOSCSICmd cmd = NULL;
    PVirtIOSCSIEventNode evtNode = NULL;
    unsigned int len = 0;
    PADAPTER_EXTENSION adaptExt = NULL;
    BOOLEAN isInterruptServiced = FALSE;
    PSRB_TYPE Srb = NULL;
    ULONG intReason = 0;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (adaptExt->bRemoved)
    {
        return FALSE;
    }

    // NOTE : SDV banned function
    // RhelDbgPrint(TRACE_LEVEL_VERBOSE, " IRQL (%d)\n", KeGetCurrentIrql());

    intReason = virtio_read_isr_status(&adaptExt->vdev);

    if (intReason == 1 || adaptExt->dump_mode)
    {
        isInterruptServiced = TRUE;

        if (adaptExt->tmf_infly)
        {
            while ((cmd = (PVirtIOSCSICmd)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_CONTROL_QUEUE], &len)) != NULL)
            {
                VirtIOSCSICtrlTMFResp *resp;
                Srb = (PSRB_TYPE)cmd->srb;
                ASSERT(Srb == (PSRB_TYPE)&adaptExt->tmf_cmd.Srb);
                resp = &cmd->resp.tmf;
                switch (resp->response)
                {
                    case VIRTIO_SCSI_S_OK:
                    case VIRTIO_SCSI_S_FUNCTION_SUCCEEDED:
                        break;
                    default:
                        RhelDbgPrint(TRACE_LEVEL_ERROR, " unknown response %d\n", resp->response);
                        ASSERT(0);
                        break;
                }
                StorPortResume(DeviceExtension);
            }
            adaptExt->tmf_infly = FALSE;
        }
        while ((evtNode = (PVirtIOSCSIEventNode)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_EVENTS_QUEUE], &len)) !=
               NULL)
        {
            PVirtIOSCSIEvent evt = &evtNode->event;
            switch (evt->event)
            {
                case VIRTIO_SCSI_T_NO_EVENT:
                    break;
                case VIRTIO_SCSI_T_TRANSPORT_RESET:
                    TransportReset(DeviceExtension, evt);
                    break;
                case VIRTIO_SCSI_T_PARAM_CHANGE:
                    ParamChange(DeviceExtension, evt);
                    break;
                default:
                    RhelDbgPrint(TRACE_LEVEL_ERROR, " Unsupport virtio scsi event %x\n", evt->event);
                    break;
            }
            SynchronizedKickEventRoutine(DeviceExtension, evtNode);
        }

        if (!adaptExt->dump_mode && adaptExt->dpc_ok)
        {
            StorPortIssueDpc(DeviceExtension,
                             &adaptExt->dpc[0],
                             ULongToPtr(QUEUE_TO_MESSAGE(VIRTIO_SCSI_REQUEST_QUEUE_0)),
                             ULongToPtr(QUEUE_TO_MESSAGE(VIRTIO_SCSI_REQUEST_QUEUE_0)));
        }
        else
        {
            ProcessBuffer(DeviceExtension, QUEUE_TO_MESSAGE(VIRTIO_SCSI_REQUEST_QUEUE_0), InterruptLock);
        }
    }

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " isInterruptServiced = %d\n", isInterruptServiced);
    return isInterruptServiced;
}

static BOOLEAN VioScsiMSInterruptWorker(IN PVOID DeviceExtension, IN ULONG MessageID)
{
    PVirtIOSCSICmd cmd;
    PVirtIOSCSIEventNode evtNode;
    unsigned int len;
    PADAPTER_EXTENSION adaptExt;
    PSRB_TYPE Srb = NULL;
    ULONG intReason = 0;

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    RhelDbgPrint(TRACE_LEVEL_VERBOSE, " MessageID 0x%x\n", MessageID);

    if (MessageID >= QUEUE_TO_MESSAGE(VIRTIO_SCSI_REQUEST_QUEUE_0))
    {
        DispatchQueue(DeviceExtension, MessageID);
        return TRUE;
    }
    if (MessageID == 0)
    {
        return TRUE;
    }
    if (MessageID == QUEUE_TO_MESSAGE(VIRTIO_SCSI_CONTROL_QUEUE))
    {
        if (adaptExt->tmf_infly)
        {
            while ((cmd = (PVirtIOSCSICmd)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_CONTROL_QUEUE], &len)) != NULL)
            {
                VirtIOSCSICtrlTMFResp *resp;
                Srb = (PSRB_TYPE)(cmd->srb);
                ASSERT(Srb == (PSRB_TYPE)&adaptExt->tmf_cmd.Srb);
                resp = &cmd->resp.tmf;
                switch (resp->response)
                {
                    case VIRTIO_SCSI_S_OK:
                    case VIRTIO_SCSI_S_FUNCTION_SUCCEEDED:
                        break;
                    default:
                        RhelDbgPrint(TRACE_LEVEL_ERROR, " Unknown response %d\n", resp->response);
                        ASSERT(0);
                        break;
                }
                StorPortResume(DeviceExtension);
            }
            adaptExt->tmf_infly = FALSE;
        }
        return TRUE;
    }
    if (MessageID == QUEUE_TO_MESSAGE(VIRTIO_SCSI_EVENTS_QUEUE))
    {
        while ((evtNode = (PVirtIOSCSIEventNode)virtqueue_get_buf(adaptExt->vq[VIRTIO_SCSI_EVENTS_QUEUE], &len)) !=
               NULL)
        {
            PVirtIOSCSIEvent evt = &evtNode->event;
            switch (evt->event)
            {
                case VIRTIO_SCSI_T_NO_EVENT:
                    break;
                case VIRTIO_SCSI_T_TRANSPORT_RESET:
                    TransportReset(DeviceExtension, evt);
                    break;
                case VIRTIO_SCSI_T_PARAM_CHANGE:
                    ParamChange(DeviceExtension, evt);
                    break;
                default:
                    RhelDbgPrint(TRACE_LEVEL_ERROR, " Unsupport virtio scsi event %x\n", evt->event);
                    break;
            }
            SynchronizedKickEventRoutine(DeviceExtension, evtNode);
        }
        return TRUE;
    }
    return FALSE;
}

BOOLEAN
VioScsiMSInterrupt(IN PVOID DeviceExtension, IN ULONG MessageID)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    BOOLEAN isInterruptServiced = FALSE;
    ULONG i;

    if (adaptExt->bRemoved)
    {
        return FALSE;
    }

    if (!adaptExt->msix_one_vector)
    {
        /* Each queue has its own vector, this is the fast and common case */
        return VioScsiMSInterruptWorker(DeviceExtension, MessageID);
    }

    /* Fall back to checking all queues */
    for (i = 0; i < adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0; i++)
    {
        if (virtqueue_has_buf(adaptExt->vq[i]))
        {
            isInterruptServiced |= VioScsiMSInterruptWorker(DeviceExtension, i + 1);
        }
    }
    return isInterruptServiced;
}

BOOLEAN
VioScsiResetBus(IN PVOID DeviceExtension, IN ULONG PathId)
{
    UNREFERENCED_PARAMETER(PathId);

    return DeviceReset(DeviceExtension);
}

SCSI_ADAPTER_CONTROL_STATUS
VioScsiAdapterControl(IN PVOID DeviceExtension, IN SCSI_ADAPTER_CONTROL_TYPE ControlType, IN PVOID Parameters)
{
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST ControlTypeList;
    ULONG AdjustedMaxControlType;
    ULONG Index;
    PADAPTER_EXTENSION adaptExt;
    SCSI_ADAPTER_CONTROL_STATUS status = ScsiAdapterControlUnsuccessful;
    BOOLEAN SupportedControlTypes[ScsiAdapterControlMax] = {FALSE};

    ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    SupportedControlTypes[ScsiQuerySupportedControlTypes] = TRUE;
    SupportedControlTypes[ScsiStopAdapter] = TRUE;
    SupportedControlTypes[ScsiRestartAdapter] = TRUE;
    SupportedControlTypes[ScsiAdapterSurpriseRemoval] = TRUE;

    switch (ControlType)
    {

        case ScsiQuerySupportedControlTypes:
            {
                RhelDbgPrint(TRACE_LEVEL_VERBOSE, " ScsiQuerySupportedControlTypes\n");
                ControlTypeList = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
                AdjustedMaxControlType = (ControlTypeList->MaxControlType < ScsiAdapterControlMax) ? ControlTypeList->MaxControlType
                                                                                                   : ScsiAdapterControlMax;
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
                ShutDown(DeviceExtension);
                if (adaptExt->pmsg_affinity != NULL)
                {
                    StorPortFreePool(DeviceExtension, (PVOID)adaptExt->pmsg_affinity);
                    adaptExt->pmsg_affinity = NULL;
                }
                adaptExt->perfFlags = 0;
                status = ScsiAdapterControlSuccess;
                break;
            }
        case ScsiRestartAdapter:
            {
                RhelDbgPrint(TRACE_LEVEL_FATAL, " ScsiRestartAdapter\n");
                ShutDown(DeviceExtension);
                if (!VioScsiHwReinitialize(DeviceExtension))
                {
                    RhelDbgPrint(TRACE_LEVEL_FATAL, " Cannot reinitialize HW\n");
                    break;
                }
                status = ScsiAdapterControlSuccess;
                break;
            }
        case ScsiAdapterSurpriseRemoval:
            {
                RhelDbgPrint(TRACE_LEVEL_FATAL, " ScsiAdapterSurpriseRemoval\n");
                adaptExt->bRemoved = TRUE;
                status = ScsiAdapterControlSuccess;
                break;
            }
        default:
            RhelDbgPrint(TRACE_LEVEL_ERROR, " Unsupported ControlType %d\n", ControlType);
            break;
    }

    EXIT_FN();
    return status;
}

SCSI_UNIT_CONTROL_STATUS
VioScsiUnitControl(IN PVOID DeviceExtension, IN SCSI_UNIT_CONTROL_TYPE ControlType, IN PVOID Parameters)
{
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST ControlTypeList;
    ULONG AdjustedMaxControlType;
    ULONG index;
    PADAPTER_EXTENSION adaptExt;
    SCSI_UNIT_CONTROL_STATUS Status = ScsiUnitControlUnsuccessful;
    BOOLEAN SupportedControlTypes[ScsiUnitControlMax] = {FALSE};

    ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
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
            for (index = 0; index < AdjustedMaxControlType; index++)
            {
                ControlTypeList->SupportedTypeList[index] = SupportedControlTypes[index];
            }
            Status = ScsiUnitControlSuccess;
            break;
        case ScsiUnitStart:
            Status = ScsiUnitControlSuccess;
            break;
        case ScsiUnitRemove:
        case ScsiUnitSurpriseRemoval:
            ULONG vq_req_idx;
            PREQUEST_LIST element;
            STOR_LOCK_HANDLE LockHandle = {0};
            PVOID LockContext = NULL; // sanity check for LockMode = InterruptLock or StartIoLock
            PSTOR_ADDR_BTL8 stor_addr = (PSTOR_ADDR_BTL8)Parameters;

            for (vq_req_idx = 0; vq_req_idx < adaptExt->num_queues; vq_req_idx++)
            {
                element = &adaptExt->processing_srbs[vq_req_idx];
                LockContext = &adaptExt->dpc[vq_req_idx];
                StorPortAcquireSpinLock(DeviceExtension, DpcLock, LockContext, &LockHandle);
                if (!IsListEmpty(&element->srb_list))
                {
                    PLIST_ENTRY entry = element->srb_list.Flink;
                    while (entry != &element->srb_list)
                    {
                        PSRB_EXTENSION currSrbExt = CONTAINING_RECORD(entry, SRB_EXTENSION, list_entry);
                        PSCSI_REQUEST_BLOCK currSrb = currSrbExt->Srb;
                        if (SRB_PATH_ID(currSrb) == stor_addr->Path && SRB_TARGET_ID(currSrb) == stor_addr->Target &&
                            SRB_LUN(currSrb) == stor_addr->Lun)
                        {
                            SRB_SET_SRB_STATUS(currSrb, SRB_STATUS_NO_DEVICE);
                            CompleteRequest(DeviceExtension, (PSRB_TYPE)currSrb);
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                         " Complete pending I/Os on Path %d Target %d Lun %d \n",
                                         SRB_PATH_ID(currSrb),
                                         SRB_TARGET_ID(currSrb),
                                         SRB_LUN(currSrb));
                            element->srb_cnt--;
                        }
                        entry = entry->Flink;
                    }
                }
                StorPortReleaseSpinLock(DeviceExtension, &LockHandle);
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
VioScsiBuildIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb;
    ULONG i;
    ULONG fragLen;
    ULONG sgElement;
    ULONG sgMaxElements;
    PADAPTER_EXTENSION adaptExt;
    PSRB_EXTENSION srbExt;
    PSTOR_SCATTER_GATHER_LIST sgList;
    VirtIOSCSICmd *cmd;
    UCHAR TargetId;
    UCHAR Lun;

    ENTER_FN_SRB();
    cdb = SRB_CDB(Srb);
    srbExt = SRB_EXTENSION(Srb);
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    TargetId = SRB_TARGET_ID(Srb);
    Lun = SRB_LUN(Srb);

    if ((SRB_PATH_ID(Srb) > (UCHAR)adaptExt->num_queues) || (TargetId >= adaptExt->scsi_config.max_target) ||
        (Lun >= adaptExt->scsi_config.max_lun) || adaptExt->bRemoved)
    {
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_NO_DEVICE);
        StorPortNotification(RequestComplete, DeviceExtension, Srb);
        return FALSE;
    }

    LOG_SRB_INFO();

    RtlZeroMemory(srbExt, sizeof(*srbExt));
    srbExt->Srb = Srb;
    srbExt->psgl = srbExt->vio_sg;
    srbExt->pdesc = srbExt->desc_alias;

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
    if (cdb != NULL)
    {
        RtlCopyMemory(cmd->req.cmd.cdb, cdb, min(VIRTIO_SCSI_CDB_SIZE, SRB_CDB_LENGTH(Srb)));
    }

    sgElement = 0;
    srbExt->psgl[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->req.cmd, &fragLen);
    srbExt->psgl[sgElement].length = sizeof(cmd->req.cmd);
    sgElement++;

    sgList = StorPortGetScatterGatherList(DeviceExtension, Srb);
    if (sgList)
    {
        sgMaxElements = min((adaptExt->max_physical_breaks + 1), sgList->NumberOfElements);

        if ((SRB_FLAGS(Srb) & SRB_FLAGS_DATA_OUT) == SRB_FLAGS_DATA_OUT)
        {
            for (i = 0; i < sgMaxElements; i++, sgElement++)
            {
                srbExt->psgl[sgElement].physAddr = sgList->List[i].PhysicalAddress;
                srbExt->psgl[sgElement].length = sgList->List[i].Length;
                srbExt->Xfer += sgList->List[i].Length;
            }
        }
    }

    srbExt->out = sgElement;
    srbExt->psgl[sgElement].physAddr = StorPortGetPhysicalAddress(DeviceExtension, NULL, &cmd->resp.cmd, &fragLen);
    srbExt->psgl[sgElement].length = sizeof(cmd->resp.cmd);
    sgElement++;
    if (sgList)
    {
        sgMaxElements = min((adaptExt->max_physical_breaks + 1), sgList->NumberOfElements);

        if ((SRB_FLAGS(Srb) & SRB_FLAGS_DATA_OUT) != SRB_FLAGS_DATA_OUT)
        {
            for (i = 0; i < sgMaxElements; i++, sgElement++)
            {
                srbExt->psgl[sgElement].physAddr = sgList->List[i].PhysicalAddress;
                srbExt->psgl[sgElement].length = sgList->List[i].Length;
                srbExt->Xfer += sgList->List[i].Length;
            }
        }
    }
    srbExt->in = sgElement - srbExt->out;

    if (adaptExt->resp_time)
    {
        LARGE_INTEGER counter = {0};
        ULONG status = STOR_STATUS_SUCCESS;
        status = StorPortQueryPerformanceCounter(DeviceExtension, NULL, &counter);
        if (status == STOR_STATUS_SUCCESS)
        {
            srbExt->time = counter.QuadPart;
        }
        else
        {
            RhelDbgPrint(TRACE_LEVEL_ERROR,
                         "SRB 0x%p StorPortQueryPerformanceCounter failed with status  0x%lx\n",
                         Srb,
                         status);
        }
    }

    EXIT_FN_SRB();
    return TRUE;
}

VOID FORCEINLINE DispatchQueue(IN PVOID DeviceExtension, IN ULONG MessageId)
{
    PADAPTER_EXTENSION adaptExt;
    ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (!adaptExt->dump_mode && adaptExt->dpc_ok)
    {
        NT_ASSERT(MessageId >= QUEUE_TO_MESSAGE(VIRTIO_SCSI_REQUEST_QUEUE_0));
        StorPortIssueDpc(DeviceExtension,
                         &adaptExt->dpc[MessageId - QUEUE_TO_MESSAGE(VIRTIO_SCSI_REQUEST_QUEUE_0)],
                         ULongToPtr(MessageId),
                         ULongToPtr(MessageId));
        EXIT_FN();
        return;
    }
    ProcessBuffer(DeviceExtension, MessageId, InterruptLock);
    EXIT_FN();
}

VOID ProcessBuffer(IN PVOID DeviceExtension, IN ULONG MessageId, IN STOR_SPINLOCK LockMode)
{
    ULONG_PTR srbId;
    unsigned int len;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONG QueueNumber = MESSAGE_TO_QUEUE(MessageId);
    STOR_LOCK_HANDLE LockHandle = {0};
    struct virtqueue *vq;
    PSRB_EXTENSION srbExt = NULL;
    PREQUEST_LIST element;
    ULONG vq_req_idx;
    PVOID LockContext = NULL; // sanity check for LockMode = InterruptLock or StartIoLock

    ENTER_FN();

    if (QueueNumber >= (adaptExt->num_queues + VIRTIO_SCSI_REQUEST_QUEUE_0))
    {
        RhelDbgPrint(TRACE_LEVEL_VERBOSE,
                     " Modulo assignment required for QueueNumber as it exceeds the number of virtqueues available.\n");
        QueueNumber %= adaptExt->num_queues;
    }
    vq_req_idx = QueueNumber - VIRTIO_SCSI_REQUEST_QUEUE_0;
    element = &adaptExt->processing_srbs[vq_req_idx];

    vq = adaptExt->vq[QueueNumber];

    if (LockMode == DpcLock)
    {
        LockContext = &adaptExt->dpc[vq_req_idx];
    }
    StorPortAcquireSpinLock(DeviceExtension, LockMode, LockContext, &LockHandle);

    do
    {
        virtqueue_disable_cb(vq);
        while ((srbId = (ULONG_PTR)virtqueue_get_buf(vq, &len)) != 0)
        {
            PLIST_ENTRY le = NULL;
            BOOLEAN bFound = FALSE;

            for (le = element->srb_list.Flink; le != &element->srb_list && !bFound; le = le->Flink)
            {
                srbExt = CONTAINING_RECORD(le, SRB_EXTENSION, list_entry);
                if (srbExt->id == srbId)
                {
                    RemoveEntryList(le);
                    bFound = TRUE;
                    element->srb_cnt--;
                    break;
                }
            }

            if (!bFound)
            {
                RhelDbgPrint(TRACE_LEVEL_WARNING, " No SRB found for ID 0x%p\n", (void *)srbId);
            }

            if (bFound)
            {
                HandleResponse(DeviceExtension, &srbExt->cmd);
            }
        }
    } while (!virtqueue_enable_cb(vq));

    StorPortReleaseSpinLock(DeviceExtension, &LockHandle);

    EXIT_FN();
}

VOID VioScsiCompleteDpcRoutine(IN PSTOR_DPC Dpc, IN PVOID Context, IN PVOID SystemArgument1, IN PVOID SystemArgument2)
{
    ULONG MessageId;

    ENTER_FN();
    MessageId = PtrToUlong(SystemArgument1);
    ProcessBuffer(Context, MessageId, DpcLock);
    EXIT_FN();
}

VOID CompletePendingRequestsOnReset(IN PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt;
    ULONG QueueNumber;
    ULONG vq_req_idx;
    PREQUEST_LIST element;
    STOR_LOCK_HANDLE LockHandle = {0};
    PVOID LockContext = NULL; // sanity check for LockMode = InterruptLock or StartIoLock

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (!adaptExt->reset_in_progress)
    {
        adaptExt->reset_in_progress = TRUE;
        StorPortPause(DeviceExtension, 10);
        DeviceReset(DeviceExtension);

        for (vq_req_idx = 0; vq_req_idx < adaptExt->num_queues; vq_req_idx++)
        {
            element = &adaptExt->processing_srbs[vq_req_idx];
            RhelDbgPrint(TRACE_LEVEL_FATAL, " queue %d cnt %d\n", vq_req_idx, element->srb_cnt);
            LockContext = &adaptExt->dpc[vq_req_idx];
            StorPortAcquireSpinLock(DeviceExtension, DpcLock, LockContext, &LockHandle);
            while (!IsListEmpty(&element->srb_list))
            {
                PLIST_ENTRY entry = RemoveHeadList(&element->srb_list);
                if (entry)
                {
                    PSRB_EXTENSION currSrbExt = CONTAINING_RECORD(entry, SRB_EXTENSION, list_entry);
                    PSCSI_REQUEST_BLOCK currSrb = currSrbExt->Srb;
                    if (currSrb)
                    {
                        SRB_SET_SRB_STATUS(currSrb, SRB_STATUS_BUS_RESET);
                        CompleteRequest(DeviceExtension, (PSRB_TYPE)currSrb);
                        element->srb_cnt--;
                    }
                }
            }
            if (element->srb_cnt)
            {
                element->srb_cnt = 0;
            }
            StorPortReleaseSpinLock(DeviceExtension, &LockHandle);
        }
        StorPortResume(DeviceExtension);
    }
    else
    {
        RhelDbgPrint(TRACE_LEVEL_FATAL, " Reset is already in progress, doing nothing.\n");
    }
    adaptExt->reset_in_progress = FALSE;
}

UCHAR
VioScsiProcessPnP(IN PVOID DeviceExtension, IN PSRB_TYPE Srb)
{
    PADAPTER_EXTENSION adaptExt;
    PSCSI_PNP_REQUEST_BLOCK pnpBlock;
    ULONG SrbPnPFlags;
    ULONG PnPAction;
    UCHAR SrbStatus;

    ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    pnpBlock = (PSCSI_PNP_REQUEST_BLOCK)Srb;
    SrbStatus = SRB_STATUS_SUCCESS;
    SRB_GET_PNP_INFO(Srb, SrbPnPFlags, PnPAction);
    switch (PnPAction)
    {
        case StorQueryCapabilities:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                         " StorQueryCapabilities on %d::%d::%d\n",
                         SRB_PATH_ID(Srb),
                         SRB_TARGET_ID(Srb),
                         SRB_LUN(Srb));
            if (((SrbPnPFlags & SRB_PNP_FLAGS_ADAPTER_REQUEST) == 0) ||
                (SRB_DATA_TRANSFER_LENGTH(Srb) >= sizeof(STOR_DEVICE_CAPABILITIES)))
            {
                PSTOR_DEVICE_CAPABILITIES devCap = (PSTOR_DEVICE_CAPABILITIES)SRB_DATA_BUFFER(Srb);
                RtlZeroMemory(devCap, sizeof(*devCap));
                devCap->Removable = 1;
                devCap->SurpriseRemovalOK = 1;
            }
            break;
        case StorRemoveDevice:
        case StorSurpriseRemoval:
            RhelDbgPrint(TRACE_LEVEL_FATAL,
                         " Adapter Removal happens on %d::%d::%d\n",
                         SRB_PATH_ID(Srb),
                         SRB_TARGET_ID(Srb),
                         SRB_LUN(Srb));
            adaptExt->bRemoved = TRUE;
            break;
        default:
            RhelDbgPrint(TRACE_LEVEL_FATAL,
                         " Unsupported PnPAction SrbPnPFlags = %d, PnPAction = %d\n",
                         SrbPnPFlags,
                         PnPAction);
            SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }
    EXIT_FN();
    return SrbStatus;
}

BOOLEAN
FORCEINLINE
PreProcessRequest(IN PVOID DeviceExtension, IN PSRB_TYPE Srb)
{
    PADAPTER_EXTENSION adaptExt;

    ENTER_FN_SRB();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    switch (SRB_FUNCTION(Srb))
    {
        case SRB_FUNCTION_PNP:
            SRB_SET_SRB_STATUS(Srb, VioScsiProcessPnP(DeviceExtension, Srb));
            return TRUE;

        case SRB_FUNCTION_POWER:
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
            return TRUE;

        case SRB_FUNCTION_RESET_BUS:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_LOGICAL_UNIT:
            RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                         " <--> SRB_FUNCTION_RESET_LOGICAL_UNIT Target (%d::%d::%d), SRB 0x%p\n",
                         SRB_PATH_ID(Srb),
                         SRB_TARGET_ID(Srb),
                         SRB_LUN(Srb),
                         Srb);
            switch (adaptExt->action_on_reset)
            {
                case VioscsiResetCompleteRequests:
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Completing all pending SRBs\n");
                    CompletePendingRequestsOnReset(DeviceExtension);
                    SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
                    return TRUE;
                case VioscsiResetDoNothing:
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Doing nothing with all pending SRBs\n");
                    SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
                    return TRUE;
                case VioscsiResetBugCheck:
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION, " Let's bugcheck due to this reset event\n");
                    KeBugCheckEx(0xDEADDEAD, (ULONG_PTR)Srb, SRB_PATH_ID(Srb), SRB_TARGET_ID(Srb), SRB_LUN(Srb));
                    return TRUE;
            }
        case SRB_FUNCTION_WMI:
            VioScsiWmiSrb(DeviceExtension, Srb);
            return TRUE;
        case SRB_FUNCTION_IO_CONTROL:
            VioScsiIoControl(DeviceExtension, Srb);
            return TRUE;
    }
    EXIT_FN_SRB();
    return FALSE;
}

VOID PostProcessRequest(IN PVOID DeviceExtension, IN PSRB_TYPE Srb)
{
    PCDB cdb = NULL;
    PADAPTER_EXTENSION adaptExt = NULL;
    PSRB_EXTENSION srbExt = NULL;
    ENTER_FN_SRB();
    if (SRB_FUNCTION(Srb) != SRB_FUNCTION_EXECUTE_SCSI)
    {
        return;
    }
    cdb = SRB_CDB(Srb);
    if (!cdb)
    {
        return;
    }

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    switch (cdb->CDB6GENERIC.OperationCode)
    {
        case SCSIOP_READ_CAPACITY:
        case SCSIOP_READ_CAPACITY16:
            break;
        case SCSIOP_INQUIRY:
            VioScsiSaveInquiryData(DeviceExtension, Srb);
            VioScsiPatchInquiryData(DeviceExtension, Srb);
            if (!StorPortSetDeviceQueueDepth(DeviceExtension,
                                             SRB_PATH_ID(Srb),
                                             SRB_TARGET_ID(Srb),
                                             SRB_LUN(Srb),
                                             adaptExt->queue_depth))
            {
                RhelDbgPrint(TRACE_LEVEL_ERROR,
                             " StorPortSetDeviceQueueDepth(%p, %x) failed.\n",
                             DeviceExtension,
                             adaptExt->queue_depth);
            }
            break;
        default:
            break;
    }
    EXIT_FN_SRB();
}

VOID CompleteRequest(IN PVOID DeviceExtension, IN PSRB_TYPE Srb)
{
    PADAPTER_EXTENSION adaptExt = NULL;
    PSRB_EXTENSION srbExt = NULL;

    ENTER_FN_SRB();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PostProcessRequest(DeviceExtension, Srb);

    if (adaptExt->resp_time)
    {
        srbExt = SRB_EXTENSION(Srb);
        if (srbExt->time != 0)
        {
            LARGE_INTEGER counter = {0};
            LARGE_INTEGER freq = {0};
            ULONG status = StorPortQueryPerformanceCounter(DeviceExtension, &freq, &counter);

            if (status == STOR_STATUS_SUCCESS)
            {
                ULONGLONG time_msec = ((counter.QuadPart - srbExt->time) * 1000) / freq.QuadPart;
                RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                             "time_msec %I64d Start %llu End %llu Freq %llu\n",
                             time_msec,
                             srbExt->time,
                             counter.QuadPart,
                             freq.QuadPart);
                if (time_msec >= adaptExt->resp_time)
                {
                    PCDB cdb = SRB_CDB(Srb);
                    if (cdb)
                    { // Check for SDV compliance
                        UCHAR OpCode = cdb->CDB6GENERIC.OperationCode;
                        RhelDbgPrint(TRACE_LEVEL_WARNING,
                                     "Response Time SRB 0x%p : time %I64d (%lu) : length %d : OpCode 0x%x (%s)\n",
                                     Srb,
                                     time_msec,
                                     SRB_GET_TIMEOUTVALUE(Srb) * 1000,
                                     SRB_DATA_TRANSFER_LENGTH(Srb),
                                     OpCode,
                                     DbgGetScsiOpStr(OpCode));
                        DbgPrint("Response Time SRB 0x%p : time %I64d (%lu) : length %d : OpCode 0x%x (%s)\n",
                                 Srb,
                                 time_msec,
                                 SRB_GET_TIMEOUTVALUE(Srb) * 1000,
                                 SRB_DATA_TRANSFER_LENGTH(Srb),
                                 OpCode,
                                 DbgGetScsiOpStr(OpCode));
                    }
                }
            }
            else
            {
                RhelDbgPrint(TRACE_LEVEL_ERROR,
                             "SRB 0x%p StorPortQueryPerformanceCounter failed with status  0x%lx\n",
                             Srb,
                             status);
            }
        }
    }
    StorPortNotification(RequestComplete, DeviceExtension, Srb);
    EXIT_FN_SRB();
}

VOID LogError(IN PVOID DeviceExtension, IN ULONG ErrorCode, IN ULONG UniqueId)
{
    STOR_LOG_EVENT_DETAILS logEvent;
    ULONG sz = 0;
    RtlZeroMemory(&logEvent, sizeof(logEvent));
    logEvent.InterfaceRevision = STOR_CURRENT_LOG_INTERFACE_REVISION;
    logEvent.Size = sizeof(logEvent);
    logEvent.EventAssociation = StorEventAdapterAssociation;
    logEvent.StorportSpecificErrorCode = TRUE;
    logEvent.ErrorCode = ErrorCode;
    logEvent.DumpDataSize = sizeof(UniqueId);
    logEvent.DumpData = &UniqueId;
    StorPortLogSystemEvent(DeviceExtension, &logEvent, &sz);
}

VOID TransportReset(IN PVOID DeviceExtension, IN PVirtIOSCSIEvent evt)
{
    UCHAR TargetId = evt->lun[1];
    UCHAR Lun = (evt->lun[2] << 8) | evt->lun[3];
    ENTER_FN();

    switch (evt->reason)
    {
        case VIRTIO_SCSI_EVT_RESET_RESCAN:
            StorPortNotification(BusChangeDetected, DeviceExtension, 0);
            break;
        case VIRTIO_SCSI_EVT_RESET_REMOVED:
            StorPortNotification(BusChangeDetected, DeviceExtension, 0);
            break;
        default:
            RhelDbgPrint(TRACE_LEVEL_VERBOSE, " <--> Unsupport virtio scsi event reason 0x%x\n", evt->reason);
    }
    EXIT_FN();
}

VOID ParamChange(IN PVOID DeviceExtension, IN PVirtIOSCSIEvent evt)
{
    UCHAR TargetId = evt->lun[1];
    UCHAR Lun = (evt->lun[2] << 8) | evt->lun[3];
    UCHAR AdditionalSenseCode = (UCHAR)(evt->reason & 255);
    UCHAR AdditionalSenseCodeQualifier = (UCHAR)(evt->reason >> 8);
    ENTER_FN();

    if (AdditionalSenseCode == SCSI_ADSENSE_PARAMETERS_CHANGED &&
        (AdditionalSenseCodeQualifier == SPC3_SCSI_SENSEQ_PARAMETERS_CHANGED ||
         AdditionalSenseCodeQualifier == SPC3_SCSI_SENSEQ_MODE_PARAMETERS_CHANGED ||
         AdditionalSenseCodeQualifier == SPC3_SCSI_SENSEQ_CAPACITY_DATA_HAS_CHANGED))
    {
        StorPortNotification(BusChangeDetected, DeviceExtension, 0);
    }
    EXIT_FN();
}

VOID VioScsiWmiInitialize(IN PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt;
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

VOID VioScsiWmiSrb(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb)
{
    UCHAR status;
    SCSIWMI_REQUEST_CONTEXT requestContext = {0};
    ULONG retSize;
    PADAPTER_EXTENSION adaptExt;
    PSRB_WMI_DATA pSrbWmi = SRB_WMI_DATA(Srb);

    ENTER_FN_SRB();
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    ASSERT(SRB_FUNCTION(Srb) == SRB_FUNCTION_WMI);
    ASSERT(SRB_LENGTH(Srb) == sizeof(SCSI_WMI_REQUEST_BLOCK));
    ASSERT(SRB_DATA_TRANSFER_LENGTH(Srb) >= sizeof(ULONG));
    ASSERT(SRB_DATA_BUFFER(Srb));

    if (!pSrbWmi)
    {
        return;
    }
    if (!(pSrbWmi->WMIFlags & SRB_WMI_FLAGS_ADAPTER_REQUEST))
    {
        SRB_SET_DATA_TRANSFER_LENGTH(Srb, 0);
        SRB_SET_SRB_STATUS(Srb, SRB_STATUS_SUCCESS);
    }
    else
    {
        requestContext.UserContext = Srb;
        (VOID) ScsiPortWmiDispatchFunction(&adaptExt->WmiLibContext,
                                           pSrbWmi->WMISubFunction,
                                           DeviceExtension,
                                           &requestContext,
                                           pSrbWmi->DataPath,
                                           SRB_DATA_TRANSFER_LENGTH(Srb),
                                           SRB_DATA_BUFFER(Srb));

        retSize = ScsiPortWmiGetReturnSize(&requestContext);
        status = ScsiPortWmiGetReturnStatus(&requestContext);

        SRB_SET_DATA_TRANSFER_LENGTH(Srb, retSize);
        SRB_SET_SRB_STATUS(Srb, status);
    }

    EXIT_FN_SRB();
}

VOID VioScsiIoControl(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb)
{
    PSRB_IO_CONTROL srbControl;
    PVOID srbDataBuffer = SRB_DATA_BUFFER(Srb);
    PADAPTER_EXTENSION adaptExt;

    ENTER_FN_SRB();

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    srbControl = (PSRB_IO_CONTROL)srbDataBuffer;

    switch (srbControl->ControlCode)
    {
        case IOCTL_SCSI_MINIPORT_NOT_QUORUM_CAPABLE:
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_ERROR);
            RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                         " <--> Signature = %02x %02x %02x %02x %02x %02x %02x %02x\n",
                         srbControl->Signature[0],
                         srbControl->Signature[1],
                         srbControl->Signature[2],
                         srbControl->Signature[3],
                         srbControl->Signature[4],
                         srbControl->Signature[5],
                         srbControl->Signature[6],
                         srbControl->Signature[7]);
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " <--> IOCTL_SCSI_MINIPORT_NOT_QUORUM_CAPABLE\n");
            break;
        case IOCTL_SCSI_MINIPORT_FIRMWARE:
            FirmwareRequest(DeviceExtension, Srb);
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " <--> IOCTL_SCSI_MINIPORT_FIRMWARE\n");
            break;
        default:
            SRB_SET_SRB_STATUS(Srb, SRB_STATUS_INVALID_REQUEST);
            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " <--> Unsupport control code 0x%x\n", srbControl->ControlCode);
            break;
    }
    EXIT_FN_SRB();
}

UCHAR
ParseIdentificationDescr(IN PVOID DeviceExtension,
                         IN PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr,
                         IN UCHAR PageLength)
{
    PADAPTER_EXTENSION adaptExt;
    UCHAR CodeSet = 0;
    UCHAR IdentifierType = 0;
    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ENTER_FN();
    if (IdentificationDescr)
    {
        CodeSet = IdentificationDescr->CodeSet;               //(UCHAR)(((PCHAR)IdentificationDescr)[0]);
        IdentifierType = IdentificationDescr->IdentifierType; //(UCHAR)(((PCHAR)IdentificationDescr)[1]);
        switch (IdentifierType)
        {
            case VioscsiVpdIdentifierTypeVendorSpecific:
                {
                    if (CodeSet == VioscsiVpdCodeSetAscii)
                    {
                        if (IdentificationDescr->IdentifierLength > 0 && adaptExt->ser_num == NULL)
                        {
                            int ln = min(64, IdentificationDescr->IdentifierLength);
                            ULONG Status = StorPortAllocatePool(DeviceExtension,
                                                                ln + 1,
                                                                VIOSCSI_POOL_TAG,
                                                                (PVOID *)&adaptExt->ser_num);
                            if (NT_SUCCESS(Status))
                            {
                                StorPortMoveMemory(adaptExt->ser_num, IdentificationDescr->Identifier, ln);
                                adaptExt->ser_num[ln] = '\0';
                                RhelDbgPrint(TRACE_LEVEL_INFORMATION, " serial number %s\n", adaptExt->ser_num);
                            }
                        }
                    }
                }
                break;
            case VioscsiVpdIdentifierTypeFCPHName:
                {
                    if ((CodeSet == VioscsiVpdCodeSetBinary) &&
                        (IdentificationDescr->IdentifierLength == sizeof(ULONGLONG)))
                    {
                        REVERSE_BYTES_QUAD(&adaptExt->wwn, IdentificationDescr->Identifier);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " wwn %llu\n", (ULONGLONG)adaptExt->wwn);
                    }
                }
                break;
            case VioscsiVpdIdentifierTypeFCTargetPortPHName:
                {
                    if ((CodeSet == VioscsiVpdCodeSetSASBinary) &&
                        (IdentificationDescr->IdentifierLength == sizeof(ULONGLONG)))
                    {
                        REVERSE_BYTES_QUAD(&adaptExt->port_wwn, IdentificationDescr->Identifier);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " port wwn %llu\n", (ULONGLONG)adaptExt->port_wwn);
                    }
                }
                break;
            case VioscsiVpdIdentifierTypeFCTargetPortRelativeTargetPort:
                {
                    if ((CodeSet == VioscsiVpdCodeSetSASBinary) &&
                        (IdentificationDescr->IdentifierLength == sizeof(ULONG)))
                    {
                        REVERSE_BYTES(&adaptExt->port_idx, IdentificationDescr->Identifier);
                        RhelDbgPrint(TRACE_LEVEL_INFORMATION, " port index %lu\n", (ULONG)adaptExt->port_idx);
                    }
                }
                break;
            default:
                RhelDbgPrint(TRACE_LEVEL_ERROR, " Unsupported IdentifierType = %x!\n", IdentifierType);
                break;
        }
        return IdentificationDescr->IdentifierLength;
    }
    EXIT_FN();
    return 0;
}

VOID VioScsiSaveInquiryData(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb)
{
    PVOID dataBuffer;
    PADAPTER_EXTENSION adaptExt;
    PCDB cdb;
    ULONG dataLen;
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    ENTER_FN_SRB();

    if (!Srb)
    {
        return;
    }

    cdb = SRB_CDB(Srb);

    if (!cdb)
    {
        return;
    }

    SRB_GET_SCSI_STATUS(Srb, SrbStatus);
    if (SrbStatus == SRB_STATUS_ERROR)
    {
        return;
    }

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    dataBuffer = SRB_DATA_BUFFER(Srb);
    dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    if (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)
    {
        switch (cdb->CDB6INQUIRY3.PageCode)
        {
            case VPD_SERIAL_NUMBER:
                {
                    PVPD_SERIAL_NUMBER_PAGE SerialPage;
                    SerialPage = (PVPD_SERIAL_NUMBER_PAGE)dataBuffer;
                    RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                 " VPD_SERIAL_NUMBER PageLength = %d\n",
                                 SerialPage->PageLength);
                    if (SerialPage->PageLength > 0 && adaptExt->ser_num == NULL)
                    {
                        int ln = min(64, SerialPage->PageLength);
                        ULONG Status = StorPortAllocatePool(DeviceExtension,
                                                            ln + 1,
                                                            VIOSCSI_POOL_TAG,
                                                            (PVOID *)&adaptExt->ser_num);
                        if (NT_SUCCESS(Status))
                        {
                            StorPortMoveMemory(adaptExt->ser_num, SerialPage->SerialNumber, ln);
                            adaptExt->ser_num[ln] = '\0';
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " serial number %s\n", adaptExt->ser_num);
                        }
                    }
                }
                break;
            case VPD_DEVICE_IDENTIFIERS:
                {
                    PVPD_IDENTIFICATION_PAGE IdentificationPage;
                    PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr;
                    UCHAR PageLength = 0;
                    IdentificationPage = (PVPD_IDENTIFICATION_PAGE)dataBuffer;
                    PageLength = IdentificationPage->PageLength;
                    if (PageLength >= sizeof(VPD_IDENTIFICATION_DESCRIPTOR))
                    {
                        UCHAR IdentifierLength = 0;
                        IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)IdentificationPage->Descriptors;
                        do
                        {
                            UCHAR offset = 0;
                            IdentifierLength = ParseIdentificationDescr(DeviceExtension,
                                                                        IdentificationDescr,
                                                                        PageLength);
                            offset = sizeof(VPD_IDENTIFICATION_DESCRIPTOR) + IdentifierLength;
                            PageLength -= min(PageLength, offset);
                            IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)((ULONG_PTR)IdentificationDescr +
                                                                                   offset);
                        } while (PageLength);
                    }
                }
                break;
        }
    }
    else if (cdb->CDB6INQUIRY3.PageCode == VPD_SUPPORTED_PAGES)
    {
        PINQUIRYDATA InquiryData = (PINQUIRYDATA)dataBuffer;
        if (InquiryData && dataLen)
        {
            CopyBufferToAnsiString(adaptExt->ven_id, InquiryData->VendorId, ' ', sizeof(InquiryData->VendorId));
            CopyBufferToAnsiString(adaptExt->prod_id, InquiryData->ProductId, ' ', sizeof(InquiryData->ProductId));
            CopyBufferToAnsiString(adaptExt->rev_id,
                                   InquiryData->ProductRevisionLevel,
                                   ' ',
                                   sizeof(InquiryData->ProductRevisionLevel));
        }
    }
    EXIT_FN_SRB();
}

VOID VioScsiPatchInquiryData(IN PVOID DeviceExtension, IN OUT PSRB_TYPE Srb)
{
    PVOID dataBuffer;
    PADAPTER_EXTENSION adaptExt;
    PCDB cdb;
    ULONG dataLen;
    UCHAR SrbStatus = SRB_STATUS_SUCCESS;
    ENTER_FN_SRB();

    if (!Srb)
    {
        return;
    }

    cdb = SRB_CDB(Srb);

    if (!cdb)
    {
        return;
    }

    SRB_GET_SCSI_STATUS(Srb, SrbStatus);
    if (SrbStatus == SRB_STATUS_ERROR)
    {
        return;
    }

    adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    dataBuffer = SRB_DATA_BUFFER(Srb);
    dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);

    if (cdb->CDB6INQUIRY3.EnableVitalProductData == 1)
    {
        switch (cdb->CDB6INQUIRY3.PageCode)
        {
            case VPD_DEVICE_IDENTIFIERS:
                {
                    PVPD_IDENTIFICATION_PAGE IdentificationPage;
                    PVPD_IDENTIFICATION_DESCRIPTOR IdentificationDescr;
                    UCHAR PageLength = 0;
                    IdentificationPage = (PVPD_IDENTIFICATION_PAGE)dataBuffer;
                    PageLength = IdentificationPage->PageLength;
                    if (dataLen >= (sizeof(VPD_IDENTIFICATION_DESCRIPTOR) + sizeof(VPD_IDENTIFICATION_PAGE) + 8) &&
                        PageLength <= sizeof(VPD_IDENTIFICATION_PAGE))
                    {
                        UCHAR IdentifierLength = 0;
                        IdentificationDescr = (PVPD_IDENTIFICATION_DESCRIPTOR)IdentificationPage->Descriptors;
                        if (IdentificationDescr->IdentifierLength == 0)
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
                            IdentificationPage->PageLength = sizeof(VPD_IDENTIFICATION_DESCRIPTOR) +
                                                             IdentificationDescr->IdentifierLength;
                            SRB_SET_DATA_TRANSFER_LENGTH(Srb,
                                                         (sizeof(VPD_IDENTIFICATION_PAGE) +
                                                          IdentificationPage->PageLength));
                        }
                    }
                }
                break;
        }
    }
    EXIT_FN_SRB();
}

BOOLEAN
VioScsiQueryWmiDataBlock(IN PVOID Context,
                         IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
                         IN ULONG GuidIndex,
                         IN ULONG InstanceIndex,
                         IN ULONG InstanceCount,
                         IN OUT PULONG InstanceLengthArray,
                         IN ULONG OutBufferSize,
                         OUT PUCHAR Buffer)
{
    ULONG size = 0;
    UCHAR status = SRB_STATUS_SUCCESS;
    PADAPTER_EXTENSION adaptExt;

    ENTER_FN();
    adaptExt = (PADAPTER_EXTENSION)Context;

    UNREFERENCED_PARAMETER(InstanceIndex);

    switch (GuidIndex)
    {
        case VIOSCSI_SETUP_GUID_INDEX:
            {
                size = VioScsiExtendedInfo_SIZE;
                if (OutBufferSize < size)
                {
                    status = SRB_STATUS_DATA_OVERRUN;
                    break;
                }

                VioScsiReadExtendedData(Context, Buffer);
                *InstanceLengthArray = size;
                status = SRB_STATUS_SUCCESS;
            }
            break;
        case VIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX:
            {
                PMS_SM_AdapterInformationQuery pOutBfr = (PMS_SM_AdapterInformationQuery)Buffer;
                RhelDbgPrint(TRACE_LEVEL_FATAL, " --> VIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX\n");
                size = sizeof(MS_SM_AdapterInformationQuery);
                if (OutBufferSize < size)
                {
                    status = SRB_STATUS_DATA_OVERRUN;
                    break;
                }

                RtlZeroMemory(pOutBfr, size);
                pOutBfr->UniqueAdapterId = adaptExt->hba_id;
                pOutBfr->HBAStatus = HBA_STATUS_OK;
                pOutBfr->NumberOfPorts = 1;
                pOutBfr->VendorSpecificID = VENDORID | (PRODUCTID << 16);
                CopyUnicodeString(pOutBfr->Manufacturer, MANUFACTURER, sizeof(pOutBfr->Manufacturer));
                if (adaptExt->ser_num)
                {
                    CopyAnsiToUnicodeString(pOutBfr->SerialNumber, adaptExt->ser_num, sizeof(pOutBfr->SerialNumber));
                }
                else
                {
                    CopyUnicodeString(pOutBfr->SerialNumber, SERIALNUMBER, sizeof(pOutBfr->SerialNumber));
                }
                CopyUnicodeString(pOutBfr->Model, MODEL, sizeof(pOutBfr->Model));
                CopyUnicodeString(pOutBfr->ModelDescription, MODELDESCRIPTION, sizeof(pOutBfr->ModelDescription));
                CopyUnicodeString(pOutBfr->HardwareVersion, HARDWAREVERSION, sizeof(pOutBfr->ModelDescription));
                CopyUnicodeString(pOutBfr->DriverVersion, DRIVERVERSION, sizeof(pOutBfr->DriverVersion));
                CopyUnicodeString(pOutBfr->OptionROMVersion, OPTIONROMVERSION, sizeof(pOutBfr->OptionROMVersion));
                CopyAnsiToUnicodeString(pOutBfr->FirmwareVersion, adaptExt->rev_id, sizeof(pOutBfr->FirmwareVersion));
                CopyUnicodeString(pOutBfr->DriverName, DRIVERNAME, sizeof(pOutBfr->DriverName));
                CopyUnicodeString(pOutBfr->HBASymbolicName, HBASYMBOLICNAME, sizeof(pOutBfr->HBASymbolicName));
                CopyUnicodeString(pOutBfr->RedundantFirmwareVersion,
                                  REDUNDANTFIRMWAREVERSION,
                                  sizeof(pOutBfr->RedundantFirmwareVersion));
                CopyUnicodeString(pOutBfr->RedundantOptionROMVersion,
                                  REDUNDANTOPTIONROMVERSION,
                                  sizeof(pOutBfr->RedundantOptionROMVersion));
                CopyUnicodeString(pOutBfr->MfgDomain, MFRDOMAIN, sizeof(pOutBfr->MfgDomain));

                *InstanceLengthArray = size;
                status = SRB_STATUS_SUCCESS;
            }
            break;
        case VIOSCSI_MS_PORT_INFORM_GUID_INDEX:
            {
                size = sizeof(ULONG);
                if (OutBufferSize < size)
                {
                    status = SRB_STATUS_DATA_OVERRUN;
                    RhelDbgPrint(TRACE_LEVEL_WARNING,
                                 " --> VIOSCSI_MS_PORT_INFORM_GUID_INDEX out buffer too small %d %d\n",
                                 OutBufferSize,
                                 size);
                    break;
                }
                *InstanceLengthArray = size;
                status = SRB_STATUS_SUCCESS;
            }
            break;
        default:
            {
                status = SRB_STATUS_ERROR;
            }
    }

    ScsiPortWmiPostProcess(RequestContext, status, size);

    EXIT_FN();
    return TRUE;
}

UCHAR
VioScsiExecuteWmiMethod(IN PVOID Context,
                        IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
                        IN ULONG GuidIndex,
                        IN ULONG InstanceIndex,
                        IN ULONG MethodId,
                        IN ULONG InBufferSize,
                        IN ULONG OutBufferSize,
                        IN OUT PUCHAR Buffer)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)Context;
    ULONG size = 0;
    UCHAR status = SRB_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(InstanceIndex);

    ENTER_FN();
    switch (GuidIndex)
    {
        case VIOSCSI_SETUP_GUID_INDEX:
            {
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, " --> VIOSCSI_SETUP_GUID_INDEX ERROR\n");
            }
            break;
        case VIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX:
            {
                PMS_SM_AdapterInformationQuery pOutBfr = (PMS_SM_AdapterInformationQuery)Buffer;
                pOutBfr;
                RhelDbgPrint(TRACE_LEVEL_INFORMATION, " --> VIOSCSI_MS_ADAPTER_INFORM_GUID_INDEX ERROR\n");
            }
            break;
        case VIOSCSI_MS_PORT_INFORM_GUID_INDEX:
            {
                switch (MethodId)
                {
                    case SM_GetPortType:
                        {
                            PSM_GetPortType_IN pInBfr = (PSM_GetPortType_IN)Buffer;
                            PSM_GetPortType_OUT pOutBfr = (PSM_GetPortType_OUT)Buffer;
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " --> SM_GetPortType\n");
                            size = SM_GetPortType_OUT_SIZE;
                            if (OutBufferSize < size)
                            {
                                status = SRB_STATUS_DATA_OVERRUN;
                                break;
                            }
                            if (InBufferSize < SM_GetPortType_IN_SIZE)
                            {
                                status = SRB_STATUS_ERROR;
                                break;
                            }
                            pOutBfr->HBAStatus = HBA_STATUS_OK;
                            pOutBfr->PortType = HBA_PORTTYPE_SASDEVICE;
                        }
                        break;
                    case SM_GetAdapterPortAttributes:
                        {
                            PSM_GetAdapterPortAttributes_IN pInBfr = (PSM_GetAdapterPortAttributes_IN)Buffer;
                            PSM_GetAdapterPortAttributes_OUT pOutBfr = (PSM_GetAdapterPortAttributes_OUT)Buffer;
                            PMS_SMHBA_FC_Port pPortSpecificAttributes = NULL;
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " --> SM_GetAdapterPortAttributes\n");
                            size = FIELD_OFFSET(SM_GetAdapterPortAttributes_OUT, PortAttributes) +
                                   FIELD_OFFSET(MS_SMHBA_PORTATTRIBUTES, PortSpecificAttributes) +
                                   sizeof(MS_SMHBA_FC_Port);
                            if (OutBufferSize < size)
                            {
                                status = SRB_STATUS_DATA_OVERRUN;
                                break;
                            }
                            if (InBufferSize < SM_GetAdapterPortAttributes_IN_SIZE)
                            {
                                status = SRB_STATUS_ERROR;
                                break;
                            }
                            pOutBfr->HBAStatus = HBA_STATUS_OK;
                            CopyUnicodeString(pOutBfr->PortAttributes.OSDeviceName,
                                              MODEL,
                                              sizeof(pOutBfr->PortAttributes.OSDeviceName));
                            pOutBfr->PortAttributes.PortState = HBA_PORTSTATE_ONLINE;
                            pOutBfr->PortAttributes.PortType = HBA_PORTTYPE_SASDEVICE;
                            pOutBfr->PortAttributes.PortSpecificAttributesSize = sizeof(MS_SMHBA_FC_Port);
                            pPortSpecificAttributes = (PMS_SMHBA_FC_Port)pOutBfr->PortAttributes.PortSpecificAttributes;
                            RtlZeroMemory(pPortSpecificAttributes, sizeof(MS_SMHBA_FC_Port));
                            RtlMoveMemory(pPortSpecificAttributes->NodeWWN,
                                          &adaptExt->wwn,
                                          sizeof(pPortSpecificAttributes->NodeWWN));
                            RtlMoveMemory(pPortSpecificAttributes->PortWWN,
                                          &adaptExt->port_wwn,
                                          sizeof(pPortSpecificAttributes->PortWWN));
                            pPortSpecificAttributes->FcId = 0;
                            pPortSpecificAttributes->PortSupportedClassofService = 0;
                            // FIXME report PortSupportedFc4Types PortActiveFc4Types FabricName;
                            pPortSpecificAttributes->NumberofDiscoveredPorts = 1;
                            pPortSpecificAttributes->NumberofPhys = 1;
                            CopyUnicodeString(pPortSpecificAttributes->PortSymbolicName,
                                              PORTSYMBOLICNAME,
                                              sizeof(pPortSpecificAttributes->PortSymbolicName));
                        }
                        break;
                    case SM_GetDiscoveredPortAttributes:
                        {
                            PSM_GetDiscoveredPortAttributes_IN pInBfr = (PSM_GetDiscoveredPortAttributes_IN)Buffer;
                            PSM_GetDiscoveredPortAttributes_OUT pOutBfr = (PSM_GetDiscoveredPortAttributes_OUT)Buffer;
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " --> SM_GetDiscoveredPortAttributes\n");
                            size = SM_GetDiscoveredPortAttributes_OUT_SIZE;
                            if (OutBufferSize < size)
                            {
                                status = SRB_STATUS_DATA_OVERRUN;
                                break;
                            }
                            if (InBufferSize < SM_GetDiscoveredPortAttributes_IN_SIZE)
                            {
                                status = SRB_STATUS_ERROR;
                                break;
                            }
                            pOutBfr->HBAStatus = HBA_STATUS_OK;
                            CopyUnicodeString(pOutBfr->PortAttributes.OSDeviceName,
                                              MODEL,
                                              sizeof(pOutBfr->PortAttributes.OSDeviceName));
                            pOutBfr->PortAttributes.PortState = HBA_PORTSTATE_ONLINE;
                            pOutBfr->PortAttributes.PortType = HBA_PORTTYPE_SASDEVICE;
                        }
                        break;
                    case SM_GetPortAttributesByWWN:
                        {
                            PSM_GetPortAttributesByWWN_IN pInBfr = (PSM_GetPortAttributesByWWN_IN)Buffer;
                            PSM_GetPortAttributesByWWN_OUT pOutBfr = (PSM_GetPortAttributesByWWN_OUT)Buffer;
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " --> SM_GetPortAttributesByWWN\n");
                            size = SM_GetPortAttributesByWWN_OUT_SIZE;
                            if (OutBufferSize < size)
                            {
                                status = SRB_STATUS_DATA_OVERRUN;
                                break;
                            }
                            if (InBufferSize < SM_GetPortAttributesByWWN_IN_SIZE)
                            {
                                status = SRB_STATUS_ERROR;
                                break;
                            }
                            pOutBfr->HBAStatus = HBA_STATUS_OK;
                            CopyUnicodeString(pOutBfr->PortAttributes.OSDeviceName,
                                              MODEL,
                                              sizeof(pOutBfr->PortAttributes.OSDeviceName));
                            pOutBfr->PortAttributes.PortState = HBA_PORTSTATE_ONLINE;
                            pOutBfr->PortAttributes.PortType = HBA_PORTTYPE_SASDEVICE;
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION,
                                         " --> SM_GetPortAttributesByWWN Not Implemented Yet\n");
                        }
                        break;
                    case SM_GetProtocolStatistics:
                        {
                            PSM_GetProtocolStatistics_IN pInBfr = (PSM_GetProtocolStatistics_IN)Buffer;
                            PSM_GetProtocolStatistics_OUT pOutBfr = (PSM_GetProtocolStatistics_OUT)Buffer;
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " --> SM_GetProtocolStatistics\n");
                            size = SM_GetProtocolStatistics_OUT_SIZE;
                            if (OutBufferSize < size)
                            {
                                status = SRB_STATUS_DATA_OVERRUN;
                                break;
                            }
                            if (InBufferSize < SM_GetProtocolStatistics_IN_SIZE)
                            {
                                status = SRB_STATUS_ERROR;
                                break;
                            }
                        }
                        break;
                    case SM_GetPhyStatistics:
                        {
                            PSM_GetPhyStatistics_IN pInBfr = (PSM_GetPhyStatistics_IN)Buffer;
                            PSM_GetPhyStatistics_OUT pOutBfr = (PSM_GetPhyStatistics_OUT)Buffer;
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " --> SM_GetPhyStatistics\n");
                            size = FIELD_OFFSET(SM_GetPhyStatistics_OUT, PhyCounter) + sizeof(LONGLONG);
                            if (OutBufferSize < size)
                            {
                                status = SRB_STATUS_DATA_OVERRUN;
                                break;
                            }
                            if (InBufferSize < SM_GetPhyStatistics_IN_SIZE)
                            {
                                status = SRB_STATUS_ERROR;
                                break;
                            }
                        }
                        break;
                    case SM_GetFCPhyAttributes:
                        {
                            PSM_GetFCPhyAttributes_IN pInBfr = (PSM_GetFCPhyAttributes_IN)Buffer;
                            PSM_GetFCPhyAttributes_OUT pOutBfr = (PSM_GetFCPhyAttributes_OUT)Buffer;

                            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " --> SM_GetFCPhyAttributes\n");
                            size = SM_GetFCPhyAttributes_OUT_SIZE;

                            if (OutBufferSize < size)
                            {
                                status = SRB_STATUS_DATA_OVERRUN;
                                break;
                            }

                            if (InBufferSize < SM_GetFCPhyAttributes_IN_SIZE)
                            {
                                status = SRB_STATUS_ERROR;
                                break;
                            }
                        }
                        break;
                    case SM_GetSASPhyAttributes:
                        {
                            PSM_GetSASPhyAttributes_IN pInBfr = (PSM_GetSASPhyAttributes_IN)Buffer;
                            PSM_GetSASPhyAttributes_OUT pOutBfr = (PSM_GetSASPhyAttributes_OUT)Buffer;
                            RhelDbgPrint(TRACE_LEVEL_INFORMATION, " --> SM_GetSASPhyAttributes\n");
                            size = SM_GetSASPhyAttributes_OUT_SIZE;
                            if (OutBufferSize < size)
                            {
                                status = SRB_STATUS_DATA_OVERRUN;
                                break;
                            }
                            if (InBufferSize < SM_GetSASPhyAttributes_IN_SIZE)
                            {
                                status = SRB_STATUS_ERROR;
                                break;
                            }
                        }
                        break;
                    case SM_RefreshInformation:
                        {
                        }
                        break;
                    default:
                        status = SRB_STATUS_INVALID_REQUEST;
                        RhelDbgPrint(TRACE_LEVEL_ERROR, " --> ERROR Unknown MethodId = %lu\n", MethodId);
                        break;
                }
            }
            break;
        default:
            status = SRB_STATUS_INVALID_REQUEST;
            RhelDbgPrint(TRACE_LEVEL_ERROR, " --> VioScsiExecuteWmiMethod Unsupported GuidIndex = %lu\n", GuidIndex);
            break;
    }
    ScsiPortWmiPostProcess(RequestContext, status, size);

    EXIT_FN();
    return SRB_STATUS_SUCCESS;
}

UCHAR
VioScsiQueryWmiRegInfo(IN PVOID Context, IN PSCSIWMI_REQUEST_CONTEXT RequestContext, OUT PWCHAR *MofResourceName)
{
    ENTER_FN();
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(RequestContext);

    *MofResourceName = VioScsiWmi_MofResourceName;
    return SRB_STATUS_SUCCESS;
}

VOID VioScsiReadExtendedData(IN PVOID Context, OUT PUCHAR Buffer)
{
    UCHAR numberOfBytes = sizeof(VioScsiExtendedInfo) - 1;
    PADAPTER_EXTENSION adaptExt;
    PVioScsiExtendedInfo extInfo;

    ENTER_FN();

    adaptExt = (PADAPTER_EXTENSION)Context;
    extInfo = (PVioScsiExtendedInfo)Buffer;

    RtlZeroMemory(Buffer, numberOfBytes);

    extInfo->QueueDepth = (ULONG)adaptExt->queue_depth;
    extInfo->QueuesCount = (UCHAR)adaptExt->num_queues;
    extInfo->Indirect = CHECKBIT(adaptExt->features, VIRTIO_RING_F_INDIRECT_DESC);
    extInfo->EventIndex = CHECKBIT(adaptExt->features, VIRTIO_RING_F_EVENT_IDX);
    extInfo->RingPacked = CHECKBIT(adaptExt->features, VIRTIO_F_RING_PACKED);
    extInfo->DpcRedirection = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_DPC_REDIRECTION);
    extInfo->ConcurrentChannels = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_CONCURRENT_CHANNELS);
    extInfo->InterruptMsgRanges = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_INTERRUPT_MESSAGE_RANGES);
    extInfo->CompletionDuringStartIo = CHECKFLAG(adaptExt->perfFlags, STOR_PERF_OPTIMIZE_FOR_COMPLETION_DURING_STARTIO);
    extInfo->PhysicalBreaks = adaptExt->max_physical_breaks;
    extInfo->ResponseTime = adaptExt->resp_time;
    EXIT_FN();
}
