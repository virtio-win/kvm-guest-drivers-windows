#pragma once
#include "helper.h"
#include "virgl_hw.h"

class VioGpuAdapter;

class VioGpuResource
{
public:
private:
};

class VioGpuAllocation {
public:
    VioGpuAllocation(VioGpuAdapter* adapter, VIOGPU_RESOURCE_OPTIONS* options);
    ~VioGpuAllocation(void);
    

    UINT GetId(void) { return m_Id; }

    void MarkBusy();
    void UnmarkBusy();

    void SetDxPhysicalAddress(size_t DxPhysicalAddress) {
        m_DxPhysicalAddress = DxPhysicalAddress;
    };

    size_t GetDxPhysicalAddress() {
        return m_DxPhysicalAddress;
    };

    BOOL IsCoherent() {
        return (m_options.flags & VIRGL_RESOURCE_FLAG_MAP_COHERENT) != 0;
    }

    void AttachBacking(MDL* pMdl, size_t pageCount, size_t pageOffset);
    void DetachBacking();

    void FlushToScreen(UINT scan_id);

    static NTSTATUS GetStandardAllocationDriverData(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA* pStandardAllocation);
    static NTSTATUS DxgkCreateAllocation(VioGpuAdapter* adapter, DXGKARG_CREATEALLOCATION* pCreateAllocation);

    NTSTATUS DescribeAllocation(DXGKARG_DESCRIBEALLOCATION* pDescribeAllocation);
    NTSTATUS MapApertureSegment(DXGKARG_BUILDPAGINGBUFFER* pBuildPagingBuffer);
    NTSTATUS UnmapApertureSegment(DXGKARG_BUILDPAGINGBUFFER* pBuildPagingBuffer);

    NTSTATUS EscapeResourceInfo(VIOGPU_RES_INFO_REQ *resInfo);
    NTSTATUS EscapeResourceBusy(VIOGPU_RES_BUSY_REQ* resBusy);

private:
    VioGpuAdapter* m_adapter;

    VIOGPU_RESOURCE_OPTIONS m_options;
    UINT m_Id;

    MDL* m_pMDL;
    size_t m_pageCount;
    size_t m_pageOffset;

    size_t m_DxPhysicalAddress;

    KEVENT m_busyNotification;
    volatile LONG m_busy;
};