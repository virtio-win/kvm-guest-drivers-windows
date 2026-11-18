#pragma once
#include "ParaNdis-VirtQueue.h"
#include "ndis56common.h"
#include "ParaNdis-AbstractPath.h"

class CParaNdisCX : public CParaNdisTemplatePath<CVirtQueue>, public CPlacementAllocatable
{
  public:
    CParaNdisCX(PPARANDIS_ADAPTER Context);
    ~CParaNdisCX();

    bool Create(UINT DeviceQueueIndex);

    virtual NDIS_STATUS SetupMessageIndex(u16 vector);

    BOOLEAN CParaNdisCX::SendControlMessage(UCHAR cls,
                                            UCHAR cmd,
                                            PVOID buffer1,
                                            ULONG size1,
                                            PVOID buffer2,
                                            ULONG size2,
                                            int levelIfOK);

    bool FireDPC(ULONG messageId) override;
    void Maintain(UINT MaxLoops = MAXUINT);

  protected:
    tCompletePhysicalAddress m_ControlData;
    // m_ControlData is in the Device Memory Buffer, not shared guest RAM.
    bool m_ControlDataInDmb = false;
    // Class and command of the last command written to m_ControlData, kept
    // here because the device can rewrite m_ControlData.
    UCHAR m_LastCls = 0;
    UCHAR m_LastCmd = 0;
    KDPC m_DPC;
    // updated under m_Lock
    ULONG m_ResultOffset = 0;
    struct CommandData
    {
        UCHAR cls;
        UCHAR cmd;
        PVOID buffer1;
        ULONG size1;
        PVOID buffer2;
        ULONG size2;
        int logLevel;
    };
    void FillSGArray(struct VirtIOBufferDescriptor sg[/*4*/], const CommandData &data, UINT &nOut);
    BOOLEAN SendInternal(const CommandData &data, bool initial);
    bool GetResponse(UCHAR &Code, int MicrosecondsToWait, int LogLevel);
    class CQueuedCommand : public CNdisAllocatable<CQueuedCommand, 'CQXC'>
    {
      public:
        CQueuedCommand(PPARANDIS_ADAPTER Ctx) : m_Context(Ctx)
        {
        }
        bool Create(const CommandData &Data);
        ~CQueuedCommand();
        const CommandData &Data()
        {
            return m_Data;
        }

      private:
        PPARANDIS_ADAPTER m_Context;
        CommandData m_Data = {};
        DECLARE_CNDISLIST_ENTRY(CQueuedCommand);
    };
    // used under m_Lock
    CNdisList<CQueuedCommand, CRawAccess, CCountingObject> m_CommandQueue;
    bool ScheduleCommand(const CommandData &Data);
    bool ReadyForControls()
    {
        return m_ControlData.Virtual && m_VirtQueue.IsValid() && m_VirtQueue.CanTouchHardware();
    }
    class CPendingCommand
    {
      public:
        CPendingCommand()
        {
            Clear();
        }
        bool Pending() const
        {
            return m_Present;
        }

      private:
        void Clear()
        {
            m_Present = m_Counted = false;
        }
        // returns true on first call, false of next ones
        bool Set()
        {
            bool b = m_Counted;
            m_Present = m_Counted = true;
            return !b;
        }
        bool m_Present;
        bool m_Counted;
        friend bool CParaNdisCX::GetResponse(UCHAR &Code, int MicrosecondsToWait, int LogLevel);
    };
    // updated under m_Lock
    CPendingCommand m_PendingCommand;
};
