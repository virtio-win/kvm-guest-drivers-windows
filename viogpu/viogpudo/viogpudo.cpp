/*
 * Copyright (C) 2019-2022 Red Hat, Inc.
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

#include "helper.h"
#include "driver.h"
#include "viogpudo.h"
#include "baseobj.h"
#include "bitops.h"
#include "viogpum.h"
#include "edid.h"

#if !DBG
#include "viogpudo.tmh"
#endif

static UINT g_InstanceId = 0;

PAGED_CODE_SEG_BEGIN
VioGpuDod::VioGpuDod(_In_ DEVICE_OBJECT *pPhysicalDeviceObject)
    : m_pPhysicalDevice(pPhysicalDeviceObject), m_MonitorPowerState(PowerDeviceD0), m_AdapterPowerState(PowerDeviceD0),
      m_pHWDevice(NULL)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    *((UINT *)&m_Flags) = 0;
    RtlZeroMemory(&m_DxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(&m_DeviceInfo, sizeof(m_DeviceInfo));
    RtlZeroMemory(&m_CurrentMode, sizeof(m_CurrentMode));
    RtlZeroMemory(&m_PointerShape, sizeof(m_PointerShape));
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VioGpuDod::~VioGpuDod(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s 0x%p\n", __FUNCTION__, m_pHWDevice));
    delete m_pHWDevice;
    m_pHWDevice = NULL;
}

BOOLEAN VioGpuDod::CheckHardware()
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_GRAPHICS_DRIVER_MISMATCH;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PCI_COMMON_HEADER Header = {0};
    ULONG BytesRead;

    Status = m_DxgkInterface.DxgkCbReadDeviceSpace(m_DxgkInterface.DeviceHandle,
                                                   DXGK_WHICHSPACE_CONFIG,
                                                   &Header,
                                                   0,
                                                   sizeof(Header),
                                                   &BytesRead);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbReadDeviceSpace failed with status 0x%X\n", Status));
        return FALSE;
    }
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("<--- %s VendorId = 0x%04X DeviceId = 0x%04X\n", __FUNCTION__, Header.VendorID, Header.DeviceID));
    if (Header.VendorID == REDHAT_PCI_VENDOR_ID && Header.DeviceID == 0x1050)
    {
        SetVgaDevice(Header.SubClass == PCI_SUBCLASS_VID_VGA_CTLR);
        return TRUE;
    }

    return FALSE;
}

NTSTATUS VioGpuDod::StartDevice(_In_ DXGK_START_INFO *pDxgkStartInfo,
                                _In_ DXGKRNL_INTERFACE *pDxgkInterface,
                                _Out_ ULONG *pNumberOfViews,
                                _Out_ ULONG *pNumberOfChildren)
{
    PAGED_CODE();

    NTSTATUS Status;

    VIOGPU_ASSERT(pDxgkStartInfo != NULL);
    VIOGPU_ASSERT(pDxgkInterface != NULL);
    VIOGPU_ASSERT(pNumberOfViews != NULL);
    VIOGPU_ASSERT(pNumberOfChildren != NULL);

    if (pDxgkInterface->Size > sizeof(m_DxgkInterface))
    {
        RtlCopyMemory(&m_DxgkInterface, pDxgkInterface, sizeof(m_DxgkInterface));
        m_DxgkInterface.Version = DXGKDDI_INTERFACE_VERSION;
        m_DxgkInterface.Size = sizeof(m_DxgkInterface);
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("VIOGPU: Provided interface version cannot be used by Viogpudo (version %u, size %u), degrading to "
                  "version %u, size %u)\n",
                  pDxgkInterface->Version,
                  pDxgkInterface->Size,
                  m_DxgkInterface.Version,
                  m_DxgkInterface.Size));
    }
    else
    {
        RtlCopyMemory(&m_DxgkInterface, pDxgkInterface, pDxgkInterface->Size);
    }

    RtlZeroMemory(&m_CurrentMode, sizeof(m_CurrentMode));
    m_CurrentMode.DispInfo.TargetId = D3DDDI_ID_UNINITIALIZED;

    Status = m_DxgkInterface.DxgkCbGetDeviceInformation(m_DxgkInterface.DeviceHandle, &m_DeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        VIOGPU_LOG_ASSERTION1("DxgkCbGetDeviceInformation failed with status 0x%X\n", Status);
        return Status;
    }

    if (CheckHardware())
    {
        m_pHWDevice = new (NonPagedPoolNx) VioGpuAdapter(this);
    }
    if (!m_pHWDevice)
    {
        Status = STATUS_NO_MEMORY;
        DbgPrint(TRACE_LEVEL_ERROR, ("StartDevice failed to allocate memory\n"));
        return Status;
    }

    Status = GetRegisterInfo();
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_WARNING, ("GetRegisterInfo failed with status 0x%X\n", Status));
    }

    Status = m_pHWDevice->HWInit(m_DeviceInfo.TranslatedResourceList, &m_CurrentMode.DispInfo);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("HWInit failed with status 0x%X\n", Status));
        return Status;
    }

    Status = SetRegisterInfo(m_pHWDevice->GetInstanceId(), 0);
    if (!NT_SUCCESS(Status))
    {
        VIOGPU_LOG_ASSERTION1("RegisterHWInfo failed with status 0x%X\n", Status);
        return Status;
    }

    if (IsVgaDevice() && m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership)
    {
        Status = m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership(m_DxgkInterface.DeviceHandle, &m_SystemDisplayInfo);
    }

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("DxgkCbAcquirePostDisplayOwnership failed with status 0x%X Width = %d\n",
                  Status,
                  m_SystemDisplayInfo.Width));
        VioGpuDbgBreak();
        return STATUS_UNSUCCESSFUL;
    }
    DbgPrint(TRACE_LEVEL_FATAL,
             ("DxgkCbAcquirePostDisplayOwnership Width = %d Height = %d Pitch = %d ColorFormat = %d\n",
              m_SystemDisplayInfo.Width,
              m_SystemDisplayInfo.Height,
              m_SystemDisplayInfo.Pitch,
              m_SystemDisplayInfo.ColorFormat));

    if (m_SystemDisplayInfo.Width == 0)
    {
        m_SystemDisplayInfo.Width = NOM_WIDTH_SIZE;
        m_SystemDisplayInfo.Height = NOM_HEIGHT_SIZE;
        m_SystemDisplayInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
        m_SystemDisplayInfo.Pitch = (BPPFromPixelFormat(m_SystemDisplayInfo.ColorFormat) / BITS_PER_BYTE) *
                                    m_SystemDisplayInfo.Width;
        m_SystemDisplayInfo.TargetId = 0;
        if (m_SystemDisplayInfo.PhysicAddress.QuadPart == 0LL)
        {
            m_SystemDisplayInfo.PhysicAddress = m_pHWDevice->GetFrameBufferPA();
        }
    }

    m_CurrentMode.DispInfo.Width = max(MIN_WIDTH_SIZE, m_SystemDisplayInfo.Width);
    m_CurrentMode.DispInfo.Height = max(MIN_HEIGHT_SIZE, m_SystemDisplayInfo.Height);
    m_CurrentMode.DispInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
    m_CurrentMode.DispInfo.Pitch = (BPPFromPixelFormat(m_CurrentMode.DispInfo.ColorFormat) / BITS_PER_BYTE) *
                                   m_CurrentMode.DispInfo.Width;
    m_CurrentMode.DispInfo.TargetId = 0;
    if (m_CurrentMode.DispInfo.PhysicAddress.QuadPart == 0LL && m_SystemDisplayInfo.PhysicAddress.QuadPart != 0LL)
    {
        m_CurrentMode.DispInfo.PhysicAddress = m_SystemDisplayInfo.PhysicAddress;
    }

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s ColorFormat = %d\n", __FUNCTION__, m_CurrentMode.DispInfo.ColorFormat));

    *pNumberOfViews = MAX_VIEWS;
    *pNumberOfChildren = MAX_CHILDREN;
    m_Flags.DriverStarted = TRUE;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::StopDevice(VOID)
{
    PAGED_CODE();

    m_Flags.DriverStarted = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::DispatchIoRequest(_In_ ULONG VidPnSourceId, _In_ VIDEO_REQUEST_PACKET *pVideoRequestPacket)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(pVideoRequestPacket);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

PCHAR
DbgDevicePowerString(__in DEVICE_POWER_STATE Type)
{
    PAGED_CODE();

    switch (Type)
    {
        case PowerDeviceUnspecified:
            return "PowerDeviceUnspecified";
        case PowerDeviceD0:
            return "PowerDeviceD0";
        case PowerDeviceD1:
            return "PowerDeviceD1";
        case PowerDeviceD2:
            return "PowerDeviceD2";
        case PowerDeviceD3:
            return "PowerDeviceD3";
        case PowerDeviceMaximum:
            return "PowerDeviceMaximum";
        default:
            return "UnKnown Device Power State";
    }
}

PCHAR
DbgPowerActionString(__in POWER_ACTION Type)
{
    PAGED_CODE();

    switch (Type)
    {
        case PowerActionNone:
            return "PowerActionNone";
        case PowerActionReserved:
            return "PowerActionReserved";
        case PowerActionSleep:
            return "PowerActionSleep";
        case PowerActionHibernate:
            return "PowerActionHibernate";
        case PowerActionShutdown:
            return "PowerActionShutdown";
        case PowerActionShutdownReset:
            return "PowerActionShutdownReset";
        case PowerActionShutdownOff:
            return "PowerActionShutdownOff";
        case PowerActionWarmEject:
            return "PowerActionWarmEject";
        default:
            return "UnKnown Device Power State";
    }
}

NTSTATUS VioGpuDod::SetPowerState(_In_ ULONG HardwareUid,
                                  _In_ DEVICE_POWER_STATE DevicePowerState,
                                  _In_ POWER_ACTION ActionType)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ActionType);
    NTSTATUS Status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_FATAL,
             ("---> %s HardwareUid = 0x%x ActionType = %s DevicePowerState = %s AdapterPowerState = %s\n",
              __FUNCTION__,
              HardwareUid,
              DbgPowerActionString(ActionType),
              DbgDevicePowerString(DevicePowerState),
              DbgDevicePowerString(m_AdapterPowerState)));

    if (HardwareUid == DISPLAY_ADAPTER_HW_ID)
    {
        if (DevicePowerState == PowerDeviceD0)
        {
            if (m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership)
            {
                Status = m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership(m_DxgkInterface.DeviceHandle,
                                                                           &m_SystemDisplayInfo);
            }

            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_FATAL,
                         ("DxgkCbAcquirePostDisplayOwnership failed with status 0x%X Width = %d\n",
                          Status,
                          m_SystemDisplayInfo.Width));
                VioGpuDbgBreak();
            }

            if (m_AdapterPowerState == PowerDeviceD3)
            {
                DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
                Visibility.VidPnSourceId = D3DDDI_ID_ALL;
                Visibility.Visible = FALSE;
                SetVidPnSourceVisibility(&Visibility);
            }
            m_AdapterPowerState = DevicePowerState;
        }

        Status = m_pHWDevice->SetPowerState(&m_DeviceInfo, DevicePowerState, &m_CurrentMode);
        return Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::QueryChildRelations(_Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR *pChildRelations,
                                        _In_ ULONG ChildRelationsSize)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    VIOGPU_ASSERT(pChildRelations != NULL);

    ULONG ChildRelationsCount = (ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR)) - 1;
    VIOGPU_ASSERT(ChildRelationsCount <= MAX_CHILDREN);

    for (UINT ChildIndex = 0; ChildIndex < ChildRelationsCount; ++ChildIndex)
    {
        pChildRelations[ChildIndex].ChildDeviceType = TypeVideoOutput;
        pChildRelations[ChildIndex].ChildCapabilities.HpdAwareness = IsVgaDevice() ? HpdAwarenessAlwaysConnected
                                                                                   : HpdAwarenessInterruptible;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = IsVgaDevice() ? D3DKMDT_VOT_INTERNAL
                                                                                                           : D3DKMDT_VOT_HD15;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
        pChildRelations[ChildIndex].AcpiUid = 0;
        pChildRelations[ChildIndex].ChildUid = ChildIndex;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::QueryChildStatus(_Inout_ DXGK_CHILD_STATUS *pChildStatus, _In_ BOOLEAN NonDestructiveOnly)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    VIOGPU_ASSERT(pChildStatus != NULL);
    VIOGPU_ASSERT(pChildStatus->ChildUid < MAX_CHILDREN);

    switch (pChildStatus->Type)
    {
        case StatusConnection:
            {
                pChildStatus->HotPlug.Connected = IsDriverActive();
                return STATUS_SUCCESS;
            }

        case StatusRotation:
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("Child status being queried for StatusRotation even though D3DKMDT_MOA_NONE was reported"));
                return STATUS_INVALID_PARAMETER;
            }

        default:
            {
                DbgPrint(TRACE_LEVEL_WARNING, ("Unknown pChildStatus->Type (0x%I64x) requested.", pChildStatus->Type));
                return STATUS_NOT_SUPPORTED;
            }
    }
}

NTSTATUS VioGpuDod::QueryDeviceDescriptor(_In_ ULONG ChildUid, _Inout_ DXGK_DEVICE_DESCRIPTOR *pDeviceDescriptor)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    VIOGPU_ASSERT(pDeviceDescriptor != NULL);
    VIOGPU_ASSERT(ChildUid < MAX_CHILDREN);
    PBYTE edid = NULL;

    edid = m_pHWDevice->GetEdidData();

    if (!edid)
    {
        return STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED;
    }
    else if (pDeviceDescriptor->DescriptorOffset < EDID_RAW_BLOCK_SIZE)
    {
        ULONG len = min(pDeviceDescriptor->DescriptorLength,
                        (EDID_RAW_BLOCK_SIZE - pDeviceDescriptor->DescriptorOffset));
        RtlCopyMemory(pDeviceDescriptor->DescriptorBuffer, (edid + pDeviceDescriptor->DescriptorOffset), len);
        pDeviceDescriptor->DescriptorLength = len;
        return STATUS_SUCCESS;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
}

NTSTATUS VioGpuDod::QueryAdapterInfo(_In_ CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pQueryAdapterInfo != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    switch (pQueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_DRIVERCAPS:
            {
                if (!pQueryAdapterInfo->OutputDataSize)
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("pQueryAdapterInfo->OutputDataSize (0x%u) is smaller than sizeof(DXGK_DRIVERCAPS) "
                              "(0x%u)\n",
                              pQueryAdapterInfo->OutputDataSize,
                              sizeof(DXGK_DRIVERCAPS)));
                    return STATUS_BUFFER_TOO_SMALL;
                }

                DXGK_DRIVERCAPS *pDriverCaps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("InterruptMessageNumber = %d, WDDMVersion = %d\n",
                          pDriverCaps->InterruptMessageNumber,
                          pDriverCaps->WDDMVersion));
                RtlZeroMemory(pDriverCaps, pQueryAdapterInfo->OutputDataSize /*sizeof(DXGK_DRIVERCAPS)*/);
                pDriverCaps->WDDMVersion = DXGKDDI_WDDMv1_2;
                pDriverCaps->HighestAcceptableAddress.QuadPart = (ULONG64)-1;

                if (IsPointerEnabled())
                {
                    pDriverCaps->MaxPointerWidth = POINTER_SIZE;
                    pDriverCaps->MaxPointerHeight = POINTER_SIZE;
                    pDriverCaps->PointerCaps.Color = 1;
                    pDriverCaps->PointerCaps.MaskedColor = 1;
                }
                pDriverCaps->SupportNonVGA = IsVgaDevice();
                pDriverCaps->SupportSmoothRotation = TRUE;
                DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s 1\n", __FUNCTION__));
                return STATUS_SUCCESS;
            }

        default:
            {
                DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
                return STATUS_NOT_SUPPORTED;
            }
    }
}

NTSTATUS VioGpuDod::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pSetPointerPosition != NULL);
    VIOGPU_ASSERT(pSetPointerPosition->VidPnSourceId < MAX_VIEWS);
    if (IsPointerEnabled() && pSetPointerPosition->VidPnSourceId == 0)
    {
        return m_pHWDevice->SetPointerPosition(pSetPointerPosition, &m_CurrentMode);
    }
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS VioGpuDod::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pSetPointerShape != NULL);

    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("<---> %s Height = %d, Width = %d, XHot= %d, YHot = %d SourceId = %d\n",
              __FUNCTION__,
              pSetPointerShape->Height,
              pSetPointerShape->Width,
              pSetPointerShape->XHot,
              pSetPointerShape->YHot,
              pSetPointerShape->VidPnSourceId));
    if (IsPointerEnabled() && pSetPointerShape->VidPnSourceId == 0)
    {
        return m_pHWDevice->SetPointerShape(pSetPointerShape, &m_CurrentMode);
    }
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS VioGpuDod::Escape(_In_ CONST DXGKARG_ESCAPE *pEscape)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pEscape != NULL);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s Flags = %d\n", __FUNCTION__, pEscape->Flags.Value));

    return m_pHWDevice->Escape(pEscape);
}

NTSTATUS VioGpuDod::PresentDisplayOnly(_In_ CONST DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    VIOGPU_ASSERT(pPresentDisplayOnly != NULL);
    VIOGPU_ASSERT(pPresentDisplayOnly->VidPnSourceId < MAX_VIEWS);

    if (pPresentDisplayOnly->BytesPerPixel < 4 || pPresentDisplayOnly->VidPnSourceId != 0)
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pPresentDisplayOnly->BytesPerPixel is 0x%d, which is lower than the allowed.\n",
                  pPresentDisplayOnly->BytesPerPixel));
        return STATUS_INVALID_PARAMETER;
    }

    if ((m_MonitorPowerState > PowerDeviceD0) || (m_CurrentMode.Flags.SourceNotVisible))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Source is not visiable\n", __FUNCTION__));
        return STATUS_SUCCESS;
    }

    if (!m_CurrentMode.Flags.FrameBufferIsActive)
    {
        DbgPrint(TRACE_LEVEL_WARNING, ("<--- %s Frame Buffer is Not active\n", __FUNCTION__));
        return STATUS_UNSUCCESSFUL;
    }

    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION RotationNeededByFb = pPresentDisplayOnly->Flags.Rotate ? m_CurrentMode.Rotation
                                                                                               : D3DKMDT_VPPR_IDENTITY;
    BYTE *pDst = (BYTE *)m_CurrentMode.FrameBuffer;
    UINT DstBitPerPixel = BPPFromPixelFormat(m_CurrentMode.DispInfo.ColorFormat);
    if (m_CurrentMode.Scaling == D3DKMDT_VPPS_CENTERED)
    {
        UINT CenterShift = (m_CurrentMode.DispInfo.Height - m_CurrentMode.SrcModeHeight) * m_CurrentMode.DispInfo.Pitch;
        CenterShift += (m_CurrentMode.DispInfo.Width - m_CurrentMode.SrcModeWidth) * DstBitPerPixel / 8;
        pDst += (int)CenterShift / 2;
    }
    Status = m_pHWDevice->ExecutePresentDisplayOnly(pDst,
                                                    DstBitPerPixel,
                                                    (BYTE *)pPresentDisplayOnly->pSource,
                                                    pPresentDisplayOnly->BytesPerPixel,
                                                    pPresentDisplayOnly->Pitch,
                                                    pPresentDisplayOnly->NumMoves,
                                                    pPresentDisplayOnly->pMoves,
                                                    pPresentDisplayOnly->NumDirtyRects,
                                                    pPresentDisplayOnly->pDirtyRect,
                                                    RotationNeededByFb,
                                                    &m_CurrentMode);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuDod::QueryInterface(_In_ CONST PQUERY_INTERFACE pQueryInterface)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pQueryInterface != NULL);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s Version = %d\n", __FUNCTION__, pQueryInterface->Version));

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS VioGpuDod::StopDeviceAndReleasePostDisplayOwnership(_In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                             _Out_ DXGK_DISPLAY_INFORMATION *pDisplayInfo)
{
    PAGED_CODE();

    VIOGPU_ASSERT(TargetId < MAX_CHILDREN);

    // FIXME!!!
    if (m_MonitorPowerState > PowerDeviceD0)
    {
        SetPowerState(TargetId, PowerDeviceD0, PowerActionNone);
    }

    m_pHWDevice->BlackOutScreen(&m_CurrentMode);
    DbgPrint(TRACE_LEVEL_FATAL,
             ("StopDeviceAndReleasePostDisplayOwnership Width = %d Height = %d Pitch = %d ColorFormat = %dn",
              m_SystemDisplayInfo.Width,
              m_SystemDisplayInfo.Height,
              m_SystemDisplayInfo.Pitch,
              m_SystemDisplayInfo.ColorFormat));

    *pDisplayInfo = m_SystemDisplayInfo;
    pDisplayInfo->TargetId = TargetId;
    pDisplayInfo->AcpiId = m_CurrentMode.DispInfo.AcpiId;
    return StopDevice();
}

NTSTATUS VioGpuDod::QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pVidPnHWCaps != NULL);
    VIOGPU_ASSERT(pVidPnHWCaps->SourceId < MAX_VIEWS);
    VIOGPU_ASSERT(pVidPnHWCaps->TargetId < MAX_CHILDREN);

    pVidPnHWCaps->VidPnHWCaps.DriverRotation = 1;
    pVidPnHWCaps->VidPnHWCaps.DriverScaling = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverCloning = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert = 1;
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay = 0;
    pVidPnHWCaps->VidPnHWCaps.Reserved = 0;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::IsSupportedVidPn(_Inout_ DXGKARG_ISSUPPORTEDVIDPN *pIsSupportedVidPn)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pIsSupportedVidPn != NULL);

    if (pIsSupportedVidPn->hDesiredVidPn == 0)
    {
        pIsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    pIsSupportedVidPn->IsVidPnSupported = FALSE;

    CONST DXGK_VIDPN_INTERFACE *pVidPnInterface;
    NTSTATUS Status = m_DxgkInterface.DxgkCbQueryVidPnInterface(pIsSupportedVidPn->hDesiredVidPn,
                                                                DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                                &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("DxgkCbQueryVidPnInterface failed with Status = 0x%X, hDesiredVidPn = %llu\n",
                  Status,
                  LONG_PTR(pIsSupportedVidPn->hDesiredVidPn)));
        return Status;
    }

    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *pVidPnTopologyInterface;
    Status = pVidPnInterface->pfnGetTopology(pIsSupportedVidPn->hDesiredVidPn,
                                             &hVidPnTopology,
                                             &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnGetTopology failed with Status = 0x%X, hDesiredVidPn = %llu\n",
                  Status,
                  LONG_PTR(pIsSupportedVidPn->hDesiredVidPn)));
        return Status;
    }

    for (D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = 0; SourceId < MAX_VIEWS; ++SourceId)
    {
        SIZE_T NumPathsFromSource = 0;
        Status = pVidPnTopologyInterface->pfnGetNumPathsFromSource(hVidPnTopology, SourceId, &NumPathsFromSource);
        if (Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY)
        {
            continue;
        }
        else if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnGetNumPathsFromSource failed with Status = 0x%X hVidPnTopology = %llu, SourceId = %llu",
                      Status,
                      LONG_PTR(hVidPnTopology),
                      LONG_PTR(SourceId)));
            return Status;
        }
        else if (NumPathsFromSource > MAX_CHILDREN)
        {
            return STATUS_SUCCESS;
        }
    }

    pIsSupportedVidPn->IsVidPnSupported = TRUE;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS
VioGpuDod::RecommendFunctionalVidPn(_In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *CONST pRecommendFunctionalVidPn)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pRecommendFunctionalVidPn == NULL);

    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS VioGpuDod::RecommendVidPnTopology(_In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *CONST pRecommendVidPnTopology)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pRecommendVidPnTopology == NULL);

    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS VioGpuDod::RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES *CONST pRecommendMonitorModes)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    return AddSingleMonitorMode(pRecommendMonitorModes);
}

NTSTATUS VioGpuDod::AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
                                        D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
                                        D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(SourceId);

    for (ULONG idx = 0; idx < m_pHWDevice->GetModeCount(); ++idx)
    {
        D3DKMDT_VIDPN_SOURCE_MODE *pVidPnSourceModeInfo = NULL;
        PVIDEO_MODE_INFORMATION pModeInfo = m_pHWDevice->GetModeInfo(idx);
        NTSTATUS Status = pVidPnSourceModeSetInterface->pfnCreateNewModeInfo(hVidPnSourceModeSet,
                                                                             &pVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnCreateNewModeInfo failed with Status = 0x%X, hVidPnSourceModeSet = %llu",
                      Status,
                      LONG_PTR(hVidPnSourceModeSet)));
            return Status;
        }

        pVidPnSourceModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx = pModeInfo->VisScreenWidth;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy = pModeInfo->VisScreenHeight;
        pVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize;
        pVidPnSourceModeInfo->Format.Graphics.Stride = pModeInfo->ScreenStride;
        pVidPnSourceModeInfo->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        pVidPnSourceModeInfo->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
        pVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

        Status = pVidPnSourceModeSetInterface->pfnAddMode(hVidPnSourceModeSet, pVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            NTSTATUS TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet,
                                                                                   pVidPnSourceModeInfo);
            UNREFERENCED_PARAMETER(TempStatus);
            NT_ASSERT(NT_SUCCESS(TempStatus));

            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("pfnAddMode failed with Status = 0x%X, hVidPnSourceModeSet = %llu, pVidPnSourceModeInfo = %p",
                          Status,
                          LONG_PTR(hVidPnSourceModeSet),
                          pVidPnSourceModeInfo));
                return Status;
            }
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

VOID VioGpuDod::BuildVideoSignalInfo(D3DKMDT_VIDEO_SIGNAL_INFO *pVideoSignalInfo, PVIDEO_MODE_INFORMATION pModeInfo)
{
    PAGED_CODE();

    pVideoSignalInfo->VideoStandard = D3DKMDT_VSS_OTHER;
    pVideoSignalInfo->TotalSize.cx = pModeInfo->VisScreenWidth;
    pVideoSignalInfo->TotalSize.cy = pModeInfo->VisScreenHeight;

    pVideoSignalInfo->VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pVideoSignalInfo->VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pVideoSignalInfo->HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pVideoSignalInfo->HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pVideoSignalInfo->PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pVideoSignalInfo->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
}

NTSTATUS VioGpuDod::AddSingleTargetMode(_In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
                                        D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
                                        _In_opt_ CONST D3DKMDT_VIDPN_SOURCE_MODE *pVidPnPinnedSourceModeInfo,
                                        D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(pVidPnPinnedSourceModeInfo);

    D3DKMDT_VIDPN_TARGET_MODE *pVidPnTargetModeInfo = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    for (UINT ModeIndex = 0; ModeIndex < m_pHWDevice->GetModeCount(); ++ModeIndex)
    {
        PVIDEO_MODE_INFORMATION pModeInfo = m_pHWDevice->GetModeInfo(SourceId);
        pVidPnTargetModeInfo = NULL;
        Status = pVidPnTargetModeSetInterface->pfnCreateNewModeInfo(hVidPnTargetModeSet, &pVidPnTargetModeInfo);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnCreateNewModeInfo failed with Status = 0x%X, hVidPnTargetModeSet = %llu",
                      Status,
                      LONG_PTR(hVidPnTargetModeSet)));
            return Status;
        }
        pVidPnTargetModeInfo->VideoSignalInfo.ActiveSize = pVidPnTargetModeInfo->VideoSignalInfo.TotalSize;
        BuildVideoSignalInfo(&pVidPnTargetModeInfo->VideoSignalInfo, pModeInfo);

        if (pModeInfo->VisScreenWidth == NOM_WIDTH_SIZE && pModeInfo->VisScreenHeight == NOM_HEIGHT_SIZE)
        {
            pVidPnTargetModeInfo->Preference = D3DKMDT_MP_PREFERRED;
        }
        else
        {
            pVidPnTargetModeInfo->Preference = D3DKMDT_MP_NOTPREFERRED;
        }

        Status = pVidPnTargetModeSetInterface->pfnAddMode(hVidPnTargetModeSet, pVidPnTargetModeInfo);
        if (!NT_SUCCESS(Status))
        {
            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("pfnAddMode failed with Status = 0x%X, hVidPnTargetModeSet = 0x%llu, pVidPnTargetModeInfo = "
                          "%p\n",
                          Status,
                          LONG_PTR(hVidPnTargetModeSet),
                          pVidPnTargetModeInfo));
            }

            Status = pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pVidPnTargetModeInfo);
            NT_ASSERT(NT_SUCCESS(Status));
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::AddSingleMonitorMode(_In_ CONST DXGKARG_RECOMMENDMONITORMODES *CONST pRecommendMonitorModes)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSourceMode = NULL;
    PVIDEO_MODE_INFORMATION pVbeModeInfo = NULL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet,
                                                                                          &pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnCreateNewModeInfo failed with Status = 0x%X, hMonitorSourceModeSet = 0x%llu\n",
                  Status,
                  LONG_PTR(pRecommendMonitorModes->hMonitorSourceModeSet)));
        return Status;
    }

    pVbeModeInfo = m_pHWDevice->GetModeInfo(m_pHWDevice->GetCurrentModeIndex());

    BuildVideoSignalInfo(&pMonitorSourceMode->VideoSignalInfo, pVbeModeInfo);

    pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
    pMonitorSourceMode->Preference = D3DKMDT_MP_PREFERRED;
    pMonitorSourceMode->ColorBasis = D3DKMDT_CB_SRGB;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FirstChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.SecondChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FourthChannel = 8;

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(pRecommendMonitorModes->hMonitorSourceModeSet,
                                                                                pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnAddMode failed with Status = 0x%X, hMonitorSourceModeSet = 0x%llu, pMonitorSourceMode = "
                      "0x%p\n",
                      Status,
                      LONG_PTR(pRecommendMonitorModes->hMonitorSourceModeSet),
                      pMonitorSourceMode));
        }
        else
        {
            Status = STATUS_SUCCESS;
        }

        NTSTATUS TempStatus = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet,
                                                                                                         pMonitorSourceMode);
        UNREFERENCED_PARAMETER(TempStatus);
        NT_ASSERT(NT_SUCCESS(TempStatus));
        return Status;
    }

    for (UINT Idx = 0; Idx < m_pHWDevice->GetModeCount(); ++Idx)
    {
        pVbeModeInfo = m_pHWDevice->GetModeInfo(Idx);

        Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet,
                                                                                              &pMonitorSourceMode);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnCreateNewModeInfo failed with Status = 0x%X, hMonitorSourceModeSet = 0x%llu\n",
                      Status,
                      LONG_PTR(pRecommendMonitorModes->hMonitorSourceModeSet)));
            return Status;
        }

        DbgPrint(TRACE_LEVEL_INFORMATION,
                 ("%s: add pref mode, dimensions %ux%u, taken from DxgkCbAcquirePostDisplayOwnership at StartDevice\n",
                  __FUNCTION__,
                  pVbeModeInfo->VisScreenWidth,
                  pVbeModeInfo->VisScreenHeight));

        BuildVideoSignalInfo(&pMonitorSourceMode->VideoSignalInfo, pVbeModeInfo);

        pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
        pMonitorSourceMode->ColorBasis = D3DKMDT_CB_SRGB;
        pMonitorSourceMode->ColorCoeffDynamicRanges.FirstChannel = 8;
        pMonitorSourceMode->ColorCoeffDynamicRanges.SecondChannel = 8;
        pMonitorSourceMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
        pMonitorSourceMode->ColorCoeffDynamicRanges.FourthChannel = 8;
        if (pVbeModeInfo->VisScreenWidth == NOM_WIDTH_SIZE && pVbeModeInfo->VisScreenHeight == NOM_HEIGHT_SIZE)
        {
            pMonitorSourceMode->Preference = D3DKMDT_MP_PREFERRED;
        }
        else
        {
            pMonitorSourceMode->Preference = D3DKMDT_MP_NOTPREFERRED;
        }

        Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(pRecommendMonitorModes->hMonitorSourceModeSet,
                                                                                    pMonitorSourceMode);
        if (!NT_SUCCESS(Status))
        {
            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("pfnAddMode failed with Status = 0x%X, hMonitorSourceModeSet = 0x%llu, pMonitorSourceMode = "
                          "0x%p\n",
                          Status,
                          LONG_PTR(pRecommendMonitorModes->hMonitorSourceModeSet),
                          pMonitorSourceMode));
            }

            Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet,
                                                                                                pMonitorSourceMode);
            NT_ASSERT(NT_SUCCESS(Status));
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuDod::EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *CONST pEnumCofuncModality)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pEnumCofuncModality != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology = 0;
    D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet = 0;
    D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet = 0;
    CONST DXGK_VIDPN_INTERFACE *pVidPnInterface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *pVidPnTopologyInterface = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface = NULL;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPath = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPathTemp = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *pVidPnPinnedSourceModeInfo = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *pVidPnPinnedTargetModeInfo = NULL;

    NTSTATUS Status = m_DxgkInterface.DxgkCbQueryVidPnInterface(pEnumCofuncModality->hConstrainingVidPn,
                                                                DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                                &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("DxgkCbQueryVidPnInterface failed with Status = 0x%X, hFunctionalVidPn = 0x%llu\n",
                  Status,
                  LONG_PTR(pEnumCofuncModality->hConstrainingVidPn)));
        return Status;
    }

    Status = pVidPnInterface->pfnGetTopology(pEnumCofuncModality->hConstrainingVidPn,
                                             &hVidPnTopology,
                                             &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnGetTopology failed with Status = 0x%X, hFunctionalVidPn = 0x%llu\n",
                  Status,
                  LONG_PTR(pEnumCofuncModality->hConstrainingVidPn)));
        return Status;
    }

    Status = pVidPnTopologyInterface->pfnAcquireFirstPathInfo(hVidPnTopology, &pVidPnPresentPath);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnAcquireFirstPathInfo failed with Status = 0x%X, hVidPnTopology = 0x%llu\n",
                  Status,
                  LONG_PTR(hVidPnTopology)));
        return Status;
    }

    while (Status != STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
    {
        Status = pVidPnInterface->pfnAcquireSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                          pVidPnPresentPath->VidPnSourceId,
                                                          &hVidPnSourceModeSet,
                                                          &pVidPnSourceModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnAcquireSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%llu, SourceId = "
                      "0x%llu\n",
                      Status,
                      LONG_PTR(pEnumCofuncModality->hConstrainingVidPn),
                      LONG_PTR(pVidPnPresentPath->VidPnSourceId)));
            break;
        }

        Status = pVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnSourceModeSet,
                                                                        &pVidPnPinnedSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnAcquirePinnedModeInfo failed with Status = 0x%X, hVidPnSourceModeSet = 0x%llu\n",
                      Status,
                      LONG_PTR(hVidPnSourceModeSet)));
            break;
        }

        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == pVidPnPresentPath->VidPnSourceId)))
        {
            if (pVidPnPinnedSourceModeInfo == NULL)
            {
                Status = pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                  hVidPnSourceModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("pfnReleaseSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%llu, "
                              "hVidPnSourceModeSet = 0x%llu\n",
                              Status,
                              LONG_PTR(pEnumCofuncModality->hConstrainingVidPn),
                              LONG_PTR(hVidPnSourceModeSet)));
                    break;
                }
                hVidPnSourceModeSet = 0;

                Status = pVidPnInterface->pfnCreateNewSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                    pVidPnPresentPath->VidPnSourceId,
                                                                    &hVidPnSourceModeSet,
                                                                    &pVidPnSourceModeSetInterface);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("pfnCreateNewSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%llu, "
                              "SourceId = 0x%llu\n",
                              Status,
                              LONG_PTR(pEnumCofuncModality->hConstrainingVidPn),
                              LONG_PTR(pVidPnPresentPath->VidPnSourceId)));
                    break;
                }

                {
                    Status = AddSingleSourceMode(pVidPnSourceModeSetInterface,
                                                 hVidPnSourceModeSet,
                                                 pVidPnPresentPath->VidPnSourceId);
                }

                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("AddSingleSourceMode failed with Status = 0x%X, hFunctionalVidPn = 0x%llu\n",
                              Status,
                              LONG_PTR(pEnumCofuncModality->hConstrainingVidPn)));
                    break;
                }

                Status = pVidPnInterface->pfnAssignSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                 pVidPnPresentPath->VidPnSourceId,
                                                                 hVidPnSourceModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("pfnAssignSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%llu, SourceId "
                              "= 0x%llu, hVidPnSourceModeSet = 0x%llu\n",
                              Status,
                              LONG_PTR(pEnumCofuncModality->hConstrainingVidPn),
                              LONG_PTR(pVidPnPresentPath->VidPnSourceId),
                              LONG_PTR(hVidPnSourceModeSet)));
                    break;
                }
                hVidPnSourceModeSet = 0;
            }
        }

        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == pVidPnPresentPath->VidPnTargetId)))
        {
            Status = pVidPnInterface->pfnAcquireTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                              pVidPnPresentPath->VidPnTargetId,
                                                              &hVidPnTargetModeSet,
                                                              &pVidPnTargetModeSetInterface);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("pfnAcquireTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%llu, TargetId = "
                          "0x%llu\n",
                          Status,
                          LONG_PTR(pEnumCofuncModality->hConstrainingVidPn),
                          LONG_PTR(pVidPnPresentPath->VidPnTargetId)));
                break;
            }

            Status = pVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnTargetModeSet,
                                                                            &pVidPnPinnedTargetModeInfo);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("pfnAcquirePinnedModeInfo failed with Status = 0x%X, hVidPnTargetModeSet = 0x%llu\n",
                          Status,
                          LONG_PTR(hVidPnTargetModeSet)));
                break;
            }

            if (pVidPnPinnedTargetModeInfo == NULL)
            {
                Status = pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                  hVidPnTargetModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("pfnReleaseTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%llu, "
                              "hVidPnTargetModeSet = 0x%llu\n",
                              Status,
                              LONG_PTR(pEnumCofuncModality->hConstrainingVidPn),
                              LONG_PTR(hVidPnTargetModeSet)));
                    break;
                }
                hVidPnTargetModeSet = 0;

                Status = pVidPnInterface->pfnCreateNewTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                    pVidPnPresentPath->VidPnTargetId,
                                                                    &hVidPnTargetModeSet,
                                                                    &pVidPnTargetModeSetInterface);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("pfnCreateNewTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%llu, "
                              "TargetId = 0x%llu\n",
                              Status,
                              LONG_PTR(pEnumCofuncModality->hConstrainingVidPn),
                              LONG_PTR(pVidPnPresentPath->VidPnTargetId)));
                    break;
                }

                Status = AddSingleTargetMode(pVidPnTargetModeSetInterface,
                                             hVidPnTargetModeSet,
                                             pVidPnPinnedSourceModeInfo,
                                             pVidPnPresentPath->VidPnSourceId);

                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("AddSingleTargetMode failed with Status = 0x%X, hFunctionalVidPn = 0x%llu\n",
                              Status,
                              LONG_PTR(pEnumCofuncModality->hConstrainingVidPn)));
                    break;
                }

                Status = pVidPnInterface->pfnAssignTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                 pVidPnPresentPath->VidPnTargetId,
                                                                 hVidPnTargetModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("pfnAssignTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%llu, TargetId "
                              "= 0x%llu, hVidPnTargetModeSet = 0x%llu\n",
                              Status,
                              LONG_PTR(pEnumCofuncModality->hConstrainingVidPn),
                              LONG_PTR(pVidPnPresentPath->VidPnTargetId),
                              LONG_PTR(hVidPnTargetModeSet)));
                    break;
                }
                hVidPnTargetModeSet = 0;
            }
            else
            {
                Status = pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet,
                                                                          pVidPnPinnedTargetModeInfo);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("pfnReleaseModeInfo failed with Status = 0x%X, hVidPnTargetModeSet = 0x%llu, "
                              "pVidPnPinnedTargetModeInfo = %p\n",
                              Status,
                              LONG_PTR(hVidPnTargetModeSet),
                              pVidPnPinnedTargetModeInfo));
                    break;
                }
                pVidPnPinnedTargetModeInfo = NULL;

                Status = pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                  hVidPnTargetModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("pfnReleaseTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%llu, "
                              "hVidPnTargetModeSet = 0x%llu\n",
                              Status,
                              LONG_PTR(pEnumCofuncModality->hConstrainingVidPn),
                              LONG_PTR(hVidPnTargetModeSet)));
                    break;
                }
                hVidPnTargetModeSet = 0;
            }
        }

        if (pVidPnPinnedSourceModeInfo != NULL)
        {
            Status = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pVidPnPinnedSourceModeInfo);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("pfnReleaseModeInfo failed with Status = 0x%X, hVidPnSourceModeSet = 0x%llu, "
                          "pVidPnPinnedSourceModeInfo = %p\n",
                          Status,
                          LONG_PTR(hVidPnSourceModeSet),
                          pVidPnPinnedSourceModeInfo));
                break;
            }
            pVidPnPinnedSourceModeInfo = NULL;
        }

        if (hVidPnSourceModeSet != 0)
        {
            Status = pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                              hVidPnSourceModeSet);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("pfnReleaseSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%llu, "
                          "hVidPnSourceModeSet = 0x%llu\n",
                          Status,
                          LONG_PTR(pEnumCofuncModality->hConstrainingVidPn),
                          LONG_PTR(hVidPnSourceModeSet)));
                break;
            }
            hVidPnSourceModeSet = 0;
        }

        D3DKMDT_VIDPN_PRESENT_PATH LocalVidPnPresentPath = *pVidPnPresentPath;
        BOOLEAN SupportFieldsModified = FALSE;

        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_SCALING) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == pVidPnPresentPath->VidPnSourceId) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == pVidPnPresentPath->VidPnTargetId)))
        {
            if (pVidPnPresentPath->ContentTransformation.Scaling == D3DKMDT_VPPS_UNPINNED)
            {
                RtlZeroMemory(&(LocalVidPnPresentPath.ContentTransformation.ScalingSupport),
                              sizeof(D3DKMDT_VIDPN_PRESENT_PATH_SCALING_SUPPORT));
                LocalVidPnPresentPath.ContentTransformation.ScalingSupport.Identity = 1;
                LocalVidPnPresentPath.ContentTransformation.ScalingSupport.Centered = 1;
                SupportFieldsModified = TRUE;
            }
        }

        if (!((pEnumCofuncModality->EnumPivotType != D3DKMDT_EPT_ROTATION) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == pVidPnPresentPath->VidPnSourceId) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == pVidPnPresentPath->VidPnTargetId)))
        {
            if (pVidPnPresentPath->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED)
            {
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Identity = 1;
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Rotate90 = 1;
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Rotate180 = 0;
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Rotate270 = 0;
                SupportFieldsModified = TRUE;
            }
        }

        if (SupportFieldsModified)
        {
            Status = pVidPnTopologyInterface->pfnUpdatePathSupportInfo(hVidPnTopology, &LocalVidPnPresentPath);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("pfnUpdatePathSupportInfo failed with Status = 0x%X, hVidPnTopology = 0x%llu\n",
                          Status,
                          LONG_PTR(hVidPnTopology)));
                break;
            }
        }

        pVidPnPresentPathTemp = pVidPnPresentPath;
        Status = pVidPnTopologyInterface->pfnAcquireNextPathInfo(hVidPnTopology,
                                                                 pVidPnPresentPathTemp,
                                                                 &pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnAcquireNextPathInfo failed with Status = 0x%X, hVidPnTopology = 0x%llu, "
                      "pVidPnPresentPathTemp = %p\n",
                      Status,
                      LONG_PTR(hVidPnTopology),
                      pVidPnPresentPathTemp));
            break;
        }

        NTSTATUS TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPathTemp);
        if (!NT_SUCCESS(TempStatus))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnReleasePathInfo failed with Status = 0x%X, hVidPnTopology = 0x%llu, pVidPnPresentPathTemp = "
                      "%p\n",
                      TempStatus,
                      LONG_PTR(hVidPnTopology),
                      pVidPnPresentPathTemp));
            Status = TempStatus;
            break;
        }
        pVidPnPresentPathTemp = NULL;
    }

    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
    {
        Status = STATUS_SUCCESS;
    }

    NTSTATUS TempStatus = STATUS_NOT_FOUND;

    if ((pVidPnSourceModeSetInterface != NULL) && (pVidPnPinnedSourceModeInfo != NULL))
    {
        TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pVidPnPinnedSourceModeInfo);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if ((pVidPnTargetModeSetInterface != NULL) && (pVidPnPinnedTargetModeInfo != NULL))
    {
        TempStatus = pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pVidPnPinnedTargetModeInfo);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (pVidPnPresentPath != NULL)
    {
        TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPath);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (pVidPnPresentPathTemp != NULL)
    {
        TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPathTemp);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (hVidPnSourceModeSet != 0)
    {
        TempStatus = pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                              hVidPnSourceModeSet);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (hVidPnTargetModeSet != 0)
    {
        TempStatus = pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                              hVidPnTargetModeSet);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    VIOGPU_ASSERT_CHK(TempStatus == STATUS_NOT_FOUND || Status != STATUS_SUCCESS);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuDod::SetVidPnSourceVisibility(_In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pSetVidPnSourceVisibility != NULL);
    VIOGPU_ASSERT((pSetVidPnSourceVisibility->VidPnSourceId < MAX_VIEWS) ||
                  (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL));

    if (pSetVidPnSourceVisibility->Visible)
    {
        m_CurrentMode.Flags.FullscreenPresent = TRUE;
    }
    else
    {
        m_pHWDevice->BlackOutScreen(&m_CurrentMode);
    }

    m_CurrentMode.Flags.SourceNotVisible = !(pSetVidPnSourceVisibility->Visible);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN *CONST pCommitVidPn)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pCommitVidPn != NULL);
    VIOGPU_ASSERT(pCommitVidPn->AffectedVidPnSourceId < MAX_VIEWS);

    NTSTATUS Status;
    SIZE_T NumPaths = 0;
    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology = 0;
    D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet = 0;
    CONST DXGK_VIDPN_INTERFACE *pVidPnInterface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *pVidPnTopologyInterface = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPath = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *pPinnedVidPnSourceModeInfo = NULL;

    if (pCommitVidPn->Flags.PathPoweredOff)
    {
        Status = STATUS_SUCCESS;
        goto CommitVidPnExit;
    }

    Status = m_DxgkInterface.DxgkCbQueryVidPnInterface(pCommitVidPn->hFunctionalVidPn,
                                                       DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                       &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("DxgkCbQueryVidPnInterface failed with Status = 0x%X, hFunctionalVidPn = 0x%llu\n",
                  Status,
                  LONG_PTR(pCommitVidPn->hFunctionalVidPn)));
        goto CommitVidPnExit;
    }

    Status = pVidPnInterface->pfnGetTopology(pCommitVidPn->hFunctionalVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnGetTopology failed with Status = 0x%X, hFunctionalVidPn = 0x%llu\n",
                  Status,
                  LONG_PTR(pCommitVidPn->hFunctionalVidPn)));
        goto CommitVidPnExit;
    }

    Status = pVidPnTopologyInterface->pfnGetNumPaths(hVidPnTopology, &NumPaths);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnGetNumPaths failed with Status = 0x%X, hVidPnTopology = 0x%llu\n",
                  Status,
                  LONG_PTR(hVidPnTopology)));
        goto CommitVidPnExit;
    }

    if (NumPaths != 0)
    {
        Status = pVidPnInterface->pfnAcquireSourceModeSet(pCommitVidPn->hFunctionalVidPn,
                                                          pCommitVidPn->AffectedVidPnSourceId,
                                                          &hVidPnSourceModeSet,
                                                          &pVidPnSourceModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnAcquireSourceModeSet failed with Status = 0x%X, hFunctionalVidPn = 0x%llu, SourceId = "
                      "0x%I64x\n",
                      Status,
                      LONG_PTR(pCommitVidPn->hFunctionalVidPn),
                      pCommitVidPn->AffectedVidPnSourceId));
            goto CommitVidPnExit;
        }

        Status = pVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnSourceModeSet,
                                                                        &pPinnedVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnAcquirePinnedModeInfo failed with Status = 0x%X, hFunctionalVidPn = 0x%llu\n",
                      Status,
                      LONG_PTR(pCommitVidPn->hFunctionalVidPn)));
            goto CommitVidPnExit;
        }
    }
    else
    {
        pPinnedVidPnSourceModeInfo = NULL;
    }

    if (pPinnedVidPnSourceModeInfo == NULL)
    {
        Status = STATUS_SUCCESS;
        goto CommitVidPnExit;
    }

    Status = IsVidPnSourceModeFieldsValid(pPinnedVidPnSourceModeInfo);
    if (!NT_SUCCESS(Status))
    {
        goto CommitVidPnExit;
    }

    SIZE_T NumPathsFromSource = 0;
    Status = pVidPnTopologyInterface->pfnGetNumPathsFromSource(hVidPnTopology,
                                                               pCommitVidPn->AffectedVidPnSourceId,
                                                               &NumPathsFromSource);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnGetNumPathsFromSource failed with Status = 0x%X, hVidPnTopology = 0x%llu\n",
                  Status,
                  LONG_PTR(hVidPnTopology)));
        goto CommitVidPnExit;
    }

    for (SIZE_T PathIndex = 0; PathIndex < NumPathsFromSource; ++PathIndex)
    {
        D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId = D3DDDI_ID_UNINITIALIZED;
        Status = pVidPnTopologyInterface->pfnEnumPathTargetsFromSource(hVidPnTopology,
                                                                       pCommitVidPn->AffectedVidPnSourceId,
                                                                       PathIndex,
                                                                       &TargetId);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnEnumPathTargetsFromSource failed with Status = 0x%X, hVidPnTopology = 0x%llu, SourceId = "
                      "0x%I64x, PathIndex = 0x%I64x\n",
                      Status,
                      LONG_PTR(hVidPnTopology),
                      pCommitVidPn->AffectedVidPnSourceId,
                      PathIndex));
            goto CommitVidPnExit;
        }

        Status = pVidPnTopologyInterface->pfnAcquirePathInfo(hVidPnTopology,
                                                             pCommitVidPn->AffectedVidPnSourceId,
                                                             TargetId,
                                                             &pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnAcquirePathInfo failed with Status = 0x%X, hVidPnTopology = 0x%llu, SourceId = 0x%I64x, "
                      "TargetId = 0x%I64x\n",
                      Status,
                      LONG_PTR(hVidPnTopology),
                      pCommitVidPn->AffectedVidPnSourceId,
                      TargetId));
            goto CommitVidPnExit;
        }

        Status = IsVidPnPathFieldsValid(pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            goto CommitVidPnExit;
        }

        Status = SetSourceModeAndPath(pPinnedVidPnSourceModeInfo, pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            goto CommitVidPnExit;
        }

        Status = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnReleasePathInfo failed with Status = 0x%X, hVidPnTopoogy = 0x%llu, pVidPnPresentPath = %p\n",
                      Status,
                      LONG_PTR(hVidPnTopology),
                      pVidPnPresentPath));
            goto CommitVidPnExit;
        }
        pVidPnPresentPath = NULL;
    }

CommitVidPnExit:

    NTSTATUS TempStatus = STATUS_SUCCESS;

    if ((pVidPnSourceModeSetInterface != NULL) && (hVidPnSourceModeSet != 0) && (pPinnedVidPnSourceModeInfo != NULL))
    {
        TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pPinnedVidPnSourceModeInfo);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }

    if ((pVidPnInterface != NULL) && (pCommitVidPn->hFunctionalVidPn != 0) && (hVidPnSourceModeSet != 0))
    {
        TempStatus = pVidPnInterface->pfnReleaseSourceModeSet(pCommitVidPn->hFunctionalVidPn, hVidPnSourceModeSet);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }

    if ((pVidPnTopologyInterface != NULL) && (hVidPnTopology != 0) && (pVidPnPresentPath != NULL))
    {
        TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPath);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return Status;
}

NTSTATUS VioGpuDod::SetSourceModeAndPath(CONST D3DKMDT_VIDPN_SOURCE_MODE *pSourceMode,
                                         CONST D3DKMDT_VIDPN_PRESENT_PATH *pPath)
{
    PAGED_CODE();
    VIOGPU_ASSERT(pPath->VidPnSourceId < MAX_VIEWS);

    NTSTATUS Status = STATUS_SUCCESS;

    CURRENT_MODE *pCurrentMode = &m_CurrentMode;
    DbgPrint(TRACE_LEVEL_FATAL,
             ("---> %s (%dx%d)\n",
              __FUNCTION__,
              pSourceMode->Format.Graphics.VisibleRegionSize.cx,
              pSourceMode->Format.Graphics.VisibleRegionSize.cy));
    pCurrentMode->Scaling = pPath->ContentTransformation.Scaling;
    pCurrentMode->SrcModeWidth = pSourceMode->Format.Graphics.VisibleRegionSize.cx;
    pCurrentMode->SrcModeHeight = pSourceMode->Format.Graphics.VisibleRegionSize.cy;
    pCurrentMode->Rotation = pPath->ContentTransformation.Rotation;

    pCurrentMode->DispInfo.Width = pSourceMode->Format.Graphics.PrimSurfSize.cx;
    pCurrentMode->DispInfo.Height = pSourceMode->Format.Graphics.PrimSurfSize.cy;
    pCurrentMode->DispInfo.Pitch = pSourceMode->Format.Graphics.PrimSurfSize.cx *
                                   BPPFromPixelFormat(pCurrentMode->DispInfo.ColorFormat) / BITS_PER_BYTE;

    if (NT_SUCCESS(Status))
    {
        pCurrentMode->Flags.FullscreenPresent = TRUE;
        for (USHORT ModeIndex = 0; ModeIndex < m_pHWDevice->GetModeCount(); ++ModeIndex)
        {
            PVIDEO_MODE_INFORMATION pModeInfo = m_pHWDevice->GetModeInfo(ModeIndex);
            if (pCurrentMode->DispInfo.Width == pModeInfo->VisScreenWidth &&
                pCurrentMode->DispInfo.Height == pModeInfo->VisScreenHeight)
            {
                Status = m_pHWDevice->SetCurrentMode(m_pHWDevice->GetModeNumber(ModeIndex), pCurrentMode);
                if (NT_SUCCESS(Status))
                {
                    m_pHWDevice->SetCurrentModeIndex(ModeIndex);
                }
                break;
            }
        }
    }

    return Status;
}

NTSTATUS VioGpuDod::IsVidPnPathFieldsValid(CONST D3DKMDT_VIDPN_PRESENT_PATH *pPath) const
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (pPath->VidPnSourceId >= MAX_VIEWS)
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("VidPnSourceId is 0x%I64x is too high (MAX_VIEWS is 0x%I64x)", pPath->VidPnSourceId, MAX_VIEWS));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }
    else if (pPath->VidPnTargetId >= MAX_CHILDREN)
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("VidPnTargetId is 0x%I64x is too high (MAX_CHILDREN is 0x%I64x)",
                  pPath->VidPnTargetId,
                  MAX_CHILDREN));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }
    else if (pPath->GammaRamp.Type != D3DDDI_GAMMARAMP_DEFAULT)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPath contains a gamma ramp (0x%I64x)", pPath->GammaRamp.Type));
        return STATUS_GRAPHICS_GAMMA_RAMP_NOT_SUPPORTED;
    }
    else if ((pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY) &&
             (pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_CENTERED) &&
             (pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_NOTSPECIFIED) &&
             (pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pPath contains a non-identity scaling (0x%I64x)", pPath->ContentTransformation.Scaling));
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    else if ((pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY) &&
             (pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_ROTATE90) &&
             (pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_NOTSPECIFIED) &&
             (pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pPath contains a not-supported rotation (0x%I64x)", pPath->ContentTransformation.Rotation));
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    else if ((pPath->VidPnTargetColorBasis != D3DKMDT_CB_SCRGB) &&
             (pPath->VidPnTargetColorBasis != D3DKMDT_CB_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPath has a non-linear RGB color basis (0x%I64x)", pPath->VidPnTargetColorBasis));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::IsVidPnSourceModeFieldsValid(CONST D3DKMDT_VIDPN_SOURCE_MODE *pSourceMode) const
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (pSourceMode->Type != D3DKMDT_RMT_GRAPHICS)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pSourceMode is a non-graphics mode (0x%I64x)", pSourceMode->Type));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }
    else if ((pSourceMode->Format.Graphics.ColorBasis != D3DKMDT_CB_SCRGB) &&
             (pSourceMode->Format.Graphics.ColorBasis != D3DKMDT_CB_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pSourceMode has a non-linear RGB color basis (0x%I64x)", pSourceMode->Format.Graphics.ColorBasis));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }
    else if (pSourceMode->Format.Graphics.PixelValueAccessMode != D3DKMDT_PVAM_DIRECT)
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pSourceMode has a palettized access mode (0x%I64x)",
                  pSourceMode->Format.Graphics.PixelValueAccessMode));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }
    else
    {
        if (pSourceMode->Format.Graphics.PixelFormat == D3DDDIFMT_A8R8G8B8)
        {
            return STATUS_SUCCESS;
        }
    }

    DbgPrint(TRACE_LEVEL_ERROR,
             ("pSourceMode has an unknown pixel format (0x%I64x)", pSourceMode->Format.Graphics.PixelFormat));

    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
}

NTSTATUS
VioGpuDod::UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *CONST pUpdateActiveVidPnPresentPath)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pUpdateActiveVidPnPresentPath != NULL);
    VIOGPU_ASSERT(pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnSourceId == 0);

    NTSTATUS Status = IsVidPnPathFieldsValid(&(pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    m_CurrentMode.Flags.FullscreenPresent = TRUE;

    m_CurrentMode.Rotation = pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.ContentTransformation.Rotation;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG_END

//
// Non-Paged Code
//
#pragma code_seg(push)
#pragma code_seg()

VOID VioGpuDod::DpcRoutine(VOID)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    m_pHWDevice->DpcRoutine(&m_DxgkInterface);
    m_DxgkInterface.DxgkCbNotifyDpc((HANDLE)m_DxgkInterface.DeviceHandle);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuDod::InterruptRoutine(_In_ ULONG MessageNumber)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s\n", __FUNCTION__));
    if (IsHardwareInit())
    {
        return m_pHWDevice ? m_pHWDevice->InterruptRoutine(&m_DxgkInterface, MessageNumber) : FALSE;
    }
    return FALSE;
}

VOID VioGpuDod::ResetDevice(VOID)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));
    m_pHWDevice->ResetDevice();
}

NTSTATUS VioGpuDod::SystemDisplayEnable(_In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                        _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                                        _Out_ UINT *pWidth,
                                        _Out_ UINT *pHeight,
                                        _Out_ D3DDDIFORMAT *pColorFormat)
{
    UNREFERENCED_PARAMETER(Flags);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT((TargetId < MAX_CHILDREN) || (TargetId == D3DDDI_ID_UNINITIALIZED));

    if (!IsVgaDevice())
    {
        return STATUS_UNSUCCESSFUL;
    }

    m_CurrentMode.Flags.FrameBufferIsActive = FALSE;
    m_pHWDevice->ResetToVgaMode();
    m_CurrentMode.FrameBuffer = m_pHWDevice->GetPciResources()[0].GetMappedAddress(0, 0);

    if (m_CurrentMode.FrameBuffer == nullptr)
    {
        return STATUS_UNSUCCESSFUL;
    }

    m_CurrentMode.Flags.FrameBufferIsActive = TRUE;
    m_CurrentMode.Rotation = D3DKMDT_VPPR_IDENTITY;
    *pWidth = m_CurrentMode.DispInfo.Width = m_SystemDisplayInfo.Width;
    *pHeight = m_CurrentMode.DispInfo.Height = m_SystemDisplayInfo.Height;
    *pColorFormat = m_CurrentMode.DispInfo.ColorFormat = m_SystemDisplayInfo.ColorFormat;
    m_CurrentMode.DispInfo.Pitch = m_SystemDisplayInfo.Pitch = (BPPFromPixelFormat(m_SystemDisplayInfo.ColorFormat) /
                                                                BITS_PER_BYTE) *
                                                               m_SystemDisplayInfo.Width;

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s (%dx%dx%d)\n", __FUNCTION__, *pWidth, *pHeight, *pColorFormat));

    return STATUS_SUCCESS;
}

VOID VioGpuDod::SystemDisplayWrite(_In_reads_bytes_(SourceHeight *SourceStride) VOID *pSource,
                                   _In_ UINT SourceWidth,
                                   _In_ UINT SourceHeight,
                                   _In_ UINT SourceStride,
                                   _In_ INT PositionX,
                                   _In_ INT PositionY)
{
    if (m_CurrentMode.Flags.FrameBufferIsActive)
    {
        RECT Rect = {0};
        BLT_INFO SrcBltInfo = {0};
        BLT_INFO DstBltInfo = {0};

        Rect.left = PositionX;
        Rect.top = PositionY;
        Rect.right = Rect.left + SourceWidth;
        Rect.bottom = Rect.top + SourceHeight;

        DstBltInfo.pBits = m_CurrentMode.FrameBuffer;
        DstBltInfo.Pitch = m_CurrentMode.DispInfo.Pitch;
        DstBltInfo.BitsPerPel = BPPFromPixelFormat(m_CurrentMode.DispInfo.ColorFormat);
        DstBltInfo.Offset.x = 0;
        DstBltInfo.Offset.y = 0;
        DstBltInfo.Rotation = m_CurrentMode.Rotation;
        DstBltInfo.Width = m_CurrentMode.DispInfo.Width;
        DstBltInfo.Height = m_CurrentMode.DispInfo.Height;

        SrcBltInfo.pBits = pSource;
        SrcBltInfo.Pitch = SourceStride;
        SrcBltInfo.BitsPerPel = BPPFromPixelFormat(D3DDDIFMT_A8R8G8B8);
        SrcBltInfo.Offset.x = -PositionX;
        SrcBltInfo.Offset.y = -PositionY;
        SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
        SrcBltInfo.Width = SourceWidth;
        SrcBltInfo.Height = SourceHeight;

        BltBits(&DstBltInfo, &SrcBltInfo, &Rect);
    }
}

#pragma code_seg(pop) // End Non-Paged Code

PAGED_CODE_SEG_BEGIN
NTSTATUS VioGpuDod::WriteRegistryString(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    ANSI_STRING AnsiStrValue;
    UNICODE_STRING UnicodeStrValue;
    UNICODE_STRING UnicodeStrValueName;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    RtlInitUnicodeString(&UnicodeStrValueName, pszwValueName);

    RtlInitAnsiString(&AnsiStrValue, pszValue);
    Status = RtlAnsiStringToUnicodeString(&UnicodeStrValue, &AnsiStrValue, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("RtlAnsiStringToUnicodeString failed with Status: 0x%X\n", Status));
        return Status;
    }

    Status = ZwSetValueKey(DevInstRegKeyHandle,
                           &UnicodeStrValueName,
                           0,
                           REG_SZ,
                           UnicodeStrValue.Buffer,
                           UnicodeStrValue.MaximumLength);

    RtlFreeUnicodeString(&UnicodeStrValue);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("ZwSetValueKey failed with Status: 0x%X\n", Status));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuDod::WriteRegistryDWORD(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PDWORD pdwValue)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    UNICODE_STRING UnicodeStrValueName;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    RtlInitUnicodeString(&UnicodeStrValueName, pszwValueName);

    Status = ZwSetValueKey(DevInstRegKeyHandle, &UnicodeStrValueName, 0, REG_DWORD, pdwValue, sizeof(DWORD));

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("ZwSetValueKey failed with Status: 0x%X\n", Status));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuDod::ReadRegistryDWORD(_In_ HANDLE DevInstRegKeyHandle,
                                      _In_ PCWSTR pszwValueName,
                                      _Inout_ PDWORD pdwValue)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    UNICODE_STRING UnicodeStrValueName;
    ULONG ulRes;
    UCHAR Buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(DWORD)];
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    RtlInitUnicodeString(&UnicodeStrValueName, pszwValueName);

    Status = ZwQueryValueKey(DevInstRegKeyHandle,
                             &UnicodeStrValueName,
                             KeyValuePartialInformation,
                             Buf,
                             sizeof(Buf),
                             &ulRes);

    if (Status == STATUS_SUCCESS)
    {
        if (((PKEY_VALUE_PARTIAL_INFORMATION)Buf)->Type == REG_DWORD &&
            (((PKEY_VALUE_PARTIAL_INFORMATION)Buf)->DataLength == sizeof(DWORD)))
        {
            *pdwValue = *((PDWORD) & (((PKEY_VALUE_PARTIAL_INFORMATION)Buf)->Data));
        }
        else
        {
            Status = STATUS_INVALID_PARAMETER;
            VioGpuDbgBreak();
        }
    }

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("ZwQueryValueKey failed with Status: 0x%X\n", Status));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuDod::SetRegisterInfo(_In_ ULONG Id, _In_ DWORD MemSize)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PCSTR StrHWInfoChipType = "QEMU VIRTIO GPU";
    PCSTR StrHWInfoDacType = "VIRTIO GPU";
    PCSTR StrHWInfoAdapterString = "VIRTIO GPU";
    PCSTR StrHWInfoBiosString = "SEABIOS VIRTIO GPU";

    HANDLE DevInstRegKeyHandle;
    Status = IoOpenDeviceRegistryKey(m_pPhysicalDevice, PLUGPLAY_REGKEY_DRIVER, KEY_SET_VALUE, &DevInstRegKeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("IoOpenDeviceRegistryKey failed for PDO: 0x%p, Status: 0x%X", m_pPhysicalDevice, Status));
        return Status;
    }

    do
    {
        Status = WriteRegistryString(DevInstRegKeyHandle, L"HardwareInformation.ChipType", StrHWInfoChipType);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("WriteRegistryString failed for ChipType with Status: 0x%X", Status));
            break;
        }

        Status = WriteRegistryString(DevInstRegKeyHandle, L"HardwareInformation.DacType", StrHWInfoDacType);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("WriteRegistryString failed DacType with Status: 0x%X", Status));
            break;
        }

        Status = WriteRegistryString(DevInstRegKeyHandle, L"HardwareInformation.AdapterString", StrHWInfoAdapterString);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("WriteRegistryString failed for AdapterString with Status: 0x%X", Status));
            break;
        }

        Status = WriteRegistryString(DevInstRegKeyHandle, L"HardwareInformation.BiosString", StrHWInfoBiosString);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("WriteRegistryString failed for BiosString with Status: 0x%X", Status));
            break;
        }

        DWORD MemorySize = MemSize;
        Status = WriteRegistryDWORD(DevInstRegKeyHandle, L"HardwareInformation.MemorySize", &MemorySize);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("WriteRegistryDWORD failed for MemorySize with Status: 0x%X", Status));
            break;
        }

        DWORD DeviceId = Id;
        Status = WriteRegistryDWORD(DevInstRegKeyHandle, L"VioGpuAdapterID", &DeviceId);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("WriteRegistryDWORD failed for VioGpuAdapterID with Status: 0x%X", Status));
        }
    } while (0);

    ZwClose(DevInstRegKeyHandle);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuDod::GetRegisterInfo(void)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    HANDLE DevInstRegKeyHandle;
    Status = IoOpenDeviceRegistryKey(m_pPhysicalDevice, PLUGPLAY_REGKEY_DRIVER, KEY_READ, &DevInstRegKeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("IoOpenDeviceRegistryKey failed for PDO: 0x%p, Status: 0x%X", m_pPhysicalDevice, Status));
        return Status;
    }

    DWORD value = 0;
    Status = ReadRegistryDWORD(DevInstRegKeyHandle, L"HWCursor", &value);
    if (NT_SUCCESS(Status))
    {
        SetPointerEnabled(!!value);
    }

    value = 0;
    Status = ReadRegistryDWORD(DevInstRegKeyHandle, L"FlexResolution", &value);
    if (NT_SUCCESS(Status))
    {
        SetFlexResolution(!!value);
    }

    value = 0;
    Status = ReadRegistryDWORD(DevInstRegKeyHandle, L"UsePhysicalMemory", &value);
    if (NT_SUCCESS(Status))
    {
        SetUsePhysicalMemory(!!value);
    }

    value = 0;
    Status = ReadRegistryDWORD(DevInstRegKeyHandle, L"UsePresentProgress", &value);
    if (NT_SUCCESS(Status))
    {
        SetUsePresentProgress(!!value);
    }

    ZwClose(DevInstRegKeyHandle);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

VioGpuAdapter::VioGpuAdapter(_In_ VioGpuDod *pVioGpuDod)
{
    PAGED_CODE();
    RtlZeroMemory(&m_VioDev, sizeof(m_VioDev));
    m_pVioGpuDod = pVioGpuDod;
    m_CurrentModeIndex = 0;
    m_CustomModeIndex = 0;
    RtlZeroMemory(m_EDIDs, sizeof(m_EDIDs));
    m_bEDID = FALSE;
    m_ModeInfo = NULL;
    m_ModeCount = 0;
    m_Id = g_InstanceId++;
    m_pFrameBuf = NULL;
    m_pCursorBuf = NULL;
    m_PendingWorks = 0;
    m_bStopWorkThread = FALSE;
    m_pWorkThread = NULL;
    m_ResolutionEvent = NULL;
    m_ResolutionEventHandle = NULL;
    m_u32NumCapsets = 0;
    m_u32NumScanouts = 0;

    KeInitializeEvent(&m_ConfigUpdateEvent, SynchronizationEvent, FALSE);
}

VioGpuAdapter::~VioGpuAdapter(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s 0x%p\n", __FUNCTION__, this));
    CloseResolutionEvent();
    DestroyCursor();
    DestroyFrameBufferObj(TRUE);
    VioGpuAdapterClose();
    HWClose();
    delete[] m_ModeInfo;
    m_ModeInfo = NULL;
    m_CurrentModeIndex = 0;
    m_ModeCount = 0;
    m_Id = 0;
    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS VioGpuAdapter::SetCurrentMode(ULONG Mode, CURRENT_MODE *pCurrentMode)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_ERROR, ("---> %s - %d: Mode = %d\n", __FUNCTION__, m_Id, Mode));
    for (ULONG idx = 0; idx < GetModeCount(); idx++)
    {
        if (Mode == m_ModeInfo[idx].ModeIndex /*m_ModeNumbers[idx]*/)
        {
            if (pCurrentMode->Flags.FrameBufferIsActive)
            {
                DestroyFrameBufferObj(FALSE);
                pCurrentMode->Flags.FrameBufferIsActive = FALSE;
            }
            if (CreateFrameBufferObj(&m_ModeInfo[idx], pCurrentMode))
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("%s device %d: setting current mode %d (%d x %d)\n",
                          __FUNCTION__,
                          m_Id,
                          Mode,
                          m_ModeInfo[idx].VisScreenWidth,
                          m_ModeInfo[idx].VisScreenHeight));
                return STATUS_SUCCESS;
            }
        }
    }
    DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s failed\n", __FUNCTION__));
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS VioGpuAdapter::VioGpuAdapterInit(DXGK_DISPLAY_INFORMATION *pDispInfo)
{
    PAGED_CODE();
    NTSTATUS status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    UNREFERENCED_PARAMETER(pDispInfo);
    if (m_pVioGpuDod->IsHardwareInit())
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("Already Initialized\n"));
        VioGpuDbgBreak();
        return status;
    }
    status = VirtIoDeviceInit();
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize virtio device, error %x\n", status));
        VioGpuDbgBreak();
        return status;
    }

    m_u64HostFeatures = virtio_get_features(&m_VioDev);
    m_u64GuestFeatures = 0;
    do
    {
        struct virtqueue *vqs[2];
        if (!AckFeature(VIRTIO_F_VERSION_1))
        {
            status = STATUS_UNSUCCESSFUL;
            break;
        }

        AckFeature(VIRTIO_F_ACCESS_PLATFORM);

        status = virtio_set_features(&m_VioDev, m_u64GuestFeatures);
        if (!NT_SUCCESS(status))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s virtio_set_features failed with %x\n", __FUNCTION__, status));
            VioGpuDbgBreak();
            break;
        }

        status = virtio_find_queues(&m_VioDev, 2, vqs);
        if (!NT_SUCCESS(status))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("virtio_find_queues failed with error %x\n", status));
            VioGpuDbgBreak();
            break;
        }

        if (!m_CtrlQueue.Init(&m_VioDev, vqs[0], 0) || !m_CursorQueue.Init(&m_VioDev, vqs[1], 1))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize virtio queues\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }

        virtio_get_config(&m_VioDev,
                          FIELD_OFFSET(GPU_CONFIG, num_scanouts),
                          &m_u32NumScanouts,
                          sizeof(m_u32NumScanouts));

        virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, num_capsets), &m_u32NumCapsets, sizeof(m_u32NumCapsets));
    } while (0);
    if (status == STATUS_SUCCESS)
    {
        virtio_device_ready(&m_VioDev);
        m_pVioGpuDod->SetHardwareInit(TRUE);
    }
    else
    {
        virtio_add_status(&m_VioDev, VIRTIO_CONFIG_S_FAILED);
        VioGpuDbgBreak();
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return status;
}

NTSTATUS VioGpuAdapter::SetPowerState(DXGK_DEVICE_INFO *pDeviceInfo,
                                      DEVICE_POWER_STATE DevicePowerState,
                                      CURRENT_MODE *pCurrentMode)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s DevicePowerState = %d\n", __FUNCTION__, DevicePowerState));
    UNREFERENCED_PARAMETER(pDeviceInfo);

    switch (DevicePowerState)
    {
        case PowerDeviceUnspecified:
        case PowerDeviceD0:
            {
                VioGpuAdapterInit(&pCurrentMode->DispInfo);
            }
            break;
        case PowerDeviceD1:
        case PowerDeviceD2:
        case PowerDeviceD3:
            {
                DestroyFrameBufferObj(TRUE);
                VioGpuAdapterClose();
                pCurrentMode->Flags.FrameBufferIsActive = FALSE;
                pCurrentMode->FrameBuffer = NULL;
            }
            break;
    }
    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

BOOLEAN VioGpuAdapter::AckFeature(UINT64 Feature)
{
    PAGED_CODE();

    if (virtio_is_feature_enabled(m_u64HostFeatures, Feature))
    {
        virtio_feature_enable(m_u64GuestFeatures, Feature);
        return TRUE;
    }
    return FALSE;
}

NTSTATUS VioGpuAdapter::VirtIoDeviceInit()
{
    PAGED_CODE();

    return virtio_device_initialize(&m_VioDev,
                                    &VioGpuSystemOps,
                                    reinterpret_cast<IVioGpuPCI *>(this),
                                    m_PciResources.IsMSIEnabled());
}

PBYTE VioGpuAdapter::GetEdidData()
{
    PAGED_CODE();

    return m_bEDID ? m_EDIDs : (PBYTE)(g_gpu_edid);
}

PBYTE VioGpuAdapter::GetCTA861Data(void)
{
    PAGED_CODE();

    if (m_bEDID)
    {
        PEDID_DATA_V1 edid_data = (PEDID_DATA_V1)m_EDIDs;
        if (edid_data->ExtensionFlag)
        {
            PEDID_CTA_861 cta_data = (PEDID_CTA_861)(m_EDIDs + EDID_V1_BLOCK_SIZE);
            if (cta_data->ExtentionTag[0] >= 2 && cta_data->Revision[0] >= 3)
            {
                return (PBYTE)cta_data;
            }
        }
    }
    return NULL;
}

VOID VioGpuAdapter::CreateResolutionEvent(VOID)
{
    PAGED_CODE();

    if (m_ResolutionEvent != NULL && m_ResolutionEventHandle != NULL)
    {
        return;
    }
    DECLARE_UNICODE_STRING_SIZE(DeviceNumber, 10);
    DECLARE_UNICODE_STRING_SIZE(EventName, 256);

    RtlIntegerToUnicodeString(m_Id, 10, &DeviceNumber);
    NTSTATUS status = RtlUnicodeStringPrintf(&EventName,
                                             L"%ws%ws%ws",
                                             BASE_NAMED_OBJECTS,
                                             RESOLUTION_EVENT_NAME,
                                             DeviceNumber.Buffer);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("RtlUnicodeStringPrintf failed 0x%x\n", status));
        return;
    }
    m_ResolutionEvent = IoCreateNotificationEvent(&EventName, &m_ResolutionEventHandle);
    if (m_ResolutionEvent == NULL)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--> %s\n", __FUNCTION__));
        return;
    }
    KeClearEvent(m_ResolutionEvent);
    ObReferenceObject(m_ResolutionEvent);
}

VOID VioGpuAdapter::NotifyResolutionEvent(VOID)
{
    PAGED_CODE();

    if (m_ResolutionEvent != NULL)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("NotifyResolutionEvent\n"));
        KeSetEvent(m_ResolutionEvent, IO_NO_INCREMENT, FALSE);
        KeClearEvent(m_ResolutionEvent);
    }
}

VOID VioGpuAdapter::CloseResolutionEvent(VOID)
{
    PAGED_CODE();

    if (m_ResolutionEventHandle != NULL)
    {
        ZwClose(m_ResolutionEventHandle);
        m_ResolutionEventHandle = NULL;
    }

    if (m_ResolutionEvent != NULL)
    {
        ObDereferenceObject(m_ResolutionEvent);
        m_ResolutionEvent = NULL;
    }
}

NTSTATUS VioGpuAdapter::HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION *pDispInfo)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;
    HANDLE threadHandle = 0;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    UINT size = 0;
    do
    {
        if (!m_PciResources.Init(GetVioGpu()->GetDxgkInterface(), pResList))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("Incomplete resources\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }

        status = VioGpuAdapterInit(pDispInfo);
        if (!NT_SUCCESS(status))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s Failed initialize adapter %x\n", __FUNCTION__, status));
            VioGpuDbgBreak();
            break;
        }

        size = m_CtrlQueue.QueryAllocation() + m_CursorQueue.QueryAllocation();
        DbgPrint(TRACE_LEVEL_FATAL, ("%s size %d\n", __FUNCTION__, size));
        ASSERT(size);

        if (!m_GpuBuf.Init(size))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize buffers\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }

        m_CtrlQueue.SetGpuBuf(&m_GpuBuf);
        m_CursorQueue.SetGpuBuf(&m_GpuBuf);

        if (!m_Idr.Init(1))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize id generator\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }

    } while (0);
    // FIXME!!! exit if the block above failed

    status = PsCreateSystemThread(&threadHandle,
                                  (ACCESS_MASK)0,
                                  NULL,
                                  (HANDLE)0,
                                  NULL,
                                  VioGpuAdapter::ThreadWork,
                                  this);

    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s failed to create system thread, status %x\n", __FUNCTION__, status));
        VioGpuDbgBreak();
        return status;
    }
    ObReferenceObjectByHandle(threadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID *)(&m_pWorkThread), NULL);

    ZwClose(threadHandle);

    status = BuildModeList(pDispInfo);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s GetModeList failed with %x\n", __FUNCTION__, status));
        VioGpuDbgBreak();
    }

    PHYSICAL_ADDRESS fb_pa = m_PciResources.GetPciBar(0)->GetPA();
    UINT fb_size = (UINT)m_PciResources.GetPciBar(0)->GetSize();
    UINT req_size = pDispInfo->Pitch * pDispInfo->Height;
    req_size = max(req_size, 0x1000000);

    UINT max_res_size = MIN_WIDTH_SIZE * MIN_HEIGHT_SIZE;
    for (UINT idx = 0; idx < m_ModeCount; idx++)
    {
        UINT res_size = m_ModeInfo[idx].VisScreenWidth * m_ModeInfo[idx].VisScreenHeight;
        max_res_size = max(max_res_size, res_size);
    }

    req_size = max(req_size, (max_res_size * 4));

    if (fb_pa.QuadPart != 0LL)
    {
        pDispInfo->PhysicAddress = fb_pa;
    }

    if (fb_size < req_size)
    {
        m_pVioGpuDod->SetUsePhysicalMemory(FALSE);
    }

    if (!m_pVioGpuDod->IsUsePhysicalMemory() || fb_pa.QuadPart == 0 || fb_size < req_size)
    {
        fb_pa.QuadPart = 0LL;
        fb_size = max(req_size, fb_size);
    }

    if (!m_FrameSegment.Init(fb_size, &fb_pa))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s failed to allocate FB memory segment\n", __FUNCTION__));
        status = STATUS_INSUFFICIENT_RESOURCES;
        VioGpuDbgBreak();
        return status;
    }

    if (!m_CursorSegment.Init(POINTER_SIZE * POINTER_SIZE * 4, NULL))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s failed to allocate Cursor memory segment\n", __FUNCTION__));
        status = STATUS_INSUFFICIENT_RESOURCES;
        VioGpuDbgBreak();
        return status;
    }

    return status;
}

NTSTATUS VioGpuAdapter::HWClose(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    m_pVioGpuDod->SetHardwareInit(FALSE);

    LARGE_INTEGER timeout = {0};
    timeout.QuadPart = Int32x32To64(1000, -10000);

    m_bStopWorkThread = TRUE;
    KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);

    if (KeWaitForSingleObject(m_pWorkThread, Executive, KernelMode, FALSE, &timeout) == STATUS_TIMEOUT)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("---> Failed to exit the worker thread\n"));
        VioGpuDbgBreak();
    }

    ObDereferenceObject(m_pWorkThread);

    m_FrameSegment.Close();
    m_CursorSegment.Close();

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

BOOLEAN FindUpdateRect(_In_ ULONG NumMoves,
                       _In_ D3DKMT_MOVE_RECT *pMoves,
                       _In_ ULONG NumDirtyRects,
                       _In_ PRECT pDirtyRect,
                       _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
                       _Out_ PRECT pUpdateRect)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Rotation);
    BOOLEAN updated = FALSE;

    if (pUpdateRect == NULL)
    {
        return FALSE;
    }

    if (NumMoves == 0 && NumDirtyRects == 0)
    {
        pUpdateRect->bottom = 0;
        pUpdateRect->left = 0;
        pUpdateRect->right = 0;
        pUpdateRect->top = 0;
    }

    for (ULONG i = 0; i < NumMoves; i++)
    {
        PRECT pRect = &pMoves[i].DestRect;
        if (!updated)
        {
            *pUpdateRect = *pRect;
            updated = TRUE;
        }
        else
        {
            pUpdateRect->bottom = max(pRect->bottom, pUpdateRect->bottom);
            pUpdateRect->left = min(pRect->left, pUpdateRect->left);
            pUpdateRect->right = max(pRect->right, pUpdateRect->right);
            pUpdateRect->top = min(pRect->top, pUpdateRect->top);
        }
    }
    for (ULONG i = 0; i < NumDirtyRects; i++)
    {
        PRECT pRect = &pDirtyRect[i];
        if (!updated)
        {
            *pUpdateRect = *pRect;
            updated = TRUE;
        }
        else
        {
            pUpdateRect->bottom = max(pRect->bottom, pUpdateRect->bottom);
            pUpdateRect->left = min(pRect->left, pUpdateRect->left);
            pUpdateRect->right = max(pRect->right, pUpdateRect->right);
            pUpdateRect->top = min(pRect->top, pUpdateRect->top);
        }
    }
    if (Rotation == D3DKMDT_VPPR_ROTATE90 || Rotation == D3DKMDT_VPPR_ROTATE270)
    {
    }
    return updated;
}

NTSTATUS VioGpuAdapter::ExecutePresentDisplayOnly(_In_ BYTE *DstAddr,
                                                  _In_ UINT DstBitPerPixel,
                                                  _In_ BYTE *SrcAddr,
                                                  _In_ UINT SrcBytesPerPixel,
                                                  _In_ LONG SrcPitch,
                                                  _In_ ULONG NumMoves,
                                                  _In_ D3DKMT_MOVE_RECT *pMoves,
                                                  _In_ ULONG NumDirtyRects,
                                                  _In_ RECT *pDirtyRect,
                                                  _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
                                                  _In_ const CURRENT_MODE *pModeCur)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    BLT_INFO SrcBltInfo = {0};
    BLT_INFO DstBltInfo = {0};
    UINT resid = 0;
    RECT updrect = {0};
    ULONG offset = 0UL;

    DbgPrint(TRACE_LEVEL_VERBOSE,
             ("SrcBytesPerPixel = %d DstBitPerPixel = %d (%dx%d)\n",
              SrcBytesPerPixel,
              DstBitPerPixel,
              pModeCur->SrcModeWidth,
              pModeCur->SrcModeHeight));

    DstBltInfo.pBits = DstAddr;
    DstBltInfo.Pitch = pModeCur->DispInfo.Pitch;
    DstBltInfo.BitsPerPel = DstBitPerPixel;
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = Rotation;
    DstBltInfo.Width = pModeCur->SrcModeWidth;
    DstBltInfo.Height = pModeCur->SrcModeHeight;

    SrcBltInfo.pBits = SrcAddr;
    SrcBltInfo.Pitch = SrcPitch;
    SrcBltInfo.BitsPerPel = SrcBytesPerPixel * BITS_PER_BYTE;
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    if (Rotation == D3DKMDT_VPPR_ROTATE90 || Rotation == D3DKMDT_VPPR_ROTATE270)
    {
        SrcBltInfo.Width = DstBltInfo.Height;
        SrcBltInfo.Height = DstBltInfo.Width;
    }
    else
    {
        SrcBltInfo.Width = DstBltInfo.Width;
        SrcBltInfo.Height = DstBltInfo.Height;
    }

    for (UINT i = 0; i < NumMoves; i++)
    {
        RECT *pDestRect = &pMoves[i].DestRect;
        BltBits(&DstBltInfo, &SrcBltInfo, pDestRect);
    }

    for (UINT i = 0; i < NumDirtyRects; i++)
    {
        RECT *pRect = &pDirtyRect[i];
        BltBits(&DstBltInfo, &SrcBltInfo, pRect);
    }
    if (!FindUpdateRect(NumMoves, pMoves, NumDirtyRects, pDirtyRect, Rotation, &updrect))
    {
        updrect.top = 0;
        updrect.left = 0;
        updrect.bottom = pModeCur->SrcModeHeight;
        updrect.right = pModeCur->SrcModeWidth;
        offset = 0UL;
    }
    // FIXME!!! rotation
    offset = (updrect.top * pModeCur->DispInfo.Pitch) +
             (updrect.left * ((DstBitPerPixel + BITS_PER_BYTE - 1) / BITS_PER_BYTE));

    resid = m_pFrameBuf->GetId();
    DbgPrint(TRACE_LEVEL_VERBOSE,
             ("offset = %lu (XxYxWxH) (%dx%dx%dx%d) vs (%dx%dx%dx%d)\n",
              offset,
              updrect.left,
              updrect.top,
              updrect.right - updrect.left,
              updrect.bottom - updrect.top,
              0,
              0,
              pModeCur->SrcModeWidth,
              pModeCur->SrcModeHeight));

    m_CtrlQueue.TransferToHost2D(resid,
                                 offset,
                                 updrect.right - updrect.left,
                                 updrect.bottom - updrect.top,
                                 updrect.left,
                                 updrect.top);
    m_CtrlQueue.ResFlush(resid, updrect.right - updrect.left, updrect.bottom - updrect.top, updrect.left, updrect.top);

    return STATUS_SUCCESS;
}

VOID VioGpuAdapter::BlackOutScreen(CURRENT_MODE *pCurrentMod)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));

    if (pCurrentMod->Flags.FrameBufferIsActive)
    {
        UINT ScreenHeight = pCurrentMod->DispInfo.Height;
        UINT ScreenPitch = pCurrentMod->DispInfo.Pitch;
        BYTE *pDst = (BYTE *)pCurrentMod->FrameBuffer;

        UINT resid = 0;

        if (pDst)
        {
            RtlZeroMemory(pDst, (ULONGLONG)ScreenHeight * ScreenPitch);
        }

        // FIXME!!! rotation

        resid = m_pFrameBuf->GetId();

        m_CtrlQueue.TransferToHost2D(resid, 0UL, pCurrentMod->DispInfo.Width, pCurrentMod->DispInfo.Height, 0, 0);
        m_CtrlQueue.ResFlush(resid, pCurrentMod->DispInfo.Width, pCurrentMod->DispInfo.Height, 0, 0);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS VioGpuAdapter::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape,
                                        _In_ CONST CURRENT_MODE *pModeCur)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("<--> %s flag = %d pitch = %d, pixels = %p, id = %d, w = %d, h = %d, x = %d, y = %d\n",
              __FUNCTION__,
              pSetPointerShape->Flags.Value,
              pSetPointerShape->Pitch,
              pSetPointerShape->pPixels,
              pSetPointerShape->VidPnSourceId,
              pSetPointerShape->Width,
              pSetPointerShape->Height,
              pSetPointerShape->XHot,
              pSetPointerShape->YHot));

    if (pSetPointerShape->Flags.Monochrome)
    {
        VioGpuDbgBreak();
        return STATUS_UNSUCCESSFUL;
    }

    if (UpdateCursor(pSetPointerShape, pModeCur))
    {
        PGPU_UPDATE_CURSOR crsr;
        PGPU_VBUFFER vbuf;
        UINT ret = 0;
        crsr = (PGPU_UPDATE_CURSOR)m_CursorQueue.AllocCursor(&vbuf);
        RtlZeroMemory(crsr, sizeof(*crsr));

        crsr->hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
        crsr->resource_id = m_pCursorBuf->GetId();
        crsr->pos.x = 0;
        crsr->pos.y = 0;
        crsr->hot_x = pSetPointerShape->XHot;
        crsr->hot_y = pSetPointerShape->YHot;
        ret = m_CursorQueue.QueueCursor(vbuf);
        DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s vbuf = %p, ret = %d\n", __FUNCTION__, vbuf, ret));
        if (ret == 0)
        {
            return STATUS_SUCCESS;
        }
        VioGpuDbgBreak();
    }
    DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Failed to create cursor\n", __FUNCTION__));
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS VioGpuAdapter::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition,
                                           _In_ CONST CURRENT_MODE *pModeCur)
{
    PAGED_CODE();
    if (m_pCursorBuf != NULL)
    {
        PGPU_UPDATE_CURSOR crsr;
        PGPU_VBUFFER vbuf;
        UINT ret = 0;
        crsr = (PGPU_UPDATE_CURSOR)m_CursorQueue.AllocCursor(&vbuf);
        RtlZeroMemory(crsr, sizeof(*crsr));

        crsr->hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
        crsr->resource_id = m_pCursorBuf->GetId();

        if (!pSetPointerPosition->Flags.Visible || (UINT)pSetPointerPosition->X > pModeCur->SrcModeWidth ||
            (UINT)pSetPointerPosition->Y > pModeCur->SrcModeHeight || pSetPointerPosition->X < 0 ||
            pSetPointerPosition->Y < 0)
        {
            DbgPrint(TRACE_LEVEL_VERBOSE,
                     ("---> %s (%d - %d) Visiable = %d Value = %x VidPnSourceId = %d\n",
                      __FUNCTION__,
                      pSetPointerPosition->X,
                      pSetPointerPosition->Y,
                      pSetPointerPosition->Flags.Visible,
                      pSetPointerPosition->Flags.Value,
                      pSetPointerPosition->VidPnSourceId));
            crsr->pos.x = 0;
            crsr->pos.y = 0;
        }
        else
        {
            DbgPrint(TRACE_LEVEL_VERBOSE,
                     ("---> %s (%d - %d) Visiable = %d Value = %x VidPnSourceId = %d posX = %d, psY = %d\n",
                      __FUNCTION__,
                      pSetPointerPosition->X,
                      pSetPointerPosition->Y,
                      pSetPointerPosition->Flags.Visible,
                      pSetPointerPosition->Flags.Value,
                      pSetPointerPosition->VidPnSourceId,
                      pSetPointerPosition->X,
                      pSetPointerPosition->Y));
            crsr->pos.x = pSetPointerPosition->X;
            crsr->pos.y = pSetPointerPosition->Y;
        }
        ret = m_CursorQueue.QueueCursor(vbuf);
        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s vbuf = %p, ret = %d\n", __FUNCTION__, vbuf, ret));
        if (ret == 0)
        {
            return STATUS_SUCCESS;
        }
        VioGpuDbgBreak();
    }
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS VioGpuAdapter::Escape(_In_ CONST DXGKARG_ESCAPE *pEscape)
{
    PAGED_CODE();
    PVIOGPU_ESCAPE pVioGpuEscape = (PVIOGPU_ESCAPE)pEscape->pPrivateDriverData;
    NTSTATUS status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    UINT size = pEscape->PrivateDriverDataSize;
    if (size < sizeof(PVIOGPU_ESCAPE))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("%s buffer too small %d, should be at least %d\n",
                  __FUNCTION__,
                  pEscape->PrivateDriverDataSize,
                  size));
        return STATUS_INVALID_BUFFER_SIZE;
    }

    switch (pVioGpuEscape->Type)
    {
        case VIOGPU_GET_DEVICE_ID:
            {
                CreateResolutionEvent();
                size = sizeof(ULONG);
                if (pVioGpuEscape->DataLength < size)
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("%s buffer too small %d, should be at least %d\n",
                              __FUNCTION__,
                              pVioGpuEscape->DataLength,
                              size));
                    return STATUS_INVALID_BUFFER_SIZE;
                }
                pVioGpuEscape->Id = m_Id;
                break;
            }
        case VIOGPU_GET_CUSTOM_RESOLUTION:
            {
                size = sizeof(VIOGPU_DISP_MODE);
                if (pVioGpuEscape->DataLength < size)
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("%s buffer too small %d, should be at least %d\n",
                              __FUNCTION__,
                              pVioGpuEscape->DataLength,
                              size));
                    return STATUS_INVALID_BUFFER_SIZE;
                }
                pVioGpuEscape->Resolution.XResolution = (USHORT)m_ModeInfo[m_CustomModeIndex].VisScreenWidth;
                pVioGpuEscape->Resolution.YResolution = (USHORT)m_ModeInfo[m_CustomModeIndex].VisScreenHeight;
                break;
            }
        default:
            DbgPrint(TRACE_LEVEL_ERROR, ("%s: invalid Escape type 0x%x\n", __FUNCTION__, pVioGpuEscape->Type));
            status = STATUS_INVALID_PARAMETER;
    }

    return status;
}

BOOLEAN VioGpuAdapter::GetDisplayInfo(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER vbuf = NULL;
    ULONG xres = 0;
    ULONG yres = 0;

    for (UINT32 i = 0; i < m_u32NumScanouts; i++)
    {
        if (m_CtrlQueue.AskDisplayInfo(&vbuf))
        {
            m_CtrlQueue.GetDisplayInfo(vbuf, i, &xres, &yres);
            m_CtrlQueue.ReleaseBuffer(vbuf);
            if (xres && yres)
            {
                DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s (%dx%d)\n", __FUNCTION__, xres, yres));
                SetCustomDisplay((USHORT)xres, (USHORT)yres);
                return TRUE;
            }
        }
    }
    xres = NOM_WIDTH_SIZE;
    yres = NOM_HEIGHT_SIZE;
    SetCustomDisplay((USHORT)xres, (USHORT)yres);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return FALSE;
}

int VioGpuAdapter::ProcessEdid(void)
{
    PAGED_CODE();

    if (virtio_is_feature_enabled(m_u64HostFeatures, VIRTIO_GPU_F_EDID))
    {
        GetEdids();
    }
    else
    {
        FixEdid();
    }

    return AddEdidModes();
}

void VioGpuAdapter::FixEdid(void)
{
    PAGED_CODE();

    UCHAR Sum = 0;
    PUCHAR buf = GetEdidData();
    ;
    PEDID_DATA_V1 pdata = (PEDID_DATA_V1)buf;
    pdata->MaximumHorizontalImageSize[0] = 0;
    pdata->MaximumVerticallImageSize[0] = 0;
    pdata->ExtensionFlag[0] = 0;
    pdata->Checksum[0] = 0;
    for (ULONG i = 0; i < EDID_V1_BLOCK_SIZE; i++)
    {
        Sum += buf[i];
    }
    pdata->Checksum[0] = -Sum;
}

BOOLEAN VioGpuAdapter::GetEdids(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER vbuf = NULL;

    for (UINT32 i = 0; i < m_u32NumScanouts; i++)
    {
        if (m_CtrlQueue.AskEdidInfo(&vbuf, i) && m_CtrlQueue.GetEdidInfo(vbuf, i, m_EDIDs))
        {
            m_bEDID = TRUE;
        }
        m_CtrlQueue.ReleaseBuffer(vbuf);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

BOOLEAN VioGpuAdapter::UpdateModes(USHORT xres, USHORT yres, int &cnt)
{
    int idx = 0;

    if ((xres < MIN_WIDTH_SIZE) || (yres < MIN_HEIGHT_SIZE))
    {
        return FALSE;
    }

    for (; idx < cnt; idx++)
    {
        if ((gpu_disp_modes[idx].XResolution == xres) && (gpu_disp_modes[idx].YResolution == yres))
        {
            return FALSE;
        }
    }
    gpu_disp_modes[idx].XResolution = xres;
    gpu_disp_modes[idx].YResolution = yres;
    cnt++;
    return TRUE;
}

int VioGpuAdapter::AddEdidModes(void)
{
    PAGED_CODE();
    PEDID_DATA_V1 edid_data = (PEDID_DATA_V1)(GetEdidData());
    ESTABLISHED_TIMINGS_1_2 est_timing_1_2 = edid_data->EstablishedTimings;
    MANUFACTURER_TIMINGS manufact_timing = edid_data->ManufacturerTimings;
    int modecount = 0;

    UpdateModes(MIN_WIDTH_SIZE, MIN_HEIGHT_SIZE, modecount);
    UpdateModes(NOM_WIDTH_SIZE, NOM_HEIGHT_SIZE, modecount);

    if (est_timing_1_2.Timing_800x600_60 || est_timing_1_2.Timing_800x600_56 || est_timing_1_2.Timing_800x600_75 ||
        est_timing_1_2.Timing_800x600_72)
    {
        UpdateModes(800, 600, modecount);
    }

    if (est_timing_1_2.Timing_720x400_88 || est_timing_1_2.Timing_720x400_70)
    {
        UpdateModes(720, 400, modecount);
    }

    if (est_timing_1_2.Timing_832x624_75)
    {
        UpdateModes(832, 624, modecount);
    }

    if (est_timing_1_2.Timing_1280x1024_75)
    {
        UpdateModes(1280, 1024, modecount);
    }

    if (manufact_timing.Timing_1152x870_75)
    {
        UpdateModes(1152, 870, modecount);
    }

    PSTANDARD_TIMING_DESCRIPTOR standard_timing = edid_data->StandardTimings;
    for (int i = 0; i < 8; i++, standard_timing++)
    {
        VIOGPU_DISP_MODE mode{0};
        if (GetStandardTimingResolution(standard_timing, &mode))
        {
            UpdateModes(mode.XResolution, mode.YResolution, modecount);
        }
    }

    if (edid_data->Revision[0] == 4)
    {
        PEDID_DETAILED_DESCRIPTOR detailed_desc = edid_data->EDIDDetailedTimings;
        for (int i = 0; i < 4; i++, detailed_desc++)
        {
            if (detailed_desc->PixelClock == 0)
            {
                PEDID_DISPLAY_DESCRIPTOR disp = (PEDID_DISPLAY_DESCRIPTOR)detailed_desc;
                if (disp->Tag == 0xF7 && disp->Reserved1 == 0)
                {
                    PESTABLISHED_TIMINGS_3 est_timing_3 = (PESTABLISHED_TIMINGS_3)disp->Data;
                    if (est_timing_3->Timing_640x350_85)
                    {
                        UpdateModes(640, 350, modecount);
                    }

                    if (est_timing_3->Timing_640x400_85)
                    {
                        UpdateModes(640, 400, modecount);
                    }

                    if (est_timing_3->Timing_640x480_85)
                    {
                        UpdateModes(640, 480, modecount);
                    }

                    if (est_timing_3->Timing_720x400_85)
                    {
                        UpdateModes(720, 400, modecount);
                    }

                    if (est_timing_3->Timing_800x600_85)
                    {
                        UpdateModes(800, 600, modecount);
                    }

                    if (est_timing_3->Timing_848x480_60)
                    {
                        UpdateModes(848, 480, modecount);
                    }

                    if (est_timing_3->Timing_1024x768_85)
                    {
                        UpdateModes(1024, 768, modecount);
                    }

                    if (est_timing_3->Timing_1152x864_75)
                    {
                        UpdateModes(1152, 864, modecount);
                    }

                    if (est_timing_3->Timing_1280x768_60 || est_timing_3->Timing_1280x768_60_RB ||
                        est_timing_3->Timing_1280x768_75 || est_timing_3->Timing_1280x768_85)
                    {
                        UpdateModes(1280, 768, modecount);
                    }

                    if (est_timing_3->Timing_1280x960_60 || est_timing_3->Timing_1280x960_85)
                    {
                        UpdateModes(1280, 960, modecount);
                    }

                    if (est_timing_3->Timing_1280x1024_60 || est_timing_3->Timing_1280x1024_85)
                    {
                        UpdateModes(1280, 1024, modecount);
                    }

                    if (est_timing_3->Timing_1360x768_60)
                    {
                        UpdateModes(1360, 768, modecount);
                    }

                    if (est_timing_3->Timing_1400x1050_60 || est_timing_3->Timing_1400x1050_60_RB ||
                        est_timing_3->Timing_1400x1050_75 || est_timing_3->Timing_1400x1050_85)
                    {
                        UpdateModes(1400, 1050, modecount);
                    }

                    if (est_timing_3->Timing_1440x900_60 || est_timing_3->Timing_1440x900_60_RB ||
                        est_timing_3->Timing_1440x900_75 || est_timing_3->Timing_1440x900_85)
                    {
                        UpdateModes(1440, 900, modecount);
                    }

                    if (est_timing_3->Timing_1600x1200_60 || est_timing_3->Timing_1600x1200_65 ||
                        est_timing_3->Timing_1600x1200_70 || est_timing_3->Timing_1600x1200_75 ||
                        est_timing_3->Timing_1600x1200_85)
                    {
                        UpdateModes(1600, 1200, modecount);
                    }

                    if (est_timing_3->Timing_1680x1050_60 || est_timing_3->Timing_1680x1050_60_RB ||
                        est_timing_3->Timing_1680x1050_75 || est_timing_3->Timing_1680x1050_85)
                    {
                        UpdateModes(1680, 1050, modecount);
                    }

                    if (est_timing_3->Timing_1792x1344_60 || est_timing_3->Timing_1792x1344_75)
                    {
                        UpdateModes(1792, 1344, modecount);
                    }

                    if (est_timing_3->Timing_1856x1392_60 || est_timing_3->Timing_1856x1392_75)
                    {
                        UpdateModes(1856, 1392, modecount);
                    }

                    if (est_timing_3->Timing_1920x1200_60 || est_timing_3->Timing_1920x1200_60_RB ||
                        est_timing_3->Timing_1920x1200_75 || est_timing_3->Timing_1920x1200_85)
                    {
                        UpdateModes(1920, 1200, modecount);
                    }

                    if (est_timing_3->Timing_1920x1440_60 || est_timing_3->Timing_1920x1440_75)
                    {
                        UpdateModes(1920, 1440, modecount);
                    }
                }
            }
        }
    }

    PEDID_CTA_861 cta_data = (PEDID_CTA_861)GetCTA861Data();
    if (cta_data && cta_data->DTDBegin[0] > 4)
    {
        int vics = (cta_data->DTDBegin[0] - 1) - 4;
        for (int idx = 0; idx < vics; idx++)
        {
            VIOGPU_DISP_MODE mode{0};
            USHORT vic_num = cta_data->Data[idx];
            if (GetVICResolution(vic_num, &mode))
            {
                UpdateModes(mode.XResolution, mode.YResolution, modecount);
            }
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return modecount;
}

void VioGpuAdapter::SetVideoModeInfo(UINT Idx, PVIOGPU_DISP_MODE pModeInfo)
{
    PAGED_CODE();

    PVIDEO_MODE_INFORMATION pMode = NULL;

    pMode = &m_ModeInfo[Idx];
    pMode->Length = sizeof(VIDEO_MODE_INFORMATION);
    pMode->ModeIndex = Idx;
    pMode->VisScreenWidth = pModeInfo->XResolution;
    pMode->VisScreenHeight = pModeInfo->YResolution;
    pMode->ScreenStride = (pModeInfo->XResolution * 4 + 3) & ~0x3;
}

NTSTATUS VioGpuAdapter::UpdateChildStatus(BOOLEAN connect)
{
    PAGED_CODE();
    NTSTATUS Status(STATUS_SUCCESS);
    DXGK_CHILD_STATUS ChildStatus;
    PDXGKRNL_INTERFACE pDXGKInterface(m_pVioGpuDod->GetDxgkInterface());

    RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));

    ChildStatus.Type = StatusConnection;
    ChildStatus.ChildUid = 0;
    ChildStatus.HotPlug.Connected = connect;
    Status = pDXGKInterface->DxgkCbIndicateChildStatus(pDXGKInterface->DeviceHandle, &ChildStatus);
    if (Status != STATUS_SUCCESS)
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("<--- %s DxgkCbIndicateChildStatus failed with status %x\n ", __FUNCTION__, Status));
    }
    return Status;
}

void VioGpuAdapter::SetCustomDisplay(_In_ USHORT xres, _In_ USHORT yres)
{
    PAGED_CODE();

    VIOGPU_DISP_MODE tmpModeInfo = {0};

    if (xres < MIN_WIDTH_SIZE || yres < MIN_HEIGHT_SIZE)
    {
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("%s: (%dx%d) less than (%dx%d)\n", __FUNCTION__, xres, yres, MIN_WIDTH_SIZE, MIN_HEIGHT_SIZE));
    }
    tmpModeInfo.XResolution = m_pVioGpuDod->IsFlexResolution() ? xres : max(MIN_WIDTH_SIZE, xres);
    tmpModeInfo.YResolution = m_pVioGpuDod->IsFlexResolution() ? yres : max(MIN_HEIGHT_SIZE, yres);

    DbgPrint(TRACE_LEVEL_FATAL,
             ("%s - %d (%dx%d)\n", __FUNCTION__, m_CustomModeIndex, tmpModeInfo.XResolution, tmpModeInfo.YResolution));

    SetVideoModeInfo(m_CustomModeIndex, &tmpModeInfo);
}

NTSTATUS VioGpuAdapter::BuildModeList(DXGK_DISPLAY_INFORMATION *pDispInfo)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    delete[] m_ModeInfo;
    m_ModeInfo = NULL;
    m_ModeCount = 0;

    m_ModeCount = ProcessEdid() + 1;

    m_ModeInfo = new (PagedPool) VIDEO_MODE_INFORMATION[m_ModeCount];
    if (!m_ModeInfo)
    {
        Status = STATUS_NO_MEMORY;
        DbgPrint(TRACE_LEVEL_ERROR, ("VioGpuAdapter::GetModeList failed to allocate m_ModeInfo memory\n"));
        return Status;
    }
    RtlZeroMemory(m_ModeInfo, sizeof(VIDEO_MODE_INFORMATION) * m_ModeCount);

    SetCurrentModeIndex(0);

    pDispInfo->Height = max(pDispInfo->Height, MIN_HEIGHT_SIZE);
    pDispInfo->Width = max(pDispInfo->Width, MIN_WIDTH_SIZE);
    pDispInfo->ColorFormat = D3DDDIFMT_X8R8G8B8;
    pDispInfo->Pitch = (BPPFromPixelFormat(pDispInfo->ColorFormat) / BITS_PER_BYTE) * pDispInfo->Width;

    for (USHORT indx = 0; indx < m_ModeCount - 1; indx++)
    {

        PVIOGPU_DISP_MODE pModeInfo = &gpu_disp_modes[indx];

        DbgPrint(TRACE_LEVEL_INFORMATION,
                 ("%s: modes[%d] x_res = %d, y_res = %d\n",
                  __FUNCTION__,
                  indx,
                  pModeInfo->XResolution,
                  pModeInfo->YResolution));

        SetVideoModeInfo(indx, pModeInfo);
        if (pModeInfo->XResolution == NOM_WIDTH_SIZE && pModeInfo->YResolution == NOM_HEIGHT_SIZE)
        {
            SetCurrentModeIndex(indx);
            DbgPrint(TRACE_LEVEL_FATAL,
                     ("%s: modes[%d] x_res = %d, y_res = %d\n",
                      __FUNCTION__,
                      m_CurrentModeIndex,
                      pModeInfo->XResolution,
                      pModeInfo->YResolution));
        }
    }

    m_CustomModeIndex = (USHORT)(m_ModeCount - 1);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("ModeCount filtered %d\n", m_ModeCount));

    GetDisplayInfo();

    for (UINT idx = 0; idx < m_ModeCount; idx++)
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("index %d, XRes = %d, YRes = %d\n",
                  m_ModeInfo[idx].ModeIndex,
                  m_ModeInfo[idx].VisScreenWidth,
                  m_ModeInfo[idx].VisScreenHeight));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}
PAGED_CODE_SEG_END

BOOLEAN VioGpuAdapter::ResetToVgaMode(void)
{
    DestroyFrameBufferObj(TRUE);
    VioGpuAdapterClose();
    return TRUE;
}

void VioGpuAdapter::DestroyFrameBufferObj(BOOLEAN bReset)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UINT resid = 0;

    if (m_pFrameBuf != NULL)
    {
        resid = (UINT)m_pFrameBuf->GetId();
        m_CtrlQueue.DetachBacking(resid);
        m_CtrlQueue.DestroyResource(resid);
        if (bReset == TRUE)
        {
            m_CtrlQueue.SetScanout(0, 0, 0, 0, 0, 0);
        }
        delete m_pFrameBuf;
        m_pFrameBuf = NULL;
        m_Idr.PutId(resid);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void VioGpuAdapter::VioGpuAdapterClose()
{
    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s\n", __FUNCTION__));

    if (m_pVioGpuDod->IsHardwareInit())
    {
        m_pVioGpuDod->SetHardwareInit(FALSE);
        m_CtrlQueue.DisableInterrupt();
        m_CursorQueue.DisableInterrupt();
        virtio_device_reset(&m_VioDev);
        virtio_delete_queues(&m_VioDev);
        m_CtrlQueue.Close();
        m_CursorQueue.Close();
        virtio_device_shutdown(&m_VioDev);
    }
    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuAdapter::InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_ ULONG MessageNumber)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s MessageNumber = %d\n", __FUNCTION__, MessageNumber));
    BOOLEAN serviced = TRUE;
    ULONG intReason = 0;

    if (m_PciResources.IsMSIEnabled())
    {
        switch (MessageNumber)
        {
            case 0:
                intReason = ISR_REASON_CHANGE;
                break;
            case 1:
                intReason = ISR_REASON_DISPLAY;
                break;
            case 2:
                intReason = ISR_REASON_CURSOR;
                break;
            default:
                serviced = FALSE;
                DbgPrint(TRACE_LEVEL_FATAL,
                         ("---> %s Unknown Interrupt Reason MessageNumber%d\n", __FUNCTION__, MessageNumber));
        }
    }
    else
    {
        UNREFERENCED_PARAMETER(MessageNumber);
        UCHAR isrstat = virtio_read_isr_status(&m_VioDev);

        switch (isrstat)
        {
            case 1:
                intReason = (ISR_REASON_DISPLAY | ISR_REASON_CURSOR);
                break;
            case 3:
                intReason = ISR_REASON_CHANGE;
                break;
            default:
                serviced = FALSE;
        }
    }

    if (serviced)
    {
        if (m_pVioGpuDod->IsUsePresentProgress() && (intReason & ISR_REASON_DISPLAY) == ISR_REASON_DISPLAY)
        {
            DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyInterrupt = {};
            NotifyInterrupt.InterruptType = DXGK_INTERRUPT_DISPLAYONLY_PRESENT_PROGRESS;
            NotifyInterrupt.DisplayOnlyPresentProgress.VidPnSourceId = 0;

            NotifyInterrupt.DisplayOnlyPresentProgress.ProgressId = DXGK_PRESENT_DISPLAYONLY_PROGRESS_ID_COMPLETE;
            pDxgkInterface->DxgkCbNotifyInterrupt(pDxgkInterface->DeviceHandle, &NotifyInterrupt);
        }

        InterlockedOr((PLONG)&m_PendingWorks, intReason);
        pDxgkInterface->DxgkCbQueueDpc(pDxgkInterface->DeviceHandle);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return serviced;
}

void VioGpuAdapter::ThreadWork(_In_ PVOID Context)
{
    VioGpuAdapter *pdev = reinterpret_cast<VioGpuAdapter *>(Context);
    pdev->ThreadWorkRoutine();
}

void VioGpuAdapter::ThreadWorkRoutine(void)
{
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    for (;;)
    {
        KeWaitForSingleObject(&m_ConfigUpdateEvent, Executive, KernelMode, FALSE, NULL);

        if (m_bStopWorkThread)
        {
            PsTerminateSystemThread(STATUS_SUCCESS);
            break;
        }
        ConfigChanged();
        NotifyResolutionEvent();
    }
}

void VioGpuAdapter::ConfigChanged(void)
{
    DbgPrint(TRACE_LEVEL_FATAL, ("<--> %s\n", __FUNCTION__));
    UINT32 events_read, events_clear = 0;
    virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_read), &events_read, sizeof(m_u32NumScanouts));
    if (events_read & VIRTIO_GPU_EVENT_DISPLAY)
    {
        GetDisplayInfo();
        events_clear |= VIRTIO_GPU_EVENT_DISPLAY;
        virtio_set_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_clear), &events_clear, sizeof(m_u32NumScanouts));
        //        UpdateChildStatus(FALSE);
        //        ProcessEdid();
        UpdateChildStatus(TRUE);
    }
}

VOID VioGpuAdapter::DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(pDxgkInterface);
    PGPU_VBUFFER pvbuf = NULL;
    UINT len = 0;
    ULONG reason;
    while ((reason = InterlockedExchange((PLONG)&m_PendingWorks, 0)) != 0)
    {
        if ((reason & ISR_REASON_DISPLAY))
        {
            while ((pvbuf = m_CtrlQueue.DequeueBuffer(&len)) != NULL)
            {
                DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s m_CtrlQueue pvbuf = %p len = %d\n", __FUNCTION__, pvbuf, len));
                PGPU_CTRL_HDR pcmd = (PGPU_CTRL_HDR)pvbuf->buf;
                PGPU_CTRL_HDR resp = (PGPU_CTRL_HDR)pvbuf->resp_buf;

                if (resp->type >= VIRTIO_GPU_RESP_ERR_UNSPEC)
                {
                    DbgPrint(TRACE_LEVEL_FATAL, ("!!!!! Command failed %d", resp->type));
                }
                if (resp->type != VIRTIO_GPU_RESP_OK_NODATA)
                {
                    DbgPrint(TRACE_LEVEL_ERROR,
                             ("<--- %s type = %xlu flags = %lu fence_id = %llu ctx_id = %lu cmd_type = %lu\n",
                              __FUNCTION__,
                              resp->type,
                              resp->flags,
                              resp->fence_id,
                              resp->ctx_id,
                              pcmd->type));
                }
                if (pvbuf->complete_cb != NULL)
                {
                    pvbuf->complete_cb(pvbuf->complete_ctx);
                }
                if (pvbuf->auto_release)
                {
                    m_CtrlQueue.ReleaseBuffer(pvbuf);
                }
            };
        }
        if ((reason & ISR_REASON_CURSOR))
        {
            while ((pvbuf = m_CursorQueue.DequeueCursor(&len)) != NULL)
            {
                DbgPrint(TRACE_LEVEL_VERBOSE,
                         ("---> %s m_CursorQueue pvbuf = %p len = %u\n", __FUNCTION__, pvbuf, len));
                m_CursorQueue.ReleaseBuffer(pvbuf);
            };
        }
        if (reason & ISR_REASON_CHANGE)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("---> %s ConfigChanged\n", __FUNCTION__));
            KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VOID VioGpuAdapter::ResetDevice(VOID)
{
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

UINT ColorFormat(UINT format)
{
    switch (format)
    {
        case D3DDDIFMT_A8R8G8B8:
            return VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
        case D3DDDIFMT_X8R8G8B8:
            return VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
        case D3DDDIFMT_A8B8G8R8:
            return VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM;
        case D3DDDIFMT_X8B8G8R8:
            return VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM;
    }
    DbgPrint(TRACE_LEVEL_ERROR, ("---> %s Unsupported color format %d\n", __FUNCTION__, format));
    return VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
}

PAGED_CODE_SEG_BEGIN
BOOLEAN VioGpuAdapter::CreateFrameBufferObj(PVIDEO_MODE_INFORMATION pModeInfo, CURRENT_MODE *pCurrentMode)
{
    UINT resid, format, size;
    VioGpuObj *obj;
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("---> %s - %d: (%d x %d)\n", __FUNCTION__, m_Id, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight));
    ASSERT(m_pFrameBuf == NULL);
    size = pModeInfo->ScreenStride * pModeInfo->VisScreenHeight;
    format = ColorFormat(pCurrentMode->DispInfo.ColorFormat);
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("---> %s - (%d -> %d)\n", __FUNCTION__, pCurrentMode->DispInfo.ColorFormat, format));
    resid = m_Idr.GetId();
    m_CtrlQueue.CreateResource(resid, format, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight);
    obj = new (NonPagedPoolNx) VioGpuObj();
    if (!obj->Init(size, &m_FrameSegment))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s Failed to init obj size = %d\n", __FUNCTION__, size));
        delete obj;
        return FALSE;
    }

    GpuObjectAttach(resid, obj);
    m_CtrlQueue.SetScanout(0 /*FIXME m_Id*/, resid, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0);
    m_CtrlQueue.TransferToHost2D(resid, 0, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0);
    m_CtrlQueue.ResFlush(resid, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0);
    m_pFrameBuf = obj;
    pCurrentMode->FrameBuffer = obj->GetVirtualAddress();
    pCurrentMode->Flags.FrameBufferIsActive = TRUE;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

BOOLEAN VioGpuAdapter::CreateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape,
                                    _In_ CONST CURRENT_MODE *pCurrentMode)
{
    UINT resid, format, size;
    VioGpuObj *obj;
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("---> %s - %d: (%d x %d - %d) (%d + %d)\n",
              __FUNCTION__,
              m_Id,
              pSetPointerShape->Width,
              pSetPointerShape->Height,
              pSetPointerShape->Pitch,
              pSetPointerShape->XHot,
              pSetPointerShape->YHot));

    size = POINTER_SIZE * POINTER_SIZE * 4;
    format = ColorFormat(D3DDDIFMT_A8R8G8B8);
    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("---> %s - (%x -> %x)\n", __FUNCTION__, pCurrentMode->DispInfo.ColorFormat, format));
    resid = (UINT)m_Idr.GetId();
    m_CtrlQueue.CreateResource(resid, format, POINTER_SIZE, POINTER_SIZE);
    obj = new (NonPagedPoolNx) VioGpuObj();
    if (!obj->Init(size, &m_CursorSegment))
    {
        VioGpuDbgBreak();
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s Failed to init obj size = %d\n", __FUNCTION__, size));
        delete obj;
        return FALSE;
    }
    if (!GpuObjectAttach(resid, obj))
    {
        VioGpuDbgBreak();
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s Failed to attach gpu object\n", __FUNCTION__));
        delete obj;
        return FALSE;
    }
    m_pCursorBuf = obj;
    return TRUE;
}

BOOLEAN VioGpuAdapter::UpdateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape,
                                    _In_ CONST CURRENT_MODE *pCurrentMode)
{
    PAGED_CODE();
    RECT Rect;
    Rect.left = 0;
    Rect.top = 0;
    Rect.right = Rect.left + pSetPointerShape->Width;
    Rect.bottom = Rect.top + pSetPointerShape->Height;

    if ((m_pCursorBuf == NULL) && !CreateCursor(pSetPointerShape, pCurrentMode))
    {
        VioGpuDbgBreak();
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Cannot create cursor\n", __FUNCTION__));
        return FALSE;
    }

    BLT_INFO DstBltInfo;
    DstBltInfo.pBits = m_pCursorBuf->GetVirtualAddress();
    DstBltInfo.Pitch = POINTER_SIZE * 4;
    DstBltInfo.BitsPerPel = BPPFromPixelFormat(D3DDDIFMT_A8R8G8B8);
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    DstBltInfo.Width = POINTER_SIZE;
    DstBltInfo.Height = POINTER_SIZE;

    BLT_INFO SrcBltInfo;
    SrcBltInfo.pBits = (PVOID)pSetPointerShape->pPixels;
    SrcBltInfo.Pitch = pSetPointerShape->Pitch;
    if (pSetPointerShape->Flags.Color)
    {
        SrcBltInfo.BitsPerPel = BPPFromPixelFormat(D3DDDIFMT_A8R8G8B8);
    }
    else
    {
        VioGpuDbgBreak();
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Invalid cursor color %d\n", __FUNCTION__, pSetPointerShape->Flags.Value));
        return FALSE;
    }
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = pCurrentMode->Rotation;
    SrcBltInfo.Width = pSetPointerShape->Width;
    SrcBltInfo.Height = pSetPointerShape->Height;

    BltBits(&DstBltInfo, &SrcBltInfo, &Rect);

    m_CtrlQueue.TransferToHost2D(m_pCursorBuf->GetId(), 0, pSetPointerShape->Width, pSetPointerShape->Height, 0, 0);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

void VioGpuAdapter::DestroyCursor()
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    if (m_pCursorBuf != NULL)
    {
        UINT id = (UINT)m_pCursorBuf->GetId();
        m_CtrlQueue.DetachBacking(id);
        m_CtrlQueue.DestroyResource(id);
        delete m_pCursorBuf;
        m_pCursorBuf = NULL;
        m_Idr.PutId(id);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuAdapter::GpuObjectAttach(UINT res_id, VioGpuObj *obj)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    PGPU_MEM_ENTRY ents = NULL;
    PSCATTER_GATHER_LIST sgl = NULL;
    UINT size = 0;
    sgl = obj->GetSGList();
    size = sizeof(GPU_MEM_ENTRY) * sgl->NumberOfElements;
    ents = reinterpret_cast<PGPU_MEM_ENTRY>(new (NonPagedPoolNx) BYTE[size]);

    if (!ents)
    {
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("<--- %s cannot allocate memory %x bytes numberofentries = %d\n",
                  __FUNCTION__,
                  size,
                  sgl->NumberOfElements));
        return FALSE;
    }
    // FIXME
    RtlZeroMemory(ents, size);

    for (UINT i = 0; i < sgl->NumberOfElements; i++)
    {
        ents[i].addr = sgl->Elements[i].Address.QuadPart;
        ents[i].length = sgl->Elements[i].Length;
        ents[i].padding = 0;
    }

    m_CtrlQueue.AttachBacking(res_id, ents, sgl->NumberOfElements);
    obj->SetId(res_id);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}
PAGED_CODE_SEG_END

PDXGKRNL_INTERFACE VioGpuAdapter::GetDxgkInterface()
{
    return m_pVioGpuDod->GetDxgkInterface();
}
