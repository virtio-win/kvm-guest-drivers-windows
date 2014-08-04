#include "ParaNdis-VirtQueue.h"
#include "ndis56common.h"
#include "ParaNdis-AbstractPath.h"

class CParaNdisCX : public CParaNdisTemplatePath<CVirtQueue>, public CNdisAllocatable < CParaNdisCX, 'CXHR' > {
public:
    CParaNdisCX();
    ~CParaNdisCX();

    bool Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex);

    BOOLEAN CParaNdisCX::SendControlMessage(
        UCHAR cls,
        UCHAR cmd,
        PVOID buffer1,
        ULONG size1,
        PVOID buffer2,
        ULONG size2,
        int levelIfOK
        );

protected:
    tCompletePhysicalAddress m_ControlData;
};

