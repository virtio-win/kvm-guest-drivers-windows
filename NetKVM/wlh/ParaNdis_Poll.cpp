#include "ParaNdis6.h"
#include "kdebugprint.h"
#include "Trace.h"

#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_Poll.tmh"
#endif

#if PARANDIS_SUPPORT_POLL

void ParaNdisPollNotify(PPARANDIS_ADAPTER pContext, UINT Index)
{
    DPrintf(POLL_PRINT_LEVEL, " notify #%d\n", Index);
    NdisPollHandler* poll = &pContext->PollHandlers[Index];
    while (true)
    {
        if (poll->m_EnableNotify.AddRef() <= 1)
        {
            DPrintf(POLL_PRINT_LEVEL, " trigger #%d\n", Index);
            NdisRequestPoll(poll->m_PollContext, NULL);
            break;
        }
        LONG val = poll->m_EnableNotify.Release();
        if (val > 0)
            break;
    }
}

static void UpdatePollAffinities(PPARANDIS_ADAPTER pContext)
{
    UINT done = 0;
    for (UINT i = 0; i < ARRAYSIZE(pContext->PollHandlers); ++i)
    {
        NdisPollHandler* poll = &pContext->PollHandlers[i];
        if (poll->m_UpdateAffinity)
        {
            poll->m_UpdateAffinity = false;
            done++;
            NdisSetPollAffinity(poll->m_PollContext, &poll->m_ProcessorNumber);
        }
    }
    DPrintf(0, "updated #%d affinities\n", done);
}

void ParaNdisPollSetAffinity(PARANDIS_ADAPTER* pContext, const CCHAR* Indices, CCHAR Size)
{
    bool needUpdate = false;
    for (CCHAR i = 0; i < Size && i < ARRAYSIZE(pContext->PollHandlers); ++i)
    {
        LONG index = Indices[i];
        if (index >= 0)
        {
            PROCESSOR_NUMBER number;
            if (KeGetProcessorNumberFromIndex(index, &number) == STATUS_SUCCESS)
            {
                NdisPollHandler* poll = &pContext->PollHandlers[i];
                if (number.Group != poll->m_ProcessorNumber.Group || number.Number != poll->m_ProcessorNumber.Number)
                {
                    poll->m_ProcessorNumber = number;
                    poll->m_UpdateAffinity = true;
                    needUpdate = true;
                }
            }
        }
    }
    if (needUpdate)
    {
        NDIS_HANDLE hwo = NdisAllocateIoWorkItem(pContext->MiniportHandle);
        if (hwo)
        {
            NdisQueueIoWorkItem(hwo,
                [](PVOID  WorkItemContext, NDIS_HANDLE  NdisIoWorkItemHandle)
                {
                    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER*)WorkItemContext;
                    UpdatePollAffinities(pContext);
                    NdisFreeIoWorkItem(NdisIoWorkItemHandle);
                },
                pContext);
        }
    }
}

void NdisPollHandler::EnableNotification(BOOLEAN Enable)
{
    DPrintf(POLL_PRINT_LEVEL, "[%s] #%d = %X\n", __FUNCTION__, m_Index, Enable);
    if (Enable)
    {
        m_EnableNotify.Release();
    }
}

void NdisPollHandler::HandlePoll(NDIS_POLL_DATA* PollData)
{
    DPrintf(POLL_PRINT_LEVEL, "[%s] #%d\n", __FUNCTION__, m_Index);
    CDpcIrqlRaiser raise;

    // RX
    RxPoll(m_AdapterContext, m_Index, PollData->Receive);
    if (PollData->Receive.NumberOfIndicatedNbls)
    {
        DPrintf(POLL_PRINT_LEVEL, "[%s] RX #%d indicated %d, max %d, still here %d\n",
            __FUNCTION__, m_Index, PollData->Receive.NumberOfIndicatedNbls,
            PollData->Receive.MaxNblsToIndicate, PollData->Receive.NumberOfRemainingNbls);
    }

    // TX
    if ((UINT)m_Index < m_AdapterContext->nPathBundles)
    {
        CPUPathBundle* bundle = &m_AdapterContext->pPathBundles[m_Index];
        if (bundle->txPath.DoPendingTasks(NULL))
        {
            PollData->Transmit.NumberOfRemainingNbls = NDIS_ANY_NUMBER_OF_NBLS;
        }
    }
}

#else

void ParaNdisPollSetAffinity(PARANDIS_ADAPTER*, const CCHAR*, CCHAR)
{
}

void ParaNdisPollNotify(PPARANDIS_ADAPTER, UINT)
{
}

#endif

bool NdisPollHandler::Register(PPARANDIS_ADAPTER AdapterContext, int Index)
{
    m_AdapterContext = AdapterContext;
    m_Index = Index;
    m_ProcessorNumber.Group = 0xffff;
    m_ProcessorNumber.Number = 0xff;

#if PARANDIS_SUPPORT_POLL
    NDIS_POLL_CHARACTERISTICS chars;
    chars.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    chars.Header.Revision = NDIS_POLL_CHARACTERISTICS_REVISION_1;
    chars.Header.Size = NDIS_SIZEOF_NDIS_POLL_CHARACTERISTICS_REVISION_1;
    chars.PollHandler = [](void* Context, NDIS_POLL_DATA* PollData)
    {
        NdisPollHandler* poll = (NdisPollHandler*)Context;
        poll->HandlePoll(PollData);
    };
    chars.SetPollNotificationHandler = [](void* Context, NDIS_POLL_NOTIFICATION* Notification)
    {
        NdisPollHandler* poll = (NdisPollHandler*)Context;
        poll->EnableNotification(Notification->Enabled);
    };
    NDIS_STATUS status = NdisRegisterPoll(AdapterContext->MiniportHandle, this, &chars, &m_PollContext);
    DPrintf(0, "[%s] poll #%d, status %X\n", __FUNCTION__, m_Index, status);
    return m_PollContext != NULL;
#else
    return false;
#endif
}

void NdisPollHandler::Unregister()
{
#if PARANDIS_SUPPORT_POLL
    if (m_PollContext)
    {
        NdisDeregisterPoll(m_PollContext);
    }
#endif
}
