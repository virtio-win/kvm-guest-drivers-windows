#include "ndis56common.h"
#include "ParaNdis-AbstractPath.h"
#include "kdebugprint.h"

NDIS_STATUS CParaNdisAbstractPath::SetupMessageIndex(u16 queueCardinal)
{
    u16 val;

    WriteVirtIODeviceWord(m_Context->IODevice->addr + VIRTIO_PCI_QUEUE_SEL, (u16)queueCardinal);
    WriteVirtIODeviceWord(m_Context->IODevice->addr + VIRTIO_MSI_QUEUE_VECTOR, (u16)queueCardinal);
    val = ReadVirtIODeviceWord(m_Context->IODevice->addr + VIRTIO_MSI_QUEUE_VECTOR);
    if (val != queueCardinal)
    {
        DPrintf(0, ("[%s] - read/write mismatch, %u vs %u\n", val, queueCardinal));
        return NDIS_STATUS_DEVICE_FAILED;
    }

    m_messageIndex = queueCardinal;
    return NDIS_STATUS_SUCCESS;
}

ULONG CParaNdisAbstractPath::getCPUIndex()
{
#if NDIS_SUPPORT_NDIS620
    PROCESSOR_NUMBER procNumber = { 0 };

    procNumber.Group = DPCAffinity.Group;
    ULONG number = ParaNdis_GetIndexFromAffinity(DPCAffinity.Mask);
    if (number == INVALID_PROCESSOR_INDEX)
    {
        DPrintf(0, ("[%s] : bad in-group processor index: mask 0x%lx\n", __FUNCTION__, (ULONG)DPCAffinity.Mask));
        ASSERT(FALSE);
        return INVALID_PROCESSOR_INDEX;
    }

    procNumber.Number = (UCHAR)number;
    procNumber.Reserved = 0;

    ULONG procIndex = KeGetProcessorIndexFromNumber(&procNumber);
    ASSERTMSG("Bad processor Index", procIndex != INVALID_PROCESSOR_INDEX);
    return procIndex;
#else
    return ParaNdis_GetIndexFromAffinity(DPCTargetProcessor);
#endif
}
