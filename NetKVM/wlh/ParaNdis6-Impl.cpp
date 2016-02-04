/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: ParaNdis6-Impl.c
 *
 * This file contains NDIS6-specific implementation of driver's procedures.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "ParaNdis6.h"
#include "kdebugprint.h"
#include "ParaNdis_DebugHistory.h"

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
    co.Header.Size = sizeof(co);
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
    tCompletePhysicalAddress *pAddresses)
{
    NdisMAllocateSharedMemory(
        pContext->MiniportHandle,
        pAddresses->size,
        TRUE,
        &pAddresses->Virtual,
        &pAddresses->Physical);
    return pAddresses->Virtual != NULL;
}


/**********************************************************
NDIS6 implementation of shared memory freeing
Parameters:
    context
    tCompletePhysicalAddress *pAddresses
            the structure accumulates all our knowledge
            about the allocation (size, addresses, cacheability etc)
            filled by ParaNdis_InitialAllocatePhysicalMemory or
            by ParaNdis_RuntimeRequestToAllocatePhysicalMemory
***********************************************************/

VOID ParaNdis_FreePhysicalMemory(
    PARANDIS_ADAPTER *pContext,
    tCompletePhysicalAddress *pAddresses)
{

    NdisMFreeSharedMemory(
        pContext->MiniportHandle,
        pAddresses->size,
        TRUE,
        pAddresses->Virtual,
        pAddresses->Physical);
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
    tSynchronizedContext SyncContext;
    NDIS_SYNC_PROC_TYPE syncProc;
#pragma warning (push)
#pragma warning (disable:4152)
    syncProc = (NDIS_SYNC_PROC_TYPE) procedure;
#pragma warning (pop)
    SyncContext.pContext  = pContext;
    SyncContext.Parameter = parameter;
    return NdisMSynchronizeWithInterruptEx(pContext->InterruptHandle, messageId, syncProc, &SyncContext);
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
    ULONG status = VirtIODeviceISR(pContext->IODevice);

    *TargetProcessors = 0;

    if((status == 0) ||
       (status == VIRTIO_NET_INVALID_INTERRUPT_STATUS))
    {
        *QueueDefaultInterruptDpc = FALSE;
        return FALSE;
    }

    PARADNIS_STORE_LAST_INTERRUPT_TIMESTAMP(pContext);

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

static CParaNdisAbstractPath *GetPathByMessageId(PARANDIS_ADAPTER *pContext, ULONG MessageId)
{
    CParaNdisAbstractPath *path = NULL;

    UINT bundleId = MessageId / 2;
    if (bundleId >= pContext->nPathBundles)
    {
        path = &pContext->CXPath;
    }
    else if (MessageId % 2)
    {
        path = &(pContext->pPathBundles[bundleId].rxPath);
    }
    else
    {
        path = &(pContext->pPathBundles[bundleId].txPath);
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

    PARADNIS_STORE_LAST_INTERRUPT_TIMESTAMP(pContext);

    *TargetProcessors = 0;

    if (!pContext->bDeviceInitialized) {
        *QueueDefaultInterruptDpc = FALSE;
        return TRUE;
    }

    CParaNdisAbstractPath *path = GetPathByMessageId(pContext, MessageId);

    path->DisableInterrupts();
    path->ReportInterrupt();


#if NDIS_SUPPORT_NDIS620
    if (path->DPCAffinity.Mask)
    {
        NdisMQueueDpcEx(pContext->InterruptHandle, MessageId, &path->DPCAffinity, NULL);
        *QueueDefaultInterruptDpc = FALSE;
    }
    else
    {
        *QueueDefaultInterruptDpc = TRUE;
    }
#else
    *TargetProcessors = (ULONG)path->DPCTargetProcessor;
    *QueueDefaultInterruptDpc = TRUE;
#endif

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
    requiresDPCRescheduling = ParaNdis_DPCWorkBody(pContext, RxThrottleParameters->MaxNblsToIndicate);
    if (requiresDPCRescheduling)
        {
            GROUP_AFFINITY Affinity;
            GetAffinityForCurrentCpu(&Affinity);

            NdisMQueueDpcEx(pContext->InterruptHandle, 0, &Affinity, MiniportDpcContext);
        }
#else /* NDIS 6.0*/
    DEBUG_ENTRY(5);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);

    requiresDPCRescheduling = ParaNdis_DPCWorkBody(pContext, PARANDIS_UNLIMITED_PACKETS_TO_INDICATE);
    if (requiresDPCRescheduling)
    {
        DPrintf(4, ("[%s] Queued additional DPC for %d\n", __FUNCTION__,  requiresDPCRescheduling));
        NdisMQueueDpc(pContext->InterruptHandle, 0, 1 << KeGetCurrentProcessorNumber(), MiniportDpcContext);
    }
#endif /* NDIS_SUPPORT_NDIS620 */

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
    requireDPCRescheduling = ParaNdis_DPCWorkBody(pContext, RxThrottleParameters->MaxNblsToIndicate);

    if (requireDPCRescheduling)
        {
            GROUP_AFFINITY Affinity;
            GetAffinityForCurrentCpu(&Affinity);

            NdisMQueueDpcEx(pContext->InterruptHandle, MessageId, &Affinity, MiniportDpcContext);
        }
#else
    UNREFERENCED_PARAMETER(NdisReserved1);

    requireDPCRescheduling = ParaNdis_DPCWorkBody(pContext, PARANDIS_UNLIMITED_PACKETS_TO_INDICATE);
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

NDIS_STATUS ParaNdis_ConfigureMSIXVectors(PARANDIS_ADAPTER *pContext)
{
    NDIS_STATUS status = NDIS_STATUS_RESOURCES;
    UINT i;
    PIO_INTERRUPT_MESSAGE_INFO pTable = pContext->pMSIXInfoTable;
    if (pTable && pTable->MessageCount)
    {
        status = NDIS_STATUS_SUCCESS;
        DPrintf(0, ("[%s] Using MSIX interrupts (%d messages, irql %d)\n",
            __FUNCTION__, pTable->MessageCount, pTable->UnifiedIrql));
        for (i = 0; i < pContext->pMSIXInfoTable->MessageCount; ++i)
        {
            DPrintf(0, ("[%s] MSIX message%d=%08X=>%I64X\n",
                __FUNCTION__, i,
                pTable->MessageInfo[i].MessageData,
                pTable->MessageInfo[i].MessageAddress));
        }
        for (UINT j = 0; j < pContext->nPathBundles && status == NDIS_STATUS_SUCCESS; ++j)
        {
            status = pContext->pPathBundles[j].rxPath.SetupMessageIndex(2 * u16(j) + 1);
            status = pContext->pPathBundles[j].txPath.SetupMessageIndex(2 * u16(j));
            DPrintf(0, ("[%s] Using messages %u/%u for RX/TX queue %u\n", __FUNCTION__,
                        pContext->pPathBundles[j].rxPath.getMessageIndex(),
                        pContext->pPathBundles[j].txPath.getMessageIndex(),
                        j));
        }

        if (status == NDIS_STATUS_SUCCESS && pContext->bCXPathCreated)
        {
            /* We need own vector for control queue. If one is not available, fail the initialization */
            if (pContext->nPathBundles * 2 > pTable->MessageCount - 1)
            {
                DPrintf(0, ("[%s] Not enough vectors for control queue!\n", __FUNCTION__));
                status = NDIS_STATUS_RESOURCES;
            }
            else
            {
                status = pContext->CXPath.SetupMessageIndex(2 * u16(pContext->nPathBundles));
                DPrintf(0, ("[%s] Using message %u for controls\n", __FUNCTION__, pContext->CXPath.getMessageIndex()));
            }
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
        if (info.Receive.IpChecksumFailed) DPrintf(level, ("W.%X=IPCS failed\n", val));
        if (info.Receive.IpChecksumSucceeded) DPrintf(level, ("W.%X=IPCS OK\n", val));
        if (info.Receive.TcpChecksumFailed) DPrintf(level, ("W.%X=TCPCS failed\n", val));
        if (info.Receive.TcpChecksumSucceeded) DPrintf(level, ("W.%X=TCPCS OK\n", val));
        if (info.Receive.UdpChecksumFailed) DPrintf(level, ("W.%X=UDPCS failed\n", val));
        if (info.Receive.UdpChecksumSucceeded) DPrintf(level, ("W.%X=UDPCS OK\n", val));
        val = val << 1;
    }
    val = 1;
    while (val)
    {
        res.value = val;
        if (res.flags.IpFailed) DPrintf(level, ("C.%X=IPCS failed\n", val));
        if (res.flags.IpOK) DPrintf(level, ("C.%X=IPCS OK\n", val));
        if (res.flags.TcpFailed) DPrintf(level, ("C.%X=TCPCS failed\n", val));
        if (res.flags.TcpOK) DPrintf(level, ("C.%X=TCPCS OK\n", val));
        if (res.flags.UdpFailed) DPrintf(level, ("C.%X=UDPCS failed\n", val));
        if (res.flags.UdpOK) DPrintf(level, ("C.%X=UDPCS OK\n", val));
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
    PoolParams.Header.Size = sizeof(PoolParams);
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

    if (status == NDIS_STATUS_SUCCESS)
    {
        status = NdisMRegisterInterruptEx(pContext->MiniportHandle, pContext, &mic, &pContext->InterruptHandle);
    }

    if (pContext->bUsingMSIX)
    {
        DPrintf(0, ("[%s] MSIX message table %savailable, count = %u\n", __FUNCTION__, (mic.MessageInfoTable == nullptr ? "not " : ""),
            (mic.MessageInfoTable == nullptr ? 0 : mic.MessageInfoTable->MessageCount)));
    }
    else
    {
        DPrintf(0, ("[%s] Not using MSIX\n", __FUNCTION__));
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        NDIS_SG_DMA_DESCRIPTION sgDesc;
        sgDesc.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
        sgDesc.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
        sgDesc.Header.Size = sizeof(sgDesc);
        sgDesc.Flags = NDIS_SG_DMA_64_BIT_ADDRESS;
        sgDesc.MaximumPhysicalMapping = 0x10000; // 64K
        sgDesc.ProcessSGListHandler = ProcessSGListHandler;
        sgDesc.SharedMemAllocateCompleteHandler = SharedMemAllocateCompleteHandler;
        sgDesc.ScatterGatherListSize = 0; // OUT value
        status = NdisMRegisterScatterGatherDma(pContext->MiniportHandle, &sgDesc, &pContext->DmaHandle);
        if (status != NDIS_STATUS_SUCCESS)
        {
            DPrintf(0, ("[%s] ERROR: NdisMRegisterScatterGatherDma failed (%X)!\n", __FUNCTION__, status));
        }
        else
        {
            DPrintf(0, ("[%s] SG recommended size %d\n", __FUNCTION__, sgDesc.ScatterGatherListSize));
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
            DPrintf(0, ("[%s] ERROR: Interrupt type %d, message table %p\n",
                __FUNCTION__, mic.InterruptType, mic.MessageInfoTable));
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
    if (pContext->DmaHandle)
    {
        NdisMDeregisterScatterGatherDma(pContext->DmaHandle);
        pContext->DmaHandle = NULL;
    }
}

BOOLEAN ParaNdis_BindRxBufferToPacket(
    PARANDIS_ADAPTER *pContext,
    pRxNetDescriptor p)
{
    ULONG i;
    PMDL *NextMdlLinkage = &p->Holder;

    for(i = PARANDIS_FIRST_RX_DATA_PAGE; i < p->PagesAllocated; i++)
    {
        *NextMdlLinkage = NdisAllocateMdl(pContext->MiniportHandle, p->PhysicalPages[i].Virtual, PAGE_SIZE);
        if(*NextMdlLinkage == NULL) goto error_exit;

        NextMdlLinkage = &(NDIS_MDL_LINKAGE(*NextMdlLinkage));
    }
    *NextMdlLinkage = NULL;

    return TRUE;

error_exit:

    ParaNdis_UnbindRxBufferFromPacket(p);
    return FALSE;
}

void ParaNdis_UnbindRxBufferFromPacket(
    pRxNetDescriptor p)
{
    PMDL NextMdlLinkage = p->Holder;

    while(NextMdlLinkage != NULL)
    {
        PMDL pThisMDL = NextMdlLinkage;
        NextMdlLinkage = NDIS_MDL_LINKAGE(pThisMDL);

        NdisAdjustMdlLength(pThisMDL, PAGE_SIZE);
        NdisFreeMdl(pThisMDL);
    }
}

static
void ParaNdis_AdjustRxBufferHolderLength(
    pRxNetDescriptor p,
    ULONG ulDataOffset)
{
    PMDL NextMdlLinkage = p->Holder;
    ULONG ulBytesLeft = p->PacketInfo.dataLength + ulDataOffset;

    while(NextMdlLinkage != NULL)
    {
        ULONG ulThisMdlBytes = min(PAGE_SIZE, ulBytesLeft);
        NdisAdjustMdlLength(NextMdlLinkage, ulThisMdlBytes);
        ulBytesLeft -= ulThisMdlBytes;
        NextMdlLinkage = NDIS_MDL_LINKAGE(NextMdlLinkage);
    }
    ASSERT(ulBytesLeft == 0);
}

static __inline
VOID NBLSetRSSInfo(PPARANDIS_ADAPTER pContext, PNET_BUFFER_LIST pNBL, PNET_PACKET_INFO PacketInfo)
{
#if PARANDIS_SUPPORT_RSS
    CNdisRWLockState lockState;

    pContext->RSSParameters.rwLock.acquireReadDpr(lockState);
    if(pContext->RSSParameters.RSSMode != PARANDIS_RSS_DISABLED)
    {
        NET_BUFFER_LIST_SET_HASH_TYPE    (pNBL, PacketInfo->RSSHash.Type);
        NET_BUFFER_LIST_SET_HASH_FUNCTION(pNBL, PacketInfo->RSSHash.Function);
        NET_BUFFER_LIST_SET_HASH_VALUE   (pNBL, PacketInfo->RSSHash.Value);
    }
    pContext->RSSParameters.rwLock.releaseDpr(lockState);
#else
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(pNBL);
    UNREFERENCED_PARAMETER(PacketInfo);
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
UINT PktGetTCPCoalescedSegmentsCount(PNET_PACKET_INFO PacketInfo, UINT nMaxTCPPayloadSize)
{
    // We have no corresponding data, following is a simulation
    return PacketInfo->L2PayloadLen / nMaxTCPPayloadSize +
        !!(PacketInfo->L2PayloadLen % nMaxTCPPayloadSize);
}

static __inline
VOID NBLSetRSCInfo(PPARANDIS_ADAPTER pContext, PNET_BUFFER_LIST pNBL,
                   PNET_PACKET_INFO PacketInfo, UINT nCoalescedSegments)
{
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO qCSInfo;

    qCSInfo.Value = NULL;
    qCSInfo.Receive.IpChecksumSucceeded = TRUE;
    qCSInfo.Receive.IpChecksumValueInvalid = TRUE;
    qCSInfo.Receive.TcpChecksumSucceeded = TRUE;
    qCSInfo.Receive.TcpChecksumValueInvalid = TRUE;
    NET_BUFFER_LIST_INFO(pNBL, TcpIpChecksumNetBufferListInfo) = qCSInfo.Value;

    NET_BUFFER_LIST_COALESCED_SEG_COUNT(pNBL) = (USHORT) nCoalescedSegments;
    NET_BUFFER_LIST_DUP_ACK_COUNT(pNBL) = 0;

    NdisInterlockedAddLargeStatistic(&pContext->RSC.Statistics.CoalescedOctets, PacketInfo->L2PayloadLen);
    NdisInterlockedAddLargeStatistic(&pContext->RSC.Statistics.CoalesceEvents, 1);
    NdisInterlockedAddLargeStatistic(&pContext->RSC.Statistics.CoalescedPkts, nCoalescedSegments);
}
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
            virtio_net_hdr *pHeader = (virtio_net_hdr *) pBuffersDesc->PhysicalPages[0].Virtual;
            tChecksumCheckResult csRes;
            pNBL->SourceHandle = pContext->MiniportHandle;
            NBLSetRSSInfo(pContext, pNBL, pPacketInfo);
            NBLSet8021QInfo(pContext, pNBL, pPacketInfo);

            pNBL->MiniportReserved[0] = pBuffersDesc;

#if PARANDIS_SUPPORT_RSC
            if(pHeader->gso_type != VIRTIO_NET_HDR_GSO_NONE)
            {
                *pnCoalescedSegmentsCount = PktGetTCPCoalescedSegmentsCount(pPacketInfo, pContext->MaxPacketSize.nMaxDataSize);
                NBLSetRSCInfo(pContext, pNBL, pPacketInfo, *pnCoalescedSegmentsCount);
            }
            else
#endif
            {
                csRes = ParaNdis_CheckRxChecksum(
                    pContext,
                    pHeader->flags,
                    &pBuffersDesc->PhysicalPages[PARANDIS_FIRST_RX_DATA_PAGE],
                    pPacketInfo->dataLength,
                    nBytesStripped, TRUE);
                if (csRes.value)
                {
                    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO qCSInfo;
                    qCSInfo.Value = NULL;
                    qCSInfo.Receive.IpChecksumFailed = csRes.flags.IpFailed;
                    qCSInfo.Receive.IpChecksumSucceeded = csRes.flags.IpOK;
                    qCSInfo.Receive.TcpChecksumFailed = csRes.flags.TcpFailed;
                    qCSInfo.Receive.TcpChecksumSucceeded = csRes.flags.TcpOK;
                    qCSInfo.Receive.UdpChecksumFailed = csRes.flags.UdpFailed;
                    qCSInfo.Receive.UdpChecksumSucceeded = csRes.flags.UdpOK;
                    NET_BUFFER_LIST_INFO(pNBL, TcpIpChecksumNetBufferListInfo) = qCSInfo.Value;
                    DPrintf(1, ("Reporting CS %X->%X\n", csRes.value, (ULONG)(ULONG_PTR)qCSInfo.Value));
                }
            }
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
    return pNBL;
}


/**********************************************************
NDIS procedure of returning us buffer of previously indicated packets
Parameters:
    context
    PNET_BUFFER_LIST pNBL - list of buffers to free
    returnFlags - is dpc

The procedure frees:
received buffer descriptors back to list of RX buffers
all the allocated MDL structures
all the received NBLs back to our pool
***********************************************************/
VOID ParaNdis6_ReturnNetBufferLists(
    NDIS_HANDLE miniportAdapterContext,
    PNET_BUFFER_LIST pNBL,
    ULONG returnFlags)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)miniportAdapterContext;

    UNREFERENCED_PARAMETER(returnFlags);

    DEBUG_ENTRY(5);
    while (pNBL)
    {
        PNET_BUFFER_LIST pTemp = pNBL;
        pRxNetDescriptor pBuffersDescriptor = (pRxNetDescriptor)pNBL->MiniportReserved[0];
        DPrintf(3, ("  Returned NBL of pBuffersDescriptor %p!\n", pBuffersDescriptor));
        pNBL = NET_BUFFER_LIST_NEXT_NBL(pNBL);
        NET_BUFFER_LIST_NEXT_NBL(pTemp) = NULL;
        NdisFreeNetBufferList(pTemp);
        pBuffersDescriptor->Queue->ReuseReceiveBuffer(pBuffersDescriptor);
    }
    ParaNdis_TestPausing(pContext);
}

/**********************************************************
Pauses of restarts RX activity.
Restart is immediate, pause may be delayed until
NDIS returns all the indicated NBL

Parameters:
    context
    bPause 1/0 - pause or restart
    ONPAUSECOMPLETEPROC Callback to be called when PAUSE finished
Return value:
    SUCCESS if finished synchronously
    PENDING if not, then callback will be called
***********************************************************/
NDIS_STATUS ParaNdis6_ReceivePauseRestart(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN bPause,
    ONPAUSECOMPLETEPROC Callback
    )
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;

    if (bPause)
    {
        CNdisPassiveWriteAutoLock tLock(pContext->m_PauseLock);

        ParaNdis_DebugHistory(pContext, hopInternalReceivePause, NULL, 1, 0, 0);
        if (pContext->m_rxPacketsOutsideRing != 0)
        {
            pContext->ReceiveState = srsPausing;
            pContext->ReceivePauseCompletionProc = Callback;
            status = NDIS_STATUS_PENDING;
        }
        else
        {
            ParaNdis_DebugHistory(pContext, hopInternalReceivePause, NULL, 0, 0, 0);
            pContext->ReceiveState = srsDisabled;
        }
    }
    else
    {
        ParaNdis_DebugHistory(pContext, hopInternalReceiveResume, NULL, 0, 0, 0);
        pContext->ReceiveState = srsEnabled;
    }
    return status;
}

NDIS_STATUS ParaNdis_ExactSendFailureStatus(PARANDIS_ADAPTER *pContext)
{
    NDIS_STATUS status = NDIS_STATUS_FAILURE;
    if (pContext->SendState != srsEnabled ) status = NDIS_STATUS_PAUSED;
    if (!pContext->bConnected) status = NDIS_STATUS_MEDIA_DISCONNECTED;
    if (pContext->bSurprizeRemoved) status = NDIS_STATUS_NOT_ACCEPTED;
    // override NDIS_STATUS_PAUSED is there is a specific reason of implicit paused state
    if (pContext->powerState != NdisDeviceStateD0) status = NDIS_STATUS_LOW_POWER_STATE;
    return status;
}

BOOLEAN ParaNdis_IsSendPossible(PARANDIS_ADAPTER *pContext)
{
    BOOLEAN b;
    b =  !pContext->bSurprizeRemoved && pContext->bConnected && pContext->SendState == srsEnabled;
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
Pauses of restarts TX activity.
Restart is immediate, pause may be delayed until
we return all the NBLs to NDIS

Parameters:
    context
    bPause 1/0 - pause or restart
    ONPAUSECOMPLETEPROC Callback to be called when PAUSE finished
Return value:
    SUCCESS if finished synchronously
    PENDING if not, then callback will be called later
***********************************************************/
NDIS_STATUS ParaNdis6_SendPauseRestart(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN bPause,
    ONPAUSECOMPLETEPROC Callback
)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    DEBUG_ENTRY(4);
    if (bPause)
    {
        ParaNdis_DebugHistory(pContext, hopInternalSendPause, NULL, 1, 0, 0);
        if (pContext->SendState == srsEnabled)
        {
            {
                CNdisPassiveWriteAutoLock tLock(pContext->m_PauseLock);

                pContext->SendState = srsPausing;
                pContext->SendPauseCompletionProc = Callback;
            }

            for (UINT i = 0; i < pContext->nPathBundles; i++)
            {
                if (!pContext->pPathBundles[i].txPath.Pause())
                {
                    status = NDIS_STATUS_PENDING;
                }
            }

            if (status == NDIS_STATUS_SUCCESS)
            {
                pContext->SendState = srsDisabled;
            }
        }
        if (status == NDIS_STATUS_SUCCESS)
        {
            ParaNdis_DebugHistory(pContext, hopInternalSendPause, NULL, 0, 0, 0);
        }
    }
    else
    {
        pContext->SendState = srsEnabled;
        ParaNdis_DebugHistory(pContext, hopInternalSendResume, NULL, 0, 0, 0);
    }
    return status;
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
