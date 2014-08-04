#include "ndis56common.h"

CParaNdisCX::CParaNdisCX()
{
    m_ControlData.size = 512;
}

CParaNdisCX::~CParaNdisCX()
{
    ParaNdis_DeleteQueue(m_Context, &m_VirtQueue, &m_VirtQueueRing);
}

bool CParaNdisCX::Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex)
{
    ULONG size;

    m_Context = Context;

    VirtIODeviceQueryQueueAllocation(&m_Context->IODevice, DeviceQueueIndex, &size, &m_VirtQueueRing.size);
    if (!m_VirtQueueRing.size)
    {
        DPrintf(0, ("CParaNdisCX::Create - VirtIODeviceQueryQueueAllocation failed for %u\n", DeviceQueueIndex));
        return false;
    }

    if (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, &m_VirtQueueRing))
    {
        DPrintf(0, ("CParaNdisCX::Create - ParaNdis_InitialAllocatePhysicalMemory failed for %u\n", DeviceQueueIndex));
        return false;
    }

    m_VirtQueue = VirtIODevicePrepareQueue(
        &m_Context->IODevice,
        DeviceQueueIndex,
        m_VirtQueueRing.Physical,
        m_VirtQueueRing.Virtual,
        m_VirtQueueRing.size,
        NULL,
        m_Context->bDoPublishIndices);

    if (m_VirtQueue == nullptr)
    {
        DPrintf(0, ("CParaNdisCX::Create - VirtIODevicePrepareQueue failed for %u\n",
            DeviceQueueIndex));
        return false;
    }

    if (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, &m_ControlData))
    {
        DPrintf(0, ("CParaNdisCX::Create - ParaNdis_InitialAllocatePhysicalMemory failed for %u\n",
            DeviceQueueIndex));
        return false;
    }
    return true;
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

        if (0 <= virtqueue_add_buf(m_VirtQueue, sg, nOut, 1, (PVOID)1, NULL, 0))
        {
            UINT len;
            void *p;
            /* Control messages are processed synchronously in QEMU, so upon kick_buf return, the response message
              has been already inserted in the queue */
            virtqueue_kick(m_VirtQueue);
            p = virtqueue_get_buf(m_VirtQueue, &len);
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
