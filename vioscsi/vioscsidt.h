#ifndef _vioscsidt_h_
#define _vioscsidt_h_

// VioScsiExtendedInfoGuid - VioScsiExtendedInfo
// VirtIO SCSI Extended Information
#define VioScsiWmi_ExtendedInfo_Guid                                                                                   \
    {                                                                                                                  \
        0x5cdac4f6, 0x3d46, 0x44e2,                                                                                    \
        {                                                                                                              \
            0x8d, 0xee, 0x01, 0x60, 0x6e, 0x11, 0xe2, 0x65                                                             \
        }                                                                                                              \
    }

#if !(defined(MIDL_PASS))
DEFINE_GUID(VioScsiExtendedInfoGuid_GUID, 0x5cdac4f6, 0x3d46, 0x44e2, 0x8d, 0xee, 0x01, 0x60, 0x6e, 0x11, 0xe2, 0x65);
#endif

typedef struct _VioScsiExtendedInfo
{
    //
    ULONG QueueDepth;
#define VioScsiExtendedInfo_QueueDepth_SIZE sizeof(ULONG)
#define VioScsiExtendedInfo_QueueDepth_ID   1

    //
    UCHAR QueuesCount;
#define VioScsiExtendedInfo_QueuesCount_SIZE sizeof(UCHAR)
#define VioScsiExtendedInfo_QueuesCount_ID   2

    //
    BOOLEAN Indirect;
#define VioScsiExtendedInfo_Indirect_SIZE sizeof(BOOLEAN)
#define VioScsiExtendedInfo_Indirect_ID   3

    //
    BOOLEAN EventIndex;
#define VioScsiExtendedInfo_EventIndex_SIZE sizeof(BOOLEAN)
#define VioScsiExtendedInfo_EventIndex_ID   4

    //
    BOOLEAN DpcRedirection;
#define VioScsiExtendedInfo_DpcRedirection_SIZE sizeof(BOOLEAN)
#define VioScsiExtendedInfo_DpcRedirection_ID   5

    //
    BOOLEAN ConcurrentChannels;
#define VioScsiExtendedInfo_ConcurrentChannels_SIZE sizeof(BOOLEAN)
#define VioScsiExtendedInfo_ConcurrentChannels_ID   6

    //
    BOOLEAN InterruptMsgRanges;
#define VioScsiExtendedInfo_InterruptMsgRanges_SIZE sizeof(BOOLEAN)
#define VioScsiExtendedInfo_InterruptMsgRanges_ID   7

    //
    BOOLEAN CompletionDuringStartIo;
#define VioScsiExtendedInfo_CompletionDuringStartIo_SIZE sizeof(BOOLEAN)
#define VioScsiExtendedInfo_CompletionDuringStartIo_ID   8

    //
    BOOLEAN RingPacked;
#define VioScsiExtendedInfo_RingPacked_SIZE sizeof(BOOLEAN)
#define VioScsiExtendedInfo_RingPacked_ID   9

    //
    ULONG PhysicalBreaks;
#define VioScsiExtendedInfo_PhysicalBreaks_SIZE sizeof(ULONG)
#define VioScsiExtendedInfo_PhysicalBreaks_ID   10

    //
    ULONG ResponseTime;
#define VioScsiExtendedInfo_ResponseTime_SIZE sizeof(ULONG)
#define VioScsiExtendedInfo_ResponseTime_ID   11
} VioScsiExtendedInfo, *PVioScsiExtendedInfo;

#define VioScsiExtendedInfo_SIZE                                                                                       \
    (FIELD_OFFSET(VioScsiExtendedInfo, ResponseTime) + VioScsiExtendedInfo_ResponseTime_SIZE)

// VioScsiDiagGuid - VioScsiDiag
// VirtIO SCSI Diagnostic Counters
#define VioScsiWmi_Diag_Guid {0x8bc55735, 0x2c57, 0x4b33, {0xa0, 0xc6, 0xd6, 0x98, 0x1a, 0x25, 0xbd, 0x1a}}

#if !(defined(MIDL_PASS))
DEFINE_GUID(VioScsiDiagGuid_GUID, 0x8bc55735, 0x2c57, 0x4b33, 0xa0, 0xc6, 0xd6, 0x98, 0x1a, 0x25, 0xbd, 0x1a);
#endif

typedef struct _VioScsiDiag
{
    //
    ULONGLONG SrbsSent;
#define VioScsiDiag_SrbsSent_SIZE sizeof(ULONGLONG)
#define VioScsiDiag_SrbsSent_ID   1

    //
    ULONGLONG SrbsCompleted;
#define VioScsiDiag_SrbsCompleted_SIZE sizeof(ULONGLONG)
#define VioScsiDiag_SrbsCompleted_ID   2

    //
    ULONG QueueFull;
#define VioScsiDiag_QueueFull_SIZE sizeof(ULONG)
#define VioScsiDiag_QueueFull_ID   3

    //
    ULONG ResetCount;
#define VioScsiDiag_ResetCount_SIZE sizeof(ULONG)
#define VioScsiDiag_ResetCount_ID   4

    //
    ULONGLONG SrbsCompletedOnReset;
#define VioScsiDiag_SrbsCompletedOnReset_SIZE sizeof(ULONGLONG)
#define VioScsiDiag_SrbsCompletedOnReset_ID   5

    //
    ULONG SlowResponses;
#define VioScsiDiag_SlowResponses_SIZE sizeof(ULONG)
#define VioScsiDiag_SlowResponses_ID   6

    //
    ULONG SrbIdCollisions;
#define VioScsiDiag_SrbIdCollisions_SIZE sizeof(ULONG)
#define VioScsiDiag_SrbIdCollisions_ID   7
} VioScsiDiag, *PVioScsiDiag;

#define VioScsiDiag_SIZE (FIELD_OFFSET(VioScsiDiag, SrbIdCollisions) + VioScsiDiag_SrbIdCollisions_SIZE)

#endif
