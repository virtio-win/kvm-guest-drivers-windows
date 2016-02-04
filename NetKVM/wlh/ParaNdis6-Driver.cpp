/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: ParaNdis6-Driver.c
 *
 * This file contains implementation of NDIS6 driver envelope.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
/* Vista SP4 DDI version hardcoded instead of using the definition from sdkddkver.h,
as NT_PROCESSOR_GROUP has to be defined before ntddk.h->wdm.h and including
sdkddkver.h before ntddk.h cause compilation failure in wdm.h and ntddk.h */

#if NTDDI_VERSION > 0x06000400
#define NT_PROCESSOR_GROUPS
#endif

#include "ParaNdis6.h"
#include "ParaNdis-Oid.h"
#include "kdebugprint.h"
#include "ParaNdis_Debug.h"
#include "ParaNdis_DebugHistory.h"

extern "C"
{
    static MINIPORT_ADD_DEVICE ParaNdis6_AddDevice;
    static MINIPORT_REMOVE_DEVICE ParaNdis6_RemoveDevice;
    static MINIPORT_INITIALIZE ParaNdis6_Initialize;
    static MINIPORT_HALT ParaNdis6_Halt;
    static MINIPORT_UNLOAD ParaNdis6_Unload;
    static MINIPORT_PAUSE ParaNdis6_Pause;
    static MINIPORT_RESTART ParaNdis6_Restart;
    static MINIPORT_CHECK_FOR_HANG ParaNdis6_CheckForHang;
    static MINIPORT_RESET ParaNdis6_Reset;
    static MINIPORT_SHUTDOWN ParaNdis6_AdapterShutdown;
    static MINIPORT_DEVICE_PNP_EVENT_NOTIFY ParaNdis6_DevicePnPEvent;
    static SET_OPTIONS ParaNdis6_SetOptions;
    static MINIPORT_SEND_NET_BUFFER_LISTS ParaNdis6_SendNetBufferLists;
#if NDIS_SUPPORT_NDIS61
    static MINIPORT_DIRECT_OID_REQUEST ParaNdis6x_DirectOidRequest;
    static MINIPORT_CANCEL_DIRECT_OID_REQUEST ParaNdis6x_CancelDirectOidRequest;
#endif
}


//#define NO_VISTA_POWER_MANAGEMENT
ULONG bDisableMSI = FALSE;

static NDIS_HANDLE      DriverHandle;
static LONG             gID = 0;

static const char *ConnectStateName(NDIS_MEDIA_CONNECT_STATE state)
{
    if (state == MediaConnectStateConnected) return "Connected";
    if (state == MediaConnectStateDisconnected) return "Disconnected";
    return "Unknown";
}

static VOID PostLinkState(PARANDIS_ADAPTER *pContext, NDIS_MEDIA_CONNECT_STATE connectState)
{
    NDIS_STATUS_INDICATION  indication;
    NDIS_LINK_STATE         state;
    NdisZeroMemory(&state, sizeof(state));
    state.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    state.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    state.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
    state.MediaConnectState = connectState;
    state.MediaDuplexState = MediaDuplexStateFull;
    state.RcvLinkSpeed = state.XmitLinkSpeed =
        connectState == MediaConnectStateConnected ?
            PARANDIS_MAXIMUM_RECEIVE_SPEED :
            NDIS_LINK_SPEED_UNKNOWN;
    state.PauseFunctions = NdisPauseFunctionsUnsupported;

    NdisZeroMemory(&indication, sizeof(indication));

    indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    indication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    indication.SourceHandle = pContext->MiniportHandle;
    indication.StatusCode = NDIS_STATUS_LINK_STATE;
    indication.StatusBuffer = &state;
    indication.StatusBufferSize = sizeof(state);
    DPrintf(0, ("Indicating %s\n", ConnectStateName(connectState)));
    ParaNdis_DebugHistory(pContext, hopConnectIndication, NULL, connectState, 0, 0);
    NdisMIndicateStatusEx(pContext->MiniportHandle , &indication);
}

VOID ParaNdis_SetLinkState(PARANDIS_ADAPTER *pContext, NDIS_MEDIA_CONNECT_STATE LinkState)
{
    DEBUG_ENTRY(3);

    if (LinkState != pContext->fCurrentLinkState)
    {
        pContext->fCurrentLinkState = LinkState;
        PostLinkState(pContext, LinkState);
    }
}

VOID ParaNdis_SynchronizeLinkState(PARANDIS_ADAPTER *pContext)
{
    ParaNdis_SetLinkState(pContext, pContext->bConnected ? MediaConnectStateConnected
                                                         : MediaConnectStateDisconnected);
}

VOID ParaNdis_SetPowerState(PARANDIS_ADAPTER *pContext, NDIS_DEVICE_POWER_STATE newState)
{
    pContext->powerState = newState;
}

/**********************************************************
Required NDIS handler
Initialize adapter context, prepare it for operation,
set in PAUSED STATE
Return value:
    SUCCESS or kind of error
***********************************************************/
static NDIS_STATUS ParaNdis6_Initialize(
    NDIS_HANDLE miniportAdapterHandle,
    NDIS_HANDLE miniportDriverContext,
    PNDIS_MINIPORT_INIT_PARAMETERS miniportInitParameters)
{
    NDIS_MINIPORT_ADAPTER_ATTRIBUTES        miniportAttributes = {};
    NDIS_STATUS  status = NDIS_STATUS_SUCCESS;
    BOOLEAN bNoPauseOnSuspend = FALSE;
    PARANDIS_ADAPTER *pContext;

    UNREFERENCED_PARAMETER(miniportDriverContext);
    DEBUG_ENTRY(0);
#pragma warning( suppress: 28197)
    /* allocate context structure */
    pContext = (PARANDIS_ADAPTER *)
        NdisAllocateMemoryWithTagPriority(
            miniportAdapterHandle,
            sizeof(PARANDIS_ADAPTER),
            PARANDIS_MEMORY_TAG,
            NormalPoolPriority);

    /* This call is for Static Driver Verifier only - has no real functionality*/
    __sdv_save_adapter_context(pContext);

    if (!pContext)
    {
        DPrintf(0, ("[%s] ERROR: Memory allocation failed!\n", __FUNCTION__));
        status = NDIS_STATUS_RESOURCES;
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        /* set mandatory fields which Common use */
        NdisZeroMemory(pContext, sizeof(PARANDIS_ADAPTER));
        pContext->ulUniqueID = NdisInterlockedIncrement(&gID);
        pContext->DriverHandle = DriverHandle;
        pContext->MiniportHandle = miniportAdapterHandle;
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        NdisAllocateSpinLock(&pContext->m_CompletionLock);
        pContext->m_CompletionLockCreated = true;

        new (&pContext->m_PauseLock, PLACEMENT_NEW) CNdisRWLock();
        if (!pContext->m_PauseLock.Create(pContext->MiniportHandle))
        {
            status = NDIS_STATUS_RESOURCES;
        }
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        NdisZeroMemory(&miniportAttributes, sizeof(miniportAttributes));
        miniportAttributes.RegistrationAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
        miniportAttributes.RegistrationAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
        miniportAttributes.RegistrationAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
        miniportAttributes.RegistrationAttributes.MiniportAdapterContext = pContext;
        miniportAttributes.RegistrationAttributes.AttributeFlags =
            // actual for USB
            // NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK
            NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
            NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER;
#ifndef NO_VISTA_POWER_MANAGEMENT
        miniportAttributes.RegistrationAttributes.AttributeFlags |= NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND;
#endif
#if NDIS_SUPPORT_NDIS630
        miniportAttributes.RegistrationAttributes.AttributeFlags |= NDIS_MINIPORT_ATTRIBUTES_NO_PAUSE_ON_SUSPEND;
        bNoPauseOnSuspend = TRUE;
#endif
        miniportAttributes.RegistrationAttributes.CheckForHangTimeInSeconds = 4;
        miniportAttributes.RegistrationAttributes.InterfaceType = NdisInterfacePci;
        status = NdisMSetMiniportAttributes(miniportAdapterHandle, &miniportAttributes);
        if (status != NDIS_STATUS_SUCCESS)
        {
            DPrintf(0, ("[%s] ERROR: NdisMSetMiniportAttributes 1 failed (%X)!\n", __FUNCTION__, status));
        }
    }

#pragma warning(push)
#pragma warning(disable:28197)
    if (status == NDIS_STATUS_SUCCESS)
    {
        pContext->IODevice = (VirtIODevice *)NdisAllocateMemoryWithTagPriority(
            miniportAdapterHandle,
            sizeof(VirtIODevice),
            PARANDIS_MEMORY_TAG,
            NormalPoolPriority);
    }
#pragma warning(pop)
    if (!pContext->IODevice)
    {
        DPrintf(0, ("[%s] ERROR: IODevice memory allocation failed!\n", __FUNCTION__));
        status = NDIS_STATUS_RESOURCES;
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        /* prepare statistics struct for further reports */
        pContext->Statistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        pContext->Statistics.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
        pContext->Statistics.Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
        /* let Common do all the rest */
        status = ParaNdis_InitializeContext(pContext, miniportInitParameters->AllocatedResources);
        if (status != NDIS_STATUS_SUCCESS)
        {
            DPrintf(0, ("[%s] ERROR: ParaNdis6_InitializeContext failed (%X)!\n", __FUNCTION__, status));
        }
        pContext->bNoPauseOnSuspend = bNoPauseOnSuspend; 
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        NDIS_PNP_CAPABILITIES power60Caps;
#if NDIS_SUPPORT_NDIS620
        NDIS_PM_CAPABILITIES power620Caps;
#endif
#ifdef NO_VISTA_POWER_MANAGEMENT
        pPowerCaps = NULL;
#endif
        ParaNdis_FillPowerCapabilities(&power60Caps);

        NdisZeroMemory(&miniportAttributes, sizeof(miniportAttributes));
        miniportAttributes.GeneralAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
        miniportAttributes.GeneralAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
        miniportAttributes.GeneralAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
#if NDIS_SUPPORT_NDIS620
        miniportAttributes.GeneralAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
        miniportAttributes.GeneralAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
        miniportAttributes.GeneralAttributes.PowerManagementCapabilitiesEx = &power620Caps;
        ParaNdis6_Fill620PowerCapabilities(&power620Caps);
#else
        miniportAttributes.GeneralAttributes.PowerManagementCapabilities = &power60Caps;
#endif
        miniportAttributes.GeneralAttributes.MediaType = NdisMedium802_3;
        miniportAttributes.GeneralAttributes.PhysicalMediumType = NdisPhysicalMedium802_3;
        miniportAttributes.GeneralAttributes.MtuSize = pContext->MaxPacketSize.nMaxDataSize;
        miniportAttributes.GeneralAttributes.LookaheadSize = pContext->MaxPacketSize.nMaxFullSizeOS;
        miniportAttributes.GeneralAttributes.MaxXmitLinkSpeed =
        miniportAttributes.GeneralAttributes.MaxRcvLinkSpeed  =  PARANDIS_FORMAL_LINK_SPEED;
        miniportAttributes.GeneralAttributes.MediaConnectState =
            pContext->bConnected ? MediaConnectStateConnected : MediaConnectStateDisconnected;
        miniportAttributes.GeneralAttributes.XmitLinkSpeed =
        miniportAttributes.GeneralAttributes.RcvLinkSpeed = pContext->bConnected ?
            PARANDIS_FORMAL_LINK_SPEED : NDIS_LINK_SPEED_UNKNOWN;
        miniportAttributes.GeneralAttributes.MediaDuplexState = MediaDuplexStateFull;
        miniportAttributes.GeneralAttributes.MacOptions =
                    NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |       /* NIC has no internal loopback support */
                    NDIS_MAC_OPTION_TRANSFERS_NOT_PEND  |       /* Must be set since using  NdisMIndicateReceivePacket */
                    NDIS_MAC_OPTION_NO_LOOPBACK;                /* NIC has no internal loopback support */
        if (IsPrioritySupported(pContext))
            miniportAttributes.GeneralAttributes.MacOptions |= NDIS_MAC_OPTION_8021P_PRIORITY;
        if (IsVlanSupported(pContext))
            miniportAttributes.GeneralAttributes.MacOptions |= NDIS_MAC_OPTION_8021Q_VLAN;
        miniportAttributes.GeneralAttributes.SupportedPacketFilters = PARANDIS_PACKET_FILTERS;
        miniportAttributes.GeneralAttributes.MaxMulticastListSize = PARANDIS_MULTICAST_LIST_SIZE;
        miniportAttributes.GeneralAttributes.MacAddressLength =     ETH_ALEN;

#if PARANDIS_SUPPORT_RSS
        if (pContext->bRSSOffloadSupported)
        {
            miniportAttributes.GeneralAttributes.RecvScaleCapabilities =
                ParaNdis6_RSSCreateConfiguration(
                                                &pContext->RSSParameters,
                                                &pContext->RSSCapabilities,
                                                pContext->RSSMaxQueuesNumber);
            pContext->bRSSInitialized = TRUE;
        }

        new (&pContext->RSSParameters.rwLock, PLACEMENT_NEW) CNdisRWLock();
        if (!pContext->RSSParameters.rwLock.Create(pContext->MiniportHandle))
        {
            DPrintf(0, ("RSS RW lock allocation failed\n"));
            status = NDIS_STATUS_RESOURCES;
        }

        for(ULONG i = 0; i < ARRAYSIZE(pContext->ReceiveQueues); i++)
        {
            NdisAllocateSpinLock(&pContext->ReceiveQueues[i].Lock);
            InitializeListHead(&pContext->ReceiveQueues[i].BuffersList);
        }

        pContext->ReceiveQueuesInitialized = TRUE;
#endif

        miniportAttributes.GeneralAttributes.AccessType = NET_IF_ACCESS_BROADCAST;
        miniportAttributes.GeneralAttributes.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
        miniportAttributes.GeneralAttributes.IfType = IF_TYPE_ETHERNET_CSMACD;
        miniportAttributes.GeneralAttributes.IfConnectorPresent = TRUE;
        miniportAttributes.GeneralAttributes.ConnectionType = NET_IF_CONNECTION_DEDICATED;

        ETH_COPY_NETWORK_ADDRESS(miniportAttributes.GeneralAttributes.PermanentMacAddress, pContext->PermanentMacAddress);
        ETH_COPY_NETWORK_ADDRESS(miniportAttributes.GeneralAttributes.CurrentMacAddress, pContext->CurrentMacAddress);

        ParaNdis6_GetSupportedOid(&miniportAttributes.GeneralAttributes);
        /* update also SupportedStatistics in ready to use statistics block */
        pContext->Statistics.SupportedStatistics = ParaNdis6_GetSupportedStatisticsFlags();
        status = NdisMSetMiniportAttributes(miniportAdapterHandle, &miniportAttributes);
        if (status != NDIS_STATUS_SUCCESS)
        {
            DPrintf(0, ("[%s] ERROR: NdisMSetMiniportAttributes 2 failed (%X)!\n", __FUNCTION__, status));
        }
    }

    if (pContext && status != NDIS_STATUS_SUCCESS && status != NDIS_STATUS_PENDING)
    {
#if PARANDIS_SUPPORT_RSS
        pContext->RSSParameters.rwLock.~CNdisRWLock();

        if (pContext->ReceiveQueuesInitialized)
        {
            ULONG i;

            for (i = 0; i < ARRAYSIZE(pContext->ReceiveQueues); i++)
            {
                NdisFreeSpinLock(&pContext->ReceiveQueues[i].Lock);
            }
        }
#endif
        if (pContext->m_CompletionLockCreated)
        {
            NdisFreeSpinLock(&pContext->m_CompletionLock);
        }

        pContext->m_PauseLock.~CNdisRWLock();

        if (pContext->IODevice != nullptr)
            NdisFreeMemoryWithTagPriority(pContext->MiniportHandle, pContext->IODevice, PARANDIS_MEMORY_TAG);


        NdisFreeMemory(pContext, 0, 0);
        pContext = NULL;
    }

    if (pContext && status == NDIS_STATUS_SUCCESS)
    {
        status = ParaNdis_FinishInitialization(pContext);
        if (status != NDIS_STATUS_SUCCESS)
        {
            ParaNdis_CleanupContext(pContext);
            NdisFreeMemory(pContext, 0, 0);
            pContext = NULL;
        }
    }
    if (pContext && status == NDIS_STATUS_SUCCESS)
    {
        if (NDIS_STATUS_SUCCESS ==
            ParaNdis6_GetRegistrationOffloadInfo(pContext,
                &miniportAttributes.OffloadAttributes))
            status = NdisMSetMiniportAttributes(miniportAdapterHandle, &miniportAttributes);
            if (status != NDIS_STATUS_SUCCESS)
            {
                DPrintf(0, ("[%s] ERROR: NdisMSetMiniportAttributes 3 failed (%X)!\n", __FUNCTION__, status));
            }
    }

    if (pContext && status == NDIS_STATUS_SUCCESS)
    {
        ParaNdis_DebugRegisterMiniport(pContext, TRUE);
    }

    DEBUG_EXIT_STATUS(status ? 0 : 2, status);
    return status;
}


/**********************************************************
called at IRQL = PASSIVE_LEVEL
Called on disable, on removal, on standby (if required)
***********************************************************/
static VOID ParaNdis6_Halt(NDIS_HANDLE miniportAdapterContext, NDIS_HALT_ACTION haltAction)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)miniportAdapterContext;
    DEBUG_ENTRY(0);
    ParaNdis_DebugHistory(pContext, hopHalt, NULL, 1, haltAction, 0);
    ParaNdis_CleanupContext(pContext);
    ParaNdis_DebugHistory(pContext, hopHalt, NULL, 0, 0, 0);
    ParaNdis_DebugRegisterMiniport(pContext, FALSE);
    NdisFreeMemory(pContext, 0, 0);
    DEBUG_EXIT_STATUS(2, 0);
}

/**********************************************************
Deregister driver
Clean up WPP
***********************************************************/
static VOID ParaNdis6_Unload(IN PDRIVER_OBJECT pDriverObject)
{
    DEBUG_ENTRY(0);
    if (DriverHandle) NdisMDeregisterMiniportDriver(DriverHandle);
    DEBUG_EXIT_STATUS(2, 0);
    ParaNdis_DebugCleanup(pDriverObject);
}

/**********************************************************
    callback from asynchronous RECEIVE PAUSE
***********************************************************/
static void OnReceivePauseComplete(void *ctx)
{
    DEBUG_ENTRY(0);
    // pause exit
    PPARANDIS_ADAPTER pContext = (PPARANDIS_ADAPTER) ctx;
    ParaNdis_DebugHistory(pContext, hopSysPause, NULL, 0, 0, 0);
    NdisMPauseComplete(pContext->MiniportHandle);
}

/**********************************************************
    callback from asynchronous SEND PAUSE
***********************************************************/
static void OnSendPauseComplete(void *ctx)
{
    PPARANDIS_ADAPTER pContext = (PPARANDIS_ADAPTER) ctx;
    NDIS_STATUS status;
    DEBUG_ENTRY(0);
    status = ParaNdis6_ReceivePauseRestart(pContext, TRUE, OnReceivePauseComplete);
    if (status != NDIS_STATUS_PENDING)
    {
        // pause exit
        ParaNdis_DebugHistory(pContext, hopSysPause, NULL, 0, 0, 0);
        NdisMPauseComplete(pContext->MiniportHandle);
    }
}


/**********************************************************
Required NDIS handler
called at IRQL = PASSIVE_LEVEL
Must pause RX and TX
Called before halt, on standby,
upon protocols activation
***********************************************************/
static NDIS_STATUS ParaNdis6_Pause(
        NDIS_HANDLE                         miniportAdapterContext,
        PNDIS_MINIPORT_PAUSE_PARAMETERS     miniportPauseParameters)
{
    NDIS_STATUS  status;
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)miniportAdapterContext;
    DEBUG_ENTRY(0);

    UNREFERENCED_PARAMETER(miniportPauseParameters);

    ParaNdis_DebugHistory(pContext, hopSysPause, NULL, 1, 0, 0);
    status = ParaNdis6_SendPauseRestart(pContext, TRUE, OnSendPauseComplete);
    if (status != NDIS_STATUS_PENDING)
    {
        status = ParaNdis6_ReceivePauseRestart(pContext, TRUE, OnReceivePauseComplete);
    }
    DEBUG_EXIT_STATUS(0, status);
    if (status == STATUS_SUCCESS)
    {
        // pause exit
        ParaNdis_DebugHistory(pContext, hopSysPause, NULL, 0, 0, 0);
    }
    return status;
}

/**********************************************************
Required NDIS handler
called at IRQL = PASSIVE_LEVEL
Must pause RX and TX
Called upon startup, on resume,
upon protocols activation
***********************************************************/
static NDIS_STATUS ParaNdis6_Restart(
    NDIS_HANDLE                         miniportAdapterContext,
    PNDIS_MINIPORT_RESTART_PARAMETERS   miniportRestartParameters)
{
    NDIS_STATUS  status = NDIS_STATUS_SUCCESS;
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)miniportAdapterContext;
    DEBUG_ENTRY(0);

    UNREFERENCED_PARAMETER(miniportRestartParameters);

    ParaNdis_DebugHistory(pContext, hopSysResume, NULL, 1, 0, 0);
    ParaNdis6_SendPauseRestart(pContext, FALSE, NULL);
    ParaNdis6_ReceivePauseRestart(pContext, FALSE, NULL);

    ParaNdis_DebugHistory(pContext, hopSysResume, NULL, 0, 0, 0);
    DEBUG_EXIT_STATUS(2, status);
    return status;
}


/**********************************************************
Required NDIS handler
called at IRQL <= DISPATCH_LEVEL
***********************************************************/
static VOID ParaNdis6_SendNetBufferLists(
    NDIS_HANDLE miniportAdapterContext,
    PNET_BUFFER_LIST    pNBL,
    NDIS_PORT_NUMBER    portNumber,
    ULONG               /* sendFlags */)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)miniportAdapterContext;
    UNREFERENCED_PARAMETER(portNumber);
#ifdef PARANDIS_SUPPORT_RSS
    if (pContext->RSS2QueueMap != nullptr)
    {
        while (pNBL)
        {
            ULONG RSSHashValue = NET_BUFFER_LIST_GET_HASH_VALUE(pNBL);
            ULONG indirectionIndex = RSSHashValue & (pContext->RSSParameters.ActiveRSSScalingSettings.RSSHashMask);

            PNET_BUFFER_LIST nextNBL = NET_BUFFER_LIST_NEXT_NBL(pNBL);
            NET_BUFFER_LIST_NEXT_NBL(pNBL) = NULL;

            pContext->RSS2QueueMap[indirectionIndex]->txPath.Send(pNBL);
            pNBL = nextNBL;
        }
    }
    else
    {
        pContext->pPathBundles[0].txPath.Send(pNBL);
    }
#else
    pContext->pPathBundles[0].txPath.Send(pNBL);
#endif
}

/**********************************************************
Required NDIS handler: happens unually each 2 second
***********************************************************/
static BOOLEAN ParaNdis6_CheckForHang(NDIS_HANDLE miniportAdapterContext)
{
    UNREFERENCED_PARAMETER(miniportAdapterContext);
    return FALSE;
}


/**********************************************************
    callback from asynchronous SEND PAUSE on reset
***********************************************************/
static void OnSendPauseCompleteOnReset(void *ctx)
{
    DEBUG_ENTRY(0);
    PPARANDIS_ADAPTER pContext = (PPARANDIS_ADAPTER) ctx;
    NdisSetEvent(&pContext->ResetEvent);
}

/**********************************************************
    callback from asynchronous RECEIVE PAUSE on reset
***********************************************************/
static void OnReceivePauseCompleteOnReset(void *ctx)
{
    DEBUG_ENTRY(0);
    PPARANDIS_ADAPTER pContext = (PPARANDIS_ADAPTER) ctx;
    NdisSetEvent(&pContext->ResetEvent);
}

VOID ParaNdis_Suspend(PARANDIS_ADAPTER *pContext)
{
    DPrintf(0, ("[%s]%s\n", __FUNCTION__, pContext->bFastSuspendInProcess ? "(Fast)" : ""));
    NdisResetEvent(&pContext->ResetEvent);
    if (NDIS_STATUS_PENDING != ParaNdis6_SendPauseRestart(pContext, TRUE, OnSendPauseCompleteOnReset))
    {
        NdisSetEvent(&pContext->ResetEvent);
    }
    NdisWaitEvent(&pContext->ResetEvent, 0);
    if (!pContext->bFastSuspendInProcess)
    {
        NdisResetEvent(&pContext->ResetEvent);
        if (NDIS_STATUS_PENDING != ParaNdis6_ReceivePauseRestart(pContext, TRUE, OnReceivePauseCompleteOnReset))
        {
            NdisSetEvent(&pContext->ResetEvent);
        }
        NdisWaitEvent(&pContext->ResetEvent, 0);
    }
    else
    {
        ParaNdis6_ReceivePauseRestart(pContext, TRUE, NULL);
    }
    DEBUG_EXIT_STATUS(0, 0);
}

/**********************************************************
Required NDIS handler for RESET operation
Never happens under normal condition, only if
OID or other call returns PENDING and not completed or if
ParaNdis6_CheckForHang returns true
***********************************************************/
static NDIS_STATUS ParaNdis6_Reset(
        NDIS_HANDLE miniportAdapterContext,
        PBOOLEAN  pAddressingReset)
{
    UNREFERENCED_PARAMETER(miniportAdapterContext);
    *pAddressingReset = FALSE;
    return NDIS_STATUS_SUCCESS;
}

VOID ParaNdis_Resume(PARANDIS_ADAPTER *pContext)
{
    DPrintf(0, ("[%s] %s\n", __FUNCTION__, pContext->bFastSuspendInProcess ? " Resuming TX and RX" : "(nothing to do)"));
    if (pContext->bFastSuspendInProcess)
    {
        ParaNdis6_SendPauseRestart(pContext, FALSE, NULL);
        ParaNdis6_ReceivePauseRestart(pContext, FALSE, NULL);
    }
}


/***************************************************
Required NDIS handler for adapter shutdown
should not call NDIS functions
The reason may be a system shutdown ( IRQL <= DPC)
may be bugcheck (arbitrary IRQL)
***************************************************/
static VOID ParaNdis6_AdapterShutdown(
    NDIS_HANDLE miniportAdapterContext,
    NDIS_SHUTDOWN_ACTION  shutdownAction)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)miniportAdapterContext;

    UNREFERENCED_PARAMETER(shutdownAction);

    ParaNdis_OnShutdown(pContext);
}

/**********************************************************
Required NDIS handler for PnP event
***********************************************************/
static VOID ParaNdis6_DevicePnPEvent(
    NDIS_HANDLE miniportAdapterContext,
    PNET_DEVICE_PNP_EVENT pNetEvent)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)miniportAdapterContext;
    ParaNdis_OnPnPEvent(pContext,
        pNetEvent->DevicePnPEvent,
        pNetEvent->InformationBuffer,
        pNetEvent->InformationBufferLength);

}

static NDIS_STATUS  ParaNdis6_AddDevice(IN NDIS_HANDLE  MiniportAdapterHandle, IN NDIS_HANDLE  MiniportDriverContext)
{
    NDIS_MINIPORT_ADAPTER_ATTRIBUTES  MiniportAttributes;
    NDIS_STATUS status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    DEBUG_ENTRY(0);
    MiniportAttributes.AddDeviceRegistrationAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES;
    MiniportAttributes.AddDeviceRegistrationAttributes.Header.Revision = NDIS_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES_REVISION_1;
    MiniportAttributes.AddDeviceRegistrationAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES_REVISION_1;
    MiniportAttributes.AddDeviceRegistrationAttributes.MiniportAddDeviceContext = MiniportAdapterHandle;
    MiniportAttributes.AddDeviceRegistrationAttributes.Flags = 0;
    status = NdisMSetMiniportAttributes(MiniportAdapterHandle, &MiniportAttributes);
    return status;
}

static VOID ParaNdis6_RemoveDevice (IN NDIS_HANDLE  MiniportAddDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportAddDeviceContext);

    DEBUG_ENTRY(0);
}

static NDIS_STATUS ParaNdis6_StartDevice(IN NDIS_HANDLE  MiniportAddDeviceContext, IN PIRP  Irp)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(MiniportAddDeviceContext);
    UNREFERENCED_PARAMETER(Irp);

    DEBUG_ENTRY(0);
    return status;
}

typedef struct _tagRRLData
{
    PIO_RESOURCE_REQUIREMENTS_LIST prrl;
    PIO_RESOURCE_LIST currentList;
    PIO_RESOURCE_DESCRIPTOR currentDesc;
}tRRLData;

/******************************************************************
Replacement of resource requirement list: initialize the new list
*******************************************************************/
static void InitializeNewResourceRequirementsList(
    tRRLData *pData,
    PIO_RESOURCE_REQUIREMENTS_LIST newList,
    PIO_RESOURCE_REQUIREMENTS_LIST oldList)
{
    pData->prrl = newList;
    pData->currentList = NULL;
    pData->currentDesc = NULL;
    if (pData->prrl)
    {
        ULONG len = RtlPointerToOffset(pData->prrl, &pData->prrl->List[0]);
        RtlCopyMemory(newList, oldList, len);
        newList->ListSize = len;
        newList->AlternativeLists = 0;
    }
}

/******************************************************************
Replacement of resource requirement list: adding new resource list
to existing resource requirement list
*******************************************************************/
static void AddNewResourceList(tRRLData *pData, PIO_RESOURCE_LIST pior)
{
    if (pData->prrl)
    {
        ULONG len = RtlPointerToOffset(pior, &pior->Descriptors[0]);
        pData->currentList = (PIO_RESOURCE_LIST)RtlOffsetToPointer(pData->prrl, pData->prrl->ListSize);
        RtlCopyMemory(pData->currentList, pior, len);
        pData->currentList->Count = 0;
        pData->prrl->ListSize += len;
        pData->prrl->AlternativeLists++;
        pData->currentDesc = &pData->currentList->Descriptors[0];
    }
}

/******************************************************************
Replacement of resource requirement list: done with new resource list,
verify if it contains all the required resources
*******************************************************************/
static void FinalizeResourceList(tRRLData *pData)
{
    if (pData->prrl && pData->currentList)
    {
        BOOLEAN bFound = FALSE;
        ULONG len = RtlPointerToOffset(pData->currentList, &pData->currentList->Descriptors[0]);
        UINT i;
        for (i = 0; i < pData->currentList->Count && !bFound; ++i)
        {
            len += sizeof(IO_RESOURCE_DESCRIPTOR);
            if (pData->currentList->Descriptors[i].Type == CmResourceTypeInterrupt) bFound = TRUE;
        }
        if (!bFound)
        {
            pData->prrl->AlternativeLists--;
            pData->prrl->ListSize -= len;
        }
    }
}


/******************************************************************
Replacement of resource requirement list: adding new resource descriptor
to current resource list
*******************************************************************/
static void AddNewResourceDescriptor(tRRLData *pData, PIO_RESOURCE_DESCRIPTOR prd)
{
    if (pData->prrl && pData->currentList && pData->currentDesc)
    {
        *(pData->currentDesc) = *prd;
        pData->currentList->Count++;
        pData->prrl->ListSize += sizeof(IO_RESOURCE_DESCRIPTOR);
        pData->currentDesc++;
    }
}

static void PrintPRRL(PIO_RESOURCE_REQUIREMENTS_LIST prrl)
{
    PIO_RESOURCE_LIST list;

    list = prrl->List;

    for (ULONG ix = 0; ix < prrl->AlternativeLists; ++ix)
    {
        DPrintf(0, ("[%s] List # %ld, count %lu\n", __FUNCTION__, ix, list->Count));
        for (ULONG jx = 0; jx < list->Count; ++jx)
        {
            PIO_RESOURCE_DESCRIPTOR desc;

            desc = list->Descriptors + jx;

            switch (desc->Type)
            {
            case CmResourceTypePort:
                DPrintf(0, ("CmResourceTypePort, align 0x%lx, length %lu, min/max 0x%llx/0x%llx\n", desc->u.Port.Alignment, desc->u.Port.Length, desc->u.Port.MinimumAddress, desc->u.Port.MaximumAddress));
                break;
            case CmResourceTypeInterrupt:
                DPrintf(0, ("CmResourceTypeInterrupt, max/min 0x%lx/0x%lx affinity 0x%llx\n", desc->u.Interrupt.MinimumVector, desc->u.Interrupt.MaximumVector, desc->u.Interrupt.TargetedProcessors));
                break;
            case CmResourceTypeMemory:
                DPrintf(0, ("CmResourceTypeMemory align %lu, length %lu, min 0x%llx, max 0x%llx\n", desc->u.Memory.Alignment, desc->u.Memory.Length, desc->u.Memory.MinimumAddress, desc->u.Memory.MaximumAddress));
                break;
            default: 
                break;
            }
        }
        list = (PIO_RESOURCE_LIST)(list->Descriptors + list->Count);
    }
}

static void SetupInterrruptAffinity(PIO_RESOURCE_REQUIREMENTS_LIST prrl)
{
    PIO_RESOURCE_LIST list;
    ULONG procIndex = 0;

    list = prrl->List;

    for (ULONG ix = 0; ix < prrl->AlternativeLists; ++ix)
    {
        for (ULONG jx = 0; jx < list->Count; ++jx)
        {
            PIO_RESOURCE_DESCRIPTOR desc;

            desc = list->Descriptors + jx;

            if (desc->Type == CmResourceTypeInterrupt && (desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            {
                desc->u.Interrupt.AffinityPolicy = IrqPolicySpecifiedProcessors;
#if defined(NT_PROCESSOR_GROUPS)
                PROCESSOR_NUMBER procNumber;
                NDIS_STATUS status;

                status = KeGetProcessorNumberFromIndex(procIndex, &procNumber);
                if (status == STATUS_SUCCESS)
                {
                    desc->Flags |= CM_RESOURCE_INTERRUPT_POLICY_INCLUDED;
                    desc->u.Interrupt.Group = procNumber.Group;
                    desc->u.Interrupt.TargetedProcessors = 1i64 << procNumber.Number;
                }
                else
                {
                    DPrintf(0, ("[%s] - can't convert index %u into processor number\n", __FUNCTION__, procIndex));
                }
#else
                desc->u.Interrupt.TargetedProcessors = 1i64 << procIndex;
#endif
                if (jx % 2 == 1)
                {
                    procIndex++;

                    if (procIndex == ParaNdis_GetSystemCPUCount())
                    {
                        procIndex = 0;
                    }
                }
            }
        }
        list = (PIO_RESOURCE_LIST)(list->Descriptors + list->Count);
    }
}

/******************************************************************
Replacement of resource requirement list, when needed:
The procedure traverses over all the resource lists in existing resource requirement list
(we receive it in IRP information field).
When the driver is not built to work with MSI resources, we must remove them from the
resource requirement list, otherwise the driver will fail to initialize
Typically MSI interrupts are labeled as preferred ones, when line interrupts are labeled as
alternative resources. Removing message interrupts, remove also "alternative" label from line interrupts.
*******************************************************************/
static PIO_RESOURCE_REQUIREMENTS_LIST ParseFilterResourceIrp(
    IN NDIS_HANDLE  MiniportAddDeviceContext,
    PIO_RESOURCE_REQUIREMENTS_LIST prrl,
    BOOLEAN bRemoveMSIResources)
{
    tRRLData newRRLData;
    ULONG nRemoved = 0;
    UINT nInterrupts = 0;
    PIO_RESOURCE_REQUIREMENTS_LIST newPrrl = NULL;
    ULONG QueueNumber;
    BOOLEAN MSIResourceListed = FALSE;
#if NDIS_SUPPORT_NDIS620    
    QueueNumber = NdisGroupActiveProcessorCount(ALL_PROCESSOR_GROUPS) * 2 + 1;
#elif NDIS_SUPPORT_NDIS6
    QueueNumber = NdisSystemProcessorCount() * 2 + 1;
#else
    QueueNumber = 0; /* Don't create MSI resource descriptors*/
#endif


    if (QueueNumber > 2048)
        QueueNumber = 2048;

    DPrintf(0, ("[%s]%s\n", __FUNCTION__, bRemoveMSIResources ? "(Remove MSI resources...)" : "(Don't remove MSI resources)"));

    newPrrl = (PIO_RESOURCE_REQUIREMENTS_LIST)NdisAllocateMemoryWithTagPriority(
            MiniportAddDeviceContext,
            prrl->ListSize + (bRemoveMSIResources ? 0 : QueueNumber * sizeof(IO_RESOURCE_DESCRIPTOR)),
            PARANDIS_MEMORY_TAG,
            NormalPoolPriority);

    InitializeNewResourceRequirementsList(&newRRLData, newPrrl, prrl);
    if (prrl)
    {
        ULONG n, offset;
        PVOID p = &prrl->List[0];
        DPrintf(0, ("[%s] %d bytes, %d lists\n", __FUNCTION__, prrl->ListSize, prrl->AlternativeLists));
        offset = RtlPointerToOffset(prrl, p);
        for (n = 0; n < prrl->AlternativeLists && offset < prrl->ListSize; ++n)
        {
            ULONG nDesc;
            IO_RESOURCE_LIST *pior = (IO_RESOURCE_LIST *)p;
            if ((offset + sizeof(*pior)) < prrl->ListSize)
            {
                IO_RESOURCE_DESCRIPTOR *pd = &pior->Descriptors[0];
                DPrintf(0, ("[%s]+%d %d:%d descriptors follow\n", __FUNCTION__, offset, n, pior->Count));
                offset += RtlPointerToOffset(p, pd);
                AddNewResourceList(&newRRLData, pior);
                for (nDesc = 0; nDesc < pior->Count; ++nDesc)
                {
                    BOOLEAN bRemove = FALSE;
                    if ((offset + sizeof(*pd)) <= prrl->ListSize)
                    {
                        if (pd->Type == CmResourceTypeInterrupt)
                        {
                            nInterrupts++;
                            DPrintf(0, ("[%s] CmResourceTypeInterrupt, min/max = %lx/%lx Option = 0x%lx, ShareDisposition = %u \n", __FUNCTION__, pd->u.Interrupt.MinimumVector, pd->u.Interrupt.MaximumVector,
                                pd->Option, pd->ShareDisposition));
                            if (pd->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
                            {
                                MSIResourceListed = TRUE;
                                bRemove = bRemoveMSIResources;
                            }
                            else
                            {
                                // reset IO_RESOURCE_ALTERNATIVE attribute on Line Interrupt,
                                // if we remove MSI vectors, otherwise Windows will not allocate it for the device
                                if (bRemoveMSIResources && (pd->Option & IO_RESOURCE_ALTERNATIVE))
                                {
                                    pd->Option &= ~IO_RESOURCE_ALTERNATIVE;
                                }
                            }
                        }
                        if (!bRemove) AddNewResourceDescriptor(&newRRLData, pd);
                        else nRemoved++;
                    }
                    offset += sizeof(*pd);
                    pd = (IO_RESOURCE_DESCRIPTOR *)RtlOffsetToPointer(prrl, offset);
                }

                DPrintf(0, ("[%s] MSI resources %s listed\n", __FUNCTION__, MSIResourceListed ? "" : "not"));

                FinalizeResourceList(&newRRLData);
                p = pd;
            }
        }
    }
    if (!bRemoveMSIResources)
    {
        SetupInterrruptAffinity(newPrrl);
    }
    if (bRemoveMSIResources && nRemoved)
    {
        DPrintf(0, ("[%s] %d resources removed\n", __FUNCTION__, nRemoved));
    }
    return newPrrl;
}

/******************************************************************
Registered procedure for filtering if resource requirement
Required when the device supports MSI, but the driver decides - will it work with MSI or not
(currently MSI is supported but does not work, exactly our case).
In this case the resource requirement list must be replaced - we need to remove from it
all the "message interrupt" resources.

When we are ready to work with MSI (VIRTIO_USE_MSIX_INTERRUPT is DEFINED),
we just enumerate allocated resources and do not modify them.
*******************************************************************/
static NDIS_STATUS ParaNdis6_FilterResource(IN NDIS_HANDLE  MiniportAddDeviceContext, IN PIRP  Irp)
{
    DPrintf(0, ("[%s] entered\n", __FUNCTION__));
    PIO_RESOURCE_REQUIREMENTS_LIST prrl = (PIO_RESOURCE_REQUIREMENTS_LIST)(PVOID)Irp->IoStatus.Information;

    PrintPRRL(prrl);

    PIO_RESOURCE_REQUIREMENTS_LIST newPrrl = ParseFilterResourceIrp(MiniportAddDeviceContext, prrl, BOOLEAN(bDisableMSI));

    if (newPrrl)
    {
        Irp->IoStatus.Information = (ULONG_PTR)newPrrl;
        NdisFreeMemory(prrl, 0, 0);
        PrintPRRL(newPrrl);
    }
    else
    {
        DPrintf(0, ("[%s] Resource requirement unchanged\n", __FUNCTION__));
    }
    return NDIS_STATUS_SUCCESS;
}



/******************************************************************************
This procedure required when we want to be able filtering resource requirements.
ParaNdis6_AddDevice need to register context (to allow other procedures to allocate memory)
ParaNdis6_FilterResource able to replace resource requirements list if needed
ParaNdis6_RemoveDevice does not do anything if other procedures do not allocate any resources
 which must be freed upon device removal
******************************************************************************/
static NDIS_STATUS ParaNdis6_SetOptions(IN  NDIS_HANDLE NdisDriverHandle, IN  NDIS_HANDLE DriverContext)
{
    NDIS_STATUS status;
    NDIS_MINIPORT_PNP_CHARACTERISTICS pnpChars;

    UNREFERENCED_PARAMETER(DriverContext);

    pnpChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_PNP_CHARACTERISTICS;
    pnpChars.Header.Revision = NDIS_MINIPORT_PNP_CHARACTERISTICS_REVISION_1;
    pnpChars.Header.Size = NDIS_SIZEOF_MINIPORT_PNP_CHARACTERISTICS_REVISION_1;
    pnpChars.MiniportAddDeviceHandler = ParaNdis6_AddDevice;
    pnpChars.MiniportRemoveDeviceHandler = ParaNdis6_RemoveDevice;
    pnpChars.MiniportStartDeviceHandler = ParaNdis6_StartDevice;
    pnpChars.MiniportFilterResourceRequirementsHandler = ParaNdis6_FilterResource;
    status = NdisSetOptionalHandlers(NdisDriverHandle, (PNDIS_DRIVER_OPTIONAL_HANDLERS)&pnpChars);
    return status;
}

static NDIS_STATUS ReadGlobalConfigurationEntry(NDIS_HANDLE cfg, const char *_name, PULONG pValue)
{
    NDIS_STATUS status;
    PNDIS_CONFIGURATION_PARAMETER pParam = NULL;
    NDIS_STRING name = {};
    const char *statusName;
    NDIS_PARAMETER_TYPE ParameterType = NdisParameterInteger;
    NdisInitializeString(&name, (PUCHAR)_name);
#pragma warning(push)
#pragma warning(disable:6102)
    NdisReadConfiguration(
        &status,
        &pParam,
        cfg,
        &name,
        ParameterType);
    if (status == NDIS_STATUS_SUCCESS)
    {
        *pValue = pParam->ParameterData.IntegerData;
        statusName = "value";
    }
    else
    {
        statusName = "nothing";
    }
#pragma warning(pop)
    DPrintf(2, ("[%s] %s read for %s - 0x%x\n", __FUNCTION__, statusName, _name, *pValue));
    if (name.Buffer) NdisFreeString(name);
    return status;
}

static void RetrieveDriverConfiguration()
{
    NDIS_CONFIGURATION_OBJECT co;
    NDIS_HANDLE cfg, params;
    NDIS_STATUS status;
    DEBUG_ENTRY(2);
    co.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    co.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    co.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
    co.Flags = 0;
    co.NdisHandle = DriverHandle;
    status = NdisOpenConfigurationEx(&co, &cfg);
    if (status == NDIS_STATUS_SUCCESS)
    {
        NDIS_STRING paramsName = {};
        NdisInitializeString(&paramsName, (PUCHAR)"Parameters");

#pragma warning(push)
#pragma warning(disable:6102) // Using <param> from failed function call at line...
        NdisOpenConfigurationKeyByName(&status, cfg, &paramsName, &params);
        if (status == NDIS_STATUS_SUCCESS)
#pragma warning(pop)
        {
            ReadGlobalConfigurationEntry(params, "DisableMSI", &bDisableMSI);
            NdisCloseConfiguration(params);
        }
        NdisCloseConfiguration(cfg);
        if (paramsName.Buffer) NdisFreeString(paramsName);
    }
}

#if NDIS_SUPPORT_NDIS61
static NDIS_STATUS ParaNdis6x_DirectOidRequest(IN  NDIS_HANDLE miniportAdapterContext,  IN  PNDIS_OID_REQUEST OidRequest)
{
    NDIS_STATUS  status = NDIS_STATUS_NOT_SUPPORTED;
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)miniportAdapterContext;

    if (pContext->bSurprizeRemoved) status = NDIS_STATUS_NOT_ACCEPTED;

    DPrintf(1, ("[%s] came %s\n", __FUNCTION__, ParaNdis_OidName(OidRequest->DATA.SET_INFORMATION.Oid)));

    return status;
}

static VOID ParaNdis6x_CancelDirectOidRequest(IN  NDIS_HANDLE miniportAdapterContext,  IN  PVOID RequestId)
{
    UNREFERENCED_PARAMETER(miniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}
#endif

/**********************************************************
Driver entry point:
Register miniport driver
Initialize WPP
Return value:
    status of driver registration
***********************************************************/
// this is for OACR

extern "C"
{
DRIVER_INITIALIZE DriverEntry;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath)
{
    NDIS_STATUS                             status = NDIS_STATUS_FAILURE;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS    chars;
#ifdef DEBUG_TIMING
    LARGE_INTEGER TickCount;
    LARGE_INTEGER SysTime;
#endif DEBUG_TIMING

    ParaNdis_DebugInitialize();

    DEBUG_ENTRY(0);
    DPrintf(0, (__DATE__ " " __TIME__ "built %d.%d\n", NDIS_MINIPORT_MAJOR_VERSION, NDIS_MINIPORT_MINOR_VERSION));
#ifdef DEBUG_TIMING
    KeQueryTickCount(&TickCount);
    NdisGetCurrentSystemTime(&SysTime);
    DPrintf(0, ("\n%s>> CPU #%d, perf-counter %I64d, tick count %I64d, NDIS_sys_time %I64d\n", __FUNCTION__, KeGetCurrentProcessorNumber(), KeQueryPerformanceCounter(NULL).QuadPart,TickCount.QuadPart, SysTime.QuadPart));
#endif

    NdisZeroMemory(&chars, sizeof(chars));

    chars.Header.Type      = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    chars.Header.Revision  = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    chars.Header.Size      = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    chars.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    chars.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    /* stupid thing, they are at least short */
    chars.MajorDriverVersion = (UCHAR)(PARANDIS_MAJOR_DRIVER_VERSION & 0xFF);
    chars.MinorDriverVersion = (UCHAR)(PARANDIS_MINOR_DRIVER_VERSION & 0xFF);

    // possible value for regular miniport NDIS_WDM_DRIVER - for USB or 1394
    // chars.Flags  = 0;

    chars.InitializeHandlerEx           = ParaNdis6_Initialize;
    chars.HaltHandlerEx                 = ParaNdis6_Halt;
    chars.UnloadHandler                 = ParaNdis6_Unload;
    chars.PauseHandler                  = ParaNdis6_Pause;
    chars.RestartHandler                = ParaNdis6_Restart;
    chars.OidRequestHandler             = ParaNdis6_OidRequest;
    chars.CancelOidRequestHandler       = ParaNdis6_OidCancelRequest;
    chars.SendNetBufferListsHandler     = ParaNdis6_SendNetBufferLists;
    chars.CancelSendHandler             = ParaNdis6_CancelSendNetBufferLists;
    chars.ReturnNetBufferListsHandler   = ParaNdis6_ReturnNetBufferLists;
    chars.CheckForHangHandlerEx         = ParaNdis6_CheckForHang;
    chars.ResetHandlerEx                = ParaNdis6_Reset;
    chars.ShutdownHandlerEx             = ParaNdis6_AdapterShutdown;
    chars.DevicePnPEventNotifyHandler   = ParaNdis6_DevicePnPEvent;
    chars.SetOptionsHandler             = ParaNdis6_SetOptions;

#if NDIS_SUPPORT_NDIS61
    chars.Header.Revision  = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    chars.Header.Size      = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    chars.DirectOidRequestHandler       = ParaNdis6x_DirectOidRequest;
    chars.CancelDirectOidRequestHandler = ParaNdis6x_CancelDirectOidRequest;
#endif

    status = NdisMRegisterMiniportDriver(
            pDriverObject,
            pRegistryPath,
            NULL,
            &chars,
            &DriverHandle);

    if (status == NDIS_STATUS_SUCCESS)
    {
        RetrieveDriverConfiguration();
    }
    DEBUG_EXIT_STATUS(status ? 0 : 4, status);
    return status;
}
