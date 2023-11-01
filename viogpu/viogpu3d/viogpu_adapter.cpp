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

#include "helper.h"
#include "driver.h"
#include "viogpu_adapter.h"
#include "baseobj.h"
#include "bitops.h"
#include "viogpum.h"
#include "viogpu_device.h"
#if !DBG
#include "viogpudo.tmh"
#endif

static UINT g_InstanceId = 0;

struct NOTIFY_CONTEXT {
    DXGKRNL_INTERFACE* pDxgkInterface;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA* interrupt;
    BOOL triggerDpc;
};

BOOLEAN NotifyRoutine(PVOID ctx_void) {
    //DbgPrint(TRACE_LEVEL_ERROR, ("<---> %s\n", __FUNCTION__));
    NOTIFY_CONTEXT* ctx = (NOTIFY_CONTEXT*)ctx_void;
    DXGKRNL_INTERFACE* pDxgkInterface = ctx->pDxgkInterface;
    pDxgkInterface->DxgkCbNotifyInterrupt(pDxgkInterface->DeviceHandle, ctx->interrupt);
    if (ctx->triggerDpc) {
        pDxgkInterface->DxgkCbQueueDpc(pDxgkInterface->DeviceHandle);
    }

    return TRUE;
}

NTSTATUS VioGpuAdapter::NotifyInterrupt(DXGKARGCB_NOTIFY_INTERRUPT_DATA* interruptData, BOOL triggerDpc) {
    NOTIFY_CONTEXT notify;
    notify.pDxgkInterface = &m_DxgkInterface;
    notify.interrupt = interruptData;
    notify.triggerDpc = triggerDpc;
    BOOLEAN bRet;
    return m_DxgkInterface.DxgkCbSynchronizeExecution(
        m_DxgkInterface.DeviceHandle,
        NotifyRoutine,
        &notify,
        0,
        &bRet
    );
}


virtio_gpu_formats ColorFormat(UINT format)
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


VioGpuAdapter::VioGpuAdapter(_In_ DEVICE_OBJECT* pPhysicalDeviceObject) : m_pPhysicalDevice(pPhysicalDeviceObject),
m_MonitorPowerState(PowerDeviceD0),
m_AdapterPowerState(PowerDeviceD0),
commander(this), vidpn(this)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    *((UINT*)&m_Flags) = 0;
    RtlZeroMemory(&m_DxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(&m_DeviceInfo, sizeof(m_DeviceInfo));
    RtlZeroMemory(&m_PointerShape, sizeof(m_PointerShape));
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    RtlZeroMemory(&m_VioDev, sizeof(m_VioDev));
    m_Id = g_InstanceId++;
    m_PendingWorks = 0;
    KeInitializeEvent(&m_ConfigUpdateEvent,
        SynchronizationEvent,
        FALSE);
    m_bStopWorkThread = FALSE;
    m_pWorkThread = NULL;
    m_ResolutionEvent = NULL;
    m_ResolutionEventHandle = NULL;
    m_u32NumCapsets = 0;
    m_u32NumScanouts = 0;
}

VioGpuAdapter::~VioGpuAdapter(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    CloseResolutionEvent();
    VioGpuAdapterClose();
    HWClose();
    m_Id = 0;
}

BOOLEAN VioGpuAdapter::CheckHardware()
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_GRAPHICS_DRIVER_MISMATCH;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PCI_COMMON_HEADER Header = { 0 };
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
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s VendorId = 0x%04X DeviceId = 0x%04X\n", __FUNCTION__, Header.VendorID, Header.DeviceID));
    if (Header.VendorID == REDHAT_PCI_VENDOR_ID &&
        Header.DeviceID == 0x1050)
    {
        SetVgaDevice(Header.SubClass == PCI_SUBCLASS_VID_VGA_CTLR);
        return TRUE;
    }

    return FALSE;
}

#pragma warning(disable: 4702)
NTSTATUS VioGpuAdapter::StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
    _In_  DXGKRNL_INTERFACE* pDxgkInterface,
    _Out_ ULONG*             pNumberOfViews,
    _Out_ ULONG*             pNumberOfChildren)
{
    PAGED_CODE();

    NTSTATUS Status;
    VIOGPU_ASSERT(pDxgkStartInfo != NULL);
    VIOGPU_ASSERT(pDxgkInterface != NULL);
    VIOGPU_ASSERT(pNumberOfViews != NULL);
    VIOGPU_ASSERT(pNumberOfChildren != NULL);
    RtlCopyMemory(&m_DxgkInterface, pDxgkInterface, sizeof(m_DxgkInterface));

    Status = m_DxgkInterface.DxgkCbGetDeviceInformation(m_DxgkInterface.DeviceHandle, &m_DeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        VIOGPU_LOG_ASSERTION1("DxgkCbGetDeviceInformation failed with status 0x%X\n",
            Status);
        return Status;
    }

    if (!CheckHardware())
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

    Status = HWInit(m_DeviceInfo.TranslatedResourceList);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("HWInit failed with status 0x%X\n", Status));
        return Status;
    }

    if (!AckFeature(VIRTIO_GPU_F_VIRGL)) {
        DbgPrint(TRACE_LEVEL_ERROR, ("VioGpu3D cannot start because virgl is not enabled\n"));
        return STATUS_UNSUCCESSFUL;
    }

    Status = SetRegisterInfo(GetInstanceId(), 0);
    if (!NT_SUCCESS(Status))
    {
        VIOGPU_LOG_ASSERTION1("RegisterHWInfo failed with status 0x%X\n",
            Status);
        return Status;
    }

    commander.Start();
    Status = vidpn.Start(pNumberOfViews, pNumberOfChildren);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("VioGpuaVidPN::Start failed with status 0x%X\n", Status));
        VioGpuDbgBreak();
        return STATUS_UNSUCCESSFUL;
    }

    m_Flags.DriverStarted = TRUE;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAdapter::StopDevice(VOID)
{
    PAGED_CODE();
    commander.Stop();

    m_Flags.DriverStarted = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAdapter::DispatchIoRequest(_In_  ULONG VidPnSourceId,
    _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(pVideoRequestPacket);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

PCHAR
DbgDevicePowerString(
    __in DEVICE_POWER_STATE Type
)
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
DbgPowerActionString(
    __in POWER_ACTION Type
)
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

NTSTATUS VioGpuAdapter::SetPowerState(_In_  ULONG HardwareUid,
    _In_  DEVICE_POWER_STATE DevicePowerState,
    _In_  POWER_ACTION       ActionType)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ActionType);

    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s HardwareUid = 0x%x ActionType = %s DevicePowerState = %s AdapterPowerState = %s\n",
        __FUNCTION__, HardwareUid, DbgPowerActionString(ActionType), DbgDevicePowerString(DevicePowerState), DbgDevicePowerString(m_AdapterPowerState)));

    if (HardwareUid == DISPLAY_ADAPTER_HW_ID)
    {
        if (DevicePowerState == PowerDeviceD0)
        {
            vidpn.AcquirePostDisplayOwnership();

            if (m_AdapterPowerState == PowerDeviceD3)
            {
                DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
                Visibility.VidPnSourceId = D3DDDI_ID_ALL;
                Visibility.Visible = FALSE;
                vidpn.SetVidPnSourceVisibility(&Visibility);
            }
            m_AdapterPowerState = DevicePowerState;
        }


        switch (DevicePowerState)
        {
        case PowerDeviceUnspecified:
        case PowerDeviceD0: {
            VioGpuAdapterInit();
        } break;
        case PowerDeviceD1:
        case PowerDeviceD2:
        case PowerDeviceD3: {
            vidpn.Powerdown();
            VioGpuAdapterClose();
        } break;
        }
        return STATUS_SUCCESS;
    }
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAdapter::QueryChildRelations(_Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
    _In_  ULONG  ChildRelationsSize)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    VIOGPU_ASSERT(pChildRelations != NULL);

    ULONG ChildRelationsCount = (ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR)) - 1;
    VIOGPU_ASSERT(ChildRelationsCount <= MAX_CHILDREN);

    for (UINT ChildIndex = 0; ChildIndex < ChildRelationsCount; ++ChildIndex)
    {
        pChildRelations[ChildIndex].ChildDeviceType = TypeVideoOutput;
        pChildRelations[ChildIndex].ChildCapabilities.HpdAwareness = IsVgaDevice() ? HpdAwarenessAlwaysConnected : HpdAwarenessInterruptible;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = IsVgaDevice() ? D3DKMDT_VOT_INTERNAL : D3DKMDT_VOT_HD15;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
        pChildRelations[ChildIndex].AcpiUid = 0;
        pChildRelations[ChildIndex].ChildUid = ChildIndex;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAdapter::QueryChildStatus(_Inout_ DXGK_CHILD_STATUS* pChildStatus,
    _In_    BOOLEAN            NonDestructiveOnly)
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
        DbgPrint(TRACE_LEVEL_ERROR, ("Child status being queried for StatusRotation even though D3DKMDT_MOA_NONE was reported"));
        return STATUS_INVALID_PARAMETER;
    }

    default:
    {
        DbgPrint(TRACE_LEVEL_WARNING, ("Unknown pChildStatus->Type (0x%I64x) requested.", pChildStatus->Type));
        return STATUS_NOT_SUPPORTED;
    }
    }
}

NTSTATUS VioGpuAdapter::QueryDeviceDescriptor(_In_    ULONG                   ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pDeviceDescriptor != NULL);
    VIOGPU_ASSERT(ChildUid < MAX_CHILDREN);
    PBYTE edid = vidpn.GetEdidData(ChildUid);
    
    if (!edid)
    {
        return STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED;
    }
    else if (pDeviceDescriptor->DescriptorOffset < EDID_V1_BLOCK_SIZE)
    {
        ULONG len = min(pDeviceDescriptor->DescriptorLength, (EDID_V1_BLOCK_SIZE - pDeviceDescriptor->DescriptorOffset));
        RtlCopyMemory(pDeviceDescriptor->DescriptorBuffer, (edid + pDeviceDescriptor->DescriptorOffset), len);
        pDeviceDescriptor->DescriptorLength = len;
        return STATUS_SUCCESS;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
}

NTSTATUS VioGpuAdapter::QueryAdapterInfo(_In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pQueryAdapterInfo != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    switch (pQueryAdapterInfo->Type)
    {
    case DXGKQAITYPE_UMDRIVERPRIVATE: {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(VIOGPU_ADAPTERINFO)) {
            DbgPrint(TRACE_LEVEL_ERROR, ("pQueryAdapterInfo->OutputDataSize (0x%u) is smaller than sizeof(VIOGPU_ADAPTERINFO) (0x%u)\n", pQueryAdapterInfo->OutputDataSize, sizeof(VIOGPU_ADAPTERINFO)))
            return STATUS_BUFFER_TOO_SMALL;
        }
        VIOGPU_ADAPTERINFO* info = (VIOGPU_ADAPTERINFO*)pQueryAdapterInfo->pOutputData;
        info->IamVioGPU = VIOGPU_IAM;
        info->Flags.Supports3d = virtio_is_feature_enabled(m_u64HostFeatures, VIRTIO_GPU_F_VIRGL);
        info->Flags.Reserved = 0;
        info->SupportedCapsetIDs = m_supportedCapsetIDs;
        return STATUS_SUCCESS;
    }
    case DXGKQAITYPE_DRIVERCAPS:
    {
        if (!pQueryAdapterInfo->OutputDataSize)
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pQueryAdapterInfo->OutputDataSize (0x%u) is smaller than sizeof(DXGK_DRIVERCAPS) (0x%u)\n", pQueryAdapterInfo->OutputDataSize, sizeof(DXGK_DRIVERCAPS)));
            return STATUS_BUFFER_TOO_SMALL;
        }

        DXGK_DRIVERCAPS* pDriverCaps = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;
        DbgPrint(TRACE_LEVEL_ERROR, ("InterruptMessageNumber = %d, WDDMVersion = %d\n",
            pDriverCaps->InterruptMessageNumber, pDriverCaps->WDDMVersion));
        RtlZeroMemory(pDriverCaps, pQueryAdapterInfo->OutputDataSize/*sizeof(DXGK_DRIVERCAPS)*/);
        pDriverCaps->WDDMVersion = DXGKDDI_WDDMv1_3;
        pDriverCaps->HighestAcceptableAddress.QuadPart = (ULONG64)-1;


        pDriverCaps->FlipCaps.FlipOnVSyncMmIo = TRUE;

        pDriverCaps->MaxQueuedFlipOnVSync = 1;

        pDriverCaps->MemoryManagementCaps.SectionBackedPrimary = TRUE;
     
        pDriverCaps->SupportDirectFlip = 1;
        pDriverCaps->SchedulingCaps.MultiEngineAware = 1;
        pDriverCaps->SchedulingCaps.PreemptionAware = 1;

        pDriverCaps->GpuEngineTopology.NbAsymetricProcessingNodes = 1;

        pDriverCaps->SupportSmoothRotation = FALSE;
        pDriverCaps->SupportNonVGA = IsVgaDevice();

        // Disable pointer on viogpu3d for now
        //if (IsPointerEnabled()) {
        //    pDriverCaps->MaxPointerWidth = POINTER_SIZE;
        //    pDriverCaps->MaxPointerHeight = POINTER_SIZE;
        //    pDriverCaps->PointerCaps.Value = 0;
        //    pDriverCaps->PointerCaps.Color = 1;
        //}


        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s Driver caps return\n", __FUNCTION__));
        return STATUS_SUCCESS;
    }
    case DXGKQAITYPE_QUERYSEGMENT3:
    {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT3))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pQueryAdapterInfo->OutputDataSize (0x%u) is smaller than sizeof(DXGK_QUERYSEGMENTOUT) (0x%u)\n", pQueryAdapterInfo->OutputDataSize, sizeof(DXGK_QUERYSEGMENTOUT)));
            return STATUS_BUFFER_TOO_SMALL;
        }

        DbgPrint(TRACE_LEVEL_ERROR, ("QUERY SEG\n"));
        DXGK_QUERYSEGMENTOUT3* pSegmentInfo = (DXGK_QUERYSEGMENTOUT3*)pQueryAdapterInfo->pOutputData;
        if (!pSegmentInfo[0].pSegmentDescriptor)
        {
            pSegmentInfo->NbSegment = 1;
        }
        else {
            DXGK_SEGMENTDESCRIPTOR3* pSegmentDesc = pSegmentInfo->pSegmentDescriptor;
            memset(&pSegmentDesc[0], 0, sizeof(pSegmentDesc[0]));

            pSegmentInfo->PagingBufferPrivateDataSize = 0;

            pSegmentInfo->PagingBufferSegmentId = 1;
            pSegmentInfo->PagingBufferSize = 10 * PAGE_SIZE;

            //
            // Fill out aperture segment descriptor
            //
            memset(&pSegmentDesc[0], 0, sizeof(pSegmentDesc[0]));

            pSegmentDesc[0].BaseAddress.QuadPart = 0xC0000000;
            pSegmentDesc[0].Flags.Aperture = TRUE;
            pSegmentDesc[0].Flags.CacheCoherent = TRUE;
            //pSegmentDesc[0].CpuTranslatedAddress.QuadPart = 0xFFFFFFFE00000000;

            pSegmentDesc[0].Flags.CpuVisible = FALSE; 
            
            
            //pSegmentDesc[0].Flags.DirectFlip = TRUE;
            pSegmentDesc[0].Size = 256 * 1024 * 4096;
            pSegmentDesc[0].CommitLimit = 256 * 1024 * 4096;

            pSegmentDesc[0].Flags.DirectFlip = TRUE;
        }
        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s Requested segments\n", __FUNCTION__));
        return STATUS_SUCCESS;
    }

    default:
    {
        DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s unknown type %d\n", __FUNCTION__, pQueryAdapterInfo->Type));
        return STATUS_NOT_SUPPORTED;
    }
    }
}

NTSTATUS VioGpuAdapter::Escape(_In_ CONST DXGKARG_ESCAPE* pEscape)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pEscape != NULL);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s Flags = %d\n", __FUNCTION__, pEscape->Flags.Value));
    PAGED_CODE();
    PVIOGPU_ESCAPE  pVioGpuEscape = (PVIOGPU_ESCAPE)pEscape->pPrivateDriverData;
    NTSTATUS        status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(pVioGpuEscape);
    
    UINT size = pEscape->PrivateDriverDataSize;
    if (size < sizeof(PVIOGPU_ESCAPE))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("%s buffer too small %d, should be at least %d\n", __FUNCTION__,
            pEscape->PrivateDriverDataSize, size));
        return STATUS_INVALID_BUFFER_SIZE;
    }

    switch (pVioGpuEscape->Type) {
    case VIOGPU_GET_DEVICE_ID: {
        CreateResolutionEvent();
        size = sizeof(ULONG);
        if (pVioGpuEscape->DataLength < size) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s buffer too small %d, should be at least %d\n", __FUNCTION__,
                pVioGpuEscape->DataLength, size));
            return STATUS_INVALID_BUFFER_SIZE;
        }
        pVioGpuEscape->Id = m_Id;
        break;
    }
    case VIOGPU_GET_CUSTOM_RESOLUTION: {
        size = sizeof(VIOGPU_DISP_MODE);
        if (pVioGpuEscape->DataLength < size) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s buffer too small %d, should be at least %d\n", __FUNCTION__,
                pVioGpuEscape->DataLength, size));
            return STATUS_INVALID_BUFFER_SIZE;
        }
        vidpn.EscapeCustomResoulution(&pVioGpuEscape->Resolution);
        break;
    }
    case VIOGPU_GET_CAPS: {
        size = sizeof(VIOGPU_CAPSET_REQ);
        if (pVioGpuEscape->DataLength < size) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s buffer too small %d, should be at least %d\n", __FUNCTION__,
                pVioGpuEscape->DataLength, size));
            return STATUS_INVALID_BUFFER_SIZE;
        }

        if (!(m_supportedCapsetIDs & (1ull << pVioGpuEscape->Capset.CapsetId))) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s capset id is not supported\n", __FUNCTION__));
            return STATUS_INVALID_PARAMETER_1;
        }
        CAPSET_INFO* pCapsetInfo = &m_capsetInfos[pVioGpuEscape->Capset.CapsetId];
        if (pCapsetInfo->max_version < pVioGpuEscape->Capset.Version) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s capset version is too low\n", __FUNCTION__));
            return STATUS_INVALID_PARAMETER_2;
        };

        PGPU_VBUFFER vbuf;
        ctrlQueue.AskCapset(&vbuf, pVioGpuEscape->Capset.CapsetId, pCapsetInfo->max_size, pVioGpuEscape->Capset.Version);
        UCHAR* buf = ((PGPU_RESP_CAPSET)vbuf->resp_buf)->capset_data;
        ULONG to_copy = min(pVioGpuEscape->Capset.Size, pCapsetInfo->max_size);
        __try {
            memcpy(pVioGpuEscape->Capset.Capset, buf, to_copy);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to copy"));
            status = STATUS_INVALID_PARAMETER;
        }
        ctrlQueue.ReleaseBuffer(vbuf);

        break;
    }
    case VIOGPU_RES_INFO: {
        size = sizeof(VIOGPU_RES_INFO_REQ);
        if (pVioGpuEscape->DataLength < size) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s buffer too small %d, should be at least %d\n", __FUNCTION__,
                pVioGpuEscape->DataLength, size));
            return STATUS_INVALID_BUFFER_SIZE;
        }
        VioGpuAllocation* allocation = AllocationFromHandle(pVioGpuEscape->ResourceInfo.ResHandle);
        if(allocation == NULL) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s ivalid handle\n", __FUNCTION__));
            return STATUS_INVALID_PARAMETER;
        }

        status = allocation->EscapeResourceInfo(&pVioGpuEscape->ResourceInfo);
        
        break;
    }
    case VIOGPU_RES_BUSY: {
        size = sizeof(VIOGPU_RES_BUSY_REQ);
        if (pVioGpuEscape->DataLength < size) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s buffer too small %d, should be at least %d\n", __FUNCTION__,
                pVioGpuEscape->DataLength, size));
            return STATUS_INVALID_BUFFER_SIZE;
        }
        VioGpuAllocation* allocation = AllocationFromHandle(pVioGpuEscape->ResourceBusy.ResHandle);
        if (allocation == NULL) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s ivalid handle\n", __FUNCTION__));
            return STATUS_INVALID_PARAMETER;
        }
        status = allocation->EscapeResourceBusy(&pVioGpuEscape->ResourceBusy);

        break;
    }
    case VIOGPU_CTX_INIT: {
        size = sizeof(VIOGPU_CTX_INIT_REQ);
        if (pVioGpuEscape->DataLength < size) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s buffer too small %d, should be at least %d\n", __FUNCTION__,
                pVioGpuEscape->DataLength, size));
            return STATUS_INVALID_BUFFER_SIZE;
        }
        VioGpuDevice* context = reinterpret_cast<VioGpuDevice*>(pEscape->hDevice);
        if (context == NULL) {
            DbgPrint(TRACE_LEVEL_ERROR, ("%s no hDdevice(context) supplied\n", __FUNCTION__));
            return STATUS_INVALID_PARAMETER;
        }
        context->Init(&pVioGpuEscape->CtxInit);
        break;
    }

    default:
        DbgPrint(TRACE_LEVEL_ERROR, ("%s: invalid Escape type 0x%x\n", __FUNCTION__, pVioGpuEscape->Type));
        status = STATUS_INVALID_PARAMETER;
    }

    return status;
}

NTSTATUS VioGpuAdapter::QueryInterface(_In_ CONST PQUERY_INTERFACE pQueryInterface)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pQueryInterface != NULL);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s Version = %d\n", __FUNCTION__, pQueryInterface->Version));

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS VioGpuAdapter::StopDeviceAndReleasePostDisplayOwnership(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION*      pDisplayInfo)
{
    PAGED_CODE();


    VIOGPU_ASSERT(TargetId < MAX_CHILDREN);
//FIXME!!!
    if (m_MonitorPowerState > PowerDeviceD0)
    {
        SetPowerState(TargetId, PowerDeviceD0, PowerActionNone);
    }
    vidpn.ReleasePostDisplayOwnership(TargetId, pDisplayInfo);
    return StopDevice();
}

PAGED_CODE_SEG_END

//
// Non-Paged Code
//
#pragma code_seg(push)
#pragma code_seg()

VOID VioGpuAdapter::DpcRoutine(VOID)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    PGPU_VBUFFER pvbuf = NULL;
    UINT len = 0;
    ULONG reason;
    while ((reason = InterlockedExchange((PLONG)&m_PendingWorks, 0)) != 0)
    {
        if ((reason & ISR_REASON_DISPLAY)) {
            while ((pvbuf = ctrlQueue.DequeueBuffer(&len)) != NULL)
            {
                DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s ctrlQueue pvbuf = %p len = %d\n", __FUNCTION__, pvbuf, len));
                PGPU_CTRL_HDR pcmd = (PGPU_CTRL_HDR)pvbuf->buf;
                PGPU_CTRL_HDR resp = (PGPU_CTRL_HDR)pvbuf->resp_buf;
                
                if (resp->type >= VIRTIO_GPU_RESP_ERR_UNSPEC) {
                    DbgPrint(TRACE_LEVEL_FATAL, ("!!!!! Command failed %d", resp->type));
                }
                if (resp->type != VIRTIO_GPU_RESP_OK_NODATA)
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s type = %xlu flags = %lu fence_id = %llu ctx_id = %lu cmd_type = %lu\n",
                        __FUNCTION__, resp->type, resp->flags, resp->fence_id, resp->ctx_id, pcmd->type));
                }
                if (pvbuf->complete_cb != NULL)
                {
                    pvbuf->complete_cb(pvbuf->complete_ctx);
                }
                if (pvbuf->auto_release) {
                    ctrlQueue.ReleaseBuffer(pvbuf);
                }
            };
        }
        if ((reason & ISR_REASON_CURSOR)) {
            while ((pvbuf = m_CursorQueue.DequeueCursor(&len)) != NULL)
            {
                DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s m_CursorQueue pvbuf = %p len = %u\n", __FUNCTION__, pvbuf, len));
                m_CursorQueue.ReleaseBuffer(pvbuf);
            };
        }
        if (reason & ISR_REASON_CHANGE) {
            DbgPrint(TRACE_LEVEL_FATAL, ("---> %s ConfigChanged\n", __FUNCTION__));
            KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    m_DxgkInterface.DxgkCbNotifyDpc((HANDLE)m_DxgkInterface.DeviceHandle);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VOID VioGpuAdapter::ResetDevice(VOID)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));
}

#pragma code_seg(pop) // End Non-Paged Code

PAGED_CODE_SEG_BEGIN
NTSTATUS VioGpuAdapter::WriteRegistryString(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue)
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

NTSTATUS VioGpuAdapter::WriteRegistryDWORD(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PDWORD pdwValue)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    UNICODE_STRING UnicodeStrValueName;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    RtlInitUnicodeString(&UnicodeStrValueName, pszwValueName);

    Status = ZwSetValueKey(DevInstRegKeyHandle,
        &UnicodeStrValueName,
        0,
        REG_DWORD,
        pdwValue,
        sizeof(DWORD));

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("ZwSetValueKey failed with Status: 0x%X\n", Status));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuAdapter::ReadRegistryDWORD(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _Inout_ PDWORD pdwValue)
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
            ASSERT(Buf.Info.DataLength == sizeof(DWORD));
            *pdwValue = *((PDWORD) &(((PKEY_VALUE_PARTIAL_INFORMATION)Buf)->Data));
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

NTSTATUS VioGpuAdapter::SetRegisterInfo(_In_ ULONG Id, _In_ DWORD MemSize)
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
        DbgPrint(TRACE_LEVEL_ERROR, ("IoOpenDeviceRegistryKey failed for PDO: 0x%p, Status: 0x%X", m_pPhysicalDevice, Status));
        return Status;
    }

    do {
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

NTSTATUS VioGpuAdapter::GetRegisterInfo(void)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    HANDLE DevInstRegKeyHandle;
    Status = IoOpenDeviceRegistryKey(m_pPhysicalDevice, PLUGPLAY_REGKEY_DRIVER, KEY_READ, &DevInstRegKeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("IoOpenDeviceRegistryKey failed for PDO: 0x%p, Status: 0x%X", m_pPhysicalDevice, Status));
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
    if (!NT_SUCCESS(Status))
    {
        SetUsePhysicalMemory(!!value);
    }

    ZwClose(DevInstRegKeyHandle);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}
PAGED_CODE_SEG_END

PAGED_CODE_SEG_BEGIN



NTSTATUS VioGpuAdapter::VioGpuAdapterInit()
{
    PAGED_CODE();
    NTSTATUS status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (IsHardwareInit()) {
        DbgPrint(TRACE_LEVEL_FATAL, ("Already Initialized\n"));
        VioGpuDbgBreak();
        return status;
    }
    status = VirtIoDeviceInit();
    if (!NT_SUCCESS(status)) {
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
#if (NTDDI_VERSION >= NTDDI_WIN10)
        AckFeature(VIRTIO_F_ACCESS_PLATFORM);
#endif


        if (!AckFeature(VIRTIO_F_VERSION_1))
        {
            status = STATUS_UNSUCCESSFUL;
            break;
        }

        status = virtio_set_features(&m_VioDev, m_u64GuestFeatures);
        if (!NT_SUCCESS(status))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s virtio_set_features failed with %x\n", __FUNCTION__, status));
            VioGpuDbgBreak();
            break;
        }

        status = virtio_find_queues(
            &m_VioDev,
            2,
            vqs);
        if (!NT_SUCCESS(status)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("virtio_find_queues failed with error %x\n", status));
            VioGpuDbgBreak();
            break;
        }

        if (!ctrlQueue.Init(&m_VioDev, vqs[0], 0) ||
            !m_CursorQueue.Init(&m_VioDev, vqs[1], 1)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize virtio queues\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }

        virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, num_scanouts),
            &m_u32NumScanouts, sizeof(m_u32NumScanouts));

        virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, num_capsets),
            &m_u32NumCapsets, sizeof(m_u32NumCapsets));
    } while (0);
    if (status == STATUS_SUCCESS)
    {
        virtio_device_ready(&m_VioDev);
        SetHardwareInit(TRUE);
    }
    else
    {
        virtio_add_status(&m_VioDev, VIRTIO_CONFIG_S_FAILED);
        VioGpuDbgBreak();
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return status;
}

void VioGpuAdapter::VioGpuAdapterClose()
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s\n", __FUNCTION__));

    if (IsHardwareInit())
    {
        SetHardwareInit(FALSE);
        ctrlQueue.DisableInterrupt();
        m_CursorQueue.DisableInterrupt();
        virtio_device_reset(&m_VioDev);
        virtio_delete_queues(&m_VioDev);
        ctrlQueue.Close();
        m_CursorQueue.Close();
        virtio_device_shutdown(&m_VioDev);
    }
    DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
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

    return virtio_device_initialize(
        &m_VioDev,
        &VioGpuSystemOps,
        reinterpret_cast<IVioGpuPCI*>(this),
        m_PciResources.IsMSIEnabled());
}



VOID VioGpuAdapter::CreateResolutionEvent(VOID)
{
    PAGED_CODE();

    if (m_ResolutionEvent != NULL &&
        m_ResolutionEventHandle != NULL)
    {
        return;
    }
    DECLARE_UNICODE_STRING_SIZE(DeviceNumber, 10);
    DECLARE_UNICODE_STRING_SIZE(EventName, 256);

    RtlIntegerToUnicodeString(m_Id, 10, &DeviceNumber);
    NTSTATUS status = RtlUnicodeStringPrintf(
        &EventName,
        L"%ws%ws%ws",
        BASE_NAMED_OBJECTS,
        RESOLUTION_EVENT_NAME,
        DeviceNumber.Buffer
    );
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("RtlUnicodeStringPrintf failed 0x%x\n", status));
        return;
    }
    m_ResolutionEvent = IoCreateNotificationEvent(&EventName, &m_ResolutionEventHandle);
    if (m_ResolutionEvent == NULL) {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--> %s\n", __FUNCTION__));
        return;
    }
    KeClearEvent(m_ResolutionEvent);
    ObReferenceObject(m_ResolutionEvent);
}

VOID VioGpuAdapter::NotifyResolutionEvent(VOID)
{
    PAGED_CODE();

    if (m_ResolutionEvent != NULL) {
        DbgPrint(TRACE_LEVEL_ERROR, ("NotifyResolutionEvent\n"));
        KeSetEvent(m_ResolutionEvent, IO_NO_INCREMENT, FALSE);
        KeClearEvent(m_ResolutionEvent);
    }
}

VOID VioGpuAdapter::CloseResolutionEvent(VOID)
{
    PAGED_CODE();

    if (m_ResolutionEventHandle != NULL) {
        ZwClose(m_ResolutionEventHandle);
        m_ResolutionEventHandle = NULL;
    }

    if (m_ResolutionEvent != NULL) {
        ObDereferenceObject(m_ResolutionEvent);
        m_ResolutionEvent = NULL;
    }
}

NTSTATUS VioGpuAdapter::HWInit(PCM_RESOURCE_LIST pResList)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;
    HANDLE   threadHandle = 0;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    UINT size = 0;
    do
    {
        if (!m_PciResources.Init(GetDxgkInterface(), pResList))
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



        size = ctrlQueue.QueryAllocation() + m_CursorQueue.QueryAllocation();
        DbgPrint(TRACE_LEVEL_FATAL, ("%s size %d\n", __FUNCTION__, size));
        ASSERT(size);

        if (!m_GpuBuf.Init(size)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize buffers\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }

        ctrlQueue.SetGpuBuf(&m_GpuBuf);
        m_CursorQueue.SetGpuBuf(&m_GpuBuf);

        if (!resourceIdr.Init(1)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize id generator\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }

        if (!ctxIdr.Init(1)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize id generator\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }
         
        m_supportedCapsetIDs = 0;
        for (UINT32 i = 0; i < m_u32NumCapsets; i++) {
            PGPU_VBUFFER vbuf = NULL;

            ctrlQueue.AskCapsetInfo(&vbuf, i);
            PGPU_RESP_CAPSET_INFO resp = (PGPU_RESP_CAPSET_INFO)vbuf->resp_buf;
            ULONG capset_id = resp->capset_id;
            if (capset_id > 63 || capset_id <= 0) continue; // Invalid capset id, capsets ids are in range from 1 to 63 per specification
            m_capsetInfos[capset_id].id = capset_id;
            m_capsetInfos[capset_id].max_size = resp->capset_max_size;
            m_capsetInfos[capset_id].max_version = resp->capset_max_version;
            m_supportedCapsetIDs |= 1ull << capset_id;
            DbgPrint(TRACE_LEVEL_FATAL, ("CAPSET INFO %d    id: %d; version: %d; size: %d\n", i, capset_id, resp->capset_max_size, resp->capset_max_version));
        }

    } while (0);
//FIXME!!! exit if the block above failed

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
    ObReferenceObjectByHandle(threadHandle,
        THREAD_ALL_ACCESS,
        NULL,
        KernelMode,
        (PVOID*)(&m_pWorkThread),
        NULL);

    ZwClose(threadHandle);

    PHYSICAL_ADDRESS fb_pa = m_PciResources.GetPciBar(0)->GetPA();
    UINT fb_size = (UINT)m_PciResources.GetPciBar(0)->GetSize();

    //FIXME
#if NTDDI_VERSION > NTDDI_WINBLUE
    UINT req_size = 0x1000000;
#else
    UINT req_size = 0x800000;
#endif

    if (!IsUsePhysicalMemory() ||
        fb_pa.QuadPart == 0 ||
        fb_size < req_size) {
        fb_pa.QuadPart = 0LL;
        fb_size = max (req_size, fb_size);
    }

    if (!frameSegment.Init(fb_size, &fb_pa))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s failed to allocate FB memory segment\n", __FUNCTION__));
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
    SetHardwareInit(FALSE);

    LARGE_INTEGER timeout = { 0 };
    timeout.QuadPart = Int32x32To64(1000, -10000);

    m_bStopWorkThread = TRUE;
    KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);

    if (KeWaitForSingleObject(m_pWorkThread,
        Executive,
        KernelMode,
        FALSE,
        &timeout) == STATUS_TIMEOUT) {
        DbgPrint(TRACE_LEVEL_FATAL, ("---> Failed to exit the worker thread\n"));
        VioGpuDbgBreak();
    }

    ObDereferenceObject(m_pWorkThread);
    
    frameSegment.Close();

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

BOOLEAN FindUpdateRect(
    _In_ ULONG             NumMoves,
    _In_ D3DKMT_MOVE_RECT* pMoves,
    _In_ ULONG             NumDirtyRects,
    _In_ PRECT             pDirtyRect,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
    _Out_ PRECT pUpdateRect)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Rotation);
    BOOLEAN updated = FALSE;

    if (pUpdateRect == NULL) return FALSE;

    if (NumMoves == 0 && NumDirtyRects == 0) {
        pUpdateRect->bottom = 0;
        pUpdateRect->left = 0;
        pUpdateRect->right = 0;
        pUpdateRect->top = 0;
    }

    for (ULONG i = 0; i < NumMoves; i++)
    {
        PRECT  pRect = &pMoves[i].DestRect;
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
        PRECT  pRect = &pDirtyRect[i];
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



NTSTATUS VioGpuAdapter::UpdateChildStatus(BOOLEAN connect)
{
    PAGED_CODE();
    NTSTATUS           Status(STATUS_SUCCESS);
    DXGK_CHILD_STATUS  ChildStatus;
    PDXGKRNL_INTERFACE pDXGKInterface(GetDxgkInterface());

    RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));

    ChildStatus.Type = StatusConnection;
    ChildStatus.ChildUid = 0;
    ChildStatus.HotPlug.Connected = connect;
    Status = pDXGKInterface->DxgkCbIndicateChildStatus(pDXGKInterface->DeviceHandle, &ChildStatus);
    if (Status != STATUS_SUCCESS)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s DxgkCbIndicateChildStatus failed with status %x\n ", __FUNCTION__, Status));
    }
    return Status;
}



PAGED_CODE_SEG_END



BOOLEAN VioGpuAdapter::InterruptRoutine(_In_  ULONG MessageNumber)
{
    if (!IsHardwareInit())  return FALSE;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s MessageNumber = %d\n", __FUNCTION__, MessageNumber));
    BOOLEAN serviced = TRUE;
    ULONG intReason = 0;
    //return FALSE;
    if (m_PciResources.IsMSIEnabled())
    {
        switch (MessageNumber) {
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
            DbgPrint(TRACE_LEVEL_FATAL, ("---> %s Unknown Interrupt Reason MessageNumber%d\n", __FUNCTION__, MessageNumber));
        }
    }
    else {
        UNREFERENCED_PARAMETER(MessageNumber);
        UCHAR  isrstat = virtio_read_isr_status(&m_VioDev);

        switch (isrstat) {
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

    if (serviced) {
        InterlockedOr((PLONG)&m_PendingWorks, intReason);
        m_DxgkInterface.DxgkCbQueueDpc(m_DxgkInterface.DeviceHandle);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return serviced;
}

void VioGpuAdapter::ThreadWork(_In_ PVOID Context)
{
    VioGpuAdapter* pdev = reinterpret_cast<VioGpuAdapter*>(Context);
    pdev->ThreadWorkRoutine();
}

void VioGpuAdapter::ThreadWorkRoutine(void)
{
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    for (;;)
    {
        KeWaitForSingleObject(&m_ConfigUpdateEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL);

        if (m_bStopWorkThread) {
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
    virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_read),
        &events_read, sizeof(m_u32NumScanouts));
    if (events_read & VIRTIO_GPU_EVENT_DISPLAY) {
        vidpn.GetDisplayInfo();
        events_clear |= VIRTIO_GPU_EVENT_DISPLAY;
        virtio_set_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_clear),
            &events_clear, sizeof(m_u32NumScanouts));
        //        UpdateChildStatus(FALSE);
        //        ProcessEdid();
        UpdateChildStatus(TRUE);
    }
}

VioGpuAllocation* VioGpuAdapter::AllocationFromHandle(D3DKMT_HANDLE handle) {
    DXGKARGCB_GETHANDLEDATA getHandleData;
    getHandleData.hObject = handle;
    getHandleData.Type = DXGK_HANDLE_ALLOCATION;
    getHandleData.Flags.DeviceSpecific = 0;
    return reinterpret_cast<VioGpuAllocation*>(m_DxgkInterface.DxgkCbGetHandleData(&getHandleData));
}

VioGpuResource* VioGpuAdapter::ResourceFromHandle(D3DKMT_HANDLE handle) {
    DXGKARGCB_GETHANDLEDATA getHandleData;
    getHandleData.hObject = handle;
    getHandleData.Type = DXGK_HANDLE_RESOURCE;
    getHandleData.Flags.DeviceSpecific = 0;
    return reinterpret_cast<VioGpuResource*>(m_DxgkInterface.DxgkCbGetHandleData(&getHandleData));
}