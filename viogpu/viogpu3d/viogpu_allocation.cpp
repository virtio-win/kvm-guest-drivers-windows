#include "baseobj.h"
#include "bitops.h"
#include "viogpum.h"
#include "viogpu_allocation.h"
#include "viogpu_adapter.h"
#include "virgl_hw.h"

VioGpuAllocation::VioGpuAllocation(VioGpuAdapter *adapter, VIOGPU_RESOURCE_OPTIONS* options)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    m_adapter = adapter;
    m_Id = m_adapter->resourceIdr.GetId();
    memcpy(&m_options, options, sizeof(VIOGPU_RESOURCE_OPTIONS));

    //m_adapter->ctrlQueue.CreateResource(m_Id, m_options.format, m_options.width, m_options.height);
    m_adapter->ctrlQueue.CreateResource3D(m_Id, options);

    m_pMDL = NULL;
    m_pageCount = 0;
    m_pageOffset = 0;
    m_DxPhysicalAddress = 0;

    KeInitializeEvent(&m_busyNotification, NotificationEvent, TRUE);
    m_busy = 0;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s res_id=%d\n", __FUNCTION__, m_Id));
}

VioGpuAllocation::~VioGpuAllocation(void)
{
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s res_id=%d\n", __FUNCTION__, m_Id));
    m_adapter->ctrlQueue.DestroyResource(m_Id);
    m_adapter->resourceIdr.PutId(m_Id);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void VioGpuAllocation::AttachBacking(MDL* pMDL, size_t pageCount, size_t pageOffset) {
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s res_id=%d\n", __FUNCTION__, m_Id));

    m_pMDL = pMDL;
    m_pageCount = pageCount;
    m_pageOffset = pageOffset;


    GPU_MEM_ENTRY* ents = new(NonPagedPoolNx) GPU_MEM_ENTRY[pageCount];

    for (UINT i = 0; i < pageCount; i++)
    {
        ents[i].addr = MmGetMdlPfnArray(pMDL)[pageOffset + i] * PAGE_SIZE;
        ents[i].length = PAGE_SIZE;
        ents[i].padding = 0;
    }

    m_adapter->ctrlQueue.AttachBacking(m_Id, ents, (UINT)pageCount);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void VioGpuAllocation::DetachBacking() {
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s res_id=%d\n", __FUNCTION__, m_Id));

    m_pMDL = NULL;
    m_pageCount = 0;
    m_pageOffset = 0;

    m_adapter->ctrlQueue.DetachBacking(m_Id);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}


PAGED_CODE_SEG_BEGIN

void VioGpuAllocation::MarkBusy() {
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s res_id=%d\n", __FUNCTION__, m_Id));

    InterlockedIncrement(&m_busy);
    KeClearEvent(&m_busyNotification);
}

void VioGpuAllocation::UnmarkBusy() {
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s res_id=%d\n", __FUNCTION__, m_Id));

    if (InterlockedDecrement(&m_busy) == 0) {
        KeSetEvent(&m_busyNotification, IO_NO_INCREMENT, FALSE);
    }
}


D3DDDIFORMAT VioGpuToD3DDDIColorFormat(virtio_gpu_formats format)
{
    PAGED_CODE();

    switch (format)
    {
    case VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM:
        return D3DDDIFMT_A8R8G8B8;
    case VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM:
        return D3DDDIFMT_X8R8G8B8;
    case VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM:
        return D3DDDIFMT_A8B8G8R8;
    case VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM:
        return D3DDDIFMT_X8B8G8R8;
    }
    DbgPrint(TRACE_LEVEL_ERROR, ("---> %s Unsupported color format %d\n", __FUNCTION__, format));
    return D3DDDIFMT_X8B8G8R8;
}

void VioGpuAllocation::FlushToScreen(UINT scan_id) {
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s res_id=%d\n", __FUNCTION__, m_Id));

    GPU_BOX box;
    box.x = 0; 
    box.y = 0;  
    box.z = 0; 
    box.width = m_options.width; 
    box.height = m_options.height; 
    box.depth = 1;
        
    m_adapter->ctrlQueue.SetScanout(scan_id, m_Id, m_options.width, m_options.height, 0, 0);
    m_adapter->ctrlQueue.ResFlush(m_Id, m_options.width, m_options.height, 0, 0);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s res_id=%d\n", __FUNCTION__, m_Id));
}

NTSTATUS VioGpuAllocation::GetStandardAllocationDriverData(DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA* pStandardAllocation)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_ERROR, ("---> %s type=%d\n", __FUNCTION__, pStandardAllocation->StandardAllocationType));

    if (!pStandardAllocation->pResourcePrivateDriverData && !pStandardAllocation->pResourcePrivateDriverData)
    {
        pStandardAllocation->ResourcePrivateDriverDataSize = sizeof(VIOGPU_CREATE_RESOURCE_EXCHANGE);
        pStandardAllocation->AllocationPrivateDriverDataSize = sizeof(VIOGPU_CREATE_ALLOCATION_EXCHANGE);
        return STATUS_SUCCESS;
    }

    VIOGPU_CREATE_ALLOCATION_EXCHANGE* allocationExchange = (VIOGPU_CREATE_ALLOCATION_EXCHANGE*)pStandardAllocation->pAllocationPrivateDriverData;

    allocationExchange->ResourceOptions.target = 2;
    allocationExchange->ResourceOptions.format = VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM;
    allocationExchange->ResourceOptions.bind = VIRGL_BIND_RENDER_TARGET | VIRGL_BIND_SAMPLER_VIEW | VIRGL_BIND_DISPLAY_TARGET | VIRGL_BIND_SCANOUT;

    allocationExchange->ResourceOptions.width = 1024;
    allocationExchange->ResourceOptions.height = 768;
    allocationExchange->ResourceOptions.depth = 1;

    allocationExchange->ResourceOptions.array_size = 1;
    allocationExchange->ResourceOptions.last_level = 0;
    allocationExchange->ResourceOptions.nr_samples = 0;
    allocationExchange->ResourceOptions.flags = 0;


    switch (pStandardAllocation->StandardAllocationType) {
    case D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE: {
        D3DKMDT_SHAREDPRIMARYSURFACEDATA* surfaceData = pStandardAllocation->pCreateSharedPrimarySurfaceData;
        //[in] UINT                           Width;
        //[in] UINT                           Height;
        //[in] D3DDDIFORMAT                   Format;


        allocationExchange->ResourceOptions.width = surfaceData->Width;
        allocationExchange->ResourceOptions.height = surfaceData->Height;
        allocationExchange->ResourceOptions.format = ColorFormat(surfaceData->Format);
        allocationExchange->Size = (ULONGLONG)surfaceData->Width * (ULONGLONG)surfaceData->Height * 4;
        
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s shared primary surface: width=%d, height=%d, format=%d\n", __FUNCTION__, surfaceData->Width, surfaceData->Height, surfaceData->Format));
        return STATUS_SUCCESS;
    }

    case D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE: {
        D3DKMDT_SHADOWSURFACEDATA* surfaceData = pStandardAllocation->pCreateShadowSurfaceData;
        //[in] UINT                           Width;
        //[in] UINT                           Height;
        //[in] D3DDDIFORMAT                   Format;


        allocationExchange->ResourceOptions.width = surfaceData->Width;
        allocationExchange->ResourceOptions.height = surfaceData->Height;
        allocationExchange->ResourceOptions.format = ColorFormat(surfaceData->Format);
        allocationExchange->Size = (ULONGLONG)surfaceData->Width * (ULONGLONG)surfaceData->Height * 4;

        allocationExchange->ResourceOptions.flags |= VIRGL_RESOURCE_FLAG_MAP_COHERENT;

        surfaceData->Pitch = surfaceData->Width * 4;
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s shadow surface: width=%d, height=%d, format=%d\n", __FUNCTION__, surfaceData->Width, surfaceData->Height, surfaceData->Format));
        return STATUS_SUCCESS;
    }


    case D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE: {
        D3DKMDT_STAGINGSURFACEDATA* surfaceData = pStandardAllocation->pCreateStagingSurfaceData;
        //[in] UINT                           Width;
        //[in] UINT                           Height;
        //[in] D3DDDIFORMAT                   Format;


        allocationExchange->ResourceOptions.width = surfaceData->Width;
        allocationExchange->ResourceOptions.height = surfaceData->Height;
        allocationExchange->ResourceOptions.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
        allocationExchange->Size = (ULONGLONG)surfaceData->Width * (ULONGLONG)surfaceData->Height * 4;

        allocationExchange->ResourceOptions.flags |= VIRGL_RESOURCE_FLAG_MAP_COHERENT;

        surfaceData->Pitch = surfaceData->Width * 4;
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s staging surface\n", __FUNCTION__));
        return STATUS_SUCCESS;
    }

    default: {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- Unknown standard allocation type \n"));
        return STATUS_NOT_SUPPORTED;
    }
    }
}


NTSTATUS VioGpuAllocation::DxgkCreateAllocation(VioGpuAdapter* adapter, DXGKARG_CREATEALLOCATION* pCreateAllocation)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    DXGK_ALLOCATIONINFO* allocationInfo = pCreateAllocation->pAllocationInfo;

    if (max(allocationInfo->PrivateDriverDataSize, pCreateAllocation->PrivateDriverDataSize) < sizeof(VIOGPU_CREATE_ALLOCATION_EXCHANGE)) {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s private driver data is too small\n", __FUNCTION__));
        return STATUS_INVALID_PARAMETER;
    }

    VIOGPU_CREATE_ALLOCATION_EXCHANGE* resourceExchange = (VIOGPU_CREATE_ALLOCATION_EXCHANGE*)allocationInfo->pPrivateDriverData;;
    if (pCreateAllocation->PrivateDriverDataSize > allocationInfo->PrivateDriverDataSize) {
        resourceExchange = (VIOGPU_CREATE_ALLOCATION_EXCHANGE*)pCreateAllocation->pPrivateDriverData;
    }


    VioGpuAllocation* allocation = new(NonPagedPoolNx) VioGpuAllocation(adapter, &resourceExchange->ResourceOptions);
    allocationInfo->hAllocation = allocation;

    if (pCreateAllocation->Flags.Resource) {
        VioGpuResource* resource = new(NonPagedPoolNx) VioGpuResource();
        pCreateAllocation->hResource = resource;
    }


    allocationInfo->Alignment = 0;
    allocationInfo->Size = (SIZE_T)resourceExchange->Size;
    allocationInfo->PitchAlignedSize = 0;
    allocationInfo->HintedBank.Value = 0;
    allocationInfo->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
    allocationInfo->EvictionSegmentSet = 1; // don't use apperture for eviction
    allocationInfo->Flags.Value = 0;

    allocationInfo->PreferredSegment.Value = 0;
    allocationInfo->PreferredSegment.SegmentId0 = 1;
    allocationInfo->PreferredSegment.Direction0 = 0;


    allocationInfo->Flags.CpuVisible = TRUE;


    allocationInfo->HintedBank.Value = 0;
    allocationInfo->MaximumRenamingListLength = 0;
    allocationInfo->pAllocationUsageHint = NULL;
    allocationInfo->PhysicalAdapterIndex = 0;
    allocationInfo->PitchAlignedSize = 0;

    allocationInfo->SupportedReadSegmentSet = 0b1;
    allocationInfo->SupportedWriteSegmentSet = 0b1;

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s res_id=%d size=%d\n", __FUNCTION__, allocation->GetId(), allocationInfo->Size));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAllocation::DescribeAllocation(DXGKARG_DESCRIBEALLOCATION* pDescribeAllocation) {
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s res_id=%d\n", __FUNCTION__, m_Id));

    pDescribeAllocation->Width = m_options.width;
    pDescribeAllocation->Height = m_options.height;
    pDescribeAllocation->PrivateDriverFormatAttribute = 0;

    pDescribeAllocation->Format = VioGpuToD3DDDIColorFormat((virtio_gpu_formats)m_options.format);

    // this values are RANDOM
    pDescribeAllocation->MultisampleMethod.NumQualityLevels = 2;
    pDescribeAllocation->MultisampleMethod.NumSamples = 2;

    pDescribeAllocation->RefreshRate.Numerator = 148500000;
    pDescribeAllocation->RefreshRate.Denominator = 2475000;

    return STATUS_SUCCESS;
};

NTSTATUS VioGpuAllocation::MapApertureSegment(DXGKARG_BUILDPAGINGBUFFER* pBuildPagingBuffer) {
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s res_id=%d\n", __FUNCTION__, m_Id));

    size_t pageCount = pBuildPagingBuffer->MapApertureSegment.NumberOfPages;
    size_t mdlPageOffset = pBuildPagingBuffer->MapApertureSegment.MdlOffset;

    MDL* pMdl = pBuildPagingBuffer->MapApertureSegment.pMdl;

    AttachBacking(pMdl, pageCount, mdlPageOffset);
    SetDxPhysicalAddress(pBuildPagingBuffer->MapApertureSegment.OffsetInPages * PAGE_SIZE);

    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAllocation::UnmapApertureSegment(DXGKARG_BUILDPAGINGBUFFER* pBuildPagingBuffer) {
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s res_id=%d\n", __FUNCTION__, m_Id));

    UNREFERENCED_PARAMETER(pBuildPagingBuffer);
    DetachBacking();
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAllocation::EscapeResourceInfo(VIOGPU_RES_INFO_REQ* resInfo) {
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s res_id=%d\n", __FUNCTION__, m_Id));

    resInfo->Id = m_Id;
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAllocation::EscapeResourceBusy(VIOGPU_RES_BUSY_REQ* resBusy) {
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s res_id=%d\n", __FUNCTION__, m_Id));

    while (resBusy->Wait && m_busy != 0) {
        KeWaitForSingleObject(&m_busyNotification, UserRequest, KernelMode, FALSE, NULL);

        LARGE_INTEGER wait;
        wait.QuadPart = -10LL;
        KeDelayExecutionThread(KernelMode, FALSE, &wait);
    }

    resBusy->IsBusy = m_busy != 0;

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG_END