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

  protected:
    tCompletePhysicalAddress m_ControlData;
    KDPC m_DPC;
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
    ULONG FillSGArray(struct VirtIOBufferDescriptor sg[/*4*/], CommandData &data, UINT &nOut);
};
