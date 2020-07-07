/*
 * This file contains general definitions for VirtIO network adapter driver,
 * common for both NDIS5 and NDIS6
 *
 * Copyright (c) 2008-2017 Red Hat, Inc.
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
#ifndef PARANDIS_56_COMMON_H
#define PARANDIS_56_COMMON_H

#if defined(OFFLOAD_UNIT_TEST)
#include <windows.h>
#include <stdio.h>

#define DoPrint(fmt, ...) printf(fmt##"\n", __VA_ARGS__)
#define DPrintf(a,b) DoPrint b
#define RtlOffsetToPointer(B,O)  ((PCHAR)( ((PCHAR)(B)) + ((ULONG_PTR)(O))  ))

#include "ethernetutils.h"
#endif //+OFFLOAD_UNIT_TEST

#if !defined(OFFLOAD_UNIT_TEST)

#if !defined(RtlOffsetToPointer)
#define RtlOffsetToPointer(Base,Offset)  ((PCHAR)(((PCHAR)(Base))+((ULONG_PTR)(Offset))))
#endif

#if !defined(RtlPointerToOffset)
#define RtlPointerToOffset(Base,Pointer)  ((ULONG)(((PCHAR)(Pointer))-((PCHAR)(Base))))
#endif

extern "C"
{
#include "osdep.h"

#if NDIS_SUPPORT_NDIS630
#define PARANDIS_SUPPORT_RSC 1
#endif

#if NDIS_SUPPORT_NDIS620
#define PARANDIS_SUPPORT_RSS 1
#endif

#if !NDIS_SUPPORT_NDIS620
    static VOID FORCEINLINE NdisFreeMemoryWithTagPriority(
        IN  NDIS_HANDLE             NdisHandle,
        IN  PVOID                   VirtualAddress,
        IN  ULONG                   Tag)
    {
        UNREFERENCED_PARAMETER(NdisHandle);
        UNREFERENCED_PARAMETER(Tag);
        NdisFreeMemory(VirtualAddress, 0, 0);
    }
#endif

#include "kdebugprint.h"
#include "virtio_pci.h"
#include "DebugData.h"
}

#if !defined(_Function_class_)
#define _Function_class_(x)
#endif

typedef struct _tagRunTimeNdisVersion
{
    UCHAR major;
    UCHAR minor;
}tRunTimeNdisVersion;

extern const tRunTimeNdisVersion& ParandisVersion;

/* true if NDIS version is at least major.minor */
static bool FORCEINLINE CheckNdisVersion(UCHAR major, UCHAR minor)
{
    if (ParandisVersion.major == major)
    {
        return ParandisVersion.minor >= minor;
    }
    return ParandisVersion.major > major;
}

typedef struct _tagPARANDIS_ADAPTER PARANDIS_ADAPTER;

#include "ParaNdis-SM.h"
#include "ParaNdis-RSS.h"

typedef union _tagTcpIpPacketParsingResult tTcpIpPacketParsingResult;

typedef struct _tagCompletePhysicalAddress
{
    PHYSICAL_ADDRESS    Physical;
    PVOID               Virtual;
    ULONG               size;
} tCompletePhysicalAddress;

struct _tagRxNetDescriptor;
typedef struct _tagRxNetDescriptor  RxNetDescriptor, *pRxNetDescriptor;

static __inline BOOLEAN ParaNDIS_IsQueueInterruptEnabled(struct virtqueue * _vq);

typedef struct _tagPARANDIS_RECEIVE_QUEUE
{
    NDIS_SPIN_LOCK          Lock;
    LIST_ENTRY              BuffersList;
    COwnership              Ownership;
} PARANDIS_RECEIVE_QUEUE, *PPARANDIS_RECEIVE_QUEUE;

#include "ParaNdis-TX.h"
#include "ParaNdis-RX.h"
#include "ParaNdis-CX.h"
#include "ParaNdis_GuestAnnounce.h"
#include "ParaNdis-VirtIO.h"

struct CPUPathBundle : public CPlacementAllocatable {
    CParaNdisRX rxPath;
    bool        rxCreated = false;

    CParaNdisTX txPath;
    bool        txCreated = false;

    CParaNdisCX *cxPath = NULL;

    ~CPUPathBundle()
    {
        if (rxCreated)
        {
            rxCreated = false;
        }
        if (txCreated)
        {
            txCreated = false;
        }
    }
} ;

// those stuff defined in NDIS
//NDIS_MINIPORT_MAJOR_VERSION
//NDIS_MINIPORT_MINOR_VERSION
// those stuff defined in build environment
// PARANDIS_MAJOR_DRIVER_VERSION
// PARANDIS_MINOR_DRIVER_VERSION

#if !defined(NDIS_MINIPORT_MAJOR_VERSION) || !defined(NDIS_MINIPORT_MINOR_VERSION)
#error "Something is wrong with NDIS environment"
#endif

#if !defined(PARANDIS_MAJOR_DRIVER_VERSION) || !defined(PARANDIS_MINOR_DRIVER_VERSION)
#error "Something is wrong with our versioning"
#endif

// define if qemu supports logging to static IO port for synchronization
// of driver output with qemu printouts; in this case define the port number
// #define VIRTIO_DBG_USE_IOPORT    0x99

// to be set to real limit later
#define MAX_RX_LOOPS    1000

#define VIRTIO_NET_INVALID_INTERRUPT_STATUS     0xFF

#define PARANDIS_MULTICAST_LIST_SIZE        32
#define PARANDIS_MEMORY_TAG                 '5muQ'
#define PARANDIS_DEFAULT_LINK_SPEED         10000000000  // 10Gbps link speed
#define PARANDIS_MIN_LSO_SEGMENTS           2
// reported
#define PARANDIS_MAX_LSO_SIZE               0xF800

#define PARANDIS_UNLIMITED_PACKETS_TO_INDICATE  (~0ul)

static const ULONG PARANDIS_PACKET_FILTERS =
    NDIS_PACKET_TYPE_DIRECTED |
    NDIS_PACKET_TYPE_MULTICAST |
    NDIS_PACKET_TYPE_BROADCAST |
    NDIS_PACKET_TYPE_PROMISCUOUS |
    NDIS_PACKET_TYPE_ALL_MULTICAST;

typedef VOID (*ONPAUSECOMPLETEPROC)(VOID *);


typedef enum _tagSendReceiveState
{
    srsDisabled = 0,        // initial state
    srsPausing,
    srsEnabled
} tSendReceiveState;

typedef enum _tagOffloadSettingsBit
{
    osbT4IpChecksum = (1 << 0),
    osbT4TcpChecksum = (1 << 1),
    osbT4UdpChecksum = (1 << 2),
    osbT4TcpOptionsChecksum = (1 << 3),
    osbT4IpOptionsChecksum = (1 << 4),
    osbT4Lso = (1 << 5),
    osbT4LsoIp = (1 << 6),
    osbT4LsoTcp = (1 << 7),
    osbT4RxTCPChecksum = (1 << 8),
    osbT4RxTCPOptionsChecksum = (1 << 9),
    osbT4RxIPChecksum = (1 << 10),
    osbT4RxIPOptionsChecksum = (1 << 11),
    osbT4RxUDPChecksum = (1 << 12),
    osbT6TcpChecksum = (1 << 13),
    osbT6UdpChecksum = (1 << 14),
    osbT6TcpOptionsChecksum = (1 << 15),
    osbT6IpExtChecksum = (1 << 16),
    osbT6Lso = (1 << 17),
    osbT6LsoIpExt = (1 << 18),
    osbT6LsoTcpOptions = (1 << 19),
    osbT6RxTCPChecksum = (1 << 20),
    osbT6RxTCPOptionsChecksum = (1 << 21),
    osbT6RxUDPChecksum = (1 << 22),
    osbT6RxIpExtChecksum = (1 << 23),
}tOffloadSettingsBit;

typedef struct _tagOffloadSettingsFlags
{
    int fTxIPChecksum       : 1;
    int fTxTCPChecksum      : 1;
    int fTxUDPChecksum      : 1;
    int fTxTCPOptions       : 1;
    int fTxIPOptions        : 1;
    int fTxLso              : 1;
    int fTxLsoIP            : 1;
    int fTxLsoTCP           : 1;
    int fRxIPChecksum       : 1;
    int fRxTCPChecksum      : 1;
    int fRxUDPChecksum      : 1;
    int fRxTCPOptions       : 1;
    int fRxIPOptions        : 1;
    int fTxTCPv6Checksum    : 1;
    int fTxUDPv6Checksum    : 1;
    int fTxTCPv6Options     : 1;
    int fTxIPv6Ext          : 1;
    int fTxLsov6            : 1;
    int fTxLsov6IP          : 1;
    int fTxLsov6TCP         : 1;
    int fRxTCPv6Checksum    : 1;
    int fRxUDPv6Checksum    : 1;
    int fRxTCPv6Options     : 1;
    int fRxIPv6Ext          : 1;
}tOffloadSettingsFlags;


typedef struct _tagOffloadSettings
{
    /* current value of enabled offload features */
    tOffloadSettingsFlags flags;
    /* load once, do not modify - bitmask of offload features, enabled in configuration */
    ULONG flagsValue;
    ULONG ipHeaderOffset;
    ULONG maxPacketSize;
}tOffloadSettings;

typedef struct _tagChecksumCheckResult
{
    union
    {
        struct
        {
            int   TcpFailed     :1;
            int   UdpFailed     :1;
            int   IpFailed      :1;
            int   TcpOK         :1;
            int   UdpOK         :1;
            int   IpOK          :1;
        } flags;
        int value;
    };
}tChecksumCheckResult;

typedef PMDL                tPacketHolderType;
typedef PNET_BUFFER_LIST    tPacketIndicationType;

typedef struct _tagMaxPacketSize
{
    UINT nMaxDataSize;
    UINT nMaxFullSizeOS;
    UINT nMaxFullSizeHwTx;
    UINT nMaxDataSizeHwRx;
    UINT nMaxFullSizeOsRx;
}tMaxPacketSize;

typedef struct _tagLinkProperties
{
    ULONGLONG Speed;
    NET_IF_MEDIA_DUPLEX_STATE DuplexState;
}tLinkProperties;

#define MAX_HW_RX_PACKET_SIZE (MAX_IP4_DATAGRAM_SIZE + ETH_HEADER_SIZE + ETH_PRIORITY_HEADER_SIZE)
#define MAX_OS_RX_PACKET_SIZE (MAX_IP4_DATAGRAM_SIZE + ETH_HEADER_SIZE)


typedef struct _tagMulticastData
{
    ULONG                   nofMulticastEntries;
    UCHAR                   MulticastList[ETH_ALEN * PARANDIS_MULTICAST_LIST_SIZE];
}tMulticastData;

typedef struct _tagNET_PACKET_INFO
{
    struct
    {
        int isBroadcast   : 1;
        int isMulticast   : 1;
        int isUnicast     : 1;
        int hasVlanHeader : 1;
        int isIP4         : 1;
        int isIP6         : 1;
        int isTCP         : 1;
        int isUDP         : 1;
        int isFragment    : 1;
    };

    struct
    {
        UINT32 UserPriority : 3;
        UINT32 VlanId       : 12;
    } Vlan;

#if PARANDIS_SUPPORT_RSS
    struct
    {
        ULONG Value;
        ULONG Type;
        ULONG Function;
    } RSSHash;
#endif

    ULONG L2HdrLen;
    ULONG L3HdrLen;
    ULONG L2PayloadLen;
    ULONG ip6HomeAddrOffset;
    ULONG ip6DestAddrOffset;

    PUCHAR ethDestAddr;

    PVOID headersBuffer;
    ULONG dataLength;
} NET_PACKET_INFO, *PNET_PACKET_INFO;

struct _tagRxNetDescriptor {
    LIST_ENTRY listEntry;
    LIST_ENTRY ReceiveQueueListEntry;

#define PARANDIS_FIRST_RX_DATA_PAGE   (1)
    struct VirtIOBufferDescriptor *BufferSGArray;
    tCompletePhysicalAddress      *PhysicalPages;
    ULONG                          BufferSGLength;
    tCompletePhysicalAddress       IndirectArea;
    tPacketHolderType              Holder;

    NET_PACKET_INFO PacketInfo;

    CParaNdisRX*                   Queue;
};

typedef struct _tagPARANDIS_ADAPTER
{
    NDIS_HANDLE             DriverHandle;
    NDIS_HANDLE             MiniportHandle;
    NDIS_HANDLE             InterruptHandle;
    NDIS_HANDLE             BufferListsPool;
    NDIS_HANDLE             BufferListsPoolForArm;

    CPciResources           PciResources;
    VirtIODevice            IODevice;
    CNdisSharedMemory       *pPageAllocator;

    LARGE_INTEGER           LastTxCompletionTimeStamp;
#ifdef PARANDIS_DEBUG_INTERRUPTS
    LARGE_INTEGER           LastInterruptTimeStamp;
#endif
    u64                     u64HostFeatures;
    u64                     u64GuestFeatures;
    BOOLEAN                 bConnected;
    BOOLEAN                 bGuestAnnounced;
    NDIS_PHYSICAL_MEDIUM    physicalMediaType;
    NDIS_MEDIA_CONNECT_STATE fCurrentLinkState;
    BOOLEAN                 bEnableInterruptHandlingDPC;
    BOOLEAN                 bDoSupportPriority;
    BOOLEAN                 bLinkDetectSupported;
    BOOLEAN                 bGuestAnnounceSupported;
    BOOLEAN                 bMaxMTUConfigSupported;
    BOOLEAN                 bLinkPropertiesConfigSupported;
    BOOLEAN                 bGuestChecksumSupported;
    BOOLEAN                 bControlQueueSupported;
    BOOLEAN                 bUseMergedBuffers;
    BOOLEAN                 bSurprizeRemoved;
    BOOLEAN                 bUsingMSIX;
    BOOLEAN                 bUseIndirect;
    BOOLEAN                 bAnyLayout;
    BOOLEAN                 bCtrlRXFiltersSupported;
    BOOLEAN                 bCtrlRXExtraFiltersSupported;
    BOOLEAN                 bCtrlVLANFiltersSupported;
    BOOLEAN                 bCtrlMACAddrSupported;
    BOOLEAN                 bCfgMACAddrSupported;
    BOOLEAN                 bMultiQueue;
    USHORT                  nHardwareQueues;
    ULONG                   ulCurrentVlansFilterSet;
    tMulticastData          MulticastData;
    UINT                    uNumberOfHandledRXPacketsInDPC;
    LONG                    counterDPCInside;
    ULONG                   ulPriorityVlanSetting;
    ULONG                   VlanId;
    ULONG                   ulEnableWakeup;
    tMaxPacketSize          MaxPacketSize;
    tLinkProperties         LinkProperties;
    ULONG                   ulUniqueID;
    UCHAR                   PermanentMacAddress[ETH_ALEN];
    UCHAR                   CurrentMacAddress[ETH_ALEN];
    ULONG                   PacketFilter;
    ULONG                   DummyLookAhead;
    ULONG                   nVirtioHeaderSize;

    CMiniportStateMachine   m_StateMachine;
    CDataFlowStateMachine   m_RxStateMachine;
    CConfigFlowStateMachine m_CxStateMachine;


    CGuestAnnouncePackets    guestAnnouncePackets;

    /* send part */
    NDIS_STATISTICS_INFO    Statistics;
    struct
    {
        ULONG framesCSOffload;
        ULONG framesLSO;
        ULONG framesRxPriority;
        ULONG framesRxCSHwOK;
        ULONG framesFilteredOut;
        ULONG framesCoalescedHost;
        ULONG framesCoalescedWindows;
        ULONG framesRSSHits;
        ULONG framesRSSMisses;
        ULONG framesRSSUnclassified;
        ULONG framesRSSError;
    } extraStatistics;

    /* initial number of free Tx descriptor(from cfg) - max number of available Tx descriptors */
    UINT                    maxFreeTxDescriptors;
    /* total of Rx buffer in turnaround */
    UINT                    NetMaxReceiveBuffers;
    UINT                    nPnpEventIndex;
    NDIS_DEVICE_PNP_EVENT   PnpEvents[16];
    tOffloadSettings        Offload;
    NDIS_OFFLOAD_PARAMETERS InitialOffloadParameters;

#ifdef PARANDIS_SUPPORT_RSS
    PARANDIS_RECEIVE_QUEUE      ReceiveQueues[PARANDIS_RSS_MAX_RECEIVE_QUEUES];
    BOOLEAN                     ReceiveQueuesInitialized;
#define PARANDIS_FIRST_RSS_RECEIVE_QUEUE    (0)
#endif
#define PARANDIS_RECEIVE_UNCLASSIFIED_PACKET (-1)
#define PARANDIS_RECEIVE_NO_QUEUE  (-2)

    CParaNdisCX CXPath;
    BOOLEAN bCXPathAllocated;
    BOOLEAN bCXPathCreated;
    BOOLEAN bSharedVectors;
    BOOLEAN bDeviceNeedsReset;

    CPUPathBundle               *pPathBundles;
    UINT                        nPathBundles;

    CPUPathBundle              **RSS2QueueMap;
    USHORT                      RSS2QueueLength;

    PIO_INTERRUPT_MESSAGE_INFO  pMSIXInfoTable;
    NDIS_HANDLE                 DmaHandle;
    ULONG                       ulIrqReceived;
    NDIS_OFFLOAD                ReportedOffloadCapabilities;
    NDIS_OFFLOAD                ReportedOffloadConfiguration;
    BOOLEAN                     bOffloadv4Enabled;
    BOOLEAN                     bOffloadv6Enabled;
    BOOLEAN                     bDeviceInitialized;
    BOOLEAN                     bRSSSupportedByDevice;
    BOOLEAN                     bRSSSupportedByDevicePersistent;
    BOOLEAN                     bHashReportedByDevice;

#if PARANDIS_SUPPORT_RSS
    BOOLEAN                     bRSSOffloadSupported;
    BOOLEAN                     bRSSInitialized;
    NDIS_RECEIVE_SCALE_CAPABILITIES RSSCapabilities;
    PARANDIS_RSS_PARAMS         RSSParameters;
    CCHAR                       RSSMaxQueuesNumber;
    struct
    {
        ULONG                   SupportedHashes;
        USHORT                  MaxIndirectEntries;
        UCHAR                   MaxKeySize;
    } DeviceRSSCapabilities;
#endif

#if PARANDIS_SUPPORT_RSC
    struct {
        BOOLEAN                     bIPv4SupportedSW;
        BOOLEAN                     bIPv6SupportedSW;
        BOOLEAN                     bIPv4SupportedHW;
        BOOLEAN                     bIPv6SupportedHW;
        BOOLEAN                     bIPv4Enabled;
        BOOLEAN                     bIPv6Enabled;
        BOOLEAN                     bQemuSupported;
        BOOLEAN                     bHasDynamicConfig;
        struct {
            LARGE_INTEGER           CoalescedPkts;
            LARGE_INTEGER           CoalescedOctets;
            LARGE_INTEGER           CoalesceEvents;
        }                           Statistics;
    } RSC;
#endif

    PVOID                   UnalignedAdapterContext;
    ULONG                   UnalignedAdapterContextSize;

    _tagPARANDIS_ADAPTER(const _tagPARANDIS_ADAPTER&) = delete;
    _tagPARANDIS_ADAPTER& operator= (const _tagPARANDIS_ADAPTER&) = delete;
}PARANDIS_ADAPTER, *PPARANDIS_ADAPTER;

typedef BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) (*tSynchronizedProcedure)(PVOID context);

BOOLEAN FORCEINLINE IsValidVlanId(PARANDIS_ADAPTER *pContext, ULONG VlanID)
{
    return pContext->VlanId == 0 || pContext->VlanId == VlanID;
}

BOOLEAN FORCEINLINE IsVlanSupported(PARANDIS_ADAPTER *pContext)
{
    return pContext->ulPriorityVlanSetting & 2;
}

BOOLEAN FORCEINLINE IsPrioritySupported(PARANDIS_ADAPTER *pContext)
{
    return pContext->ulPriorityVlanSetting & 1;
}

NDIS_STATUS ParaNdis_InitializeContext(
    PARANDIS_ADAPTER *pContext,
    PNDIS_RESOURCE_LIST ResourceList);

NDIS_STATUS ParaNdis_FinishInitialization(
    PARANDIS_ADAPTER *pContext);

NDIS_STATUS ParaNdis_ConfigureMSIXVectors(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_CleanupContext(
    PARANDIS_ADAPTER *pContext);

void ParaNdis_CXDPCWorkBody(PARANDIS_ADAPTER *pContext);

void ParaNdis_ReuseRxNBLs(PNET_BUFFER_LIST pNBL);

NDIS_STATUS ParaNdis_SetMulticastList(
    PARANDIS_ADAPTER *pContext,
    PVOID Buffer,
    ULONG BufferSize,
    PUINT pBytesRead,
    PUINT pBytesNeeded);

VOID ParaNdis_VirtIOEnableIrqSynchronized(
    PARANDIS_ADAPTER *pContext,
    ULONG interruptSource);

VOID ParaNdis_VirtIODisableIrqSynchronized(
    PARANDIS_ADAPTER *pContext,
    ULONG interruptSource);

static __inline VOID ParaNdis_ProcessorNumberToGroupAffinity(
    PGROUP_AFFINITY Affinity,
    const PPROCESSOR_NUMBER Processor)
{
    Affinity->Group = Processor->Group;
    Affinity->Mask = 1;
    Affinity->Mask <<= Processor->Number;
}

static __inline BOOLEAN
ParaNDIS_IsQueueInterruptEnabled(struct virtqueue * _vq)
{
    return virtqueue_is_interrupt_enabled(_vq);
}

VOID ParaNdis_OnPnPEvent(
    PARANDIS_ADAPTER *pContext,
    NDIS_DEVICE_PNP_EVENT pEvent,
    PVOID   pInfo,
    ULONG   ulSize);

BOOLEAN ParaNdis_OnLegacyInterrupt(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN *pRunDpc);

BOOLEAN ParaNdis_OnQueuedInterrupt(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN *pRunDpc,
    ULONG knownInterruptSources);

VOID ParaNdis_OnShutdown(
    PARANDIS_ADAPTER *pContext);

NDIS_STATUS ParaNdis_PowerOn(
    PARANDIS_ADAPTER *pContext
);

VOID ParaNdis_PowerOff(
    PARANDIS_ADAPTER *pContext
);

void ParaNdis_DeviceConfigureRSC(PARANDIS_ADAPTER *pContext);

void ParaNdis_ResetOffloadSettings(PARANDIS_ADAPTER *pContext, tOffloadSettingsFlags *pDest, PULONG from);

tChecksumCheckResult ParaNdis_CheckRxChecksum(
                                            PARANDIS_ADAPTER *pContext,
                                            ULONG virtioFlags,
                                            tCompletePhysicalAddress *pPacketPages,
                                            PNET_PACKET_INFO pPacketInfo,
                                            ULONG ulDataOffset,
                                            BOOLEAN verifyLength);

void ParaNdis_CallOnBugCheck(PARANDIS_ADAPTER *pContext);

/*****************************************************
Procedures to implement for NDIS specific implementation
******************************************************/

PVOID ParaNdis_AllocateMemory(
    PARANDIS_ADAPTER *pContext,
    ULONG ulRequiredSize);

PVOID ParaNdis_AllocateMemoryRaw(
    NDIS_HANDLE MiniportHandle,
    ULONG ulRequiredSize);

NDIS_STATUS ParaNdis_FinishSpecificInitialization(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_FinalizeCleanup(
    PARANDIS_ADAPTER *pContext);

NDIS_HANDLE ParaNdis_OpenNICConfiguration(
    PARANDIS_ADAPTER *pContext);

tPacketIndicationType ParaNdis_PrepareReceivedPacket(
    PARANDIS_ADAPTER *pContext,
    pRxNetDescriptor pBufferDesc,
    PUINT            pnCoalescedSegmentsCount);

BOOLEAN ParaNdis_SynchronizeWithInterrupt(
    PARANDIS_ADAPTER *pContext,
    ULONG messageId,
    tSynchronizedProcedure procedure,
    PVOID parameter);

typedef VOID (*tOnAdditionalPhysicalMemoryAllocated)(
    PARANDIS_ADAPTER *pContext,
    tCompletePhysicalAddress *pAddresses);


typedef struct _tagPhysicalAddressAllocationContext
{
    tCompletePhysicalAddress address;
    PARANDIS_ADAPTER *pContext;
    tOnAdditionalPhysicalMemoryAllocated Callback;
} tPhysicalAddressAllocationContext;


BOOLEAN ParaNdis_InitialAllocatePhysicalMemory(
    PARANDIS_ADAPTER *pContext,
    ULONG ulSize,
    tCompletePhysicalAddress *pAddresses);

VOID ParaNdis_FreePhysicalMemory(
    PARANDIS_ADAPTER *pContext,
    tCompletePhysicalAddress *pAddresses);

void ParaNdis_RestoreDeviceConfigurationAfterReset(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_UpdateDeviceFilters(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_DeviceFiltersUpdateVlanId(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_SynchronizeLinkState(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_SendGratuitousArpPacket(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_SetLinkState(
    PARANDIS_ADAPTER *pContext,
    NDIS_MEDIA_CONNECT_STATE LinkState);

#endif //-OFFLOAD_UNIT_TEST

typedef enum _tagppResult
{
    ppresNotTested = 0,
    ppresNotIP     = 1,
    ppresIPV4      = 2,
    ppresIPV6      = 3,
    ppresIPTooShort  = 1,
    ppresPCSOK       = 1,
    ppresCSOK        = 2,
    ppresCSBad       = 3,
    ppresXxpOther    = 1,
    ppresXxpKnown    = 2,
    ppresXxpIncomplete = 3,
    ppresIsTCP         = 0,
    ppresIsUDP         = 1,
}ppResult;

typedef union _tagTcpIpPacketParsingResult
{
    struct {
        /* 0 - not tested, 1 - not IP, 2 - IPV4, 3 - IPV6 */
        ULONG ipStatus        : 2;
        /* 0 - not tested, 1 - n/a, 2 - CS, 3 - bad */
        ULONG ipCheckSum      : 2;
        /* 0 - not tested, 1 - PCS, 2 - CS, 3 - bad */
        ULONG xxpCheckSum     : 2;
        /* 0 - not tested, 1 - other, 2 - known(contains basic TCP or UDP header), 3 - known incomplete */
        ULONG xxpStatus       : 2;
        /* 1 - contains complete payload */
        ULONG xxpFull         : 1;
        ULONG TcpUdp          : 1;
        ULONG fixedIpCS       : 1;
        ULONG fixedXxpCS      : 1;
        ULONG IsFragment      : 1;
        ULONG reserved        : 3;
        ULONG ipHeaderSize    : 8;
        ULONG XxpIpHeaderSize : 8;
    };
    ULONG value;
}tTcpIpPacketParsingResult;

typedef enum _tagPacketOffloadRequest
{
    pcrIpChecksum  = (1 << 0),
    pcrTcpV4Checksum = (1 << 1),
    pcrUdpV4Checksum = (1 << 2),
    pcrTcpV6Checksum = (1 << 3),
    pcrUdpV6Checksum = (1 << 4),
    pcrTcpChecksum = (pcrTcpV4Checksum | pcrTcpV6Checksum),
    pcrUdpChecksum = (pcrUdpV4Checksum | pcrUdpV6Checksum),
    pcrAnyChecksum = (pcrIpChecksum | pcrTcpV4Checksum | pcrUdpV4Checksum | pcrTcpV6Checksum | pcrUdpV6Checksum),
    pcrLSO   = (1 << 5),
    pcrIsIP  = (1 << 6),
    pcrFixIPChecksum = (1 << 7),
    pcrFixPHChecksum = (1 << 8),
    pcrFixTcpV4Checksum = (1 << 9),
    pcrFixUdpV4Checksum = (1 << 10),
    pcrFixTcpV6Checksum = (1 << 11),
    pcrFixUdpV6Checksum = (1 << 12),
    pcrFixXxpChecksum = (pcrFixTcpV4Checksum | pcrFixUdpV4Checksum | pcrFixTcpV6Checksum | pcrFixUdpV6Checksum),
    pcrPriorityTag = (1 << 13),
    pcrNoIndirect  = (1 << 14)
}tPacketOffloadRequest;

// sw offload

tTcpIpPacketParsingResult ParaNdis_CheckSumVerify(
                                                tCompletePhysicalAddress *pDataPages,
                                                ULONG ulDataLength,
                                                ULONG ulStartOffset,
                                                ULONG flags,
                                                BOOLEAN verifyLength,
                                                LPCSTR caller);

static __inline
tTcpIpPacketParsingResult ParaNdis_CheckSumVerifyFlat(
                                                PVOID pBuffer,
                                                ULONG ulDataLength,
                                                ULONG flags,
                                                BOOLEAN verifyLength,
                                                LPCSTR caller)
{
    tCompletePhysicalAddress SGBuffer;
    SGBuffer.Virtual = pBuffer;
    SGBuffer.size = ulDataLength;
    return ParaNdis_CheckSumVerify(&SGBuffer, ulDataLength, 0, flags, verifyLength, caller);
}

VOID _Function_class_(KDEFERRED_ROUTINE) MiniportMSIInterruptCXDpc(
    struct _KDPC  *Dpc,
    IN PVOID  MiniportInterruptContext,
    IN PVOID                  NdisReserved1,
    IN PVOID                  NdisReserved2
);

bool ParaNdis_RXTXDPCWorkBody(PARANDIS_ADAPTER *pContext,
    ULONG ulMaxPacketsToIndicate);


USHORT CheckSumCalculator(PVOID buffer, ULONG len);

tTcpIpPacketParsingResult ParaNdis_ReviewIPPacket(PVOID buffer, ULONG size, BOOLEAN verityLength, LPCSTR caller);

BOOLEAN ParaNdis_AnalyzeReceivedPacket(PVOID headersBuffer, ULONG dataLength, PNET_PACKET_INFO packetInfo);
ULONG ParaNdis_StripVlanHeaderMoveHead(PNET_PACKET_INFO packetInfo);
VOID ParaNdis_PadPacketToMinimalLength(PNET_PACKET_INFO packetInfo);
BOOLEAN ParaNdis_IsSendPossible(PARANDIS_ADAPTER *pContext);
NDIS_STATUS ParaNdis_ExactSendFailureStatus(PARANDIS_ADAPTER *pContext);

VOID ParaNdis_PropagateOid(PARANDIS_ADAPTER *pContext, NDIS_OID oid, PVOID buffer, UINT length);

void ParaNdis_PrintIndirectionTable(const NDIS_RECEIVE_SCALE_PARAMETERS* Params);
#endif
