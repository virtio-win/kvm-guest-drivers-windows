#pragma once

#include "ParaNdis-Util.h"

class CDataFlowStateMachine : public CPlacementAllocatable
{
public:
    void Start()
    {
        NETKVM_ASSERT(m_State == FlowState::Stopped);
        m_Counter.AddRef();
        m_State = FlowState::Running;
        m_Counter.ClearMask(StoppedMask);
    }

    void Stop(NDIS_STATUS Reason = NDIS_STATUS_PAUSED)
    {
        NETKVM_ASSERT(m_State == FlowState::Running);
        m_State = FlowState::Stopping;
        m_StopReason = Reason;
        m_NoOutstandingItems.Clear();
        m_Counter.SetMask(StoppedMask);
        UnregisterOutstandingItem();
        m_NoOutstandingItems.Wait();
    }

    bool RegisterOutstandingItems(ULONG NumItems,
                                  NDIS_STATUS *FailureReason = nullptr)
    {
        auto value = m_Counter.AddRef(NumItems);
        if (value & StoppedMask)
        {
            value = m_Counter.Release(NumItems);
            if (value == StoppedMask)
            {
                CompleteStopping();
            }
            if (FailureReason != nullptr)
            {
                *FailureReason = m_StopReason;
            }
            return false;
        }
        return true;
    }

    void UnregisterOutstandingItems(ULONG NumItems)
    {
        NETKVM_ASSERT(m_State != FlowState::Stopped);
        LONG value = m_Counter.Release(NumItems);
        if (value == StoppedMask)
        {
            CompleteStopping();
        }
        else if (value)
        {
            // common case, data transfer (StoppedMask not set)
            // pausing or completing not last packet during pausing (StoppedMask set)
        }
        else
        {
            // illegal case
            NETKVM_ASSERT(value != 0);
        }
    }

    bool RegisterOutstandingItem()
    { return RegisterOutstandingItems(1); }

    void UnregisterOutstandingItem()
    { UnregisterOutstandingItems(1); }

    CDataFlowStateMachine() { m_Counter.SetMask(StoppedMask); }
    ~CDataFlowStateMachine() = default;
    CDataFlowStateMachine(const CDataFlowStateMachine&) = delete;
    CDataFlowStateMachine& operator= (const CDataFlowStateMachine&) = delete;

private:
    void CompleteStopping()
    {
        TPassiveSpinLocker lock(m_CompleteStoppingLock);
        if (m_State == FlowState::Stopping)
        {
            m_State = FlowState::Stopped;
            m_NoOutstandingItems.Notify();
        }
    }

    enum { StoppedMask = 0x40000000 };

    using FlowState = enum
    {
        Running,
        Stopping,
        Stopped
    };

    CNdisRefCounter m_Counter;
    FlowState m_State = FlowState::Stopped;
    CNdisSpinLock m_CompleteStoppingLock;
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
    void ChangeState(MiniportState NewState,
                     Args...
#ifdef DBG
                     AllowedStates
#endif
                    )
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
