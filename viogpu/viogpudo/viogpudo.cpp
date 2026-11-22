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
    RtlZeroMemory(&m_SystemDisplay, sizeof(m_SystemDisplay));
    RtlZeroMemory(&m_SystemDisplayInfo, sizeof(m_SystemDisplayInfo));
    RtlZeroMemory(&m_PointerShape, sizeof(m_PointerShape));
    m_PersistentDispMode0Width = 0;
    m_PersistentDispMode0Height = 0;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VioGpuDod::~VioGpuDod(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s 0x%p\n", __FUNCTION__, m_pHWDevice));
    DeleteAdapter();
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

    NTSTATUS Status = StartAdapter(pDxgkStartInfo, pDxgkInterface, pNumberOfViews, pNumberOfChildren);
    if (!NT_SUCCESS(Status))
    {
        // The adapter a failed start made goes: the device may be started again on this context.
        DeleteAdapter();
    }
    return Status;
}

NTSTATUS VioGpuDod::StartAdapter(_In_ DXGK_START_INFO *pDxgkStartInfo,
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

    RtlZeroMemory(&m_SystemDisplay, sizeof(m_SystemDisplay));
    m_SystemDisplay.DispInfo.TargetId = D3DDDI_ID_UNINITIALIZED;

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

    Status = m_pHWDevice->HWInit(m_DeviceInfo.TranslatedResourceList, &m_SystemDisplay.DispInfo);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("HWInit failed with status 0x%X\n", Status));
        return Status;
    }

    m_SystemDisplay.FrameBuffer = m_pHWDevice->GetPciResources()[0].GetMappedAddress(0, 0);
    if (!m_SystemDisplay.FrameBuffer)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("Failed to map RamFrameBuffer for VGA mode"));
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

    m_SystemDisplay.DispInfo.Width = max(MIN_WIDTH_SIZE, m_SystemDisplayInfo.Width);
    m_SystemDisplay.DispInfo.Height = max(MIN_HEIGHT_SIZE, m_SystemDisplayInfo.Height);
    m_SystemDisplay.DispInfo.ColorFormat = VIOGPU_FRAMEBUFFER_FORMAT;
    m_SystemDisplay.DispInfo.Pitch = (BPPFromPixelFormat(m_SystemDisplay.DispInfo.ColorFormat) / BITS_PER_BYTE) *
                                     m_SystemDisplay.DispInfo.Width;
    m_SystemDisplay.DispInfo.TargetId = 0;
    if (m_SystemDisplay.DispInfo.PhysicAddress.QuadPart == 0LL && m_SystemDisplayInfo.PhysicAddress.QuadPart != 0LL)
    {
        m_SystemDisplay.DispInfo.PhysicAddress = m_SystemDisplayInfo.PhysicAddress;
    }

    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("<--- %s ColorFormat = %d\n", __FUNCTION__, m_SystemDisplay.DispInfo.ColorFormat));

    *pNumberOfViews = MAX_VIEWS;
    *pNumberOfChildren = MAX_CHILDREN;
    m_Flags.DriverStarted = TRUE;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::StopDevice(VOID)
{
    PAGED_CODE();

    // Marked first: the DDIs that can still run check it before they use the adapter.
    m_Flags.DriverStarted = FALSE;

    // The device may be started again on this context, with other resources: what StartDevice set up goes, and the
    // device is reset.
    DeleteAdapter();
    return STATUS_SUCCESS;
}

// Shuts the adapter down and frees it. It shuts down while the ISR and the DPC still reach it, which it needs until
// the device is reset; the shutdown flushes the DPCs, and an ISR with handling off queues none. Then the ISR stops
// reaching it, under the interrupt lock of every message Windows granted, so that no ISR still holds it when it is
// freed.
void VioGpuDod::DeleteAdapter(void)
{
    PAGED_CODE();

    // The BAR mapping goes with the adapter.
    m_SystemDisplay.Enabled = FALSE;
    m_SystemDisplay.FrameBuffer = NULL;

    VioGpuAdapter *pHWDevice = m_pHWDevice;
    if (pHWDevice == NULL)
    {
        return;
    }
    pHWDevice->Shutdown();

    ULONG Messages = pHWDevice->GetMessageCount();
    ULONG Synchronized = 0;
    for (ULONG Message = 0; Message < Messages; Message++)
    {
        BOOLEAN Ret = FALSE;
        if (NT_SUCCESS(m_DxgkInterface.DxgkCbSynchronizeExecution(m_DxgkInterface.DeviceHandle,
                                                                  DetachAdapterCb,
                                                                  this,
                                                                  Message,
                                                                  &Ret)))
        {
            Synchronized++;
        }
    }
    if (Synchronized == 0)
    {
        // Without a connected interrupt no ISR runs; with one, an ISR may still hold the adapter.
        DbgPrint(TRACE_LEVEL_FATAL, ("%s: no message synchronized before the adapter goes\n", __FUNCTION__));
        DetachAdapterCb(this);
    }
    else if (Synchronized != Messages)
    {
        // An ISR on a message not synchronized may still hold the adapter.
        DbgPrint(TRACE_LEVEL_FATAL,
                 ("%s: %lu of %lu message(s) synchronized before the adapter goes\n",
                  __FUNCTION__,
                  Synchronized,
                  Messages));
    }
    delete pHWDevice;
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

        Status = m_pHWDevice->SetPowerState(&m_DeviceInfo, DevicePowerState);
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
        // The host enables and disables a scanout at any time, and raises VIRTIO_GPU_EVENT_DISPLAY when it does: the
        // output is hot-pluggable on a VGA device too.
        pChildRelations[ChildIndex].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
        // A VGA device's output is an external monitor, not a built-in panel: Windows treats a built-in panel that goes
        // away like a closed lid, keeps it, and does not read the EDID of the monitor that takes its place.
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = IsVgaDevice() ? D3DKMDT_VOT_HDMI
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
                pChildStatus->HotPlug.Connected = IsDriverActive() && m_pHWDevice != NULL &&
                                                  m_pHWDevice->GetController()->IsConnected(pChildStatus->ChildUid);
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

    ULONG length = pDeviceDescriptor->DescriptorLength;
    NTSTATUS Status = m_pHWDevice->GetController()->CopyEdid(ChildUid,
                                                             pDeviceDescriptor->DescriptorOffset,
                                                             pDeviceDescriptor->DescriptorBuffer,
                                                             &length);
    if (NT_SUCCESS(Status))
    {
        pDeviceDescriptor->DescriptorLength = length;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
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
                // DXGKCB_NOTIFY_INTERRUPT requires the message the ISR notifies dxgkrnl from (GetNotifyMessage).
                if (m_pHWDevice != NULL)
                {
                    pDriverCaps->InterruptMessageNumber = m_pHWDevice->GetNotifyMessage();
                }

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
        VIOGPU_SOURCE_VIEW source;
        m_pHWDevice->GetController()->GetSource(pSetPointerPosition->VidPnSourceId, &source);
        return m_pHWDevice->GetCursor()->SetPointerPosition(pSetPointerPosition, &source);
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
        VIOGPU_SOURCE_VIEW source;
        m_pHWDevice->GetController()->GetSource(pSetPointerShape->VidPnSourceId, &source);
        return m_pHWDevice->GetCursor()->SetPointerShape(pSetPointerShape, &source);
    }
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS VioGpuDod::Escape(_In_ CONST DXGKARG_ESCAPE *pEscape)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pEscape != NULL);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s Flags = %d\n", __FUNCTION__, pEscape->Flags.Value));

    return m_pHWDevice->GetHelperChannel()->Escape(pEscape);
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

    if (m_MonitorPowerState > PowerDeviceD0)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s the monitor is off\n", __FUNCTION__));
        return STATUS_SUCCESS;
    }

    Status = m_pHWDevice->GetController()->Present(pPresentDisplayOnly->VidPnSourceId, pPresentDisplayOnly);

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

    m_pHWDevice->GetController()->BlackOut(0);
    DbgPrint(TRACE_LEVEL_FATAL,
             ("StopDeviceAndReleasePostDisplayOwnership Width = %d Height = %d Pitch = %d ColorFormat = %dn",
              m_SystemDisplayInfo.Width,
              m_SystemDisplayInfo.Height,
              m_SystemDisplayInfo.Pitch,
              m_SystemDisplayInfo.ColorFormat));

    *pDisplayInfo = m_SystemDisplayInfo;
    pDisplayInfo->TargetId = TargetId;
    pDisplayInfo->AcpiId = m_SystemDisplay.DispInfo.AcpiId;
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

    for (D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = 0; SourceId < m_pHWDevice->GetController()->GetSourceCount();
         ++SourceId)
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
        else if (NumPathsFromSource > 1)
        {
            // The driver does not clone: a source drives one path at most.
            return STATUS_SUCCESS;
        }

        if (NumPathsFromSource == 0)
        {
            continue;
        }

        D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet;
        CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface;
        Status = pVidPnInterface->pfnAcquireSourceModeSet(pIsSupportedVidPn->hDesiredVidPn,
                                                          SourceId,
                                                          &hVidPnSourceModeSet,
                                                          &pVidPnSourceModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE)
            {
                continue;
            }
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("<--- %s pfnAcquireSourceModeSet failed with Status = 0x%X, SourceId = %llu\n",
                      __FUNCTION__,
                      Status,
                      LONG_PTR(SourceId)));
            return Status;
        }

        CONST D3DKMDT_VIDPN_SOURCE_MODE *pPinnedVidPnSourceModeInfo = NULL;
        Status = pVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnSourceModeSet,
                                                                        &pPinnedVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            pVidPnInterface->pfnReleaseSourceModeSet(pIsSupportedVidPn->hDesiredVidPn, hVidPnSourceModeSet);
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("<--- %s pfnAcquirePinnedModeInfo failed with Status = 0x%X\n", __FUNCTION__, Status));
            return Status;
        }

        // A pinned source mode the driver cannot back (ModeFits) makes the VidPN unsupported, and so does a pinned
        // target mode of another size: the driver scans out the source mode's size.
        BOOLEAN bReject = FALSE;
        BOOLEAN SourcePinned = (pPinnedVidPnSourceModeInfo != NULL);
        UINT cx = 0;
        UINT cy = 0;
        if (SourcePinned)
        {
            cx = pPinnedVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx;
            cy = pPinnedVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy;
            if (!m_pHWDevice->ModeFits(cx, cy))
            {
                DbgPrint(TRACE_LEVEL_WARNING,
                         ("<--- %s %dx%d is more than the driver can back\n", __FUNCTION__, cx, cy));
                bReject = TRUE;
            }
            pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pPinnedVidPnSourceModeInfo);
        }
        pVidPnInterface->pfnReleaseSourceModeSet(pIsSupportedVidPn->hDesiredVidPn, hVidPnSourceModeSet);

        for (SIZE_T PathIndex = 0; SourcePinned && !bReject && PathIndex < NumPathsFromSource; ++PathIndex)
        {
            D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId = D3DDDI_ID_UNINITIALIZED;
            Status = pVidPnTopologyInterface->pfnEnumPathTargetsFromSource(hVidPnTopology,
                                                                           SourceId,
                                                                           PathIndex,
                                                                           &TargetId);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("<--- %s pfnEnumPathTargetsFromSource failed with Status = 0x%X, SourceId = %llu\n",
                          __FUNCTION__,
                          Status,
                          LONG_PTR(SourceId)));
                return Status;
            }

            D3DKMDT_2DREGION TargetSize = {};
            BOOLEAN TargetPinned = FALSE;
            Status = GetPinnedTargetSize(pIsSupportedVidPn->hDesiredVidPn,
                                         pVidPnInterface,
                                         TargetId,
                                         &TargetSize,
                                         &TargetPinned);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            if (TargetPinned && (TargetSize.cx != cx || TargetSize.cy != cy))
            {
                DbgPrint(TRACE_LEVEL_WARNING,
                         ("<--- %s source %ux%u on a %ux%u target needs scaling\n",
                          __FUNCTION__,
                          cx,
                          cy,
                          TargetSize.cx,
                          TargetSize.cy));
                bReject = TRUE;
            }
        }

        if (bReject)
        {
            pIsSupportedVidPn->IsVidPnSupported = FALSE;
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

// The modes offered on one end of a path to ScanoutId. The driver scans out the source mode's size and never reads the
// target mode, so it neither stretches nor centers: a mode pinned on the other end allows exactly one mode, of its
// size. Without one, every mode is offered.
NTSTATUS VioGpuDod::GetOfferedModes(UINT ScanoutId,
                                    _In_opt_ CONST D3DKMDT_2DREGION *pPinnedSize,
                                    VIOGPU_DISP_MODE (&Modes)[VIOGPU_MAX_MODES + 1],
                                    _Out_ PULONG pCount)
{
    PAGED_CODE();

    if (pPinnedSize == NULL)
    {
        *pCount = m_pHWDevice->GetController()->GetModes(ScanoutId, Modes);
        return STATUS_SUCCESS;
    }

    *pCount = 0;
    if (!m_pHWDevice->ModeFits(pPinnedSize->cx, pPinnedSize->cy))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("<--- %s the pinned %ux%u is more than the driver can back\n",
                  __FUNCTION__,
                  pPinnedSize->cx,
                  pPinnedSize->cy));
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    Modes[0].XResolution = (USHORT)pPinnedSize->cx;
    Modes[0].YResolution = (USHORT)pPinnedSize->cy;
    *pCount = 1;
    return STATUS_SUCCESS;
}

// The size of the target mode pinned on TargetId in hVidPn, if one is.
NTSTATUS VioGpuDod::GetPinnedTargetSize(D3DKMDT_HVIDPN hVidPn,
                                        _In_ CONST DXGK_VIDPN_INTERFACE *pVidPnInterface,
                                        D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                        _Out_ D3DKMDT_2DREGION *pSize,
                                        _Out_ PBOOLEAN pPinned)
{
    PAGED_CODE();

    D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet = 0;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *pPinnedTargetModeInfo = NULL;

    pSize->cx = 0;
    pSize->cy = 0;
    *pPinned = FALSE;

    NTSTATUS Status = pVidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                                                               TargetId,
                                                               &hVidPnTargetModeSet,
                                                               &pVidPnTargetModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("<--- %s pfnAcquireTargetModeSet failed with Status = 0x%X, TargetId = %llu\n",
                  __FUNCTION__,
                  Status,
                  LONG_PTR(TargetId)));
        return Status;
    }

    Status = pVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnTargetModeSet, &pPinnedTargetModeInfo);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("<--- %s pfnAcquirePinnedModeInfo failed with Status = 0x%X, TargetId = %llu\n",
                  __FUNCTION__,
                  Status,
                  LONG_PTR(TargetId)));
    }
    else if (pPinnedTargetModeInfo != NULL)
    {
        *pSize = pPinnedTargetModeInfo->VideoSignalInfo.ActiveSize;
        *pPinned = TRUE;
        pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pPinnedTargetModeInfo);
    }
    pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hVidPnTargetModeSet);
    return Status;
}

NTSTATUS VioGpuDod::AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
                                        D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
                                        UINT ScanoutId,
                                        _In_opt_ CONST D3DKMDT_2DREGION *pPinnedTargetSize)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_DISP_MODE modes[VIOGPU_MAX_MODES + 1];
    ULONG count = 0;
    NTSTATUS Status = GetOfferedModes(ScanoutId, pPinnedTargetSize, modes, &count);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    for (ULONG idx = 0; idx < count; ++idx)
    {
        D3DKMDT_VIDPN_SOURCE_MODE *pVidPnSourceModeInfo = NULL;
        PVIOGPU_DISP_MODE pModeInfo = &modes[idx];
        Status = pVidPnSourceModeSetInterface->pfnCreateNewModeInfo(hVidPnSourceModeSet, &pVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("pfnCreateNewModeInfo failed with Status = 0x%X, hVidPnSourceModeSet = %llu",
                      Status,
                      LONG_PTR(hVidPnSourceModeSet)));
            return Status;
        }

        pVidPnSourceModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx = pModeInfo->XResolution;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy = pModeInfo->YResolution;
        pVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize;
        pVidPnSourceModeInfo->Format.Graphics.Stride = pModeInfo->XResolution * 4;
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

VOID VioGpuDod::BuildVideoSignalInfo(D3DKMDT_VIDEO_SIGNAL_INFO *pVideoSignalInfo, PVIOGPU_DISP_MODE pModeInfo)
{
    PAGED_CODE();

    pVideoSignalInfo->VideoStandard = D3DKMDT_VSS_OTHER;
    pVideoSignalInfo->TotalSize.cx = pModeInfo->XResolution;
    pVideoSignalInfo->TotalSize.cy = pModeInfo->YResolution;
    // The signal has no blanking, so the active size is the total size. Monitor modes and target modes are both
    // built here: Windows lists a size outside its raw mode list only if a target mode matches a monitor mode.
    pVideoSignalInfo->ActiveSize = pVideoSignalInfo->TotalSize;

    pVideoSignalInfo->VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pVideoSignalInfo->VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pVideoSignalInfo->HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pVideoSignalInfo->HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pVideoSignalInfo->PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pVideoSignalInfo->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
}

NTSTATUS VioGpuDod::AddSingleTargetMode(_In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
                                        D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
                                        UINT ScanoutId,
                                        _In_opt_ CONST D3DKMDT_2DREGION *pPinnedSourceSize)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    D3DKMDT_VIDPN_TARGET_MODE *pVidPnTargetModeInfo = NULL;

    // With a pinned source mode the one target mode of its size is preferred, otherwise the size the host asks for on
    // the scanout, once.
    VIOGPU_DISP_MODE modes[VIOGPU_MAX_MODES + 1];
    ULONG count = 0;
    NTSTATUS Status = GetOfferedModes(ScanoutId, pPinnedSourceSize, modes, &count);
    VIOGPU_DISP_MODE preferred = m_pHWDevice->GetController()->GetCustomMode(ScanoutId);
    BOOLEAN PreferredAdded = FALSE;
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    for (UINT ModeIndex = 0; ModeIndex < count; ++ModeIndex)
    {
        PVIOGPU_DISP_MODE pModeInfo = &modes[ModeIndex];
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
        BuildVideoSignalInfo(&pVidPnTargetModeInfo->VideoSignalInfo, pModeInfo);

        BOOLEAN Preferred = pPinnedSourceSize != NULL ||
                            (!PreferredAdded && pModeInfo->XResolution == preferred.XResolution &&
                             pModeInfo->YResolution == preferred.YResolution);
        pVidPnTargetModeInfo->Preference = Preferred ? D3DKMDT_MP_PREFERRED : D3DKMDT_MP_NOTPREFERRED;

        Status = pVidPnTargetModeSetInterface->pfnAddMode(hVidPnTargetModeSet, pVidPnTargetModeInfo);
        if (!NT_SUCCESS(Status))
        {
            NTSTATUS TempStatus = pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet,
                                                                                   pVidPnTargetModeInfo);
            UNREFERENCED_PARAMETER(TempStatus);
            NT_ASSERT(NT_SUCCESS(TempStatus));

            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("pfnAddMode failed with Status = 0x%X, hVidPnTargetModeSet = 0x%llu, pVidPnTargetModeInfo = "
                          "%p\n",
                          Status,
                          LONG_PTR(hVidPnTargetModeSet),
                          pVidPnTargetModeInfo));
                return Status;
            }
        }
        else if (Preferred)
        {
            PreferredAdded = TRUE;
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

// Adds a driver mode of pMode's size to a monitor source mode set. A mode the set holds already is not an error: the
// monitor's EDID can list the size.
NTSTATUS VioGpuDod::AddMonitorMode(D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet,
                                   _In_ CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSourceModeSetInterface,
                                   _In_ CONST VIOGPU_DISP_MODE *pMode,
                                   D3DKMDT_MODE_PREFERENCE Preference)
{
    PAGED_CODE();

    D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSourceMode = NULL;
    NTSTATUS Status = pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(hMonitorSourceModeSet, &pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnCreateNewModeInfo failed with Status = 0x%X, hMonitorSourceModeSet = 0x%llu\n",
                  Status,
                  LONG_PTR(hMonitorSourceModeSet)));
        return Status;
    }

    VIOGPU_DISP_MODE mode = *pMode;
    BuildVideoSignalInfo(&pMonitorSourceMode->VideoSignalInfo, &mode);
    pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
    pMonitorSourceMode->Preference = Preference;
    pMonitorSourceMode->ColorBasis = D3DKMDT_CB_SRGB;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FirstChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.SecondChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FourthChannel = 0;

    Status = pMonitorSourceModeSetInterface->pfnAddMode(hMonitorSourceModeSet, pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        NTSTATUS TempStatus = pMonitorSourceModeSetInterface->pfnReleaseModeInfo(hMonitorSourceModeSet,
                                                                                 pMonitorSourceMode);
        UNREFERENCED_PARAMETER(TempStatus);
        NT_ASSERT(NT_SUCCESS(TempStatus));

        if (Status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
        {
            return STATUS_SUCCESS;
        }
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnAddMode failed with Status = 0x%X, hMonitorSourceModeSet = 0x%llu, %ux%u\n",
                  Status,
                  LONG_PTR(hMonitorSourceModeSet),
                  pMode->XResolution,
                  pMode->YResolution));
        return Status;
    }

    DbgPrint(TRACE_LEVEL_INFORMATION,
             ("%s: add monitor mode %ux%u\n", __FUNCTION__, pMode->XResolution, pMode->YResolution));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::AddSingleMonitorMode(_In_ CONST DXGKARG_RECOMMENDMONITORMODES *CONST pRecommendMonitorModes)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    UINT ScanoutId = pRecommendMonitorModes->VideoPresentTargetId;
    VIOGPU_DISP_MODE modes[VIOGPU_MAX_MODES + 1];
    ULONG count = m_pHWDevice->GetController()->GetModes(ScanoutId, modes);
    // The size the host asks for on the scanout is the monitor's one preferred mode.
    VIOGPU_DISP_MODE preferred = m_pHWDevice->GetController()->GetCustomMode(ScanoutId);

    NTSTATUS Status = AddMonitorMode(pRecommendMonitorModes->hMonitorSourceModeSet,
                                     pRecommendMonitorModes->pMonitorSourceModeSetInterface,
                                     &preferred,
                                     D3DKMDT_MP_PREFERRED);
    for (UINT Idx = 0; NT_SUCCESS(Status) && Idx < count; ++Idx)
    {
        Status = AddMonitorMode(pRecommendMonitorModes->hMonitorSourceModeSet,
                                pRecommendMonitorModes->pMonitorSourceModeSetInterface,
                                &modes[Idx],
                                D3DKMDT_MP_NOTPREFERRED);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

// Adds the driver's modes the monitor's mode set lacks. Windows sets a target mode only if a monitor mode matches it,
// and the set was filled when the monitor arrived, before a custom mode set later through the escape existed. The set
// is walked first: pfnAddMode refuses only an identical mode, and each mode the driver creates has a new Id.
VOID VioGpuDod::AddMissingMonitorModes(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    PAGED_CODE();

    CONST DXGK_MONITOR_INTERFACE *pMonitorInterface = NULL;
    NTSTATUS Status = m_DxgkInterface.DxgkCbQueryMonitorInterface(m_DxgkInterface.DeviceHandle,
                                                                  DXGK_MONITOR_INTERFACE_VERSION_V1,
                                                                  &pMonitorInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("<--- %s DxgkCbQueryMonitorInterface failed with Status = 0x%X\n", __FUNCTION__, Status));
        return;
    }

    D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet = 0;
    CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSourceModeSetInterface = NULL;
    Status = pMonitorInterface->pfnAcquireMonitorSourceModeSet(m_DxgkInterface.DeviceHandle,
                                                               TargetId,
                                                               &hMonitorSourceModeSet,
                                                               &pMonitorSourceModeSetInterface);
    if (Status == STATUS_GRAPHICS_MONITOR_NOT_CONNECTED)
    {
        return;
    }
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("<--- %s pfnAcquireMonitorSourceModeSet failed with Status = 0x%X, TargetId = %llu\n",
                  __FUNCTION__,
                  Status,
                  LONG_PTR(TargetId)));
        return;
    }

    VIOGPU_DISP_MODE modes[VIOGPU_MAX_MODES + 1];
    BOOLEAN listed[VIOGPU_MAX_MODES + 1] = {};
    ULONG count = m_pHWDevice->GetController()->GetModes(TargetId, modes);
    ULONG inSet = 0;

    CONST D3DKMDT_MONITOR_SOURCE_MODE *pMode = NULL;
    Status = pMonitorSourceModeSetInterface->pfnAcquireFirstModeInfo(hMonitorSourceModeSet, &pMode);
    while (Status == STATUS_SUCCESS && pMode != NULL)
    {
        ++inSet;
        for (ULONG Idx = 0; Idx < count; ++Idx)
        {
            if (pMode->VideoSignalInfo.ActiveSize.cx == modes[Idx].XResolution &&
                pMode->VideoSignalInfo.ActiveSize.cy == modes[Idx].YResolution)
            {
                listed[Idx] = TRUE;
            }
        }
        CONST D3DKMDT_MONITOR_SOURCE_MODE *pNext = NULL;
        Status = pMonitorSourceModeSetInterface->pfnAcquireNextModeInfo(hMonitorSourceModeSet, pMode, &pNext);
        pMonitorSourceModeSetInterface->pfnReleaseModeInfo(hMonitorSourceModeSet, pMode);
        pMode = pNext;
    }
    if (Status != STATUS_SUCCESS && Status != STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET &&
        Status != STATUS_GRAPHICS_DATASET_IS_EMPTY)
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("<--- %s walking the monitor's modes failed with Status = 0x%X, TargetId = %llu\n",
                  __FUNCTION__,
                  Status,
                  LONG_PTR(TargetId)));
    }
    else
    {
        Status = STATUS_SUCCESS;
        for (ULONG Idx = 0; NT_SUCCESS(Status) && Idx < count; ++Idx)
        {
            if (!listed[Idx])
            {
                DbgPrint(TRACE_LEVEL_INFORMATION,
                         ("%s: target %llu, %u modes in the monitor's set, %ux%u missing\n",
                          __FUNCTION__,
                          LONG_PTR(TargetId),
                          inSet,
                          modes[Idx].XResolution,
                          modes[Idx].YResolution));
                Status = AddMonitorMode(hMonitorSourceModeSet,
                                        pMonitorSourceModeSetInterface,
                                        &modes[Idx],
                                        D3DKMDT_MP_NOTPREFERRED);
            }
        }
    }
    pMonitorInterface->pfnReleaseMonitorSourceModeSet(m_DxgkInterface.DeviceHandle, hMonitorSourceModeSet);
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

                D3DKMDT_2DREGION PinnedTargetSize = {};
                BOOLEAN TargetPinned = FALSE;
                Status = GetPinnedTargetSize(pEnumCofuncModality->hConstrainingVidPn,
                                             pVidPnInterface,
                                             pVidPnPresentPath->VidPnTargetId,
                                             &PinnedTargetSize,
                                             &TargetPinned);
                if (NT_SUCCESS(Status))
                {
                    Status = AddSingleSourceMode(pVidPnSourceModeSetInterface,
                                                 hVidPnSourceModeSet,
                                                 pVidPnPresentPath->VidPnTargetId,
                                                 TargetPinned ? &PinnedTargetSize : NULL);
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
                    // pfnAssignSourceModeSet released the set, unless a parameter was invalid.
                    if (Status != STATUS_GRAPHICS_INVALID_VIDPN &&
                        Status != STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE &&
                        Status != STATUS_GRAPHICS_INVALID_VIDPN_SOURCEMODESET)
                    {
                        hVidPnSourceModeSet = 0;
                    }
                    break;
                }
                hVidPnSourceModeSet = 0;
            }
        }

        AddMissingMonitorModes(pVidPnPresentPath->VidPnTargetId);

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
                                             pVidPnPresentPath->VidPnTargetId,
                                             pVidPnPinnedSourceModeInfo != NULL ? &pVidPnPinnedSourceModeInfo->Format.Graphics.PrimSurfSize
                                                                                : NULL);

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
                    // pfnAssignTargetModeSet released the set, unless a parameter was invalid.
                    if (Status != STATUS_GRAPHICS_INVALID_VIDPN &&
                        Status != STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET &&
                        Status != STATUS_GRAPHICS_INVALID_VIDPN_TARGETMODESET)
                    {
                        hVidPnTargetModeSet = 0;
                    }
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
                // The driver scans out the source at the source mode's size.
                LocalVidPnPresentPath.ContentTransformation.ScalingSupport.Identity = 1;
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

    VioGpuDisplayController *pController = m_pHWDevice->GetController();
    for (UINT SourceId = 0; SourceId < pController->GetSourceCount(); ++SourceId)
    {
        if (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL ||
            pSetVidPnSourceVisibility->VidPnSourceId == SourceId)
        {
            pController->SetVisible(SourceId, pSetVidPnSourceVisibility->Visible);
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN *CONST pCommitVidPn)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pCommitVidPn != NULL);
    VIOGPU_ASSERT(pCommitVidPn->AffectedVidPnSourceId < MAX_VIEWS ||
                  pCommitVidPn->AffectedVidPnSourceId == D3DDDI_ID_ALL);

    if (pCommitVidPn->Flags.PathPoweredOff)
    {
        return STATUS_SUCCESS;
    }

    CONST DXGK_VIDPN_INTERFACE *pVidPnInterface = NULL;
    NTSTATUS Status = m_DxgkInterface.DxgkCbQueryVidPnInterface(pCommitVidPn->hFunctionalVidPn,
                                                                DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                                &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("DxgkCbQueryVidPnInterface failed with Status = 0x%X, hFunctionalVidPn = 0x%llu\n",
                  Status,
                  LONG_PTR(pCommitVidPn->hFunctionalVidPn)));
        return Status;
    }

    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *pVidPnTopologyInterface = NULL;
    Status = pVidPnInterface->pfnGetTopology(pCommitVidPn->hFunctionalVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnGetTopology failed with Status = 0x%X, hFunctionalVidPn = 0x%llu\n",
                  Status,
                  LONG_PTR(pCommitVidPn->hFunctionalVidPn)));
        return Status;
    }

    // The affected source, or every source for D3DDDI_ID_ALL, read into what the VidPN asks of it; the controller
    // checks them all before it changes any.
    VioGpuDisplayController *pController = m_pHWDevice->GetController();
    VIOGPU_SOURCE_STATE States[MAX_VIEWS];
    RtlZeroMemory(States, sizeof(States));
    ULONG Sources = 0;
    for (UINT SourceId = 0; SourceId < pController->GetSourceCount(); ++SourceId)
    {
        if (pCommitVidPn->AffectedVidPnSourceId != D3DDDI_ID_ALL && pCommitVidPn->AffectedVidPnSourceId != SourceId)
        {
            continue;
        }
        Status = ReadSourceState(pCommitVidPn->hFunctionalVidPn,
                                 pVidPnInterface,
                                 hVidPnTopology,
                                 pVidPnTopologyInterface,
                                 SourceId,
                                 &States[SourceId]);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_FATAL,
                     ("<--- %s source %d refused (0x%x): nothing changed\n", __FUNCTION__, SourceId, Status));
            return Status;
        }
        Sources |= 1UL << SourceId;
    }
    Status = pController->Commit(Sources, States, pCommitVidPn->MonitorConnectivityChecks == D3DKMDT_MCC_ENFORCE);
    if (NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_INFORMATION,
                 ("<--- %s source 0x%x committed\n", __FUNCTION__, pCommitVidPn->AffectedVidPnSourceId));
    }
    return Status;
}

// Reads what a functional VidPN asks of one source: the mode pinned on it and the target of its path, or no path when
// the source has none or no pinned mode. Refuses a source with more than one path, as the driver does not clone
// (IsSupportedVidPn), and a mode or a path the driver does not take.
NTSTATUS VioGpuDod::ReadSourceState(D3DKMDT_HVIDPN hVidPn,
                                    _In_ CONST DXGK_VIDPN_INTERFACE *pVidPnInterface,
                                    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
                                    _In_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE *pVidPnTopologyInterface,
                                    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
                                    _Out_ PVIOGPU_SOURCE_STATE pState)
{
    PAGED_CODE();

    D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *pPinnedVidPnSourceModeInfo = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPath = NULL;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId = D3DDDI_ID_UNINITIALIZED;

    RtlZeroMemory(pState, sizeof(*pState));
    pState->ScanoutId = D3DDDI_ID_UNINITIALIZED;

    SIZE_T NumPathsFromSource = 0;
    NTSTATUS Status = pVidPnTopologyInterface->pfnGetNumPathsFromSource(hVidPnTopology, SourceId, &NumPathsFromSource);
    if (Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY)
    {
        NumPathsFromSource = 0;
        Status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnGetNumPathsFromSource failed with Status = 0x%X, SourceId = %d\n", Status, SourceId));
        return Status;
    }
    if (NumPathsFromSource > 1)
    {
        // The driver does not clone (IsSupportedVidPn).
        DbgPrint(TRACE_LEVEL_ERROR, ("%s source %d has %Iu paths\n", __FUNCTION__, SourceId, NumPathsFromSource));
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    }
    if (NumPathsFromSource == 0)
    {
        return STATUS_SUCCESS;
    }

    Status = pVidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                                                      SourceId,
                                                      &hVidPnSourceModeSet,
                                                      &pVidPnSourceModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnAcquireSourceModeSet failed with Status = 0x%X, SourceId = %d\n", Status, SourceId));
        return Status;
    }
    Status = pVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnSourceModeSet, &pPinnedVidPnSourceModeInfo);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnAcquirePinnedModeInfo failed with Status = 0x%X, SourceId = %d\n", Status, SourceId));
        goto ReadSourceStateExit;
    }
    if (pPinnedVidPnSourceModeInfo == NULL)
    {
        goto ReadSourceStateExit;
    }

    Status = IsVidPnSourceModeFieldsValid(pPinnedVidPnSourceModeInfo);
    if (!NT_SUCCESS(Status))
    {
        goto ReadSourceStateExit;
    }
    {
        // The frame buffer has the primary surface's size, and presents fill its visible part.
        CONST D3DKMDT_GRAPHICS_RENDERING_FORMAT *pFormat = &pPinnedVidPnSourceModeInfo->Format.Graphics;
        if (pFormat->VisibleRegionSize.cx > pFormat->PrimSurfSize.cx ||
            pFormat->VisibleRegionSize.cy > pFormat->PrimSurfSize.cy)
        {
            DbgPrint(TRACE_LEVEL_ERROR,
                     ("%s source %d: a %ux%u surface cannot show %ux%u\n",
                      __FUNCTION__,
                      SourceId,
                      pFormat->PrimSurfSize.cx,
                      pFormat->PrimSurfSize.cy,
                      pFormat->VisibleRegionSize.cx,
                      pFormat->VisibleRegionSize.cy));
            Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
            goto ReadSourceStateExit;
        }
        pState->Width = (ULONG)pFormat->PrimSurfSize.cx;
        pState->Height = (ULONG)pFormat->PrimSurfSize.cy;
    }

    Status = pVidPnTopologyInterface->pfnEnumPathTargetsFromSource(hVidPnTopology, SourceId, 0, &TargetId);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnEnumPathTargetsFromSource failed with Status = 0x%X, SourceId = %d\n", Status, SourceId));
        goto ReadSourceStateExit;
    }
    Status = pVidPnTopologyInterface->pfnAcquirePathInfo(hVidPnTopology, SourceId, TargetId, &pVidPnPresentPath);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR,
                 ("pfnAcquirePathInfo failed with Status = 0x%X, SourceId = %d, TargetId = %d\n",
                  Status,
                  SourceId,
                  TargetId));
        goto ReadSourceStateExit;
    }
    Status = IsVidPnPathFieldsValid(pVidPnPresentPath);
    if (!NT_SUCCESS(Status))
    {
        goto ReadSourceStateExit;
    }
    // The source modes on offer have a visible region of the surface's size, and paths scale by identity only.
    pState->HasPath = TRUE;
    pState->ScanoutId = pVidPnPresentPath->VidPnTargetId;
    pState->Rotation = pVidPnPresentPath->ContentTransformation.Rotation;

ReadSourceStateExit:
    if (pVidPnPresentPath != NULL)
    {
        NTSTATUS TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPath);
        UNREFERENCED_PARAMETER(TempStatus);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }
    if (pPinnedVidPnSourceModeInfo != NULL)
    {
        NTSTATUS TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet,
                                                                               pPinnedVidPnSourceModeInfo);
        UNREFERENCED_PARAMETER(TempStatus);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }
    if (hVidPnSourceModeSet != 0)
    {
        NTSTATUS TempStatus = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hVidPnSourceModeSet);
        UNREFERENCED_PARAMETER(TempStatus);
        NT_ASSERT(NT_SUCCESS(TempStatus));
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

    m_pHWDevice->GetController()->SetRotation(pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnSourceId,
                                              pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.ContentTransformation.Rotation);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

PAGED_CODE_SEG_END

//
// Non-Paged Code
//
#pragma code_seg(push)
#pragma code_seg()

// Every DPC calls DxgkCbNotifyDpc. DXGKCB_NOTIFY_INTERRUPT requires it from the DPC an ISR queues after notifying
// dxgkrnl, with no exception for a device closed in between, and one DPC can stand for several ISRs (one is queued per
// adapter at a time), so a DPC cannot tell whether such an ISR queued it. Only the adapter's own work depends on the
// device's state.
VOID VioGpuDod::DpcRoutine(VOID)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    if (m_pHWDevice != NULL)
    {
        m_pHWDevice->DpcRoutine(&m_DxgkInterface);
    }
    m_DxgkInterface.DxgkCbNotifyDpc((HANDLE)m_DxgkInterface.DeviceHandle);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

// Under the interrupt lock, or with no interrupt connected: the ISR reaches no adapter from here on.
BOOLEAN VioGpuDod::DetachAdapterCb(PVOID Context)
{
    WritePointerNoFence((PVOID *)&reinterpret_cast<VioGpuDod *>(Context)->m_pHWDevice, NULL);
    return TRUE;
}

BOOLEAN VioGpuDod::InterruptRoutine(_In_ ULONG MessageNumber)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s\n", __FUNCTION__));
    // Read once: DeleteAdapter clears it under one message's lock at a time.
    VioGpuAdapter *pHWDevice = (VioGpuAdapter *)ReadPointerNoFence((PVOID *)&m_pHWDevice);
    return pHWDevice ? pHWDevice->InterruptRoutine(&m_DxgkInterface, MessageNumber) : FALSE;
}

VOID VioGpuDod::ResetDevice(VOID)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));
    if (m_pHWDevice != NULL)
    {
        m_pHWDevice->ResetDevice();
    }
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

    m_SystemDisplay.Enabled = FALSE;
    if (m_pHWDevice != NULL)
    {
        m_pHWDevice->ResetToVgaMode();
    }

    if (m_SystemDisplay.FrameBuffer == nullptr)
    {
        return STATUS_UNSUCCESSFUL;
    }

    m_SystemDisplay.Enabled = TRUE;
    *pWidth = m_SystemDisplay.DispInfo.Width = m_SystemDisplayInfo.Width;
    *pHeight = m_SystemDisplay.DispInfo.Height = m_SystemDisplayInfo.Height;
    *pColorFormat = m_SystemDisplay.DispInfo.ColorFormat = m_SystemDisplayInfo.ColorFormat;
    m_SystemDisplay.DispInfo.Pitch = m_SystemDisplayInfo.Pitch = (BPPFromPixelFormat(m_SystemDisplayInfo.ColorFormat) /
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
    if (m_SystemDisplay.Enabled)
    {
        RECT Rect = {0};
        BLT_INFO SrcBltInfo = {0};
        BLT_INFO DstBltInfo = {0};

        Rect.left = PositionX;
        Rect.top = PositionY;
        Rect.right = Rect.left + SourceWidth;
        Rect.bottom = Rect.top + SourceHeight;

        DstBltInfo.pBits = m_SystemDisplay.FrameBuffer;
        DstBltInfo.Pitch = m_SystemDisplay.DispInfo.Pitch;
        DstBltInfo.BitsPerPel = BPPFromPixelFormat(m_SystemDisplay.DispInfo.ColorFormat);
        DstBltInfo.Offset.x = 0;
        DstBltInfo.Offset.y = 0;
        DstBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
        DstBltInfo.Width = m_SystemDisplay.DispInfo.Width;
        DstBltInfo.Height = m_SystemDisplay.DispInfo.Height;

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

// Saves scanout 0's persistent mode, Width x Height, in the registry, where the next start reads it (GetRegisterInfo).
NTSTATUS VioGpuDod::SetRegisterConfigInfo(USHORT Width, USHORT Height)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    DWORD value = 0;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

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
        if (Width > 0 && Height > 0)
        {
            value = Width;
            Status = WriteRegistryDWORD(DevInstRegKeyHandle, L"PersistentDispMode0Width", &value);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("WriteRegistryDWORD failed for PersistentDispMode0Width with Status: 0x%X", Status));
                break;
            }

            value = Height;
            Status = WriteRegistryDWORD(DevInstRegKeyHandle, L"PersistentDispMode0Height", &value);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR,
                         ("WriteRegistryDWORD failed for PersistentDispMode0Height with Status: 0x%X", Status));
                break;
            }
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
    NTSTATUS StatusOptional = STATUS_SUCCESS;
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

    // The following keys are optional and no need to report error if them are missing
    value = 0;
    StatusOptional = ReadRegistryDWORD(DevInstRegKeyHandle, L"PersistentDispMode0Width", &value);
    if (NT_SUCCESS(StatusOptional))
    {
        SetPersistentDispMode0Width((USHORT)value);
    }

    value = 0;
    StatusOptional = ReadRegistryDWORD(DevInstRegKeyHandle, L"PersistentDispMode0Height", &value);
    if (NT_SUCCESS(StatusOptional))
    {
        SetPersistentDispMode0Height((USHORT)value);
    }

    ZwClose(DevInstRegKeyHandle);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

VioGpuAdapter::VioGpuAdapter(_In_ VioGpuDod *pVioGpuDod)
{
    PAGED_CODE();
    m_Commands.Init(&m_CtrlQueue, &m_CursorQueue);
    m_Resources.Init(&m_Commands);
    RtlZeroMemory(&m_VioDev, sizeof(m_VioDev));
    m_pVioGpuDod = pVioGpuDod;
    m_Controller.Init(this);
    m_Id = g_InstanceId++;
    m_PendingWorks = 0;
    m_InterruptsOn = FALSE;
    m_DroppedWorks = 0;
    // Closed until VioGpuAdapterInit opens the device.
    m_bDeviceOpen = FALSE;
    m_Cursor.Init(this);
    m_HelperChannel.Init(this);
    m_Controller.AddListener(&m_HelperChannel);
    m_HostDisplays.Init(this);
    // VioGpuHostDisplays last: its thread reads the host only once every other part is open, and is idle before any
    // of them closes.
    VioGpuComponent *Components[] = {&m_Controller, &m_Cursor, &m_HelperChannel, &m_HostDisplays};
    C_ASSERT(ARRAYSIZE(Components) <= VIOGPU_MAX_COMPONENTS);
    for (m_ComponentCount = 0; m_ComponentCount < ARRAYSIZE(Components); ++m_ComponentCount)
    {
        m_Components[m_ComponentCount] = Components[m_ComponentCount];
    }
    m_ComponentsStarted = 0;
    m_bComponentsOpen = FALSE;
    m_u64HostFeatures = 0;
    m_u64GuestFeatures = 0;
    m_u32NumCapsets = 0;
    m_u32NumScanouts = 0;
}

VioGpuAdapter::~VioGpuAdapter(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s 0x%p\n", __FUNCTION__, this));
    Shutdown();
    m_Id = 0;
    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
}

// Closes the components and what the adapter keeps open with the device, resets the device and stops the components;
// the destructor frees the rest. The ISR and the DPC must still reach the adapter: the
// config-change thread and the teardown commands wait for the device's answers, and an INTx line has to be
// acknowledged until the device is reset. It may run more than once.
void VioGpuAdapter::Shutdown(void)
{
    PAGED_CODE();
    // The config-change thread uses the device, the display state and the resolution event: it pauses first, and stops
    // once the device is reset, before the helper channel closes the event.
    CloseComponents();
    VioGpuAdapterClose();
    StopComponents();
}

// Starts the components, in list order. When one fails, those started before it are stopped.
NTSTATUS VioGpuAdapter::StartComponents(void)
{
    PAGED_CODE();
    for (; m_ComponentsStarted < m_ComponentCount; ++m_ComponentsStarted)
    {
        NTSTATUS status = m_Components[m_ComponentsStarted]->Start();
        if (!NT_SUCCESS(status))
        {
            DbgPrint(TRACE_LEVEL_FATAL,
                     ("%s component %d failed to start, 0x%x\n", __FUNCTION__, m_ComponentsStarted, status));
            StopComponents();
            return status;
        }
    }
    return STATUS_SUCCESS;
}

// Opens the started components, in list order, once the device is open.
void VioGpuAdapter::OpenComponents(void)
{
    PAGED_CODE();
    ASSERT(m_bDeviceOpen);
    // A D0 while the device is open already finds them open: a second Open would re-arm what a component holds, such
    // as a rundown its thread may hold.
    if (m_bComponentsOpen)
    {
        return;
    }
    for (UINT i = 0; i < m_ComponentsStarted; ++i)
    {
        m_Components[i]->Open();
    }
    m_bComponentsOpen = TRUE;
}

// Closes the components, in the opposite order, before the device is reset. Nothing when they are not open.
void VioGpuAdapter::CloseComponents(void)
{
    PAGED_CODE();
    if (!m_bComponentsOpen)
    {
        return;
    }
    m_bComponentsOpen = FALSE;
    for (UINT i = m_ComponentsStarted; i-- > 0;)
    {
        m_Components[i]->Close();
    }
}

// Stops the started components, in the opposite order, when they are not open.
void VioGpuAdapter::StopComponents(void)
{
    PAGED_CODE();
    ASSERT(!m_bComponentsOpen);
    while (m_ComponentsStarted > 0)
    {
        m_Components[--m_ComponentsStarted]->Stop();
    }
}

NTSTATUS VioGpuAdapter::VioGpuAdapterInit(void)
{
    PAGED_CODE();
    NTSTATUS status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (m_bDeviceOpen)
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
            DbgPrint(TRACE_LEVEL_FATAL, ("%s the device does not offer VIRTIO_F_VERSION_1\n", __FUNCTION__));
            status = STATUS_UNSUCCESSFUL;
            break;
        }

        AckFeature(VIRTIO_F_ACCESS_PLATFORM);
        AckFeature(VIRTIO_RING_F_INDIRECT_DESC);
        // GET_EDID may be sent only once the feature is negotiated.
        AckFeature(VIRTIO_GPU_F_EDID);

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

        BOOLEAN indirect = virtio_is_feature_enabled(m_u64GuestFeatures, VIRTIO_RING_F_INDIRECT_DESC);
        if (!m_CtrlQueue.Init(&m_VioDev, vqs[0], 0, indirect) || !m_CursorQueue.Init(&m_VioDev, vqs[1], 1, indirect))
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
        // Handling is switched on before DRIVER_OK: the device sends no used-buffer notification before it, and QEMU no
        // configuration change either. So the first interrupt is handled, and nothing of this open is pending when
        // InterruptsOnCb clears m_PendingWorks.
        SwitchInterrupts(TRUE);
        virtio_device_ready(&m_VioDev);
        m_bDeviceOpen = TRUE;
    }
    else
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s failed with 0x%x: the device is marked failed\n", __FUNCTION__, status));
        // What the open made goes, so that the next D0 starts over: the queues, which no interrupt reached, and the
        // rings, if they were found.
        m_CtrlQueue.Close();
        m_CursorQueue.Close();
        virtio_delete_queues(&m_VioDev);
        virtio_add_status(&m_VioDev, VIRTIO_CONFIG_S_FAILED);
        VioGpuDbgBreak();
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return status;
}

NTSTATUS VioGpuAdapter::SetPowerState(DXGK_DEVICE_INFO *pDeviceInfo, DEVICE_POWER_STATE DevicePowerState)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s DevicePowerState = %d\n", __FUNCTION__, DevicePowerState));
    UNREFERENCED_PARAMETER(pDeviceInfo);

    switch (DevicePowerState)
    {
        case PowerDeviceUnspecified:
        case PowerDeviceD0:
            {
                // DxgkDdiSetPowerState must not fail, so a device that does not come back only stays closed, and so
                // do its components.
                NTSTATUS status = VioGpuAdapterInit();
                if (NT_SUCCESS(status))
                {
                    OpenComponents();
                }
                else
                {
                    DbgPrint(TRACE_LEVEL_FATAL, ("%s the device stays closed at D0, 0x%x\n", __FUNCTION__, status));
                }
            }
            break;
        case PowerDeviceD1:
        case PowerDeviceD2:
        case PowerDeviceD3:
            {
                // The reset drops every resource the host has: the frame buffers and the cursor go as their components
                // close.
                CloseComponents();
                VioGpuAdapterClose();
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

NTSTATUS VioGpuAdapter::HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION *pDispInfo)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;
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

        status = VioGpuAdapterInit();
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

    } while (0);
    // Exit if the block above failed
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    // BAR 0, where the firmware's frame buffer is, recorded in the current mode. Frame buffers get their own pages, or
    // BAR 0's with UsePhysicalMemory (CreateFrameBuffer).
    PHYSICAL_ADDRESS fb_pa = m_PciResources.GetPciBar(0)->GetPA();
    if (fb_pa.QuadPart != 0LL)
    {
        pDispInfo->PhysicAddress = fb_pa;
    }

    // The components start first: the display controller takes scanout 0's persistent mode, which the display read
    // applies. The displays are read before the components open, so that dxgkrnl's first questions about the children
    // find them. A change the host signals meanwhile, and a re-read InitDisplays posts, wait for the config-change
    // thread in m_WorkPosted, which uses the device only once its component opens.
    status = StartComponents();
    if (NT_SUCCESS(status))
    {
        InitDisplay(pDispInfo);
        OpenComponents();
    }
    return status;
}

// The device's configuration events, VIRTIO_GPU_EVENT_*.
UINT32 VioGpuAdapter::ReadConfigEvents(void)
{
    UINT32 events = 0;
    virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_read), &events, sizeof(events));
    return events;
}

void VioGpuAdapter::ClearConfigEvents(UINT32 Events)
{
    virtio_set_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_clear), &Events, sizeof(Events));
}

// A mode the driver offers and accepts: no larger than 8192x8192, the limit Linux's virtio-gpu driver uses too. Its
// pages are allocated when it is set, and the set fails when they cannot be (VioGpuMemSegment::Init).
BOOLEAN VioGpuAdapter::ModeFits(ULONG Width, ULONG Height)
{
    PAGED_CODE();

    return Width <= MAX_WIDTH_SIZE && Height <= MAX_HEIGHT_SIZE;
}

// Sets up the display at start, before the config-change thread runs: the firmware's frame buffer as dxgkrnl knows it,
// and the host's displays (VioGpuHostDisplays::InitDisplays).
void VioGpuAdapter::InitDisplay(DXGK_DISPLAY_INFORMATION *pDispInfo)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    pDispInfo->Height = max(pDispInfo->Height, MIN_HEIGHT_SIZE);
    pDispInfo->Width = max(pDispInfo->Width, MIN_WIDTH_SIZE);
    pDispInfo->ColorFormat = D3DDDIFMT_X8R8G8B8;
    pDispInfo->Pitch = (BPPFromPixelFormat(pDispInfo->ColorFormat) / BITS_PER_BYTE) * pDispInfo->Width;

    m_HostDisplays.InitDisplays();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

PAGED_CODE_SEG_END

BOOLEAN VioGpuAdapter::ResetToVgaMode(void)
{
    m_Controller.DropFrameBuffers();
    VioGpuAdapterClose();
    return TRUE;
}

void VioGpuAdapter::VioGpuAdapterClose()
{
    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s\n", __FUNCTION__));

    if (m_bDeviceOpen)
    {
        m_bDeviceOpen = FALSE;
        m_CtrlQueue.DisableInterrupt();
        m_CursorQueue.DisableInterrupt();
        // Reset the device before switching interrupt handling off: after the reset it raises nothing, and it lowers
        // an INTx line it may still hold, which the ISR does not acknowledge while handling is off. Once handling is
        // off the ISR returns at once, and flushing the DPCs lets those it queued finish before the queues hand the
        // commands left on the rings their outcome and the rings are deleted. At DISPATCH_LEVEL and above (only the
        // bugcheck flow, ResetToVgaMode) DPCs are not flushed, none run, and the commands stay on the rings.
        virtio_device_reset(&m_VioDev);
        SwitchInterrupts(FALSE);
        if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        {
            KeFlushQueuedDpcs();
        }
        m_CtrlQueue.Close();
        m_CursorQueue.Close();
        // Above DISPATCH_LEVEL the reset is all: no memory may be freed there.
        if (KeGetCurrentIrql() <= DISPATCH_LEVEL)
        {
            virtio_delete_queues(&m_VioDev);
            virtio_device_shutdown(&m_VioDev);
        }
    }
    // After the reset the host no longer uses the pages of the objects whose release got no answer.
    if (!m_bDeviceOpen && KeGetCurrentIrql() <= APC_LEVEL)
    {
        m_Resources.FreeReleased();
    }
    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuAdapter::InterruptsOnCb(PVOID Context)
{
    VioGpuAdapter *pThis = reinterpret_cast<VioGpuAdapter *>(Context);
    // A DPC that ran after the last close switched handling off left the ISR's causes pending. They refer to rings
    // deleted at that close; the device is not DRIVER_OK yet, so it has completed nothing of this open.
    pThis->m_DroppedWorks |= pThis->m_PendingWorks;
    pThis->m_PendingWorks = 0;
    pThis->m_InterruptsOn = TRUE;
    return TRUE;
}

BOOLEAN VioGpuAdapter::InterruptsOffCb(PVOID Context)
{
    reinterpret_cast<VioGpuAdapter *>(Context)->m_InterruptsOn = FALSE;
    return TRUE;
}

// Switches interrupt handling under the interrupt lock of each message in turn (the routine runs once per message), so
// that an ISR already running finishes first and the next one sees the change. DxgkCbSynchronizeExecution is callable
// only at DISPATCH_LEVEL or below. When no lock is taken, no ISR runs alongside: the interrupt is not connected (before
// StartDevice connects it, after StopDevice disconnects it), or the system is bugchecking. Some messages locked and
// others not would leave an ISR unfenced, which is logged.
void VioGpuAdapter::SwitchInterrupts(BOOLEAN On)
{
    PDXGKRNL_INTERFACE pDxgkInterface = GetDxgkInterface();
    PKSYNCHRONIZE_ROUTINE Routine = On ? InterruptsOnCb : InterruptsOffCb;
    ULONG Messages = GetMessageCount();
    ULONG Synchronized = 0;
    NTSTATUS Failure = STATUS_SUCCESS;

    if (KeGetCurrentIrql() <= DISPATCH_LEVEL)
    {
        for (ULONG Message = 0; Message < Messages; Message++)
        {
            BOOLEAN Ret = FALSE;
            NTSTATUS Status = pDxgkInterface->DxgkCbSynchronizeExecution(pDxgkInterface->DeviceHandle,
                                                                         Routine,
                                                                         this,
                                                                         Message,
                                                                         &Ret);
            if (NT_SUCCESS(Status))
            {
                Synchronized++;
            }
            else
            {
                Failure = Status;
            }
        }
    }
    if (Synchronized == 0)
    {
        Routine(this);
    }
    // A local: DbgPrint does not parenthesize its level.
    UCHAR TraceLevel = (Synchronized != 0 && !NT_SUCCESS(Failure)) ? TRACE_LEVEL_FATAL : TRACE_LEVEL_INFORMATION;
    DbgPrint(TraceLevel,
             ("<--> %s %d: %lu of %lu message(s) synchronized (0x%x)\n",
              __FUNCTION__,
              On,
              Synchronized,
              Messages,
              Failure));
    if (m_DroppedWorks != 0)
    {
        DbgPrint(TRACE_LEVEL_WARNING,
                 ("%s: causes 0x%x of the last open were not handled before its close\n",
                  __FUNCTION__,
                  m_DroppedWorks));
        m_DroppedWorks = 0;
    }
}

BOOLEAN VioGpuAdapter::InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_ ULONG MessageNumber)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s MessageNumber = %d\n", __FUNCTION__, MessageNumber));
    BOOLEAN serviced = TRUE;
    ULONG intReason = 0;

    if (!m_InterruptsOn)
    {
        return FALSE;
    }

    if (m_PciResources.IsMSIEnabled() && !m_PciResources.IsMessagePerCause())
    {
        // Every cause shares message 0, which no other device sends. Both queues are checked on every interrupt, and
        // the ISR status register tells a configuration change: the device sets its bit before it sends one, with
        // MSI-X too.
        if (MessageNumber == 0)
        {
            UCHAR isrstat = virtio_read_isr_status(&m_VioDev);
            intReason = ISR_REASON_DISPLAY | ISR_REASON_CURSOR;
            if (isrstat != 0xFF && (isrstat & VIRTIO_PCI_ISR_CONFIG))
            {
                intReason |= ISR_REASON_CHANGE;
            }
        }
        else
        {
            serviced = FALSE;
            DbgPrint(TRACE_LEVEL_FATAL,
                     ("---> %s message %d, all causes are on message 0\n", __FUNCTION__, MessageNumber));
        }
    }
    else if (m_PciResources.IsMSIEnabled())
    {
        switch (MessageNumber)
        {
            case VIRTIO_GPU_MSIX_CONFIG_VECTOR:
                intReason = ISR_REASON_CHANGE;
                break;
            case VIRTIO_GPU_MSIX_CTRL_VECTOR:
                intReason = ISR_REASON_DISPLAY;
                break;
            case VIRTIO_GPU_MSIX_CURSOR_VECTOR:
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
        // The line may be shared. The ISR status register has one bit per cause. The bits accumulate until the driver
        // reads the register, which returns and clears them all and lowers the line, so one read can report both.
        UCHAR isrstat = virtio_read_isr_status(&m_VioDev);
        // A device that is gone reads all ones. Only bits 0 and 1 are defined, so a live device does not report all
        // ones: the interrupt is another device's on the shared line.
        if (isrstat == 0xFF)
        {
            return FALSE;
        }

        if (isrstat & VIOGPU_ISR_QUEUE)
        {
            intReason |= ISR_REASON_DISPLAY | ISR_REASON_CURSOR;
        }
        if (isrstat & VIRTIO_PCI_ISR_CONFIG)
        {
            intReason |= ISR_REASON_CHANGE;
        }
        serviced = (intReason != 0);
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

VOID VioGpuAdapter::DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(pDxgkInterface);
    ULONG reason;
    if (!m_InterruptsOn)
    {
        return;
    }
    while ((reason = InterlockedExchange((PLONG)&m_PendingWorks, 0)) != 0)
    {
        if ((reason & ISR_REASON_DISPLAY))
        {
            m_CtrlQueue.Complete();
        }
        if ((reason & ISR_REASON_CURSOR))
        {
            m_CursorQueue.Complete();
        }
        if (reason & ISR_REASON_CHANGE)
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("---> %s ConfigChanged\n", __FUNCTION__));
            m_HostDisplays.Post(VIOGPU_WORK_EVENT);
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
PAGED_CODE_SEG_END

PDXGKRNL_INTERFACE VioGpuAdapter::GetDxgkInterface()
{
    return m_pVioGpuDod->GetDxgkInterface();
}
