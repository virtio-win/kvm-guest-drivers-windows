#pragma once
#include "helper.h"
#include "viogpu_allocation.h"

class VioGpuAdapter;

// Class that represents DXGKRNL Context, often passed as hContext
class VioGpuDevice
{
public:
    VioGpuDevice(VioGpuAdapter* pAdapter);
    ~VioGpuDevice();


    NTSTATUS Init(VIOGPU_CTX_INIT_REQ* pOptions);
    NTSTATUS OpenAllocation(_In_ CONST DXGKARG_OPENALLOCATION* pOpenAllocation);
    
    NTSTATUS GenerateBltPresent(DXGKARG_PRESENT* pPresent, VioGpuAllocation* src, VioGpuAllocation* dst);
    NTSTATUS Present(_Inout_ DXGKARG_PRESENT* pPresent);
    NTSTATUS Render(DXGKARG_RENDER* pRender);

    ULONG GetId() {
        return m_id;
    }

    CtrlQueue* GetCtrlQueue();

private:
	VioGpuAdapter* m_pAdapter;
    ULONG m_id;
};

class VioGpuDeviceAllocation 
{
public:
    VioGpuDeviceAllocation(VioGpuDevice *device, VioGpuAllocation* allocation);
    ~VioGpuDeviceAllocation();

    VioGpuAllocation* GetAllocation();

private:
    VioGpuAllocation* m_pAllocation;
    VioGpuDevice* m_pDevice;
};