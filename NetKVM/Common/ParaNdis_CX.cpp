#include "ndis56common.h"
#include "virtio_net.h"
#include "kdebugprint.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_CX.tmh"
#endif

#define LONG_CTL_TIMEOUT  500000
#define SHORT_CTL_TIMEOUT 1000

CParaNdisCX::CParaNdisCX(PPARANDIS_ADAPTER Context)
{
    m_Context = Context;
    m_ControlData.Virtual = nullptr;
    KeInitializeDpc(&m_DPC, MiniportMSIInterruptCXDpc, m_Context);
}

CParaNdisCX::~CParaNdisCX()
{
    CLockedContext<CNdisSpinLock> autoLock(m_Lock);
    if (m_ControlData.Virtual != nullptr)
    {
        ParaNdis_FreePhysicalMemory(m_Context, &m_ControlData);
    }
    m_CommandQueue.ForEachDetached([&](CQueuedCommand *e) { CQueuedCommand::Destroy(e, m_Context->MiniportHandle); });
}

bool CParaNdisCX::Create(UINT DeviceQueueIndex)
{
    m_queueIndex = (u16)DeviceQueueIndex;

    if (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, 512, &m_ControlData))
    {
        DPrintf(0, "CParaNdisCX::Create - ParaNdis_InitialAllocatePhysicalMemory failed for %u\n", DeviceQueueIndex);
        m_ControlData.Virtual = nullptr;
        return false;
    }

    m_Context->m_CxStateMachine.Start();

    CreatePath();

    return m_VirtQueue.Create(DeviceQueueIndex, &m_Context->IODevice, m_Context->MiniportHandle);
}

// should be called under m_Lock
// fills the data area with command parameters
// returns the offset of the response field
void CParaNdisCX::FillSGArray(struct VirtIOBufferDescriptor sg[/*4*/], const CommandData &data, UINT &nOut)
{
    PUCHAR pBase = (PUCHAR)m_ControlData.Virtual;
    PHYSICAL_ADDRESS phBase = m_ControlData.Physical;
    ULONG offset = 0;
    nOut = 1;

    ((virtio_net_ctrl_hdr *)pBase)->class_of_command = data.cls;
    ((virtio_net_ctrl_hdr *)pBase)->cmd = data.cmd;
    sg[0].physAddr = phBase;
    sg[0].length = sizeof(virtio_net_ctrl_hdr);
    offset += (USHORT)sg[0].length;
    offset = (offset + 3) & ~3;
    if (data.size1)
    {
        NdisMoveMemory(pBase + offset, data.buffer1, data.size1);
        sg[nOut].physAddr = phBase;
        sg[nOut].physAddr.QuadPart += offset;
        sg[nOut].length = data.size1;
        offset += data.size1;
        offset = (offset + 3) & ~3;
        nOut++;
    }
    if (data.size2)
    {
        NdisMoveMemory(pBase + offset, data.buffer2, data.size2);
        sg[nOut].physAddr = phBase;
        sg[nOut].physAddr.QuadPart += offset;
        sg[nOut].length = data.size2;
        offset += data.size2;
        offset = (offset + 3) & ~3;
        nOut++;
    }
    sg[nOut].physAddr = phBase;
    sg[nOut].physAddr.QuadPart += offset;
    sg[nOut].length = sizeof(virtio_net_ctrl_ack);
    *(virtio_net_ctrl_ack *)(pBase + offset) = VIRTIO_NET_ERR;
    m_ResultOffset = offset;
}

// should be called under m_Lock
// returns false it there is no response at all
// if there is some response returns true and
// sets Code to received response code if possible
bool CParaNdisCX::GetResponse(UCHAR &Code, int MicrosecondsToWait, int LogLevel)
{
    PUCHAR pBase = (PUCHAR)m_ControlData.Virtual;
    UINT len = 0;
    auto ctl = (virtio_net_ctrl_hdr *)pBase;
    UCHAR cls = ctl->class_of_command;
    UCHAR cmd = ctl->cmd;
    void *p;

    p = m_VirtQueue.GetBuf(&len);
    for (int i = 0; i < MicrosecondsToWait && !p; ++i)
    {
        UINT interval = 1;
        NdisStallExecution(interval);
        p = m_VirtQueue.GetBuf(&len);
    }
    if (!p)
    {
        // timed out
        DPrintf(0, "%s - ERROR: cmd %d.%d timed out\n", __FUNCTION__, cls, cmd);
        m_Context->extraStatistics.ctrlTimedOut += m_PendingCommand.Set();
        return false;
    }
    if (len == sizeof(virtio_net_ctrl_ack))
    {
        // the response status is probably OK or ERR
        Code = *(virtio_net_ctrl_ack *)(pBase + m_ResultOffset);
        m_Context->extraStatistics.ctrlFailed += Code != VIRTIO_NET_OK;
        switch (Code)
        {
            case VIRTIO_NET_OK:
                DPrintf(LogLevel, "%s: - %d.%d finished OK\n", __FUNCTION__, cls, cmd);
                break;
            case VIRTIO_NET_ERR:
                DPrintf(0, "%s - VIRTIO_NET_ERROR returned for %d.%d\n", __FUNCTION__, cls, cmd);
                break;
            default:
                // TODO: raise error if the code is not one of these
                DPrintf(0, "%s: unexpected ERROR %d for %d.%d\n", __FUNCTION__, Code, cls, cmd);
                break;
        }
        m_PendingCommand.Clear();
        return true;
    }
    // the length of response is wrong, we can't expect
    // meaningful result, the device is probably broken
    DPrintf(0, "%s - ERROR: wrong len %d on %d.%d\n", __FUNCTION__, len, cls, cmd);
    m_Context->extraStatistics.ctrlFailed++;
    Code = (UCHAR)(-1);
    // TODO: raise error
    m_PendingCommand.Clear();
    return true;
}

BOOLEAN CParaNdisCX::SendControlMessage(UCHAR cls,
                                        UCHAR cmd,
                                        PVOID buffer1,
                                        ULONG size1,
                                        PVOID buffer2,
                                        ULONG size2,
                                        int levelIfOK)
{
    if (m_ControlData.size <= (size1 + size2 + 16))
    {
        DPrintf(0, "%s (buffer %d,%d) - ERROR: message too LARGE\n", __FUNCTION__, size1, size2);
        m_Context->extraStatistics.ctrlFailed++;
        return FALSE;
    }
    CommandData data;
    data.cls = cls;
    data.cmd = cmd;
    data.buffer1 = buffer1;
    data.buffer2 = buffer2;
    data.size1 = size1;
    data.size2 = size2;
    data.logLevel = levelIfOK;
    CLockedContext<CNdisSpinLock> autoLock(m_Lock);
    return SendInternal(data, true);
}

// called under m_Lock
// initial = true when called from SendControlMessage
// initial = true when called from Maintain
// timeout is long
// if cvq is busy OR the list is full - queue current command
// if possible, continue with one from the list
// timeout is short
// do not schedule current command and do not get it from the list
BOOLEAN CParaNdisCX::SendInternal(const CommandData &data, bool initial)
{
    UCHAR result = VIRTIO_NET_ERR;
    BOOLEAN bOK = FALSE;
    UINT nOut = 0;
    if (m_ControlData.Virtual && m_VirtQueue.IsValid() && m_VirtQueue.CanTouchHardware())
    {
        struct VirtIOBufferDescriptor sg[4];
        int logLevel = data.logLevel;
        if (m_PendingCommand.Pending())
        {
            GetResponse(result, SHORT_CTL_TIMEOUT, 0);
        }
        if (initial && (m_PendingCommand.Pending() || !m_CommandQueue.IsEmpty()))
        {
            ScheduleCommand(data);
        }
        if (m_PendingCommand.Pending())
        {
            return FALSE;
        }
        if (initial && !m_CommandQueue.IsEmpty())
        {
            // retrieve first queued command
            CQueuedCommand *e = m_CommandQueue.Pop();
            // use the data from it
            FillSGArray(sg, e->Data(), nOut);
            logLevel = e->Data().logLevel;
            CQueuedCommand::Destroy(e, m_Context->MiniportHandle);
        }
        else
        {
            FillSGArray(sg, data, nOut);
        }

        m_Context->extraStatistics.ctrlCommands++;

        if (0 <= m_VirtQueue.AddBuf(sg, nOut, 1, (PVOID)1, NULL, 0))
        {
            ULONG timeout = initial ? LONG_CTL_TIMEOUT : SHORT_CTL_TIMEOUT;

            m_Context->m_CxStateMachine.RegisterOutstandingItem();

            m_VirtQueue.Kick();

            if (GetResponse(result, timeout, logLevel))
            {
                // OK/error/invalid
                bOK = result == VIRTIO_NET_OK;
            }
            // we just keep the previous behavior at the moment
            // although the command is inside and can't be aborted
            m_Context->m_CxStateMachine.UnregisterOutstandingItem();
        }
        else
        {
            DPrintf(0, "%s - ERROR: add_buf failed\n", __FUNCTION__);
            m_Context->extraStatistics.ctrlFailed++;
        }
    }
    else
    {
        DPrintf(0, "%s: control queue is not ready\n", __FUNCTION__);
        m_Context->extraStatistics.ctrlFailed++;
    }
    return bOK;
}

NDIS_STATUS CParaNdisCX::SetupMessageIndex(u16 vector)
{
    DPrintf(0, "[%s] Using message %u for controls\n", __FUNCTION__, vector);

    virtio_set_config_vector(&m_Context->IODevice, vector);

    return CParaNdisAbstractPath::SetupMessageIndex(vector);
}

bool CParaNdisCX::FireDPC(ULONG messageId)
{
    DPrintf(0, "[%s] message %u\n", __FUNCTION__, messageId);
    KeInsertQueueDpc(&m_DPC, NULL, NULL);
    return TRUE;
}

bool CParaNdisCX::CQueuedCommand::Create(const CommandData &Data)
{
    m_Data = Data;
    m_Data.buffer1 = m_Data.buffer2 = NULL;
    if (Data.buffer1 && Data.size1)
    {
        m_Data.buffer1 = ParaNdis_AllocateMemory(m_Context, m_Data.size1);
        if (m_Data.buffer1)
        {
            NdisMoveMemory(m_Data.buffer1, Data.buffer1, m_Data.size1);
        }
    }
    if (Data.buffer2 && Data.size2)
    {
        m_Data.buffer2 = ParaNdis_AllocateMemory(m_Context, m_Data.size2);
        if (m_Data.buffer2)
        {
            NdisMoveMemory(m_Data.buffer2, Data.buffer2, m_Data.size2);
        }
    }
    return (!m_Data.size1 || m_Data.buffer1) && (!m_Data.size2 || m_Data.buffer2);
}

CParaNdisCX::CQueuedCommand::~CQueuedCommand()
{
    if (m_Data.buffer1)
    {
        NdisFreeMemory(m_Data.buffer1, 0, 0);
    }
    if (m_Data.buffer2)
    {
        NdisFreeMemory(m_Data.buffer2, 0, 0);
    }
}

bool CParaNdisCX::ScheduleCommand(const CommandData &Data)
{
    CQueuedCommand *e = new (m_Context->MiniportHandle) CQueuedCommand(m_Context);
    if (e && e->Create(Data))
    {
        m_CommandQueue.PushBack(e);
        DPrintf(0, "%s: command %d.%d scheduled\n", __FUNCTION__, Data.cls, Data.cmd);
        return true;
    }
    if (e)
    {
        CQueuedCommand::Destroy(e, m_Context->MiniportHandle);
    }
    DPrintf(0, "%s: failed to %s %d.%d\n", __FUNCTION__, e ? "schedule" : "allocate", Data.cls, Data.cmd);
    return false;
}

// to be called from CX DRPC
// check pending command and submit the
// next one if possible
void CParaNdisCX::Maintain(UINT MaxLoops)
{
    CLockedContext<CNdisSpinLock> autoLock(m_Lock);
    UCHAR result = VIRTIO_NET_ERR;
    if (!m_PendingCommand.Pending() || GetResponse(result, SHORT_CTL_TIMEOUT, 0))
    {
        UINT n = 1;
        while (!m_CommandQueue.IsEmpty() && n++ <= MaxLoops)
        {
            CQueuedCommand *e = m_CommandQueue.Pop();
            SendInternal(e->Data(), false);
            CQueuedCommand::Destroy(e, m_Context->MiniportHandle);
            if (m_PendingCommand.Pending())
            {
                break;
            }
        }
    }
    m_VirtQueue.Restart();
}
