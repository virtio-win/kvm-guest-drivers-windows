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

extern "C"
DRIVER_INITIALIZE DriverEntry;

VOID
VioGpuDodUnload(VOID);

NTSTATUS
VioGpuDodAddDevice(
    _In_ DEVICE_OBJECT* pPhysicalDeviceObject,
    _Outptr_ PVOID*  ppDeviceContext);

NTSTATUS
VioGpuDodRemoveDevice(
    _In_  VOID* pDeviceContext);

NTSTATUS
VioGpuDodStartDevice(
    _In_  VOID*              pDeviceContext,
    _In_  DXGK_START_INFO*   pDxgkStartInfo,
    _In_  DXGKRNL_INTERFACE* pDxgkInterface,
    _Out_ ULONG*             pNumberOfViews,
    _Out_ ULONG*             pNumberOfChildren);

NTSTATUS
VioGpuDodStopDevice(
    _In_  VOID* pDeviceContext);

VOID
VioGpuDodResetDevice(
    _In_  VOID* pDeviceContext);


NTSTATUS
VioGpuDodDispatchIoRequest(
    _In_  VOID*                 pDeviceContext,
    _In_  ULONG                 VidPnSourceId,
    _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket);

NTSTATUS
VioGpuDodSetPowerState(
    _In_  VOID*              pDeviceContext,
    _In_  ULONG              HardwareUid,
    _In_  DEVICE_POWER_STATE DevicePowerState,
    _In_  POWER_ACTION       ActionType);

NTSTATUS
VioGpuDodQueryChildRelations(
    _In_                             VOID*                  pDeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
    _In_                             ULONG                  ChildRelationsSize);

NTSTATUS
VioGpuDodQueryChildStatus(
    _In_    VOID*              pDeviceContext,
    _Inout_ DXGK_CHILD_STATUS* pChildStatus,
    _In_    BOOLEAN            NonDestructiveOnly);

NTSTATUS
VioGpuDodQueryDeviceDescriptor(
    _In_  VOID*                     pDeviceContext,
    _In_  ULONG                     ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor);

BOOLEAN
VioGpuDodInterruptRoutine(
    _In_  VOID* pDeviceContext,
    _In_  ULONG MessageNumber);

VOID
VioGpuDodDpcRoutine(
    _In_  VOID* pDeviceContext);

NTSTATUS
APIENTRY
VioGpuDodQueryAdapterInfo(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO*      pQueryAdapterInfo);

NTSTATUS
APIENTRY
VioGpuDodSetPointerPosition(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION*    pSetPointerPosition);

NTSTATUS
APIENTRY
VioGpuDodSetPointerShape(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE*       pSetPointerShape);

NTSTATUS
APIENTRY
VioGpuDodEscape(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_ESCAPE*                pEscape);

NTSTATUS
VioGpuDodQueryInterface(
    _In_ CONST PVOID                          pDeviceContext,
    _In_ CONST PQUERY_INTERFACE               pQueryInterface);

NTSTATUS
APIENTRY
VioGpuDodPresentDisplayOnly(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY*   pPresentDisplayOnly);

NTSTATUS
APIENTRY
VioGpuDodIsSupportedVidPn(
    _In_ CONST HANDLE                         hAdapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN*         pIsSupportedVidPn);

NTSTATUS
APIENTRY
VioGpuDodRecommendFunctionalVidPn(
    _In_ CONST HANDLE                                   hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST  pRecommendFunctionalVidPn);

NTSTATUS
APIENTRY
VioGpuDodRecommendVidPnTopology(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology);

NTSTATUS
APIENTRY
VioGpuDodRecommendMonitorModes(
    _In_ CONST HANDLE                                hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes);

NTSTATUS
APIENTRY
VioGpuDodEnumVidPnCofuncModality(
    _In_ CONST HANDLE                                  hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModality);

NTSTATUS
APIENTRY
VioGpuDodSetVidPnSourceVisibility(
    _In_ CONST HANDLE                             hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY*  pSetVidPnSourceVisibility);

NTSTATUS
APIENTRY
VioGpuDodCommitVidPn(
    _In_ CONST HANDLE                         hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN* CONST     pCommitVidPn);

NTSTATUS
APIENTRY
VioGpuDodUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE                                       hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPath);

NTSTATUS
APIENTRY
VioGpuDodQueryVidPnHWCapability(
    _In_ CONST HANDLE                         hAdapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY*   pVidPnHWCaps);

NTSTATUS
APIENTRY
VioGpuDodStopDeviceAndReleasePostDisplayOwnership(
    _In_  VOID*                          pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo);

NTSTATUS
APIENTRY
VioGpuDodSystemDisplayEnable(
    _In_  VOID* pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat);

VOID
APIENTRY
VioGpuDodSystemDisplayWrite(
    _In_  VOID* pDeviceContext,
    _In_  VOID* Source,
    _In_  UINT  SourceWidth,
    _In_  UINT  SourceHeight,
    _In_  UINT  SourceStride,
    _In_  UINT  PositionX,
    _In_  UINT  PositionY);

