/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * File: ndis56common.h
 *
 * This file contains general definitions for VirtIO network adapter driver,
 * common for both NDIS5 and NDIS6
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#ifndef PARANDIS_56_COMMON_H
#define PARANDIS_56_COMMON_H

//#define PARANDIS_TEST_TX_KICK_ALWAYS
#if defined(OFFLOAD_UNIT_TEST)
#include <windows.h>
#include <stdio.h>

#define ETH_LENGTH_OF_ADDRESS       6
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
#define PARANDIS_SUPPORT_RSC 0 // Disable RSC support until support on the host side is ready
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
#include "ethernetutils.h"
#include "virtio_pci.h"
#include "VirtIO.h"
#include "IONetDescriptor.h"
#include "DebugData.h"
}

#if !defined(_Function_class_)
#define _Function_class_(x)
#endif

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

#include "ParaNdis-TX.h"
#include "ParaNdis-RX.h"
#include "ParaNdis-CX.h"

struct CPUPathesBundle : public CNdisAllocatable<CPUPathesBundle, 'CPPB'> {
    CParaNdisRX rxPath;
    bool        rxCreated = false;

    CParaNdisTX txPath;
    bool        txCreated = false;

    CParaNdisCX *cxPath = NULL;
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

/* The feature bitmap for virtio net */
#define VIRTIO_NET_F_CSUM   0   /* Host handles pkts w/ partial csum */
#define VIRTIO_NET_F_GUEST_CSUM 1   /* Guest handles pkts w/ partial csum */
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS 2 /* Control channel offload
                                         * configuration support */
#define VIRTIO_NET_F_MAC    5   /* Host has given MAC address. */
#define VIRTIO_NET_F_GSO    6   /* Host handles pkts w/ any GSO type */
#define VIRTIO_NET_F_GUEST_TSO4 7   /* Guest can handle TSOv4 in. */
#define VIRTIO_NET_F_GUEST_TSO6 8   /* Guest can handle TSOv6 in. */
#define VIRTIO_NET_F_GUEST_ECN  9   /* Guest can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_GUEST_UFO  10  /* Guest can handle UFO in. */
#define VIRTIO_NET_F_HOST_TSO4  11  /* Host can handle TSOv4 in. */
#define VIRTIO_NET_F_HOST_TSO6  12  /* Host can handle TSOv6 in. */
#define VIRTIO_NET_F_HOST_ECN   13  /* Host can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_HOST_UFO   14  /* Host can handle UFO in. */
#define VIRTIO_NET_F_MRG_RXBUF  15  /* Host can handle merged Rx buffers and requires bigger header for that. */
#define VIRTIO_NET_F_STATUS     16
#define VIRTIO_NET_F_CTRL_VQ    17      /* Control channel available */
#define VIRTIO_NET_F_CTRL_RX    18      /* Control channel RX mode support */
#define VIRTIO_NET_F_CTRL_VLAN  19      /* Control channel VLAN filtering */
#define VIRTIO_NET_F_CTRL_RX_EXTRA 20   /* Extra RX mode control support */
#define VIRTIO_NET_F_CTRL_MQ    22

#define VIRTIO_NET_F_CTRL_MAC_ADDR   23 /* Set MAC address */

#define VIRTIO_NET_S_LINK_UP    1       /* Link is up */

#define VIRTIO_NET_INVALID_INTERRUPT_STATUS     0xFF

#define PARANDIS_MULTICAST_LIST_SIZE        32
#define PARANDIS_MEMORY_TAG                 '5muQ'
#define PARANDIS_FORMAL_LINK_SPEED          (pContext->ulFormalLinkSpeed)
#define PARANDIS_MAXIMUM_TRANSMIT_SPEED     PARANDIS_FORMAL_LINK_SPEED
#define PARANDIS_MAXIMUM_RECEIVE_SPEED      PARANDIS_FORMAL_LINK_SPEED
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

typedef struct _tagAdapterResources
{
    ULONG ulIOAddress;
    ULONG IOLength;
    ULONG Vector;
    ULONG Level;
    KAFFINITY Affinity;
    ULONG InterruptFlags;
} tAdapterResources;

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

#pragma warning (push)
#pragma warning (disable:4201)
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
#pragma warning (pop)

typedef PMDL                tPacketHolderType;
typedef PNET_BUFFER_LIST    tPacketIndicationType;

typedef struct _tagOurCounters
{
    UINT nReusedRxBuffers;
    UINT nPrintDiagnostic;
    ULONG64 prevIn;
    UINT nRxInactivity;
}tOurCounters;

typedef struct _tagMaxPacketSize
{
    UINT nMaxDataSize;
    UINT nMaxFullSizeOS;
    UINT nMaxFullSizeHwTx;
    UINT nMaxDataSizeHwRx;
    UINT nMaxFullSizeOsRx;
}tMaxPacketSize;

#define MAX_HW_RX_PACKET_SIZE (MAX_IP4_DATAGRAM_SIZE + ETH_HEADER_SIZE + ETH_PRIORITY_HEADER_SIZE)
#define MAX_OS_RX_PACKET_SIZE (MAX_IP4_DATAGRAM_SIZE + ETH_HEADER_SIZE)


typedef struct _tagMulticastData
{
    ULONG                   nofMulticastEntries;
    UCHAR                   MulticastList[ETH_LENGTH_OF_ADDRESS * PARANDIS_MULTICAST_LIST_SIZE];
}tMulticastData;

#pragma warning (push)
#pragma warning (disable:4201)
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
#pragma warning (pop)

struct _tagRxNetDescriptor {
    LIST_ENTRY listEntry;
    LIST_ENTRY ReceiveQueueListEntry;

#define PARANDIS_FIRST_RX_DATA_PAGE   (1)
    struct VirtIOBufferDescriptor *BufferSGArray;
    tCompletePhysicalAddress      *PhysicalPages;
    ULONG                          PagesAllocated;
    tCompletePhysicalAddress       IndirectArea;
    tPacketHolderType              Holder;

    NET_PACKET_INFO PacketInfo;

    CParaNdisRX*                   Queue;
};

typedef struct _tagPARANDIS_RECEIVE_QUEUE
{
    NDIS_SPIN_LOCK          Lock;
    LIST_ENTRY              BuffersList;

    LONG                    ActiveProcessorsCount;
} PARANDIS_RECEIVE_QUEUE, *PPARANDIS_RECEIVE_QUEUE;

typedef struct _tagPARANDIS_ADAPTER
{
    NDIS_HANDLE             DriverHandle;
    NDIS_HANDLE             MiniportHandle;
    NDIS_HANDLE             InterruptHandle;
    NDIS_HANDLE             BufferListsPool;
    NDIS_EVENT              ResetEvent;
    tAdapterResources       AdapterResources;
    PVOID                   pIoPortOffset;
    VirtIODevice            *IODevice;
    LARGE_INTEGER           LastTxCompletionTimeStamp;
#ifdef PARANDIS_DEBUG_INTERRUPTS
    LARGE_INTEGER           LastInterruptTimeStamp;
#endif
    u32                     u32HostFeatures;
    u32                     u32GuestFeatures;
    BOOLEAN                 bConnected;
    NDIS_MEDIA_CONNECT_STATE fCurrentLinkState;
    BOOLEAN                 bEnableInterruptHandlingDPC;
    BOOLEAN                 bEnableInterruptChecking;
    BOOLEAN                 bDoSupportPriority;
    BOOLEAN                 bLinkDetectSupported;
    BOOLEAN                 bGuestChecksumSupported;
    BOOLEAN                 bUseMergedBuffers;
    BOOLEAN                 bDoPublishIndices;
    BOOLEAN                 bSurprizeRemoved;
    BOOLEAN                 bUsingMSIX;
    BOOLEAN                 bUseIndirect;
    BOOLEAN                 bAnyLaypout;
    BOOLEAN                 bHasHardwareFilters;
    BOOLEAN                 bNoPauseOnSuspend;
    BOOLEAN                 bFastSuspendInProcess;
    BOOLEAN                 bResetInProgress;
    BOOLEAN                 bCtrlMACAddrSupported;
    BOOLEAN                 bCfgMACAddrSupported;
    BOOLEAN                 bMultiQueue;
    USHORT                  nHardwareQueues;
    ULONG                   ulCurrentVlansFilterSet;
    tMulticastData          MulticastData;
    UINT                    uNumberOfHandledRXPacketsInDPC;
    NDIS_DEVICE_POWER_STATE powerState;
    LONG                    nPendingDPCs;
    LONG                    counterDPCInside;
    LONG                    bDPCInactive;
    ULONG                   ulPriorityVlanSetting;
    ULONG                   VlanId;
    ULONGLONG               ulFormalLinkSpeed;
    ULONG                   ulEnableWakeup;
    tMaxPacketSize          MaxPacketSize;
    ULONG                   ulUniqueID;
    UCHAR                   PermanentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR                   CurrentMacAddress[ETH_LENGTH_OF_ADDRESS];
    ULONG                   PacketFilter;
    ULONG                   DummyLookAhead;
    ULONG                   nDetectedStoppedTx;
    ULONG                   nDetectedInactivity;
    ULONG                   nVirtioHeaderSize;
    /* send part */
    NDIS_STATISTICS_INFO    Statistics;
    struct
    {
        ULONG framesCSOffload;
        ULONG framesLSO;
        ULONG framesIndirect;
        ULONG framesRxPriority;
        ULONG framesRxCSHwOK;
        ULONG framesRxCSHwMissedBad;
        ULONG framesRxCSHwMissedGood;
        ULONG framesFilteredOut;
    } extraStatistics;
    tOurCounters            Counters;
    tOurCounters            Limits;
    tSendReceiveState       SendState;
    tSendReceiveState       ReceiveState;
    ONPAUSECOMPLETEPROC     SendPauseCompletionProc;
    ONPAUSECOMPLETEPROC     ReceivePauseCompletionProc;

    CNdisRWLock             m_PauseLock;
    CNdisRefCounter         m_upstreamPacketPending;

    LONG                    ReuseBufferRegular;
    /* initial number of free Tx descriptor(from cfg) - max number of available Tx descriptors */
    UINT                    maxFreeTxDescriptors;
    /* total of Rx buffer in turnaround */
    UINT                    NetMaxReceiveBuffers;
    UINT                    nPnpEventIndex;
    NDIS_DEVICE_PNP_EVENT   PnpEvents[16];
    tOffloadSettings        Offload;
    NDIS_OFFLOAD_PARAMETERS InitialOffloadParameters;

    PARANDIS_RECEIVE_QUEUE      ReceiveQueues[PARANDIS_RSS_MAX_RECEIVE_QUEUES + 1];
    BOOLEAN                     ReceiveQueuesInitialized;
#define PARANDIS_RECEIVE_QUEUE_UNCLASSIFIED (0)
#define PARANDIS_FIRST_RSS_RECEIVE_QUEUE    (1)

    CParaNdisCX CXPath;
    BOOLEAN bCXPathAllocated;
    BOOLEAN bCXPathCreated;

    CPUPathesBundle             *pPathBundles;
    UINT                        nPathBundles;

    CPUPathesBundle            **RSS2QueueMap;
    USHORT                      RSS2QueueLength;

    PIO_INTERRUPT_MESSAGE_INFO  pMSIXInfoTable;
    NDIS_HANDLE                 DmaHandle;
    ULONG                       ulIrqReceived;
    NDIS_OFFLOAD                ReportedOffloadCapabilities;
    NDIS_OFFLOAD                ReportedOffloadConfiguration;
    BOOLEAN                     bOffloadv4Enabled;
    BOOLEAN                     bOffloadv6Enabled;
    BOOLEAN                     bDeviceInitialized;

#if PARANDIS_SUPPORT_RSS
    BOOLEAN                     bRSSOffloadSupported;
    BOOLEAN                     bRSSInitialized;
    NDIS_RECEIVE_SCALE_CAPABILITIES RSSCapabilities;
    PARANDIS_RSS_PARAMS         RSSParameters;
    CCHAR                       RSSMaxQueuesNumber;
#endif

#if PARANDIS_SUPPORT_RSC
    struct {
        BOOLEAN                     bIPv4SupportedSW;
        BOOLEAN                     bIPv6SupportedSW;
        BOOLEAN                     bIPv4SupportedHW;
        BOOLEAN                     bIPv6SupportedHW;
        BOOLEAN                     bIPv4Enabled;
        BOOLEAN                     bIPv6Enabled;
        BOOLEAN                     bHasDynamicConfig;
        struct {
            LARGE_INTEGER           CoalescedPkts;
            LARGE_INTEGER           CoalescedOctets;
            LARGE_INTEGER           CoalesceEvents;
        }                           Statistics;
    } RSC;
#endif

    _tagPARANDIS_ADAPTER(const _tagPARANDIS_ADAPTER&) = delete;
    _tagPARANDIS_ADAPTER& operator= (const _tagPARANDIS_ADAPTER&) = delete;
}PARANDIS_ADAPTER, *PPARANDIS_ADAPTER;

typedef struct _tagSynchronizedContext
{
    PARANDIS_ADAPTER    *pContext;
    PVOID               Parameter;
}tSynchronizedContext;

typedef BOOLEAN _Function_class_(MINIPORT_SYNCHRONIZE_INTERRUPT) (*tSynchronizedProcedure)(tSynchronizedContext *context);

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

BOOLEAN ParaNdis_ValidateMacAddress(
    PUCHAR pcMacAddress,
    BOOLEAN bLocal);

NDIS_STATUS ParaNdis_InitializeContext(
    PARANDIS_ADAPTER *pContext,
    PNDIS_RESOURCE_LIST ResourceList);

NDIS_STATUS ParaNdis_FinishInitialization(
    PARANDIS_ADAPTER *pContext);

NDIS_STATUS ParaNdis_ConfigureMSIXVectors(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_CleanupContext(
    PARANDIS_ADAPTER *pContext);

bool ParaNdis_DPCWorkBody(
    PARANDIS_ADAPTER *pContext,
    ULONG ulMaxPacketsToIndicate);

VOID ParaNdis_ResetRxClassification(
    PARANDIS_ADAPTER *pContext);

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

void ParaNdis_DeleteQueue(
    PARANDIS_ADAPTER *pContext, 
    struct virtqueue **ppq,
    tCompletePhysicalAddress *ppa);

void ParaNdis_FreeRxBufferDescriptor(
    PARANDIS_ADAPTER *pContext,
    pRxNetDescriptor p);

BOOLEAN ParaNdis_PerformPacketAnalyzis(
#if PARANDIS_SUPPORT_RSS
    PPARANDIS_RSS_PARAMS RSSParameters,
#endif
    PNET_PACKET_INFO PacketInfo,
    PVOID HeadersBuffer,
    ULONG DataLength);

CCHAR ParaNdis_GetScalingDataForPacket(
    PARANDIS_ADAPTER *pContext,
    PNET_PACKET_INFO pPacketInfo,
    PPROCESSOR_NUMBER pTargetProcessor);

#if PARANDIS_SUPPORT_RSS
NDIS_STATUS ParaNdis_SetupRSSQueueMap(PARANDIS_ADAPTER *pContext);
#endif

VOID ParaNdis_ReceiveQueueAddBuffer(
    PPARANDIS_RECEIVE_QUEUE pQueue,
    pRxNetDescriptor pBuffer);

VOID ParaMdis_TestPausing(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_ProcessorNumberToGroupAffinity(
    PGROUP_AFFINITY Affinity,
    const PPROCESSOR_NUMBER Processor);

VOID ParaNdis_QueueRSSDpc(
    PARANDIS_ADAPTER *pContext,
    PGROUP_AFFINITY pTargetAffinity);






static __inline BOOLEAN
ParaNDIS_IsQueueInterruptEnabled(struct virtqueue * _vq)
{
    return virtqueue_is_interrupt_enabled(_vq);
}


void ParaNdis_FreeRxBufferDescriptor(
    PARANDIS_ADAPTER *pContext,
    pRxNetDescriptor p);

BOOLEAN ParaNdis_PerformPacketAnalyzis(
#if PARANDIS_SUPPORT_RSS
    PPARANDIS_RSS_PARAMS RSSParameters,
#endif
    PNET_PACKET_INFO PacketInfo,
    PVOID HeadersBuffer,
    ULONG DataLength);

CCHAR ParaNdis_GetScalingDataForPacket(
    PARANDIS_ADAPTER *pContext,
    PNET_PACKET_INFO pPacketInfo,
    PPROCESSOR_NUMBER pTargetProcessor);

VOID ParaNdis_ReceiveQueueAddBuffer(
    PPARANDIS_RECEIVE_QUEUE pQueue,
    pRxNetDescriptor pBuffer);

VOID ParaNdis_ProcessorNumberToGroupAffinity(
    PGROUP_AFFINITY Affinity,
    const PPROCESSOR_NUMBER Processor);

VOID ParaNdis_QueueRSSDpc(
    PARANDIS_ADAPTER *pContext,
    ULONG MessageIndex,
    PGROUP_AFFINITY pTargetAffinity);

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

BOOLEAN ParaNdis_CheckForHang(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_PowerOn(
    PARANDIS_ADAPTER *pContext
);

VOID ParaNdis_PowerOff(
    PARANDIS_ADAPTER *pContext
);

#if PARANDIS_SUPPORT_RSC
VOID ParaNdis_UpdateGuestOffloads(
    PARANDIS_ADAPTER *pContext,
    UINT64 Offloads
);
#endif

void ParaNdis_DebugInitialize();
void ParaNdis_DebugCleanup(PDRIVER_OBJECT  pDriverObject);
void ParaNdis_DebugRegisterMiniport(PARANDIS_ADAPTER *pContext, BOOLEAN bRegister);


//#define ENABLE_HISTORY_LOG
#if !defined(ENABLE_HISTORY_LOG)

void FORCEINLINE ParaNdis_DebugHistory(
    PARANDIS_ADAPTER *pContext,
    eHistoryLogOperation op,
    PVOID pParam1,
    ULONG lParam2,
    ULONG lParam3,
    ULONG lParam4)
{
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(op);
    UNREFERENCED_PARAMETER(pParam1);
    UNREFERENCED_PARAMETER(lParam2);
    UNREFERENCED_PARAMETER(lParam3);
    UNREFERENCED_PARAMETER(lParam4);
}

#else

void ParaNdis_DebugHistory(
    PARANDIS_ADAPTER *pContext,
    eHistoryLogOperation op,
    PVOID pParam1,
    ULONG lParam2,
    ULONG lParam3,
    ULONG lParam4);

#endif

void ParaNdis_ResetOffloadSettings(PARANDIS_ADAPTER *pContext, tOffloadSettingsFlags *pDest, PULONG from);

tChecksumCheckResult ParaNdis_CheckRxChecksum(
                                            PARANDIS_ADAPTER *pContext,
                                            ULONG virtioFlags,
                                            tCompletePhysicalAddress *pPacketPages,
                                            ULONG ulPacketLength,
                                            ULONG ulDataOffset);

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

VOID ParaNdis_Suspend(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_Resume(
    PARANDIS_ADAPTER *pContext);

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
    tCompletePhysicalAddress *pAddresses);

BOOLEAN ParaNdis_RuntimeRequestToAllocatePhysicalMemory(
    PARANDIS_ADAPTER *pContext,
    tCompletePhysicalAddress *pAddresses,
    tOnAdditionalPhysicalMemoryAllocated Callback
    );

VOID ParaNdis_FreePhysicalMemory(
    PARANDIS_ADAPTER *pContext,
    tCompletePhysicalAddress *pAddresses);

BOOLEAN ParaNdis_BindRxBufferToPacket(
    PARANDIS_ADAPTER *pContext,
    pRxNetDescriptor p);

void ParaNdis_UnbindRxBufferFromPacket(
    pRxNetDescriptor p);

void ParaNdis_RestoreDeviceConfigurationAfterReset(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_UpdateDeviceFilters(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_DeviceFiltersUpdateVlanId(
    PARANDIS_ADAPTER *pContext);

VOID ParaNdis_SetPowerState(
    PARANDIS_ADAPTER *pContext,
    NDIS_DEVICE_POWER_STATE newState);

VOID ParaNdis_SynchronizeLinkState(
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

#pragma warning (push)
#pragma warning (disable:4201) //nonstandard extension used : nameless struct/union
#pragma warning (disable:4214) //nonstandard extension used : bit field types other than int
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
#pragma warning(pop)

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
                                                LPCSTR caller);

static __inline
tTcpIpPacketParsingResult ParaNdis_CheckSumVerifyFlat(
                                                PVOID pBuffer,
                                                ULONG ulDataLength,
                                                ULONG flags,
                                                LPCSTR caller)
{
    tCompletePhysicalAddress SGBuffer;
    SGBuffer.Virtual = pBuffer;
    SGBuffer.size = ulDataLength;
    return ParaNdis_CheckSumVerify(&SGBuffer, ulDataLength, 0, flags, caller);
}

tTcpIpPacketParsingResult ParaNdis_ReviewIPPacket(PVOID buffer, ULONG size, LPCSTR caller);

BOOLEAN ParaNdis_AnalyzeReceivedPacket(PVOID headersBuffer, ULONG dataLength, PNET_PACKET_INFO packetInfo);
ULONG ParaNdis_StripVlanHeaderMoveHead(PNET_PACKET_INFO packetInfo);
VOID ParaNdis_PadPacketToMinimalLength(PNET_PACKET_INFO packetInfo);
BOOLEAN ParaNdis_IsSendPossible(PARANDIS_ADAPTER *pContext);
NDIS_STATUS ParaNdis_ExactSendFailureStatus(PARANDIS_ADAPTER *pContext);

#endif
