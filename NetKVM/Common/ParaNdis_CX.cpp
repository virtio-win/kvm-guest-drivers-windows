#include "ndis56common.h"
#include "virtio_net.h"
#include "kdebugprint.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_CX.tmh"
#endif

CParaNdisCX::CParaNdisCX(PPARANDIS_ADAPTER Context)
{
    m_Context = Context;
    m_ControlData.Virtual = nullptr;
    KeInitializeDpc(&m_DPC, MiniportMSIInterruptCXDpc, m_Context);
}

CParaNdisCX::~CParaNdisCX()
{
    if (m_ControlData.Virtual != nullptr)
    {
        ParaNdis_FreePhysicalMemory(m_Context, &m_ControlData);
    }
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
ULONG CParaNdisCX::FillSGArray(struct VirtIOBufferDescriptor sg[/*4*/], CommandData &data, UINT &nOut)
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
    return offset;
}

BOOLEAN CParaNdisCX::SendControlMessage(UCHAR cls,
                                        UCHAR cmd,
                                        PVOID buffer1,
                                        ULONG size1,
                                        PVOID buffer2,
                                        ULONG size2,
                                        int levelIfOK)
{
    BOOLEAN bOK = FALSE;
    PUCHAR pBase = (PUCHAR)m_ControlData.Virtual;
    UINT nOut = 0;
    CommandData data;
    data.cls = cls;
    data.cmd = cmd;
    data.buffer1 = buffer1;
    data.buffer2 = buffer2;
    data.size1 = size1;
    data.size2 = size2;
    data.logLevel = levelIfOK;
    CLockedContext<CNdisSpinLock> autoLock(m_Lock);

    if (m_ControlData.Virtual && m_ControlData.size > (size1 + size2 + 16) && m_VirtQueue.IsValid() &&
        m_VirtQueue.CanTouchHardware())
    {
        struct VirtIOBufferDescriptor sg[4];
        ULONG offset = FillSGArray(sg, data, nOut);

        m_Context->extraStatistics.ctrlCommands++;

        if (0 <= m_VirtQueue.AddBuf(sg, nOut, 1, (PVOID)1, NULL, 0))
        {
            UINT len;
            void *p;
            m_Context->m_CxStateMachine.RegisterOutstandingItem();

            m_VirtQueue.Kick();
            p = m_VirtQueue.GetBuf(&len);
            for (int i = 0; i < 500000 && !p; ++i)
            {
                UINT interval = 1;
                NdisStallExecution(interval);
                p = m_VirtQueue.GetBuf(&len);
            }
            m_Context->m_CxStateMachine.UnregisterOutstandingItem();

            if (!p)
            {
                DPrintf(0, "%s - ERROR: get_buf failed\n", __FUNCTION__);
                m_Context->extraStatistics.ctrlTimedOut++;
            }
            else if (len != sizeof(virtio_net_ctrl_ack))
            {
                DPrintf(0, "%s - ERROR: wrong len %d\n", __FUNCTION__, len);
                m_Context->extraStatistics.ctrlFailed++;
            }
            else if (*(virtio_net_ctrl_ack *)(pBase + offset) != VIRTIO_NET_OK)
            {
                DPrintf(0,
                        "%s - ERROR: error %d returned for class %d\n",
                        __FUNCTION__,
                        *(virtio_net_ctrl_ack *)(pBase + offset),
                        cls);
                m_Context->extraStatistics.ctrlFailed++;
            }
            else
            {
                // everything is OK
                DPrintf(levelIfOK, "%s OK(%d.%d,buffers of %d and %d) \n", __FUNCTION__, cls, cmd, size1, size2);
                bOK = TRUE;
            }
        }
        else
        {
            DPrintf(0, "%s - ERROR: add_buf failed\n", __FUNCTION__);
            m_Context->extraStatistics.ctrlFailed++;
        }
    }
    else
    {
        DPrintf(0, "%s (buffer %d,%d) - ERROR: message too LARGE\n", __FUNCTION__, size1, size2);
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
