#pragma once

#include "ParaNdis-Util.h"

class CDataFlowStateMachine : public CPlacementAllocatable
{
public:
    void Start()
    {
        TSpinLocker LockedContext(m_ObjectLock);

        NETKVM_ASSERT(m_State == FlowState::Stopped);
        NETKVM_ASSERT(m_NumOutstandingItems == 0);

        m_NoOutstandingItems.Clear();

        m_State = FlowState::Running;
        m_NumOutstandingItems = 1;
    }

    void Stop(NDIS_STATUS Reason = NDIS_STATUS_PAUSED)
    {
        m_ObjectLock.Lock();

        NETKVM_ASSERT(m_State == FlowState::Running);
        m_State = FlowState::Stopping;
        m_StopReason = Reason;

        m_ObjectLock.Unlock();

        UnregisterOutstandingItem();

        m_NoOutstandingItems.Wait();
    }

    bool RegisterOutstandingItems(ULONG NumItems,
                                  NDIS_STATUS *FailureReason = nullptr)
    {
        TSpinLocker LockedContext(m_ObjectLock);

        if (m_State != FlowState::Running)
        {
            if (FailureReason != nullptr)
            {
                *FailureReason = m_StopReason;
            }
            return false;
        }

        m_NumOutstandingItems += NumItems;
        return true;
    }

    void UnregisterOutstandingItems(ULONG NumItems)
    {
        TSpinLocker LockedContext(m_ObjectLock);

        NETKVM_ASSERT(m_State != FlowState::Stopped);
        NETKVM_ASSERT(m_NumOutstandingItems >= NumItems);

        m_NumOutstandingItems -= NumItems;

        if (m_NumOutstandingItems == 0)
        {
            NETKVM_ASSERT(m_State == FlowState::Stopping);
            m_State = FlowState::Stopped;
            m_NoOutstandingItems.Notify();
        }
    }

    bool RegisterOutstandingItem()
    { return RegisterOutstandingItems(1); }

    void UnregisterOutstandingItem()
    { UnregisterOutstandingItems(1); }

    CDataFlowStateMachine() = default;
    ~CDataFlowStateMachine() = default;
    CDataFlowStateMachine(const CDataFlowStateMachine&) = delete;
    CDataFlowStateMachine& operator= (const CDataFlowStateMachine&) = delete;

private:

    using FlowState = enum
    {
        Running,
        Stopping,
        Stopped
    };

    ULONG m_NumOutstandingItems = 0;
    FlowState m_State = FlowState::Stopped;
    CNdisSpinLock m_ObjectLock;
    CNdisEvent m_NoOutstandingItems;
    NDIS_STATUS m_StopReason = NDIS_STATUS_PAUSED;

    DECLARE_CNDISLIST_ENTRY(CDataFlowStateMachine);
};

class CMiniportStateMachine : public CPlacementAllocatable
{
public:
        void RegisterFlow(CDataFlowStateMachine &Flow)
        { m_Flows.PushBack(&Flow); }

        void UnregisterFlow(CDataFlowStateMachine &Flow)
        { m_Flows.Remove(&Flow); }

        void NotifyInitialized()
        { ChangeState(MiniportState::Paused, MiniportState::Halted); }

        void NotifyShutdown()
        { ChangeState(MiniportState::Shutdown,
                      MiniportState::Paused,
                      MiniportState::Running); }

        void NotifyRestarted()
        {
            StartFlows();
            ChangeState(MiniportState::Running, MiniportState::Paused);
        }

        void NotifyPaused()
        {
            StopFlows(NDIS_STATUS_PAUSED);
            ChangeState(MiniportState::Paused, MiniportState::Running);
        }

        void NotifyResumed()
        {
            if (IsInState(MiniportState::FastSuspend))
            {
                StartFlows();
                ChangeState(MiniportState::Running, MiniportState::FastSuspend);
            }
            else
            {
                ChangeState(MiniportState::Paused, MiniportState::Suspended);
            }

        }

        void NotifySuspended()
        {
            if (IsInState(MiniportState::Running))
            {
                StopFlows(NDIS_STATUS_LOW_POWER_STATE);
                ChangeState(MiniportState::FastSuspend, MiniportState::Running);
            }
            else
            {
                ChangeState(MiniportState::Suspended, MiniportState::Paused);
            }
        }

        CMiniportStateMachine() = default;
        ~CMiniportStateMachine() = default;
        CMiniportStateMachine(const CMiniportStateMachine&) = delete;
        CMiniportStateMachine& operator= (const CMiniportStateMachine&) = delete;

private:
    using MiniportState = enum
    {
        Halted,
        Running,
        Paused,
        Shutdown,
        Suspended,
        FastSuspend
    };

    bool IsInState(MiniportState State) const
    { return m_State == State; }

    template <typename... Args>
    bool IsInState(MiniportState State, Args... MoreStates) const
    { return IsInState(State) || IsInState(MoreStates...); }

    template <typename... Args>
    void ChangeState(MiniportState NewState, Args... AllowedStates)
    {
        NETKVM_ASSERT(IsInState(AllowedStates...));
        m_State = NewState;
    }

    void StartFlows()
    { m_Flows.ForEach([](CDataFlowStateMachine* Flow) { Flow->Start(); }); }

    void StopFlows(NDIS_STATUS Reason)
    { m_Flows.ForEach([Reason](CDataFlowStateMachine* Flow) { Flow->Stop(Reason); }); }

    MiniportState m_State = MiniportState::Halted;
    CNdisList<CDataFlowStateMachine, CRawAccess, CNonCountingObject> m_Flows;
};
