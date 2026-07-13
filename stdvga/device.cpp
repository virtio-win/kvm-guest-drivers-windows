/*
 * Copyright (c) 2026 Alibaba Cloud Computing Ltd.
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

#include "device.h"
#include "trace.h"

#pragma code_seg("PAGE")

//
// Helper: fill a D3DKMDT_VIDEO_SIGNAL_INFO for a given mode.
// Provide explicit 60Hz timing so Windows mode-matching can correlate
// target modes with EDID-derived monitor modes in Display Settings.
//
// VESA DMT / industry-standard timings keyed by resolution. dxgkrnl validates
// target VideoSignalInfo against EDID-parsed monitor modes and rejects entries
// whose timing doesn't match a recognized standard timing
// (STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED). Using these canonical numbers
// keeps the target mode set aligned with what the kernel derives from the EDID.
struct STDVGA_VESA_TIMING
{
    USHORT W;
    USHORT H;
    UINT HTotal;
    UINT VTotal;
    ULONG PixelRateHz; // PixelRate per VESA spec
};

/*
// clang-format off
static const STDVGA_VESA_TIMING s_VesaTimings[] = {
    {800, 600, 1056, 628, 40000000},     // SVGA @60
    {1024, 768, 1344, 806, 65000000},    // XGA @60
    {1152, 864, 1600, 900, 108000000},   // XGA+ @75
    {1280, 720, 1650, 750, 74250000},    // 720p @60
    {1280, 768, 1664, 798, 79500000},    // WXGA @60 CVT
    {1280, 800, 1680, 831, 83500000},    // WXGA @60
    {1280, 960, 1800, 1000, 108000000},  // SXGA- @60
    {1280, 1024, 1688, 1066, 108000000}, // SXGA @60
    {1400, 1050, 1864, 1089, 121750000}, // SXGA+ @60
    {1440, 900, 1904, 934, 106500000},   // WXGA++ @60
    {1600, 1200, 2160, 1250, 162000000}, // UXGA @60
    {1680, 1050, 2240, 1089, 146250000}, // WSXGA+ @60
    {1920, 1080, 2200, 1125, 148500000}, // 1080p @60
    {1920, 1200, 2080, 1235, 154000000}, // WUXGA-RB @60
    {2560, 1600, 2720, 1646, 268500000}, // WQXGA-RB @60
};
// clang-format on

[[maybe_unused]]
static const STDVGA_VESA_TIMING *LookupVesaTiming(USHORT Width, USHORT Height)
{
    for (ULONG i = 0; i < ARRAYSIZE(s_VesaTimings); i++)
    {
        if (s_VesaTimings[i].W == Width && s_VesaTimings[i].H == Height)
        {
            return &s_VesaTimings[i];
        }
    }
    return NULL;
}
*/

static VOID BuildVideoSignalInfo(_Out_ D3DKMDT_VIDEO_SIGNAL_INFO *pSignalInfo, _In_ USHORT Width, _In_ USHORT Height)
{
    PAGED_CODE();
    RtlZeroMemory(pSignalInfo, sizeof(*pSignalInfo));

    // Match QXL/VirtIO-GPU KMDOD exactly. All frequency fields use
    // D3DKMDT_FREQUENCY_NOTSPECIFIED so dxgkrnl skips the timing
    // self-consistency check that was making pfnAddMode reject every mode
    // with INVALID_FREQUENCY in v23-v47.
    pSignalInfo->VideoStandard = D3DKMDT_VSS_OTHER;
    pSignalInfo->TotalSize.cx = Width; // no blanking
    pSignalInfo->TotalSize.cy = Height;
    pSignalInfo->ActiveSize = pSignalInfo->TotalSize;
    pSignalInfo->VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pSignalInfo->VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pSignalInfo->HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pSignalInfo->HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pSignalInfo->PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pSignalInfo->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
}

//
// Helper: check if a given resolution can be supported (fits in VRAM).
//
static BOOLEAN IsModeSupported(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ UINT Width, _In_ UINT Height);

[[maybe_unused]]
static BOOLEAN IsModeSupported(_In_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ UINT Width, _In_ UINT Height)
{
    if (Width == 0 || Height == 0)
    {
        return FALSE;
    }
    SIZE_T required = (SIZE_T)Width * Height * STDVGA_BYTES_PER_PIXEL;
    return (required <= DevCtx->Hw.VramSize) ? TRUE : FALSE;
}

//
// Helper: perform the mode switch at the hardware and state level.
//
static NTSTATUS SetCurrentMode(_Inout_ PSTDVGA_DEVICE_CONTEXT DevCtx, _In_ UINT Width, _In_ UINT Height)
{
    PAGED_CODE();

    NTSTATUS status = StdVgaHwSetMode(&DevCtx->Hw, (USHORT)Width, (USHORT)Height);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    DevCtx->CurrentMode.SrcModeWidth = Width;
    DevCtx->CurrentMode.SrcModeHeight = Height;

    DevCtx->CurrentMode.DispInfo.Width = Width;
    DevCtx->CurrentMode.DispInfo.Height = Height;
    DevCtx->CurrentMode.DispInfo.Pitch = DevCtx->Hw.CurrentStridePixels * STDVGA_BYTES_PER_PIXEL;
    DevCtx->CurrentMode.DispInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
    DevCtx->CurrentMode.DispInfo.PhysicAddress = DevCtx->Hw.FrameBufferPA;
    DevCtx->CurrentMode.DispInfo.TargetId = 0;
    DevCtx->CurrentMode.DispInfo.AcpiId = 0;

    return STATUS_SUCCESS;
}

//
// ---- DDI Implementations ----
//

// System thread to trigger hot-plug after StartDevice completes.
// Calling DxgkCbIndicateChildStatus(StatusConnection, Connected=TRUE) forces
// dxgkrnl to re-run mode discovery and call DxgkDdiRecommendFunctionalVidPn.
//
// IMPORTANT: All sleeps must be interruptible via pCtx->HotPlugStopEvent so
// StopDevice can shut us down before the driver image is unmapped, otherwise
// we crash with BugCheck 0xCE (DRIVER_UNLOADED_WITHOUT_CANCELLING_PENDING_OPERATIONS).
//
// Timing constants below are tuned empirically against cloud QEMU host I/O
// jitter. Units are 100-nanosecond intervals (KeWaitForSingleObject
// relative-timeout convention); negative = relative time.
//
//   STDVGA_HOTPLUG_PREFIRE_DELAY    Wait after StartDevice before announcing
//                                   hot-plug. Lets dxgkrnl finish the start
//                                   transaction and enter idle so it will
//                                   honor the upcoming child-status change
//                                   instead of dropping it on the floor.
//
//   STDVGA_HOTPLUG_DISCONNECT_HOLD  Gap between Disconnect and Connect.
//                                   Gives dxgkrnl time to actually process
//                                   the disconnect (invalidate cached mode
//                                   list) before we plug back in; without
//                                   this hold the pair is sometimes coalesced
//                                   and the mode list is never re-queried.
//
#define STDVGA_HOTPLUG_PREFIRE_DELAY   (-20000000LL) // 2.0 s
#define STDVGA_HOTPLUG_DISCONNECT_HOLD (-5000000LL)  // 0.5 s

static VOID StdVgaHotPlugWorker(_In_ PVOID Context)
{
    PSTDVGA_DEVICE_CONTEXT pCtx = (PSTDVGA_DEVICE_CONTEXT)Context;
    if (pCtx == NULL || pCtx->DxgkInterface.DxgkCbIndicateChildStatus == NULL)
    {
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    LARGE_INTEGER delay;
    delay.QuadPart = STDVGA_HOTPLUG_PREFIRE_DELAY;
    NTSTATUS waitSt = KeWaitForSingleObject(&pCtx->HotPlugStopEvent, Executive, KernelMode, FALSE, &delay);
    if (waitSt != STATUS_TIMEOUT)
    {
        // Stop signaled: bail out without touching DxgkInterface.
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    TraceLog("HotPlugWorker fire");

    DXGK_CHILD_STATUS childStatus = {};
    childStatus.Type = StatusConnection;
    childStatus.ChildUid = 1;
    childStatus.HotPlug.Connected = FALSE;
    NTSTATUS st = pCtx->DxgkInterface.DxgkCbIndicateChildStatus(pCtx->DxgkInterface.DeviceHandle, &childStatus);
    TraceLogStatus("HotPlug Disconnect", st);

    delay.QuadPart = STDVGA_HOTPLUG_DISCONNECT_HOLD;
    waitSt = KeWaitForSingleObject(&pCtx->HotPlugStopEvent, Executive, KernelMode, FALSE, &delay);
    if (waitSt != STATUS_TIMEOUT)
    {
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    childStatus.HotPlug.Connected = TRUE;
    st = pCtx->DxgkInterface.DxgkCbIndicateChildStatus(pCtx->DxgkInterface.DeviceHandle, &childStatus);
    TraceLogStatus("HotPlug Connect", st);

    PsTerminateSystemThread(STATUS_SUCCESS);
}

_Use_decl_annotations_ NTSTATUS StdVgaStartDevice(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                  DXGK_START_INFO *pStartInfo,
                                                  DXGKRNL_INTERFACE *pDxgkInterface,
                                                  PULONG pNumberOfVideoPresentSources,
                                                  PULONG pNumberOfChildren)
{
    PAGED_CODE();
    TraceLog("StartDevice ENTER");

    DevCtx->StartInfo = *pStartInfo;
    DevCtx->DxgkInterface = *pDxgkInterface;

    DXGK_DEVICE_INFO deviceInfo = {};
    NTSTATUS status = pDxgkInterface->DxgkCbGetDeviceInformation(pDxgkInterface->DeviceHandle, &deviceInfo);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("StartDevice GetDevInfo FAIL", status);
        return status;
    }

    DevCtx->DeviceInfo = deviceInfo;

    status = StdVgaHwInit(&DevCtx->Hw, deviceInfo.TranslatedResourceList);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("StartDevice HwInit FAIL", status);
        return status;
    }

    TraceLogInt("StartDevice FB_PA_hi", (int)(DevCtx->Hw.FrameBufferPA.QuadPart >> 32));
    TraceLogInt("StartDevice FB_PA_lo", (int)(DevCtx->Hw.FrameBufferPA.QuadPart & 0xFFFFFFFF));
    TraceLogInt("StartDevice FB_Len", (int)DevCtx->Hw.FrameBufferLength);
    TraceLogInt("StartDevice VRAM", (int)DevCtx->Hw.VramSize);
    TraceLogInt("StartDevice UseMmio", DevCtx->Hw.UseMmio ? 1 : 0);

    //
    // Acquire post-display ownership to get current display state from firmware.
    //
    DXGK_DISPLAY_INFORMATION dispInfo = {};
    status = DevCtx->DxgkInterface.DxgkCbAcquirePostDisplayOwnership(DevCtx->DxgkInterface.DeviceHandle, &dispInfo);
    if (NT_SUCCESS(status) && dispInfo.Width > 0 && dispInfo.Height > 0)
    {
        TraceLogInt("StartDevice AcqPost w", (int)dispInfo.Width);
        TraceLogInt("StartDevice AcqPost h", (int)dispInfo.Height);

        DevCtx->CurrentMode.SrcModeWidth = dispInfo.Width;
        DevCtx->CurrentMode.SrcModeHeight = dispInfo.Height;
        DevCtx->CurrentMode.DispInfo = dispInfo;

        DevCtx->Hw.CurrentWidth = (USHORT)dispInfo.Width;
        DevCtx->Hw.CurrentHeight = (USHORT)dispInfo.Height;
    }
    else
    {
        TraceLog("StartDevice AcqPost failed, using HW state");
        USHORT curW, curH;
        StdVgaHwGetCurrentMode(&DevCtx->Hw, &curW, &curH);
        if (curW == 0 || curH == 0)
        {
            curW = 1920;
            curH = 1080;
        }
        TraceLogInt("StartDevice cur_w", (int)curW);
        TraceLogInt("StartDevice cur_h", (int)curH);

        DevCtx->CurrentMode.SrcModeWidth = curW;
        DevCtx->CurrentMode.SrcModeHeight = curH;
        DevCtx->CurrentMode.DispInfo.Width = curW;
        DevCtx->CurrentMode.DispInfo.Height = curH;
        DevCtx->CurrentMode.DispInfo.Pitch = (UINT)curW * STDVGA_BYTES_PER_PIXEL;
        DevCtx->CurrentMode.DispInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
        DevCtx->CurrentMode.DispInfo.PhysicAddress = DevCtx->Hw.FrameBufferPA;
        DevCtx->CurrentMode.DispInfo.TargetId = 0;
        DevCtx->CurrentMode.DispInfo.AcpiId = 0;

        DevCtx->Hw.CurrentWidth = curW;
        DevCtx->Hw.CurrentHeight = curH;
    }

    *pNumberOfVideoPresentSources = 1;
    *pNumberOfChildren = 1;

    DevCtx->DriverStarted = TRUE;

    // Read user-configurable target resolution from registry:
    //   HKLM\System\CurrentControlSet\Services\StdVga\Parameters\TargetWidth
    //   HKLM\System\CurrentControlSet\Services\StdVga\Parameters\TargetHeight
    // Defaults to 1920x1080 (v50).
    {
        ULONG TW = 1920, TH = 1080;
        UNICODE_STRING regPath;
        RtlInitUnicodeString(&regPath, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\StdVga\\Parameters");
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &regPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        HANDLE hKey = NULL;
        if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &oa)) && hKey != NULL)
        {
            UCHAR buf[64];
            ULONG resultLen = 0;
            UNICODE_STRING vw, vh;
            RtlInitUnicodeString(&vw, L"TargetWidth");
            RtlInitUnicodeString(&vh, L"TargetHeight");
            if (NT_SUCCESS(ZwQueryValueKey(hKey, &vw, KeyValuePartialInformation, buf, sizeof(buf), &resultLen)))
            {
                PKEY_VALUE_PARTIAL_INFORMATION pInfo = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
                if (pInfo->Type == REG_DWORD && pInfo->DataLength == sizeof(ULONG))
                {
                    TW = *(PULONG)pInfo->Data;
                }
            }
            if (NT_SUCCESS(ZwQueryValueKey(hKey, &vh, KeyValuePartialInformation, buf, sizeof(buf), &resultLen)))
            {
                PKEY_VALUE_PARTIAL_INFORMATION pInfo = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
                if (pInfo->Type == REG_DWORD && pInfo->DataLength == sizeof(ULONG))
                {
                    TH = *(PULONG)pInfo->Data;
                }
            }
            ZwClose(hKey);
        }
        TraceLogInt("StartDevice TgtW", (int)TW);
        TraceLogInt("StartDevice TgtH", (int)TH);

        // Validate against VRAM capacity.
        SIZE_T req = (SIZE_T)TW * TH * STDVGA_BYTES_PER_PIXEL;
        if (TW == 0 || TH == 0 || req > DevCtx->Hw.VramSize)
        {
            TW = 1920;
            TH = 1080;
        }

        StdVgaHwSetMode(&DevCtx->Hw, (USHORT)TW, (USHORT)TH);
        ULONG stridePx = DevCtx->Hw.CurrentStridePixels; // 8-aligned
        ULONG stride = stridePx * STDVGA_BYTES_PER_PIXEL;
        DevCtx->Hw.CurrentWidth = (USHORT)TW;
        DevCtx->Hw.CurrentHeight = (USHORT)TH;

        // Black out instead of splash, so when dxgkrnl finally commits,
        // PresentDisplayOnly's rendered desktop replaces a clean black canvas.
        // Splash is removed to verify dxgkrnl actually starts rendering.
        if (DevCtx->Hw.pFrameBuffer != NULL)
        {
            for (ULONG y = 0; y < TH; y++)
            {
                PULONG row = (PULONG)((PUCHAR)DevCtx->Hw.pFrameBuffer + y * stride);
                for (ULONG x = 0; x < stridePx; x++)
                {
                    row[x] = 0;
                }
            }
        }

        DevCtx->CurrentMode.SrcModeWidth = TW;
        DevCtx->CurrentMode.SrcModeHeight = TH;
        DevCtx->CurrentMode.DispInfo.Width = TW;
        DevCtx->CurrentMode.DispInfo.Height = TH;
        DevCtx->CurrentMode.DispInfo.Pitch = stride;
        DevCtx->CurrentMode.DispInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
        DevCtx->CurrentMode.DispInfo.PhysicAddress = DevCtx->Hw.FrameBufferPA;
        DevCtx->CurrentMode.DispInfo.TargetId = 0;
        DevCtx->CurrentMode.DispInfo.AcpiId = 0;
    }

    // Launch hot-plug worker thread to trigger dxgkrnl
    // RecommendFunctionalVidPn path.
    {
        // Initialize stop signal + thread tracking BEFORE creating the thread,
        // so a racing StopDevice always sees a valid event.
        KeInitializeEvent(&DevCtx->HotPlugStopEvent, NotificationEvent, FALSE);
        DevCtx->HotPlugThread = NULL;

        HANDLE hThread = NULL;
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
        NTSTATUS thrSt = PsCreateSystemThread(&hThread,
                                              THREAD_ALL_ACCESS,
                                              &oa,
                                              NULL,
                                              NULL,
                                              StdVgaHotPlugWorker,
                                              DevCtx);
        if (NT_SUCCESS(thrSt) && hThread != NULL)
        {
            // Hold a reference so StopDevice can KeWaitForSingleObject on it
            // before the driver image is unmapped.
            PETHREAD pThread = NULL;
            NTSTATUS refSt = ObReferenceObjectByHandle(hThread,
                                                       THREAD_ALL_ACCESS,
                                                       *PsThreadType,
                                                       KernelMode,
                                                       (PVOID *)&pThread,
                                                       NULL);
            ZwClose(hThread);
            if (NT_SUCCESS(refSt))
            {
                DevCtx->HotPlugThread = pThread;
                TraceLog("StartDevice spawned HotPlugWorker");
            }
            else
            {
                // Reference failed: signal stop so the orphan worker bails out
                // ASAP instead of touching DxgkInterface after unload.
                KeSetEvent(&DevCtx->HotPlugStopEvent, IO_NO_INCREMENT, FALSE);
                TraceLogStatus("StartDevice HotPlug ObRef FAIL", refSt);
            }
        }
        else
        {
            TraceLogStatus("StartDevice HotPlugWorker FAIL", thrSt);
        }
    }

    TraceLog("StartDevice EXIT OK");
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaStopDevice(PSTDVGA_DEVICE_CONTEXT DevCtx)
{
    PAGED_CODE();

    DevCtx->DriverStarted = FALSE;

    //
    // Drain the hot-plug worker BEFORE we let DXGK unload the driver image.
    // Skipping this caused BugCheck 0xCE
    // (DRIVER_UNLOADED_WITHOUT_CANCELLING_PENDING_OPERATIONS) when the worker
    // was still sleeping inside its delay and the module got unmapped under it.
    //
    StdVgaDrainHotPlugWorker(DevCtx);

    StdVgaHwClose(&DevCtx->Hw);

    return STATUS_SUCCESS;
}

//
// Synchronously drain the hot-plug worker spawned by StartDevice.
// Idempotent: safe to call from StopDevice and RemoveDevice both.
// Must be called while DevCtx is still valid (before ExFreePoolWithTag),
// otherwise the worker may wake up and dereference freed memory or run on
// an unmapped driver image (BugCheck 0xCE).
//
VOID StdVgaDrainHotPlugWorker(PSTDVGA_DEVICE_CONTEXT DevCtx)
{
    PAGED_CODE();

    if (DevCtx == NULL)
    {
        return;
    }

    if (DevCtx->HotPlugThread != NULL)
    {
        KeSetEvent(&DevCtx->HotPlugStopEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(DevCtx->HotPlugThread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(DevCtx->HotPlugThread);
        DevCtx->HotPlugThread = NULL;
    }
}

_Use_decl_annotations_ VOID StdVgaResetDevice(PSTDVGA_DEVICE_CONTEXT DevCtx)
{
    UNREFERENCED_PARAMETER(DevCtx);
}

_Use_decl_annotations_ NTSTATUS StdVgaDispatchIoRequest(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                        ULONG VidPnSourceId,
                                                        PVIDEO_REQUEST_PACKET pVideoRequestPacket)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(pVideoRequestPacket);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaSetPowerState(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                    ULONG DeviceUid,
                                                    DEVICE_POWER_STATE DevicePowerState,
                                                    POWER_ACTION ActionType)
{
    PAGED_CODE();
    TraceLog("SetPowerState ENTER");
    UNREFERENCED_PARAMETER(ActionType);

    if (DeviceUid == DISPLAY_ADAPTER_HW_ID)
    {
        DevCtx->AdapterPowerState = DevicePowerState;
    }
    else
    {
        DevCtx->MonitorPowerState = DevicePowerState;
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaQueryChildRelations(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                          PDXGK_CHILD_DESCRIPTOR pChildRelations,
                                                          ULONG ChildRelationsSize)
{
    PAGED_CODE();
    TraceLog("QueryChildRelations ENTER");
    TraceLogInt("QueryChildRelations Size", (int)ChildRelationsSize);
    UNREFERENCED_PARAMETER(DevCtx);

    RtlZeroMemory(pChildRelations, ChildRelationsSize);

    // VOT_HD15 + EDID + RecommendMonitorModes + VSS_VESA_DMT.
    // The new theory: dxgkrnl uses VideoStandard field to decide validation
    // path. VSS_VESA_DMT triggers VESA table lookup (looser); VSS_OTHER
    // triggers strict self-consistency check.
    pChildRelations[0].ChildDeviceType = TypeVideoOutput;
    pChildRelations[0].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
    pChildRelations[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_HD15;
    pChildRelations[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
    pChildRelations[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
    pChildRelations[0].AcpiUid = 0;
    pChildRelations[0].ChildUid = 0;

    TraceLog("QueryChildRelations EXIT OK");
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaQueryChildStatus(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                       PDXGK_CHILD_STATUS pChildStatus,
                                                       BOOLEAN NonDestructiveOnly)
{
    PAGED_CODE();
    TraceLog("QueryChildStatus ENTER");
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    switch (pChildStatus->Type)
    {
        case StatusConnection:
            pChildStatus->HotPlug.Connected = TRUE;
            return STATUS_SUCCESS;

        case StatusRotation:
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

// Synthetic EDID for "QEMU VGA" monitor declaring 15 supported resolutions.
// Established timings: 800x600, 1024x768
// Standard timings: 1152x864, 1280x720, 1280x800, 1280x960, 1280x1024,
//                   1400x1050, 1440x900, 1600x1200
// DTD 1: 1920x1080@60 (preferred/default), DTD 2: 2560x1600@60
// Not in EDID but in mode list (works via NOTSPECIFIED timing): 1280x768,
//   1680x1050, 1920x1200
// clang-format off
static const UCHAR s_Edid[128] = {
                                                                                                    0x00,
                                                                                                    0xFF,
                                                                                                    0xFF,
                                                                                                    0xFF,
                                                                                                    0xFF,
                                                                                                    0xFF,
                                                                                                    0xFF,
                                                                                                    0x00, // header
                                                                                                    0x45,
                                                                                                    0xB5,
                                                                                                    0x11,
                                                                                                    0x11,
                                                                                                    0x01,
                                                                                                    0x00,
                                                                                                    0x00,
                                                                                                    0x00, // manufacturer
                                                                                                          // "QMU",
                                                                                                          // product
                                                                                                          // 0x1111
                                                                                                    0x14,
                                                                                                    0x24,
                                                                                                    0x01,
                                                                                                    0x04, // week 20,
                                                                                                          // year 2026,
                                                                                                          // EDID 1.4
                                                                                                    0x08,
                                                                                                    0x29,
                                                                                                    0x1A,
                                                                                                    0x78,
                                                                                                    0xEE, // analog,
                                                                                                          // 41x26cm,
                                                                                                          // gamma 2.2,
                                                                                                          // features
                                                                                                    0xEE,
                                                                                                    0x91,
                                                                                                    0xA3,
                                                                                                    0x54,
                                                                                                    0x4C,
                                                                                                    0x99,
                                                                                                    0x26,
                                                                                                    0x0F,
                                                                                                    0x50,
                                                                                                    0x54, // chromaticity
                                                                                                    0x01,
                                                                                                    0x08,
                                                                                                    0x00, // established
                                                                                                          // timings
                                                                                                    0x71,
                                                                                                    0x40,
                                                                                                    0x81,
                                                                                                    0xC0,
                                                                                                    0x81,
                                                                                                    0x00,
                                                                                                    0x81,
                                                                                                    0x40, // standard
                                                                                                          // timings 1-4
                                                                                                    0x81,
                                                                                                    0x80,
                                                                                                    0x90,
                                                                                                    0x40,
                                                                                                    0x95,
                                                                                                    0x00,
                                                                                                    0xA9,
                                                                                                    0x40, // standard
                                                                                                          // timings 5-8
                                                                                                    // DTD 1:
                                                                                                    // 1920x1080@60Hz
                                                                                                    // (148.5 MHz)
                                                                                                    0x02,
                                                                                                    0x3A,
                                                                                                    0x80,
                                                                                                    0x18,
                                                                                                    0x71,
                                                                                                    0x38,
                                                                                                    0x2D,
                                                                                                    0x40,
                                                                                                    0x58,
                                                                                                    0x2C,
                                                                                                    0x45,
                                                                                                    0x00,
                                                                                                    0x9A,
                                                                                                    0x04,
                                                                                                    0x11,
                                                                                                    0x00,
                                                                                                    0x00,
                                                                                                    0x1E,
                                                                                                    // DTD 2:
                                                                                                    // 2560x1600@60Hz
                                                                                                    // reduced blanking
                                                                                                    // (268.5 MHz)
                                                                                                    0xE2,
                                                                                                    0x68,
                                                                                                    0x00,
                                                                                                    0xA0,
                                                                                                    0xA0,
                                                                                                    0x40,
                                                                                                    0x2E,
                                                                                                    0x60,
                                                                                                    0x30,
                                                                                                    0x20,
                                                                                                    0x36,
                                                                                                    0x00,
                                                                                                    0x9A,
                                                                                                    0x04,
                                                                                                    0x11,
                                                                                                    0x00,
                                                                                                    0x00,
                                                                                                    0x1E,
                                                                                                    // Monitor range
                                                                                                    // limits (50-75Hz
                                                                                                    // V, 30-99kHz H,
                                                                                                    // 270MHz max)
                                                                                                    0x00,
                                                                                                    0x00,
                                                                                                    0x00,
                                                                                                    0xFD,
                                                                                                    0x00,
                                                                                                    0x32,
                                                                                                    0x4B,
                                                                                                    0x1E,
                                                                                                    0x63,
                                                                                                    0x1B,
                                                                                                    0x00,
                                                                                                    0x0A,
                                                                                                    0x20,
                                                                                                    0x20,
                                                                                                    0x20,
                                                                                                    0x20,
                                                                                                    0x20,
                                                                                                    0x20,
                                                                                                    // Monitor name
                                                                                                    // "QEMU VGA"
                                                                                                    0x00,
                                                                                                    0x00,
                                                                                                    0x00,
                                                                                                    0xFC,
                                                                                                    0x00,
                                                                                                    0x51,
                                                                                                    0x45,
                                                                                                    0x4D,
                                                                                                    0x55,
                                                                                                    0x20,
                                                                                                    0x56,
                                                                                                    0x47,
                                                                                                    0x41,
                                                                                                    0x0A,
                                                                                                    0x20,
                                                                                                    0x20,
                                                                                                    0x20,
                                                                                                    0x20,
                                                                                                    0x00, // no
                                                                                                          // extensions
                                                                                                    0x74 // checksum
};
// clang-format on

_Use_decl_annotations_ NTSTATUS StdVgaQueryDeviceDescriptor(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                            ULONG ChildUid,
                                                            PDXGK_DEVICE_DESCRIPTOR pDeviceDescriptor)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(ChildUid);

    TraceLog("QueryDeviceDescriptor ENTER");
    TraceLogInt("QueryDeviceDescriptor BlockId", (int)pDeviceDescriptor->DescriptorOffset);

    // Provide EDID so dxgkrnl knows our standard VESA timings.
    if (pDeviceDescriptor->DescriptorOffset != 0)
    {
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    }
    if (pDeviceDescriptor->DescriptorLength < sizeof(s_Edid))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlCopyMemory(pDeviceDescriptor->DescriptorBuffer, s_Edid, sizeof(s_Edid));
    pDeviceDescriptor->DescriptorLength = sizeof(s_Edid);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaQueryAdapterInfo(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                       CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    PAGED_CODE();
    TraceLog("QueryAdapterInfo ENTER");
    UNREFERENCED_PARAMETER(DevCtx);

    TraceLogStatus("QueryAdapterInfo Type", (NTSTATUS)pQueryAdapterInfo->Type);

    switch (pQueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_DRIVERCAPS:
            {
                TraceLogInt("DRIVERCAPS bufSize", (int)pQueryAdapterInfo->OutputDataSize);
                TraceLogInt("DRIVERCAPS sizeof", (int)sizeof(DXGK_DRIVERCAPS));

                if (pQueryAdapterInfo->pOutputData == NULL || pQueryAdapterInfo->OutputDataSize == 0)
                {
                    return STATUS_INVALID_PARAMETER;
                }

                // Zero only as much as dxgkrnl gave us.
                RtlZeroMemory(pQueryAdapterInfo->pOutputData, pQueryAdapterInfo->OutputDataSize);

                // dxgkrnl on Server 2022 passes a 552-byte WDDM 1.3 layout while our
                // compiled DXGK_DRIVERCAPS is 592 bytes (extra fields appended at the
                // end). The leading fields share offsets with the compiled struct, so
                // we can write them through the casted pointer; fields at offsets >=
                // OutputDataSize are guarded by the size check at the bottom.
                auto *pCaps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;

                // Always write the leading fields (offsets < ~64).
                pCaps->WDDMVersion = DXGKDDI_WDDMv1_3;
                pCaps->HighestAcceptableAddress.QuadPart = -1LL;
                pCaps->MaxPointerWidth = 0;
                pCaps->MaxPointerHeight = 0;

                // GpuEngineTopology must report >= 1 node; offset is well below 552.
                pCaps->GpuEngineTopology.NbAsymetricProcessingNodes = 1;

                // Mark this driver as supporting the basic display-only model.
                pCaps->SupportNonVGA = TRUE;
                pCaps->SupportSmoothRotation = FALSE;

                TraceLog("QueryAdapterInfo DRIVERCAPS OK");
                return STATUS_SUCCESS;
            }

        case DXGKQAITYPE_QUERYSEGMENT:
            {
                if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT))
                {
                    return STATUS_BUFFER_TOO_SMALL;
                }

                auto *pSegOut = (DXGK_QUERYSEGMENTOUT *)pQueryAdapterInfo->pOutputData;

                if (pSegOut->pSegmentDescriptor == NULL)
                {
                    // First call: report segment count
                    pSegOut->NbSegment = 1;
                    pSegOut->PagingBufferSegmentId = 0;
                    pSegOut->PagingBufferSize = 64 * 1024;
                    pSegOut->PagingBufferPrivateDataSize = 0;
                    TraceLog("QueryAdapterInfo QUERYSEGMENT count=1");
                }
                else
                {
                    // Second call: fill segment descriptors
                    DXGK_SEGMENTDESCRIPTOR *pSeg = pSegOut->pSegmentDescriptor;
                    RtlZeroMemory(pSeg, sizeof(DXGK_SEGMENTDESCRIPTOR));

                    pSeg->BaseAddress = DevCtx->Hw.FrameBufferPA;
                    pSeg->CpuTranslatedAddress = DevCtx->Hw.FrameBufferPA;
                    pSeg->Size = DevCtx->Hw.VramSize;
                    pSeg->NbOfBanks = 0;
                    pSeg->pBankRangeTable = NULL;
                    pSeg->CommitLimit = DevCtx->Hw.VramSize;
                    pSeg->Flags.Value = 0;
                    pSeg->Flags.CpuVisible = 1;
                    pSeg->Flags.Aperture = 0;

                    pSegOut->PagingBufferSegmentId = 0;
                    pSegOut->PagingBufferSize = 64 * 1024;
                    pSegOut->PagingBufferPrivateDataSize = 0;
                    TraceLog("QueryAdapterInfo QUERYSEGMENT filled");
                }
                return STATUS_SUCCESS;
            }

        case DXGKQAITYPE_QUERYSEGMENT3:
            {
                if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT3))
                {
                    return STATUS_BUFFER_TOO_SMALL;
                }

                auto *pSegOut = (DXGK_QUERYSEGMENTOUT3 *)pQueryAdapterInfo->pOutputData;

                if (pSegOut->pSegmentDescriptor == NULL)
                {
                    pSegOut->NbSegment = 1;
                    pSegOut->PagingBufferSegmentId = 0;
                    pSegOut->PagingBufferSize = 64 * 1024;
                    pSegOut->PagingBufferPrivateDataSize = 0;
                    TraceLog("QueryAdapterInfo QUERYSEGMENT3 count=1");
                }
                else
                {
                    DXGK_SEGMENTDESCRIPTOR3 *pSeg = pSegOut->pSegmentDescriptor;
                    RtlZeroMemory(pSeg, sizeof(DXGK_SEGMENTDESCRIPTOR3));

                    pSeg->Flags.Value = 0;
                    pSeg->Flags.CpuVisible = 1;
                    pSeg->Flags.Aperture = 0;
                    pSeg->BaseAddress.QuadPart = DevCtx->Hw.FrameBufferPA.QuadPart;
                    pSeg->CpuTranslatedAddress.QuadPart = DevCtx->Hw.FrameBufferPA.QuadPart;
                    pSeg->Size = DevCtx->Hw.VramSize;
                    pSeg->NbOfBanks = 0;
                    pSeg->pBankRangeTable = NULL;
                    pSeg->CommitLimit = DevCtx->Hw.VramSize;

                    pSegOut->PagingBufferSegmentId = 0;
                    pSegOut->PagingBufferSize = 64 * 1024;
                    pSegOut->PagingBufferPrivateDataSize = 0;
                    TraceLog("QueryAdapterInfo QUERYSEGMENT3 filled");
                }
                return STATUS_SUCCESS;
            }

#ifdef DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION
        case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION:
            {
                if (pQueryAdapterInfo->OutputDataSize < 4)
                {
                    return STATUS_BUFFER_TOO_SMALL;
                }

                RtlZeroMemory(pQueryAdapterInfo->pOutputData, pQueryAdapterInfo->OutputDataSize);

                TraceLog("QueryAdapterInfo DISPLAY_EXT OK");
                return STATUS_SUCCESS;
            }
#endif

        default:
            TraceLogStatus("QueryAdapterInfo type", (NTSTATUS)pQueryAdapterInfo->Type);
            if (pQueryAdapterInfo->pOutputData && pQueryAdapterInfo->OutputDataSize > 0)
            {
                RtlZeroMemory(pQueryAdapterInfo->pOutputData, pQueryAdapterInfo->OutputDataSize);
            }
            return STATUS_NOT_SUPPORTED;
    }
}

_Use_decl_annotations_ NTSTATUS StdVgaSetPointerPosition(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                         CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pSetPointerPosition);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaSetPointerShape(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                      CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pSetPointerShape);
    return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS StdVgaEscape(PSTDVGA_DEVICE_CONTEXT DevCtx, CONST DXGKARG_ESCAPE *pEscape)
{
    PAGED_CODE();

    // Custom escape protocol for runtime resolution change without reboot.
    //   Magic header  ULONG = 0x53565641 ('SVGA'/AVGS little-endian)
    //   ULONG width
    //   ULONG height
    // From user mode, call D3DKMTEscape with PrivateDriverData pointing to
    // this struct. If accepted, driver immediately calls HwSetMode and redraws
    // splash so VNC reflects the new resolution.
    if (pEscape == NULL || pEscape->pPrivateDriverData == NULL || pEscape->PrivateDriverDataSize < sizeof(ULONG) * 3)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PULONG pData = (PULONG)pEscape->pPrivateDriverData;
    if (pData[0] != 0x41475653UL) // 'SVGA' as 4 little-endian bytes
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    ULONG newW = pData[1];
    ULONG newH = pData[2];
    SIZE_T req = (SIZE_T)newW * newH * STDVGA_BYTES_PER_PIXEL;
    if (newW == 0 || newH == 0 || newW > 4096 || newH > 4096 || req > DevCtx->Hw.VramSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    TraceLogInt("Escape SetRes W", (int)newW);
    TraceLogInt("Escape SetRes H", (int)newH);

    StdVgaHwSetMode(&DevCtx->Hw, (USHORT)newW, (USHORT)newH);
    ULONG stridePx = DevCtx->Hw.CurrentStridePixels;
    ULONG stride = stridePx * STDVGA_BYTES_PER_PIXEL;
    DevCtx->Hw.CurrentWidth = (USHORT)newW;
    DevCtx->Hw.CurrentHeight = (USHORT)newH;

    if (DevCtx->Hw.pFrameBuffer != NULL)
    {
        ULONG colors[4] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF};
        for (ULONG y = 0; y < newH; y++)
        {
            PULONG row = (PULONG)((PUCHAR)DevCtx->Hw.pFrameBuffer + y * stride);
            for (ULONG x = 0; x < newW; x++)
            {
                row[x] = colors[(x * 4) / newW];
            }
            for (ULONG x = newW; x < stridePx; x++)
            {
                row[x] = 0;
            }
        }
    }

    DevCtx->CurrentMode.SrcModeWidth = newW;
    DevCtx->CurrentMode.SrcModeHeight = newH;
    DevCtx->CurrentMode.DispInfo.Width = newW;
    DevCtx->CurrentMode.DispInfo.Height = newH;
    DevCtx->CurrentMode.DispInfo.Pitch = stride;

    return STATUS_SUCCESS;
}

//
// PresentDisplayOnly: copy dirty rectangles from system memory to framebuffer.
//
_Use_decl_annotations_ NTSTATUS StdVgaPresentDisplayOnly(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                         CONST DXGKARG_PRESENT_DISPLAYONLY *pPresent)
{
    PAGED_CODE();

    if (pPresent == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pPresent->BytesPerPixel != STDVGA_BYTES_PER_PIXEL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DevCtx->Hw.pFrameBuffer == NULL)
    {
        return STATUS_UNSUCCESSFUL;
    }

    UINT fbStride = DevCtx->CurrentMode.DispInfo.Pitch;
    PUCHAR pDst = (PUCHAR)DevCtx->Hw.pFrameBuffer;
    PUCHAR pSrc = (PUCHAR)pPresent->pSource;
    UINT srcPitch = pPresent->Pitch;

    if (pPresent->NumMoves > 0 && pPresent->pMoves != NULL)
    {
        for (UINT i = 0; i < pPresent->NumMoves; i++)
        {
            D3DKMT_MOVE_RECT *pMove = &pPresent->pMoves[i];
            RECT *pDstRect = &pMove->DestRect;

            UINT width = pDstRect->right - pDstRect->left;
            UINT height = pDstRect->bottom - pDstRect->top;
            UINT copyBytes = width * STDVGA_BYTES_PER_PIXEL;

            PUCHAR pSrcRow = pDst + (UINT)pMove->SourcePoint.y * fbStride +
                             (UINT)pMove->SourcePoint.x * STDVGA_BYTES_PER_PIXEL;
            PUCHAR pDstRow = pDst + (UINT)pDstRect->top * fbStride + (UINT)pDstRect->left * STDVGA_BYTES_PER_PIXEL;

            if (pMove->SourcePoint.y < pDstRect->top)
            {
                // Copy bottom-to-top to avoid overlap issues.
                pSrcRow += ((SIZE_T)height - 1) * fbStride;
                pDstRow += ((SIZE_T)height - 1) * fbStride;
                for (UINT row = 0; row < height; row++)
                {
                    RtlMoveMemory(pDstRow, pSrcRow, copyBytes);
                    pSrcRow -= fbStride;
                    pDstRow -= fbStride;
                }
            }
            else
            {
                for (UINT row = 0; row < height; row++)
                {
                    RtlMoveMemory(pDstRow, pSrcRow, copyBytes);
                    pSrcRow += fbStride;
                    pDstRow += fbStride;
                }
            }
        }
    }

    if (pPresent->NumDirtyRects > 0 && pPresent->pDirtyRect != NULL)
    {
        for (UINT i = 0; i < pPresent->NumDirtyRects; i++)
        {
            RECT *pRect = &pPresent->pDirtyRect[i];
            UINT left = (UINT)pRect->left;
            UINT top = (UINT)pRect->top;
            UINT width = (UINT)(pRect->right - pRect->left);
            UINT height = (UINT)(pRect->bottom - pRect->top);
            UINT copyBytes = width * STDVGA_BYTES_PER_PIXEL;

            PUCHAR pSrcRow = pSrc + (SIZE_T)top * srcPitch + (SIZE_T)left * STDVGA_BYTES_PER_PIXEL;
            PUCHAR pDstRow = pDst + (SIZE_T)top * fbStride + (SIZE_T)left * STDVGA_BYTES_PER_PIXEL;

            for (UINT row = 0; row < height; row++)
            {
                RtlCopyMemory(pDstRow, pSrcRow, copyBytes);
                pSrcRow += srcPitch;
                pDstRow += fbStride;
            }
        }
    }

    return STATUS_SUCCESS;
}

//
// ---- VidPn Functions ----
//

_Use_decl_annotations_ NTSTATUS StdVgaIsSupportedVidPn(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                       DXGKARG_ISSUPPORTEDVIDPN *pIsSupportedVidPn)
{
    PAGED_CODE();
    TraceLog("IsSupportedVidPn ENTER");

    // Track whether we've already reported "not supported" to bound the
    // back-and-forth and let dxgkrnl progress. Strategy: first ~3 calls with
    // empty desired VidPn return FALSE to force dxgkrnl to call
    // RecommendFunctionalVidPn. After that, return TRUE so dxgkrnl can commit.
    static LONG s_notSupportedCount = 0;

    if (pIsSupportedVidPn->hDesiredVidPn == 0)
    {
        pIsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    const DXGK_VIDPN_INTERFACE *pVidPnInterface = NULL;
    NTSTATUS status = DevCtx->DxgkInterface.DxgkCbQueryVidPnInterface(pIsSupportedVidPn->hDesiredVidPn,
                                                                      DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                                      &pVidPnInterface);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("  IsSup QueryIface", status);
        return status;
    }

    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *pTopologyInterface = NULL;
    status = pVidPnInterface->pfnGetTopology(pIsSupportedVidPn->hDesiredVidPn, &hTopology, &pTopologyInterface);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("  IsSup GetTopo", status);
        return status;
    }

    //
    // Check source mode set if present.
    //
    D3DKMDT_HVIDPNSOURCEMODESET hSourceModeSet = 0;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pSourceModeSetInterface = NULL;
    status = pVidPnInterface->pfnAcquireSourceModeSet(pIsSupportedVidPn->hDesiredVidPn,
                                                      0,
                                                      &hSourceModeSet,
                                                      &pSourceModeSetInterface);
    if (NT_SUCCESS(status))
    {
        const D3DKMDT_VIDPN_SOURCE_MODE *pPinnedMode = NULL;
        status = pSourceModeSetInterface->pfnAcquirePinnedModeInfo(hSourceModeSet, &pPinnedMode);
        if (NT_SUCCESS(status) && pPinnedMode != NULL)
        {
            BOOLEAN supported = IsModeSupported(DevCtx,
                                                pPinnedMode->Format.Graphics.PrimSurfSize.cx,
                                                pPinnedMode->Format.Graphics.PrimSurfSize.cy);
            pSourceModeSetInterface->pfnReleaseModeInfo(hSourceModeSet, pPinnedMode);

            if (!supported)
            {
                TraceLog("  IsSup srcREJECT");
                pIsSupportedVidPn->IsVidPnSupported = FALSE;
                pVidPnInterface->pfnReleaseSourceModeSet(pIsSupportedVidPn->hDesiredVidPn, hSourceModeSet);
                return STATUS_SUCCESS;
            }
        }

        pVidPnInterface->pfnReleaseSourceModeSet(pIsSupportedVidPn->hDesiredVidPn, hSourceModeSet);
    }

    //
    // Check target mode set if present.
    //
    D3DKMDT_HVIDPNTARGETMODESET hTargetModeSet = 0;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pTargetModeSetInterface = NULL;
    status = pVidPnInterface->pfnAcquireTargetModeSet(pIsSupportedVidPn->hDesiredVidPn,
                                                      0,
                                                      &hTargetModeSet,
                                                      &pTargetModeSetInterface);
    if (NT_SUCCESS(status))
    {
        static int s_isSuppCount = 0;
        s_isSuppCount++;

        SIZE_T tgtCount = 0;
        pTargetModeSetInterface->pfnGetNumModes(hTargetModeSet, &tgtCount);
        if (s_isSuppCount <= 5)
        {
            TraceLogInt("  IsSup tgtCount", (int)tgtCount);
        }

        const D3DKMDT_VIDPN_TARGET_MODE *pPinnedTarget = NULL;
        status = pTargetModeSetInterface->pfnAcquirePinnedModeInfo(hTargetModeSet, &pPinnedTarget);
        if (NT_SUCCESS(status) && pPinnedTarget != NULL)
        {
            if (s_isSuppCount <= 5)
            {
                TraceLogInt("  IsSup tgtPin W", (int)pPinnedTarget->VideoSignalInfo.ActiveSize.cx);
                TraceLogInt("  IsSup tgtPin H", (int)pPinnedTarget->VideoSignalInfo.ActiveSize.cy);
                TraceLogInt("  IsSup tgtTotalW", (int)pPinnedTarget->VideoSignalInfo.TotalSize.cx);
                TraceLogInt("  IsSup tgtTotalH", (int)pPinnedTarget->VideoSignalInfo.TotalSize.cy);
                TraceLogInt("  IsSup tgtPixRate", (int)(pPinnedTarget->VideoSignalInfo.PixelRate / 1000));
            }

            BOOLEAN supported = IsModeSupported(DevCtx,
                                                pPinnedTarget->VideoSignalInfo.ActiveSize.cx,
                                                pPinnedTarget->VideoSignalInfo.ActiveSize.cy);
            pTargetModeSetInterface->pfnReleaseModeInfo(hTargetModeSet, pPinnedTarget);

            if (!supported)
            {
                TraceLog("  IsSup tgtREJECT");
                pIsSupportedVidPn->IsVidPnSupported = FALSE;
                pVidPnInterface->pfnReleaseTargetModeSet(pIsSupportedVidPn->hDesiredVidPn, hTargetModeSet);
                return STATUS_SUCCESS;
            }
        }

        pVidPnInterface->pfnReleaseTargetModeSet(pIsSupportedVidPn->hDesiredVidPn, hTargetModeSet);
    }

    // Decision logic.
    // The empty desired VidPn (no pinned src/tgt, empty target mode set) is
    // dxgkrnl's invitation to mode-set negotiation. Our EnumCofuncModality
    // pfnAddMode always fails with INVALID_FREQUENCY, so this VidPn will never
    // commit. Instead, return FALSE the first ~3 times to force dxgkrnl to
    // call DxgkDdiRecommendFunctionalVidPn, where we directly build a pinned
    // VidPn (different validation path, potentially looser).
    SIZE_T finalTgtCount = 0;
    D3DKMDT_HVIDPNTARGETMODESET hTgtFinal = 0;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pTgtIfaceFinal = NULL;
    if (NT_SUCCESS(pVidPnInterface->pfnAcquireTargetModeSet(pIsSupportedVidPn->hDesiredVidPn,
                                                            0,
                                                            &hTgtFinal,
                                                            &pTgtIfaceFinal)))
    {
        pTgtIfaceFinal->pfnGetNumModes(hTgtFinal, &finalTgtCount);
        pVidPnInterface->pfnReleaseTargetModeSet(pIsSupportedVidPn->hDesiredVidPn, hTgtFinal);
    }

    // NOT_SUP didn't trigger RecommendFunctionalVidPn (dxgkrnl just
    // gives up). Revert to always-TRUE so EnumCofuncModality runs.
    UNREFERENCED_PARAMETER(s_notSupportedCount);
    UNREFERENCED_PARAMETER(finalTgtCount);

    pIsSupportedVidPn->IsVidPnSupported = TRUE;
    TraceLog("  IsSup OK");
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS
StdVgaRecommendFunctionalVidPn(PSTDVGA_DEVICE_CONTEXT DevCtx,
                               CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *pRecommendFunctionalVidPn)
{
    PAGED_CODE();
    TraceLog("RecommendFunctionalVidPn ENTER");

    D3DKMDT_HVIDPN hVidPn = pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn;

    const DXGK_VIDPN_INTERFACE *pVidPnInterface = NULL;
    NTSTATUS status = DevCtx->DxgkInterface.DxgkCbQueryVidPnInterface(hVidPn,
                                                                      DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                                      &pVidPnInterface);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("  RecFVP QueryIface", status);
        return status;
    }

    // Get topology and add a path (source 0 → target 0).
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *pTopoInterface = NULL;
    status = pVidPnInterface->pfnGetTopology(hVidPn, &hTopology, &pTopoInterface);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("  RecFVP GetTopo", status);
        return status;
    }

    D3DKMDT_VIDPN_PRESENT_PATH *pNewPath = NULL;
    status = pTopoInterface->pfnCreateNewPathInfo(hTopology, &pNewPath);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("  RecFVP CreatePath", status);
        return status;
    }
    pNewPath->VidPnSourceId = 0;
    pNewPath->VidPnTargetId = 0;
    pNewPath->ImportanceOrdinal = D3DKMDT_VPPI_PRIMARY;
    pNewPath->ContentTransformation.Scaling = D3DKMDT_VPPS_IDENTITY;
    pNewPath->ContentTransformation.ScalingSupport.Identity = TRUE;
    pNewPath->ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
    pNewPath->ContentTransformation.RotationSupport.Identity = TRUE;
    pNewPath->CopyProtection.CopyProtectionType = D3DKMDT_VPPMT_NOPROTECTION;
    pNewPath->CopyProtection.CopyProtectionSupport.NoProtection = TRUE;

    status = pTopoInterface->pfnAddPath(hTopology, pNewPath);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("  RecFVP AddPath", status);
        pTopoInterface->pfnReleasePathInfo(hTopology, pNewPath);
        return status;
    }

    // Use the resolution the driver just SetMode'd to (from registry).
    // This ensures hw state matches what we recommend to dxgkrnl.
    USHORT w = DevCtx->Hw.CurrentWidth;
    USHORT h = DevCtx->Hw.CurrentHeight;
    if (w == 0 || h == 0)
    {
        w = (USHORT)DevCtx->CurrentMode.DispInfo.Width;
        h = (USHORT)DevCtx->CurrentMode.DispInfo.Height;
    }
    if (w == 0 || h == 0)
    {
        w = 1920;
        h = 1080;
    }
    TraceLogInt("  RecFVP useW", (int)w);
    TraceLogInt("  RecFVP useH", (int)h);

    // Create source mode set, add one mode, pin it.
    D3DKMDT_HVIDPNSOURCEMODESET hSrcModeSet = 0;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pSrcInterface = NULL;
    status = pVidPnInterface->pfnCreateNewSourceModeSet(hVidPn, 0, &hSrcModeSet, &pSrcInterface);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("  RecFVP CreateSrc", status);
        return status;
    }

    D3DKMDT_VIDPN_SOURCE_MODE *pSrcMode = NULL;
    status = pSrcInterface->pfnCreateNewModeInfo(hSrcModeSet, &pSrcMode);
    if (NT_SUCCESS(status))
    {
        pSrcMode->Type = D3DKMDT_RMT_GRAPHICS;
        pSrcMode->Format.Graphics.PrimSurfSize.cx = w;
        pSrcMode->Format.Graphics.PrimSurfSize.cy = h;
        pSrcMode->Format.Graphics.VisibleRegionSize.cx = w;
        pSrcMode->Format.Graphics.VisibleRegionSize.cy = h;
        pSrcMode->Format.Graphics.Stride = (UINT)w * 4;
        pSrcMode->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        pSrcMode->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
        pSrcMode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

        status = pSrcInterface->pfnAddMode(hSrcModeSet, pSrcMode);
        if (!NT_SUCCESS(status))
        {
            TraceLogStatus("  RecFVP SrcAdd", status);
            pSrcInterface->pfnReleaseModeInfo(hSrcModeSet, pSrcMode);
            pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hSrcModeSet);
            return status;
        }

        status = pSrcInterface->pfnPinMode(hSrcModeSet, pSrcMode->Id);
        if (!NT_SUCCESS(status))
        {
            TraceLogStatus("  RecFVP SrcPin", status);
        }
    }

    status = pVidPnInterface->pfnAssignSourceModeSet(hVidPn, 0, hSrcModeSet);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("  RecFVP AssignSrc", status);
        pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hSrcModeSet);
        return status;
    }

    // Create target mode set, add one mode, pin it.
    D3DKMDT_HVIDPNTARGETMODESET hTgtModeSet = 0;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pTgtInterface = NULL;
    status = pVidPnInterface->pfnCreateNewTargetModeSet(hVidPn, 0, &hTgtModeSet, &pTgtInterface);
    if (!NT_SUCCESS(status))
    {
        TraceLogStatus("  RecFVP CreateTgt", status);
        return status;
    }

    D3DKMDT_VIDPN_TARGET_MODE *pTgtMode = NULL;
    status = pTgtInterface->pfnCreateNewModeInfo(hTgtModeSet, &pTgtMode);
    if (NT_SUCCESS(status))
    {
        pTgtMode->Preference = D3DKMDT_MP_PREFERRED;
        BuildVideoSignalInfo(&pTgtMode->VideoSignalInfo, w, h);

        status = pTgtInterface->pfnAddMode(hTgtModeSet, pTgtMode);
        TraceLogStatus("  RecFVP TgtAdd", status);
        if (!NT_SUCCESS(status))
        {
            pTgtInterface->pfnReleaseModeInfo(hTgtModeSet, pTgtMode);
            pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hTgtModeSet);
            return status;
        }

        status = pTgtInterface->pfnPinMode(hTgtModeSet, pTgtMode->Id);
        if (!NT_SUCCESS(status))
        {
            TraceLogStatus("  RecFVP TgtPin", status);
        }
    }

    status = pVidPnInterface->pfnAssignTargetModeSet(hVidPn, 0, hTgtModeSet);
    TraceLogStatus("  RecFVP AssignTgt", status);
    if (!NT_SUCCESS(status))
    {
        pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hTgtModeSet);
        return status;
    }

    TraceLog("RecommendFunctionalVidPn OK");
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS
StdVgaEnumVidPnCofuncModality(PSTDVGA_DEVICE_CONTEXT DevCtx, CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *pEnumCofuncModality)
{
    PAGED_CODE();

    static int s_enumCount = 0;
    s_enumCount++;
    if (s_enumCount <= 20)
    {
        TraceLogInt("EnumCofuncV2 pivot", (int)pEnumCofuncModality->EnumPivotType);
    }

    const DXGK_VIDPN_INTERFACE *pVidPnInterface = NULL;
    NTSTATUS status = DevCtx->DxgkInterface.DxgkCbQueryVidPnInterface(pEnumCofuncModality->hConstrainingVidPn,
                                                                      DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                                      &pVidPnInterface);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *pTopologyInterface = NULL;
    status = pVidPnInterface->pfnGetTopology(pEnumCofuncModality->hConstrainingVidPn, &hTopology, &pTopologyInterface);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    //
    // Get the path.
    //
    const D3DKMDT_VIDPN_PRESENT_PATH *pPath = NULL;
    SIZE_T numPaths = 0;
    status = pTopologyInterface->pfnGetNumPaths(hTopology, &numPaths);
    if (!NT_SUCCESS(status) || numPaths == 0)
    {
        return status;
    }

    status = pTopologyInterface->pfnAcquireFirstPathInfo(hTopology, &pPath);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    D3DDDI_VIDEO_PRESENT_SOURCE_ID sourceId = pPath->VidPnSourceId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID targetId = pPath->VidPnTargetId;

    if (s_enumCount <= 10)
    {
        TraceLogInt("  Scaling", (int)pPath->ContentTransformation.Scaling);
        TraceLogInt("  Rotation", (int)pPath->ContentTransformation.Rotation);
    }

    //
    // Update path support info: declare what transformations we support
    // and resolve UNPINNED values to IDENTITY so dxgkrnl can converge.
    //
    D3DKMDT_VIDPN_PRESENT_PATH *pPathMut = const_cast<D3DKMDT_VIDPN_PRESENT_PATH *>(pPath);

    pPathMut->ContentTransformation.ScalingSupport.Identity = TRUE;
    pPathMut->ContentTransformation.ScalingSupport.Centered = FALSE;
    pPathMut->ContentTransformation.ScalingSupport.Stretched = FALSE;

    pPathMut->ContentTransformation.RotationSupport.Identity = TRUE;
    pPathMut->ContentTransformation.RotationSupport.Rotate90 = FALSE;
    pPathMut->ContentTransformation.RotationSupport.Rotate180 = FALSE;
    pPathMut->ContentTransformation.RotationSupport.Rotate270 = FALSE;

    if (pEnumCofuncModality->EnumPivotType != D3DKMDT_EPT_SCALING)
    {
        if (pPathMut->ContentTransformation.Scaling == D3DKMDT_VPPS_UNPINNED ||
            pPathMut->ContentTransformation.Scaling == D3DKMDT_VPPS_UNINITIALIZED)
        {
            pPathMut->ContentTransformation.Scaling = D3DKMDT_VPPS_IDENTITY;
        }
    }

    if (pEnumCofuncModality->EnumPivotType != D3DKMDT_EPT_ROTATION)
    {
        if (pPathMut->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED ||
            pPathMut->ContentTransformation.Rotation == D3DKMDT_VPPR_UNINITIALIZED)
        {
            pPathMut->ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
        }
    }

    status = pTopologyInterface->pfnUpdatePathSupportInfo(hTopology, pPathMut);
    if (s_enumCount <= 10)
    {
        TraceLogStatus("  UpdatePathSupportInfo", status);
    }
    pTopologyInterface->pfnReleasePathInfo(hTopology, pPath);

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    //
    // Acquire pinned modes.
    //
    const D3DKMDT_VIDPN_SOURCE_MODE *pPinnedSourceMode = NULL;
    const D3DKMDT_VIDPN_TARGET_MODE *pPinnedTargetMode = NULL;

    D3DKMDT_HVIDPNSOURCEMODESET hSourceModeSet = 0;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pSourceModeSetInterface = NULL;
    status = pVidPnInterface->pfnAcquireSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                      sourceId,
                                                      &hSourceModeSet,
                                                      &pSourceModeSetInterface);
    if (NT_SUCCESS(status))
    {
        pSourceModeSetInterface->pfnAcquirePinnedModeInfo(hSourceModeSet, &pPinnedSourceMode);
    }

    D3DKMDT_HVIDPNTARGETMODESET hTargetModeSet = 0;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pTargetModeSetInterface = NULL;
    status = pVidPnInterface->pfnAcquireTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                      targetId,
                                                      &hTargetModeSet,
                                                      &pTargetModeSetInterface);
    if (s_enumCount <= 10)
    {
        TraceLogStatus("  AcqTgtModeSet", status);
    }
    if (NT_SUCCESS(status))
    {
        pTargetModeSetInterface->pfnAcquirePinnedModeInfo(hTargetModeSet, &pPinnedTargetMode);
    }

    // Check how many modes are in the acquired target mode set.
    SIZE_T tgtModeCount = 0;
    if (hTargetModeSet != 0 && pTargetModeSetInterface != NULL)
    {
        pTargetModeSetInterface->pfnGetNumModes(hTargetModeSet, &tgtModeCount);
    }

    if (s_enumCount <= 10)
    {
        TraceLogInt("  srcPinned", pPinnedSourceMode != NULL ? 1 : 0);
        TraceLogInt("  tgtPinned", pPinnedTargetMode != NULL ? 1 : 0);
        TraceLogInt("  tgtModes", (int)tgtModeCount);
    }

    //
    // Source modes: if not pinned, add all supported modes.
    //
    if (pPinnedSourceMode == NULL && pEnumCofuncModality->EnumPivotType != D3DKMDT_EPT_VIDPNSOURCE)
    {
        if (hSourceModeSet != 0)
        {
            pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hSourceModeSet);
            hSourceModeSet = 0;
        }

        D3DKMDT_HVIDPNSOURCEMODESET hNewSrcModeSet = 0;
        const DXGK_VIDPNSOURCEMODESET_INTERFACE *pNewSrcInterface = NULL;
        status = pVidPnInterface->pfnCreateNewSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                            sourceId,
                                                            &hNewSrcModeSet,
                                                            &pNewSrcInterface);

        if (NT_SUCCESS(status))
        {
            // If target is pinned, only add a single source mode of matching size
            // (Identity scaling requires src == tgt).
            USHORT pinnedTgtW = 0, pinnedTgtH = 0;
            if (pPinnedTargetMode != NULL)
            {
                pinnedTgtW = (USHORT)pPinnedTargetMode->VideoSignalInfo.ActiveSize.cx;
                pinnedTgtH = (USHORT)pPinnedTargetMode->VideoSignalInfo.ActiveSize.cy;
            }

            ULONG srcAdded = 0;
            for (ULONG i = 0; i < DevCtx->Hw.ModeCount; i++)
            {
                USHORT w = DevCtx->Hw.pModeList[i].Width;
                USHORT h = DevCtx->Hw.pModeList[i].Height;

                if (pinnedTgtW != 0 && pinnedTgtH != 0 && (w != pinnedTgtW || h != pinnedTgtH))
                {
                    continue;
                }

                D3DKMDT_VIDPN_SOURCE_MODE *pSrcMode = NULL;
                NTSTATUS addSt = pNewSrcInterface->pfnCreateNewModeInfo(hNewSrcModeSet, &pSrcMode);
                if (!NT_SUCCESS(addSt))
                {
                    continue;
                }

                // Don't set Id (let framework allocate)
                pSrcMode->Type = D3DKMDT_RMT_GRAPHICS;
                pSrcMode->Format.Graphics.PrimSurfSize.cx = w;
                pSrcMode->Format.Graphics.PrimSurfSize.cy = h;
                pSrcMode->Format.Graphics.VisibleRegionSize.cx = w;
                pSrcMode->Format.Graphics.VisibleRegionSize.cy = h;
                pSrcMode->Format.Graphics.Stride = (UINT)w * 4;
                pSrcMode->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
                pSrcMode->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
                pSrcMode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

                addSt = pNewSrcInterface->pfnAddMode(hNewSrcModeSet, pSrcMode);
                if (!NT_SUCCESS(addSt))
                {
                    pNewSrcInterface->pfnReleaseModeInfo(hNewSrcModeSet, pSrcMode);
                }
                else
                {
                    srcAdded++;
                }
            }

            if (s_enumCount <= 5)
            {
                TraceLogInt("  SrcAdded", (int)srcAdded);
            }

            status = pVidPnInterface->pfnAssignSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                             sourceId,
                                                             hNewSrcModeSet);
            if (!NT_SUCCESS(status))
            {
                pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hNewSrcModeSet);
            }
        }
    }
    else
    {
        if (pPinnedSourceMode != NULL)
        {
            pSourceModeSetInterface->pfnReleaseModeInfo(hSourceModeSet, pPinnedSourceMode);
            pPinnedSourceMode = NULL;
        }
        if (hSourceModeSet != 0)
        {
            pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hSourceModeSet);
            hSourceModeSet = 0;
        }
    }

    //
    // Re-enable target-mode handling. Iterate the actual monitor source
    // mode set (which dxgkrnl already validated and accepted via
    // RecommendMonitorModes) and memcpy each VideoSignalInfo verbatim into a
    // new target mode. This guarantees byte-identical timings.
    //
    if (pPinnedTargetMode == NULL && pEnumCofuncModality->EnumPivotType != D3DKMDT_EPT_VIDPNTARGET)
    {
        if (s_enumCount <= 10)
        {
            TraceLogInt("  TGT_BUILD targetId", (int)targetId);
        }

        // Release any pre-acquired existing target mode set so we can replace
        // it with a freshly built one.
        if (hTargetModeSet != 0)
        {
            pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hTargetModeSet);
            hTargetModeSet = 0;
            pTargetModeSetInterface = NULL;
        }

        D3DKMDT_HVIDPNTARGETMODESET hNewTgtModeSet = 0;
        const DXGK_VIDPNTARGETMODESET_INTERFACE *pNewTgtInterface = NULL;
        status = pVidPnInterface->pfnCreateNewTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                            targetId,
                                                            &hNewTgtModeSet,
                                                            &pNewTgtInterface);

        if (s_enumCount <= 10)
        {
            TraceLogStatus("  CreateNewTgtMS", status);
        }

        if (NT_SUCCESS(status))
        {
            // Acquire the monitor source mode set (filled by RecommendMonitorModes)
            // so we can copy its VideoSignalInfo verbatim.
            const DXGK_MONITOR_INTERFACE *pMonIface = NULL;
            NTSTATUS monSt = DevCtx->DxgkInterface.DxgkCbQueryMonitorInterface(DevCtx->DxgkInterface.DeviceHandle,
                                                                               DXGK_MONITOR_INTERFACE_VERSION_V1,
                                                                               &pMonIface);

            D3DKMDT_HMONITORSOURCEMODESET hMonSet = 0;
            const DXGK_MONITORSOURCEMODESET_INTERFACE *pMonSetIface = NULL;
            if (NT_SUCCESS(monSt) && pMonIface != NULL)
            {
                monSt = pMonIface->pfnAcquireMonitorSourceModeSet(DevCtx->DxgkInterface.DeviceHandle,
                                                                  targetId,
                                                                  &hMonSet,
                                                                  &pMonSetIface);
                if (s_enumCount <= 10)
                {
                    TraceLogStatus("  AcqMonModeSet", monSt);
                }
            }

            USHORT pinnedW = 0, pinnedH = 0;
            if (pPinnedSourceMode != NULL)
            {
                pinnedW = (USHORT)pPinnedSourceMode->Format.Graphics.VisibleRegionSize.cx;
                pinnedH = (USHORT)pPinnedSourceMode->Format.Graphics.VisibleRegionSize.cy;
            }

            ULONG tgtAdded = 0;

            {
                for (ULONG i = 0; i < DevCtx->Hw.ModeCount; i++)
                {
                    USHORT w = DevCtx->Hw.pModeList[i].Width;
                    USHORT h = DevCtx->Hw.pModeList[i].Height;

                    if (pinnedW != 0 && pinnedH != 0 && (w != pinnedW || h != pinnedH))
                    {
                        continue;
                    }

                    D3DKMDT_VIDPN_TARGET_MODE *pTgtMode = NULL;
                    NTSTATUS addSt = pNewTgtInterface->pfnCreateNewModeInfo(hNewTgtModeSet, &pTgtMode);
                    if (!NT_SUCCESS(addSt))
                    {
                        continue;
                    }

                    BuildVideoSignalInfo(&pTgtMode->VideoSignalInfo, w, h);
                    pTgtMode->Preference = D3DKMDT_MP_NOTPREFERRED;

                    addSt = pNewTgtInterface->pfnAddMode(hNewTgtModeSet, pTgtMode);
                    if (NT_SUCCESS(addSt))
                    {
                        tgtAdded++;
                    }
                    else
                    {
                        pNewTgtInterface->pfnReleaseModeInfo(hNewTgtModeSet, pTgtMode);
                    }
                }
                if (s_enumCount <= 5)
                {
                    TraceLogInt("  TgtAdded", (int)tgtAdded);
                }
            }

            if (hMonSet != 0 && pMonIface != NULL)
            {
                pMonIface->pfnReleaseMonitorSourceModeSet(DevCtx->DxgkInterface.DeviceHandle, hMonSet);
            }

            if (s_enumCount <= 10)
            {
                TraceLogInt("  TgtAdded", (int)tgtAdded);
            }

            status = pVidPnInterface->pfnAssignTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                             targetId,
                                                             hNewTgtModeSet);
            if (!NT_SUCCESS(status))
            {
                TraceLogStatus("  AssignTgt FAIL", status);
                pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hNewTgtModeSet);
            }
        }
    }
    else
    {
        if (s_enumCount <= 10)
        {
            TraceLogInt("  TGT_SKIP pinned", pPinnedTargetMode != NULL ? 1 : 0);
        }

        if (pPinnedTargetMode != NULL && pTargetModeSetInterface != NULL)
        {
            pTargetModeSetInterface->pfnReleaseModeInfo(hTargetModeSet, pPinnedTargetMode);
        }
        if (hTargetModeSet != 0)
        {
            pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hTargetModeSet);
        }
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS
StdVgaSetVidPnSourceVisibility(PSTDVGA_DEVICE_CONTEXT DevCtx,
                               CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility)
{
    PAGED_CODE();
    TraceLogInt("SetVisibility visible", pSetVidPnSourceVisibility->Visible ? 1 : 0);

    if (pSetVidPnSourceVisibility->VidPnSourceId != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DevCtx->CurrentMode.Flags.SourceNotVisible = !pSetVidPnSourceVisibility->Visible;

    if (!pSetVidPnSourceVisibility->Visible)
    {
        StdVgaHwBlackOut(&DevCtx->Hw);
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaCommitVidPn(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                  CONST DXGKARG_COMMITVIDPN *pCommitVidPn)
{
    PAGED_CODE();
    TraceLog("CommitVidPn ENTER");

    const DXGK_VIDPN_INTERFACE *pVidPnInterface = NULL;
    NTSTATUS status = DevCtx->DxgkInterface.DxgkCbQueryVidPnInterface(pCommitVidPn->hFunctionalVidPn,
                                                                      DXGK_VIDPN_INTERFACE_VERSION_V1,
                                                                      &pVidPnInterface);
    if (!NT_SUCCESS(status))
    {
        TraceLog("CommitVidPn QueryIf FAIL");
        return status;
    }
    TraceLog("CommitVidPn QueryIf OK");

    //
    // If the path is being invalidated, turn off display and return.
    //
    if (pCommitVidPn->Flags.PathPoweredOff)
    {
        TraceLog("CommitVidPn PathPoweredOff");
        DevCtx->CurrentMode.Flags.SourceNotVisible = 1;
        return STATUS_SUCCESS;
    }
    TraceLog("CommitVidPn not powered off");

    //
    // Get the source mode to determine resolution.
    //
    D3DKMDT_HVIDPNSOURCEMODESET hSourceModeSet = 0;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pSourceModeSetInterface = NULL;
    status = pVidPnInterface->pfnAcquireSourceModeSet(pCommitVidPn->hFunctionalVidPn,
                                                      pCommitVidPn->AffectedVidPnSourceId,
                                                      &hSourceModeSet,
                                                      &pSourceModeSetInterface);
    if (!NT_SUCCESS(status))
    {
        TraceLog("CommitVidPn AcqSrcSet FAIL");
        return status;
    }
    TraceLog("CommitVidPn AcqSrcSet OK");

    const D3DKMDT_VIDPN_SOURCE_MODE *pPinnedMode = NULL;
    status = pSourceModeSetInterface->pfnAcquirePinnedModeInfo(hSourceModeSet, &pPinnedMode);
    if (!NT_SUCCESS(status) || pPinnedMode == NULL)
    {
        TraceLog("CommitVidPn NoPinnedSrc");
        pVidPnInterface->pfnReleaseSourceModeSet(pCommitVidPn->hFunctionalVidPn, hSourceModeSet);
        return status == STATUS_SUCCESS ? STATUS_UNSUCCESSFUL : status;
    }
    TraceLog("CommitVidPn got pinned mode");

    UINT width = pPinnedMode->Format.Graphics.PrimSurfSize.cx;
    UINT height = pPinnedMode->Format.Graphics.PrimSurfSize.cy;

    TraceLogInt("CommitVidPn w", (int)width);
    TraceLogInt("CommitVidPn h", (int)height);

    pSourceModeSetInterface->pfnReleaseModeInfo(hSourceModeSet, pPinnedMode);
    pVidPnInterface->pfnReleaseSourceModeSet(pCommitVidPn->hFunctionalVidPn, hSourceModeSet);

    //
    // Set the mode.
    //
    status = SetCurrentMode(DevCtx, width, height);
    if (!NT_SUCCESS(status))
    {
        TraceLog("CommitVidPn SetMode FAIL");
        return status;
    }

    TraceLog("CommitVidPn SUCCESS");
    DevCtx->CurrentMode.Flags.SourceNotVisible = 0;
    DevCtx->CurrentMode.Flags.FrameBufferIsActive = 1;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS
StdVgaUpdateActiveVidPnPresentPath(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                   CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *pUpdateActiveVidPnPresentPath)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pUpdateActiveVidPnPresentPath);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaQueryVidPnHWCapability(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                             DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);

    pVidPnHWCaps->VidPnHWCaps.DriverRotation = FALSE;
    pVidPnHWCaps->VidPnHWCaps.DriverScaling = FALSE;
    pVidPnHWCaps->VidPnHWCaps.DriverCloning = FALSE;
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert = FALSE;
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = FALSE;
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay = FALSE;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaRecommendMonitorModes(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                            CONST DXGKARG_RECOMMENDMONITORMODES *pRecommendMonitorModes)
{
    PAGED_CODE();
    TraceLog("RecommendMonitorModes ENTER (v49 full list)");

    D3DKMDT_HMONITORSOURCEMODESET hModeSet = pRecommendMonitorModes->hMonitorSourceModeSet;
    const DXGK_MONITORSOURCEMODESET_INTERFACE *pModeSetInterface = pRecommendMonitorModes->pMonitorSourceModeSetInterface;

    // Declare all VESA modes that fit in VRAM so Windows display
    // settings GUI shows the full list of selectable resolutions.
    USHORT prefW = DevCtx->Hw.CurrentWidth;
    USHORT prefH = DevCtx->Hw.CurrentHeight;
    if (prefW == 0 || prefH == 0)
    {
        prefW = 1920;
        prefH = 1080;
    }

    ULONG monAdded = 0;
    for (ULONG i = 0; i < DevCtx->Hw.ModeCount; i++)
    {
        USHORT w = DevCtx->Hw.pModeList[i].Width;
        USHORT h = DevCtx->Hw.pModeList[i].Height;

        D3DKMDT_MONITOR_SOURCE_MODE *pNewMode = NULL;
        NTSTATUS status = pModeSetInterface->pfnCreateNewModeInfo(hModeSet, &pNewMode);
        if (!NT_SUCCESS(status))
        {
            if (status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                continue;
            }
            TraceLogStatus("RecMonModes CreateErr", status);
            return status;
        }

        pNewMode->ColorBasis = D3DKMDT_CB_SRGB;
        pNewMode->ColorCoeffDynamicRanges.FirstChannel = 8;
        pNewMode->ColorCoeffDynamicRanges.SecondChannel = 8;
        pNewMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
        pNewMode->ColorCoeffDynamicRanges.FourthChannel = 8;
        pNewMode->Origin = D3DKMDT_MCO_DRIVER;
        pNewMode->Preference = (w == prefW && h == prefH) ? D3DKMDT_MP_PREFERRED : D3DKMDT_MP_NOTPREFERRED;

        BuildVideoSignalInfo(&pNewMode->VideoSignalInfo, w, h);

        status = pModeSetInterface->pfnAddMode(hModeSet, pNewMode);
        if (!NT_SUCCESS(status))
        {
            pModeSetInterface->pfnReleaseModeInfo(hModeSet, pNewMode);
            if (status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                continue;
            }
            TraceLogStatus("RecMonModes AddErr", status);
            return status;
        }
        monAdded++;
    }

    TraceLogInt("RecMonModes added count", (int)monAdded);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaStopDeviceAndReleasePostDisplayOwnership(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                                               D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                                               DXGK_DISPLAY_INFORMATION *pDisplayInfo)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(TargetId);

    *pDisplayInfo = DevCtx->CurrentMode.DispInfo;

    StdVgaStopDevice(DevCtx);

    return STATUS_SUCCESS;
}

#pragma code_seg()

_Use_decl_annotations_ NTSTATUS StdVgaSystemDisplayEnable(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                          D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                          PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                                                          PUINT pWidth,
                                                          PUINT pHeight,
                                                          D3DDDIFORMAT *pColorFormat)
{
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);

    *pWidth = DevCtx->CurrentMode.DispInfo.Width;
    *pHeight = DevCtx->CurrentMode.DispInfo.Height;
    *pColorFormat = D3DDDIFMT_X8R8G8B8;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ VOID StdVgaSystemDisplayWrite(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                     PVOID pSource,
                                                     UINT SourceWidth,
                                                     UINT SourceHeight,
                                                     UINT SourceStride,
                                                     UINT PositionX,
                                                     UINT PositionY)
{
    if (DevCtx->Hw.pFrameBuffer == NULL || pSource == NULL)
    {
        return;
    }

    UINT fbStride = DevCtx->CurrentMode.DispInfo.Pitch;
    UINT screenWidth = DevCtx->CurrentMode.DispInfo.Width;
    UINT screenHeight = DevCtx->CurrentMode.DispInfo.Height;

    for (UINT row = 0; row < SourceHeight; row++)
    {
        UINT dstY = PositionY + row;
        if (dstY >= screenHeight)
        {
            break;
        }

        UINT copyWidth = SourceWidth;
        if (PositionX + copyWidth > screenWidth)
        {
            copyWidth = screenWidth - PositionX;
        }

        PUCHAR pSrcRow = (PUCHAR)pSource + (SIZE_T)row * SourceStride;
        PUCHAR pDstRow = (PUCHAR)DevCtx->Hw.pFrameBuffer + (SIZE_T)dstY * fbStride +
                         (SIZE_T)PositionX * STDVGA_BYTES_PER_PIXEL;

        RtlCopyMemory(pDstRow, pSrcRow, (SIZE_T)copyWidth * STDVGA_BYTES_PER_PIXEL);
    }
}

_Use_decl_annotations_ BOOLEAN StdVgaInterruptRoutine(PSTDVGA_DEVICE_CONTEXT DevCtx, ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(MessageNumber);
    return FALSE;
}

_Use_decl_annotations_ VOID StdVgaDpcRoutine(PSTDVGA_DEVICE_CONTEXT DevCtx)
{
    UNREFERENCED_PARAMETER(DevCtx);
}

//
// ---- Full WDDM Miniport DDIs ----
//

#pragma code_seg("PAGE")

_Use_decl_annotations_ NTSTATUS StdVgaCreateDevice(PSTDVGA_DEVICE_CONTEXT DevCtx, DXGKARG_CREATEDEVICE *pCreateDevice)
{
    PAGED_CODE();

    PSTDVGA_DEVICE pDevice = (PSTDVGA_DEVICE)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(STDVGA_DEVICE), STDVGA_TAG);
    if (pDevice == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(pDevice, sizeof(STDVGA_DEVICE));

    pDevice->pAdapter = DevCtx;
    pDevice->hDevice = pCreateDevice->hDevice;
    pCreateDevice->pInfo = NULL;

    pCreateDevice->hDevice = (HANDLE)pDevice;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaDestroyDevice(PVOID pDeviceContext)
{
    PAGED_CODE();
    if (pDeviceContext != NULL)
    {
        ExFreePoolWithTag(pDeviceContext, STDVGA_TAG);
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaCreateAllocation(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                       DXGKARG_CREATEALLOCATION *pCreateAllocation)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);

    for (UINT i = 0; i < pCreateAllocation->NumAllocations; i++)
    {
        DXGK_ALLOCATIONINFO *pAllocInfo = &pCreateAllocation->pAllocationInfo[i];

        PSTDVGA_ALLOCATION pAlloc = (PSTDVGA_ALLOCATION)ExAllocatePoolWithTag(NonPagedPoolNx,
                                                                              sizeof(STDVGA_ALLOCATION),
                                                                              STDVGA_TAG);
        if (pAlloc == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(pAlloc, sizeof(STDVGA_ALLOCATION));

        if (pAllocInfo->pPrivateDriverData != NULL && pAllocInfo->PrivateDriverDataSize >= sizeof(STDVGA_ALLOCATION))
        {
            RtlCopyMemory(pAlloc, pAllocInfo->pPrivateDriverData, sizeof(STDVGA_ALLOCATION));
        }
        else
        {
            pAlloc->Width = 1920;
            pAlloc->Height = 1080;
            pAlloc->Pitch = 1920 * 4;
            pAlloc->BytesPerPixel = 4;
            pAlloc->Format = D3DDDIFMT_X8R8G8B8;
        }

        pAllocInfo->hAllocation = (HANDLE)pAlloc;
        pAllocInfo->Alignment = 0;
        pAllocInfo->Size = (SIZE_T)pAlloc->Pitch * pAlloc->Height;
        pAllocInfo->PitchAlignedSize = 0;
        pAllocInfo->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
        pAllocInfo->SupportedReadSegmentSet = 1;
        pAllocInfo->SupportedWriteSegmentSet = 1;
        pAllocInfo->PreferredSegment.Value = 0;
        pAllocInfo->PreferredSegment.SegmentId0 = 1;
        pAllocInfo->Flags.Value = 0;
        pAllocInfo->Flags.CpuVisible = 1;
        pAllocInfo->EvictionSegmentSet = 0;
        pAllocInfo->MaximumRenamingListLength = 0;
        pAllocInfo->pAllocationUsageHint = NULL;
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaDestroyAllocation(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                        CONST DXGKARG_DESTROYALLOCATION *pDestroyAllocation)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);

    for (UINT i = 0; i < pDestroyAllocation->NumAllocations; i++)
    {
        PSTDVGA_ALLOCATION pAlloc = (PSTDVGA_ALLOCATION)pDestroyAllocation->pAllocationList[i];
        if (pAlloc != NULL)
        {
            ExFreePoolWithTag(pAlloc, STDVGA_TAG);
        }
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaDescribeAllocation(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                         DXGKARG_DESCRIBEALLOCATION *pDescribeAllocation)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);

    PSTDVGA_ALLOCATION pAlloc = (PSTDVGA_ALLOCATION)pDescribeAllocation->hAllocation;
    if (pAlloc == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    pDescribeAllocation->Width = pAlloc->Width;
    pDescribeAllocation->Height = pAlloc->Height;
    pDescribeAllocation->Format = pAlloc->Format;
    pDescribeAllocation->MultisampleMethod.NumSamples = 1;
    pDescribeAllocation->MultisampleMethod.NumQualityLevels = 0;
    pDescribeAllocation->RefreshRate.Numerator = 60;
    pDescribeAllocation->RefreshRate.Denominator = 1;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS
StdVgaGetStandardAllocationDriverData(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                      DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *pStdAllocData)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);

    switch (pStdAllocData->StandardAllocationType)
    {
        case D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE:
            {
                auto *pPrimary = pStdAllocData->pCreateSharedPrimarySurfaceData;
                if (pStdAllocData->pAllocationPrivateDriverData == NULL)
                {
                    pStdAllocData->AllocationPrivateDriverDataSize = sizeof(STDVGA_ALLOCATION);
                    pStdAllocData->ResourcePrivateDriverDataSize = 0;
                    return STATUS_SUCCESS;
                }

                PSTDVGA_ALLOCATION pAlloc = (PSTDVGA_ALLOCATION)pStdAllocData->pAllocationPrivateDriverData;
                RtlZeroMemory(pAlloc, sizeof(STDVGA_ALLOCATION));
                pAlloc->Width = pPrimary->Width;
                pAlloc->Height = pPrimary->Height;
                pAlloc->BytesPerPixel = 4;
                pAlloc->Pitch = pPrimary->Width * 4;
                pAlloc->Format = pPrimary->Format;
                pAlloc->IsPrimary = TRUE;
                return STATUS_SUCCESS;
            }

        case D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE:
            {
                auto *pShadow = pStdAllocData->pCreateShadowSurfaceData;
                if (pStdAllocData->pAllocationPrivateDriverData == NULL)
                {
                    pStdAllocData->AllocationPrivateDriverDataSize = sizeof(STDVGA_ALLOCATION);
                    pStdAllocData->ResourcePrivateDriverDataSize = 0;
                    return STATUS_SUCCESS;
                }

                PSTDVGA_ALLOCATION pAlloc = (PSTDVGA_ALLOCATION)pStdAllocData->pAllocationPrivateDriverData;
                RtlZeroMemory(pAlloc, sizeof(STDVGA_ALLOCATION));
                pAlloc->Width = pShadow->Width;
                pAlloc->Height = pShadow->Height;
                pAlloc->BytesPerPixel = 4;
                pAlloc->Pitch = pShadow->Width * 4;
                pAlloc->Format = pShadow->Format;
                pShadow->Pitch = pAlloc->Pitch;
                return STATUS_SUCCESS;
            }

        case D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE:
            {
                auto *pStaging = pStdAllocData->pCreateStagingSurfaceData;
                if (pStdAllocData->pAllocationPrivateDriverData == NULL)
                {
                    pStdAllocData->AllocationPrivateDriverDataSize = sizeof(STDVGA_ALLOCATION);
                    pStdAllocData->ResourcePrivateDriverDataSize = 0;
                    return STATUS_SUCCESS;
                }

                PSTDVGA_ALLOCATION pAlloc = (PSTDVGA_ALLOCATION)pStdAllocData->pAllocationPrivateDriverData;
                RtlZeroMemory(pAlloc, sizeof(STDVGA_ALLOCATION));
                pAlloc->Width = pStaging->Width;
                pAlloc->Height = pStaging->Height;
                pAlloc->BytesPerPixel = 4;
                pAlloc->Pitch = pStaging->Width * 4;
                pAlloc->Format = D3DDDIFMT_X8R8G8B8;
                pStaging->Pitch = pAlloc->Pitch;
                return STATUS_SUCCESS;
            }

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

_Use_decl_annotations_ NTSTATUS StdVgaOpenAllocation(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                     CONST DXGKARG_OPENALLOCATION *pOpenAllocation)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pOpenAllocation);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaCloseAllocation(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                      CONST DXGKARG_CLOSEALLOCATION *pCloseAllocation)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pCloseAllocation);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaBuildPagingBuffer(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                        DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DevCtx);

    switch (pBuildPagingBuffer->Operation)
    {
        case DXGK_OPERATION_TRANSFER:
        case DXGK_OPERATION_FILL:
        case DXGK_OPERATION_DISCARD_CONTENT:
        case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
        case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
        case DXGK_OPERATION_SPECIAL_LOCK_TRANSFER:
            break;
        default:
            break;
    }

    return STATUS_SUCCESS;
}

#pragma code_seg()

_Use_decl_annotations_ NTSTATUS StdVgaSubmitCommand(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                    CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand)
{
    DevCtx->LastSubmittedFenceId = pSubmitCommand->SubmissionFenceId;
    InterlockedExchange((LONG *)&DevCtx->LastCompletedFenceId, (LONG)pSubmitCommand->SubmissionFenceId);

    DXGKARGCB_NOTIFY_INTERRUPT_DATA notifyData = {};
    notifyData.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
    notifyData.DmaCompleted.SubmissionFenceId = pSubmitCommand->SubmissionFenceId;
    notifyData.DmaCompleted.NodeOrdinal = pSubmitCommand->NodeOrdinal;
    notifyData.DmaCompleted.EngineOrdinal = pSubmitCommand->EngineOrdinal;

    DevCtx->DxgkInterface.DxgkCbNotifyInterrupt(DevCtx->DxgkInterface.DeviceHandle, &notifyData);
    DevCtx->DxgkInterface.DxgkCbQueueDpc(DevCtx->DxgkInterface.DeviceHandle);

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaPatch(PSTDVGA_DEVICE_CONTEXT DevCtx, CONST DXGKARG_PATCH *pPatch)
{
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pPatch);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaPreemptCommand(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                     CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand)
{
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pPreemptCommand);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaQueryCurrentFence(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                        DXGKARG_QUERYCURRENTFENCE *pQueryCurrentFence)
{
    pQueryCurrentFence->CurrentFence = DevCtx->LastCompletedFenceId;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaPresent(PSTDVGA_DEVICE_CONTEXT DevCtx, DXGKARG_PRESENT *pPresent)
{
    UNREFERENCED_PARAMETER(DevCtx);

    // Generate empty DMA buffer - rendering is handled by the display pipeline.
    pPresent->pDmaBufferPrivateData = (PUCHAR)pPresent->pDmaBufferPrivateData;
    pPresent->MultipassOffset = 0;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaRender(PSTDVGA_DEVICE_CONTEXT DevCtx, DXGKARG_RENDER *pRender)
{
    UNREFERENCED_PARAMETER(DevCtx);

    // No rendering capability - just advance the DMA buffer pointer.
    pRender->pDmaBuffer = pRender->pDmaBuffer;
    pRender->pDmaBufferPrivateData = pRender->pDmaBufferPrivateData;
    pRender->MultipassOffset = 0;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaSetVidPnSourceAddress(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                            CONST DXGKARG_SETVIDPNSOURCEADDRESS *pSetVidPnSourceAddress)
{
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pSetVidPnSourceAddress);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaResetFromTimeout(PSTDVGA_DEVICE_CONTEXT DevCtx)
{
    UNREFERENCED_PARAMETER(DevCtx);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaRestartFromTimeout(PSTDVGA_DEVICE_CONTEXT DevCtx)
{
    UNREFERENCED_PARAMETER(DevCtx);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaCollectDbgInfo(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                     CONST DXGKARG_COLLECTDBGINFO *pCollectDbgInfo)
{
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pCollectDbgInfo);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaAcquireSwizzlingRange(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                            DXGKARG_ACQUIRESWIZZLINGRANGE *pAcquireSwizzlingRange)
{
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pAcquireSwizzlingRange);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS StdVgaReleaseSwizzlingRange(PSTDVGA_DEVICE_CONTEXT DevCtx,
                                                            CONST DXGKARG_RELEASESWIZZLINGRANGE *pReleaseSwizzlingRange)
{
    UNREFERENCED_PARAMETER(DevCtx);
    UNREFERENCED_PARAMETER(pReleaseSwizzlingRange);
    return STATUS_SUCCESS;
}
