/*
 * Copyright (C) 2019-2022 Red Hat, Inc.
 * Copyright (C) 2026 Zededa, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
 *             Mikhail Malyshev <mike.malyshev@gmail.com>
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

#include "viogpu.h"
#include "viogpu_queue.h"
#include "viogpu_commands.h"
#include "viogpu_resources.h"
#include "viogpu_display.h"
#include "viogpu_cursor.h"
#include "viogpu_helper.h"
#include "viogpu_hostdisplays.h"

#pragma pack(push)
#pragma pack(1)

typedef struct
{
    UINT DriverStarted : 1;
    UINT PointerEnabled : 1;
    UINT VgaDevice : 1;
    UINT FlexResolution : 1;
    UINT UsePhysicalMemory : 1;
    UINT UsePresentProgress : 1;
    UINT Unused : 26;
} DRIVER_STATUS_FLAG;

#pragma pack(pop)

// The bugcheck screen: the firmware's frame buffer in BAR 0, as dxgkrnl knows it, mapped once at start.
// SystemDisplayEnable shows it, and SystemDisplayWrite draws into it.
typedef struct _VIOGPU_SYSTEM_DISPLAY
{
    DXGK_DISPLAY_INFORMATION DispInfo;
    PVOID FrameBuffer;
    BOOLEAN Enabled;
} VIOGPU_SYSTEM_DISPLAY;

class VioGpuDod;

// The virtio-gpu format of a D3DDDIFORMAT.
UINT ColorFormat(UINT format);

// The most components an adapter has.
#define VIOGPU_MAX_COMPONENTS 8

class VioGpuAdapter : IVioGpuPCI
{
  public:
    VioGpuAdapter(_In_ VioGpuDod *pVioGpuDod);
    ~VioGpuAdapter(void);
    void Shutdown(void);
    VioGpuDisplayController *GetController(void)
    {
        return &m_Controller;
    }
    VioGpuResources *GetResources(void)
    {
        return &m_Resources;
    }
    VioGpuCursor *GetCursor(void)
    {
        return &m_Cursor;
    }
    VioGpuHelperChannel *GetHelperChannel(void)
    {
        return &m_HelperChannel;
    }
    VioGpuCommands *GetCommands(void)
    {
        return &m_Commands;
    }
    UINT32 GetNumScanouts(void)
    {
        return m_u32NumScanouts;
    }
    BOOLEAN HasFeature(UINT Feature)
    {
        return virtio_is_feature_enabled(m_u64GuestFeatures, Feature);
    }
    UINT32 ReadConfigEvents(void);
    void ClearConfigEvents(UINT32 Events);
    BOOLEAN ModeFits(ULONG Width, ULONG Height);
    NTSTATUS SetPowerState(DXGK_DEVICE_INFO *pDeviceInfo, DEVICE_POWER_STATE DevicePowerState);
    NTSTATUS HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION *pDispInfo);
    BOOLEAN InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_ ULONG MessageNumber);
    VOID DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface);
    VOID ResetDevice(VOID);
    CPciResources *GetPciResources(void)
    {
        return &m_PciResources;
    }
    BOOLEAN ResetToVgaMode(void);
    BOOLEAN IsMSIEnabled()
    {
        return m_PciResources.IsMSIEnabled();
    }
    // The interrupt messages whose lock fences the ISR: every MSI message Windows granted, or the line.
    ULONG GetMessageCount()
    {
        return IsMSIEnabled() ? m_PciResources.GetMessageCount() : 1;
    }
    // The message whose ISR notifies dxgkrnl, DXGK_DRIVERCAPS.InterruptMessageNumber: the control queue's with one
    // message per cause, else message 0, which also stands for the line.
    ULONG GetNotifyMessage()
    {
        return m_PciResources.IsMessagePerCause() ? VIRTIO_GPU_MSIX_CTRL_VECTOR : 0;
    }
    PHYSICAL_ADDRESS GetFrameBufferPA(void)
    {
        return m_PciResources.GetPciBar(0)->GetPA();
    }
    PDXGKRNL_INTERFACE GetDxgkInterface(void);

    VioGpuDod *GetVioGpu(void)
    {
        return m_pVioGpuDod;
    }
    ULONG GetInstanceId(void)
    {
        return m_Id;
    }

  protected:
  private:
    NTSTATUS VioGpuAdapterInit(void);
    void VioGpuAdapterClose(void);
    void InitDisplay(DXGK_DISPLAY_INFORMATION *pDispInfo);
    BOOLEAN AckFeature(UINT64 Feature);
    NTSTATUS VirtIoDeviceInit(void);
    NTSTATUS StartComponents(void);
    void OpenComponents(void);
    void CloseComponents(void);
    void StopComponents(void);

  private:
    VioGpuDod *m_pVioGpuDod;
    VioGpuDisplayController m_Controller;
    ULONG m_Id;

    VirtIODevice m_VioDev;
    CPciResources m_PciResources;
    UINT64 m_u64HostFeatures;
    UINT64 m_u64GuestFeatures;
    UINT32 m_u32NumCapsets;
    UINT32 m_u32NumScanouts;
    VioGpuQueue m_CtrlQueue;
    VioGpuQueue m_CursorQueue;
    VioGpuCommands m_Commands;
    VioGpuBuf m_GpuBuf;
    VioGpuResources m_Resources;
    volatile ULONG m_PendingWorks;
    // The ISR and the DPC use the device and the queues only while this is set. It changes only in
    // SwitchInterrupts, under the interrupt lock of each message whenever one can be taken, so an ISR never touches a
    // device that is not open yet or whose rings are gone. The DPC reads it without the lock: VioGpuAdapterClose
    // switches it off before flushing the DPCs, so a DPC that read it set finishes before the rings are deleted.
    volatile BOOLEAN m_InterruptsOn;
    // The causes still pending when interrupts were last switched on, which belonged to the open before: logged by
    // SwitchInterrupts.
    ULONG m_DroppedWorks;
    // The device is open between VioGpuAdapterInit and VioGpuAdapterClose, which run in level 3 DDIs,
    // DxgkDdiStopDeviceAndReleasePostDisplayOwnership or the bugcheck flow.
    BOOLEAN m_bDeviceOpen;
    void SwitchInterrupts(BOOLEAN On);
    static BOOLEAN InterruptsOnCb(PVOID Context);
    static BOOLEAN InterruptsOffCb(PVOID Context);
    VioGpuCursor m_Cursor;
    VioGpuHelperChannel m_HelperChannel;
    VioGpuHostDisplays m_HostDisplays;
    // The components, in the order they open (VioGpuComponent): how many there are, how many of them started, and
    // whether they are open.
    VioGpuComponent *m_Components[VIOGPU_MAX_COMPONENTS];
    UINT m_ComponentCount;
    UINT m_ComponentsStarted;
    BOOLEAN m_bComponentsOpen;
};

class VioGpuDod
{
  private:
    DEVICE_OBJECT *m_pPhysicalDevice;
    DXGKRNL_INTERFACE m_DxgkInterface;
    DXGK_DEVICE_INFO m_DeviceInfo;

    DEVICE_POWER_STATE m_MonitorPowerState;
    DEVICE_POWER_STATE m_AdapterPowerState;
    DRIVER_STATUS_FLAG m_Flags;

    VIOGPU_SYSTEM_DISPLAY m_SystemDisplay;

    DXGK_DISPLAY_INFORMATION m_SystemDisplayInfo;

    DXGKARG_SETPOINTERSHAPE m_PointerShape;
    // The adapter, from the StartDevice that makes it (the ISR and the DPC reach it while it starts) until StopDevice
    // or a failed StartDevice frees it; NULL otherwise. The DDIs driver.cpp gates on IsDriverActive use it only while
    // DriverStarted is set; QueryAdapterInfo, the ISR, the DPC, ResetDevice and SystemDisplayEnable, which dxgkrnl may
    // call on a stopped device, check it.
    VioGpuAdapter *m_pHWDevice;

    USHORT m_PersistentDispMode0Width;
    USHORT m_PersistentDispMode0Height;

  public:
    VioGpuDod(_In_ DEVICE_OBJECT *pPhysicalDeviceObject);
    ~VioGpuDod(void);
#pragma code_seg(push)
#pragma code_seg()
    BOOLEAN IsDriverActive() const
    {
        return m_Flags.DriverStarted;
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
    BOOLEAN IsUsePresentProgress() const
    {
        return m_Flags.UsePresentProgress;
    }
    void SetUsePresentProgress(BOOLEAN enable)
    {
        m_Flags.UsePresentProgress = enable;
    }
    void SetPersistentDispMode0Width(USHORT res)
    {
        m_PersistentDispMode0Width = res;
    }
    USHORT GetPersistentDispMode0Width()
    {
        return m_PersistentDispMode0Width;
    }
    void SetPersistentDispMode0Height(USHORT res)
    {
        m_PersistentDispMode0Height = res;
    }
    USHORT GetPersistentDispMode0Height()
    {
        return m_PersistentDispMode0Height;
    }
    BOOLEAN IsPersistentDispMode0Set()
    {
        return (m_PersistentDispMode0Width > 0) && (m_PersistentDispMode0Height > 0);
    }
#pragma code_seg(pop)

    NTSTATUS StartDevice(_In_ DXGK_START_INFO *pDxgkStartInfo,
                         _In_ DXGKRNL_INTERFACE *pDxgkInterface,
                         _Out_ ULONG *pNumberOfViews,
                         _Out_ ULONG *pNumberOfChildren);
    NTSTATUS StopDevice(VOID);
    VOID ResetDevice(VOID);
    NTSTATUS DispatchIoRequest(_In_ ULONG VidPnSourceId, _In_ VIDEO_REQUEST_PACKET *pVideoRequestPacket);
    NTSTATUS SetPowerState(_In_ ULONG HardwareUid,
                           _In_ DEVICE_POWER_STATE DevicePowerState,
                           _In_ POWER_ACTION ActionType);
    NTSTATUS QueryChildRelations(_Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR *pChildRelations,
                                 _In_ ULONG ChildRelationsSize);
    NTSTATUS QueryChildStatus(_Inout_ DXGK_CHILD_STATUS *pChildStatus, _In_ BOOLEAN NonDestructiveOnly);
    NTSTATUS QueryDeviceDescriptor(_In_ ULONG ChildUid, _Inout_ DXGK_DEVICE_DESCRIPTOR *pDeviceDescriptor);
    BOOLEAN InterruptRoutine(_In_ ULONG MessageNumber);
    VOID DpcRoutine(VOID);
    NTSTATUS QueryAdapterInfo(_In_ CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo);
    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition);
    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape);
    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE *pEscape);
    NTSTATUS PresentDisplayOnly(_In_ CONST DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly);
    NTSTATUS QueryInterface(_In_ CONST PQUERY_INTERFACE QueryInterface);
    NTSTATUS IsSupportedVidPn(_Inout_ DXGKARG_ISSUPPORTEDVIDPN *pIsSupportedVidPn);
    NTSTATUS RecommendFunctionalVidPn(_In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *CONST pRecommendFunctionalVidPn);
    NTSTATUS RecommendVidPnTopology(_In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *CONST pRecommendVidPnTopology);
    NTSTATUS RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES *CONST pRecommendMonitorModes);
    NTSTATUS EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *CONST pEnumCofuncModality);
    NTSTATUS SetVidPnSourceVisibility(_In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility);
    NTSTATUS CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN *CONST pCommitVidPn);
    NTSTATUS
    UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *CONST pUpdateActiveVidPnPresentPath);
    NTSTATUS QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps);
    NTSTATUS StopDeviceAndReleasePostDisplayOwnership(_In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                      _Out_ DXGK_DISPLAY_INFORMATION *pDisplayInfo);
    NTSTATUS SystemDisplayEnable(_In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                 _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                                 _Out_ UINT *pWidth,
                                 _Out_ UINT *pHeight,
                                 _Out_ D3DDDIFORMAT *pColorFormat);
    VOID SystemDisplayWrite(_In_reads_bytes_(SourceHeight *SourceStride) VOID *pSource,
                            _In_ UINT SourceWidth,
                            _In_ UINT SourceHeight,
                            _In_ UINT SourceStride,
                            _In_ INT PositionX,
                            _In_ INT PositionY);
    NTSTATUS SetRegisterConfigInfo(void);

    PDXGKRNL_INTERFACE GetDxgkInterface(void)
    {
        return &m_DxgkInterface;
    }

  private:
    void DeleteAdapter(void);
    static BOOLEAN DetachAdapterCb(PVOID Context);
    NTSTATUS StartAdapter(_In_ DXGK_START_INFO *pDxgkStartInfo,
                          _In_ DXGKRNL_INTERFACE *pDxgkInterface,
                          _Out_ ULONG *pNumberOfViews,
                          _Out_ ULONG *pNumberOfChildren);
    BOOLEAN CheckHardware();
    NTSTATUS WriteRegistryString(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue);
    NTSTATUS WriteRegistryDWORD(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PDWORD pdwValue);
    NTSTATUS ReadRegistryDWORD(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _Inout_ PDWORD pdwValue);
    NTSTATUS SetSourceModeAndPath(CONST D3DKMDT_VIDPN_SOURCE_MODE *pSourceMode,
                                  CONST D3DKMDT_VIDPN_PRESENT_PATH *pPath);
    NTSTATUS AddSingleMonitorMode(_In_ CONST DXGKARG_RECOMMENDMONITORMODES *CONST pRecommendMonitorModes);
    NTSTATUS AddMonitorMode(D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet,
                            _In_ CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSourceModeSetInterface,
                            _In_ CONST VIOGPU_DISP_MODE *pMode,
                            D3DKMDT_MODE_PREFERENCE Preference);
    VOID AddMissingMonitorModes(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId);
    NTSTATUS GetOfferedModes(_In_opt_ CONST D3DKMDT_2DREGION *pPinnedSize,
                             VIOGPU_DISP_MODE (&Modes)[VIOGPU_MAX_MODES + 1],
                             _Out_ PULONG pCount);
    NTSTATUS GetPinnedTargetSize(D3DKMDT_HVIDPN hVidPn,
                                 _In_ CONST DXGK_VIDPN_INTERFACE *pVidPnInterface,
                                 D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                 _Out_ D3DKMDT_2DREGION *pSize,
                                 _Out_ PBOOLEAN pPinned);
    NTSTATUS AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
                                 D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
                                 _In_opt_ CONST D3DKMDT_2DREGION *pPinnedTargetSize);
    NTSTATUS AddSingleTargetMode(_In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
                                 D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
                                 _In_opt_ CONST D3DKMDT_2DREGION *pPinnedSourceSize);
    NTSTATUS IsVidPnSourceModeFieldsValid(CONST D3DKMDT_VIDPN_SOURCE_MODE *pSourceMode) const;
    NTSTATUS IsVidPnPathFieldsValid(CONST D3DKMDT_VIDPN_PRESENT_PATH *pPath) const;
    NTSTATUS SetRegisterInfo(_In_ ULONG Id, _In_ DWORD MemSize);
    NTSTATUS GetRegisterInfo(void);
    VOID BuildVideoSignalInfo(D3DKMDT_VIDEO_SIGNAL_INFO *pVideoSignalInfo, PVIOGPU_DISP_MODE pModeInfo);
};
