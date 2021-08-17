/*
 * Copyright (C) 2019-2020 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma once

#include "helper.h"


#pragma pack(push)
#pragma pack(1)


typedef struct
{
    UINT DriverStarted : 1;
    UINT HardwareInit : 1;
    UINT PointerEnabled : 1;
    UINT VgaDevice : 1;
    UINT FlexResolution : 1;
    UINT UsePhysicalMemory : 1;
    UINT Unused : 26;
} DRIVER_STATUS_FLAG;

#pragma pack(pop)

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
        VOID*                            Ptr;
        ULONG64                          Force8Bytes;
    } FrameBuffer;
} CURRENT_MODE;

class VioGpuDod;

class IVioGpuAdapter {
public:
    IVioGpuAdapter(_In_ VioGpuDod* pVioGpuDod) : m_pVioGpuDod(pVioGpuDod),
        m_ModeInfo(NULL), m_ModeCount(0), m_CurrentMode(0), m_CustomMode(0),
        m_Id(0), m_bEDID(FALSE) { RtlZeroMemory(m_EDIDs, sizeof(m_EDIDs)); }
    virtual ~IVioGpuAdapter(void) { ; }
    virtual NTSTATUS SetCurrentMode(ULONG Mode, CURRENT_MODE* pCurrentBddMode) = 0;
    virtual NTSTATUS SetPowerState(DXGK_DEVICE_INFO* pDeviceInfo, DEVICE_POWER_STATE DevicePowerState, CURRENT_MODE* pCurrentMode) = 0;
    virtual NTSTATUS HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo) = 0;
    virtual NTSTATUS HWClose(void) = 0;
    virtual BOOLEAN InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber) = 0;
    virtual VOID DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface) = 0;
    virtual VOID ResetDevice(void) = 0;

    virtual ULONG GetModeCount(void) = 0;
    PVIDEO_MODE_INFORMATION GetModeInfo(UINT idx) { return &m_ModeInfo[idx]; }
    USHORT GetModeNumber(USHORT idx) { return m_ModeNumbers[idx]; }
    USHORT GetCurrentModeIndex(void) { return m_CurrentMode; }
    VOID SetCurrentModeIndex(USHORT idx) { m_CurrentMode = idx; }
    virtual NTSTATUS ExecutePresentDisplayOnly(_In_ BYTE*             DstAddr,
        _In_ UINT              DstBitPerPixel,
        _In_ BYTE*             SrcAddr,
        _In_ UINT              SrcBytesPerPixel,
        _In_ LONG              SrcPitch,
        _In_ ULONG             NumMoves,
        _In_ D3DKMT_MOVE_RECT* pMoves,
        _In_ ULONG             NumDirtyRects,
        _In_ RECT*             pDirtyRect,
        _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
        _In_ const CURRENT_MODE* pModeCur) = 0;

    virtual VOID BlackOutScreen(CURRENT_MODE* pCurrentMod) = 0;
    virtual NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_MODE* pModeCur) = 0;
    virtual NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition, _In_ CONST CURRENT_MODE* pModeCur) = 0;
    virtual NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE* pEscap) = 0;
    ULONG GetInstanceId(void) { return m_Id; }
    VioGpuDod* GetVioGpu(void) { return m_pVioGpuDod; }
    virtual PBYTE GetEdidData(UINT Idx) = 0;
    virtual PHYSICAL_ADDRESS GetFrameBufferPA(void) = 0;
protected:
    virtual NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo) = 0;
protected:
    VioGpuDod* m_pVioGpuDod;
    PVIDEO_MODE_INFORMATION m_ModeInfo;
    ULONG m_ModeCount;
    PUSHORT m_ModeNumbers;
    USHORT m_CurrentMode;
    USHORT m_CustomMode;
    ULONG  m_Id;
    BYTE m_EDIDs[MAX_CHILDREN][EDID_V1_BLOCK_SIZE];
    BOOLEAN m_bEDID;
};

class VioGpuAdapter :
    public IVioGpuAdapter
{
public:
    VioGpuAdapter(_In_ VioGpuDod* pVioGpuDod);
    ~VioGpuAdapter(void);
    NTSTATUS SetCurrentMode(ULONG Mode, CURRENT_MODE* pCurrentMode);
    ULONG GetModeCount(void) { return m_ModeCount; }
    NTSTATUS SetPowerState(DXGK_DEVICE_INFO* pDeviceInfo, DEVICE_POWER_STATE DevicePowerState, CURRENT_MODE* pCurrentMode);
    NTSTATUS HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo);
    NTSTATUS HWClose(void);
    NTSTATUS ExecutePresentDisplayOnly(_In_ BYTE*       DstAddr,
        _In_ UINT              DstBitPerPixel,
        _In_ BYTE*             SrcAddr,
        _In_ UINT              SrcBytesPerPixel,
        _In_ LONG              SrcPitch,
        _In_ ULONG             NumMoves,
        _In_ D3DKMT_MOVE_RECT* pMoves,
        _In_ ULONG             NumDirtyRects,
        _In_ RECT*             pDirtyRect,
        _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
        _In_ const CURRENT_MODE* pModeCur);
    VOID BlackOutScreen(CURRENT_MODE* pCurrentMod);
    BOOLEAN InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber);
    VOID DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface);
    VOID ResetDevice(VOID);
    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_MODE* pModeCur);
    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition, _In_ CONST CURRENT_MODE* pModeCur);
    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE* pEscap);
    CPciResources* GetPciResources(void) { return &m_PciResources; }
    BOOLEAN IsMSIEnabled() { return m_PciResources.IsMSIEnabled(); }
    PHYSICAL_ADDRESS GetFrameBufferPA(void) { return  m_PciResources.GetPciBar(0)->GetPA(); }

protected:
private:
    NTSTATUS VioGpuAdapterInit(DXGK_DISPLAY_INFORMATION* pDispInfo);
    void SetVideoModeInfo(UINT Idx, PVIOGPU_DISP_MODE pModeInfo);
    void VioGpuAdapterClose(void);
    NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo);
    BOOLEAN AckFeature(UINT64 Feature);
    BOOLEAN GetDisplayInfo(void);
    void ProcessEdid(void);
    void FixEdid(void);
    BOOLEAN GetEdids(void);
    void AddEdidModes(void);
    NTSTATUS UpdateChildStatus(BOOLEAN connect);
    void SetCustomDisplay(_In_ USHORT xres,
        _In_ USHORT yres);
    void CreateFrameBufferObj(PVIDEO_MODE_INFORMATION pModeInfo, CURRENT_MODE* pCurrentMode);
    void DestroyFrameBufferObj(BOOLEAN bReset);
    BOOLEAN CreateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_MODE* pCurrentMode);
    void DestroyCursor(void);
    BOOLEAN GpuObjectAttach(UINT res_id, VioGpuObj* obj);
    void static ThreadWork(_In_ PVOID Context);
    void ThreadWorkRoutine(void);
    void ConfigChanged(void);
    NTSTATUS VirtIoDeviceInit(void);
    PBYTE GetEdidData(UINT Idx);
    VOID CreateResolutionEvent(VOID);
    VOID NotifyResolutionEvent(VOID);
    VOID CloseResolutionEvent(VOID);
private:
    VirtIODevice m_VioDev;
    CPciResources m_PciResources;
    UINT64 m_u64HostFeatures;
    UINT64 m_u64GuestFeatures;
    UINT32 m_u32NumCapsets;
    UINT32 m_u32NumScanouts;
    CtrlQueue m_CtrlQueue;
    CrsrQueue m_CursorQueue;
    VioGpuBuf m_GpuBuf;
    VioGpuIdr m_Idr;
    VioGpuObj* m_pFrameBuf;
    VioGpuObj* m_pCursorBuf;
    VioGpuMemSegment m_CursorSegment;
    VioGpuMemSegment m_FrameSegment;
    volatile ULONG m_PendingWorks;
    KEVENT m_ConfigUpdateEvent;
    PETHREAD m_pWorkThread;
    BOOLEAN m_bStopWorkThread;
    PKEVENT m_ResolutionEvent;
    HANDLE m_ResolutionEventHandle;
};

class VioGpuDod {
private:
    DEVICE_OBJECT* m_pPhysicalDevice;
    DXGKRNL_INTERFACE m_DxgkInterface;
    DXGK_DEVICE_INFO m_DeviceInfo;

    DEVICE_POWER_STATE m_MonitorPowerState;
    DEVICE_POWER_STATE m_AdapterPowerState;
    DRIVER_STATUS_FLAG m_Flags;

    CURRENT_MODE m_CurrentModes[MAX_VIEWS];

    DXGK_DISPLAY_INFORMATION m_SystemDisplayInfo;

    D3DDDI_VIDEO_PRESENT_SOURCE_ID m_SystemDisplaySourceId;
    DXGKARG_SETPOINTERSHAPE m_PointerShape;
    IVioGpuAdapter* m_pHWDevice;
public:
    VioGpuDod(_In_ DEVICE_OBJECT* pPhysicalDeviceObject);
    ~VioGpuDod(void);
#pragma code_seg(push)
#pragma code_seg()
    BOOLEAN IsDriverActive() const
    {
        return m_Flags.DriverStarted;
    }
    BOOLEAN IsHardwareInit() const
    {
        return m_Flags.HardwareInit;
    }
    void SetHardwareInit(BOOLEAN init)
    {
        m_Flags.HardwareInit = init;
    }
    BOOLEAN IsPointerEnabled() const
    {
        return m_Flags.PointerEnabled;
    }
    void SetPointerEnabled(BOOLEAN Enabled)
    {
        m_Flags.PointerEnabled = Enabled;
    }
    BOOLEAN IsVgaDevice(void) const
    {
        return m_Flags.VgaDevice;
    }
    void SetVgaDevice(BOOLEAN Vga)
    {
        m_Flags.VgaDevice = Vga;
    }
    BOOLEAN IsFlexResolution(void) const
    {
        return m_Flags.FlexResolution;
    }
    void SetFlexResolution(BOOLEAN FlexRes)
    {
        m_Flags.FlexResolution = FlexRes;
    }
    BOOLEAN IsUsePhysicalMemory() const
    {
        return m_Flags.UsePhysicalMemory;
    }
    void SetUsePhysicalMemory(BOOLEAN enable)
    {
        m_Flags.UsePhysicalMemory = enable;
    }
#pragma code_seg(pop)

    NTSTATUS StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
        _In_  DXGKRNL_INTERFACE* pDxgkInterface,
        _Out_ ULONG*             pNumberOfViews,
        _Out_ ULONG*             pNumberOfChildren);
    NTSTATUS StopDevice(VOID);
    VOID ResetDevice(VOID);
    NTSTATUS DispatchIoRequest(_In_  ULONG VidPnSourceId,
        _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket);
    NTSTATUS SetPowerState(_In_  ULONG HardwareUid,
        _In_  DEVICE_POWER_STATE DevicePowerState,
        _In_  POWER_ACTION       ActionType);
    NTSTATUS QueryChildRelations(_Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
        _In_                             ULONG                  ChildRelationsSize);
    NTSTATUS QueryChildStatus(_Inout_ DXGK_CHILD_STATUS* pChildStatus,
        _In_    BOOLEAN            NonDestructiveOnly);
    NTSTATUS QueryDeviceDescriptor(_In_    ULONG                   ChildUid,
        _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor);
    BOOLEAN InterruptRoutine(_In_  ULONG MessageNumber);
    VOID DpcRoutine(VOID);
    NTSTATUS QueryAdapterInfo(_In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo);
    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition);
    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape);
    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE* pEscape);
    NTSTATUS PresentDisplayOnly(_In_ CONST DXGKARG_PRESENT_DISPLAYONLY* pPresentDisplayOnly);
    NTSTATUS QueryInterface(_In_ CONST PQUERY_INTERFACE     QueryInterface);
    NTSTATUS IsSupportedVidPn(_Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn);
    NTSTATUS RecommendFunctionalVidPn(_In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn);
    NTSTATUS RecommendVidPnTopology(_In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST pRecommendVidPnTopology);
    NTSTATUS RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes);
    NTSTATUS EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality);
    NTSTATUS SetVidPnSourceVisibility(_In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility);
    NTSTATUS CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn);
    NTSTATUS UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath);
    NTSTATUS QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps);
    NTSTATUS StopDeviceAndReleasePostDisplayOwnership(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
        _Out_ DXGK_DISPLAY_INFORMATION*      pDisplayInfo);
    NTSTATUS SystemDisplayEnable(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID       TargetId,
        _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
        _Out_ UINT*                                pWidth,
        _Out_ UINT*                                pHeight,
        _Out_ D3DDDIFORMAT*                        pColorFormat);
    VOID SystemDisplayWrite(_In_reads_bytes_(SourceHeight * SourceStride) VOID* pSource,
        _In_  UINT                                 SourceWidth,
        _In_  UINT                                 SourceHeight,
        _In_  UINT                                 SourceStride,
        _In_  INT                                  PositionX,
        _In_  INT                                  PositionY);
    PDXGKRNL_INTERFACE GetDxgkInterface(void) { return &m_DxgkInterface; }
private:
    BOOLEAN CheckHardware();
    NTSTATUS WriteRegistryString(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue);
    NTSTATUS WriteRegistryDWORD(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PDWORD pdwValue);
    NTSTATUS ReadRegistryDWORD(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _Inout_ PDWORD pdwValue);
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
    NTSTATUS IsVidPnSourceModeFieldsValid(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode) const;
    NTSTATUS IsVidPnPathFieldsValid(CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath) const;
    NTSTATUS SetRegisterInfo(_In_ ULONG Id, _In_ DWORD MemSize);
    NTSTATUS GetRegisterInfo(void);
    VOID BuildVideoSignalInfo(D3DKMDT_VIDEO_SIGNAL_INFO* pVideoSignalInfo, PVIDEO_MODE_INFORMATION pModeInfo);
};

