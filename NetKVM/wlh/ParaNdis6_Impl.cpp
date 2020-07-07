/*
 * This file contains NDIS6-specific implementation of driver's procedures.
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
#include "ParaNdis6.h"
#include "kdebugprint.h"
#include "ParaNdis_DebugHistory.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis6_Impl.tmh"
#endif

static MINIPORT_DISABLE_INTERRUPT MiniportDisableInterruptEx;
static MINIPORT_ENABLE_INTERRUPT MiniportEnableInterruptEx;
static MINIPORT_INTERRUPT_DPC MiniportInterruptDPC;
static MINIPORT_ISR MiniportInterrupt;
static MINIPORT_ENABLE_MESSAGE_INTERRUPT MiniportEnableMSIInterrupt;
static MINIPORT_DISABLE_MESSAGE_INTERRUPT MiniportDisableMSIInterrupt;
static MINIPORT_MESSAGE_INTERRUPT MiniportMSIInterrupt;
static MINIPORT_MESSAGE_INTERRUPT_DPC MiniportMSIInterruptDpc;
static MINIPORT_PROCESS_SG_LIST ProcessSGListHandler;
static MINIPORT_ALLOCATE_SHARED_MEM_COMPLETE SharedMemAllocateCompleteHandler;

static MINIPORT_PROCESS_SG_LIST ProcessSGListHandler;

/**********************************************************
Implements general-purpose memory allocation routine
Parameters:
    ULONG ulRequiredSize: block size
Return value:
    PVOID allocated memory block
    NULL on error
***********************************************************/
PVOID ParaNdis_AllocateMemoryRaw(NDIS_HANDLE MiniportHandle, ULONG ulRequiredSize)
{
    return NdisAllocateMemoryWithTagPriority(
            MiniportHandle,
            ulRequiredSize,
            PARANDIS_MEMORY_TAG,
            NormalPoolPriority);
}

PVOID ParaNdis_AllocateMemory(PARANDIS_ADAPTER *pContext, ULONG ulRequiredSize)
{
    return ParaNdis_AllocateMemoryRaw(pContext->MiniportHandle, ulRequiredSize);
}

/**********************************************************
Implements opening of adapter-specific configuration
Parameters:

Return value:
    NDIS_HANDLE Handle of open configuration
    NULL on error
***********************************************************/
NDIS_HANDLE ParaNdis_OpenNICConfiguration(PARANDIS_ADAPTER *pContext)
{
    NDIS_CONFIGURATION_OBJECT co;
    NDIS_HANDLE cfg;
    NDIS_STATUS status;
    DEBUG_ENTRY(2);
    co.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    co.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    co.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
    co.Flags = 0;
    co.NdisHandle = pContext->MiniportHandle;
    status = NdisOpenConfigurationEx(&co, &cfg);
    if (status != NDIS_STATUS_SUCCESS)
        cfg = NULL;
    DEBUG_EXIT_STATUS(status == NDIS_STATUS_SUCCESS ? 2 : 0, status);
    return cfg;
}

/**********************************************************
NDIS6 implementation of shared memory allocation
Parameters:
    context
    tCompletePhysicalAddress *pAddresses
            the structure accumulates all our knowledge
            about the allocation (size, addresses, cacheability etc)
Return value:
    TRUE if the allocation was successful
***********************************************************/
BOOLEAN ParaNdis_InitialAllocatePhysicalMemory(
    PARANDIS_ADAPTER *pContext,
    ULONG ulSize,
    tCompletePhysicalAddress *pAddresses)
{
#if defined(_ARM64_)
    UNREFERENCED_PARAMETER(pContext);
    /*
     * On Windows on Arm, do not use NdisMAllocateSharedMemory.
     * TODO: Figure out a neater way to allocate memory in those cases.
     */
    LARGE_INTEGER bound1, bound2;
    bound1.QuadPart = 0;
    bound2.QuadPart = MAXUINT64;
    pAddresses->Virtual = MmAllocateContiguousMemorySpecifyCache(ulSize, bound1, bound2, bound1, MmCached);
    pAddresses->Physical = MmGetPhysicalAddress(pAddresses->Virtual);
#else
    NdisMAllocateSharedMemory(
        pContext->MiniportHandle,
        ulSize,
        TRUE,
        &pAddresses->Virtual,
        &pAddresses->Physical);
#endif
    if (pAddresses->Virtual != NULL)
    {
        pAddresses->size = ulSize;
        return TRUE;
    }
    return FALSE;
}

/**********************************************************
NDIS6 implementation of shared memory freeing
Parameters:
    context
    tCompletePhysicalAddress *pAddresses
            the structure accumulates all our knowledge
            about the allocation (size, addresses, cacheability etc)
            filled by ParaNdis_InitialAllocatePhysicalMemory
***********************************************************/
VOID ParaNdis_FreePhysicalMemory(
    PARANDIS_ADAPTER *pContext,
    tCompletePhysicalAddress *pAddresses)
{
#if defined(_ARM64_)
    UNREFERENCED_PARAMETER(pContext);
    MmFreeContiguousMemorySpecifyCache(pAddresses->Virtual, pAddresses->size, MmCached);
#else
    NdisMFreeSharedMemory(
        pContext->MiniportHandle,
        pAddresses->size,
        TRUE,
        pAddresses->Virtual,
        pAddresses->Physical);
#endif
}

#if (NDIS_SUPPORT_NDIS620)
typedef MINIPORT_SYNCHRONIZE_INTERRUPT_HANDLER NDIS_SYNC_PROC_TYPE;
#else
typedef PVOID NDIS_SYNC_PROC_TYPE;
#endif


BOOLEAN ParaNdis_SynchronizeWithInterrupt(
    PARANDIS_ADAPTER *pContext,
    ULONG messageId,
    tSynchronizedProcedure procedure,
    PVOID parameter)
{
    NDIS_SYNC_PROC_TYPE syncProc;
    syncProc = (NDIS_SYNC_PROC_TYPE) procedure;
    return NdisMSynchronizeWithInterruptEx(pContext->InterruptHandle, messageId, syncProc, parameter);
}

/**********************************************************
NDIS-required procedure for hardware interrupt registration
Parameters:
    IN PVOID MiniportInterruptContext (actually Adapter context)
***********************************************************/
static VOID MiniportDisableInterruptEx(IN PVOID MiniportInterruptContext)
{
    DEBUG_ENTRY(0);
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;

    /* TODO - make sure that interrups are not reenabled by the DPC callback*/
    for (UINT i = 0; i < pContext->nPathBundles; i++)
    {
        pContext->pPathBundles[i].txPath.DisableInterrupts();
        pContext->pPathBundles[i].rxPath.DisableInterrupts();
    }
    if (pContext->bCXPathCreated)
    {
        pContext->CXPath.DisableInterrupts();
    }
}

/**********************************************************
NDIS-required procedure for hardware interrupt registration
Parameters:
    IN PVOID MiniportInterruptContext (actually Adapter context)
***********************************************************/
static VOID MiniportEnableInterruptEx(IN PVOID MiniportInterruptContext)
{
    DEBUG_ENTRY(0);
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;

    for (UINT i = 0; i < pContext->nPathBundles; i++)
    {
        pContext->pPathBundles[i].txPath.EnableInterrupts();
        pContext->pPathBundles[i].rxPath.EnableInterrupts();
    }
    if (pContext->bCXPathCreated)
    {
        pContext->CXPath.EnableInterrupts();
    }
}

/**********************************************************
NDIS-required procedure for hardware interrupt handling
Parameters:
    IN PVOID  MiniportInterruptContext (actually Adapter context)
    OUT PBOOLEAN  QueueDefaultInterruptDpc - set to TRUE for default DPC spawning
    OUT PULONG  TargetProcessors
Return value:
    TRUE if recognized
***********************************************************/
static BOOLEAN MiniportInterrupt(
    IN PVOID  MiniportInterruptContext,
    OUT PBOOLEAN  QueueDefaultInterruptDpc,
    OUT PULONG  TargetProcessors
    )
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;
    ULONG status = virtio_read_isr_status(&pContext->IODevice);

    *TargetProcessors = 0;

    if((status == 0) ||
       (status == VIRTIO_NET_INVALID_INTERRUPT_STATUS))
    {
        *QueueDefaultInterruptDpc = FALSE;
        return FALSE;
    }

    PARANDIS_STORE_LAST_INTERRUPT_TIMESTAMP(pContext);

    if(!pContext->bDeviceInitialized) {
        *QueueDefaultInterruptDpc = FALSE;
        return TRUE;
    }

    for (UINT i = 0; i < pContext->nPathBundles; i++)
    {
        pContext->pPathBundles[i].txPath.DisableInterrupts();
        pContext->pPathBundles[i].rxPath.DisableInterrupts();
    }
    if (pContext->bCXPathCreated)
    {
        pContext->CXPath.DisableInterrupts();
    }
    
    *QueueDefaultInterruptDpc = TRUE;
    pContext->ulIrqReceived += 1;

    return true;
}

// This procedure must work the same way as
// ParaNdis_ConfigureMSIXVectors when spreads vectors over RX/TX/CX pathes.
// Returns respective TX or RX path if exists, then CX path if exists
// (i.e. returns CX path only if it has dedicated vector)
// otherwise (unlikely) returns NULL
static CParaNdisAbstractPath *GetPathByMessageId(PARANDIS_ADAPTER *pContext, ULONG MessageId)
{
    CParaNdisAbstractPath *path;
    UINT bundleId = MessageId / 2;

    if (bundleId < pContext->nPathBundles)
    {
        if (MessageId & 1)
        {
            path = &(pContext->pPathBundles[bundleId].rxPath);
        }
        else
        {
            path = &(pContext->pPathBundles[bundleId].txPath);
        }
    }
    else if (pContext->CXPath.getMessageIndex() == MessageId)
    {
        path = &pContext->CXPath;
    }
    else
    {
        path = NULL;
    }
    return path;
}

/**********************************************************
NDIS-required procedure for MSI hardware interrupt handling
Parameters:
    IN PVOID  MiniportInterruptContext (actually Adapter context)
    IN ULONG  MessageId - specific interrupt index
    OUT PBOOLEAN  QueueDefaultInterruptDpc - - set to TRUE for default DPC spawning
    OUT PULONG  TargetProcessors
Return value:
    TRUE if recognized
***********************************************************/
static BOOLEAN MiniportMSIInterrupt(
    IN PVOID  MiniportInterruptContext,
    IN ULONG  MessageId,
    OUT PBOOLEAN  QueueDefaultInterruptDpc,
    OUT PULONG  TargetProcessors
    )
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;

    PARANDIS_STORE_LAST_INTERRUPT_TIMESTAMP(pContext);

    *TargetProcessors = 0;
    *QueueDefaultInterruptDpc = FALSE;

    if (!pContext->bDeviceInitialized) {
        return TRUE;
    }

    CParaNdisAbstractPath *path = GetPathByMessageId(pContext, MessageId);
	if (NULL == path) {
		*QueueDefaultInterruptDpc = FALSE;
		return TRUE;
	}

    path->SetLastInterruptTimestamp(pContext->LastInterruptTimeStamp);

    path->DisableInterrupts();

    // emit DPC for processing of TX/RX or DPC for processing of CX path
    // note that in case the CX shares vector with TX/RX the CX DPC is not
    // scheduled and CX path is processed by ParaNdis_RXTXDPCWorkBody
    // (see pContext->bSharedVectors for such case)
    if (!path->FireDPC(MessageId))
    {
        *QueueDefaultInterruptDpc = TRUE;
#if !NDIS_SUPPORT_NDIS620
        *TargetProcessors = (ULONG)path->DPCTargetProcessor;
#endif
    }

    pContext->ulIrqReceived += 1;
    return true;
}

#if NDIS_SUPPORT_NDIS620

static __inline
VOID GetAffinityForCurrentCpu(PGROUP_AFFINITY pAffinity)
{
    PROCESSOR_NUMBER ProcNum;
    KeGetCurrentProcessorNumberEx(&ProcNum);

    pAffinity->Group = ProcNum.Group;
    pAffinity->Mask = 1;
    pAffinity->Mask <<= ProcNum.Number;
}

#endif

/**********************************************************
NDIS-required procedure for DPC handling
Parameters:
    PVOID  MiniportInterruptContext (Adapter context)
***********************************************************/
static VOID MiniportInterruptDPC(
    IN NDIS_HANDLE  MiniportInterruptContext,
    IN PVOID  MiniportDpcContext,
    IN PVOID                   ReceiveThrottleParameters,
    IN PVOID                   NdisReserved2
    )
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;
    bool requiresDPCRescheduling;

#if NDIS_SUPPORT_NDIS620
    PNDIS_RECEIVE_THROTTLE_PARAMETERS RxThrottleParameters = (PNDIS_RECEIVE_THROTTLE_PARAMETERS)ReceiveThrottleParameters;
    DEBUG_ENTRY(5);
    RxThrottleParameters->MoreNblsPending = 0;
    requiresDPCRescheduling = ParaNdis_RXTXDPCWorkBody(pContext, RxThrottleParameters->MaxNblsToIndicate);
    if (requiresDPCRescheduling)
        {
            GROUP_AFFINITY Affinity;
            GetAffinityForCurrentCpu(&Affinity);

            NdisMQueueDpcEx(pContext->InterruptHandle, 0, &Affinity, MiniportDpcContext);
        }
#else /* NDIS 6.0*/
    DEBUG_ENTRY(5);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);

    requiresDPCRescheduling = ParaNdis_RXTXDPCWorkBody(pContext, PARANDIS_UNLIMITED_PACKETS_TO_INDICATE);
    if (requiresDPCRescheduling)
    {
        DPrintf(4, "[%s] Queued additional DPC for %d\n", __FUNCTION__,  requiresDPCRescheduling);
        NdisMQueueDpc(pContext->InterruptHandle, 0, 1 << KeGetCurrentProcessorNumber(), MiniportDpcContext);
    }
#endif /* NDIS_SUPPORT_NDIS620 */

    UNREFERENCED_PARAMETER(NdisReserved2);
}

/**********************************************************
A CX procedure for MSI DPC handling
Parameters:
KDPC *  Dpc - The dpc structure for CX
IN ULONG  MessageId - specific interrupt index
***********************************************************/
VOID _Function_class_(KDEFERRED_ROUTINE) MiniportMSIInterruptCXDpc(
    struct _KDPC  *Dpc,
    IN PVOID  MiniportInterruptContext,
    IN PVOID                  NdisReserved1,
    IN PVOID                  NdisReserved2
)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;
    ParaNdis_CXDPCWorkBody(pContext);
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(NdisReserved1);
    UNREFERENCED_PARAMETER(NdisReserved2);
}

/**********************************************************
NDIS-required procedure for MSI DPC handling
Parameters:
    PVOID  MiniportInterruptContext (Adapter context)
    IN ULONG  MessageId - specific interrupt index
***********************************************************/
static VOID MiniportMSIInterruptDpc(
    IN PVOID  MiniportInterruptContext,
    IN ULONG  MessageId,
    IN PVOID  MiniportDpcContext,
#if NDIS_SUPPORT_NDIS620
    IN PVOID                   ReceiveThrottleParameters,
    IN PVOID                   NdisReserved2
#else
    IN PULONG                  NdisReserved1,
    IN PULONG                  NdisReserved2
#endif
    )
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;
    bool requireDPCRescheduling;

#if NDIS_SUPPORT_NDIS620
    PNDIS_RECEIVE_THROTTLE_PARAMETERS RxThrottleParameters = (PNDIS_RECEIVE_THROTTLE_PARAMETERS)ReceiveThrottleParameters;

    RxThrottleParameters->MoreNblsPending = 0;
    requireDPCRescheduling = ParaNdis_RXTXDPCWorkBody(pContext, RxThrottleParameters->MaxNblsToIndicate);

    if (requireDPCRescheduling)
        {
            GROUP_AFFINITY Affinity;
            GetAffinityForCurrentCpu(&Affinity);

            NdisMQueueDpcEx(pContext->InterruptHandle, MessageId, &Affinity, MiniportDpcContext);
        }
#else
    UNREFERENCED_PARAMETER(NdisReserved1);

    requireDPCRescheduling = ParaNdis_RXTXDPCWorkBody(pContext, PARANDIS_UNLIMITED_PACKETS_TO_INDICATE);
    if (requireDPCRescheduling)
    {
        NdisMQueueDpc(pContext->InterruptHandle, MessageId, 1 << KeGetCurrentProcessorNumber(), MiniportDpcContext);
    }
#endif

    UNREFERENCED_PARAMETER(NdisReserved2);
}

static VOID MiniportDisableMSIInterrupt(
    IN PVOID  MiniportInterruptContext,
    IN ULONG  MessageId
    )
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;
    /* TODO - How we prevent DPC procedure from re-enabling interrupt? */

    CParaNdisAbstractPath *path = GetPathByMessageId(pContext, MessageId);
    path->DisableInterrupts();
}

static VOID MiniportEnableMSIInterrupt(
    IN PVOID  MiniportInterruptContext,
    IN ULONG  MessageId
    )
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;
    CParaNdisAbstractPath *path = GetPathByMessageId(pContext, MessageId);
    path->EnableInterrupts();
}


/**********************************************************
NDIS required handler for run-time allocation of physical memory
Parameters:

Return value:
***********************************************************/
static VOID SharedMemAllocateCompleteHandler(
    IN NDIS_HANDLE  MiniportAdapterContext,
    IN PVOID  VirtualAddress,
    IN PNDIS_PHYSICAL_ADDRESS  PhysicalAddress,
    IN ULONG  Length,
    IN PVOID  Context
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Context);
}

/*
  We enter this procedure in two cases:
  - number of MSIX vectors is at least nPathBundles*2 + 1
  - there is single MSIX vector and one path bundle
*/
NDIS_STATUS ParaNdis_ConfigureMSIXVectors(PARANDIS_ADAPTER *pContext)
{
    NDIS_STATUS status = NDIS_STATUS_RESOURCES;
    UINT i;
    u16 nVectors = (u16)pContext->pMSIXInfoTable->MessageCount;
    PIO_INTERRUPT_MESSAGE_INFO pTable = pContext->pMSIXInfoTable;
    if (pTable && pTable->MessageCount)
    {
        status = NDIS_STATUS_SUCCESS;
        DPrintf(0, "[%s] Using MSIX interrupts (%d messages, irql %d)\n",
            __FUNCTION__, pTable->MessageCount, pTable->UnifiedIrql);
        for (i = 0; i < pContext->pMSIXInfoTable->MessageCount; ++i)
        {
            DPrintf(0, "[%s] MSIX message%d=%08X=>%I64X\n",
                __FUNCTION__, i,
                pTable->MessageInfo[i].MessageData,
                pTable->MessageInfo[i].MessageAddress.QuadPart);
        }
        for (UINT j = 0; j < pContext->nPathBundles && status == NDIS_STATUS_SUCCESS; ++j)
        {
            u16 vector = 2 * u16(j);
            status = pContext->pPathBundles[j].txPath.SetupMessageIndex(vector);
            if (status == NDIS_STATUS_SUCCESS)
            {
                if (nVectors > 1) vector++;
                status = pContext->pPathBundles[j].rxPath.SetupMessageIndex(vector);
            }
            DPrintf(0, "[%s] Using messages %u/%u for RX/TX queue %u\n", __FUNCTION__,
                        pContext->pPathBundles[j].rxPath.getMessageIndex(),
                        pContext->pPathBundles[j].txPath.getMessageIndex(),
                        j);
        }

        if (status == NDIS_STATUS_SUCCESS && pContext->bCXPathCreated)
        {
            u16 nVector;
            /*
            Usually there is own vector for control queue.
            In corner case of one or two vectors control queue uses the same vector as RX or TX
            and does not spawn its own DPC for processing
            */
            if (nVectors < 3)
            {
                nVector = nVectors - 1;
                pContext->bSharedVectors = TRUE;
            }
            else
            {
                nVector = 2 * u16(pContext->nPathBundles);
            }
            status = pContext->CXPath.SetupMessageIndex(nVector);
        }
    }

    DEBUG_EXIT_STATUS(0, status);
    return status;
}

void ParaNdis_RestoreDeviceConfigurationAfterReset(
    PARANDIS_ADAPTER *pContext)
{
    if (pContext->bUsingMSIX)
    {
        ParaNdis_ConfigureMSIXVectors(pContext);
    }
}

static void DebugParseOffloadBits()
{
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO info;
    tChecksumCheckResult res;
    ULONG val = 1;
    int level = 1;
    while (val)
    {
        info.Value = (PVOID)(ULONG_PTR)val;
        if (info.Receive.IpChecksumFailed) DPrintf(level, "W.%X=IPCS failed\n", val);
        if (info.Receive.IpChecksumSucceeded) DPrintf(level, "W.%X=IPCS OK\n", val);
        if (info.Receive.TcpChecksumFailed) DPrintf(level, "W.%X=TCPCS failed\n", val);
        if (info.Receive.TcpChecksumSucceeded) DPrintf(level, "W.%X=TCPCS OK\n", val);
        if (info.Receive.UdpChecksumFailed) DPrintf(level, "W.%X=UDPCS failed\n", val);
        if (info.Receive.UdpChecksumSucceeded) DPrintf(level, "W.%X=UDPCS OK\n", val);
        val = val << 1;
    }
    val = 1;
    while (val)
    {
        res.value = val;
        if (res.flags.IpFailed) DPrintf(level, "C.%X=IPCS failed\n", val);
        if (res.flags.IpOK) DPrintf(level, "C.%X=IPCS OK\n", val);
        if (res.flags.TcpFailed) DPrintf(level, "C.%X=TCPCS failed\n", val);
        if (res.flags.TcpOK) DPrintf(level, "C.%X=TCPCS OK\n", val);
        if (res.flags.UdpFailed) DPrintf(level, "C.%X=UDPCS failed\n", val);
        if (res.flags.UdpOK) DPrintf(level, "C.%X=UDPCS OK\n", val);
        val = val << 1;
    }
}

/**********************************************************
NDIS6-related final initialization:
    Installing interrupt handler
    Allocate buffer list pool

Parameters:

Return value:

***********************************************************/
NDIS_STATUS ParaNdis_FinishSpecificInitialization(PARANDIS_ADAPTER *pContext)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS mic;
    DEBUG_ENTRY(0);

    NdisZeroMemory(&mic, sizeof(mic));
    mic.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    mic.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    mic.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    mic.DisableInterruptHandler = MiniportDisableInterruptEx;
    mic.EnableInterruptHandler  = MiniportEnableInterruptEx;
    mic.InterruptDpcHandler = MiniportInterruptDPC;
    mic.InterruptHandler = MiniportInterrupt;
    if (pContext->bUsingMSIX)
    {
        mic.MsiSupported = TRUE;
        mic.MsiSyncWithAllMessages = TRUE;
        mic.EnableMessageInterruptHandler = MiniportEnableMSIInterrupt;
        mic.DisableMessageInterruptHandler = MiniportDisableMSIInterrupt;
        mic.MessageInterruptHandler = MiniportMSIInterrupt;
        mic.MessageInterruptDpcHandler = MiniportMSIInterruptDpc;
    }
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.ContextSize = 0;
    PoolParams.PoolTag = PARANDIS_MEMORY_TAG;
    PoolParams.DataSize = 0;

    pContext->BufferListsPool = NdisAllocateNetBufferListPool(pContext->MiniportHandle, &PoolParams);
    if (!pContext->BufferListsPool)
    {
        status = NDIS_STATUS_RESOURCES;
    }

#if defined(NETKVM_COPY_RX_DATA)
    if (status == NDIS_STATUS_SUCCESS)
    {
        PoolParams.DataSize = pContext->MaxPacketSize.nMaxFullSizeOsRx;
        pContext->BufferListsPoolForArm = NdisAllocateNetBufferListPool(pContext->MiniportHandle, &PoolParams);
        if (!pContext->BufferListsPoolForArm)
        {
            status = NDIS_STATUS_RESOURCES;
        }
    }
#endif

    if (status == NDIS_STATUS_SUCCESS)
    {
        status = NdisMRegisterInterruptEx(pContext->MiniportHandle, pContext, &mic, &pContext->InterruptHandle);
    }

    if (pContext->bUsingMSIX)
    {
        DPrintf(0, "[%s] MSIX message table %savailable, count = %u\n", __FUNCTION__, (mic.MessageInfoTable == nullptr ? "not " : ""),
            (mic.MessageInfoTable == nullptr ? 0 : mic.MessageInfoTable->MessageCount));
    }
    else
    {
        DPrintf(0, "[%s] Not using MSIX\n", __FUNCTION__);
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        NDIS_SG_DMA_DESCRIPTION sgDesc;
        sgDesc.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
        sgDesc.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
        sgDesc.Header.Size = NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;
        sgDesc.Flags = NDIS_SG_DMA_64_BIT_ADDRESS;
        sgDesc.MaximumPhysicalMapping = 0x10000; // 64K
        sgDesc.ProcessSGListHandler = ProcessSGListHandler;
        sgDesc.SharedMemAllocateCompleteHandler = SharedMemAllocateCompleteHandler;
        sgDesc.ScatterGatherListSize = 0; // OUT value
        status = NdisMRegisterScatterGatherDma(pContext->MiniportHandle, &sgDesc, &pContext->DmaHandle);
        if (status != NDIS_STATUS_SUCCESS)
        {
            DPrintf(0, "[%s] ERROR: NdisMRegisterScatterGatherDma failed (%X)!\n", __FUNCTION__, status);
        }
        else
        {
            DPrintf(0, "[%s] SG recommended size %d\n", __FUNCTION__, sgDesc.ScatterGatherListSize);
        }
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        if (NDIS_CONNECT_MESSAGE_BASED == mic.InterruptType)
        {
            pContext->pMSIXInfoTable = mic.MessageInfoTable;
        }
        else if (pContext->bUsingMSIX)
        {
            DPrintf(0, "[%s] ERROR: Interrupt type %d, message table %p\n",
                __FUNCTION__, mic.InterruptType, mic.MessageInfoTable);
            status = NDIS_STATUS_RESOURCE_CONFLICT;
        }
        ParaNdis6_ApplyOffloadPersistentConfiguration(pContext);
        DebugParseOffloadBits();
    }
    DEBUG_EXIT_STATUS(0, status);
    return status;
}

/**********************************************************
NDIS6-related final initialization:
    Uninstalling interrupt handler
    Dellocate buffer list pool
Parameters:
    context
***********************************************************/
VOID ParaNdis_FinalizeCleanup(PARANDIS_ADAPTER *pContext)
{
    // we zero context members to be able examine them in the debugger/dump
    if (pContext->InterruptHandle)
    {
        NdisMDeregisterInterruptEx(pContext->InterruptHandle);
        pContext->InterruptHandle = NULL;
    }
    if (pContext->BufferListsPool)
    {
        NdisFreeNetBufferListPool(pContext->BufferListsPool);
        pContext->BufferListsPool = NULL;
    }
    if (pContext->BufferListsPoolForArm)
    {
        NdisFreeNetBufferListPool(pContext->BufferListsPoolForArm);
        pContext->BufferListsPoolForArm = NULL;
    }
    if (pContext->DmaHandle)
    {
        NdisMDeregisterScatterGatherDma(pContext->DmaHandle);
        pContext->DmaHandle = NULL;
    }
}

static
void ParaNdis_AdjustRxBufferHolderLength(
    pRxNetDescriptor p,
    ULONG ulDataOffset)
{
    PMDL NextMdlLinkage = p->Holder;
    ULONG ulBytesLeft = p->PacketInfo.dataLength + ulDataOffset;
    ULONG ulPageDescIndex = PARANDIS_FIRST_RX_DATA_PAGE;

    while(NextMdlLinkage != NULL)
    {
        ULONG ulThisMdlBytes = min(p->PhysicalPages[ulPageDescIndex].size, ulBytesLeft);
        NdisAdjustMdlLength(NextMdlLinkage, ulThisMdlBytes);
        ulBytesLeft -= ulThisMdlBytes;
        NextMdlLinkage = NDIS_MDL_LINKAGE(NextMdlLinkage);
        ulPageDescIndex++;
    }
    NETKVM_ASSERT(ulBytesLeft == 0);
}

#if PARANDIS_SUPPORT_RSS
static ULONG HashReportToHashType(USHORT report)
{
    static const ULONG table[VIRTIO_NET_HASH_REPORT_MAX + 1] =
    {
#if (NDIS_SUPPORT_NDIS680)
        0, NDIS_HASH_IPV4, NDIS_HASH_TCP_IPV4, NDIS_HASH_UDP_IPV4,
        NDIS_HASH_IPV6, NDIS_HASH_TCP_IPV6, NDIS_HASH_UDP_IPV6,
        NDIS_HASH_IPV6_EX, NDIS_HASH_TCP_IPV6_EX, NDIS_HASH_UDP_IPV6_EX
#else
        0, NDIS_HASH_IPV4, NDIS_HASH_TCP_IPV4, 0,
        NDIS_HASH_IPV6, NDIS_HASH_TCP_IPV6, 0,
        NDIS_HASH_IPV6_EX, NDIS_HASH_TCP_IPV6_EX, 0
#endif
    };
    if (report > VIRTIO_NET_HASH_REPORT_MAX)
    {
        return 0;
    }
    return table[report];
}
#endif

static __inline
VOID NBLSetRSSInfo(PPARANDIS_ADAPTER pContext, PNET_BUFFER_LIST pNBL, PNET_PACKET_INFO PacketInfo, PVOID virtioHeader)
{
#if PARANDIS_SUPPORT_RSS
    CNdisRWLockState lockState;

    pContext->RSSParameters.rwLock.acquireReadDpr(lockState);
    if(pContext->RSSParameters.RSSMode != PARANDIS_RSS_DISABLED)
    {
        NET_BUFFER_LIST_SET_HASH_TYPE    (pNBL, PacketInfo->RSSHash.Type);
        NET_BUFFER_LIST_SET_HASH_FUNCTION(pNBL, PacketInfo->RSSHash.Function);
        NET_BUFFER_LIST_SET_HASH_VALUE   (pNBL, PacketInfo->RSSHash.Value);
        if (PacketInfo->RSSHash.Type && pContext->bHashReportedByDevice)
        {
            virtio_net_hdr_v1_hash *ph = (virtio_net_hdr_v1_hash *)virtioHeader;
            ULONG val = HashReportToHashType(ph->hash_report);
            if (val != PacketInfo->RSSHash.Type || ph->hash_value != PacketInfo->RSSHash.Value)
            {
                pContext->extraStatistics.framesRSSError++;
                TraceNoPrefix(0, "%s: hash mistake: reported %d, correct %X\n", __FUNCTION__,
                    ph->hash_report, PacketInfo->RSSHash.Type);
            }
        }

    }
    pContext->RSSParameters.rwLock.releaseDpr(lockState);
#else
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(pNBL);
    UNREFERENCED_PARAMETER(PacketInfo);
    UNREFERENCED_PARAMETER(virtioHeader);
#endif
}

static __inline
VOID NBLSet8021QInfo(PPARANDIS_ADAPTER pContext, PNET_BUFFER_LIST pNBL, PNET_PACKET_INFO pPacketInfo)
{
    NDIS_NET_BUFFER_LIST_8021Q_INFO qInfo;
    qInfo.Value = NULL;

    if (IsPrioritySupported(pContext))
        qInfo.TagHeader.UserPriority = pPacketInfo->Vlan.UserPriority;

    if (IsVlanSupported(pContext))
        qInfo.TagHeader.VlanId = pPacketInfo->Vlan.VlanId;

    if(qInfo.Value != NULL)
        pContext->extraStatistics.framesRxPriority++;

    NET_BUFFER_LIST_INFO(pNBL, Ieee8021QNetBufferListInfo) = qInfo.Value;
}

#if PARANDIS_SUPPORT_RSC
static __inline
UINT PktGetTCPCoalescedSegmentsCount(PPARANDIS_ADAPTER pContext,
                                     PNET_PACKET_INFO PacketInfo,
                                     UINT mss)
{
    // We have no exact data from the device, but can evaluate the number
    // of coalesced segments according to mss (max segment size) value
    // provided in the packet header. It includes only size of
    // TCP payload and TCP payload of final packet was earlier
    // splitted over TCP segments with payload of mss bytes each
    ULONG TcpHeaderOffset = PacketInfo->L2HdrLen + PacketInfo->L3HdrLen;
    auto TCPHdr = reinterpret_cast<TCPHeader *>RtlOffsetToPointer(PacketInfo->headersBuffer, TcpHeaderOffset);
    ULONG IpAndTcpHeaderSize = PacketInfo->L3HdrLen + TCP_HEADER_LENGTH(TCPHdr);
    ULONG payloadLen = PacketInfo->L2PayloadLen - IpAndTcpHeaderSize;

    // if we have meaningless value from the device, use
    // the best estimation - max possible TCP payload
    // nMaxDataSize (typically 1500) starts after ethernet header
    ULONG maxMss = pContext->MaxPacketSize.nMaxDataSize - IpAndTcpHeaderSize;
    if (mss > maxMss || mss == 0)
    {
        mss = maxMss;
    }
    return  payloadLen / mss + !!(payloadLen % mss);
}

static __inline
VOID NBLSetRSCInfo(PPARANDIS_ADAPTER pContext, PNET_BUFFER_LIST pNBL,
                   PNET_PACKET_INFO PacketInfo, UINT nCoalescedSegments, USHORT nDupAck)
{
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO qCSInfo;

    qCSInfo.Value = NULL;
    qCSInfo.Receive.IpChecksumSucceeded = TRUE;
    qCSInfo.Receive.IpChecksumValueInvalid = TRUE;
    qCSInfo.Receive.TcpChecksumSucceeded = TRUE;
    qCSInfo.Receive.TcpChecksumValueInvalid = TRUE;
    NET_BUFFER_LIST_INFO(pNBL, TcpIpChecksumNetBufferListInfo) = qCSInfo.Value;

    NET_BUFFER_LIST_COALESCED_SEG_COUNT(pNBL) = (USHORT) nCoalescedSegments;
    NET_BUFFER_LIST_DUP_ACK_COUNT(pNBL) = nDupAck;

    NdisInterlockedAddLargeStatistic(&pContext->RSC.Statistics.CoalescedOctets, PacketInfo->L2PayloadLen);
    NdisInterlockedAddLargeStatistic(&pContext->RSC.Statistics.CoalesceEvents, 1);
    NdisInterlockedAddLargeStatistic(&pContext->RSC.Statistics.CoalescedPkts, nCoalescedSegments);
}
#endif

#if defined(NETKVM_COPY_RX_DATA)
static PNET_BUFFER_LIST CloneNblFreeOriginalForArm(PARANDIS_ADAPTER *pContext, PNET_BUFFER_LIST original, PVOID bufferDesc)
{
    PNET_BUFFER_LIST pNewNbl;
    if (!original)
    {
        return NULL;
    }
    pNewNbl = NdisAllocateNetBufferList(pContext->BufferListsPoolForArm, 0, NULL);
    if (pNewNbl)
    {
        PNET_BUFFER src = NET_BUFFER_LIST_FIRST_NB(original);
        PNET_BUFFER dest = NET_BUFFER_LIST_FIRST_NB(pNewNbl);
        ULONG done = 0;
        NET_BUFFER_DATA_LENGTH(dest) = NET_BUFFER_DATA_LENGTH(src);
        NET_BUFFER_DATA_OFFSET(dest) = 0;
        NET_BUFFER_CURRENT_MDL_OFFSET(dest) = 0;
        NdisCopyFromNetBufferToNetBuffer(dest, 0, NET_BUFFER_DATA_LENGTH(src), src, 0, &done);
        if (done == NET_BUFFER_DATA_LENGTH(src))
        {
            NdisCopyReceiveNetBufferListInfo(pNewNbl, original);
            pNewNbl->MiniportReserved[0] = bufferDesc;
        }
        else
        {
            DPrintf(0, "[%s] ERROR: Can't copy data to NBL (%d != %d)\n",
                __FUNCTION__, done, NET_BUFFER_DATA_LENGTH(src));
            NdisFreeNetBufferList(pNewNbl);
            pNewNbl = NULL;
        }
    }
    else
    {
        DPrintf(0, "[%s] ERROR: Can't allocate NBL\n", __FUNCTION__);
    }
    NdisFreeNetBufferList(original);
    return pNewNbl;
}
#else
#define CloneNblFreeOriginalForArm(ctx, org, bufDesc) (org)
#endif

/**********************************************************
NDIS6 implementation of packet indication

Parameters:
    context
    PVOID pBuffersDescriptor - VirtIO buffer descriptor of data buffer
    BOOLEAN bPrepareOnly - only return NBL for further indication in batch
Return value:
    TRUE  is packet indicated
    FALSE if not (in this case, the descriptor should be freed now)
If priority header is in the packet. it will be removed and *pLength decreased
***********************************************************/
tPacketIndicationType ParaNdis_PrepareReceivedPacket(
    PARANDIS_ADAPTER *pContext,
    pRxNetDescriptor pBuffersDesc,
    PUINT            pnCoalescedSegmentsCount)
{
    PMDL pMDL = pBuffersDesc->Holder;
    PNET_BUFFER_LIST pNBL = NULL;
    *pnCoalescedSegmentsCount = 1;

    if (pMDL)
    {
        ULONG nBytesStripped = 0;
        PNET_PACKET_INFO pPacketInfo = &pBuffersDesc->PacketInfo;

        if (pContext->ulPriorityVlanSetting && pPacketInfo->hasVlanHeader)
        {
            nBytesStripped = ParaNdis_StripVlanHeaderMoveHead(pPacketInfo);
        }

        ParaNdis_PadPacketToMinimalLength(pPacketInfo);
        ParaNdis_AdjustRxBufferHolderLength(pBuffersDesc, nBytesStripped);
        pNBL = NdisAllocateNetBufferAndNetBufferList(pContext->BufferListsPool, 0, 0, pMDL, nBytesStripped, pPacketInfo->dataLength);

        if (pNBL)
        {
            virtio_net_hdr_mrg_rxbuf *pHeader = (virtio_net_hdr_mrg_rxbuf *) pBuffersDesc->PhysicalPages[0].Virtual;
            tChecksumCheckResult csRes;
            NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO qCSInfo;
            qCSInfo.Value = NULL;
            pNBL->SourceHandle = pContext->MiniportHandle;
            NBLSetRSSInfo(pContext, pNBL, pPacketInfo, pHeader);
            NBLSet8021QInfo(pContext, pNBL, pPacketInfo);

            pNBL->MiniportReserved[0] = pBuffersDesc;

#if PARANDIS_SUPPORT_RSC
            csRes.value = 0;
            csRes.flags.IpOK = true;
            csRes.flags.TcpOK = true;
            if (pHeader->hdr.gso_type != VIRTIO_NET_HDR_GSO_NONE)
            {
                USHORT nDupAcks = 0;
                if (pHeader->hdr.flags & VIRTIO_NET_HDR_F_RSC_INFO)
                {
                    *pnCoalescedSegmentsCount = pHeader->hdr.rsc_ext_num_packets;
                    nDupAcks = pHeader->hdr.rsc_ext_num_dupacks;
                    pContext->extraStatistics.framesCoalescedWindows++;
                }
                else
                {
                    *pnCoalescedSegmentsCount = PktGetTCPCoalescedSegmentsCount(pContext, pPacketInfo, pHeader->hdr.gso_size);
                    pContext->extraStatistics.framesCoalescedHost++;
                }
                NBLSetRSCInfo(pContext, pNBL, pPacketInfo, *pnCoalescedSegmentsCount, 0);
                // according to the spec the device does not calculate TCP checksum
                qCSInfo.Receive.IpChecksumValueInvalid = true;
                qCSInfo.Receive.TcpChecksumValueInvalid = true;
            }
            else
#endif
            {
                csRes = ParaNdis_CheckRxChecksum(
                    pContext,
                    pHeader->hdr.flags,
                    &pBuffersDesc->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE],
                    pPacketInfo,
                    nBytesStripped, TRUE);
            }
            if (csRes.value)
            {
                qCSInfo.Receive.IpChecksumFailed = csRes.flags.IpFailed;
                qCSInfo.Receive.IpChecksumSucceeded = csRes.flags.IpOK;
                qCSInfo.Receive.TcpChecksumFailed = csRes.flags.TcpFailed;
                qCSInfo.Receive.TcpChecksumSucceeded = csRes.flags.TcpOK;
                qCSInfo.Receive.UdpChecksumFailed = csRes.flags.UdpFailed;
                qCSInfo.Receive.UdpChecksumSucceeded = csRes.flags.UdpOK;
            }
            NET_BUFFER_LIST_INFO(pNBL, TcpIpChecksumNetBufferListInfo) = qCSInfo.Value;
            DPrintf(1, "datalen %d, GSO type/flags %d:%d, mss %d, %d segments, CS %X->%X\n",
                pPacketInfo->dataLength, pHeader->hdr.gso_type, pHeader->hdr.flags, pHeader->hdr.gso_size,
                *pnCoalescedSegmentsCount, csRes.value, (ULONG)(ULONG_PTR)qCSInfo.Value);

            pNBL->Status = NDIS_STATUS_SUCCESS;
#if defined(ENABLE_HISTORY_LOG)
            {
                tTcpIpPacketParsingResult packetReview = ParaNdis_CheckSumVerify(
                    RtlOffsetToPointer(pPacketInfo->headersBuffer, ETH_HEADER_SIZE),
                    pPacketInfo->dataLength,
                    pcrIpChecksum | pcrTcpChecksum | pcrUdpChecksum,
                    __FUNCTION__
                    );
                ParaNdis_DebugHistory(pContext, hopPacketReceived, pNBL, pPacketInfo->dataLength, (ULONG)(ULONG_PTR)qInfo.Value, packetReview.value);
            }
#endif
        }
    }
    return CloneNblFreeOriginalForArm(pContext, pNBL, pBuffersDesc);
}

NDIS_STATUS ParaNdis_ExactSendFailureStatus(PARANDIS_ADAPTER *pContext)
{
    NDIS_STATUS status = NDIS_STATUS_FAILURE;
    if (!pContext->bConnected) status = NDIS_STATUS_MEDIA_DISCONNECTED;
    if (pContext->bSurprizeRemoved) status = NDIS_STATUS_NOT_ACCEPTED;
    return status;
}

BOOLEAN ParaNdis_IsSendPossible(PARANDIS_ADAPTER *pContext)
{
    BOOLEAN b;
    b =  !pContext->bSurprizeRemoved && pContext->bConnected;
    return b;
}

/**********************************************************
NDIS required handler for run-time allocation of scatter-gather list
Parameters:
pSGL - scatter-hather list of elements (possible NULL when called directly)
Context - (tNetBufferEntry *) for specific NET_BUFFER in NBL
Called on DPC (DDK claims it)
***********************************************************/
VOID ProcessSGListHandler(
    IN PDEVICE_OBJECT  pDO,
    IN PVOID  Reserved,
    IN PSCATTER_GATHER_LIST  pSGL,
    IN PVOID  Context
    )
{
    UNREFERENCED_PARAMETER(Reserved);
    UNREFERENCED_PARAMETER(pDO);

    auto NBHolder = static_cast<CNB*>(Context);
    NBHolder->MappingDone(pSGL);
}

/**********************************************************
Required procedure of NDIS
NDIS wants to cancel sending of each list which has specified CancelID
Can be tested only under NDIS Test
***********************************************************/
VOID ParaNdis6_CancelSendNetBufferLists(
    NDIS_HANDLE  miniportAdapterContext,
    PVOID pCancelId)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)miniportAdapterContext;

    DEBUG_ENTRY(0);
    for (UINT i = 0; i < pContext->nPathBundles; i++)
    {
        pContext->pPathBundles[i].txPath.CancelNBLs(pCancelId);
    }
}
