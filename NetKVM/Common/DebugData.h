/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: DebugData.h
 *
 * This file contains definitions and data structures, common between
 * NDIS driver and debugger helper unit, processing crash dump with built-in
 * data provided by the driver.
 *
 * Included in NetKVM NDIS kernel driver for Windows.
 * Included in NetKVMDumpParser application.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#ifndef PARANDIS_DEBUG_DATA_H
#define PARANDIS_DEBUG_DATA_H

typedef enum _etagHistoryLogOperation
{
    hopPowerOff,                // common::PowerOff, 1/0 - entry/exit (none, entry, none, none)
    hopPowerOn,                 // common::PowerOn, 1/0 - entry/exit (none, entry, none, none)
    hopSysPause,                // ndis6::Pause, 1/0 - entry/completion
    hopSysResume,               // ndis6::Restart, 1/0 - entry/completion
    hopInternalSendPause,       // implementation, 1/0 - entry/completion
    hopInternalReceivePause,    // implementation, 1/0 - entry/completion
    hopInternalSendResume,      // implementation
    hopInternalReceiveResume,   // implementation
    hopSysReset,                // implementation driver, 1/0 - entry/completion
    hopHalt,                    // implementation driver, 1/0 - entry/completion
    hopConnectIndication,       // implementation
    hopDPC,                     // common::DpcWorkBody (1, none, none, none) (0, left, free buffers, free desc)
    hopSend,                    // implementation, when Send requested (nbl, nof lists, nof bufs, nof bytes) (packet, 1, nof packets, none)
    hopSendNBLRequest,          // ndis6 implementation (nbl, nof packets, none, none)
    hopSendPacketRequest,       // not used
    hopSendPacketMapped,        // implementation, before the packet inserted into queue (nbl, which packet, nof frags, none)
    hopSubmittedPacket,         // implementation, when the packet submitted (nbl, which packet, result, flags)
    hopBufferSent,              // implementation, when the packet returned from VirtIO queue (nbl, packet no., free buf, free desc)
    hopReceiveStat,             // common: RX (none, retrieved, reported, ready rx buffers)
    hopBufferReturned,          // not used
    hopSendComplete,            // implementation, when the packet completed
    hopTxProcess,
    hopPacketReceived,          // implementation, when the packet prepared for indication (nbl, length, prio tag, type)
    hopOidRequest,              // implementation, none, OID, on entry(type, 1), on exit (status, 0), on complete (status, 2)
    hopPnpEvent                 // common, none, event, 0, 0
}eHistoryLogOperation;

// {E51FCE18-B3E7-441e-B18C-D9E9B71616F3}
static const GUID ParaNdis_CrashGuid =
{ 0xe51fce18, 0xb3e7, 0x441e, { 0xb1, 0x8c, 0xd9, 0xe9, 0xb7, 0x16, 0x16, 0xf3 } };

/* This structure is NOT changeable */
typedef struct _tagBugCheckStaticDataHeader
{
    USHORT              SizeOfPointer;
    USHORT              StaticDataVersion;
    USHORT              PerNicDataVersion;
    USHORT              ulMaxContexts;
    LARGE_INTEGER       qCrashTime;
    UINT64              PerNicData;
    UINT64              DataArea;
    UINT64              DataAreaSize;
}tBugCheckStaticDataHeader;

/* This structure is NOT changeable */
typedef struct _tagBugCheckDataLocation
{
    UINT64              Address;
    UINT64              Size;
}tBugCheckDataLocation;

#define PARANDIS_DEBUG_STATIC_DATA_VERSION          0
#define PARANDIS_DEBUG_PER_NIC_DATA_VERSION         0
#define PARANDIS_DEBUG_HISTORY_DATA_VERSION         1
/* This structure is NOT changeable */
typedef struct _tagBugCheckStaticDataContent_V0
{
    ULONG               SizeOfHistory;
    ULONG               SizeOfHistoryEntry;
    LONG                CurrentHistoryIndex;
    ULONG               HistoryDataVersion;
    ULONG64             HistoryData;
}tBugCheckStaticDataContent_V0;

#define PARANDIS_DEBUG_INTERRUPTS

#ifdef PARANDIS_DEBUG_INTERRUPTS
#   define PARADNIS_STORE_LAST_INTERRUPT_TIMESTAMP(p) \
        NdisGetCurrentSystemTime(&(p)->LastInterruptTimeStamp)
#   define PARADNIS_GET_LAST_INTERRUPT_TIMESTAMP(p) \
        (p)->LastInterruptTimeStamp.QuadPart
#else
#   define PARADNIS_STORE_LAST_INTERRUPT_TIMESTAMP(p)
#   define PARADNIS_GET_LAST_INTERRUPT_TIMESTAMP(p) (0)
#endif

typedef struct _tagBugCheckPerNicDataContent_V0
{
    UINT64              Context;
    LARGE_INTEGER       LastInterruptTimeStamp;
    LARGE_INTEGER       LastTxCompletionTimeStamp;
    ULONG               nofReadyTxBuffers;
}tBugCheckPerNicDataContent_V0;

typedef struct _tagBugCheckHistoryDataEntry_V0
{
    LARGE_INTEGER       TimeStamp;
    UINT64              Context;
    UINT64              pParam1;
    ULONG               operation;
    ULONG               lParam2;
    ULONG               lParam3;
    ULONG               lParam4;
}tBugCheckHistoryDataEntry_V0;

typedef struct _tagBugCheckHistoryDataEntry_V1
{
    LARGE_INTEGER       TimeStamp;
    UINT64              Context;
    ULONG               uIRQL;
    ULONG               uProcessor;
    UINT64              pParam1;
    ULONG               operation;
    ULONG               lParam2;
    ULONG               lParam3;
    ULONG               lParam4;
}tBugCheckHistoryDataEntry_V1;


#if (PARANDIS_DEBUG_STATIC_DATA_VERSION == 0)
typedef tBugCheckStaticDataContent_V0 tBugCheckStaticDataContent;
#endif

#if (PARANDIS_DEBUG_PER_NIC_DATA_VERSION == 0)
typedef tBugCheckPerNicDataContent_V0 tBugCheckPerNicDataContent;
#endif

#if (PARANDIS_DEBUG_HISTORY_DATA_VERSION == 0)
typedef tBugCheckHistoryDataEntry_V0 tBugCheckHistoryDataEntry;
#elif (PARANDIS_DEBUG_HISTORY_DATA_VERSION == 1)
typedef tBugCheckHistoryDataEntry_V1 tBugCheckHistoryDataEntry;
#endif

typedef struct _tagBugCheckStaticDataContent_V1
{
    UINT64              res1;
    UINT64              res2;
    UINT64              History;
}tBugCheckStaticDataContent_V1;

typedef struct _tagBugCheckPerNicDataContent_V1
{
    UINT64              Context;
    LARGE_INTEGER       LastInterruptTimeStamp;
    LARGE_INTEGER       LastTxCompletionTimeStamp;
    ULONG               nofReadyTxBuffers;
}tBugCheckPerNicDataContent_V1;


#if (PARANDIS_DEBUG_HEADER_VERSION == 1)
typedef tBugCheckStaticDataContent_V1 tBugCheckStaticDataContent;
#endif

#if (PARANDIS_DEBUG_PER_NIC_DATA_VERSION == 1)
typedef tBugCheckPerNicDataContent_V1 tBugCheckPerNicDataContent;
#endif

// etc



#endif
