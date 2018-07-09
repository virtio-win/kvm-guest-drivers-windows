#include "ndis56common.h"
#include "virtio_net.h"
#include "kdebugprint.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis-CX.tmh"
#endif

#define ALIGN_OFFSET(OFFSET) (OFFSET = ((OFFSET + 3) & ~3))

CParaNdisCX::CParaNdisCX()
{
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

    if (!ParaNdis_InitialAllocatePhysicalMemory(m_Context, 512, &m_ControlData))
    {
        DPrintf(0, "CParaNdisCX::Create - ParaNdis_InitialAllocatePhysicalMemory failed for %u\n",
            DeviceQueueIndex);
        m_ControlData.Virtual = nullptr;
        return false;
    }

    m_Context->m_CxStateMachine.Start();

    CreatePath();
    InitDPC();

    return m_VirtQueue.Create(DeviceQueueIndex,
        &m_Context->IODevice,
        m_Context->MiniportHandle);
}


void CParaNdisCX::InitDPC()
{
    KeInitializeDpc(&m_DPC, MiniportMSIInterruptCXDpc, m_Context);
}

VOID CParaNdisCX::InsertBuffersToList(BufferList &list)
{
    UNREFERENCED_PARAMETER(list);
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
    BufferList outList;
    BufferList inList;
    CBuffer buff1, buff2;

    buff1.buffer = buffer1;
    buff1.size = size1;
    buff2.buffer = buffer2;
    buff2.size = size2;

    InsertBuffersToList(outList, &buff1, &buff2);

    return SendControlMessage(cls, cmd, outList, inList, levelIfOK);
}

ULONG SumBuffersSize(BufferList &list)
{
    ULONG size = 0;
    list.ForEach([&size](CBuffer *buffer) { size += buffer->size; });
    return size;
}

UINT CParaNdisCX::GetBuffer(PVOID inBuff)
{
    UINT len;
    int tries = 1000;
    do
    {
        inBuff = m_VirtQueue.GetBuf(&len);
        UINT interval = 1;
        NdisStallExecution(interval);
        tries--;
    } while (inBuff == nullptr && tries);

    return len;
}

/* According to section "5.1.6.5 Control Virtqueue" of the virtio spec, the
 * send convention to the CX path is:
 *
 * struct virtio_net_ctrl {
 *         u8 class;
 *         u8 command;
 *         u8 command-specific-data[];
 *         u8 ack;
 * };
 *
 * How it is implemented:
 *
 * * Send a scatter gather to the device which includes:
 *  ** virtio_net_ctrl_hdr as an output (class and command fields)
 *  ** 0 or more output buffers
 *  ** 0 or more input buffers
 *  ** virtio_net_ctrl_ack as an input buffer
 */

BOOLEAN CParaNdisCX::SendControlMessage(
    UCHAR cls,
    UCHAR cmd,
    BufferList &outList,
    BufferList &inList,
    int levelIfOK
)
{
    BOOLEAN bOK = FALSE;
    CLockedContext<CNdisSpinLock> autoLock(m_Lock);
    ULONG totalSize = SumBuffersSize(inList) + SumBuffersSize(outList) + sizeof(virtio_net_ctrl_hdr) + sizeof(virtio_net_ctrl_ack);

    if (m_ControlData.Virtual && m_ControlData.size > (totalSize) || inList.GetCount() + outList.GetCount() + 2 > MAX_SCATTER_GATHER_BUFFERS)
    {
        struct VirtIOBufferDescriptor sg[MAX_SCATTER_GATHER_BUFFERS];
        PUCHAR pBase = (PUCHAR)m_ControlData.Virtual;
        PHYSICAL_ADDRESS phBase = m_ControlData.Physical;
        ULONG inBaseOffset = 0, offset = 0;
        UINT nOut = 1;
        UINT nIn = 0;

        ((virtio_net_ctrl_hdr *)pBase)->class_of_command = cls;
        ((virtio_net_ctrl_hdr *)pBase)->cmd = cmd;
        sg[0].physAddr = phBase;
        sg[0].length = sizeof(virtio_net_ctrl_hdr);
        offset += sg[0].length;
        ALIGN_OFFSET(offset);

        outList.ForEach([&](CBuffer *buffer)
        {
            NdisMoveMemory(pBase + offset, buffer->buffer, buffer->size);
            sg[nOut].physAddr = phBase;
            sg[nOut].physAddr.QuadPart += offset;
            sg[nOut].length = buffer->size;
            offset += buffer->size;
            ALIGN_OFFSET(offset);
            nOut++;
        });

        inBaseOffset = offset;

        inList.ForEach([&](CBuffer *buffer)
        {
            NdisMoveMemory(pBase + offset, buffer->buffer, buffer->size);
            sg[nOut + nIn].physAddr = phBase;
            sg[nOut + nIn].physAddr.QuadPart += offset;
            sg[nOut + nIn].length = buffer->size;
            offset += buffer->size;
            ALIGN_OFFSET(offset);
            nIn++;
        });

        sg[nOut + nIn].physAddr = phBase;
        sg[nOut + nIn].physAddr.QuadPart += offset;
        sg[nOut + nIn].length = sizeof(virtio_net_ctrl_ack);
        nIn++;
        *(virtio_net_ctrl_ack *)(pBase + offset) = VIRTIO_NET_ERR;

        if (0 <= m_VirtQueue.AddBuf(sg, nOut, nIn, (PVOID)1, NULL, 0))
        {
            UINT len, i = nOut;
            PVOID inBuff = nullptr;

            m_Context->m_CxStateMachine.RegisterOutstandingItem();

            m_VirtQueue.Kick();
            len = GetBuffer(inBuff);

            m_Context->m_CxStateMachine.UnregisterOutstandingItem();

            if (!inBuff)
            {
                DPrintf(0, "%s - ERROR: get_buf failed\n", __FUNCTION__);
            }
            /* Receive the input buffers, they are packed as one virtqueue element */
            else if (len != SumBuffersSize(inList) + sizeof(virtio_net_ctrl_ack))
            {
                DPrintf(0, "%s - ERROR: wrong len %d\n", __FUNCTION__, len);
                m_Context->m_CxStateMachine.UnregisterOutstandingItem();
                return bOK;
            }

            /* Iterate over the input buffers and return them to the user */
            offset = inBaseOffset;
            inList.ForEach([&](CBuffer *buffer)
            {
                inBuff = pBase + offset;
                NdisMoveMemory(buffer->buffer, inBuff, sg[nOut + i].length);
                offset += sg[nOut + i].length;
                ALIGN_OFFSET(offset);
                i++;
            });

            ASSERT(i < nIn + nOut);

            /* Now all is left to check the virtio_net_ctrl_ack buffer, the
            * virtio_net_ctrl_ack should be the last element that we should
            * receive from the device in the queue
            */

            i++;
            if ((i == nOut + nIn) && *(virtio_net_ctrl_ack *)(pBase + offset) != VIRTIO_NET_OK)
            {
                DPrintf(0, "%s - ERROR: error %d returned for class %d\n", __FUNCTION__, *(virtio_net_ctrl_ack *)(pBase + offset), cls);
            }
            else if (i == nOut + nIn)
            {
                // everything is OK
                DPrintf(levelIfOK, "%s OK(%d.%d) \n", __FUNCTION__, cls, cmd);
                bOK = TRUE;
            } else
            {
                DPrintf(0, "%s - ERROR: Some output buffers didn't get returned for class %d\n", __FUNCTION__, cls);
            }

        }
        else
        {
            DPrintf(0, "%s - ERROR: add_buf failed\n", __FUNCTION__);
        }
    }
    else
    {
        DPrintf(0, "%s - ERROR: message too LARGE\n", __FUNCTION__);
    }
    return bOK;
}

NDIS_STATUS CParaNdisCX::SetupMessageIndex(u16 vector)
{
    DPrintf(0, "[%s] Using message %u for controls\n", __FUNCTION__, vector);

    virtio_set_config_vector(&m_Context->IODevice, vector);

    return CParaNdisAbstractPath::SetupMessageIndex(vector);
}
