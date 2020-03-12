#ifndef _vioscsidt_h_
#define _vioscsidt_h_

// VioScsiExtendedInfoGuid - VioScsiExtendedInfo
// VirtIO SCSI Extended Information
#define VioScsiWmi_ExtendedInfo_Guid \
    { 0x5cdac4f6,0x3d46,0x44e2, { 0x8d,0xee,0x01,0x60,0x6e,0x11,0xe2,0x65 } }

#if ! (defined(MIDL_PASS))
DEFINE_GUID(VioScsiExtendedInfoGuid_GUID, \
            0x5cdac4f6,0x3d46,0x44e2,0x8d,0xee,0x01,0x60,0x6e,0x11,0xe2,0x65);
#endif


typedef struct _VioScsiExtendedInfo
{
    // 
    ULONG QueueDepth;
    #define VioScsiExtendedInfo_QueueDepth_SIZE sizeof(ULONG)
    #define VioScsiExtendedInfo_QueueDepth_ID 1

    // 
    UCHAR QueuesCount;
    #define VioScsiExtendedInfo_QueuesCount_SIZE sizeof(UCHAR)
    #define VioScsiExtendedInfo_QueuesCount_ID 2

    // 
    BOOLEAN Indirect;
    #define VioScsiExtendedInfo_Indirect_SIZE sizeof(BOOLEAN)
    #define VioScsiExtendedInfo_Indirect_ID 3

    // 
    BOOLEAN EventIndex;
    #define VioScsiExtendedInfo_EventIndex_SIZE sizeof(BOOLEAN)
    #define VioScsiExtendedInfo_EventIndex_ID 4

    // 
    BOOLEAN DpcRedirection;
    #define VioScsiExtendedInfo_DpcRedirection_SIZE sizeof(BOOLEAN)
    #define VioScsiExtendedInfo_DpcRedirection_ID 5

    // 
    BOOLEAN ConcurrentChannels;
    #define VioScsiExtendedInfo_ConcurrentChannels_SIZE sizeof(BOOLEAN)
    #define VioScsiExtendedInfo_ConcurrentChannels_ID 6

    // 
    BOOLEAN InterruptMsgRanges;
    #define VioScsiExtendedInfo_InterruptMsgRanges_SIZE sizeof(BOOLEAN)
    #define VioScsiExtendedInfo_InterruptMsgRanges_ID 7

    // 
    BOOLEAN CompletionDuringStartIo;
    #define VioScsiExtendedInfo_CompletionDuringStartIo_SIZE sizeof(BOOLEAN)
    #define VioScsiExtendedInfo_CompletionDuringStartIo_ID 8

    // 
    BOOLEAN RingPacked;
    #define VioScsiExtendedInfo_RingPacked_SIZE sizeof(BOOLEAN)
    #define VioScsiExtendedInfo_RingPacked_ID 9

    // 
    ULONG PhysicalBreaks;
    #define VioScsiExtendedInfo_PhysicalBreaks_SIZE sizeof(ULONG)
    #define VioScsiExtendedInfo_PhysicalBreaks_ID 10

} VioScsiExtendedInfo, *PVioScsiExtendedInfo;

#define VioScsiExtendedInfo_SIZE (FIELD_OFFSET(VioScsiExtendedInfo, PhysicalBreaks) + VioScsiExtendedInfo_PhysicalBreaks_SIZE)

#endif
