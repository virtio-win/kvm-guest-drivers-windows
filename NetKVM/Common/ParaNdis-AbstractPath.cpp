#include "ndis56common.h"
#include "ParaNdis-AbstractPath.h"

NDIS_STATUS CParaNdisAbstractPath::SetupMessageIndex(u16 queueCardinal)
{
    u16 val;

    WriteVirtIODeviceWord(m_Context->IODevice.addr + VIRTIO_PCI_QUEUE_SEL, (u16)queueCardinal);
    WriteVirtIODeviceWord(m_Context->IODevice.addr + VIRTIO_MSI_QUEUE_VECTOR, (u16)queueCardinal);
    val = ReadVirtIODeviceWord(m_Context->IODevice.addr + VIRTIO_MSI_QUEUE_VECTOR);
    if (val != queueCardinal)
    {
        DPrintf(0, ("[%s] - read/write mismatch, %u vs %u\n", val, queueCardinal));
        return NDIS_STATUS_DEVICE_FAILED;
    }

    m_messageIndex = queueCardinal;
    return NDIS_STATUS_SUCCESS;
}
