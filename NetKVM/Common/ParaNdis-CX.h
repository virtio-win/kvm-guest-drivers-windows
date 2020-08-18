#pragma once
#include "ParaNdis-VirtQueue.h"
#include "ndis56common.h"
#include "ParaNdis-AbstractPath.h"

class CParaNdisCX : public CParaNdisTemplatePath<CVirtQueue>, public CPlacementAllocatable {
public:
    CParaNdisCX(PPARANDIS_ADAPTER Context);
    ~CParaNdisCX();

    bool Create(UINT DeviceQueueIndex);

    virtual NDIS_STATUS SetupMessageIndex(u16 vector);

    BOOLEAN CParaNdisCX::SendControlMessage(
        UCHAR cls,
        UCHAR cmd,
        PVOID buffer1,
        ULONG size1,
        PVOID buffer2,
        ULONG size2,
        int levelIfOK
        );

    bool FireDPC(ULONG messageId) override;
    KDPC m_DPC;

protected:
    tCompletePhysicalAddress m_ControlData;
};
