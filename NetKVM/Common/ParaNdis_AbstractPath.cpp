#include "ndis56common.h"
#include "ParaNdis-AbstractPath.h"
#include "kdebugprint.h"
#include "Trace.h"
#ifdef NETKVM_WPP_ENABLED
#include "ParaNdis_AbstractPath.tmh"
#endif

NDIS_STATUS CParaNdisAbstractPath::SetupMessageIndex(u16 vector)
{
    u16 val = m_pVirtQueue->SetMSIVector(vector);

    if (val != vector)
    {
        DPrintf(0, "[%s] - read/write mismatch, %u vs %u\n",__FUNCTION__ , val, vector);
        return NDIS_STATUS_DEVICE_FAILED;
    }

    m_messageIndex = vector;
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
        DPrintf(0, "[%s] : bad in-group processor index: mask 0x%lx\n", __FUNCTION__, (ULONG)DPCAffinity.Mask);
        NETKVM_ASSERT(FALSE);
        return INVALID_PROCESSOR_INDEX;
    }

    procNumber.Number = (UCHAR)number;
    procNumber.Reserved = 0;

    ULONG procIndex = KeGetProcessorIndexFromNumber(&procNumber);
    NETKVM_ASSERT(procIndex != INVALID_PROCESSOR_INDEX);
    return procIndex;
#else
    return ParaNdis_GetIndexFromAffinity(DPCTargetProcessor);
#endif
}
