#include "ParaNdis-VirtQueue.h"
#include "ndis56common.h"

class CParaNdisCX : public CNdisAllocatable < CParaNdisCX, 'CXHR' > {
public:
    CParaNdisCX();
    ~CParaNdisCX();

    bool Create(PPARANDIS_ADAPTER Context, UINT DeviceQueueIndex);

    void Shutdown() {
        CLockedContext<CNdisSpinLock> autoLock(m_Lock);
        virtqueue_shutdown(m_VirtQueue);
    }

    void Renew() {
        VirtIODeviceRenewQueue(m_VirtQueue);
    }

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
    PPARANDIS_ADAPTER m_Context;
    CNdisSpinLock m_Lock;

    struct virtqueue *       m_VirtQueue;
    tCompletePhysicalAddress m_VirtQueueRing;
    tCompletePhysicalAddress m_ControlData;
};

