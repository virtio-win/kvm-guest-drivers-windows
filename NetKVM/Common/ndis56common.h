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

#define ETH_LENGTH_OF_ADDRESS		6
#define DoPrint(fmt, ...) printf(fmt##"\n", __VA_ARGS__)
#define DPrintf(a,b) DoPrint b
#define RtlOffsetToPointer(B,O)  ((PCHAR)( ((PCHAR)(B)) + ((ULONG_PTR)(O))  ))

#include "ethernetutils.h"
#endif //+OFFLOAD_UNIT_TEST

#if !defined(OFFLOAD_UNIT_TEST)

#include <ntifs.h>
#include <ndis.h>
#include "osdep.h"
#include "kdebugprint.h"
#include "ethernetutils.h"
#include "virtio_pci.h"
#include "VirtIO.h"
#include "IONetDescriptor.h"
#include "DebugData.h"

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

//define to see when the status register is unreadable(see ParaNdis_ResetVirtIONetDevice)
//#define VIRTIO_RESET_VERIFY

//define to if hardware raise interrupt on error (see ParaNdis_DPCWorkBody)
//#define VIRTIO_SIGNAL_ERROR

// define if qemu supports logging to static IO port for synchronization
// of driver output with qemu printouts; in this case define the port number
// #define VIRTIO_DBG_USE_IOPORT	0x99

// to be set to real limit later 
#define MAX_RX_LOOPS	1000

/* The feature bitmap for virtio net */
#define VIRTIO_NET_F_CSUM	0	/* Host handles pkts w/ partial csum */
#define VIRTIO_NET_F_GUEST_CSUM	1	/* Guest handles pkts w/ partial csum */
#define VIRTIO_NET_F_MAC	5	/* Host has given MAC address. */
#define VIRTIO_NET_F_GSO	6	/* Host handles pkts w/ any GSO type */
#define VIRTIO_NET_F_GUEST_TSO4	7	/* Guest can handle TSOv4 in. */
#define VIRTIO_NET_F_GUEST_TSO6	8	/* Guest can handle TSOv6 in. */
#define VIRTIO_NET_F_GUEST_ECN	9	/* Guest can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_GUEST_UFO	10	/* Guest can handle UFO in. */
#define VIRTIO_NET_F_HOST_TSO4	11	/* Host can handle TSOv4 in. */
#define VIRTIO_NET_F_HOST_TSO6	12	/* Host can handle TSOv6 in. */
#define VIRTIO_NET_F_HOST_ECN	13	/* Host can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_HOST_UFO	14	/* Host can handle UFO in. */
#define VIRTIO_NET_F_MRG_RXBUF	15  /* Host can handle merged Rx buffers and requires bigger header for that. */
#define VIRTIO_NET_F_STATUS     16


#define VIRTIO_NET_S_LINK_UP    1       /* Link is up */

#define VIRTIO_NET_INVALID_INTERRUPT_STATUS		0xFF

#define PARANDIS_MULTICAST_LIST_SIZE		32
#define PARANDIS_MEMORY_TAG					'5muQ'
#define PARANDIS_FORMAL_LINK_SPEED			(pContext->ulFormalLinkSpeed)
#define PARANDIS_MAXIMUM_TRANSMIT_SPEED		PARANDIS_FORMAL_LINK_SPEED
#define PARANDIS_MAXIMUM_RECEIVE_SPEED		PARANDIS_FORMAL_LINK_SPEED
#define PARANDIS_MIN_LSO_SEGMENTS			2
#define PARANDIS_MAX_LSO_SIZE				0xF000

typedef enum _tagInterruptSource 
{ 
	isReceive  = 0x01, 
	isTransmit = 0x02, 
	isControl  = 0x04,
	isUnknown  = 0x08,
	isBothTransmitReceive = isReceive | isTransmit, 
	isAny      = isReceive | isTransmit | isControl | isUnknown,
	isDisable  = 0x80
}tInterruptSource;

static const ULONG PARANDIS_PACKET_FILTERS =
	NDIS_PACKET_TYPE_DIRECTED |
	NDIS_PACKET_TYPE_MULTICAST |
	NDIS_PACKET_TYPE_BROADCAST |
	NDIS_PACKET_TYPE_PROMISCUOUS |
	NDIS_PACKET_TYPE_ALL_MULTICAST;

typedef VOID (*ONPAUSECOMPLETEPROC)(VOID *);


typedef enum _tagSendReceiveState
{
	srsDisabled = 0,		// initial state
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
	osbT4IpChecksum = 1,
	osbT4TcpChecksum = 2,
	osbT4UdpChecksum = 4,
	osbT4TcpOptionsChecksum = 8,
	osbT4IpOptionsChecksum = 0x10,
	osbT4Lso = 0x20,
	osbT4LsoIp = 0x40,
	osbT4LsoTcp = 0x80
}tOffloadSettingsBit;

typedef struct _tagOffloadSettingsFlags
{
	ULONG fTxIPChecksum		: 1;
	ULONG fTxTCPChecksum	: 1;
	ULONG fTxUDPChecksum	: 1;
	ULONG fTxTCPOptions		: 1;
	ULONG fTxIPOptions		: 1;
	ULONG fTxLso			: 1;
	ULONG fTxLsoIP			: 1;
	ULONG fTxLsoTCP			: 1;
	ULONG fRxIPChecksum		: 1;
	ULONG fRxTCPChecksum	: 1;
	ULONG fRxUDPChecksum	: 1;
	ULONG fRxTCPOptions		: 1;
	ULONG fRxIPOptions		: 1;
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

/*
for simplicity, we use for NDIS5 the same statistics as native NDIS6 uses
*/
#if !NDIS60_MINIPORT
typedef struct _tagNdisStatustics
{
    ULONG64                     ifHCInOctets;
    ULONG64                     ifHCInUcastPkts;
    ULONG64                     ifHCInUcastOctets;
    ULONG64                     ifHCInMulticastPkts;
    ULONG64                     ifHCInMulticastOctets;
    ULONG64                     ifHCInBroadcastPkts;
    ULONG64                     ifHCInBroadcastOctets;
    ULONG64                     ifInDiscards;
    ULONG64                     ifInErrors;
    ULONG64                     ifHCOutOctets;
    ULONG64                     ifHCOutUcastPkts;
    ULONG64                     ifHCOutUcastOctets;
    ULONG64                     ifHCOutMulticastPkts;
    ULONG64                     ifHCOutMulticastOctets;
    ULONG64                     ifHCOutBroadcastPkts;
    ULONG64                     ifHCOutBroadcastOctets;
    ULONG64                     ifOutDiscards;
    ULONG64                     ifOutErrors;
}NDIS_STATISTICS_INFO;

typedef PNDIS_PACKET tPacketType;
typedef PNDIS_PACKET tPacketHolderType;
typedef PNDIS_PACKET tPacketIndicationType;

typedef struct _tagNdisOffloadParams
{
    UCHAR	IPv4Checksum;
    UCHAR	TCPIPv4Checksum;
    UCHAR	UDPIPv4Checksum;
    UCHAR	LsoV1;
    UCHAR	LsoV2IPv4;
    UCHAR	TCPIPv6Checksum;
    UCHAR	UDPIPv6Checksum;
    UCHAR	LsoV2IPv6;
}NDIS_OFFLOAD_PARAMETERS;

#else // NDIS60_MINIPORT

typedef PNET_BUFFER			tPacketType;
typedef PMDL				tPacketHolderType;
typedef PNET_BUFFER_LIST	tPacketIndicationType;

#endif	//!NDIS60_MINIPORT

//#define UNIFY_LOCKS

typedef struct _tagOurCounters
{
	UINT nReusedRxBuffers;
	UINT nPrintDiagnostic;
}tOurCounters;

typedef struct _tagMaxPacketSize
{
	UINT nMaxDataSize;
	UINT nMaxFullSizeOS;
	UINT nMaxFullSizeHwTx;
	UINT nMaxFullSizeHwRx;
}tMaxPacketSize;

typedef struct _tagPARANDIS_ADAPTER
{
	NDIS_HANDLE				DriverHandle;
	NDIS_HANDLE				MiniportHandle;
	NDIS_HANDLE				InterruptHandle;
	NDIS_HANDLE				BufferListsPool;
	NDIS_EVENT				ResetEvent;
	tAdapterResources		AdapterResources;
	PVOID					pIoPortOffset;
	VirtIODevice			IODevice;
	LARGE_INTEGER			LastTxCompletionTimeStamp;
	LARGE_INTEGER			LastInterruptTimeStamp;
	BOOLEAN					bConnected;
	BOOLEAN					bEnableInterruptHandlingDPC;
	BOOLEAN					bEnableInterruptChecking;
	BOOLEAN					bDoInterruptRecovery;
	BOOLEAN					bDoHardReset;
	BOOLEAN					bDoSupportPriority;
	BOOLEAN					bDoPacketFiltering;
	BOOLEAN					bUseScatterGather;
	BOOLEAN					bBatchReceive;
	BOOLEAN					bLinkDetectSupported;
	BOOLEAN					bDoHardwareOffload;
	BOOLEAN					bDoIPCheck;
	BOOLEAN					bFixIPChecksum;
	BOOLEAN					bUseMergedBuffers;
	BOOLEAN					bDoPublishIndices;
	BOOLEAN					bDoKickOnNoBuffer;
	BOOLEAN					bSurprizeRemoved;
	BOOLEAN					bUsingMSIX;
	NDIS_DEVICE_POWER_STATE powerState;
	LONG 					counterDPCInside;
	LONG 					bDPCInactive;
	LONG					InterruptStatus;
	ULONG					ulIrqReceived;
	ULONG					ulPriorityVlanSetting;
	ULONG					VlanId;
	ULONG					ulFormalLinkSpeed;
	ULONG					ulEnableWakeup;
	tMaxPacketSize			MaxPacketSize;
	ULONG					nEnableDPCChecker;
	ULONG					ulUniqueID;
	UCHAR					PermanentMacAddress[ETH_LENGTH_OF_ADDRESS];
	UCHAR					CurrentMacAddress[ETH_LENGTH_OF_ADDRESS];
	ULONG					PacketFilter;
	ULONG					DummyLookAhead;
	UCHAR					MulticastList[ETH_LENGTH_OF_ADDRESS * PARANDIS_MULTICAST_LIST_SIZE];
	ULONG					MulticastListSize;
	ULONG					ulMilliesToConnect;
	ULONG					nDetectedStoppedTx;
	ULONG					nDetectedInactivity;
	ULONG					nVirtioHeaderSize;
	/* send part */
#if !defined(UNIFY_LOCKS)
	NDIS_SPIN_LOCK			SendLock;
	NDIS_SPIN_LOCK			ReceiveLock;
#else
	union
	{
	NDIS_SPIN_LOCK			SendLock;
	NDIS_SPIN_LOCK			ReceiveLock;
	};
#endif
	NDIS_STATISTICS_INFO	Statistics;
	tOurCounters			Counters;
	tOurCounters			Limits;
	UINT					uRXPacketsDPCLimit;
	tSendReceiveState		SendState;
	tSendReceiveState		ReceiveState;
	ONPAUSECOMPLETEPROC		SendPauseCompletionProc;
	ONPAUSECOMPLETEPROC		ReceivePauseCompletionProc;
	/* Net part - management of buffers and queues of QEMU */
	struct virtqueue *		NetReceiveQueue;
	struct virtqueue *		NetSendQueue;
	/* list of Rx buffers available for data (under VIRTIO management) */
	LIST_ENTRY				NetReceiveBuffers;
	UINT					NetNofReceiveBuffers;
	/* list of Rx buffers waiting for return (under NDIS management) */
	LIST_ENTRY				NetReceiveBuffersWaiting;
	/* list of Tx buffers in process (under VIRTIO management) */
	LIST_ENTRY				NetSendBuffersInUse;
	/* list of Tx buffers ready for data (under MINIPORT management) */
	LIST_ENTRY				NetFreeSendBuffers;
	/* current number of free Tx descriptors */
	UINT					nofFreeTxDescriptors;
	/* initial number of free Tx descriptor(from cfg) - max number of available Tx descriptors */
	UINT					maxFreeTxDescriptors;
	/* current number of free Tx buffers, which can be submitted */
	UINT					nofFreeHardwareBuffers;
	/* maximal number of free Tx buffers, which can be used by SG */
	UINT					maxFreeHardwareBuffers;
	/* minimal number of free Tx buffers */
	UINT					minFreeHardwareBuffers;
	/* current number of Tx packets (or lists) to return */
	LONG					NetTxPacketsToReturn;
	/* total of Rx buffer in turnaround */
	UINT					NetMaxReceiveBuffers;
	struct VirtIOBufferDescriptor *sgTxGatherTable;
	UINT					nPnpEventIndex;
	NDIS_DEVICE_PNP_EVENT	PnpEvents[16];
	tOffloadSettings		Offload;
	NDIS_OFFLOAD_PARAMETERS	InitialOffloadParameters;
	// we keep these members common for XP and Vista
	// for XP and non-MSI case of Vista they are set to zero
	ULONG						ulRxMessage;
	ULONG						ulTxMessage;
	ULONG						ulControlMessage;

#if NDIS60_MINIPORT
// Vista +
	PIO_INTERRUPT_MESSAGE_INFO	pMSIXInfoTable;
	PNET_BUFFER_LIST			SendHead;
	PNET_BUFFER_LIST			SendTail;
	PNET_BUFFER_LIST			SendWaitingList;
	LIST_ENTRY					WaitingMapping;
	NDIS_HANDLE					DmaHandle;
	NDIS_HANDLE					ConnectTimer;
	NDIS_HANDLE					InterruptRecoveryTimer;
	NDIS_OFFLOAD				ReportedOffloadCapabilities;
	NDIS_OFFLOAD				ReportedOffloadConfiguration;
	BOOLEAN						bOffloadEnabled;
#else
// Vista -
	NDIS_MINIPORT_INTERRUPT		Interrupt;
	NDIS_HANDLE					PacketPool;
	NDIS_HANDLE					BuffersPool;
	NDIS_HANDLE					WrapperConfigurationHandle;
	LIST_ENTRY					SendQueue;
	LIST_ENTRY					TxWaitingList;
	NDIS_EVENT					HaltEvent;
	NDIS_TIMER					ConnectTimer;
	NDIS_TIMER					DPCPostProcessTimer;
	BOOLEAN						bDmaInitialized;
#endif
}PARANDIS_ADAPTER, *PPARANDIS_ADAPTER;


typedef struct _tagCopyPacketResult
{
	ULONG		size;
	enum tCopyPacketError { cpeOK, cpeNoBuffer, cpeInternalError, cpeTooLarge } error;
}tCopyPacketResult;

typedef struct _tagSynchronizedContext
{
	PARANDIS_ADAPTER *pContext;
	ULONG			 Parameter;
}tSynchronizedContext;

typedef BOOLEAN (*tSynchronizedProcedure)(tSynchronizedContext *context);

/**********************************************************
LAZZY release procedure returns buffers to VirtIO
only where there are no free buffers available

NON-LAZZY release releases transmit buffers from VirtIO
library every time there is something to release
***********************************************************/
//#define LAZZY_TX_RELEASE

BOOLEAN FORCEINLINE IsTimeToReleaseTx(PARANDIS_ADAPTER *pContext)
{
#ifndef LAZZY_TX_RELEASE
	return pContext->nofFreeTxDescriptors < pContext->maxFreeTxDescriptors;
#else
	return pContext->nofFreeTxDescriptors == 0;
#endif
}

BOOLEAN FORCEINLINE IsValidVlanId(PARANDIS_ADAPTER *pContext, ULONG VlanID)
{
	return pContext->VlanId == 0 || pContext->VlanId == VlanID;
}

typedef struct _tagCompletePhysicalAddress
{
	PHYSICAL_ADDRESS	Physical;
	PVOID				Virtual;
	ULONG				IsCached		: 1;
	ULONG				IsTX			: 1;
	// the size limit will be 1G instead of 4G
	ULONG				size			: 30;
} tCompletePhysicalAddress;

typedef struct _tagIONetDescriptor {
	LIST_ENTRY listEntry;
	tCompletePhysicalAddress HeaderInfo;
	tCompletePhysicalAddress DataInfo;
	tPacketHolderType pHolder;
	PVOID ReferenceValue;
	UINT  nofUsedBuffers;
} IONetDescriptor, * pIONetDescriptor;

BOOLEAN ParaNdis_ValidateMacAddress(
	PUCHAR pcMacAddress,
	BOOLEAN bLocal);

NDIS_STATUS ParaNdis_InitializeContext(
	PARANDIS_ADAPTER *pContext,
	PNDIS_RESOURCE_LIST ResourceList);

NDIS_STATUS ParaNdis_FinishInitialization(
	PARANDIS_ADAPTER *pContext);

VOID ParaNdis_CleanupContext(
	PARANDIS_ADAPTER *pContext);


void ParaNdis_VirtIONetReuseRecvBuffer(
	PARANDIS_ADAPTER *pContext,
	void *pDescriptor);

UINT ParaNdis_VirtIONetReleaseTransmitBuffers(
	PARANDIS_ADAPTER *pContext);

ULONG ParaNdis_DPCWorkBody(
	PARANDIS_ADAPTER *pContext);

NDIS_STATUS ParaNdis_SetMulticastList(
	PARANDIS_ADAPTER *pContext,
	PVOID Buffer,
	ULONG BufferSize,
	PUINT pBytesRead,
	PUINT pBytesNeeded);

VOID ParaNdis_VirtIOEnableIrqSynchronized(
	PARANDIS_ADAPTER *pContext,
	ULONG interruptSource,
	BOOLEAN b);

VOID ParaNdis_OnPnPEvent(
	PARANDIS_ADAPTER *pContext,
	NDIS_DEVICE_PNP_EVENT pEvent,
	PVOID	pInfo,
	ULONG	ulSize);

BOOLEAN ParaNdis_OnInterrupt(
	PARANDIS_ADAPTER *pContext,
	BOOLEAN *pRunDpc,
	ULONG knownInterruptSources);

VOID ParaNdis_OnShutdown(
	PARANDIS_ADAPTER *pContext);

BOOLEAN ParaNdis_CheckForHang(
	PARANDIS_ADAPTER *pContext);

void ParaNdis_ReportLinkStatus(
	PARANDIS_ADAPTER *pContext);
VOID ParaNdis_PowerOn(
	PARANDIS_ADAPTER *pContext
);

VOID ParaNdis_PowerOff(
	PARANDIS_ADAPTER *pContext
);

void ParaNdis_DebugInitialize(PVOID DriverObject,PVOID RegistryPath);
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

typedef struct _tagTxOperationParameters
{
	tPacketType		packet;
	PVOID			ReferenceValue;
	UINT			nofSGFragments;
	ULONG			ulDataSize;
	ULONG			offloalMss;
	ULONG			flags;		//see tPacketOffloadRequest
}tTxOperationParameters;

tCopyPacketResult ParaNdis_DoCopyPacketData(
	PARANDIS_ADAPTER *pContext,
	const tTxOperationParameters *pParams);

typedef struct _tagMapperResult
{
	ULONG	nBuffersMapped	: 8;
	ULONG	ulDataSize		: 24;
}tMapperResult;


tCopyPacketResult ParaNdis_DoSubmitPacket(PARANDIS_ADAPTER *pContext, tTxOperationParameters *Params);

void ParaNdis_ResetOffloadSettings(PARANDIS_ADAPTER *pContext, tOffloadSettingsFlags *pDest, PULONG from);

void ParaNdis_CallOnBugCheck(PARANDIS_ADAPTER *pContext);

/*****************************************************
Procedures to implement for NDIS specific implementation
******************************************************/

PVOID ParaNdis_AllocateMemory(
	PARANDIS_ADAPTER *pContext,
	ULONG ulRequiredSize);

NDIS_STATUS ParaNdis_FinishSpecificInitialization(
	PARANDIS_ADAPTER *pContext);

VOID ParaNdis_FinalizeCleanup(
	PARANDIS_ADAPTER *pContext);

NDIS_HANDLE ParaNdis_OpenNICConfiguration(
	PARANDIS_ADAPTER *pContext);

tPacketIndicationType ParaNdis_IndicateReceivedPacket(
	PARANDIS_ADAPTER *pContext,
	PVOID dataBuffer,
	PULONG pLength,
	BOOLEAN bPrepareOnly,
	pIONetDescriptor pBufferDesc);

VOID ParaNdis_IndicateReceivedBatch(
	PARANDIS_ADAPTER *pContext,
	tPacketIndicationType *pBatch,
	ULONG nofPackets);


tMapperResult ParaNdis_PacketMapper(
	PARANDIS_ADAPTER *pContext,
	tPacketType packet,
	PVOID Reference,
	struct VirtIOBufferDescriptor *buffers,
	pIONetDescriptor pDesc
	);

tCopyPacketResult ParaNdis_PacketCopier(
	tPacketType packet,
	PVOID dest,
	ULONG maxSize,
	PVOID refValue,
	BOOLEAN bPreview);

VOID ParaNdis_ProcessTx(
	PARANDIS_ADAPTER *pContext,
	BOOLEAN IsDpc);

BOOLEAN ParaNdis_SetTimer(
	NDIS_HANDLE timer,
	LONG millies);

BOOLEAN ParaNdis_SynchronizeWithInterrupt(
	PARANDIS_ADAPTER *pContext,
	ULONG messageId,
	tSynchronizedProcedure procedure,
	ULONG parameter);

VOID ParaNdis_Suspend(
	PARANDIS_ADAPTER *pContext);

VOID ParaNdis_Resume(
	PARANDIS_ADAPTER *pContext);

VOID ParaNdis_OnTransmitBufferReleased(
	PARANDIS_ADAPTER *pContext,
	IONetDescriptor *pDesc);


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

BOOLEAN ParaNdis_BindBufferToPacket(
	PARANDIS_ADAPTER *pContext,
	pIONetDescriptor pBufferDesc);

void ParaNdis_UnbindBufferFromPacket(
	PARANDIS_ADAPTER *pContext,
	pIONetDescriptor pBufferDesc);

void ParaNdis_IndicateConnect(
	PARANDIS_ADAPTER *pContext,
	BOOLEAN bConnected,
	BOOLEAN bForce);

void ParaNdis_RestoreDeviceConfigurationAfterReset(
	PARANDIS_ADAPTER *pContext);

#endif //-OFFLOAD_UNIT_TEST

typedef enum _tagppResult
{
	ppresNotTested = 0,
	ppresNotIP     = 1,
	ppresIPV4      = 2,
	ppresIPforFuture = 3,
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
		/* 0 - not tested, 1 - not IP, 2 - IPV4, 3 -n/a */
		ULONG ipStatus			: 2;
		/* 0 - not tested, 1 - n/a, 2 - CS, 3 - bad */
		ULONG ipCheckSum		: 2;
		/* 0 - not tested, 1 - PCS, 2 - CS, 3 - bad */
		ULONG xxpCheckSum		: 2;
		/* 0 - not tested, 1 - other, 2 - known(contains basic TCP or UDP header), 3 - known incomplete */
		ULONG xxpStatus			: 2;
		/* 1 - contains complete payload */
		ULONG xxpFull			: 1;
		ULONG TcpUdp			: 1;
		ULONG fixedIpCS			: 1;
		ULONG fixedXxpCS		: 1;
		ULONG ipHeaderSize		: 8;
		ULONG XxpIpHeaderSize	: 8;
	};
	ULONG value;
}tTcpIpPacketParsingResult;

typedef enum _tagPacketOffloadRequest
{
	pcrIpChecksum  = 1,
	pcrTcpChecksum = 2,
	pcrUdpChecksum = 4,
	pcrAnyChecksum = (pcrIpChecksum | pcrTcpChecksum | pcrUdpChecksum),
	pcrLSO   = 0x10,
	pcrIsIP  = 0x40,
	pcrFixIPChecksum = 0x100,
	pcrFixPHChecksum = 0x200,
	pcrFixXxpChecksum = 0x400,
	pcrPriorityTag = 0x800
}tPacketOffloadRequest;

// sw offload
void ParaNdis_CheckSumCalculate(PVOID buffer, ULONG size, ULONG flags);
tTcpIpPacketParsingResult ParaNdis_CheckSumVerify(PVOID buffer, ULONG size, ULONG flags, LPCSTR caller);
tTcpIpPacketParsingResult ParaNdis_ReviewIPPacket(PVOID buffer, ULONG size, LPCSTR caller);


#endif
