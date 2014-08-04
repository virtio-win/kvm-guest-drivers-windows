#include "ParaNdis-VirtQueue.h"
#include "ParaNdis-AbstractPath.h"
#include "ndis56common.h"

class CParaNdisCX : public CParaNdisAbstractPath<CVirtQueue>, public CNdisAllocatable < CParaNdisCX, 'CXHR' > {
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

