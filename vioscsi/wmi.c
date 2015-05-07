#ifdef ENABLE_WMI
#include "vioscsi.h"
#include "wmidata.h"

UCHAR
VioScsiQueryWmiRegInfo(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    _Out_ PWCHAR *MofResourceName);

BOOLEAN
VioScsiQueryWmiDataBlock(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT DispatchContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG BufferAvail,
    OUT PUCHAR Buffer);

BOOLEAN
VioScsiPdoQueryWmiDataBlock(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT DispatchContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG BufferAvail,
    OUT PUCHAR Buffer);

UCHAR
BuildVirtQueueStatistics(
    IN PADAPTER_EXTENSION AdapterExtension,
    IN PSCSIWMI_REQUEST_CONTEXT DispatchContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG BufferAvail,
    OUT PUCHAR Buffer,
    OUT PULONG SizeNeeded);

UCHAR
BuildTargetStatistics(
    IN PADAPTER_EXTENSION AdapterExtension,
    IN PSCSIWMI_REQUEST_CONTEXT DispatchContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG BufferAvail,
    OUT PUCHAR Buffer,
    OUT PULONG SizeNeeded);

//
// The index numbers should correspond to the offset in
// WmiGuidList array given below.
//
#define VirtQueueStatistics_GUID_INDEX 0
GUID VirtQueueStatisticsGuid = VirtQueue_StatisticsGuid;

//
// GUID List and number of GUIDs in the list for the adapter WMI.
//
SCSIWMIGUIDREGINFO WmiGuidList[] =
{
    {
        &VirtQueueStatisticsGuid,
        0xffffffff, //dynamic instance names
        0
    },
};

#define WmiGuidCount (sizeof(WmiGuidList) / sizeof(SCSIWMIGUIDREGINFO))

//
// The index numbers should correspond to the offset in
// PdoWmiGuidList array given below.
//
#define TargetStatistics_GUID_INDEX 0
GUID TargetStatisticsGuid = Target_StatisticsGuid;

//
// GUID List and number of GUIDs in the list for the PDO WMI.
//
SCSIWMIGUIDREGINFO PdoWmiGuidList[] =
{
    {
        &TargetStatisticsGuid,
        0xffffffff, //dynamic instance names
        0
    },
};

#define PdoWmiGuidCount (sizeof(PdoWmiGuidList) / sizeof(SCSIWMIGUIDREGINFO))

#define WmiMofResourceName L"MofResource"

VOID
WmiInitializeContext(
    IN PADAPTER_EXTENSION AdapterExtension
    )
/*+++

Routine Description:

    This routine will initialize the wmilib context structure with the
    guid list and the pointers to the wmilib callback functions.

Arguments:

    AdapterExtension - Adpater extension

Return Value:

    None.

--*/
{
    PSCSI_WMILIB_CONTEXT wmiLibContext;

    // Initialize the wmilib context for the adapter
    wmiLibContext = &(AdapterExtension->WmiLibContext);

    wmiLibContext->GuidList = WmiGuidList;
    wmiLibContext->GuidCount = WmiGuidCount;

    // Set pointers to WMI callback routines
    wmiLibContext->QueryWmiRegInfo = VioScsiQueryWmiRegInfo;
    wmiLibContext->QueryWmiDataBlock = VioScsiQueryWmiDataBlock;
    wmiLibContext->ExecuteWmiMethod = NULL;
    wmiLibContext->WmiFunctionControl = NULL;
    wmiLibContext->SetWmiDataItem = NULL;
    wmiLibContext->SetWmiDataBlock = NULL;

    // Initialize the wmilib context for the pdo
    wmiLibContext = &(AdapterExtension->PdoWmiLibContext);

    wmiLibContext->GuidList = PdoWmiGuidList;
    wmiLibContext->GuidCount = PdoWmiGuidCount;

    // Set pointers to WMI callback routines
    wmiLibContext->QueryWmiRegInfo = VioScsiQueryWmiRegInfo;
    wmiLibContext->QueryWmiDataBlock = VioScsiPdoQueryWmiDataBlock;
    wmiLibContext->ExecuteWmiMethod = NULL;
    wmiLibContext->WmiFunctionControl = NULL;
    wmiLibContext->SetWmiDataItem = NULL;
    wmiLibContext->SetWmiDataBlock = NULL;
}

BOOLEAN
WmiSrb(
    IN     PADAPTER_EXTENSION          AdapterExtension,
    IN OUT PSCSI_WMI_REQUEST_BLOCK     Srb
    )
/*++

Routine Description:

   Called from StartIo routine to process an SRB_FUNCTION_WMI request.
   Main entry point for all WMI routines.

Arguments:

   AdapterExtension - ISCSI miniport driver's Adapter extension.

   Srb              - IO request packet.

Return Value:

   Always TRUE.

--*/
{
    BOOLEAN adapterRequest;
    SCSIWMI_REQUEST_CONTEXT requestContext = {0};

    // Validate our assumptions.
    NT_ASSERT(Srb->Function == SRB_FUNCTION_WMI);
    NT_ASSERT(Srb->Length == sizeof(SCSI_WMI_REQUEST_BLOCK));

    // Check if the WMI SRB is targetted for the adapter or one of the devices
    adapterRequest = (Srb->WMIFlags & SRB_WMI_FLAGS_ADAPTER_REQUEST) == SRB_WMI_FLAGS_ADAPTER_REQUEST;
    // Note: the dispatch functions are not allowed to pend the srb.
    ScsiPortWmiDispatchFunction(
        adapterRequest ? &AdapterExtension->WmiLibContext : &AdapterExtension->PdoWmiLibContext,
        Srb->WMISubFunction,
        AdapterExtension,
        &requestContext,
        Srb->DataPath,
        Srb->DataTransferLength,
        Srb->DataBuffer);
    Srb->DataTransferLength = ScsiPortWmiGetReturnSize(&requestContext);
    Srb->SrbStatus = ScsiPortWmiGetReturnStatus(&requestContext);

    return TRUE;
}

UCHAR
VioScsiQueryWmiRegInfo(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT RequestContext,
    _Out_ PWCHAR *MofResourceName
    )
/*+++

Routine Description:

    This routine returns MofResourceName for this driver.

--*/
{
    *MofResourceName = WmiMofResourceName;
    return SRB_STATUS_SUCCESS;
}

BOOLEAN
VioScsiQueryWmiDataBlock(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT DispatchContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG BufferAvail,
    OUT PUCHAR Buffer
    )
/*+++

Routine Description :

    Called to query WMI Data blocks

--*/
{
    UCHAR status = SRB_STATUS_ERROR;
    ULONG sizeNeeded = 0;
    PADAPTER_EXTENSION adapterExtension = (PADAPTER_EXTENSION) Context;

    switch (GuidIndex) {
    case VirtQueueStatistics_GUID_INDEX: {
            status = BuildVirtQueueStatistics(adapterExtension,
                                              DispatchContext,
                                              GuidIndex,
                                              InstanceIndex,
                                              InstanceCount,
                                              InstanceLengthArray,
                                              BufferAvail,
                                              Buffer,
                                              &sizeNeeded);

            break;
        }
    }
    ScsiPortWmiPostProcess(DispatchContext, status, sizeNeeded);

    return status;
}

static inline
VOID
CopyQueueStatistics(
    IN PADAPTER_EXTENSION AdapterExtension,
    IN ULONG InstanceIdx,
    OUT PVOID Buffer) 
/*+++

Routine Description:

Copies the statistics for a virtqueue to the given destination.

--*/
{
    VirtQueue_Statistics* stats = (VirtQueue_Statistics*)Buffer;
    stats->TotalRequests = AdapterExtension->QueueStats[InstanceIdx].TotalRequests;
    stats->InFlightRequests = AdapterExtension->QueueStats[InstanceIdx].TotalRequests -
        AdapterExtension->QueueStats[InstanceIdx].CompletedRequests;
    stats->TotalKicks = AdapterExtension->QueueStats[InstanceIdx].TotalKicks;
    stats->SkippedKicks = AdapterExtension->QueueStats[InstanceIdx].SkippedKicks;
    stats->TotalInterrupts = AdapterExtension->QueueStats[InstanceIdx].TotalInterrupts;
    stats->LastUsedIdx = virtqueue_get_last_used_idx(AdapterExtension->vq[InstanceIdx + VIRTIO_SCSI_REQUEST_QUEUE_0]);
    stats->UsedIdx = virtqueue_get_used_idx(AdapterExtension->vq[InstanceIdx + VIRTIO_SCSI_REQUEST_QUEUE_0]);
    stats->QueueFullEvents = AdapterExtension->QueueStats[InstanceIdx].QueueFullEvents;
}

WCHAR InstancePrefix[] = L"Adapter";
#define ADAPTER_DIGITS 4
#define INSTANCE_DIGITS 3

static USHORT powers_of_10[] = { 1, 10, 100, 1000, 10000 };

// These utility methods are needed because we
// can't call RtlStringCbPrintf at DISPATCH_LEVEL
VOID
AppendNumber(LPWSTR Destination, USHORT Number, UCHAR Digits) {
    int i;
    NT_ASSERT(Digits < 6);
    for (i = Digits; i > 0; i--) {
        *Destination = (Number / powers_of_10[i - 1]) + '0';
        Number = Number % powers_of_10[i - 1];
        Destination++;
    }
}

USHORT ExtractNumber(LPWSTR Destination, UCHAR Digits) {
    int i;
    USHORT result = 0;
    NT_ASSERT(Digits < 6);
    for (i = Digits; i > 0; i--) {
        NT_ASSERT(*Destination >= '0' && *Destination < '9');
        result += (*Destination - '0') * powers_of_10[i - 1];
        Destination++;
    }
    return result;
}

USHORT
GetPortNumber(IN PADAPTER_EXTENSION AdapterExtension) {
#if (NTDDI_VERSION >= NTDDI_WIN8)
    STOR_ADDR_BTL8 stor_addr = {
        STOR_ADDRESS_TYPE_BTL8,
        0,
        STOR_ADDR_BTL8_ADDRESS_LENGTH,
        0,
        0,
        0,
        0 };
    ULONG status = StorPortGetSystemPortNumber(AdapterExtension, (PSTOR_ADDRESS)&stor_addr);
    NT_ASSERT(status == STOR_STATUS_SUCCESS);
    return stor_addr.Port;
#else
    return AdapterExtension->PortNumber;
#endif
}

VOID
BuildInstanceName(
    IN PADAPTER_EXTENSION AdapterExtension,
    IN PCWSTR BaseName,
    IN USHORT BaseNameCchLen,
    IN UCHAR InstanceIdx,
    OUT PWMIString Name)
/*+++

Routine Description:

Returns the performance counter instance name for a given counter.
--*/
{
    USHORT len = _ARRAYSIZE(InstancePrefix) - 1;
    RtlCopyMemory(Name->Buffer, InstancePrefix, len * sizeof(WCHAR));
    AppendNumber(Name->Buffer + len, GetPortNumber(AdapterExtension), ADAPTER_DIGITS);
    len += ADAPTER_DIGITS;
    RtlCopyMemory(Name->Buffer + len, BaseName, BaseNameCchLen * sizeof(WCHAR));
    len += BaseNameCchLen;
    AppendNumber(Name->Buffer + len, InstanceIdx, INSTANCE_DIGITS);
    len += INSTANCE_DIGITS;
    Name->Buffer[len] = 0;
    Name->Length = len * sizeof(WCHAR);
}

UCHAR
GetInstanceIdx(
    IN PADAPTER_EXTENSION AdapterExtension,
    IN PCWSTR BaseName,
    IN USHORT BaseNameCchLen,
    IN PWMIString Name)
/*+++

Routine Description:

Given a instance name, returns the virtqueue whose these counters belong to.
Returns -1 if no instance is found.
--*/
{
    NT_ASSERT(BaseNameCchLen >= 1);
    int len = _ARRAYSIZE(InstancePrefix) - 1 + ADAPTER_DIGITS + BaseNameCchLen;
    if ((len + INSTANCE_DIGITS) * sizeof(WCHAR) == Name->Length &&
        Name->Buffer[len - 1] == BaseName[BaseNameCchLen - 1] &&
        Name->Buffer[len - BaseNameCchLen] == BaseName[0]) {
        return (UCHAR)ExtractNumber(Name->Buffer + len, INSTANCE_DIGITS);
    }

    NT_ASSERT(FALSE);
    return -1;
}

WCHAR VQBASENAME[] = L"Queue";

UCHAR
BuildVirtQueueStatistics(
    IN PADAPTER_EXTENSION AdapterExtension,
    IN PSCSIWMI_REQUEST_CONTEXT DispatchContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG BufferAvail,
    OUT PUCHAR Buffer,
    OUT PULONG SizeNeeded
    )
/*+++

Routine Description:

Build queue statistics WMI data for all IO queues. Admin and event queues are not included.
--*/
{
    PWCHAR NameOffset;
    WMIString DynamicInstanceName;
    PUCHAR currentDataPos;
    UCHAR instanceIdx;
    PWMIString instanceName;
    ULONG newOutBufferAvail;
    UCHAR srbStatus = SRB_STATUS_SUCCESS;
    *SizeNeeded = 0;

    if (DispatchContext->MinorFunction == IRP_MN_QUERY_ALL_DATA) {
        if (ScsiPortWmiSetInstanceCount(DispatchContext,
                                        AdapterExtension->num_queues,
                                        &newOutBufferAvail,
                                        SizeNeeded)) {
            if (newOutBufferAvail == 0) {
                srbStatus = SRB_STATUS_DATA_OVERRUN;
            }
            // WMI packs the results in a WNODE_ALL_DATA structure.
            // Even if we cannot copy the data, we still have to go through the loop to calculate
            // how much space is actually needed.
            for (instanceIdx = 0; instanceIdx < AdapterExtension->num_queues; instanceIdx++) {
                currentDataPos = (PUCHAR)ScsiPortWmiSetData(DispatchContext,
                    instanceIdx,
                    sizeof(VirtQueue_Statistics),
                    &newOutBufferAvail,
                    SizeNeeded);
                if (newOutBufferAvail == 0 || currentDataPos == NULL ||
                    currentDataPos + sizeof(VirtQueue_Statistics) > Buffer + BufferAvail) {
                    srbStatus = SRB_STATUS_DATA_OVERRUN;
                }
                if (srbStatus == SRB_STATUS_SUCCESS) {
                    CopyQueueStatistics(AdapterExtension, instanceIdx, currentDataPos);
                }
                BuildInstanceName(
                    AdapterExtension,
                    VQBASENAME,
                    _ARRAYSIZE(VQBASENAME) - 1,
                    instanceIdx,
                    &DynamicInstanceName);
                NameOffset = ScsiPortWmiSetInstanceName(
                    DispatchContext,
                    instanceIdx,
                    DynamicInstanceName.Length + sizeof(USHORT),
                    &newOutBufferAvail,
                    SizeNeeded);
                if (newOutBufferAvail == 0 || NameOffset == NULL ||
                    (PUCHAR)NameOffset + DynamicInstanceName.Length + sizeof(USHORT) > Buffer + BufferAvail) {
                    srbStatus = SRB_STATUS_DATA_OVERRUN;
                }
                if (srbStatus == SRB_STATUS_SUCCESS) {
                    RtlCopyMemory(NameOffset, (PUCHAR)(&DynamicInstanceName),
                        (DynamicInstanceName.Length + sizeof(USHORT)));
                }
            }
       } else {
           srbStatus = SRB_STATUS_ERROR;
       }
    } else {
        // single instance
        instanceName = (PWMIString)ScsiPortWmiGetInstanceName(DispatchContext);
        if (instanceName != NULL)
        {
            instanceIdx = GetInstanceIdx(AdapterExtension, VQBASENAME, _ARRAYSIZE(VQBASENAME) - 1, instanceName);
            *SizeNeeded = sizeof(VirtQueue_Statistics);
            if (BufferAvail >= *SizeNeeded && instanceIdx < AdapterExtension->num_queues) {
                CopyQueueStatistics(AdapterExtension, instanceIdx, Buffer);
                *InstanceLengthArray = *SizeNeeded;
            } else {
                // The buffer passed to return the data is too small
                srbStatus = SRB_STATUS_DATA_OVERRUN;
            }
        } else {
            srbStatus = SRB_STATUS_ERROR;
        }
    }

    return srbStatus;
}

BOOLEAN
VioScsiPdoQueryWmiDataBlock(
    IN PVOID Context,
    IN PSCSIWMI_REQUEST_CONTEXT DispatchContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG BufferAvail,
    OUT PUCHAR Buffer
    )
/*+++

Routine Description :

Called to query WMI Data blocks

--*/
{
    UCHAR status = SRB_STATUS_ERROR;
    ULONG sizeNeeded = 0;
    PADAPTER_EXTENSION adapterExtension = (PADAPTER_EXTENSION)Context;

    switch (GuidIndex) {
        case TargetStatistics_GUID_INDEX: {
            status = BuildTargetStatistics(adapterExtension,
                DispatchContext,
                GuidIndex,
                InstanceIndex,
                InstanceCount,
                InstanceLengthArray,
                BufferAvail,
                Buffer,
                &sizeNeeded);

            break;
        }
    }
    ScsiPortWmiPostProcess(DispatchContext, status, sizeNeeded);

    return status;
}

static inline
VOID
CopyTargetStatistics(
    IN PADAPTER_EXTENSION AdapterExtension,
    IN ULONG InstanceIdx,
    OUT PVOID Buffer)
/*+++

Routine Description:

Copies the statistics for a scsi target to the given destination.

--*/
{
    Target_Statistics* stats = (Target_Statistics*)Buffer;
    stats->TotalRequests = AdapterExtension->TargetStats[InstanceIdx].TotalRequests;
    stats->InFlightRequests = AdapterExtension->TargetStats[InstanceIdx].TotalRequests -
        AdapterExtension->TargetStats[InstanceIdx].CompletedRequests;
}

WCHAR TARGETBASENAME[] = L"Target";

UCHAR
BuildTargetStatistics(
    IN PADAPTER_EXTENSION AdapterExtension,
    IN PSCSIWMI_REQUEST_CONTEXT DispatchContext,
    IN ULONG GuidIndex,
    IN ULONG InstanceIndex,
    IN ULONG InstanceCount,
    IN OUT PULONG InstanceLengthArray,
    IN ULONG BufferAvail,
    OUT PUCHAR Buffer,
    OUT PULONG SizeNeeded
    )
/*+++

Routine Description:

Build Target statistics WMI data for all Targets we saw so far.
--*/
{
    PWCHAR NameOffset;
    WMIString DynamicInstanceName;
    PUCHAR currentDataPos;
    UCHAR instanceIdx;
    PWMIString instanceName;
    ULONG newOutBufferAvail;
    UCHAR srbStatus = SRB_STATUS_SUCCESS;
    *SizeNeeded = 0;

    if (DispatchContext->MinorFunction == IRP_MN_QUERY_ALL_DATA) {
        if (ScsiPortWmiSetInstanceCount(DispatchContext,
                                        AdapterExtension->MaxTarget,
                                        &newOutBufferAvail,
                                        SizeNeeded)) {
            if (newOutBufferAvail == 0) {
                srbStatus = SRB_STATUS_DATA_OVERRUN;
            }
            // WMI packs the results in a WNODE_ALL_DATA structure.
            // Even if we cannot copy the data, we still have to go through the loop to calculate
            // how much space is actually needed.
            for (instanceIdx = 0; instanceIdx < AdapterExtension->MaxTarget; instanceIdx++) {
                currentDataPos = (PUCHAR)ScsiPortWmiSetData(DispatchContext,
                    instanceIdx,
                    sizeof(Target_Statistics),
                    &newOutBufferAvail,
                    SizeNeeded);
                if (newOutBufferAvail == 0 || currentDataPos == NULL ||
                    currentDataPos + sizeof(Target_Statistics) > Buffer + BufferAvail) {
                    srbStatus = SRB_STATUS_DATA_OVERRUN;
                }
                if (srbStatus == SRB_STATUS_SUCCESS) {
                    CopyTargetStatistics(AdapterExtension, instanceIdx, currentDataPos);
                }
                BuildInstanceName(
                    AdapterExtension,
                    TARGETBASENAME,
                    _ARRAYSIZE(TARGETBASENAME) - 1,
                    instanceIdx,
                    &DynamicInstanceName);
                NameOffset = ScsiPortWmiSetInstanceName(
                    DispatchContext,
                    instanceIdx,
                    DynamicInstanceName.Length + sizeof(USHORT),
                    &newOutBufferAvail,
                    SizeNeeded);
                if (newOutBufferAvail == 0 || NameOffset == NULL ||
                    (PUCHAR)NameOffset + DynamicInstanceName.Length + sizeof(USHORT) > Buffer + BufferAvail) {
                    srbStatus = SRB_STATUS_DATA_OVERRUN;
                }
                if (srbStatus == SRB_STATUS_SUCCESS) {
                    RtlCopyMemory(NameOffset, (PUCHAR)(&DynamicInstanceName),
                            (DynamicInstanceName.Length + sizeof(USHORT)));
                }
            }
        } else {
           srbStatus = SRB_STATUS_ERROR;
       }
    } else {
        // single instance
        instanceName = (PWMIString)ScsiPortWmiGetInstanceName(DispatchContext);
        if (instanceName != NULL)
        {
            instanceIdx = GetInstanceIdx(AdapterExtension, TARGETBASENAME, _ARRAYSIZE(TARGETBASENAME) - 1, instanceName);
            *SizeNeeded = sizeof(Target_Statistics);
            if (BufferAvail >= *SizeNeeded && instanceIdx < AdapterExtension->MaxTarget) {
                CopyTargetStatistics(AdapterExtension, instanceIdx, Buffer);
                *InstanceLengthArray = *SizeNeeded;
            } else {
                // The buffer passed to return the data is too small
                srbStatus = SRB_STATUS_DATA_OVERRUN;
            }
        } else {
            srbStatus = SRB_STATUS_ERROR;
        }
    }

    return srbStatus;
}

#endif
