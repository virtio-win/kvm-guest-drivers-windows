#pragma once

#include "helper.h"

class VioGpuAdapter;
class VioGpuAllocation;

typedef struct _CURRENT_MODE
{
    DXGK_DISPLAY_INFORMATION             DispInfo;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION  Rotation;
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling;
    UINT SrcModeWidth;
    UINT SrcModeHeight;
    struct _CURRENT_MODE_FLAGS
    {
        UINT SourceNotVisible : 1;
        UINT FullscreenPresent : 1;
        UINT FrameBufferIsActive : 1;
        UINT DoNotMapOrUnmap : 1;
        UINT IsInternal : 1;
        UINT Unused : 27;
    } Flags;

    PHYSICAL_ADDRESS ZeroedOutStart;
    PHYSICAL_ADDRESS ZeroedOutEnd;

    union
    {
        VOID* Ptr;
        ULONG64                          Force8Bytes;
    } FrameBuffer;
} CURRENT_MODE;

class VioGpuVidPN
{
public:
    VioGpuVidPN(VioGpuAdapter *adapter);
    ~VioGpuVidPN();

    NTSTATUS Start(ULONG* pNumberOfViews, ULONG* pNumberOfChildren); 
    NTSTATUS AcquirePostDisplayOwnership();
    void ReleasePostDisplayOwnership(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, DXGK_DISPLAY_INFORMATION* pDisplayInfo);
    void Powerdown();

    NTSTATUS IsVidPnSourceModeFieldsValid(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode) const;
    NTSTATUS IsVidPnPathFieldsValid(CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath) const;

    NTSTATUS CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn);
    NTSTATUS UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath);

    NTSTATUS SetCurrentMode(ULONG Mode, CURRENT_MODE* pCurrentMode);
    ULONG GetModeCount(void) { return m_ModeCount; }
    VOID BlackOutScreen(CURRENT_MODE* pCurrentMod);

    NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo);

    void CreateFrameBufferObj(PVIDEO_MODE_INFORMATION pModeInfo, CURRENT_MODE* pCurrentMode);
    void DestroyFrameBufferObj(BOOLEAN bReset);

    BOOLEAN GpuObjectAttach(UINT res_id, VioGpuObj* obj);
    PBYTE GetEdidData(UINT Idx);

    void SetVideoModeInfo(UINT Idx, PVIOGPU_DISP_MODE pModeInfo);
    BOOLEAN GetDisplayInfo(void);
    void ProcessEdid(void);
    void FixEdid(void);
    BOOLEAN GetEdids(void);
    void AddEdidModes(void);
    void SetCustomDisplay(_In_ USHORT xres,
        _In_ USHORT yres);

    NTSTATUS EscapeCustomResoulution(VIOGPU_DISP_MODE* resolution);

    NTSTATUS IsSupportedVidPn(_Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn);
    NTSTATUS RecommendFunctionalVidPn(_In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn);
    NTSTATUS RecommendVidPnTopology(_In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST pRecommendVidPnTopology);
    NTSTATUS RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes);
    NTSTATUS EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality);
    NTSTATUS SetVidPnSourceVisibility(_In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility);
    NTSTATUS QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps);

    NTSTATUS SystemDisplayEnable(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID       TargetId,
        _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
        _Out_ UINT* pWidth,
        _Out_ UINT* pHeight,
        _Out_ D3DDDIFORMAT* pColorFormat);
    VOID SystemDisplayWrite(_In_reads_bytes_(SourceHeight* SourceStride) VOID* pSource,
        _In_  UINT                                 SourceWidth,
        _In_  UINT                                 SourceHeight,
        _In_  UINT                                 SourceStride,
        _In_  INT                                  PositionX,
        _In_  INT                                  PositionY);

    void Flip();
    static void FlipThread(void *ctx);

    NTSTATUS SetVidPnSourceAddress(const DXGKARG_SETVIDPNSOURCEADDRESS* pSetVidPnSourceAddress);

private:
    NTSTATUS SetSourceModeAndPath(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode,
        CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath);
    NTSTATUS AddSingleMonitorMode(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes);
    NTSTATUS AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface,
        D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId);
    NTSTATUS AddSingleTargetMode(_In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pVidPnTargetModeSetInterface,
        D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
        _In_opt_ CONST D3DKMDT_VIDPN_SOURCE_MODE* pVidPnPinnedSourceModeInfo,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId);
    D3DDDI_VIDEO_PRESENT_SOURCE_ID FindSourceForTarget(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, BOOLEAN DefaultToZero);
    VOID BuildVideoSignalInfo(D3DKMDT_VIDEO_SIGNAL_INFO* pVideoSignalInfo, PVIDEO_MODE_INFORMATION pModeInfo);


    VioGpuAdapter *m_pAdapter;
    DXGKRNL_INTERFACE* m_pDxgkInterface;

    CURRENT_MODE m_CurrentModes[MAX_VIEWS];

    PVIDEO_MODE_INFORMATION m_ModeInfo;
    ULONG m_ModeCount;
    PUSHORT m_ModeNumbers;
    USHORT m_CurrentMode;
    USHORT m_CustomMode;
    BYTE m_EDIDs[MAX_CHILDREN][EDID_V1_BLOCK_SIZE];
    BOOLEAN m_bEDID;

    DXGK_DISPLAY_INFORMATION m_SystemDisplayInfo;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID m_SystemDisplaySourceId;

    VioGpuObj* m_pFrameBuf;

    PHYSICAL_ADDRESS m_sourceAddress = { 0 };
    VioGpuAllocation* m_sourceRes = NULL;
    volatile LONG m_shouldFlip = 0;

    PETHREAD m_pFlipThread;
    BOOL m_shouldFlipStop = false;
};

