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
#include "viogpu_allocation.h"
#include <viogpu_command.h>
#include <viogpu_vidpn.h>

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


struct CAPSET_INFO {
    ULONG max_version;
    ULONG max_size;
    ULONG id;
};

virtio_gpu_formats ColorFormat(UINT format);

class VioGpuAdapter: IVioGpuPCI {
public:
    VioGpuCommander commander;
    VioGpuVidPN vidpn;
    VioGpuIdr resourceIdr;
    VioGpuIdr ctxIdr;
    CtrlQueue ctrlQueue;

    VioGpuMemSegment frameSegment;

    UINT64 m_u64HostFeatures;
    UINT64 m_u64GuestFeatures;
    UINT32 m_u32NumCapsets;
    UINT32 m_u32NumScanouts;
    UINT64 m_supportedCapsetIDs;


private:
    DEVICE_OBJECT* m_pPhysicalDevice;
    DXGKRNL_INTERFACE m_DxgkInterface;
    DXGK_DEVICE_INFO m_DeviceInfo;

    DEVICE_POWER_STATE m_MonitorPowerState;
    DEVICE_POWER_STATE m_AdapterPowerState;
    DRIVER_STATUS_FLAG m_Flags;

    DXGKARG_SETPOINTERSHAPE m_PointerShape;

    VirtIODevice m_VioDev;
    CPciResources m_PciResources;
    
    CrsrQueue m_CursorQueue;
    VioGpuBuf m_GpuBuf;
    volatile ULONG m_PendingWorks;
    KEVENT m_ConfigUpdateEvent;
    PETHREAD m_pWorkThread;
    BOOLEAN m_bStopWorkThread;
    PKEVENT m_ResolutionEvent;
    HANDLE m_ResolutionEventHandle;

    VioGpuObj* m_pCursorBuf;
    VioGpuMemSegment m_CursorSegment;

    ULONG  m_Id;
    CAPSET_INFO m_capsetInfos[VIRTIO_GPU_MAX_CAPSET_ID + 1];

public:
    VioGpuAdapter(_In_ DEVICE_OBJECT* pPhysicalDeviceObject);
    ~VioGpuAdapter(void);
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
#ifdef RENDER_ONLY
        return FALSE;
#else
        return m_Flags.VgaDevice;
#endif
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
    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE* pEscape);
    NTSTATUS QueryInterface(_In_ CONST PQUERY_INTERFACE     QueryInterface);
    NTSTATUS StopDeviceAndReleasePostDisplayOwnership(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
        _Out_ DXGK_DISPLAY_INFORMATION*      pDisplayInfo);
   PDXGKRNL_INTERFACE GetDxgkInterface(void) { return &m_DxgkInterface; }
    NTSTATUS NotifyInterrupt(DXGKARGCB_NOTIFY_INTERRUPT_DATA* interruptData, BOOL triggerDpc);

    CPciResources* GetPciResources(void) { return &m_PciResources; }
    BOOLEAN IsMSIEnabled() { return m_PciResources.IsMSIEnabled(); }

    VioGpuAllocation* AllocationFromHandle(D3DKMT_HANDLE handle);
    VioGpuResource* ResourceFromHandle(D3DKMT_HANDLE handle);

    PHYSICAL_ADDRESS GetFrameBufferPA(void) { return  m_PciResources.GetPciBar(0)->GetPA(); }

private:
    BOOLEAN CheckHardware();
    NTSTATUS WriteRegistryString(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue);
    NTSTATUS WriteRegistryDWORD(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PDWORD pdwValue);
    NTSTATUS ReadRegistryDWORD(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _Inout_ PDWORD pdwValue);
     NTSTATUS SetRegisterInfo(_In_ ULONG Id, _In_ DWORD MemSize);
    NTSTATUS GetRegisterInfo(void);
    
    NTSTATUS HWInit(PCM_RESOURCE_LIST pResList);
    NTSTATUS HWClose(void);

    ULONG GetInstanceId(void) { return m_Id; }
    

    NTSTATUS VioGpuAdapterInit();
    void VioGpuAdapterClose(void);
    NTSTATUS VirtIoDeviceInit(void);
    BOOLEAN AckFeature(UINT64 Feature);

    void static ThreadWork(_In_ PVOID Context);
    void ThreadWorkRoutine(void);

    void ConfigChanged(void);

    VOID CreateResolutionEvent(VOID);
    VOID NotifyResolutionEvent(VOID);
    VOID CloseResolutionEvent(VOID);

    NTSTATUS UpdateChildStatus(BOOLEAN connect);


    NTSTATUS SetPowerState(DXGK_DEVICE_INFO* pDeviceInfo, DEVICE_POWER_STATE DevicePowerState, CURRENT_MODE* pCurrentMode);
    BOOLEAN InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber);
    VOID DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface);
    
    UINT64 RequestParameter(ULONG parmeter);
};
