#include "ndis56common.h"
#include "virtio_net.h"
#include "kdebugprint.h"

CParaNdisCX::CParaNdisCX()
{
    m_ControlData.size = 512;
    m_ControlData.Virtual = nullptr;
}

CParaNdisCX::~CParaNdisCX()
{
    if (m_ControlData.Virtual != nullptr)
    {
        ParaNdis_FreePhysicalMemory(m_Context, &m_ControlData);
    }
}

bool CParaNdisCX::Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex)
{
    m_Context = Context;
    m_queueIndex = (u16)DeviceQueueIndex;

    if (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, &m_ControlData))
    {
        DPrintf(0, ("CParaNdisCX::Create - ParaNdis_InitialAllocatePhysicalMemory failed for %u\n",
            DeviceQueueIndex));
        m_ControlData.Virtual = nullptr;
        return false;
    }

    return m_VirtQueue.Create(DeviceQueueIndex,
        m_Context->IODevice,
        m_Context->MiniportHandle,
        m_Context->bDoPublishIndices ? true : false);
}

BOOLEAN CParaNdisCX::SendControlMessage(
    UCHAR cls,
    UCHAR cmd,
    PVOID buffer1,
    ULONG size1,
    PVOID buffer2,
    ULONG size2,
    int levelIfOK
    )
{
    BOOLEAN bOK = FALSE;
    CLockedContext<CNdisSpinLock> autoLock(m_Lock);

    if (m_ControlData.Virtual && m_ControlData.size > (size1 + size2 + 16))
    {
        struct VirtIOBufferDescriptor sg[4];
        PUCHAR pBase = (PUCHAR)m_ControlData.Virtual;
        PHYSICAL_ADDRESS phBase = m_ControlData.Physical;
        ULONG offset = 0;
        UINT nOut = 1;

        ((virtio_net_ctrl_hdr *)pBase)->class_of_command = cls;
        ((virtio_net_ctrl_hdr *)pBase)->cmd = cmd;
        sg[0].physAddr = phBase;
        sg[0].length = sizeof(virtio_net_ctrl_hdr);
        offset += sg[0].length;
        offset = (offset + 3) & ~3;
        if (size1)
        {
            NdisMoveMemory(pBase + offset, buffer1, size1);
            sg[nOut].physAddr = phBase;
            sg[nOut].physAddr.QuadPart += offset;
            sg[nOut].length = size1;
            offset += size1;
            offset = (offset + 3) & ~3;
            nOut++;
        }
        if (size2)
        {
            NdisMoveMemory(pBase + offset, buffer2, size2);
            sg[nOut].physAddr = phBase;
            sg[nOut].physAddr.QuadPart += offset;
            sg[nOut].length = size2;
            offset += size2;
            offset = (offset + 3) & ~3;
            nOut++;
        }
        sg[nOut].physAddr = phBase;
        sg[nOut].physAddr.QuadPart += offset;
        sg[nOut].length = sizeof(virtio_net_ctrl_ack);
        *(virtio_net_ctrl_ack *)(pBase + offset) = VIRTIO_NET_ERR;

        if (0 <= m_VirtQueue.AddBuf(sg, nOut, 1, (PVOID)1, NULL, 0))
        {
            UINT len;
            void *p;
            /* Control messages are processed synchronously in QEMU, so upon kick_buf return, the response message
              has been already inserted in the queue */
            m_VirtQueue.KickAlways();
            p = m_VirtQueue.GetBuf(&len);
            if (!p)
            {
                DPrintf(0, ("%s - ERROR: get_buf failed\n", __FUNCTION__));
            }
            else if (len != sizeof(virtio_net_ctrl_ack))
            {
                DPrintf(0, ("%s - ERROR: wrong len %d\n", __FUNCTION__, len));
            }
            else if (*(virtio_net_ctrl_ack *)(pBase + offset) != VIRTIO_NET_OK)
            {
                DPrintf(0, ("%s - ERROR: error %d returned for class %d\n", __FUNCTION__, *(virtio_net_ctrl_ack *)(pBase + offset), cls));
            }
            else
            {
                // everything is OK
                DPrintf(levelIfOK, ("%s OK(%d.%d,buffers of %d and %d) \n", __FUNCTION__, cls, cmd, size1, size2));
                bOK = TRUE;
            }
        }
        else
        {
            DPrintf(0, ("%s - ERROR: add_buf failed\n", __FUNCTION__));
        }
    }
    else
    {
        DPrintf(0, ("%s (buffer %d,%d) - ERROR: message too LARGE\n", __FUNCTION__, size1, size2));
    }
    return bOK;
}

NDIS_STATUS CParaNdisCX::SetupMessageIndex(u16 queueCardinal)
{
    WriteVirtIODeviceWord(m_Context->IODevice->addr + VIRTIO_MSI_CONFIG_VECTOR, (u16)queueCardinal);

    return CParaNdisAbstractPath::SetupMessageIndex(queueCardinal);
}
